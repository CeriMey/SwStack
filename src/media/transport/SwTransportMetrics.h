#pragma once

/**
 * @file src/media/transport/SwTransportMetrics.h
 * @brief Shared counters for media transports.
 */

#include <cstdint>

struct SwTransportMetrics {
    uint64_t datagramsSent{0};
    uint64_t datagramsReceived{0};
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
    uint64_t sendFailures{0};
    uint32_t activeClients{0};
    double liveSendKbps{0.0};
    double liveReceiveKbps{0.0};
};
