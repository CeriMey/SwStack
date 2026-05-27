#pragma once

/**
 * @file src/media/SwSdpMediaDescription.h
 * @ingroup media
 * @brief Declares a small SDP parser used by direct RTP/UDP media sources.
 */

#include "media/SwMediaOpenOptions.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace SwSdpMediaDescription_detail {

inline std::string toLowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

inline void trim(std::string& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
}

inline std::string trimmed(std::string value) {
    trim(value);
    return value;
}

inline std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::string current;
    for (char ch : value) {
        if (ch == delimiter) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        result.push_back(current);
    }
    return result;
}

inline int parseInt(const std::string& value, int fallback = 0) {
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

inline int parseMediaPort(const std::string& value) {
    const std::size_t slash = value.find('/');
    return parseInt(slash == std::string::npos ? value : value.substr(0, slash), 0);
}

inline std::string parseConnectionAddress(const std::string& line) {
    std::vector<std::string> parts = split(line, ' ');
    if (parts.size() < 3U) {
        return std::string();
    }
    std::string address = parts[2];
    const std::size_t slash = address.find('/');
    if (slash != std::string::npos) {
        address = address.substr(0, slash);
    }
    return address;
}

inline bool isWildcardAddress(const std::string& value) {
    const std::string normalized = toLowerAscii(trimmed(value));
    return normalized.empty() || normalized == "*" || normalized == "0.0.0.0" ||
           normalized == "::";
}

inline bool isIpv4MulticastAddress(const std::string& value) {
    if (value.empty() || value.find(':') != std::string::npos) {
        return false;
    }
    const std::size_t dot = value.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    const int firstOctet = parseInt(value.substr(0, dot), -1);
    return firstOctet >= 224 && firstOctet <= 239;
}

inline bool isIpv6MulticastAddress(const std::string& value) {
    const std::string normalized = toLowerAscii(trimmed(value));
    return normalized.size() >= 2U && normalized[0] == 'f' && normalized[1] == 'f';
}

inline bool isMulticastAddress(const std::string& value) {
    return isIpv4MulticastAddress(value) || isIpv6MulticastAddress(value);
}

inline std::string normalizeCodecName(const std::string& codecName) {
    std::string codec = toLowerAscii(trimmed(codecName));
    if (codec == "hevc" || codec == "h265" || codec == "hvc1" || codec == "hev1") {
        return "h265";
    }
    if (codec == "avc" || codec == "avc1") {
        return "h264";
    }
    if (codec == "mp2t" || codec == "mpegts" || codec == "mpeg-ts") {
        return "mp2t";
    }
    return codec;
}

} // namespace SwSdpMediaDescription_detail

struct SwSdpMediaTrackDescription {
    std::string mediaType{};
    int port{0};
    int rtcpPort{0};
    std::string protocol{};
    int payloadType{-1};
    std::string codecName{};
    int clockRate{0};
    int channelCount{0};
    std::string fmtp{};
    std::string control{};
    std::string connectionAddress{};
    std::string rtcpAddress{};
    std::string sourceAddressFilter{};
    std::uint32_t ssrc{0};
    double frameRate{0.0};
    bool recvOnly{false};
    bool sendOnly{false};
    bool inactive{false};

    bool isVideo() const { return mediaType == "video"; }
    bool isAudio() const { return mediaType == "audio"; }
    bool isMetadata() const { return mediaType == "application"; }

    bool isRtpPacketized() const {
        const std::string protocolText = SwSdpMediaDescription_detail::toLowerAscii(protocol);
        return protocolText.find("rtp/") != std::string::npos ||
               protocolText.find("/rtp") != std::string::npos ||
               protocolText == "rtp";
    }

    bool isMpegTsPayload() const {
        const std::string codec = SwSdpMediaDescription_detail::normalizeCodecName(codecName);
        return payloadType == 33 || codec == "mp2t" || codec == "mpegts";
    }
};

