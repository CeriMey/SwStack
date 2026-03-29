/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/**
 * @file src/media/SwMediaFoundationVideoDecoder.h
 * @ingroup media
 * @brief Declares the Media Foundation video decoder implementations exposed by the CoreSw
 * media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the Media Foundation video decoder interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are SwMediaFoundationDecoderBase,
 * SwMediaFoundationH264Decoder, SwMediaFoundationH265Decoder, and SwMediaFoundationAv1Decoder.
 *
 * Decoder-oriented declarations here establish the boundary between encoded input and decoded
 * output, including the format assumptions and ownership expectations that surround that
 * conversion.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


/***************************************************************************************************
 * Windows-only H.26x decoder(s) using Media Foundation MFTs.
 * Converts to BGRA32 SwVideoFrames and emits via SwVideoDecoder interface.
 **************************************************************************************************/

/**
 * @file
 * @brief Declares Windows Media Foundation video decoders exposed through the SwVideoDecoder API.
 *
 * This header adapts Media Foundation transforms (MFTs) to the generic video
 * decoder contract used by the rest of the stack. The implementation accepts
 * compressed packets such as H.264 or H.265, configures the appropriate MFT,
 * drains decoded output, and publishes frames as BGRA32 SwVideoFrame objects.
 */

#include "media/SwVideoDecoder.h"
#include "media/SwVideoPacket.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
static constexpr const char* kSwLogCategory_SwMediaFoundationH264Decoder = "sw.media.swmediafoundationh264decoder";


#if defined(_WIN32)
#include "platform/win/SwD3D11VideoInterop.h"
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <combaseapi.h>
#include <wmcodecdsp.h>



/**
 * @brief Shared Media Foundation decoder implementation used by codec-specific wrappers.
 *
 * The base class owns COM and Media Foundation initialization, transform
 * creation, input submission, output draining, and lifecycle control. Derived
 * classes only provide codec-specific details such as the target input subtype
 * and decoder creation strategy.
 */
class SwMediaFoundationDecoderBase : public SwVideoDecoder {
public:
    enum class DecoderMode {
        Auto,
        HardwareOnly,
        SoftwareOnly
    };

    /**
     * @brief Constructs a `SwMediaFoundationDecoderBase` instance.
     * @param codec Value passed to the method.
     * @param decoderName Value passed to the method.
     * @param decoderMode Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMediaFoundationDecoderBase(SwVideoPacket::Codec codec,
                                 const char* decoderName,
                                 DecoderMode decoderMode = DecoderMode::Auto)
        : m_targetCodec(codec), m_name(decoderName), m_decoderMode(decoderMode) {}
    /**
     * @brief Destroys the `SwMediaFoundationDecoderBase` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwMediaFoundationDecoderBase() override {
        shutdown();
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return m_name; }

    /**
     * @brief Opens the underlying resource managed by the object.
     * @param expectedFormat Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool open(const SwVideoFormatInfo& expectedFormat) override {
        (void)expectedFormat;
        return ensureInitialized();
    }

    /**
     * @brief Performs the `feed` operation.
     * @param packet Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool feed(const SwVideoPacket& packet) override {
        if (packet.codec() != m_targetCodec || packet.payload().isEmpty()) {
            return false;
        }
        cacheSequenceHeader(packet);
        if (!ensureInitialized()) {
            if (!m_loggedInitializationFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                    << "[" << m_name << "] Initialization failed before feed";
            }
            return false;
        }
        m_loggedInitializationFailure.store(false);
        if (!pushInput(packet)) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] pushInput failed bytes="
                        << packet.payload().size()
                        << " key=" << (packet.isKeyFrame() ? 1 : 0);
            return false;
        }
        drainOutput();
        return true;
    }

    /**
     * @brief Performs the `flush` operation.
     */
    void flush() override {
        if (m_decoder) {
            m_decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        }
        m_nextInputDiscontinuity.store(true);
    }

protected:
    /**
     * @brief Returns the current input Subtype.
     * @return The current input Subtype.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual GUID inputSubtype() const = 0;
    /**
     * @brief Creates the requested decoder.
     * @param decoder Value passed to the method.
     * @return The resulting decoder.
     */
    virtual HRESULT createDecoder(IMFTransform** decoder) const = 0;
    /**
     * @brief Performs the `configureInputType` operation.
     * @param type Value passed to the method.
     * @return The requested configure Input Type.
     */
    virtual void configureInputType(IMFMediaType* type) const {
        (void)type;
    }

    HRESULT activateDecoderFromEnumFlags_(const GUID& subtype,
                                          IMFTransform** decoder,
                                          UINT32 enumFlags) const {
        if (!decoder) {
            return E_POINTER;
        }
        *decoder = nullptr;
        MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Video, subtype};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                               enumFlags,
                               &inputInfo,
                               nullptr,
                               &activates,
                               &count);
        if (FAILED(hr)) {
            return hr;
        }
        if (count == 0 || !activates) {
            if (activates) {
                CoTaskMemFree(activates);
            }
            return MF_E_TOPO_CODEC_NOT_FOUND;
        }
        hr = MF_E_TOPO_CODEC_NOT_FOUND;
        for (UINT32 i = 0; i < count; ++i) {
            hr = activates[i]->ActivateObject(IID_PPV_ARGS(decoder));
            if (SUCCEEDED(hr) && *decoder) {
                break;
            }
        }
        for (UINT32 i = 0; i < count; ++i) {
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
        return hr;
    }

    HRESULT createDecoderFromEnum(const GUID& subtype, IMFTransform** decoder) const {
        static constexpr UINT32 kHardwareFlags =
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_HARDWARE;
        static constexpr UINT32 kSoftwareFlags =
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
        if (m_decoderMode == DecoderMode::HardwareOnly) {
            return activateDecoderFromEnumFlags_(subtype, decoder, kHardwareFlags);
        }
        if (m_decoderMode == DecoderMode::SoftwareOnly) {
            return activateDecoderFromEnumFlags_(subtype, decoder, kSoftwareFlags);
        }
        HRESULT hr = activateDecoderFromEnumFlags_(subtype, decoder, kHardwareFlags);
        if (SUCCEEDED(hr)) {
            return hr;
        }
        return activateDecoderFromEnumFlags_(subtype, decoder, kSoftwareFlags);
    }

