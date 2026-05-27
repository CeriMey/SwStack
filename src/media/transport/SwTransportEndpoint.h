#pragma once

/**
 * @file src/media/transport/SwTransportEndpoint.h
 * @brief Shared media transport endpoint descriptor.
 */

#include "core/types/SwString.h"

#include <cstdint>

enum class SwMediaTransportProtocol {
    Unknown,
    Udp,
    Rtp,
    Rtsp,
    SwVtp
};

enum class SwMediaTransportDeliveryMode {
    Unicast,
    Broadcast,
    Multicast
};

struct SwTransportEndpoint {
    SwMediaTransportProtocol protocol{SwMediaTransportProtocol::Unknown};
    SwMediaTransportDeliveryMode deliveryMode{SwMediaTransportDeliveryMode::Unicast};
    SwString bindAddress{"0.0.0.0"};
    SwString host{"0.0.0.0"};
    uint16_t port{0};
    SwString interfaceAddress{};
    uint8_t ttl{1};
    bool multicastLoopback{false};

    bool isValid() const {
        return protocol != SwMediaTransportProtocol::Unknown && port != 0U;
    }
};

inline SwString swMediaTransportProtocolName(SwMediaTransportProtocol protocol) {
    switch (protocol) {
    case SwMediaTransportProtocol::Udp:
        return "udp";
    case SwMediaTransportProtocol::Rtp:
        return "rtp";
    case SwMediaTransportProtocol::Rtsp:
        return "rtsp";
    case SwMediaTransportProtocol::SwVtp:
        return "swvtp";
    case SwMediaTransportProtocol::Unknown:
    default:
        break;
    }
    return "unknown";
}
