#pragma once

/**
 * @file src/media/SwHevcBitstream.h
 * @ingroup media
 * @brief Helpers for parsing HEVC Annex-B elementary streams.
 */

#include "core/types/SwByteArray.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

struct SwHevcAnnexBNalUnitView {
    const uint8_t* data{nullptr};
    std::size_t size{0};
    std::size_t startCodeOffset{0};
    std::size_t startCodeSize{0};
    uint8_t nalType{0};

    bool isValid() const {
        return data != nullptr && size > 0;
    }
};

inline bool swFindAnnexBStartCode(const uint8_t* data,
                                  std::size_t size,
                                  std::size_t from,
                                  std::size_t& start,
                                  std::size_t& prefixLen) {
    if (!data || from >= size) {
        return false;
    }
    for (std::size_t i = from; i + 2 < size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0) {
            continue;
        }
        if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
            start = i;
            prefixLen = 4;
            return true;
        }
        if (data[i + 2] == 1) {
            start = i;
            prefixLen = 3;
            return true;
        }
    }
    return false;
}

inline bool swForEachHevcAnnexBNalUnit(const uint8_t* data,
                                       std::size_t size,
                                       const std::function<bool(
                                           const SwHevcAnnexBNalUnitView&)>& visitor) {
    if (!data || size == 0 || !visitor) {
        return false;
    }
    std::size_t start = 0;
    std::size_t prefixLen = 0;
    std::size_t searchFrom = 0;
    bool foundAny = false;
    while (swFindAnnexBStartCode(data, size, searchFrom, start, prefixLen)) {
        const std::size_t nalStart = start + prefixLen;
        std::size_t nextStart = size;
        std::size_t nextPrefixLen = 0;
        if (!swFindAnnexBStartCode(data, size, nalStart, nextStart, nextPrefixLen)) {
            nextStart = size;
        }
        if (nalStart < nextStart) {
            SwHevcAnnexBNalUnitView nal;
            nal.data = data + nalStart;
            nal.size = nextStart - nalStart;
            nal.startCodeOffset = start;
            nal.startCodeSize = prefixLen;
            nal.nalType = static_cast<uint8_t>((nal.data[0] >> 1) & 0x3Fu);
            foundAny = true;
            if (!visitor(nal)) {
                return true;
            }
        }
        if (nextStart >= size) {
            break;
        }
        searchFrom = nextStart;
    }
    return foundAny;
}

class SwHevcBitReader {
public:
    SwHevcBitReader(const uint8_t* data, std::size_t size)
        : m_data(data), m_sizeBits(size * 8u) {}

    bool readBit(uint32_t& bit) {
        if (!m_data || m_bitPos >= m_sizeBits) {
            return false;
        }
        const std::size_t byteIndex = m_bitPos / 8u;
        const std::size_t bitIndex = 7u - (m_bitPos % 8u);
        bit = static_cast<uint32_t>((m_data[byteIndex] >> bitIndex) & 0x01u);
        ++m_bitPos;
        return true;
    }

    bool readBits(std::size_t count, uint32_t& value) {
        if (count > 32u) {
            return false;
        }
        value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            uint32_t bit = 0;
            if (!readBit(bit)) {
                return false;
            }
            value = (value << 1u) | bit;
        }
        return true;
    }

    bool skipBits(std::size_t count) {
        if (!m_data || (m_bitPos + count) > m_sizeBits) {
            return false;
        }
        m_bitPos += count;
        return true;
    }

    bool readUE(uint32_t& value) {
        std::size_t leadingZeroBits = 0;
        uint32_t bit = 0;
        while (true) {
            if (!readBit(bit)) {
                return false;
            }
            if (bit != 0) {
                break;
            }
            ++leadingZeroBits;
            if (leadingZeroBits > 31u) {
                return false;
            }
        }
        if (leadingZeroBits == 0u) {
            value = 0;
            return true;
        }
        uint32_t suffix = 0;
        if (!readBits(leadingZeroBits, suffix)) {
            return false;
        }
        value = ((1u << leadingZeroBits) - 1u) + suffix;
        return true;
    }

    bool readSE(int32_t& value) {
        uint32_t ue = 0;
        if (!readUE(ue)) {
            return false;
        }
        value = (ue & 1u) ? static_cast<int32_t>((ue + 1u) / 2u)
                          : -static_cast<int32_t>(ue / 2u);
        return true;
    }