private:
    static bool findAnnexBStartCode(const uint8_t* data,
                                    size_t size,
                                    size_t from,
                                    size_t& start,
                                    size_t& prefixLen) {
        if (!data || from >= size) {
            return false;
        }
        for (size_t i = from; i + 2 < size; ++i) {
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

    class BitReader {
    public:
        BitReader(const uint8_t* data, size_t size)
            : m_data(data), m_sizeBits(size * 8u) {}

        bool readBit(uint32_t& bit) {
            if (!m_data || m_bitPos >= m_sizeBits) {
                return false;
            }
            const size_t byteIndex = m_bitPos / 8u;
            const size_t bitIndex = 7u - (m_bitPos % 8u);
            bit = static_cast<uint32_t>((m_data[byteIndex] >> bitIndex) & 0x01u);
            ++m_bitPos;
            return true;
        }

        bool readBits(size_t count, uint32_t& value) {
            if (count > 32u) {
                return false;
            }
            value = 0;
            for (size_t i = 0; i < count; ++i) {
                uint32_t bit = 0;
                if (!readBit(bit)) {
                    return false;
                }
                value = (value << 1u) | bit;
            }
            return true;
        }

        bool skipBits(size_t count) {
            if (!m_data || (m_bitPos + count) > m_sizeBits) {
                return false;
            }
            m_bitPos += count;
            return true;
        }

        bool readUE(uint32_t& value) {
            size_t leadingZeroBits = 0;
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

    private:
        const uint8_t* m_data{nullptr};
        size_t m_sizeBits{0};
        size_t m_bitPos{0};
    };

    static SwByteArray annexBToRbsp(const uint8_t* nalData,
                                    size_t nalSize,
                                    size_t nalHeaderBytes) {
        SwByteArray rbsp;
        if (!nalData || nalSize <= nalHeaderBytes) {
            return rbsp;
        }
        rbsp.reserve(static_cast<int>(nalSize - nalHeaderBytes));
        int zeroCount = 0;
        for (size_t i = nalHeaderBytes; i < nalSize; ++i) {
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

    static bool skipHevcProfileTierLevel(BitReader& reader, uint32_t maxSubLayersMinus1) {
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
        if (maxSubLayersMinus1 > 0u) {
            if (!reader.skipBits(static_cast<size_t>(2u * (8u - maxSubLayersMinus1)))) {
                return false;
            }
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

    static bool parseHevcSpsDimensions(const uint8_t* nalData,
                                       size_t nalSize,
                                       int& width,
                                       int& height) {
        width = 0;
        height = 0;
        if (!nalData || nalSize <= 2u) {
            return false;
        }

        const SwByteArray rbsp = annexBToRbsp(nalData, nalSize, 2u);
        if (rbsp.isEmpty()) {
            return false;
        }

        BitReader reader(reinterpret_cast<const uint8_t*>(rbsp.constData()),
                         static_cast<size_t>(rbsp.size()));
        uint32_t value = 0;
        uint32_t maxSubLayersMinus1 = 0;
        if (!reader.readBits(4u, value) ||
            !reader.readBits(3u, maxSubLayersMinus1) ||
            !reader.readBits(1u, value) ||
            !skipHevcProfileTierLevel(reader, maxSubLayersMinus1) ||
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

    void updateInputGeometryFromSequenceHeader(SwVideoPacket::Codec codec,
                                               const SwByteArray& sequenceHeader) {
        if (m_inputWidth > 0 && m_inputHeight > 0) {
            return;
        }
        if (codec != SwVideoPacket::Codec::H265 || sequenceHeader.isEmpty()) {
            return;
        }

        const uint8_t* data = reinterpret_cast<const uint8_t*>(sequenceHeader.constData());
        const size_t size = static_cast<size_t>(sequenceHeader.size());
        size_t start = 0;
        size_t prefixLen = 0;
        size_t searchFrom = 0;
        while (findAnnexBStartCode(data, size, searchFrom, start, prefixLen)) {
            const size_t nalStart = start + prefixLen;
            size_t nextStart = size;
            size_t nextPrefixLen = 0;
            if (!findAnnexBStartCode(data, size, nalStart, nextStart, nextPrefixLen)) {
                nextStart = size;
            }
            if (nalStart < nextStart) {
                const uint8_t nalType = static_cast<uint8_t>((data[nalStart] >> 1) & 0x3Fu);
                if (nalType == 33u) {
                    int width = 0;
                    int height = 0;
                    if (parseHevcSpsDimensions(data + nalStart,
                                               nextStart - nalStart,
                                               width,
                                               height)) {
                        m_inputWidth = width;
                        m_inputHeight = height;
                        swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                            << "[" << m_name << "] Parsed HEVC SPS dimensions "
                            << m_inputWidth << "x" << m_inputHeight;
                    }
                    return;
                }
            }
            if (nextStart >= size) {
                break;
            }
            searchFrom = nextStart;
        }
    }

    void cacheSequenceHeader(const SwVideoPacket& packet) {
        if (!m_sequenceHeader.isEmpty() || packet.payload().isEmpty()) {
            return;
        }
        if (packet.codec() != SwVideoPacket::Codec::H264 &&
            packet.codec() != SwVideoPacket::Codec::H265) {
            return;
        }

        const uint8_t* data = reinterpret_cast<const uint8_t*>(packet.payload().constData());
        const size_t size = packet.payload().size();
        bool foundVps = false;
        bool foundSps = false;
        bool foundPps = false;
        SwByteArray sequenceHeader;

        size_t start = 0;
        size_t prefixLen = 0;
        size_t searchFrom = 0;
        static const char kStartCode[] = {0x00, 0x00, 0x00, 0x01};
        while (findAnnexBStartCode(data, size, searchFrom, start, prefixLen)) {
            const size_t nalStart = start + prefixLen;
            size_t nextStart = size;
            size_t nextPrefixLen = 0;
            size_t candidate = nalStart;
            if (findAnnexBStartCode(data, size, candidate, nextStart, nextPrefixLen)) {
                (void)nextPrefixLen;
            } else {
                nextStart = size;
            }
            if (nalStart < nextStart) {
                const uint8_t nalType =
                    (packet.codec() == SwVideoPacket::Codec::H264)
                        ? static_cast<uint8_t>(data[nalStart] & 0x1Fu)
                        : static_cast<uint8_t>((data[nalStart] >> 1) & 0x3Fu);
                const bool wantNal =
                    (packet.codec() == SwVideoPacket::Codec::H264)
                        ? ((nalType == 7 && !foundSps) || (nalType == 8 && !foundPps))
                        : ((nalType == 32 && !foundVps) || (nalType == 33 && !foundSps) ||
                           (nalType == 34 && !foundPps));
                if (wantNal) {
                    sequenceHeader.append(kStartCode, sizeof(kStartCode));
                    sequenceHeader.append(reinterpret_cast<const char*>(data + nalStart),
                                          nextStart - nalStart);
                    if (packet.codec() == SwVideoPacket::Codec::H264) {
                        foundSps = foundSps || (nalType == 7);
                        foundPps = foundPps || (nalType == 8);
                    } else {
                        foundVps = foundVps || (nalType == 32);
                        foundSps = foundSps || (nalType == 33);
                        foundPps = foundPps || (nalType == 34);
                    }
                }
            }
            if (nextStart >= size) {
                break;
            }
            searchFrom = nextStart;
        }

        const bool haveHeaders =
            (packet.codec() == SwVideoPacket::Codec::H264) ? (foundSps && foundPps)
                                                           : (foundVps && foundSps && foundPps);
        if (!haveHeaders) {
            return;
        }
        m_sequenceHeader = std::move(sequenceHeader);
        updateInputGeometryFromSequenceHeader(packet.codec(), m_sequenceHeader);
        if (!m_loggedSequenceHeader.exchange(true)) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                << "[" << m_name << "] Cached sequence header bytes="
                << m_sequenceHeader.size();
        }
    }

    virtual int outputSubtypeRank(const GUID& subtype) const {
        if (m_hardwareInteropActive) {
            if (subtype == MFVideoFormat_NV12) {
                return 0;
            }
            if (subtype == MFVideoFormat_P010) {
                return 1;
            }
            if (subtype == MFVideoFormat_P016) {
                return 2;
            }
            if (subtype == MFVideoFormat_RGB32) {
                return 3;
            }
            if (subtype == MFVideoFormat_YUY2) {
                return 4;
            }
            return -1;
        }
        if (subtype == MFVideoFormat_RGB32) {
            return 0;
        }
        if (subtype == MFVideoFormat_NV12) {
            return 1;
        }
        if (subtype == MFVideoFormat_P010) {
            return 2;
        }
        if (subtype == MFVideoFormat_P016) {
            return 3;
        }
        if (subtype == MFVideoFormat_YUY2) {
            return 4;
        }
        return -1;
    }

    const char* outputSubtypeName(const GUID& subtype) const {
        if (subtype == MFVideoFormat_RGB32) {
            return "RGB32";
        }
        if (subtype == MFVideoFormat_NV12) {
            return "NV12";
        }
        if (subtype == MFVideoFormat_P010) {
            return "P010";
        }
        if (subtype == MFVideoFormat_P016) {
            return "P016";
        }
        if (subtype == MFVideoFormat_YUY2) {
            return "YUY2";
        }
        return "other";
    }

    bool ensureInitialized() {
        if (m_ready) {
            return true;
        }
        if (!m_comInit) {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                    << "[" << m_name << "] CoInitializeEx failed hr=0x"
                    << std::hex << hr << std::dec;
                return false;
            }
            m_comInit = (hr != RPC_E_CHANGED_MODE);
        }
        if (!m_mfStarted) {
            HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
            if (FAILED(hr)) {
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                    << "[" << m_name << "] MFStartup failed hr=0x"
                    << std::hex << hr << std::dec;
                return false;
            }
            m_mfStarted = true;
        }

        Microsoft::WRL::ComPtr<IMFTransform> decoder;
        HRESULT hr = createDecoder(&decoder);
        if (FAILED(hr)) {
            swCError(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] Failed to create decoder: 0x" << std::hex << hr << std::dec;
            return false;
        }
        m_decoder = decoder;
        configureLowLatencyMode();
        configureHardwareInterop();

        if (!setInputType()) {
            return false;
        }
        if (setOutputType(false)) {
            maybeStartStreaming();
        }
        m_nextInputDiscontinuity.store(true);
        m_ready = true;
        return true;
    }

    bool setInputType() {
        Microsoft::WRL::ComPtr<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                << "[" << m_name << "] MFCreateMediaType(input) failed hr=0x"
                << std::hex << hr << std::dec;
            return false;
        }
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, inputSubtype());
        type->SetUINT32(MF_MT_AVG_BITRATE, 8000000);
        if (!m_sequenceHeader.isEmpty()) {
            type->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER,
                          reinterpret_cast<const UINT8*>(m_sequenceHeader.constData()),
                          static_cast<UINT32>(m_sequenceHeader.size()));
        }
        if (m_inputWidth > 0 && m_inputHeight > 0) {
            MFSetAttributeSize(type.Get(),
                               MF_MT_FRAME_SIZE,
                               static_cast<UINT32>(m_inputWidth),
                               static_cast<UINT32>(m_inputHeight));
            type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        }
        configureInputType(type.Get());
        hr = m_decoder->SetInputType(0, type.Get(), 0);
        if (FAILED(hr)) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                << "[" << m_name << "] SetInputType failed hr=0x"
                << std::hex << hr << std::dec;
        } else {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                << "[" << m_name << "] Input type configured subtype="
                << (inputSubtype() == MFVideoFormat_HEVC ? "HEVC" :
                    inputSubtype() == MFVideoFormat_HEVC_ES ? "HEVC_ES" :
                    inputSubtype() == MFVideoFormat_H264 ? "H264" :
                    inputSubtype() == MFVideoFormat_H264_ES ? "H264_ES" : "other")
                << " width=" << m_inputWidth
                << " height=" << m_inputHeight
                << " seqhdr=" << m_sequenceHeader.size();
        }
        return SUCCEEDED(hr);
    }

    bool setOutputType(bool logOnFailure = true) {
        if (!m_decoder) {
            return false;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> bestType;
        GUID bestSubtype = GUID_NULL;
        int bestRank = (std::numeric_limits<int>::max)();
        for (UINT32 i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IMFMediaType> type;
            HRESULT hr = m_decoder->GetOutputAvailableType(0, i, &type);
            if (hr == MF_E_NO_MORE_TYPES) {
                break;
            }
            if (FAILED(hr)) {
                break;
            }

            GUID subtype = GUID_NULL;
            if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                continue;
            }

            const int rank = outputSubtypeRank(subtype);
            if (rank < 0 || rank >= bestRank) {
                continue;
            }
            bestRank = rank;
            bestSubtype = subtype;
            bestType = type;
        }

        if (!bestType) {
            if (logOnFailure && !m_loggedOutputTypeFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                    << "[" << m_name << "] No supported output type available yet";
            }
            return false;
        }

        HRESULT hr = m_decoder->SetOutputType(0, bestType.Get(), 0);
        if (FAILED(hr)) {
            if (logOnFailure && !m_loggedOutputTypeFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                    << "[" << m_name << "] Failed to negotiate output type subtype="
                    << outputSubtypeName(bestSubtype)
                    << " hr=0x" << std::hex << hr << std::dec;
            }
            return false;
        }

        UINT32 width = 0;
        UINT32 height = 0;
        if (SUCCEEDED(MFGetAttributeSize(bestType.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
            m_width = static_cast<int>(width);
            m_height = static_cast<int>(height);
        }
        m_outputSubtype = bestSubtype;
        m_outputTypeReady = true;
        m_loggedOutputTypeFailure.store(false);
        maybeStartStreaming();
        swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
            << "[" << m_name << "] Selected output type "
            << outputSubtypeName(bestSubtype)
            << " width=" << m_width
            << " height=" << m_height;
        return true;
    }

    void maybeStartStreaming(bool allowWithoutOutputType = false) {
        if (!m_decoder || m_streamingStarted) {
            return;
        }
        if (!allowWithoutOutputType && !m_outputTypeReady) {
            return;
        }
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        m_streamingStarted = true;
    }

    void configureLowLatencyMode() {
        if (!m_decoder) {
            return;
        }
        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(m_decoder->GetAttributes(&attributes)) && attributes) {
            attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        }
        Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
        if (SUCCEEDED(m_decoder.As(&codecApi)) && codecApi) {
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_UI4;
            value.ulVal = TRUE;
            codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &value);
            VariantClear(&value);
        }
    }

    void configureHardwareInterop() {
        m_hardwareInteropActive = false;
        m_dxgiDeviceManager.Reset();
        if (!m_decoder || m_decoderMode == DecoderMode::SoftwareOnly) {
            return;
        }
        SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
        if (!interop.ensure() || !interop.deviceManager()) {
            return;
        }
        HRESULT hr =
            m_decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                      reinterpret_cast<ULONG_PTR>(interop.deviceManager()));
        if (FAILED(hr)) {
            swCDebug(kSwLogCategory_SwMediaFoundationH264Decoder)
                << "[" << m_name << "] D3D manager rejected hr=0x"
                << std::hex << hr << std::dec;
            return;
        }
        m_dxgiDeviceManager = interop.deviceManager();
        m_hardwareInteropActive = true;
        swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
            << "[" << m_name << "] D3D11 video interop enabled";
    }

    bool pushInput(const SwVideoPacket& packet) {
        Microsoft::WRL::ComPtr<IMFSample> sample;
        HRESULT hr = MFCreateSample(&sample);
        if (FAILED(hr)) {
            return false;
        }
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        hr = MFCreateMemoryBuffer(static_cast<DWORD>(packet.payload().size()), &buffer);
        if (FAILED(hr)) {
            return false;
        }
        BYTE* dst = nullptr;
        DWORD maxLen = 0;
        hr = buffer->Lock(&dst, &maxLen, nullptr);
        if (FAILED(hr)) {
            return false;
        }
        const size_t toCopy = std::min(static_cast<size_t>(maxLen),
                                       static_cast<size_t>(packet.payload().size()));
        std::memcpy(dst, packet.payload().constData(), toCopy);
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(toCopy));
        sample->AddBuffer(buffer.Get());

        if (packet.pts() >= 0) {
            const LONGLONG hns = static_cast<LONGLONG>((packet.pts() * 10000000LL) / 90000LL);
            sample->SetSampleTime(hns);
            sample->SetSampleDuration(0);
        }
        if (packet.isKeyFrame()) {
            sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
        }
        if (packet.isDiscontinuity() || m_nextInputDiscontinuity.exchange(false)) {
            sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
        }

        hr = m_decoder->ProcessInput(0, sample.Get(), 0);
        if (FAILED(hr)) {
            if (hr == MF_E_NOTACCEPTING) {
                drainOutput();
                hr = m_decoder->ProcessInput(0, sample.Get(), 0);
            }
        }
        if (FAILED(hr)) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] ProcessInput failed hr=0x"
                        << std::hex << hr << std::dec;
        }
        if (SUCCEEDED(hr) && !m_outputTypeReady) {
            setOutputType(false);
            if (!m_outputTypeReady) {
                probeOutputType(false);
            }
        }
        return SUCCEEDED(hr);
    }

    bool probeOutputType(bool logOnFailure) {
        if (!m_decoder || m_outputTypeReady) {
            return m_outputTypeReady;
        }

        MFT_OUTPUT_STREAM_INFO info{};
        if (FAILED(m_decoder->GetOutputStreamInfo(0, &info))) {
            return false;
        }
        MFT_OUTPUT_DATA_BUFFER output{};
        Microsoft::WRL::ComPtr<IMFSample> probeSample;
        prepareOutputBuffer(info, output, probeSample);
        DWORD status = 0;
        HRESULT hr = m_decoder->ProcessOutput(0, 1, &output, &status);
        if (FAILED(hr) &&
            hr != MF_E_TRANSFORM_STREAM_CHANGE &&
            hr != MF_E_TRANSFORM_TYPE_NOT_SET &&
            hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
            output = {};
            probeSample.Reset();
            prepareOutputBuffer(info, output, probeSample, true);
            hr = m_decoder->ProcessOutput(0, 1, &output, &status);
        }
        if (output.pEvents) {
            output.pEvents->Release();
            output.pEvents = nullptr;
        }

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            m_outputTypeReady = false;
            m_outputSubtype = GUID_NULL;
            return setOutputType(logOnFailure);
        }
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            return setOutputType(logOnFailure);
        }
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            return false;
        }

        if (FAILED(hr)) {
            if (logOnFailure && !m_loggedOutputTypeFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder)
                    << "[" << m_name << "] Output probe failed hr=0x"
                    << std::hex << hr << std::dec;
            }
            if (output.pSample && output.pSample != probeSample.Get()) {
                output.pSample->Release();
            }
            return false;
        }

        if (output.pSample) {
            if (!m_outputTypeReady && !setOutputType(logOnFailure)) {
                if (output.pSample != probeSample.Get()) {
                    output.pSample->Release();
                }
                return false;
            }
            emitFrameFromSample(output.pSample);
            if (output.pSample != probeSample.Get()) {
                output.pSample->Release();
            }
        }
        return m_outputTypeReady;
    }

    void prepareOutputBuffer(const MFT_OUTPUT_STREAM_INFO& info,
                             MFT_OUTPUT_DATA_BUFFER& output,
                             Microsoft::WRL::ComPtr<IMFSample>& sample,
                             bool forceCpuSample = false) {
        output = {};
        sample.Reset();
        const bool transformProvidesSample =
            (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
        const bool transformCanProvideSample =
            (info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) != 0;
        if (!forceCpuSample &&
            (transformProvidesSample || (m_hardwareInteropActive && transformCanProvideSample))) {
            return;
        }
        if (info.cbSize == 0) {
            return;
        }
        if (FAILED(MFCreateSample(&sample))) {
            return;
        }
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateMemoryBuffer(info.cbSize, &buffer))) {
            sample.Reset();
            return;
        }
        sample->AddBuffer(buffer.Get());
        output.pSample = sample.Get();
    }

    void drainOutput() {
        if (!m_outputTypeReady) {
            if (!setOutputType(false) && !probeOutputType(false)) {
                return;
            }
            if (!m_outputTypeReady) {
                return;
            }
        }

        while (true) {
            MFT_OUTPUT_STREAM_INFO info{};
            HRESULT hr = m_decoder->GetOutputStreamInfo(0, &info);
            if (FAILED(hr)) {
                return;
            }

            Microsoft::WRL::ComPtr<IMFSample> sample;
            MFT_OUTPUT_DATA_BUFFER output{};
            prepareOutputBuffer(info, output, sample);
            DWORD status = 0;
            hr = m_decoder->ProcessOutput(0, 1, &output, &status);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                if (output.pEvents) {
                    output.pEvents->Release();
                    output.pEvents = nullptr;
                }
                if (output.pSample && output.pSample != sample.Get()) {
                    output.pSample->Release();
                }
                auto needMore = ++m_needMoreInputCount;
                if (needMore <= 3 || (needMore % 50) == 0) {
                    swCDebug(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] ProcessOutput needs more input count="
                              << needMore;
                }
                return;
            }
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (output.pEvents) {
                    output.pEvents->Release();
                    output.pEvents = nullptr;
                }
                if (output.pSample && output.pSample != sample.Get()) {
                    output.pSample->Release();
                }
                m_outputTypeReady = false;
                m_outputSubtype = GUID_NULL;
                setOutputType();
                continue;
            }
            if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
                if (output.pEvents) {
                    output.pEvents->Release();
                    output.pEvents = nullptr;
                }
                if (output.pSample && output.pSample != sample.Get()) {
                    output.pSample->Release();
                }
                m_outputTypeReady = false;
                m_outputSubtype = GUID_NULL;
                if (!setOutputType(false)) {
                    return;
                }
                continue;
            }
            if (FAILED(hr)) {
                if (output.pEvents) {
                    output.pEvents->Release();
                    output.pEvents = nullptr;
                }
                if (output.pSample && output.pSample != sample.Get()) {
                    output.pSample->Release();
                }
                swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] ProcessOutput failed hr=0x"
                            << std::hex << hr << std::dec;
                return;
            }
            m_needMoreInputCount.store(0);
            IMFSample* decodedSample = output.pSample ? output.pSample : sample.Get();
            emitFrameFromSample(decodedSample);
            if (output.pEvents) {
                output.pEvents->Release();
                output.pEvents = nullptr;
            }
            if (output.pSample && output.pSample != sample.Get()) {
                output.pSample->Release();
            }
        }
    }

    void emitFrameFromSample(IMFSample* sample) {
        if (!sample) {
            return;
        }
        SwVideoFrame frame;
        if (m_hardwareInteropActive) {
            frame = wrapNativeFrame(sample);
        }
        if (!frame.isValid()) {
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->GetBufferByIndex(0, &buffer))) {
                return;
            }
            if (m_outputSubtype == MFVideoFormat_NV12) {
                frame = convertNV12(buffer.Get());
            } else if (m_outputSubtype == MFVideoFormat_P010 || m_outputSubtype == MFVideoFormat_P016) {
                frame = convertP010(buffer.Get());
            } else if (m_outputSubtype == MFVideoFormat_YUY2) {
                frame = convertYUY2(buffer.Get());
            } else if (m_outputSubtype == MFVideoFormat_RGB32) {
                frame = copyBGRA(buffer.Get());
            }
        }
        if (!frame.isValid()) {
            return;
        }
        auto decoded = ++m_decodedFrameCount;
        LONGLONG ts = 0;
        if (SUCCEEDED(sample->GetSampleTime(&ts))) {
            frame.setTimestamp(static_cast<std::int64_t>(ts));
        }
        if (!m_loggedFirstOutputFrame.exchange(true)) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] First decoded frame "
                        << frame.width() << "x" << frame.height()
                        << " ts=" << frame.timestamp()
                        << " subtype=" << outputSubtypeName(m_outputSubtype);
        } else if ((decoded % 25) == 0) {
            swCWarning(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] Decoded frame count="
                        << decoded
                        << " ts=" << frame.timestamp();
        }
        emitFrame(frame);
    }

    SwVideoPixelFormat outputPixelFormatForSubtype(const GUID& subtype) const {
        if (subtype == MFVideoFormat_NV12) {
            return SwVideoPixelFormat::NV12;
        }
        if (subtype == MFVideoFormat_P010) {
            return SwVideoPixelFormat::P010;
        }
        if (subtype == MFVideoFormat_P016) {
            return SwVideoPixelFormat::P016;
        }
        if (subtype == MFVideoFormat_YUY2) {
            return SwVideoPixelFormat::YUY2;
        }
        if (subtype == MFVideoFormat_RGB32) {
            return SwVideoPixelFormat::BGRA32;
        }
        return SwVideoPixelFormat::Unknown;
    }

    SwVideoFrame wrapNativeFrame(IMFSample* sample) {
        if (!sample || !m_hardwareInteropActive) {
            return {};
        }
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->GetBufferByIndex(0, &buffer))) {
            return {};
        }
        Microsoft::WRL::ComPtr<IMFDXGIBuffer> dxgiBuffer;
        if (FAILED(buffer.As(&dxgiBuffer)) || !dxgiBuffer) {
            return {};
        }
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(dxgiBuffer->GetResource(IID_PPV_ARGS(texture.GetAddressOf()))) || !texture) {
            return {};
        }
        UINT subresourceIndex = 0;
        if (FAILED(dxgiBuffer->GetSubresourceIndex(&subresourceIndex))) {
            subresourceIndex = 0;
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        SwVideoPixelFormat pixelFormat = outputPixelFormatForSubtype(m_outputSubtype);
        if (pixelFormat == SwVideoPixelFormat::Unknown) {
            return {};
        }
        SwVideoFormatInfo info =
            SwDescribeVideoFormat(pixelFormat, m_width > 0 ? m_width : static_cast<int>(desc.Width),
                                  m_height > 0 ? m_height : static_cast<int>(desc.Height));
        if (!info.isValid()) {
            return {};
        }
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        texture->GetDevice(&device);
        SwVideoFrame frame =
            SwVideoFrame::wrapNativeD3D11(info,
                                          device.Get(),
                                          texture.Get(),
                                          desc.Format,
                                          subresourceIndex);
        frame.setAspectRatio(1.0);
        return frame;
    }

    SwVideoFrame copyBGRA(IMFMediaBuffer* buffer) {
        if (!buffer) {
            return {};
        }
        const int width = m_width > 0 ? m_width : 0;
        const int height = m_height > 0 ? m_height : 0;
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        BYTE* data = nullptr;
        LONG pitch = 0;
        DWORD curLen = 0;
        bool locked2D = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            if (SUCCEEDED(buffer2D->Lock2D(&data, &pitch))) {
                locked2D = true;
            }
        }
        DWORD maxLen = 0;
        if (!locked2D) {
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
                return {};
            }
            pitch = width * 4;
        }

        const int rowBytes = width * 4;
        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            if (locked2D) {
                buffer2D->Unlock2D();
            } else {
                buffer->Unlock();
            }
            return {};
        }
        for (int y = 0; y < height; ++y) {
            std::memcpy(frame.planeData(0) + y * frame.planeStride(0),
                        data + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(rowBytes));
        }
        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
        frame.setAspectRatio(1.0);
        return frame;
    }

    SwVideoFrame convertNV12(IMFMediaBuffer* buffer) {
        if (!buffer) {
            return {};
        }
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        BYTE* data = nullptr;
        LONG pitch = 0;
        DWORD curLen = 0;
        bool locked2D = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            if (SUCCEEDED(buffer2D->Lock2D(&data, &pitch))) {
                locked2D = true;
            }
        }
        DWORD maxLen = 0;
        if (!locked2D) {
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
                return {};
            }
            pitch = deriveNV12Stride(static_cast<int>(curLen));
        }
        const int width = m_width > 0 ? m_width : 0;
        const int height = m_height > 0 ? m_height : 0;
        const int yStride = static_cast<int>(pitch);
        const int uvStride = static_cast<int>(pitch);
        const BYTE* yPlane = data;
        const BYTE* uvPlane = data + static_cast<size_t>(yStride) * height;

        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            if (locked2D) {
                buffer2D->Unlock2D();
            } else {
                buffer->Unlock();
            }
            return {};
        }
        auto clamp = [](int v) -> uint8_t { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
        uint8_t* dstBase = frame.planeData(0);
        const int dstStride = frame.planeStride(0);
        for (int y = 0; y < height; ++y) {
            const BYTE* yRow = yPlane + static_cast<size_t>(y) * yStride;
            const BYTE* uvRow = uvPlane + static_cast<size_t>(y / 2) * uvStride;
            uint8_t* dstRow = dstBase + static_cast<size_t>(y) * dstStride;
            for (int x = 0; x < width; ++x) {
                const uint8_t Y = yRow[x];
                const int chromaIndex = (x / 2) * 2;
                const int U = static_cast<int>(uvRow[chromaIndex]) - 128;
                const int V = static_cast<int>(uvRow[chromaIndex + 1]) - 128;
                const int C = std::max(0, static_cast<int>(Y) - 16);
                int r = (298 * C + 409 * V + 128) >> 8;
                int g = (298 * C - 100 * U - 208 * V + 128) >> 8;
                int b = (298 * C + 516 * U + 128) >> 8;
                dstRow[x * 4 + 0] = clamp(b);
                dstRow[x * 4 + 1] = clamp(g);
                dstRow[x * 4 + 2] = clamp(r);
                dstRow[x * 4 + 3] = 255;
            }
        }
        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
        frame.setAspectRatio(1.0);
        return frame;
    }

    SwVideoFrame convertP010(IMFMediaBuffer* buffer) {
        if (!buffer) {
            return {};
        }
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        BYTE* data = nullptr;
        LONG pitch = 0;
        DWORD curLen = 0;
        bool locked2D = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            if (SUCCEEDED(buffer2D->Lock2D(&data, &pitch))) {
                locked2D = true;
            }
        }
        DWORD maxLen = 0;
        if (!locked2D) {
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
                return {};
            }
            pitch = deriveP010Stride(static_cast<int>(curLen));
        }
        const int width = m_width > 0 ? m_width : 0;
        const int height = m_height > 0 ? m_height : 0;
        const int yStrideBytes = static_cast<int>(pitch);
        const int uvStrideBytes = static_cast<int>(pitch);
        const uint16_t* yPlane = reinterpret_cast<const uint16_t*>(data);
        const uint16_t* uvPlane =
            reinterpret_cast<const uint16_t*>(data + static_cast<size_t>(yStrideBytes) * height);

        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            if (locked2D) {
                buffer2D->Unlock2D();
            } else {
                buffer->Unlock();
            }
            return {};
        }
        auto clamp = [](int v) -> uint8_t { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
        uint8_t* dstBase = frame.planeData(0);
        const int dstStride = frame.planeStride(0);
        const int yStrideWords = yStrideBytes / static_cast<int>(sizeof(uint16_t));
        const int uvStrideWords = uvStrideBytes / static_cast<int>(sizeof(uint16_t));
        for (int y = 0; y < height; ++y) {
            const uint16_t* yRow = yPlane + static_cast<size_t>(y) * yStrideWords;
            const uint16_t* uvRow = uvPlane + static_cast<size_t>(y / 2) * uvStrideWords;
            uint8_t* dstRow = dstBase + static_cast<size_t>(y) * dstStride;
            for (int x = 0; x < width; ++x) {
                const uint8_t Y = static_cast<uint8_t>(yRow[x] >> 8);
                const int chromaIndex = (x / 2) * 2;
                const int U = static_cast<int>(static_cast<uint8_t>(uvRow[chromaIndex] >> 8)) - 128;
                const int V = static_cast<int>(static_cast<uint8_t>(uvRow[chromaIndex + 1] >> 8)) - 128;
                const int C = std::max(0, static_cast<int>(Y) - 16);
                int r = (298 * C + 409 * V + 128) >> 8;
                int g = (298 * C - 100 * U - 208 * V + 128) >> 8;
                int b = (298 * C + 516 * U + 128) >> 8;
                dstRow[x * 4 + 0] = clamp(b);
                dstRow[x * 4 + 1] = clamp(g);
                dstRow[x * 4 + 2] = clamp(r);
                dstRow[x * 4 + 3] = 255;
            }
        }
        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
        frame.setAspectRatio(1.0);
        return frame;
    }

    SwVideoFrame convertYUY2(IMFMediaBuffer* buffer) {
        if (!buffer) {
            return {};
        }
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        BYTE* data = nullptr;
        LONG pitch = 0;
        DWORD curLen = 0;
        bool locked2D = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            if (SUCCEEDED(buffer2D->Lock2D(&data, &pitch))) {
                locked2D = true;
            }
        }
        DWORD maxLen = 0;
        if (!locked2D) {
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
                return {};
            }
            pitch = m_width > 0 ? m_width * 2 : 0;
        }
        const int width = m_width > 0 ? m_width : 0;
        const int height = m_height > 0 ? m_height : 0;

        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            if (locked2D) {
                buffer2D->Unlock2D();
            } else {
                buffer->Unlock();
            }
            return {};
        }
        auto clamp = [](int v) -> uint8_t { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
        uint8_t* dstBase = frame.planeData(0);
        const int dstStride = frame.planeStride(0);
        const LONG absPitch = pitch < 0 ? -pitch : pitch;
        for (int y = 0; y < height; ++y) {
            const BYTE* srcRow =
                pitch >= 0 ? data + static_cast<size_t>(y) * absPitch
                           : data + static_cast<size_t>(height - 1 - y) * absPitch;
            uint8_t* dstRow = dstBase + static_cast<size_t>(y) * dstStride;
            for (int x = 0; x < width; x += 2) {
                const int idx = x * 2;
                const int y0 = srcRow[idx + 0];
                const int u = srcRow[idx + 1] - 128;
                const int y1 = srcRow[idx + 2];
                const int v = srcRow[idx + 3] - 128;
                const int c0 = std::max(0, y0 - 16);
                const int c1 = std::max(0, y1 - 16);

                auto convertPixel = [&](int C, uint8_t* dstPixel) {
                    int r = (298 * C + 409 * v + 128) >> 8;
                    int g = (298 * C - 100 * u - 208 * v + 128) >> 8;
                    int b = (298 * C + 516 * u + 128) >> 8;
                    dstPixel[0] = clamp(b);
                    dstPixel[1] = clamp(g);
                    dstPixel[2] = clamp(r);
                    dstPixel[3] = 255;
                };

                convertPixel(c0, dstRow + x * 4);
                if (x + 1 < width) {
                    convertPixel(c1, dstRow + (x + 1) * 4);
                }
            }
        }
        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
        frame.setAspectRatio(1.0);
        return frame;
    }

    int deriveNV12Stride(int bufferSize) const {
        if (m_height <= 0) {
            return m_width;
        }
        const int numerator = bufferSize * 2;
        const int denominator = m_height * 3;
        if (denominator == 0) {
            return m_width;
        }
        int stride = numerator / denominator;
        return stride > 0 ? stride : m_width;
    }

    int deriveP010Stride(int bufferSize) const {
        if (m_height <= 0) {
            return m_width * 2;
        }
        const int numerator = bufferSize * 2;
        const int denominator = m_height * 3;
        if (denominator == 0) {
            return m_width * 2;
        }
        int stride = numerator / denominator;
        return stride > 0 ? stride : (m_width * 2);
    }

    void shutdown() {
        m_dxgiDeviceManager.Reset();
        m_decoder.Reset();
        if (m_mfStarted) {
            MFShutdown();
            m_mfStarted = false;
        }
        if (m_comInit) {
            CoUninitialize();
            m_comInit = false;
        }
        m_ready = false;
        m_outputTypeReady = false;
        m_outputSubtype = GUID_NULL;
        m_streamingStarted = false;
        m_hardwareInteropActive = false;
    }

    SwVideoPacket::Codec m_targetCodec;
    const char* m_name{nullptr};
    DecoderMode m_decoderMode{DecoderMode::Auto};
    bool m_comInit{false};
    bool m_mfStarted{false};
    bool m_ready{false};
    int m_width{0};
    int m_height{0};
    GUID m_outputSubtype{GUID_NULL};
    Microsoft::WRL::ComPtr<IMFTransform> m_decoder;
    std::atomic<bool> m_loggedFirstOutputFrame{false};
    std::atomic<bool> m_loggedInitializationFailure{false};
    std::atomic<bool> m_loggedOutputTypeFailure{false};
    std::atomic<bool> m_loggedSequenceHeader{false};
    std::atomic<bool> m_nextInputDiscontinuity{true};
    std::atomic<uint64_t> m_needMoreInputCount{0};
    std::atomic<uint64_t> m_decodedFrameCount{0};
    bool m_outputTypeReady{false};
    bool m_streamingStarted{false};
    bool m_hardwareInteropActive{false};
    SwByteArray m_sequenceHeader;
    int m_inputWidth{0};
    int m_inputHeight{0};
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_dxgiDeviceManager;
};

