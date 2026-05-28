#pragma once

/**
 * @file src/media/rtp/SwRtpPacketizer.h
 * @brief RTP packetization helpers for server-side H264, H265 and AV1 video.
 */

#include "core/types/SwByteArray.h"
#include "media/SwVideoPacket.h"
#include "media/server/SwVideoPublishStream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

class SwRtpPacketizer {
public:
    void setSsrc(uint32_t ssrc) { m_ssrc = ssrc == 0U ? 1U : ssrc; }
    uint32_t ssrc() const { return m_ssrc; }
    uint16_t nextSequenceNumber() const { return m_sequenceNumber; }
    uint32_t lastTimestamp() const { return m_lastTimestamp; }

    std::vector<SwByteArray> packetize(const SwVideoPublishStream& stream,
                                       const SwVideoPacket& packet,
                                       uint16_t mtuBytes) {
        std::vector<SwByteArray> out;
        const SwByteArray& payload = packet.payload();
        if (payload.isEmpty() || !payload.constData()) {
            return out;
        }

        const size_t mtu = mtuBytes < 64U ? 64U : static_cast<size_t>(mtuBytes);
        if (mtu <= kRtpHeaderBytes + 4U) {
            return out;
        }

        const uint32_t timestamp = rtpTimestamp_(stream, packet);
        m_lastTimestamp = timestamp;
        const uint8_t payloadType = payloadTypeForCodec(packet.codec());
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
        const size_t size = payload.size();
        const size_t maxPayload = mtu - kRtpHeaderBytes;

        switch (packet.codec()) {
        case SwVideoPacket::Codec::H264:
            packetizeH264_(data, size, maxPayload, payloadType, timestamp, out);
            break;
        case SwVideoPacket::Codec::H265:
            packetizeH265_(data, size, maxPayload, payloadType, timestamp, out);
            break;
        case SwVideoPacket::Codec::AV1:
            packetizeAv1_(data, size, maxPayload, payloadType, timestamp, packet.isKeyFrame(), out);
            break;
        default:
            packetizeGeneric_(data, size, maxPayload, payloadType, timestamp, out);
            break;
        }
        return out;
    }

    static uint8_t payloadTypeForCodec(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return 96;
        case SwVideoPacket::Codec::AV1:
            return 97;
        case SwVideoPacket::Codec::H265:
            return 98;
        case SwVideoPacket::Codec::VP8:
            return 100;
        case SwVideoPacket::Codec::VP9:
            return 101;
        case SwVideoPacket::Codec::MotionJPEG:
            return 26;
        default:
            break;
        }
        return 96;
    }

