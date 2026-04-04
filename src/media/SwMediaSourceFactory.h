#pragma once

/**
 * @file src/media/SwMediaSourceFactory.h
 * @ingroup media
 * @brief Declares the scheme-based media source factory used by generic player examples.
 */

#include "media/SwFileVideoSource.h"
#include "media/SwHttpMjpegSource.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwRtpVideoSource.h"
#include "media/SwRtspUdpSource.h"
#include "media/SwUdpVideoSource.h"
#include "media/SwMediaSource.h"
#include "media/SwVideoSource.h"

#include <memory>

class SwMediaSourceFactory {
public:
    static std::shared_ptr<SwMediaSource> createMediaSource(const SwString& rawUrl) {
        return createMediaSource(SwMediaOpenOptions::fromUrl(rawUrl));
    }

    static std::shared_ptr<SwMediaSource> createMediaSource(const SwMediaOpenOptions& options) {
        return std::dynamic_pointer_cast<SwMediaSource>(createVideoSource(options));
    }

    static std::shared_ptr<SwVideoSource> createVideoSource(const SwString& rawUrl) {
        return createVideoSource(SwMediaOpenOptions::fromUrl(rawUrl));
    }

    static std::shared_ptr<SwVideoSource> createVideoSource(const SwMediaOpenOptions& options) {
        const SwString scheme = options.mediaUrl.scheme().toLower();

        if (scheme == "rtsp") {
            auto source = std::make_shared<SwRtspUdpSource>(options.sourceUrl());
            source->setLowLatencyMode(options.lowLatency, 500);
            source->setEnableAudio(options.enableAudio);
            source->setEnableMetadata(options.enableMetadata);
            if (!options.bindAddress.isEmpty()) {
                source->setLocalAddress(options.bindAddress);
            }
            if (options.rtpPort != 0) {
                const uint16_t rtcpPort =
                    options.rtcpPort != 0 ? options.rtcpPort
                                          : static_cast<uint16_t>(options.rtpPort + 1);
                source->forceLocalBind(options.bindAddress.isEmpty() ? SwString("0.0.0.0")
                                                                     : options.bindAddress,
                                       options.rtpPort,
                                       rtcpPort);
            }
            if (options.transport == SwMediaOpenOptions::TransportPreference::Tcp) {
                source->setUseTcpTransport(true);
            }
            return source;
        }

        if (scheme == "http") {
            return std::make_shared<SwHttpMjpegSource>(options.sourceUrl());
        }

        if (scheme == "rtp") {
            return std::make_shared<SwRtpVideoSource>(options);
        }

        if (scheme == "udp") {
            if (options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::Rtp) {
                return std::make_shared<SwRtpVideoSource>(options);
            }
            return std::make_shared<SwUdpVideoSource>(options);
        }

        if (scheme == "file" || scheme.isEmpty()) {
            return std::make_shared<SwFileVideoSource>(options.mediaUrl.path().toStdString(),
                                                       options.codec);
        }

        return std::shared_ptr<SwVideoSource>();
    }
};