class SwMediaFoundationH264Decoder : public SwMediaFoundationDecoderBase {
public:
    /**
     * @brief Constructs a `SwMediaFoundationH264Decoder` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationH264Decoder(DecoderMode mode = DecoderMode::Auto,
                                          const char* decoderName = "SwMediaFoundationH264Decoder")
        : SwMediaFoundationDecoderBase(SwVideoPacket::Codec::H264, decoderName, mode) {}

protected:
    GUID inputSubtype() const override { return MFVideoFormat_H264_ES; }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        return createDecoderFromEnum(MFVideoFormat_H264, decoder);
    }

    /**
     * @brief Performs the `configureInputType` operation.
     * @param type Value passed to the method.
     */
    void configureInputType(IMFMediaType* type) const override {
        if (type) {
            type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
        }
    }
};

class SwMediaFoundationH265Decoder : public SwMediaFoundationDecoderBase {
public:
    /**
     * @brief Constructs a `SwMediaFoundationH265Decoder` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationH265Decoder(DecoderMode mode = DecoderMode::Auto,
                                          const char* decoderName = "SwMediaFoundationH265Decoder")
        : SwMediaFoundationDecoderBase(SwVideoPacket::Codec::H265, decoderName, mode) {}

protected:
    GUID inputSubtype() const override { return MFVideoFormat_HEVC; }

    int outputSubtypeRank(const GUID& subtype) const override {
        if (subtype == MFVideoFormat_NV12) {
            return 0;
        }
        if (subtype == MFVideoFormat_P010) {
            return 1;
        }
        if (subtype == MFVideoFormat_P016) {
            return 2;
        }
        if (subtype == MFVideoFormat_RGB32) {
            return 3;
        }
        if (subtype == MFVideoFormat_YUY2) {
            return 4;
        }
        return -1;
    }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        HRESULT hr = createDecoderFromEnum(MFVideoFormat_HEVC, decoder);
        if (FAILED(hr)) {
            hr = createDecoderFromEnum(MFVideoFormat_HEVC_ES, decoder);
        }
        return hr;
    }

    /**
     * @brief Performs the `configureInputType` operation.
     * @param type Value passed to the method.
     */
    void configureInputType(IMFMediaType* type) const override {
        if (type) {
            type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH265VProfile_Main_420_8);
        }
    }
};

