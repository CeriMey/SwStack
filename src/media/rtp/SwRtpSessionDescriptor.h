#pragma once

/**
 * @file src/media/rtp/SwRtpSessionDescriptor.h
 * @ingroup media
 * @brief Declares the normalized RTP session descriptor used by RTP-based video sources.
 */

#include "media/SwMediaOpenOptions.h"

struct SwRtpSessionDescriptor {
    SwString bindAddress{"0.0.0.0"};
    uint16_t localRtpPort{0};
    uint16_t localRtcpPort{0};
    SwString sourceAddressFilter{};
    uint16_t sourceRtpPort{0};
    uint16_t sourceRtcpPort{0};
    SwVideoPacket::Codec codec{SwVideoPacket::Codec::Unknown};
    int payloadType{96};
    int clockRate{90000};
    SwMediaOpenOptions::UdpPayloadFormat format{SwMediaOpenOptions::UdpPayloadFormat::Rtp};
    SwString fmtp{};
    bool allowKeyFrameRequests{true};
    bool lowLatency{true};

    static SwRtpSessionDescriptor fromOpenOptions(const SwMediaOpenOptions& options) {
        SwRtpSessionDescriptor descriptor;
        descriptor.bindAddress =
            options.bindAddress.isEmpty() ? SwString("0.0.0.0") : options.bindAddress;
        descriptor.localRtpPort = options.rtpPort != 0
                                      ? options.rtpPort
                                      : static_cast<uint16_t>(options.mediaUrl.port() > 0
                                                                   ? options.mediaUrl.port()
                                                                   : 5004);
        descriptor.localRtcpPort = options.rtcpPort != 0
                                       ? options.rtcpPort
                                       : static_cast<uint16_t>(descriptor.localRtpPort + 1);
        descriptor.sourceAddressFilter = options.sourceAddressFilter;
        descriptor.sourceRtpPort = 0;
        descriptor.sourceRtcpPort = options.sourceRtcpPort;
        descriptor.codec = options.codec == SwVideoPacket::Codec::Unknown
                               ? SwVideoPacket::Codec::H264
                               : options.codec;
        descriptor.payloadType = options.payloadType >= 0 ? options.payloadType : 96;
        descriptor.clockRate = options.clockRate > 0 ? options.clockRate : 90000;
        descriptor.format =
            options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::Auto
                ? SwMediaOpenOptions::UdpPayloadFormat::Rtp
                : options.udpFormat;
        descriptor.fmtp = options.fmtp;
        descriptor.lowLatency = options.lowLatency;
        return descriptor;
    }
};