namespace SwSdpMediaDescription_detail {

inline int videoTrackScore(const SwSdpMediaTrackDescription& track) {
    if (!track.isVideo()) {
        return -1;
    }
    const std::string codec = normalizeCodecName(track.codecName);
    if (codec == "h264") {
        return 400;
    }
    if (codec == "h265") {
        return 350;
    }
    if (codec == "mp2t" || track.payloadType == 33) {
        return 300;
    }
    return -1;
}

} // namespace SwSdpMediaDescription_detail

class SwSdpMediaDescription {
public:
    std::string sessionConnectionAddress{};
    std::string sessionSourceAddressFilter{};
    std::string sessionControl{};
    std::vector<SwSdpMediaTrackDescription> tracks{};

    bool isValid() const { return !tracks.empty(); }

    static bool parse(const std::string& text, SwSdpMediaDescription& outDescription) {
        outDescription = SwSdpMediaDescription();
        std::istringstream stream(text);
        std::string line;
        SwSdpMediaTrackDescription currentTrack;
        bool haveTrack = false;

        auto finalizeTrack = [&]() {
            if (!haveTrack) {
                return;
            }
            currentTrack.mediaType =
                SwSdpMediaDescription_detail::toLowerAscii(currentTrack.mediaType);
            currentTrack.protocol =
                SwSdpMediaDescription_detail::toLowerAscii(currentTrack.protocol);
            currentTrack.codecName =
                SwSdpMediaDescription_detail::normalizeCodecName(currentTrack.codecName);
            if (currentTrack.connectionAddress.empty()) {
                currentTrack.connectionAddress = outDescription.sessionConnectionAddress;
            }
            if (currentTrack.sourceAddressFilter.empty()) {
                currentTrack.sourceAddressFilter = outDescription.sessionSourceAddressFilter;
            }
            if (currentTrack.codecName.empty() && currentTrack.payloadType == 33) {
                currentTrack.codecName = "mp2t";
                currentTrack.clockRate = 90000;
            }
            outDescription.tracks.push_back(currentTrack);
            currentTrack = SwSdpMediaTrackDescription();
            haveTrack = false;
        };

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            SwSdpMediaDescription_detail::trim(line);
            if (line.empty()) {
                continue;
            }

            if (line.rfind("c=", 0) == 0) {
                const std::string address =
                    SwSdpMediaDescription_detail::parseConnectionAddress(line.substr(2));
                if (haveTrack) {
                    currentTrack.connectionAddress = address;
                } else {
                    outDescription.sessionConnectionAddress = address;
                }
                continue;
            }

            if (!haveTrack && line.rfind("a=control:", 0) == 0) {
                outDescription.sessionControl = line.substr(std::strlen("a=control:"));
                continue;
            }

            if (line.rfind("a=source-filter:", 0) == 0) {
                std::string sourceAddress = parseSourceFilterAddress_(
                    line.substr(std::strlen("a=source-filter:")));
                if (haveTrack) {
                    currentTrack.sourceAddressFilter = sourceAddress;
                } else {
                    outDescription.sessionSourceAddressFilter = sourceAddress;
                }
                continue;
            }

            if (line.rfind("m=", 0) == 0) {
                finalizeTrack();
                const std::vector<std::string> parts =
                    SwSdpMediaDescription_detail::split(line, ' ');
                if (parts.size() < 4U || parts[0].size() <= 2U) {
                    continue;
                }
                haveTrack = true;
                currentTrack.mediaType = parts[0].substr(2);
                currentTrack.port = SwSdpMediaDescription_detail::parseMediaPort(parts[1]);
                currentTrack.protocol = parts[2];
                currentTrack.payloadType =
                    SwSdpMediaDescription_detail::parseInt(parts[3], -1);
                if (currentTrack.payloadType == 33) {
                    currentTrack.clockRate = 90000;
                    currentTrack.codecName = "mp2t";
                }
                continue;
            }

            if (!haveTrack) {
                continue;
            }

            if (line.rfind("a=control:", 0) == 0) {
                currentTrack.control = line.substr(std::strlen("a=control:"));
                continue;
            }

            if (line == "a=recvonly") {
                currentTrack.recvOnly = true;
                continue;
            }
            if (line == "a=sendonly") {
                currentTrack.sendOnly = true;
                continue;
            }
            if (line == "a=inactive") {
                currentTrack.inactive = true;
                continue;
            }

            if (line.rfind("a=rtcp:", 0) == 0) {
                parseRtcpAttribute_(line.substr(std::strlen("a=rtcp:")),
                                    currentTrack.rtcpPort,
                                    currentTrack.rtcpAddress);
                continue;
            }

            if (line.rfind("a=ssrc:", 0) == 0) {
                currentTrack.ssrc = parseSsrc_(line.substr(std::strlen("a=ssrc:")));
                continue;
            }

            if (line.rfind("a=framerate:", 0) == 0) {
                currentTrack.frameRate =
                    std::atof(line.substr(std::strlen("a=framerate:")).c_str());
                continue;
            }

            if (line.rfind("a=rtpmap:", 0) == 0) {
                const std::size_t valueOffset = std::strlen("a=rtpmap:");
                const std::size_t space = line.find(' ', valueOffset);
                if (space == std::string::npos) {
                    continue;
                }
                const int payload = SwSdpMediaDescription_detail::parseInt(
                    line.substr(valueOffset, space - valueOffset),
                    -1);
                if (payload != currentTrack.payloadType && !currentTrack.codecName.empty()) {
                    continue;
                }
                std::string codecPart = line.substr(space + 1);
                SwSdpMediaDescription_detail::trim(codecPart);
                const std::size_t slash = codecPart.find('/');
                if (slash == std::string::npos) {
                    continue;
                }
                currentTrack.payloadType = payload;
                currentTrack.codecName = codecPart.substr(0, slash);
                const std::string remaining = codecPart.substr(slash + 1);
                const std::size_t nextSlash = remaining.find('/');
                currentTrack.clockRate = SwSdpMediaDescription_detail::parseInt(
                    nextSlash == std::string::npos ? remaining : remaining.substr(0, nextSlash),
                    0);
                if (nextSlash != std::string::npos) {
                    currentTrack.channelCount = SwSdpMediaDescription_detail::parseInt(
                        remaining.substr(nextSlash + 1),
                        0);
                }
                continue;
            }

            if (line.rfind("a=fmtp:", 0) == 0) {
                std::string raw = line.substr(std::strlen("a=fmtp:"));
                SwSdpMediaDescription_detail::trim(raw);
                const std::size_t space = raw.find(' ');
                int payload = currentTrack.payloadType;
                std::string body = raw;
                if (space != std::string::npos) {
                    payload = SwSdpMediaDescription_detail::parseInt(raw.substr(0, space), -1);
                    body = raw.substr(space + 1);
                    SwSdpMediaDescription_detail::trim(body);
                }
                if (payload == currentTrack.payloadType || currentTrack.fmtp.empty()) {
                    currentTrack.fmtp = body;
                }
                continue;
            }
        }

