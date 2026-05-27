#pragma once

/**
 * @file src/media/server/SwVideoPublishStream.h
 * @brief Describes one video stream published by SwMediaServer.
 */

#include "core/types/SwString.h"
#include "media/SwMediaTrack.h"
#include "media/SwVideoPacket.h"

#include <cstdint>

struct SwVideoPublishStream {
    SwString id{"video"};
    SwString trackId{"video"};
    SwVideoPacket::Codec codec{SwVideoPacket::Codec::Unknown};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t fpsNumerator{30};
    uint32_t fpsDenominator{1};
    uint32_t startBitrateKbps{4000};
    uint32_t minBitrateKbps{500};
    uint32_t maxBitrateKbps{20000};
    uint16_t latencyBudgetMs{90};
    uint16_t keyFrameIntervalFrames{30};
    bool lowLatency{true};

    bool isValid() const {
        return !id.isEmpty() &&
               !trackId.isEmpty() &&
               codec != SwVideoPacket::Codec::Unknown;
    }

    SwMediaTrack toTrack() const {
        SwMediaTrack track;
        track.id = trackId;
        track.type = SwMediaTrack::Type::Video;
        track.codec = codecName();
        track.selected = true;
        track.availability = SwMediaTrack::Availability::Available;
        return track;
    }

    SwString codecName() const {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return "h264";
        case SwVideoPacket::Codec::H265:
            return "h265";
        case SwVideoPacket::Codec::AV1:
            return "av1";
        case SwVideoPacket::Codec::VP8:
            return "vp8";
        case SwVideoPacket::Codec::VP9:
            return "vp9";
        case SwVideoPacket::Codec::MotionJPEG:
            return "mjpeg";
        default:
            break;
        }
        return "unknown";
    }
};
