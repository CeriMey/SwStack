#pragma once

/**
 * @file src/media/SwMediaSourceFactory.h
 * @ingroup media
 * @brief Declares the scheme-based media source factory used by generic player examples.
 */

#include "media/SwDirectRtpMediaSource.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwPlatformMovieSource.h"
#include "media/SwSdpMediaDescription.h"
#include "media/source/SwFileVideoSource.h"
#include "media/source/SwHttpMjpegSource.h"
#include "media/source/SwMediaSource.h"
#include "media/source/SwRtpVideoSource.h"
#include "media/source/SwRtspSource.h"
#include "media/source/SwUdpVideoSource.h"
#include "media/source/SwVideoSource.h"
#include "media/source/SwVtpVideoSource.h"
#include "SwDebug.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>

static constexpr const char* kSwLogCategory_SwMediaSourceFactory = "sw.media.swmediasourcefactory";

class SwMediaSourceFactory {
public:
    static std::shared_ptr<SwMediaSource> createMediaSource(const SwString& rawUrl) {
        return createMediaSource(SwMediaOpenOptions::fromUrl(rawUrl));
    }

    static std::shared_ptr<SwMediaSource> createMediaSource(const SwMediaOpenOptions& options) {
        const SwString scheme = options.mediaUrl.scheme().toLower();

        if (scheme == "rtp" || scheme == "udp") {
            // The SDP (inline text or file) is loaded and parsed exactly once: it both
            // resolves the transport options and feeds the direct RTP source.
            if (hasSdpOption_(options)) {
                SwSdpMediaDescription description;
                if (loadSdpDescription_(options, description)) {
                    return createDirectRtpSource_(options, description);
                }
            }
            if (scheme == "rtp" || options.rtpPacketized ||
                options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::Rtp) {
                return std::make_shared<SwRtpVideoSource>(options);
            }
            return std::make_shared<SwUdpVideoSource>(options);
        }

        if (scheme == "rtsp" || scheme == "rtsps") {
            return std::make_shared<SwRtspSource>(options);
        }

        if (scheme == "http") {
            return std::make_shared<SwHttpMjpegSource>(options.sourceUrl());
        }

        if (scheme == "swvtp") {
            return std::make_shared<SwVtpVideoSource>(options);
        }

        if (scheme == "file" || scheme.isEmpty()) {
            if (auto source = createDirectRtpSourceFromSdpFile_(options)) {
                return source;
            }
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
    static bool hasSdpOption_(const SwMediaOpenOptions& options) {
        return !options.sdpText.isEmpty() || !options.sdpFile.isEmpty();
    }

    static bool isSdpPath_(const SwString& path) {
        std::string text = path.toStdString();
        std::transform(text.begin(),
                       text.end(),
                       text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text.size() >= 4U && text.substr(text.size() - 4U) == ".sdp";
    }

    static bool loadSdpDescription_(const SwMediaOpenOptions& options,
                                    SwSdpMediaDescription& description) {
        std::string sdpText = options.sdpText.toStdString();
        if (sdpText.empty() && !options.sdpFile.isEmpty()) {
            if (!SwSdpMediaDescription::loadTextFromFile(options.sdpFile, sdpText)) {
                swCWarning(kSwLogCategory_SwMediaSourceFactory)
                    << "[SwMediaSourceFactory] Failed to load SDP file "
                    << options.sdpFile;
                return false;
            }
        }
        if (sdpText.empty()) {
            return false;
        }
        if (!SwSdpMediaDescription::parse(sdpText, description)) {
            swCWarning(kSwLogCategory_SwMediaSourceFactory)
                << "[SwMediaSourceFactory] Failed to parse SDP options for "
                << options.mediaUrl.toString();
            return false;
        }
        return true;
    }

    static std::shared_ptr<SwMediaSource> createDirectRtpSource_(
        const SwMediaOpenOptions& options,
        const SwSdpMediaDescription& description) {
        SwMediaOpenOptions directOptions = options;
        if (description.applyToOpenOptions(directOptions)) {
            swCWarning(kSwLogCategory_SwMediaSourceFactory)
                << "[SwMediaSourceFactory] Applied SDP to "
                << directOptions.mediaUrl.toString()
                << " transport=" << (directOptions.rtpPacketized ? "rtp" : "udp")
                << " port="
                << (directOptions.rtpPort != 0 ? directOptions.rtpPort
                                               : directOptions.mediaUrl.port())
                << " payload=" << directOptions.payloadType
                << " codec=" << SwMediaOpenOptions::codecToString(directOptions.codec);
        } else {
            swCWarning(kSwLogCategory_SwMediaSourceFactory)
                << "[SwMediaSourceFactory] SDP has no supported video track for "
                << directOptions.mediaUrl.toString();
        }
        return std::make_shared<SwDirectRtpMediaSource>(directOptions, description);
    }

    static std::shared_ptr<SwMediaSource> createDirectRtpSourceFromSdpFile_(
        const SwMediaOpenOptions& options) {
        const SwString filePath = localFilePath_(options);
        if (!isSdpPath_(filePath)) {
            return nullptr;
        }
        SwMediaOpenOptions directOptions = options;
        directOptions.sdpFile = filePath;
        SwSdpMediaDescription description;
        if (!loadSdpDescription_(directOptions, description)) {
            return nullptr;
        }
        return createDirectRtpSource_(directOptions, description);
    }

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
#if defined(_WIN32)
        static const std::array<const char*, 11> kContainerExtensions = {{
            ".mp4", ".mov", ".m4v", ".mkv", ".avi", ".wmv",
            ".asf", ".mpg", ".mpeg", ".webm", ".ts"
        }};
        return std::find(kContainerExtensions.begin(),
                         kContainerExtensions.end(),
                         extension) != kContainerExtensions.end();
#else
        // The native MP4 demuxer behind SwMp4MovieSource only handles the BMFF family.
        // Sniff the header too: raw Annex-B elementary streams merely named .mp4 keep
        // going through SwFileVideoSource, as they did before containers were supported.
        static const std::array<const char*, 3> kContainerExtensions = {{
            ".mp4", ".mov", ".m4v"
        }};
        if (std::find(kContainerExtensions.begin(),
                      kContainerExtensions.end(),
                      extension) == kContainerExtensions.end()) {
            return false;
        }
        return SwMp4Demuxer::looksLikeBmff(filePath.toStdString());
#endif
    }

    static std::shared_ptr<SwMediaSource> createFileMediaSource_(const SwMediaOpenOptions& options) {
        const SwString filePath = localFilePath_(options);
        if (shouldUsePlatformFileSource_(options, filePath)) {
#if defined(_WIN32)
            return std::make_shared<SwPlatformMovieSource>(filePath.toStdWString());
#else
            return std::make_shared<SwPlatformMovieSource>(filePath.toStdString());
#endif
        }
        return std::make_shared<SwFileVideoSource>(filePath.toStdString(),
                                                   options.codec);
    }
};