        finalizeTrack();
        return outDescription.isValid();
    }

    static bool loadTextFromFile(const SwString& path, std::string& outText) {
        outText.clear();
        if (path.isEmpty()) {
            return false;
        }
        std::ifstream file(path.toStdString().c_str(), std::ios::in | std::ios::binary);
        if (!file) {
            return false;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        outText = buffer.str();
        return !outText.empty();
    }

    const SwSdpMediaTrackDescription* bestVideoTrack() const {
        const SwSdpMediaTrackDescription* bestTrack = nullptr;
        int bestScore = -1;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const int score = SwSdpMediaDescription_detail::videoTrackScore(tracks[i]);
            if (score > bestScore) {
                bestScore = score;
                bestTrack = &tracks[i];
            }
        }
        return bestTrack;
    }

    bool applyToOpenOptions(SwMediaOpenOptions& options) const {
        const SwSdpMediaTrackDescription* selectedTrack = bestVideoTrack();
        if (!selectedTrack) {
            return false;
        }

        options.sdpApplied = true;
        if (selectedTrack->isRtpPacketized()) {
            options.rtpPacketized = true;
        }

        if (!options.payloadTypeExplicit && selectedTrack->payloadType >= 0) {
            options.payloadType = selectedTrack->payloadType;
        }
        if (!options.clockRateExplicit && selectedTrack->clockRate > 0) {
            options.clockRate = selectedTrack->clockRate;
        }
        if (!options.codecExplicit && !selectedTrack->codecName.empty() &&
            !selectedTrack->isMpegTsPayload()) {
            options.codec =
                SwMediaOpenOptions::codecFromString(SwString(selectedTrack->codecName));
        }
        if (options.fmtp.isEmpty() && !selectedTrack->fmtp.empty()) {
            options.fmtp = SwString(selectedTrack->fmtp);
        }

        if (!options.udpFormatExplicit) {
            if (selectedTrack->isRtpPacketized()) {
                options.udpFormat = selectedTrack->isMpegTsPayload()
                                        ? SwMediaOpenOptions::UdpPayloadFormat::MpegTs
                                        : SwMediaOpenOptions::UdpPayloadFormat::Rtp;
            } else if (selectedTrack->isMpegTsPayload()) {
                options.udpFormat = SwMediaOpenOptions::UdpPayloadFormat::MpegTs;
            } else if (SwSdpMediaDescription_detail::normalizeCodecName(selectedTrack->codecName) ==
                       "h265") {
                options.udpFormat = SwMediaOpenOptions::UdpPayloadFormat::AnnexBH265;
            } else if (SwSdpMediaDescription_detail::normalizeCodecName(selectedTrack->codecName) ==
                       "h264") {
                options.udpFormat = SwMediaOpenOptions::UdpPayloadFormat::AnnexBH264;
            }
        }

        if (options.rtpPort == 0 && options.mediaUrl.port() <= 0 && selectedTrack->port > 0) {
            options.rtpPort = static_cast<uint16_t>(selectedTrack->port);
        }
        if (options.rtcpPort == 0 && options.rtpPort != 0 && selectedTrack->isRtpPacketized()) {
            options.rtcpPort = selectedTrack->rtcpPort > 0
                                   ? static_cast<uint16_t>(selectedTrack->rtcpPort)
                                   : static_cast<uint16_t>(options.rtpPort + 1);
        }

        if (!selectedTrack->connectionAddress.empty() &&
            SwSdpMediaDescription_detail::isMulticastAddress(selectedTrack->connectionAddress)) {
            options.multicastGroup = SwString(selectedTrack->connectionAddress);
        }
        if (options.sourceAddressFilter.isEmpty() &&
            !selectedTrack->sourceAddressFilter.empty()) {
            options.sourceAddressFilter = SwString(selectedTrack->sourceAddressFilter);
        }
        return true;
    }