private:
    struct NalView {
        NalView() = default;
        NalView(size_t offsetValue, size_t sizeValue)
            : offset(offsetValue), size(sizeValue) {}

        size_t offset{0};
        size_t size{0};
    };

    enum { kRtpHeaderBytes = 12U };

    uint32_t rtpTimestamp_(const SwVideoPublishStream& stream,
                           const SwVideoPacket& packet) {
        if (packet.pts() >= 0) {
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(packet.pts()) * 90ULL) / 1000ULL);
        }
        const uint32_t fpsNum = stream.fpsNumerator == 0U ? 30U : stream.fpsNumerator;
        const uint32_t fpsDen = stream.fpsDenominator == 0U ? 1U : stream.fpsDenominator;
        const uint32_t step = static_cast<uint32_t>(
            (90000ULL * static_cast<uint64_t>(fpsDen)) / fpsNum);
        m_nextTimestamp += step == 0U ? 3000U : step;
        return m_nextTimestamp;
    }

    void appendRtpPacket_(const uint8_t* payload,
                          size_t payloadSize,
                          uint8_t payloadType,
                          uint32_t timestamp,
                          bool marker,
                          std::vector<SwByteArray>& out) {
        if (!payload || payloadSize == 0U) {
            return;
        }
        const uint16_t sequence = m_sequenceNumber++;
        char header[kRtpHeaderBytes] = {};
        header[0] = static_cast<char>(0x80U);
        header[1] = static_cast<char>((marker ? 0x80U : 0U) | (payloadType & 0x7FU));
        header[2] = static_cast<char>((sequence >> 8U) & 0xFFU);
        header[3] = static_cast<char>(sequence & 0xFFU);
        header[4] = static_cast<char>((timestamp >> 24U) & 0xFFU);
        header[5] = static_cast<char>((timestamp >> 16U) & 0xFFU);
        header[6] = static_cast<char>((timestamp >> 8U) & 0xFFU);
        header[7] = static_cast<char>(timestamp & 0xFFU);
        header[8] = static_cast<char>((m_ssrc >> 24U) & 0xFFU);
        header[9] = static_cast<char>((m_ssrc >> 16U) & 0xFFU);
        header[10] = static_cast<char>((m_ssrc >> 8U) & 0xFFU);
        header[11] = static_cast<char>(m_ssrc & 0xFFU);

        SwByteArray datagram;
        datagram.reserve(kRtpHeaderBytes + payloadSize);
        datagram.append(header, sizeof(header));
        datagram.append(reinterpret_cast<const char*>(payload), payloadSize);
        out.push_back(std::move(datagram));
    }

    static size_t startCodeSizeAt_(const uint8_t* data, size_t size, size_t pos) {
        if (!data || pos + 3U > size) {
            return 0U;
        }
        if (data[pos] == 0x00U && data[pos + 1U] == 0x00U && data[pos + 2U] == 0x01U) {
            return 3U;
        }
        if (pos + 4U <= size &&
            data[pos] == 0x00U && data[pos + 1U] == 0x00U &&
            data[pos + 2U] == 0x00U && data[pos + 3U] == 0x01U) {
            return 4U;
        }
        return 0U;
    }

    static size_t findStartCode_(const uint8_t* data, size_t size, size_t from) {
        for (size_t pos = from; pos + 3U <= size; ++pos) {
            if (startCodeSizeAt_(data, size, pos) != 0U) {
                return pos;
            }
        }
        return size;
    }

    static std::vector<NalView> splitAnnexB_(const uint8_t* data, size_t size) {
        std::vector<NalView> nals;
        size_t start = findStartCode_(data, size, 0U);
        while (start < size) {
            const size_t codeSize = startCodeSizeAt_(data, size, start);
            if (codeSize == 0U) {
                break;
            }
            const size_t nalStart = start + codeSize;
            const size_t next = findStartCode_(data, size, nalStart);
            if (next > nalStart) {
                nals.push_back(NalView(nalStart, next - nalStart));
            }
            start = next;
        }
        return nals;
    }

    static uint32_t readBeLength_(const uint8_t* data, size_t bytes) {
        uint32_t value = 0U;
        for (size_t i = 0; i < bytes; ++i) {
            value = (value << 8U) | static_cast<uint32_t>(data[i]);
        }
        return value;
    }

    static bool splitLengthPrefixed_(const uint8_t* data,
                                     size_t size,
                                     size_t lengthBytes,
                                     std::vector<NalView>& out) {
        std::vector<NalView> parsed;
        size_t offset = 0U;
        while (offset + lengthBytes <= size) {
            const uint32_t nalSize = readBeLength_(data + offset, lengthBytes);
            offset += lengthBytes;
            if (nalSize == 0U || nalSize > size - offset) {
                return false;
            }
            parsed.push_back(NalView(offset, nalSize));
            offset += nalSize;
        }
        if (offset != size || parsed.empty()) {
            return false;
        }
        out.swap(parsed);
        return true;
    }

    static std::vector<NalView> splitNals_(const uint8_t* data, size_t size) {
        std::vector<NalView> nals = splitAnnexB_(data, size);
        if (!nals.empty()) {
            return nals;
        }
        if (splitLengthPrefixed_(data, size, 4U, nals) ||
            splitLengthPrefixed_(data, size, 2U, nals)) {
            return nals;
        }
        if (size > 0U) {
            nals.push_back(NalView(0U, size));
        }
        return nals;
    }

    void packetizeH264_(const uint8_t* data,
                        size_t size,
                        size_t maxPayload,
                        uint8_t payloadType,
                        uint32_t timestamp,
                        std::vector<SwByteArray>& out) {
        const std::vector<NalView> nals = splitNals_(data, size);
        for (size_t nalIndex = 0; nalIndex < nals.size(); ++nalIndex) {
            const uint8_t* nal = data + nals[nalIndex].offset;
            const size_t nalSize = nals[nalIndex].size;
            if (!nal || nalSize == 0U) {
                continue;
            }
            const bool lastNal = nalIndex + 1U == nals.size();
            if (nalSize <= maxPayload) {
                appendRtpPacket_(nal, nalSize, payloadType, timestamp, lastNal, out);
                continue;
            }
            if (maxPayload <= 2U || nalSize <= 1U) {
                continue;
            }
            const uint8_t fuIndicator = static_cast<uint8_t>((nal[0] & 0xE0U) | 28U);
            const uint8_t nalType = static_cast<uint8_t>(nal[0] & 0x1FU);
            const size_t fragmentCapacity = maxPayload - 2U;
            size_t offset = 1U;
            while (offset < nalSize) {
                const size_t chunk = (std::min)(fragmentCapacity, nalSize - offset);
                const bool start = offset == 1U;
                const bool end = offset + chunk >= nalSize;
                uint8_t fuHeader = nalType;
                if (start) {
                    fuHeader = static_cast<uint8_t>(fuHeader | 0x80U);
                }
                if (end) {
                    fuHeader = static_cast<uint8_t>(fuHeader | 0x40U);
                }
                SwByteArray payload;
                payload.reserve(2U + chunk);
                appendByte_(payload, fuIndicator);
                appendByte_(payload, fuHeader);
                payload.append(reinterpret_cast<const char*>(nal + offset), chunk);
                appendRtpPacket_(reinterpret_cast<const uint8_t*>(payload.constData()),
                                 payload.size(),
                                 payloadType,
                                 timestamp,
                                 lastNal && end,
                                 out);
                offset += chunk;
            }
        }
    }

    void packetizeH265_(const uint8_t* data,
                        size_t size,
                        size_t maxPayload,
                        uint8_t payloadType,
                        uint32_t timestamp,
                        std::vector<SwByteArray>& out) {
        const std::vector<NalView> nals = splitNals_(data, size);
        for (size_t nalIndex = 0; nalIndex < nals.size(); ++nalIndex) {
            const uint8_t* nal = data + nals[nalIndex].offset;
            const size_t nalSize = nals[nalIndex].size;
            if (!nal || nalSize < 2U) {
                continue;
            }
            const bool lastNal = nalIndex + 1U == nals.size();
            if (nalSize <= maxPayload) {
                appendRtpPacket_(nal, nalSize, payloadType, timestamp, lastNal, out);
                continue;
            }
            if (maxPayload <= 3U) {
                continue;
            }
            const uint8_t nalType = static_cast<uint8_t>((nal[0] >> 1U) & 0x3FU);
            const uint8_t fuIndicator0 = static_cast<uint8_t>((nal[0] & 0x81U) | (49U << 1U));
            const uint8_t fuIndicator1 = nal[1];
            const size_t fragmentCapacity = maxPayload - 3U;
            size_t offset = 2U;
            while (offset < nalSize) {
                const size_t chunk = (std::min)(fragmentCapacity, nalSize - offset);
                const bool start = offset == 2U;
                const bool end = offset + chunk >= nalSize;
                uint8_t fuHeader = nalType;
                if (start) {
                    fuHeader = static_cast<uint8_t>(fuHeader | 0x80U);
                }
                if (end) {
                    fuHeader = static_cast<uint8_t>(fuHeader | 0x40U);
                }
                SwByteArray payload;
                payload.reserve(3U + chunk);
                appendByte_(payload, fuIndicator0);
                appendByte_(payload, fuIndicator1);
                appendByte_(payload, fuHeader);
                payload.append(reinterpret_cast<const char*>(nal + offset), chunk);
                appendRtpPacket_(reinterpret_cast<const uint8_t*>(payload.constData()),
                                 payload.size(),
                                 payloadType,
                                 timestamp,
                                 lastNal && end,
                                 out);
                offset += chunk;
            }
        }
    }

    void packetizeAv1_(const uint8_t* data,
                       size_t size,
                       size_t maxPayload,
                       uint8_t payloadType,
                       uint32_t timestamp,
                       bool keyFrame,
                       std::vector<SwByteArray>& out) {
        size_t offset = 0U;
        while (offset < size) {
            size_t chunk = maxPayload > 2U ? (std::min)(maxPayload - 2U, size - offset) : 0U;
            while (chunk > 0U && 1U + leb128Size_(chunk) + chunk > maxPayload) {
                --chunk;
            }
            if (chunk == 0U) {
                break;
            }
            const bool continuation = offset != 0U;
            const bool more = offset + chunk < size;
            uint8_t aggregationHeader = 0U;
            if (continuation) {
                aggregationHeader = static_cast<uint8_t>(aggregationHeader | 0x80U);
            }
            if (more) {
                aggregationHeader = static_cast<uint8_t>(aggregationHeader | 0x40U);
            }
            if (!continuation && keyFrame) {
                aggregationHeader = static_cast<uint8_t>(aggregationHeader | 0x08U);
            }
            SwByteArray payload;
            payload.reserve(1U + leb128Size_(chunk) + chunk);
            appendByte_(payload, aggregationHeader);
            appendLeb128_(payload, chunk);
            payload.append(reinterpret_cast<const char*>(data + offset), chunk);
            appendRtpPacket_(reinterpret_cast<const uint8_t*>(payload.constData()),
                             payload.size(),
                             payloadType,
                             timestamp,
                             !more,
                             out);
            offset += chunk;
        }
    }

    void packetizeGeneric_(const uint8_t* data,
                           size_t size,
                           size_t maxPayload,
                           uint8_t payloadType,
                           uint32_t timestamp,
                           std::vector<SwByteArray>& out) {
        size_t offset = 0U;
        while (offset < size) {
            const size_t chunk = (std::min)(maxPayload, size - offset);
            appendRtpPacket_(data + offset,
                             chunk,
                             payloadType,
                             timestamp,
                             offset + chunk >= size,
                             out);
            offset += chunk;
        }
    }

    static void appendByte_(SwByteArray& out, uint8_t value) {
        const char byte = static_cast<char>(value);
        out.append(&byte, 1);
    }

    static size_t leb128Size_(size_t value) {
        size_t count = 1U;
        while (value >= 0x80U) {
            value >>= 7U;
            ++count;
        }
        return count;
    }

    static void appendLeb128_(SwByteArray& out, size_t value) {
        do {
            uint8_t byte = static_cast<uint8_t>(value & 0x7FU);
            value >>= 7U;
            if (value != 0U) {
                byte = static_cast<uint8_t>(byte | 0x80U);
            }
            appendByte_(out, byte);
        } while (value != 0U);
    }

    uint16_t m_sequenceNumber{1};
    uint32_t m_nextTimestamp{0};
    uint32_t m_lastTimestamp{0};
    uint32_t m_ssrc{0x53575254U};
};