class SwMediaFoundationAv1Decoder : public SwMediaFoundationDecoderBase {
public:
    /**
     * @brief Constructs a `SwMediaFoundationAv1Decoder` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationAv1Decoder(DecoderMode mode = DecoderMode::Auto,
                                         const char* decoderName = "SwMediaFoundationAv1Decoder")
        : SwMediaFoundationDecoderBase(SwVideoPacket::Codec::AV1, decoderName, mode) {}

protected:
    GUID inputSubtype() const override { return MFVideoFormat_AV1; }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        return createDecoderFromEnum(MFVideoFormat_AV1, decoder);
    }
};

// Registration helpers
static bool g_registerMFH264Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "media-foundation",
        "Media Foundation",
        []() { return std::make_shared<SwMediaFoundationH264Decoder>(); },
        100);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "media-foundation-hardware",
        "Media Foundation Hardware",
        []() {
            return std::make_shared<SwMediaFoundationH264Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::HardwareOnly,
                "SwMediaFoundationH264DecoderHW");
        },
        90);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "media-foundation-software",
        "Media Foundation Software",
        []() {
            return std::make_shared<SwMediaFoundationH264Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::SoftwareOnly,
                "SwMediaFoundationH264DecoderSW");
        },
        80);
    return true;
}();

static bool g_registerMFH265Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "media-foundation",
        "Media Foundation",
        []() { return std::make_shared<SwMediaFoundationH265Decoder>(); },
        100);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "media-foundation-hardware",
        "Media Foundation Hardware",
        []() {
            return std::make_shared<SwMediaFoundationH265Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::HardwareOnly,
                "SwMediaFoundationH265DecoderHW");
        },
        90);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "media-foundation-software",
        "Media Foundation Software",
        []() {
            return std::make_shared<SwMediaFoundationH265Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::SoftwareOnly,
                "SwMediaFoundationH265DecoderSW");
        },
        80);
    return true;
}();

static bool g_registerMFAv1Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        "media-foundation",
        "Media Foundation",
        []() { return std::make_shared<SwMediaFoundationAv1Decoder>(); },
        100);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        "media-foundation-hardware",
        "Media Foundation Hardware",
        []() {
            return std::make_shared<SwMediaFoundationAv1Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::HardwareOnly,
                "SwMediaFoundationAv1DecoderHW");
        },
        90);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        "media-foundation-software",
        "Media Foundation Software",
        []() {
            return std::make_shared<SwMediaFoundationAv1Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::SoftwareOnly,
                "SwMediaFoundationAv1DecoderSW");
        },
        80);
    return true;
}();

#else

// Stub for non-Windows platforms
class SwMediaFoundationH264Decoder : public SwVideoDecoder {
public:
    explicit SwMediaFoundationH264Decoder(SwMediaFoundationDecoderBase::DecoderMode = SwMediaFoundationDecoderBase::DecoderMode::Auto,
                                          const char* = "SwMediaFoundationH264DecoderStub") {}
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "SwMediaFoundationH264DecoderStub"; }
    /**
     * @brief Performs the `feed` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool feed(const SwVideoPacket&) override { return false; }
};

class SwMediaFoundationH265Decoder : public SwVideoDecoder {
public:
    explicit SwMediaFoundationH265Decoder(SwMediaFoundationDecoderBase::DecoderMode = SwMediaFoundationDecoderBase::DecoderMode::Auto,
                                          const char* = "SwMediaFoundationH265DecoderStub") {}
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "SwMediaFoundationH265DecoderStub"; }
    /**
     * @brief Performs the `feed` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool feed(const SwVideoPacket&) override { return false; }
};

class SwMediaFoundationAv1Decoder : public SwVideoDecoder {
public:
    explicit SwMediaFoundationAv1Decoder(SwMediaFoundationDecoderBase::DecoderMode = SwMediaFoundationDecoderBase::DecoderMode::Auto,
                                         const char* = "SwMediaFoundationAv1DecoderStub") {}
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "SwMediaFoundationAv1DecoderStub"; }
    /**
     * @brief Performs the `feed` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool feed(const SwVideoPacket&) override { return false; }
};

static bool g_registerMFH264Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "media-foundation",
        "Media Foundation",
        []() { return std::make_shared<SwMediaFoundationH264Decoder>(); },
        100,
        false,
        false);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "media-foundation-hardware",
        "Media Foundation Hardware",
        []() {
            return std::make_shared<SwMediaFoundationH264Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::HardwareOnly,
                "SwMediaFoundationH264DecoderHWStub");
        },
        90,
        false,
        false);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "media-foundation-software",
        "Media Foundation Software",
        []() {
            return std::make_shared<SwMediaFoundationH264Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::SoftwareOnly,
                "SwMediaFoundationH264DecoderSWStub");
        },
        80,
        false,
        false);
    return true;
}();

static bool g_registerMFH265Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "media-foundation",
        "Media Foundation",
        []() { return std::make_shared<SwMediaFoundationH265Decoder>(); },
        100,
        false,
        false);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "media-foundation-hardware",
        "Media Foundation Hardware",
        []() {
            return std::make_shared<SwMediaFoundationH265Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::HardwareOnly,
                "SwMediaFoundationH265DecoderHWStub");
        },
        90,
        false,
        false);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "media-foundation-software",
        "Media Foundation Software",
        []() {
            return std::make_shared<SwMediaFoundationH265Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::SoftwareOnly,
                "SwMediaFoundationH265DecoderSWStub");
        },
        80,
        false,
        false);
    return true;
}();

static bool g_registerMFAv1Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        "media-foundation",
        "Media Foundation",
        []() { return std::make_shared<SwMediaFoundationAv1Decoder>(); },
        100,
        false,
        false);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        "media-foundation-hardware",
        "Media Foundation Hardware",
        []() {
            return std::make_shared<SwMediaFoundationAv1Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::HardwareOnly,
                "SwMediaFoundationAv1DecoderHWStub");
        },
        90,
        false,
        false);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        "media-foundation-software",
        "Media Foundation Software",
        []() {
            return std::make_shared<SwMediaFoundationAv1Decoder>(
                SwMediaFoundationDecoderBase::DecoderMode::SoftwareOnly,
                "SwMediaFoundationAv1DecoderSWStub");
        },
        80,
        false,
        false);
    return true;
}();

#endif
