#pragma once

/**
 * @file src/media/SwAudioPacket.h
 * @ingroup media
 * @brief Declares the compressed-audio packet exchanged between media sources and audio decoders.
 */

#include "core/types/SwByteArray.h"
#include "core/types/SwString.h"

#include <cstdint>

class SwAudioPacket {
public:
    enum class Codec {
        Unknown,
        PCMU,
        PCMA,
        Opus,
        AAC,
        RawF32
    };

    SwAudioPacket() = default;

    Codec codec() const { return m_codec; }
    void setCodec(Codec codec) { m_codec = codec; }

    const SwString& trackId() const { return m_trackId; }
    void setTrackId(const SwString& trackId) { m_trackId = trackId; }

    int payloadType() const { return m_payloadType; }
    void setPayloadType(int payloadType) { m_payloadType = payloadType; }

    int clockRate() const { return m_clockRate; }
    void setClockRate(int clockRate) { m_clockRate = clockRate; }

    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int sampleRate) { m_sampleRate = sampleRate; }

    int channelCount() const { return m_channelCount; }
    void setChannelCount(int channelCount) { m_channelCount = channelCount; }

    const SwByteArray& payload() const { return m_payload; }
    void setPayload(const SwByteArray& payload) { m_payload = payload; }
    void setPayload(SwByteArray&& payload) { m_payload = std::move(payload); }

    std::int64_t pts() const { return m_pts; }
    void setPts(std::int64_t pts) { m_pts = pts; }

    std::int64_t dts() const { return m_dts; }
    void setDts(std::int64_t dts) { m_dts = dts; }

    bool isDiscontinuity() const { return m_discontinuity; }
    void setDiscontinuity(bool discontinuity) { m_discontinuity = discontinuity; }

private:
    Codec m_codec{Codec::Unknown};
    SwString m_trackId{};
    int m_payloadType{-1};
    int m_clockRate{0};
    int m_sampleRate{0};
    int m_channelCount{0};
    SwByteArray m_payload{};
    std::int64_t m_pts{-1};
    std::int64_t m_dts{-1};
    bool m_discontinuity{false};
};
