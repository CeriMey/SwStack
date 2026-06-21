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
    SwString userName{};
    SwString password{};
    SwString trustedCaFile{};
    SwString sourceAddressFilter{};
    SwString multicastGroup{};
    uint16_t sourceRtcpPort{0};
    SwString sdpText{};
    SwString sdpFile{};
    int latencyTargetMs{0};
    int rtpJitterDelayMs{0};
    int rtpJitterMaxPackets{0};
    bool lowLatency{true};
    bool enableAudio{false};
    bool enableMetadata{false};
    bool udpPunch{true};
    bool transportExplicit{false};
    bool udpFormatExplicit{false};
    bool codecExplicit{false};
    bool payloadTypeExplicit{false};
    bool clockRateExplicit{false};
    bool latencyTargetExplicit{false};
    bool rtpJitterDelayExplicit{false};
    bool rtpJitterMaxPacketsExplicit{false};
    bool rtpPacketized{false};
    bool sdpApplied{false};

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
        result += mediaUrl.authority();
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
        options.transportExplicit =
            hasAnyQueryValue_(options.mediaUrl, {"transport", "rtsp_transport", "rtsp-transport"});
        options.udpFormatExplicit = hasAnyQueryValue_(options.mediaUrl, {"format"});
        options.codecExplicit = hasAnyQueryValue_(options.mediaUrl, {"codec"});
        options.payloadTypeExplicit =
            hasAnyQueryValue_(options.mediaUrl, {"pt", "payload", "payloadtype"});
        options.clockRateExplicit =
            hasAnyQueryValue_(options.mediaUrl, {"clock", "clockrate"});
        options.latencyTargetExplicit =
            hasAnyQueryValue_(options.mediaUrl, {"latency",
                                                 "latency_ms",
                                                 "latency-ms",
                                                 "latency_target",
                                                 "latency-target",
                                                 "latency_target_ms",
                                                 "latency-target-ms"});
        options.rtpJitterDelayExplicit =
            hasAnyQueryValue_(options.mediaUrl, {"jitter",
                                                 "jitter_ms",
                                                 "jitter-ms",
                                                 "rtp_jitter",
                                                 "rtp-jitter",
                                                 "rtp_jitter_ms",
                                                 "rtp-jitter-ms",
                                                 "reorder_delay",
                                                 "reorder-delay"});
        options.rtpJitterMaxPacketsExplicit =
            hasAnyQueryValue_(options.mediaUrl, {"jitter_packets",
                                                 "jitter-packets",
                                                 "rtp_jitter_packets",
                                                 "rtp-jitter-packets"});
        options.transport = transportFromString(firstQueryValue_(options.mediaUrl,
                                                                 {"transport",
                                                                  "rtsp_transport",
                                                                  "rtsp-transport"}));
        options.udpFormat = udpFormatFromString(firstQueryValue_(options.mediaUrl,
                                                                 {"format"}));
        options.codec = codecFromString(firstQueryValue_(options.mediaUrl,
                                                         {"codec"}));
        options.payloadType = queryInt_(options.mediaUrl, {"pt", "payload", "payloadtype"}, -1);
        options.clockRate = queryInt_(options.mediaUrl, {"clock", "clockrate"}, 0);
        options.latencyTargetMs = queryInt_(options.mediaUrl,
                                            {"latency",
                                             "latency_ms",
                                             "latency-ms",
                                             "latency_target",
                                             "latency-target",
                                             "latency_target_ms",
                                             "latency-target-ms"},
                                            0);
        options.rtpJitterDelayMs = queryInt_(options.mediaUrl,
                                             {"jitter",
                                              "jitter_ms",
                                              "jitter-ms",
                                              "rtp_jitter",
                                              "rtp-jitter",
                                              "rtp_jitter_ms",
                                              "rtp-jitter-ms",
                                              "reorder_delay",
                                              "reorder-delay"},
                                             0);
        options.rtpJitterMaxPackets = queryInt_(options.mediaUrl,
                                                {"jitter_packets",
                                                 "jitter-packets",
                                                 "rtp_jitter_packets",
                                                 "rtp-jitter-packets"},
                                                0);
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
        options.sdpText = firstQueryValue_(options.mediaUrl,
                                           {"sdp", "sdp_text", "sdp-text"});
        options.sdpFile = firstQueryValue_(options.mediaUrl,
                                           {"sdp_file", "sdp-file", "sdp_path", "sdp-path"});
        options.decoderId = firstQueryValue_(options.mediaUrl, {"decoder"});
        options.audioDecoderId = firstQueryValue_(options.mediaUrl, {"audio_decoder", "audio-decoder"});
        options.preferredAudioTrackId = firstQueryValue_(options.mediaUrl, {"audio_track", "audio-track"});
        options.preferredVideoTrackId = firstQueryValue_(options.mediaUrl, {"video_track", "video-track"});
        options.audioDeviceId = firstQueryValue_(options.mediaUrl, {"audio_device", "audio-device"});
        options.userName = firstQueryValue_(options.mediaUrl, {"username", "user"});
        options.password = firstQueryValue_(options.mediaUrl, {"password", "pass"});
        options.trustedCaFile = firstQueryValue_(options.mediaUrl,
                                                 {"trusted_ca", "trusted-ca", "ca_file", "ca-file"});
        options.lowLatency = queryBool_(options.mediaUrl, {"lowlatency", "low_latency"}, true);
        options.enableAudio = queryBool_(options.mediaUrl, {"audio", "enable_audio", "enable-audio"}, false);
        options.enableMetadata =
            queryBool_(options.mediaUrl, {"metadata", "enable_metadata", "enable-metadata", "klv"}, false);
        options.udpPunch =
            queryBool_(options.mediaUrl, {"udp_punch", "udp-punch", "rtsp_udp_punch", "rtsp-udp-punch"}, true);
        if ((!options.userName.isEmpty() || !options.password.isEmpty()) &&
            options.mediaUrl.userInfo().isEmpty()) {
            return finalizeDerived_(options);
        }
        splitUserInfo_(options.mediaUrl.userInfo(), options.userName, options.password);
        return finalizeDerived_(options);
    }

