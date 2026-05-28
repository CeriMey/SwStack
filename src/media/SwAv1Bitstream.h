#pragma once

/**
 * @file src/media/SwAv1Bitstream.h
 * @brief Small AV1 OBU parsing helpers shared by media decoders and transports.
 */

#include "core/types/SwByteArray.h"
#include "core/types/SwList.h"

#include <cstddef>
#include <cstdint>

enum class SwAv1ObuType : uint8_t {
    Reserved0 = 0,
    SequenceHeader = 1,
    TemporalDelimiter = 2,
    FrameHeader = 3,
    TileGroup = 4,
    Metadata = 5,
    Frame = 6,
    RedundantFrameHeader = 7,
    TileList = 8,
    Padding = 15
};

struct SwAv1ObuInfo {
    std::size_t offset{0};
    std::size_t headerBytes{0};
    std::size_t payloadOffset{0};
    std::size_t payloadBytes{0};
    std::size_t totalBytes{0};
    SwAv1ObuType type{SwAv1ObuType::Reserved0};
    bool hasExtension{false};
    bool hasSizeField{false};
    uint8_t temporalLayer{0};
    uint8_t spatialLayer{0};

    bool overlaps(std::size_t begin, std::size_t end) const {
        const std::size_t obuEnd = offset + totalBytes;
        return begin < obuEnd && end > offset;
    }
};

class SwAv1Bitstream {
public:
    static bool readLeb128(const uint8_t* data,
                           std::size_t size,
                           std::size_t& offset,
                           uint64_t& value,
                           std::size_t& bytesRead) {
        value = 0;
        bytesRead = 0;
        uint32_t shift = 0;
        while (bytesRead < 8U) {
            if (!data || offset >= size) {
                return false;
            }
            const uint8_t byte = data[offset++];
            ++bytesRead;
            value |= static_cast<uint64_t>(byte & 0x7FU) << shift;
            if ((byte & 0x80U) == 0U) {
                return true;
            }
            shift += 7U;
        }
        return false;
    }

    static SwList<SwAv1ObuInfo> parseObus(const SwByteArray& payload) {
        SwList<SwAv1ObuInfo> out;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
        const std::size_t size = static_cast<std::size_t>(payload.size());
        if (!data || size == 0U) {
            return out;
        }

        std::size_t offset = 0;
        while (offset < size) {
            const std::size_t obuOffset = offset;
            const uint8_t header = data[offset++];
            if ((header & 0x80U) != 0U) {
                break;
            }

            SwAv1ObuInfo obu;
            obu.offset = obuOffset;
            obu.type = static_cast<SwAv1ObuType>((header >> 3U) & 0x0FU);
            obu.hasExtension = (header & 0x04U) != 0U;
            obu.hasSizeField = (header & 0x02U) != 0U;

            if (obu.hasExtension) {
                if (offset >= size) {
                    break;
                }
                const uint8_t extension = data[offset++];
                obu.temporalLayer = static_cast<uint8_t>((extension >> 5U) & 0x07U);
                obu.spatialLayer = static_cast<uint8_t>((extension >> 3U) & 0x03U);
            }

            uint64_t obuPayloadBytes = 0;
            std::size_t sizeFieldBytes = 0;
            if (obu.hasSizeField) {
                if (!readLeb128(data, size, offset, obuPayloadBytes, sizeFieldBytes)) {
                    break;
                }
            } else {
                obuPayloadBytes = static_cast<uint64_t>(size - offset);
            }

            if (obuPayloadBytes > static_cast<uint64_t>(size - offset)) {
                break;
            }

            obu.headerBytes = offset - obuOffset;
            obu.payloadOffset = offset;
            obu.payloadBytes = static_cast<std::size_t>(obuPayloadBytes);
            obu.totalBytes = obu.headerBytes + obu.payloadBytes;
            out.append(obu);
            offset += obu.payloadBytes;
        }
        return out;
    }

    static bool containsObuType(const SwList<SwAv1ObuInfo>& obus, SwAv1ObuType type) {
        for (SwList<SwAv1ObuInfo>::const_iterator it = obus.begin(); it != obus.end(); ++it) {
            if (it->type == type) {
                return true;
            }
        }
        return false;
    }

    static bool fragmentOverlapsObuType(const SwList<SwAv1ObuInfo>& obus,
                                        std::size_t begin,
                                        std::size_t end,
                                        SwAv1ObuType type) {
        for (SwList<SwAv1ObuInfo>::const_iterator it = obus.begin(); it != obus.end(); ++it) {
            if (it->type == type && it->overlaps(begin, end)) {
                return true;
            }
        }
        return false;
    }

    static uint8_t highestTemporalLayer(const SwList<SwAv1ObuInfo>& obus) {
        uint8_t layer = 0;
        for (SwList<SwAv1ObuInfo>::const_iterator it = obus.begin(); it != obus.end(); ++it) {
            if (it->temporalLayer > layer) {
                layer = it->temporalLayer;
            }
        }
        return layer;
    }

    static SwByteArray collectSequenceHeader(const SwByteArray& payload) {
        return collectSequenceHeader(payload, parseObus(payload));
    }

    static SwByteArray collectSequenceHeader(const SwByteArray& payload,
                                             const SwList<SwAv1ObuInfo>& obus) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
        const std::size_t size = static_cast<std::size_t>(payload.size());
        if (!data || size == 0U) {
            return SwByteArray();
        }

        for (SwList<SwAv1ObuInfo>::const_iterator it = obus.begin(); it != obus.end(); ++it) {
            if (it->type != SwAv1ObuType::SequenceHeader ||
                it->totalBytes == 0U ||
                it->offset + it->totalBytes > size) {
                continue;
            }
            SwByteArray sequenceHeader;
            sequenceHeader.append(reinterpret_cast<const char*>(data + it->offset),
                                  it->totalBytes);
            return sequenceHeader;
        }
        return SwByteArray();
    }

    static SwByteArray insertSequenceHeader(const SwByteArray& payload,
                                            const SwByteArray& sequenceHeader) {
        if (payload.isEmpty() || sequenceHeader.isEmpty()) {
            return payload;
        }

        const char* data = payload.constData();
        const std::size_t size = static_cast<std::size_t>(payload.size());
        if (!data || size == 0U) {
            return payload;
        }

        std::size_t insertOffset = 0;
        const SwList<SwAv1ObuInfo> obus = parseObus(payload);
        if (containsObuType(obus, SwAv1ObuType::SequenceHeader)) {
            return payload;
        }
        for (SwList<SwAv1ObuInfo>::const_iterator it = obus.begin(); it != obus.end(); ++it) {
            if (it->offset != insertOffset || it->type != SwAv1ObuType::TemporalDelimiter) {
                break;
            }
            insertOffset = it->offset + it->totalBytes;
        }

        SwByteArray patched;
        patched.reserve(size + sequenceHeader.size());
        if (insertOffset > 0U) {
            patched.append(data, insertOffset);
        }
        patched.append(sequenceHeader);
        patched.append(data + insertOffset, size - insertOffset);
        return patched;
    }
};
