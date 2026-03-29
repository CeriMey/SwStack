#pragma once

/**
 * @file src/media/SwMediaPacket.h
 * @ingroup media
 * @brief Declares a generic media packet exchanged between SwMediaSource and SwMediaPlayer.
 */

#include "core/types/SwByteArray.h"
#include "core/types/SwString.h"
#include "media/SwVideoPacket.h"

#include <cstdint>

class SwMediaPacket {
public:
    enum class Type {
        Unknown,
        Audio,
        Video,
        Metadata
    };

    SwMediaPacket() = default;

    Type type() const { return m_type; }
    void setType(Type type) { m_type = type; }

    const SwString& trackId() const { return m_trackId; }
    void setTrackId(const SwString& trackId) { m_trackId = trackId; }

    const SwString& codec() const { return m_codec; }
    void setCodec(const SwString& codec) { m_codec = codec; }

    int payloadType() const { return m_payloadType; }
    void setPayloadType(int payloadType) { m_payloadType = payloadType; }

    int clockRate() const { return m_clockRate; }
    void setClockRate(int clockRate) { m_clockRate = clockRate; }

    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int sampleRate) { m_sampleRate = sampleRate; }

    int channelCount() const { return m_channelCount; }
    void setChannelCount(int channelCount) { m_channelCount = channelCount; }

    const SwByteArray& payload() const { return m_payload; }
    SwByteArray& payload() { return m_payload; }
    void setPayload(const SwByteArray& payload) { m_payload = payload; }
    void setPayload(SwByteArray&& payload) { m_payload = std::move(payload); }

    std::int64_t pts() const { return m_pts; }
    void setPts(std::int64_t pts) { m_pts = pts; }

    std::int64_t dts() const { return m_dts; }
    void setDts(std::int64_t dts) { m_dts = dts; }

    bool isKeyFrame() const { return m_keyFrame; }
    void setKeyFrame(bool keyFrame) { m_keyFrame = keyFrame; }

    bool isDiscontinuity() const { return m_discontinuity; }
    void setDiscontinuity(bool discontinuity) { m_discontinuity = discontinuity; }

    static SwMediaPacket fromVideoPacket(const SwVideoPacket& packet,
                                         const SwString& trackId = SwString()) {
        SwMediaPacket mediaPacket;
        mediaPacket.setType(Type::Video);
        mediaPacket.setTrackId(trackId);
        mediaPacket.setCodec(videoCodecName_(packet.codec()));
        mediaPacket.setPayload(packet.payload());
        mediaPacket.setPts(packet.pts());
        mediaPacket.setDts(packet.dts());
        mediaPacket.setKeyFrame(packet.isKeyFrame());
        mediaPacket.setDiscontinuity(packet.isDiscontinuity());
        return mediaPacket;
    }

private:
    static SwString videoCodecName_(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::RawRGB:
            return "raw-rgb";
        case SwVideoPacket::Codec::RawBGR:
            return "raw-bgr";
        case SwVideoPacket::Codec::RawRGBA:
            return "raw-rgba";
        case SwVideoPacket::Codec::RawBGRA:
            return "raw-bgra";
        case SwVideoPacket::Codec::H264:
            return "h264";
        case SwVideoPacket::Codec::H265:
            return "h265";
        case SwVideoPacket::Codec::VP8:
            return "vp8";
        case SwVideoPacket::Codec::VP9:
            return "vp9";
        case SwVideoPacket::Codec::AV1:
            return "av1";
        case SwVideoPacket::Codec::MotionJPEG:
            return "mjpeg";
        case SwVideoPacket::Codec::Unknown:
        default:
            break;
        }
        return "unknown";
    }

    Type m_type{Type::Unknown};
    SwString m_trackId{};
    SwString m_codec{};
    int m_payloadType{-1};
    int m_clockRate{0};
    int m_sampleRate{0};
    int m_channelCount{0};
    SwByteArray m_payload{};
    std::int64_t m_pts{-1};
    std::int64_t m_dts{-1};
    bool m_keyFrame{false};
    bool m_discontinuity{false};
};