private:
    const uint8_t* m_data{nullptr};
    std::size_t m_sizeBits{0};
    std::size_t m_bitPos{0};
};

inline SwByteArray swHevcAnnexBToRbsp(const uint8_t* nalData,
                                      std::size_t nalSize,
                                      std::size_t nalHeaderBytes = 2u) {
    SwByteArray rbsp;
    if (!nalData || nalSize <= nalHeaderBytes) {
        return rbsp;
    }
    rbsp.reserve(static_cast<int>(nalSize - nalHeaderBytes));
    int zeroCount = 0;
    for (std::size_t i = nalHeaderBytes; i < nalSize; ++i) {
        const uint8_t byte = nalData[i];
        if (zeroCount >= 2 && byte == 0x03u) {
            zeroCount = 0;
            continue;
        }
        rbsp.append(static_cast<char>(byte));
        if (byte == 0x00u) {
            ++zeroCount;
        } else {
            zeroCount = 0;
        }
    }
    return rbsp;
}

inline bool swSkipHevcProfileTierLevel(SwHevcBitReader& reader, uint32_t maxSubLayersMinus1) {
    if (!reader.skipBits(2u + 1u + 5u + 32u + 48u + 8u)) {
        return false;
    }

    bool subLayerProfilePresent[8] = {};
    bool subLayerLevelPresent[8] = {};
    for (uint32_t i = 0; i < maxSubLayersMinus1; ++i) {
        uint32_t flag = 0;
        if (!reader.readBits(1u, flag)) {
            return false;
        }
        subLayerProfilePresent[i] = (flag != 0);
        if (!reader.readBits(1u, flag)) {
            return false;
        }
        subLayerLevelPresent[i] = (flag != 0);
    }
    if (maxSubLayersMinus1 > 0u &&
        !reader.skipBits(static_cast<std::size_t>(2u * (8u - maxSubLayersMinus1)))) {
        return false;
    }
    for (uint32_t i = 0; i < maxSubLayersMinus1; ++i) {
        if (subLayerProfilePresent[i] &&
            !reader.skipBits(2u + 1u + 5u + 32u + 48u)) {
            return false;
        }
        if (subLayerLevelPresent[i] && !reader.skipBits(8u)) {
            return false;
        }
    }
    return true;
}

inline bool swParseHevcSpsDimensions(const uint8_t* nalData,
                                     std::size_t nalSize,
                                     int& width,
                                     int& height) {
    width = 0;
    height = 0;
    if (!nalData || nalSize <= 2u) {
        return false;
    }

    const SwByteArray rbsp = swHevcAnnexBToRbsp(nalData, nalSize, 2u);
    if (rbsp.isEmpty()) {
        return false;
    }

    SwHevcBitReader reader(reinterpret_cast<const uint8_t*>(rbsp.constData()),
                           static_cast<std::size_t>(rbsp.size()));
    uint32_t value = 0;
    uint32_t maxSubLayersMinus1 = 0;
    if (!reader.readBits(4u, value) ||
        !reader.readBits(3u, maxSubLayersMinus1) ||
        !reader.readBits(1u, value) ||
        !swSkipHevcProfileTierLevel(reader, maxSubLayersMinus1) ||
        !reader.readUE(value)) {
        return false;
    }

    uint32_t chromaFormatIdc = 1;
    if (!reader.readUE(chromaFormatIdc)) {
        return false;
    }
    if (chromaFormatIdc == 3u && !reader.readBits(1u, value)) {
        return false;
    }

    uint32_t codedWidth = 0;
    uint32_t codedHeight = 0;
    if (!reader.readUE(codedWidth) || !reader.readUE(codedHeight)) {
        return false;
    }

    uint32_t conformanceWindowFlag = 0;
    if (!reader.readBits(1u, conformanceWindowFlag)) {
        return false;
    }

    uint32_t confLeft = 0;
    uint32_t confRight = 0;
    uint32_t confTop = 0;
    uint32_t confBottom = 0;
    if (conformanceWindowFlag != 0u) {
        if (!reader.readUE(confLeft) ||
            !reader.readUE(confRight) ||
            !reader.readUE(confTop) ||
            !reader.readUE(confBottom)) {
            return false;
        }
    }

    uint32_t subWidthC = 1;
    uint32_t subHeightC = 1;
    if (chromaFormatIdc == 1u) {
        subWidthC = 2;
        subHeightC = 2;
    } else if (chromaFormatIdc == 2u) {
        subWidthC = 2;
        subHeightC = 1;
    }

    const uint32_t cropWidth = subWidthC * (confLeft + confRight);
    const uint32_t cropHeight = subHeightC * (confTop + confBottom);
    if (codedWidth <= cropWidth || codedHeight <= cropHeight) {
        return false;
    }

    width = static_cast<int>(codedWidth - cropWidth);
    height = static_cast<int>(codedHeight - cropHeight);
    return width > 0 && height > 0;
}

