#pragma once

/**
 * @file src/media/server/SwMediaServerFactory.h
 * @brief Scheme-based factory for media server transports.
 */

#include "media/SwMediaUrl.h"
#include "media/rtp/SwRtpServerTransport.h"
#include "media/rtsp/SwRtspServerTransport.h"
#include "media/server/SwMediaServerConfig.h"
#include "media/server/SwVideoTransportServer.h"
#include "media/swvtp/SwVtpServerTransport.h"
#include "media/transport/SwUdpServerTransport.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

class SwMediaServerFactory {
public:
    static std::shared_ptr<SwVideoTransportServer> createTransport(const SwString& rawUrl) {
        SwMediaServerConfig config;
        config.endpoint = endpointFromUrl_(SwMediaUrl::parse(rawUrl));
        return createTransport(config);
    }

    static std::shared_ptr<SwVideoTransportServer> createTransport(
        const SwMediaServerConfig& config) {
        std::shared_ptr<SwVideoTransportServer> transport;
        switch (config.endpoint.protocol) {
        case SwMediaTransportProtocol::SwVtp:
            transport = std::make_shared<SwVtpServerTransport>();
            break;
        case SwMediaTransportProtocol::Rtp:
            transport = std::make_shared<SwRtpServerTransport>();
            break;
        case SwMediaTransportProtocol::Rtsp:
            transport = std::make_shared<SwRtspServerTransport>();
            break;
        case SwMediaTransportProtocol::Udp:
            transport = std::make_shared<SwUdpServerTransport>();
            break;
        case SwMediaTransportProtocol::Unknown:
        default:
            return std::shared_ptr<SwVideoTransportServer>();
        }
        transport->configure(config);
        return transport;
    }

private:
    static SwTransportEndpoint endpointFromUrl_(const SwMediaUrl& url) {
        SwTransportEndpoint endpoint;
        const SwString scheme = url.scheme().toLower();
        if (scheme == "swvtp") {
            endpoint.protocol = SwMediaTransportProtocol::SwVtp;
        } else if (scheme == "rtp") {
            endpoint.protocol = SwMediaTransportProtocol::Rtp;
        } else if (scheme == "rtsp" || scheme == "rtsps") {
            endpoint.protocol = SwMediaTransportProtocol::Rtsp;
        } else if (scheme == "udp") {
            endpoint.protocol = SwMediaTransportProtocol::Udp;
        }

        endpoint.host = url.host().isEmpty() ? SwString("0.0.0.0") : url.host();
        endpoint.bindAddress = url.queryValue("bind", "0.0.0.0");
        endpoint.port = static_cast<uint16_t>(url.port() > 0 ? url.port() : defaultPort_(endpoint.protocol));
        endpoint.interfaceAddress = url.queryValue("iface");
        endpoint.multicastLoopback = url.queryValue("loopback").toLower() == "1" ||
                                     url.queryValue("loopback").toLower() == "true";
        endpoint.ttl = static_cast<uint8_t>(
            std::max(1, url.queryValue("ttl", "1").toInt()));

        const SwString mode = url.queryValue("mode").toLower();
        if (mode == "broadcast" || endpoint.host == "255.255.255.255") {
            endpoint.deliveryMode = SwMediaTransportDeliveryMode::Broadcast;
        } else if (mode == "multicast" || isMulticastHost_(endpoint.host)) {
            endpoint.deliveryMode = SwMediaTransportDeliveryMode::Multicast;
        } else {
            endpoint.deliveryMode = SwMediaTransportDeliveryMode::Unicast;
        }
        return endpoint;
    }

    static uint16_t defaultPort_(SwMediaTransportProtocol protocol) {
        switch (protocol) {
        case SwMediaTransportProtocol::Rtsp:
            return 8554;
        case SwMediaTransportProtocol::Rtp:
            return 5004;
        case SwMediaTransportProtocol::Udp:
            return 5000;
        case SwMediaTransportProtocol::SwVtp:
            return 55245;
        case SwMediaTransportProtocol::Unknown:
        default:
            break;
        }
        return 0;
    }

    static bool isIpv4MulticastHost_(const SwString& host) {
        const std::string text = host.trimmed().toStdString();
        if (text.empty() || text.find(':') != std::string::npos) {
            return false;
        }
        const std::size_t dot = text.find('.');
        if (dot == std::string::npos) {
            return false;
        }
        const int firstOctet = std::atoi(text.substr(0, dot).c_str());
        return firstOctet >= 224 && firstOctet <= 239;
    }

    static bool isIpv6MulticastHost_(const SwString& host) {
        const SwString normalized = host.trimmed().toLower();
        return normalized.startsWith("ff");
    }

    static bool isMulticastHost_(const SwString& host) {
        return isIpv4MulticastHost_(host) || isIpv6MulticastHost_(host);
    }
};