private:
    static SwMediaOpenOptions finalizeDerived_(SwMediaOpenOptions options) {
        if ((options.mediaUrl.scheme() == "rtp") &&
            options.udpFormat == UdpPayloadFormat::Auto) {
            options.udpFormat = UdpPayloadFormat::Rtp;
        }
        if (options.mediaUrl.scheme() == "rtp" ||
            options.udpFormat == UdpPayloadFormat::Rtp) {
            options.rtpPacketized = true;
        }
        if (options.udpFormat == UdpPayloadFormat::AnnexBH264) {
            options.codec = SwVideoPacket::Codec::H264;
        } else if (options.udpFormat == UdpPayloadFormat::AnnexBH265) {
            options.codec = SwVideoPacket::Codec::H265;
        } else if (options.codec == SwVideoPacket::Codec::Unknown &&
                   options.mediaUrl.scheme() == "rtp") {
            options.codec = SwVideoPacket::Codec::H264;
        }
        if ((options.mediaUrl.scheme() == "rtp" || options.mediaUrl.scheme() == "udp") &&
            isMulticastHost_(options.mediaUrl.host())) {
            options.multicastGroup = options.mediaUrl.host();
        }
        if (options.sourceAddressFilter.isEmpty() &&
            options.multicastGroup.isEmpty() &&
            (options.mediaUrl.scheme() == "rtp" || options.mediaUrl.scheme() == "udp") &&
            !isWildcardHost_(options.mediaUrl.host())) {
            options.sourceAddressFilter = options.mediaUrl.host();
        }
        return options;
    }

public:
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

    static bool hasAnyQueryValue_(const SwMediaUrl& url,
                                  std::initializer_list<const char*> keys) {
        for (const char* key : keys) {
            if (url.hasQueryValue(SwString(key))) {
                return true;
            }
        }
        return false;
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
        return normalized.isEmpty() || normalized == "*" || normalized == "0.0.0.0" ||
               normalized == "::";
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

    static void splitUserInfo_(const SwString& userInfo,
                               SwString& outUserName,
                               SwString& outPassword) {
        if (userInfo.isEmpty()) {
            return;
        }
        const int colon = userInfo.indexOf(":");
        if (colon < 0) {
            if (outUserName.isEmpty()) {
                outUserName = userInfo;
            }
            return;
        }
        if (outUserName.isEmpty()) {
            outUserName = userInfo.left(colon);
        }
        if (outPassword.isEmpty()) {
            outPassword = userInfo.mid(colon + 1);
        }
    }

    static bool isLocalOptionKey_(const SwString& rawKey) {
        const SwString key = rawKey.trimmed().toLower();
        return key == "transport" ||
               key == "rtsp_transport" ||
               key == "rtsp-transport" ||
               key == "format" ||
               key == "codec" ||
               key == "pt" ||
               key == "payload" ||
               key == "payloadtype" ||
               key == "clock" ||
               key == "clockrate" ||
               key == "latency" ||
               key == "latency_ms" ||
               key == "latency-ms" ||
               key == "latency_target" ||
               key == "latency-target" ||
               key == "latency_target_ms" ||
               key == "latency-target-ms" ||
               key == "jitter" ||
               key == "jitter_ms" ||
               key == "jitter-ms" ||
               key == "rtp_jitter" ||
               key == "rtp-jitter" ||
               key == "rtp_jitter_ms" ||
               key == "rtp-jitter-ms" ||
               key == "reorder_delay" ||
               key == "reorder-delay" ||
               key == "jitter_packets" ||
               key == "jitter-packets" ||
               key == "rtp_jitter_packets" ||
               key == "rtp-jitter-packets" ||
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
               key == "sdp" ||
               key == "sdp_text" ||
               key == "sdp-text" ||
               key == "sdp_file" ||
               key == "sdp-file" ||
               key == "sdp_path" ||
               key == "sdp-path" ||
               key == "decoder" ||
               key == "audio_decoder" ||
               key == "audio-decoder" ||
               key == "audio_track" ||
               key == "audio-track" ||
               key == "video_track" ||
               key == "video-track" ||
               key == "audio_device" ||
               key == "audio-device" ||
               key == "username" ||
               key == "user" ||
               key == "password" ||
               key == "pass" ||
               key == "trusted_ca" ||
               key == "trusted-ca" ||
               key == "ca_file" ||
               key == "ca-file" ||
               key == "lowlatency" ||
               key == "low_latency" ||
               key == "audio" ||
               key == "enable_audio" ||
               key == "enable-audio" ||
               key == "metadata" ||
               key == "enable_metadata" ||
               key == "enable-metadata" ||
               key == "klv" ||
               key == "udp_punch" ||
               key == "udp-punch" ||
               key == "rtsp_udp_punch" ||
               key == "rtsp-udp-punch";
    }
};