struct SwHevcSequenceHeaderInfo {
    SwByteArray annexB;
    int width{0};
    int height{0};
    bool hasVps{false};
    bool hasSps{false};
    bool hasPps{false};

    bool isComplete() const {
        return hasVps && hasSps && hasPps;
    }
};

struct SwHevcSpsInfo {
    bool valid{false};
    int width{0};
    int height{0};
    uint32_t sps_max_dec_pic_buffering_minus1{0};
    uint32_t bit_depth_luma_minus8{0};
    uint32_t bit_depth_chroma_minus8{0};
    uint32_t log2_min_luma_coding_block_size_minus3{0};
    uint32_t log2_diff_max_min_luma_coding_block_size{0};
    uint32_t log2_min_transform_block_size_minus2{0};
    uint32_t log2_diff_max_min_transform_block_size{0};
    uint32_t pcm_enabled_flag{0};
    uint32_t pcm_sample_bit_depth_luma_minus1{0};
    uint32_t pcm_sample_bit_depth_chroma_minus1{0};
    uint32_t log2_min_pcm_luma_coding_block_size_minus3{0};
    uint32_t log2_diff_max_min_pcm_luma_coding_block_size{0};
    uint32_t pcm_loop_filter_disabled_flag{0};
    uint32_t max_transform_hierarchy_depth_intra{0};
    uint32_t max_transform_hierarchy_depth_inter{0};
    uint32_t amp_enabled_flag{0};
    uint32_t sample_adaptive_offset_enabled_flag{0};
    uint32_t scaling_list_enabled_flag{0};
    uint32_t strong_intra_smoothing_enabled_flag{0};
    uint32_t separate_colour_plane_flag{0};
    uint32_t chroma_format_idc{1};
    uint32_t long_term_ref_pics_present_flag{0};
    uint32_t sps_temporal_mvp_enabled_flag{0};
    uint32_t log2_max_pic_order_cnt_lsb_minus4{0};
    uint32_t num_short_term_ref_pic_sets{0};
    uint32_t num_long_term_ref_pic_sps{0};
    uint32_t vui_parameters_present_flag{0};
};

struct SwHevcPpsInfo {
    bool valid{false};
    uint32_t dependent_slice_segments_enabled_flag{0};
    uint32_t output_flag_present_flag{0};
    uint32_t num_extra_slice_header_bits{0};
    uint32_t sign_data_hiding_enabled_flag{0};
    uint32_t cabac_init_present_flag{0};
    uint32_t num_ref_idx_l0_default_active_minus1{0};
    uint32_t num_ref_idx_l1_default_active_minus1{0};
    int32_t init_qp_minus26{0};
    uint32_t constrained_intra_pred_flag{0};
    uint32_t transform_skip_enabled_flag{0};
    uint32_t cu_qp_delta_enabled_flag{0};
    uint32_t diff_cu_qp_delta_depth{0};
    int32_t pps_cb_qp_offset{0};
    int32_t pps_cr_qp_offset{0};
    uint32_t pps_slice_chroma_qp_offsets_present_flag{0};
    uint32_t weighted_pred_flag{0};
    uint32_t weighted_bipred_flag{0};
    uint32_t transquant_bypass_enabled_flag{0};
    uint32_t tiles_enabled_flag{0};
    uint32_t entropy_coding_sync_enabled_flag{0};
    uint32_t num_tile_columns_minus1{0};
    uint32_t num_tile_rows_minus1{0};
    uint32_t uniform_spacing_flag{1};
    uint16_t column_width_minus1[19] = {};
    uint16_t row_height_minus1[21] = {};
    uint32_t loop_filter_across_tiles_enabled_flag{0};
    uint32_t pps_loop_filter_across_slices_enabled_flag{0};
    uint32_t deblocking_filter_control_present_flag{0};
    uint32_t deblocking_filter_override_enabled_flag{0};
    uint32_t pps_disable_deblocking_filter_flag{0};
    int32_t pps_beta_offset_div2{0};
    int32_t pps_tc_offset_div2{0};
    uint32_t lists_modification_present_flag{0};
    uint32_t log2_parallel_merge_level_minus2{0};
    uint32_t slice_segment_header_extension_present_flag{0};
};

