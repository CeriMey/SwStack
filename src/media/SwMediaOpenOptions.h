#pragma once

/**
 * @file src/media/SwMediaOpenOptions.h
 * @ingroup media
 * @brief Declares normalized media-opening options used by generic source factories.
 */

#include "media/SwMediaUrl.h"
#include "media/SwVideoPacket.h"

#include <initializer_list>

struct SwMediaOpenOptions {
    enum class TransportPreference {
        Auto,
        Udp,
        Tcp
    };

    enum class UdpPayloadFormat {
        Auto,
        Rtp,
        MpegTs,
        AnnexBH264,
        AnnexBH265
    };

    SwMediaUrl mediaUrl{};
    SwString bindAddress{};
    uint16_t rtpPort{0};
    uint16_t rtcpPort{0};
    TransportPreference transport{TransportPreference::Auto};
    UdpPayloadFormat udpFormat{UdpPayloadFormat::Auto};
    SwVideoPacket::Codec codec{SwVideoPacket::Codec::Unknown};
    int payloadType{-1};
    int clockRate{0};
    SwString fmtp{};
    SwString decoderId{};
    SwString audioDecoderId{};
    SwString preferredAudioTrackId{};
    SwString preferredVideoTrackId{};
    SwString audioDeviceId{};
    SwString sourceAddressFilter{};
    uint16_t sourceRtcpPort{0};
    bool lowLatency{true};
    bool enableAudio{false};
    bool enableMetadata{false};

    SwString sourceUrl() const {
        const SwString scheme = mediaUrl.scheme();
        if (scheme.isEmpty()) {
            return mediaUrl.toString();
        }

        SwString result = scheme + "://";
        if (!mediaUrl.userInfo().isEmpty()) {
            result += mediaUrl.userInfo();
            result += "@";
        }
        result += mediaUrl.host();
        if (mediaUrl.port() >= 0) {
            result += ":";
            result += SwString::number(mediaUrl.port());
        }
        result += mediaUrl.path().isEmpty() ? SwString("/") : mediaUrl.path();

        SwString filteredQuery;
        for (auto it = mediaUrl.queryItems().begin(); it != mediaUrl.queryItems().end(); ++it) {
            const SwString key = it.key();
            if (isLocalOptionKey_(key)) {
                continue;
            }
            if (!filteredQuery.isEmpty()) {
                filteredQuery += "&";
            }
            filteredQuery += key;
            if (!it.value().isEmpty()) {
                filteredQuery += "=";
                filteredQuery += it.value();
            }
        }
        if (!filteredQuery.isEmpty()) {
            result += "?";
            result += filteredQuery;
        }
        return result;
    }

    static SwMediaOpenOptions fromUrl(const SwString& rawUrl) {
        SwMediaOpenOptions options;
        options.mediaUrl = SwMediaUrl::parse(rawUrl);
        options.transport = transportFromString(firstQueryValue_(options.mediaUrl,
                                                                 {"transport"}));
        options.udpFormat = udpFormatFromString(firstQueryValue_(options.mediaUrl,
                                                                 {"format"}));
        options.codec = codecFromString(firstQueryValue_(options.mediaUrl,
                                                         {"codec"}));
        options.payloadType = queryInt_(options.mediaUrl, {"pt", "payload", "payloadtype"}, -1);
        options.clockRate = queryInt_(options.mediaUrl, {"clock", "clockrate"}, 0);
        options.bindAddress = firstQueryValue_(options.mediaUrl,
                                               {"bind", "local", "listen", "local-address"});
        options.rtpPort = static_cast<uint16_t>(
            queryInt_(options.mediaUrl, {"local_rtp", "rtp_port", "localport"}, 0));
        options.rtcpPort = static_cast<uint16_t>(
            queryInt_(options.mediaUrl, {"local_rtcp", "rtcp_port"}, 0));
        options.sourceAddressFilter = firstQueryValue_(options.mediaUrl,
                                                       {"source", "source-address", "remote"});
        options.sourceRtcpPort = static_cast<uint16_t>(
            queryInt_(options.mediaUrl, {"source_rtcp", "remote_rtcp", "source-rtcp"}, 0));
        options.fmtp = firstQueryValue_(options.mediaUrl, {"fmtp"});
        options.decoderId = firstQueryValue_(options.mediaUrl, {"decoder"});
        options.audioDecoderId = firstQueryValue_(options.mediaUrl, {"audio_decoder", "audio-decoder"});
        options.preferredAudioTrackId = firstQueryValue_(options.mediaUrl, {"audio_track", "audio-track"});
        options.preferredVideoTrackId = firstQueryValue_(options.mediaUrl, {"video_track", "video-track"});
        options.audioDeviceId = firstQueryValue_(options.mediaUrl, {"audio_device", "audio-device"});
        options.lowLatency = queryBool_(options.mediaUrl, {"lowlatency", "low_latency"}, true);
        options.enableAudio = queryBool_(options.mediaUrl, {"audio", "enable_audio", "enable-audio"}, false);
        options.enableMetadata =
            queryBool_(options.mediaUrl, {"metadata", "enable_metadata", "enable-metadata", "klv"}, false);

        if ((options.mediaUrl.scheme() == "rtp") &&
            options.udpFormat == UdpPayloadFormat::Auto) {
            options.udpFormat = UdpPayloadFormat::Rtp;
        }
        if ((options.mediaUrl.scheme() == "udp") &&
            options.udpFormat == UdpPayloadFormat::Auto) {
            options.udpFormat = UdpPayloadFormat::MpegTs;
        }
        if (options.udpFormat == UdpPayloadFormat::AnnexBH264) {
            options.codec = SwVideoPacket::Codec::H264;
        } else if (options.udpFormat == UdpPayloadFormat::AnnexBH265) {
            options.codec = SwVideoPacket::Codec::H265;
        } else if (options.codec == SwVideoPacket::Codec::Unknown &&
                   options.mediaUrl.scheme() == "rtp") {
            options.codec = SwVideoPacket::Codec::H264;
        }
        if (options.sourceAddressFilter.isEmpty() &&
            (options.mediaUrl.scheme() == "rtp" || options.mediaUrl.scheme() == "udp") &&
            !isWildcardHost_(options.mediaUrl.host())) {
            options.sourceAddressFilter = options.mediaUrl.host();
        }
        return options;
    }