private:
    static void parseRtcpAttribute_(const std::string& value,
                                    int& rtcpPort,
                                    std::string& rtcpAddress) {
        std::vector<std::string> parts = SwSdpMediaDescription_detail::split(value, ' ');
        if (parts.empty()) {
            return;
        }
        rtcpPort = SwSdpMediaDescription_detail::parseInt(parts[0], 0);
        if (parts.size() >= 4U) {
            rtcpAddress = parts[3];
        }
    }

    static std::uint32_t parseSsrc_(const std::string& value) {
        std::string text = SwSdpMediaDescription_detail::trimmed(value);
        const std::size_t delimiter = text.find_first_of(" \t:");
        if (delimiter != std::string::npos) {
            text = text.substr(0, delimiter);
        }
        return static_cast<std::uint32_t>(
            SwSdpMediaDescription_detail::parseInt(text, 0));
    }

    static std::string parseSourceFilterAddress_(const std::string& value) {
        std::vector<std::string> parts =
            SwSdpMediaDescription_detail::split(SwSdpMediaDescription_detail::trimmed(value), ' ');
        if (parts.size() < 5U) {
            return std::string();
        }
        if (SwSdpMediaDescription_detail::toLowerAscii(parts[0]) != "incl") {
            return std::string();
        }
        return parts.back();
    }
};