struct SwHevcAccessUnitInfo {
    std::vector<SwHevcAnnexBNalUnitView> nalUnits;
    std::size_t firstSliceIndex{static_cast<std::size_t>(-1)};
    bool hasVps{false};
    bool hasSps{false};
    bool hasPps{false};
    bool hasSlice{false};
    bool isRap{false};
    bool isIdr{false};

    bool hasParameterSets() const {
        return hasVps && hasSps && hasPps;
    }
};

inline SwHevcSequenceHeaderInfo swCollectHevcSequenceHeaderInfo(const SwByteArray& payload) {
    SwHevcSequenceHeaderInfo info;
    if (payload.isEmpty()) {
        return info;
    }

    static const char kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    const std::size_t size = static_cast<std::size_t>(payload.size());
    (void)swForEachHevcAnnexBNalUnit(data, size, [&](const SwHevcAnnexBNalUnitView& nal) {
        const bool wantNal = (nal.nalType == 32u && !info.hasVps) ||
                             (nal.nalType == 33u && !info.hasSps) ||
                             (nal.nalType == 34u && !info.hasPps);
        if (!wantNal) {
            return true;
        }
        info.annexB.append(kStartCode, sizeof(kStartCode));
        info.annexB.append(reinterpret_cast<const char*>(nal.data), static_cast<int>(nal.size));
        if (nal.nalType == 32u) {
            info.hasVps = true;
        } else if (nal.nalType == 33u) {
            info.hasSps = true;
            (void)swParseHevcSpsDimensions(nal.data, nal.size, info.width, info.height);
        } else if (nal.nalType == 34u) {
            info.hasPps = true;
        }
        return true;
    });
    return info;
}