    static TransportPreference transportFromString(const SwString& value) {
        const SwString normalized = value.trimmed().toLower();
        if (normalized == "udp") {
            return TransportPreference::Udp;
        }
        if (normalized == "tcp") {
            return TransportPreference::Tcp;
        }
        return TransportPreference::Auto;
    }

    static UdpPayloadFormat udpFormatFromString(const SwString& value) {
        const SwString normalized = value.trimmed().toLower();
        if (normalized == "rtp") {
            return UdpPayloadFormat::Rtp;
        }
        if (normalized == "mpegts" || normalized == "mp2t" || normalized == "ts") {
            return UdpPayloadFormat::MpegTs;
        }
        if (normalized == "annexb-h264" || normalized == "h264-annexb" || normalized == "h264") {
            return UdpPayloadFormat::AnnexBH264;
        }
        if (normalized == "annexb-h265" || normalized == "annexb-hevc" ||
            normalized == "h265-annexb" || normalized == "hevc-annexb" ||
            normalized == "h265" || normalized == "hevc") {
            return UdpPayloadFormat::AnnexBH265;
        }
        return UdpPayloadFormat::Auto;
    }

    static SwVideoPacket::Codec codecFromString(const SwString& value) {
        const SwString normalized = value.trimmed().toLower();
        if (normalized == "h264" || normalized == "avc") {
            return SwVideoPacket::Codec::H264;
        }
        if (normalized == "h265" || normalized == "hevc") {
            return SwVideoPacket::Codec::H265;
        }
        if (normalized == "vp8") {
            return SwVideoPacket::Codec::VP8;
        }
        if (normalized == "vp9") {
            return SwVideoPacket::Codec::VP9;
        }
        if (normalized == "av1") {
            return SwVideoPacket::Codec::AV1;
        }
        if (normalized == "mjpeg" || normalized == "jpeg" || normalized == "motionjpeg") {
            return SwVideoPacket::Codec::MotionJPEG;
        }
        return SwVideoPacket::Codec::Unknown;
    }

    static SwString codecToString(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return "h264";
        case SwVideoPacket::Codec::H265:
            return "h265";
        case SwVideoPacket::Codec::VP8:
            return "vp8";
        case SwVideoPacket::Codec::VP9:
            return "vp9";
        case SwVideoPacket::Codec::AV1:
            return "av1";
        case SwVideoPacket::Codec::MotionJPEG:
            return "mjpeg";
        default:
            return "unknown";
        }
    }

private:
    static SwString firstQueryValue_(const SwMediaUrl& url,
                                     std::initializer_list<const char*> keys) {
        for (const char* key : keys) {
            const SwString value = url.queryValue(SwString(key));
            if (!value.isEmpty()) {
                return value;
            }
        }
        return SwString();
    }

    static int queryInt_(const SwMediaUrl& url,
                         std::initializer_list<const char*> keys,
                         int fallback) {
        const SwString value = firstQueryValue_(url, keys);
        if (value.isEmpty()) {
            return fallback;
        }
        bool ok = false;
        const int parsed = value.toInt(&ok);
        return ok ? parsed : fallback;
    }

    static bool queryBool_(const SwMediaUrl& url,
                           std::initializer_list<const char*> keys,
                           bool fallback) {
        const SwString value = firstQueryValue_(url, keys).trimmed().toLower();
        if (value.isEmpty()) {
            return fallback;
        }
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "0" || value == "false" || value == "no" || value == "off") {
            return false;
        }
        return fallback;
    }

    static bool isWildcardHost_(const SwString& host) {
        const SwString normalized = host.trimmed().toLower();
        return normalized.isEmpty() || normalized == "*" || normalized == "0.0.0.0";
    }

    static bool isLocalOptionKey_(const SwString& rawKey) {
        const SwString key = rawKey.trimmed().toLower();
        return key == "transport" ||
               key == "format" ||
               key == "codec" ||
               key == "pt" ||
               key == "payload" ||
               key == "payloadtype" ||
               key == "clock" ||
               key == "clockrate" ||
               key == "bind" ||
               key == "local" ||
               key == "listen" ||
               key == "local-address" ||
               key == "local_rtp" ||
               key == "rtp_port" ||
               key == "localport" ||
               key == "local_rtcp" ||
               key == "rtcp_port" ||
               key == "source" ||
               key == "source-address" ||
               key == "remote" ||
               key == "source_rtcp" ||
               key == "remote_rtcp" ||
               key == "source-rtcp" ||
               key == "fmtp" ||
               key == "decoder" ||
               key == "audio_decoder" ||
               key == "audio-decoder" ||
               key == "audio_track" ||
               key == "audio-track" ||
               key == "video_track" ||
               key == "video-track" ||
               key == "audio_device" ||
               key == "audio-device" ||
               key == "lowlatency" ||
               key == "low_latency" ||
               key == "audio" ||
               key == "enable_audio" ||
               key == "enable-audio" ||
               key == "metadata" ||
               key == "enable_metadata" ||
               key == "enable-metadata" ||
               key == "klv";
    }
};
