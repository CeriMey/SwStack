#pragma once

/**
 * @file src/media/rtsp/SwRtspHeaderUtils.h
 * @ingroup media
 * @brief Small header-only helpers for RTSP header parsing and normalization.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

struct SwRtspTransportInfo {
    uint16_t serverRtpPort{0};
    uint16_t serverRtcpPort{0};
    uint8_t interleavedRtpChannel{0};
    uint8_t interleavedRtcpChannel{1};
};

struct SwRtspSessionInfo {
    std::string sessionId{};
    int timeoutSeconds{0};
};

namespace SwRtspHeaderUtils {

inline std::string toLowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

inline std::vector<std::string> headerValues(const std::string& header, const char* key) {
    std::vector<std::string> values;
    if (!key || !*key) {
        return values;
    }
    const std::string keyLower = toLowerCopy(key);
    std::istringstream iss(header);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string lower = toLowerCopy(line);
        if (lower.rfind(keyLower, 0) != 0) {
            continue;
        }
        std::string value = line.substr(keyLower.size());
        trim(value);
        values.push_back(value);
    }
    return values;
}

inline std::string headerValue(const std::string& header, const char* key) {
    const auto values = headerValues(header, key);
    return values.empty() ? std::string() : values.front();
}

inline std::string parseControlBaseUrl(const std::string& header) {
    std::string value = headerValue(header, "Content-Base:");
    if (value.empty()) {
        value = headerValue(header, "Content-Location:");
    }
    return value;
}

inline bool supportsMethod(const std::string& header, const char* method) {
    if (!method || !*method) {
        return false;
    }
    std::string publicHeader = headerValue(header, "Public:");
    if (publicHeader.empty()) {
        return false;
    }
    std::string candidate;
    std::istringstream iss(publicHeader);
    while (std::getline(iss, candidate, ',')) {
        trim(candidate);
        if (toLowerCopy(candidate) == toLowerCopy(method)) {
            return true;
        }
    }
    return false;
}

inline std::string sanitizeMessageForLog(const std::string& message) {
    std::istringstream iss(message);
    std::ostringstream oss;
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (!first) {
            oss << "\n";
        }
        first = false;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string lower = toLowerCopy(line);
        if (lower.rfind("authorization:", 0) == 0) {
            oss << "Authorization: <redacted>";
        } else {
            oss << line;
        }
    }
    return oss.str();
}

inline bool parseSessionInfo(const std::string& header, SwRtspSessionInfo& outInfo) {
    std::istringstream iss(header);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string lower = toLowerCopy(line);
        const std::string key = "session:";
        if (lower.rfind(key, 0) != 0) {
            continue;
        }

        std::string value = line.substr(key.size());
        trim(value);
        std::string sessionId = value;
        auto semicolon = value.find(';');
        if (semicolon != std::string::npos) {
            sessionId = value.substr(0, semicolon);
        }
        trim(sessionId);
        outInfo.sessionId = sessionId;

        std::string lowerValue = toLowerCopy(value);
        const std::string timeoutKey = "timeout=";
        auto timeoutPos = lowerValue.find(timeoutKey);
        if (timeoutPos != std::string::npos) {
            timeoutPos += timeoutKey.size();
            std::size_t timeoutEnd = timeoutPos;
            while (timeoutEnd < lowerValue.size() &&
                   std::isdigit(static_cast<unsigned char>(lowerValue[timeoutEnd]))) {
                ++timeoutEnd;
            }
            if (timeoutEnd > timeoutPos) {
                outInfo.timeoutSeconds =
                    std::atoi(lowerValue.substr(timeoutPos, timeoutEnd - timeoutPos).c_str());
            }
        }
        return !outInfo.sessionId.empty();
    }
    return false;
}

inline bool parseTransportInfo(const std::string& header, SwRtspTransportInfo& info) {
    std::istringstream iss(header);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string lower = toLowerCopy(line);
        const std::string key = "transport:";
        if (lower.rfind(key, 0) != 0) {
            continue;
        }

        auto pos = lower.find("server_port=");
        if (pos != std::string::npos) {
            int rtp = 0;
            int rtcp = 0;
#if defined(_MSC_VER)
            ::sscanf_s(lower.c_str() + pos, "server_port=%d-%d", &rtp, &rtcp);
#else
            std::sscanf(lower.c_str() + pos, "server_port=%d-%d", &rtp, &rtcp);
#endif
            info.serverRtpPort = static_cast<uint16_t>(rtp);
            info.serverRtcpPort = static_cast<uint16_t>(rtcp);
        }

        pos = lower.find("interleaved=");
        if (pos != std::string::npos) {
            int rtp = 0;
            int rtcp = 1;
#if defined(_MSC_VER)
            ::sscanf_s(lower.c_str() + pos, "interleaved=%d-%d", &rtp, &rtcp);
#else
            std::sscanf(lower.c_str() + pos, "interleaved=%d-%d", &rtp, &rtcp);
#endif
            info.interleavedRtpChannel = static_cast<uint8_t>(rtp);
            info.interleavedRtcpChannel = static_cast<uint8_t>(rtcp);
        }
        return true;
    }
    return false;
}

inline int keepAliveIntervalMsForTimeoutSeconds(int timeoutSeconds) {
    if (timeoutSeconds <= 0) {
        return 15000;
    }
    const int conservativeSeconds = (timeoutSeconds > 10) ? (timeoutSeconds - 5) : (timeoutSeconds / 2);
    return (std::max)(1000, conservativeSeconds * 1000);
}

} // namespace SwRtspHeaderUtils
