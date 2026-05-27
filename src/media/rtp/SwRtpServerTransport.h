#pragma once

/**
 * @file src/media/rtp/SwRtpServerTransport.h
 * @brief RTP server transport implementation of the common media server interface.
 */

#include "media/transport/SwUdpServerTransport.h"

#include <cstdint>

class SwRtpServerTransport : public SwUdpServerTransport {
public:
    explicit SwRtpServerTransport(const SwString& protocolName = SwString("rtp"))
        : SwUdpServerTransport(protocolName) {}

protected:
    SwByteArray makeDatagramLocked_(const SwVideoPublishStream& stream,
                                    const SwVideoPacket& packet) override {
        const SwByteArray& payload = packet.payload();
        if (payload.isEmpty() || !payload.constData()) {
            return SwByteArray();
        }

        const uint16_t sequence = m_sequenceNumber++;
        const uint32_t timestamp = rtpTimestampLocked_(stream, packet);
        const uint8_t payloadType = payloadTypeForCodec_(packet.codec());

        char header[12] = {};
        header[0] = static_cast<char>(0x80U);
        header[1] = static_cast<char>(0x80U | (payloadType & 0x7FU));
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
        datagram.append(header, sizeof(header));
        datagram.append(payload.constData(), payload.size());
        return datagram;
    }

private:
    static uint8_t payloadTypeForCodec_(SwVideoPacket::Codec codec) {
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

    uint32_t rtpTimestampLocked_(const SwVideoPublishStream& stream,
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

    uint16_t m_sequenceNumber{1};
    uint32_t m_nextTimestamp{0};
    uint32_t m_ssrc{0x53575254U};
};
