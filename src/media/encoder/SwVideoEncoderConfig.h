#pragma once

/**
 * @file src/media/encoder/SwVideoEncoderConfig.h
 * @brief Shared video encoder configuration.
 */

#include "media/SwVideoPacket.h"

#include <cstdint>

struct SwVideoEncoderConfig {
    SwVideoPacket::Codec codec{SwVideoPacket::Codec::Unknown};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t fpsNumerator{30};
    uint32_t fpsDenominator{1};
    uint32_t startBitrateKbps{4000};
    uint32_t minBitrateKbps{500};
    uint32_t maxBitrateKbps{20000};
    uint16_t keyFrameIntervalFrames{30};
    bool lowLatency{true};
    bool realtime{true};
    bool temporalLayersEnabled{false};

    bool isValid() const {
        return codec != SwVideoPacket::Codec::Unknown &&
               width != 0U &&
               height != 0U &&
               fpsNumerator != 0U &&
               fpsDenominator != 0U &&
               minBitrateKbps <= maxBitrateKbps;
    }
};
