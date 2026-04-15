#pragma once

/**
 * @file src/media/SwMediaSourceFactory.h
 * @ingroup media
 * @brief Declares the scheme-based media source factory used by generic player examples.
 */

#include "media/SwFileVideoSource.h"
#include "media/SwHttpMjpegSource.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwMediaSource.h"
#include "media/SwPlatformMovieSource.h"
#include "media/SwRtpVideoSource.h"
#include "media/SwRtspSource.h"
#include "media/SwUdpVideoSource.h"
#include "media/SwVideoSource.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>

class SwMediaSourceFactory {
public:
    static std::shared_ptr<SwMediaSource> createMediaSource(const SwString& rawUrl) {
        return createMediaSource(SwMediaOpenOptions::fromUrl(rawUrl));
    }

    static std::shared_ptr<SwMediaSource> createMediaSource(const SwMediaOpenOptions& options) {
        const SwString scheme = options.mediaUrl.scheme().toLower();

        if (scheme == "rtsp" || scheme == "rtsps") {
            return std::make_shared<SwRtspSource>(options);
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
            return createFileMediaSource_(options);
        }

        return std::shared_ptr<SwMediaSource>();
    }

    static std::shared_ptr<SwVideoSource> createVideoSource(const SwString& rawUrl) {
        return createVideoSource(SwMediaOpenOptions::fromUrl(rawUrl));
    }

    static std::shared_ptr<SwVideoSource> createVideoSource(const SwMediaOpenOptions& options) {
        return std::dynamic_pointer_cast<SwVideoSource>(createMediaSource(options));
    }

private:
    static SwString localFilePath_(const SwMediaOpenOptions& options) {
        std::string path = options.mediaUrl.path().toStdString();
#if defined(_WIN32)
        if (path.size() > 2U &&
            path[0] == '/' &&
            std::isalpha(static_cast<unsigned char>(path[1])) &&
            path[2] == ':') {
            path.erase(path.begin());
        }
#endif
        return SwString(path);
    }

    static bool shouldUsePlatformFileSource_(const SwMediaOpenOptions& options,
                                             const SwString& filePath) {
#if defined(_WIN32)
        if (options.codec != SwVideoPacket::Codec::Unknown) {
            return false;
        }
        std::string lowerPath = filePath.toStdString();
        std::transform(lowerPath.begin(),
                       lowerPath.end(),
                       lowerPath.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::size_t dotPos = lowerPath.find_last_of('.');
        if (dotPos == std::string::npos) {
            return false;
        }
        const std::string extension = lowerPath.substr(dotPos);
        static const std::array<const char*, 11> kContainerExtensions = {{
            ".mp4", ".mov", ".m4v", ".mkv", ".avi", ".wmv",
            ".asf", ".mpg", ".mpeg", ".webm", ".ts"
        }};
        return std::find(kContainerExtensions.begin(),
                         kContainerExtensions.end(),
                         extension) != kContainerExtensions.end();
#else
        (void)options;
        (void)filePath;
        return false;
#endif
    }

    static std::shared_ptr<SwMediaSource> createFileMediaSource_(const SwMediaOpenOptions& options) {
        const SwString filePath = localFilePath_(options);
#if defined(_WIN32)
        if (shouldUsePlatformFileSource_(options, filePath)) {
            return std::make_shared<SwPlatformMovieSource>(filePath.toStdWString());
        }
#endif
        return std::make_shared<SwFileVideoSource>(filePath.toStdString(),
                                                   options.codec);
    }
};