inline bool swSkipHevcScalingListData(SwHevcBitReader& reader) {
    for (int sizeId = 0; sizeId < 4; ++sizeId) {
        const int matrixCount = (sizeId == 3) ? 2 : 6;
        for (int matrixId = 0; matrixId < matrixCount; ++matrixId) {
            uint32_t predModeFlag = 0;
            if (!reader.readBits(1u, predModeFlag)) {
                return false;
            }
            if (predModeFlag == 0u) {
                uint32_t value = 0;
                if (!reader.readUE(value)) {
                    return false;
                }
            } else {
                if (sizeId > 1) {
                    int32_t dcCoef = 0;
                    if (!reader.readSE(dcCoef)) {
                        return false;
                    }
                }
                const int coefNum = std::min(64, 1 << (4 + (sizeId << 1)));
                for (int i = 0; i < coefNum; ++i) {
                    int32_t deltaCoef = 0;
                    if (!reader.readSE(deltaCoef)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

inline bool swSkipHevcShortTermRefPicSet(SwHevcBitReader& reader,
                                         uint32_t stRpsIdx,
                                         uint32_t numShortTermRefPicSets) {
    uint32_t interRefPicSetPredictionFlag = 0;
    if (stRpsIdx != 0u) {
        if (!reader.readBits(1u, interRefPicSetPredictionFlag)) {
            return false;
        }
    }
    if (interRefPicSetPredictionFlag != 0u) {
        if (stRpsIdx == numShortTermRefPicSets) {
            uint32_t value = 0;
            if (!reader.readUE(value)) {
                return false;
            }
        }
        uint32_t value = 0;
        if (!reader.readBits(1u, value) || !reader.readUE(value)) {
            return false;
        }
        const uint32_t refCount = 16u;
        for (uint32_t j = 0; j <= refCount; ++j) {
            if (!reader.readBits(1u, value)) {
                return false;
            }
            if (value == 0u && !reader.readBits(1u, value)) {
                return false;
            }
        }
        return true;
    }

    uint32_t numNegativePics = 0;
    uint32_t numPositivePics = 0;
    if (!reader.readUE(numNegativePics) || !reader.readUE(numPositivePics)) {
        return false;
    }
    for (uint32_t i = 0; i < numNegativePics; ++i) {
        uint32_t value = 0;
        if (!reader.readUE(value) || !reader.readBits(1u, value)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < numPositivePics; ++i) {
        uint32_t value = 0;
        if (!reader.readUE(value) || !reader.readBits(1u, value)) {
            return false;
        }
    }
    return true;
}

inline bool swParseHevcSps(const uint8_t* nalData, std::size_t nalSize, SwHevcSpsInfo& info) {
    info = SwHevcSpsInfo();
    if (!nalData || nalSize <= 2u) {
        return false;
    }

    const SwByteArray rbsp = swHevcAnnexBToRbsp(nalData, nalSize, 2u);
    if (rbsp.isEmpty()) {
        return false;
    }

    SwHevcBitReader reader(reinterpret_cast<const uint8_t*>(rbsp.constData()),
                           static_cast<std::size_t>(rbsp.size()));
    uint32_t value = 0;
    uint32_t maxSubLayersMinus1 = 0;
    if (!reader.readBits(4u, value) ||
        !reader.readBits(3u, maxSubLayersMinus1) ||
        !reader.readBits(1u, value) ||
        !swSkipHevcProfileTierLevel(reader, maxSubLayersMinus1) ||
        !reader.readUE(value)) {
        return false;
    }

    info.chroma_format_idc = 1;
    if (!reader.readUE(info.chroma_format_idc)) {
        return false;
    }
    if (info.chroma_format_idc == 3u &&
        !reader.readBits(1u, info.separate_colour_plane_flag)) {
        return false;
    }

    uint32_t codedWidth = 0;
    uint32_t codedHeight = 0;
    if (!reader.readUE(codedWidth) || !reader.readUE(codedHeight)) {
        return false;
    }

    uint32_t conformanceWindowFlag = 0;
    if (!reader.readBits(1u, conformanceWindowFlag)) {
        return false;
    }
    uint32_t confLeft = 0;
    uint32_t confRight = 0;
    uint32_t confTop = 0;
    uint32_t confBottom = 0;
    if (conformanceWindowFlag != 0u) {
        if (!reader.readUE(confLeft) ||
            !reader.readUE(confRight) ||
            !reader.readUE(confTop) ||
            !reader.readUE(confBottom)) {
            return false;
        }
    }
    uint32_t subWidthC = 1;
    uint32_t subHeightC = 1;
    if (info.chroma_format_idc == 1u) {
        subWidthC = 2;
        subHeightC = 2;
    } else if (info.chroma_format_idc == 2u) {
        subWidthC = 2;
        subHeightC = 1;
    }
    info.width = static_cast<int>(codedWidth - subWidthC * (confLeft + confRight));
    info.height = static_cast<int>(codedHeight - subHeightC * (confTop + confBottom));

    if (!reader.readUE(info.bit_depth_luma_minus8) ||
        !reader.readUE(info.bit_depth_chroma_minus8) ||
        !reader.readUE(info.log2_max_pic_order_cnt_lsb_minus4)) {
        return false;
    }

    uint32_t subLayerOrderingInfoPresentFlag = 0;
    if (!reader.readBits(1u, subLayerOrderingInfoPresentFlag)) {
        return false;
    }
    const uint32_t startLayer = (subLayerOrderingInfoPresentFlag != 0u) ? 0u : maxSubLayersMinus1;
    for (uint32_t i = startLayer; i <= maxSubLayersMinus1; ++i) {
        if (!reader.readUE(info.sps_max_dec_pic_buffering_minus1) ||
            !reader.readUE(value) ||
            !reader.readUE(value)) {
            return false;
        }
    }

    if (!reader.readUE(info.log2_min_luma_coding_block_size_minus3) ||
        !reader.readUE(info.log2_diff_max_min_luma_coding_block_size) ||
        !reader.readUE(info.log2_min_transform_block_size_minus2) ||
        !reader.readUE(info.log2_diff_max_min_transform_block_size) ||
        !reader.readUE(info.max_transform_hierarchy_depth_inter) ||
        !reader.readUE(info.max_transform_hierarchy_depth_intra)) {
        return false;
    }

    if (!reader.readBits(1u, info.scaling_list_enabled_flag)) {
        return false;
    }
    if (info.scaling_list_enabled_flag != 0u) {
        uint32_t spsScalingListDataPresentFlag = 0;
        if (!reader.readBits(1u, spsScalingListDataPresentFlag)) {
            return false;
        }
        if (spsScalingListDataPresentFlag != 0u && !swSkipHevcScalingListData(reader)) {
            return false;
        }
    }

    if (!reader.readBits(1u, info.amp_enabled_flag) ||
        !reader.readBits(1u, info.sample_adaptive_offset_enabled_flag) ||
        !reader.readBits(1u, info.pcm_enabled_flag)) {
        return false;
    }
    if (info.pcm_enabled_flag != 0u) {
        if (!reader.readBits(4u, info.pcm_sample_bit_depth_luma_minus1) ||
            !reader.readBits(4u, info.pcm_sample_bit_depth_chroma_minus1) ||
            !reader.readUE(info.log2_min_pcm_luma_coding_block_size_minus3) ||
            !reader.readUE(info.log2_diff_max_min_pcm_luma_coding_block_size) ||
            !reader.readBits(1u, info.pcm_loop_filter_disabled_flag)) {
            return false;
        }
    }

    if (!reader.readUE(info.num_short_term_ref_pic_sets)) {
        return false;
    }
    for (uint32_t i = 0; i < info.num_short_term_ref_pic_sets; ++i) {
        if (!swSkipHevcShortTermRefPicSet(reader, i, info.num_short_term_ref_pic_sets)) {
            return false;
        }
    }

    if (!reader.readBits(1u, info.long_term_ref_pics_present_flag)) {
        return false;
    }
    if (info.long_term_ref_pics_present_flag != 0u) {
        if (!reader.readUE(info.num_long_term_ref_pic_sps)) {
            return false;
        }
        for (uint32_t i = 0; i < info.num_long_term_ref_pic_sps; ++i) {
            if (!reader.skipBits(info.log2_max_pic_order_cnt_lsb_minus4 + 4u) ||
                !reader.readBits(1u, value)) {
                return false;
            }
        }
    }

    if (!reader.readBits(1u, info.sps_temporal_mvp_enabled_flag) ||
        !reader.readBits(1u, info.strong_intra_smoothing_enabled_flag) ||
        !reader.readBits(1u, info.vui_parameters_present_flag)) {
        return false;
    }
    info.valid = info.width > 0 && info.height > 0;
    return info.valid;
}

inline bool swParseHevcPps(const uint8_t* nalData, std::size_t nalSize, SwHevcPpsInfo& info) {
    info = SwHevcPpsInfo();
    if (!nalData || nalSize <= 2u) {
        return false;
    }

    const SwByteArray rbsp = swHevcAnnexBToRbsp(nalData, nalSize, 2u);
    if (rbsp.isEmpty()) {
        return false;
    }

    SwHevcBitReader reader(reinterpret_cast<const uint8_t*>(rbsp.constData()),
                           static_cast<std::size_t>(rbsp.size()));
    uint32_t value = 0;
    if (!reader.readUE(value) || !reader.readUE(value) ||
        !reader.readBits(1u, info.dependent_slice_segments_enabled_flag) ||
        !reader.readBits(1u, info.output_flag_present_flag) ||
        !reader.readBits(3u, info.num_extra_slice_header_bits) ||
        !reader.readBits(1u, info.sign_data_hiding_enabled_flag) ||
        !reader.readBits(1u, info.cabac_init_present_flag) ||
        !reader.readUE(info.num_ref_idx_l0_default_active_minus1) ||
        !reader.readUE(info.num_ref_idx_l1_default_active_minus1) ||
        !reader.readSE(info.init_qp_minus26) ||
        !reader.readBits(1u, info.constrained_intra_pred_flag) ||
        !reader.readBits(1u, info.transform_skip_enabled_flag) ||
        !reader.readBits(1u, info.cu_qp_delta_enabled_flag)) {
        return false;
    }
    if (info.cu_qp_delta_enabled_flag != 0u &&
        !reader.readUE(info.diff_cu_qp_delta_depth)) {
        return false;
    }
    if (!reader.readSE(info.pps_cb_qp_offset) ||
        !reader.readSE(info.pps_cr_qp_offset) ||
        !reader.readBits(1u, info.pps_slice_chroma_qp_offsets_present_flag) ||
        !reader.readBits(1u, info.weighted_pred_flag) ||
        !reader.readBits(1u, info.weighted_bipred_flag) ||
        !reader.readBits(1u, info.transquant_bypass_enabled_flag) ||
        !reader.readBits(1u, info.tiles_enabled_flag) ||
        !reader.readBits(1u, info.entropy_coding_sync_enabled_flag)) {
        return false;
    }

    if (info.tiles_enabled_flag != 0u) {
        if (!reader.readUE(info.num_tile_columns_minus1) ||
            !reader.readUE(info.num_tile_rows_minus1) ||
            !reader.readBits(1u, info.uniform_spacing_flag)) {
            return false;
        }
        if (info.uniform_spacing_flag == 0u) {
            for (uint32_t i = 0; i < info.num_tile_columns_minus1 && i < 19u; ++i) {
                uint32_t widthMinus1 = 0;
                if (!reader.readUE(widthMinus1)) {
                    return false;
                }
                info.column_width_minus1[i] = static_cast<uint16_t>(widthMinus1);
            }
            for (uint32_t i = 0; i < info.num_tile_rows_minus1 && i < 21u; ++i) {
                uint32_t heightMinus1 = 0;
                if (!reader.readUE(heightMinus1)) {
                    return false;
                }
                info.row_height_minus1[i] = static_cast<uint16_t>(heightMinus1);
            }
        }
        if (!reader.readBits(1u, info.loop_filter_across_tiles_enabled_flag)) {
            return false;
        }
    }

    if (!reader.readBits(1u, info.pps_loop_filter_across_slices_enabled_flag) ||
        !reader.readBits(1u, info.deblocking_filter_control_present_flag)) {
        return false;
    }
    if (info.deblocking_filter_control_present_flag != 0u) {
        if (!reader.readBits(1u, info.deblocking_filter_override_enabled_flag) ||
            !reader.readBits(1u, info.pps_disable_deblocking_filter_flag)) {
            return false;
        }
        if (info.pps_disable_deblocking_filter_flag == 0u &&
            (!reader.readSE(info.pps_beta_offset_div2) ||
             !reader.readSE(info.pps_tc_offset_div2))) {
            return false;
        }
    }

    if (!reader.readBits(1u, info.lists_modification_present_flag) ||
        !reader.readUE(info.log2_parallel_merge_level_minus2) ||
        !reader.readBits(1u, info.slice_segment_header_extension_present_flag)) {
        return false;
    }
    info.valid = true;
    return true;
}

inline SwHevcAccessUnitInfo swParseHevcAccessUnit(const SwByteArray& payload) {
    SwHevcAccessUnitInfo info;
    if (payload.isEmpty()) {
        return info;
    }
    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    const std::size_t size = static_cast<std::size_t>(payload.size());
    (void)swForEachHevcAnnexBNalUnit(data, size, [&](const SwHevcAnnexBNalUnitView& nal) {
        if (!nal.isValid()) {
            return true;
        }
        info.nalUnits.push_back(nal);
        if (nal.nalType == 32u) {
            info.hasVps = true;
        } else if (nal.nalType == 33u) {
            info.hasSps = true;
        } else if (nal.nalType == 34u) {
            info.hasPps = true;
        } else if (nal.nalType <= 31u) {
            if (!info.hasSlice) {
                info.firstSliceIndex = info.nalUnits.size() - 1u;
            }
            info.hasSlice = true;
            if (nal.nalType >= 16u && nal.nalType <= 21u) {
                info.isRap = true;
            }
            if (nal.nalType == 19u || nal.nalType == 20u) {
                info.isIdr = true;
            }
        }
        return true;
    });
    return info;
}
