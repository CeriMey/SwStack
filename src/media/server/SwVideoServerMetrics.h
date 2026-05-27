#pragma once

/**
 * @file src/media/server/SwVideoServerMetrics.h
 * @brief Metrics shared by all video publishing transports.
 */

#include "media/transport/SwTransportMetrics.h"

#include <cstdint>

struct SwVideoServerMetrics {
    SwTransportMetrics transport{};
    uint64_t framesAccepted{0};
    uint64_t framesSent{0};
    uint64_t framesDropped{0};
    uint64_t videoBytesAccepted{0};
    uint64_t videoBytesSent{0};
    uint32_t targetBitrateKbps{0};
    uint32_t encoderBitrateKbps{0};
    double liveVideoKbps{0.0};
    double averageEncodeLatencyMs{0.0};
    double averagePacketizeLatencyMs{0.0};
};
