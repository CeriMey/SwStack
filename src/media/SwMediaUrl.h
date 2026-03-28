#pragma once

/**
 * @file src/media/SwMediaUrl.h
 * @ingroup media
 * @brief Declares a lightweight parsed-media-URL helper for the CoreSw media layer.
 */

#include "core/types/SwMap.h"
#include "core/types/SwString.h"

#include <cctype>
#include <cstdlib>
#include <string>

class SwMediaUrl {
public:
    static SwMediaUrl parse(const SwString& rawUrl) {
        SwMediaUrl url;
        url.m_original = rawUrl.trimmed();
        if (url.m_original.isEmpty()) {
            return url;
        }

        std::string text = url.m_original.toStdString();
        const std::size_t schemePos = text.find("://");
        if (schemePos == std::string::npos) {
            url.m_scheme = "file";
            url.m_path = decodeComponent_(url.m_original);
            url.m_valid = !url.m_path.isEmpty();
            return url;
        }

        url.m_scheme = toLowerAscii_(SwString(text.substr(0, schemePos)));
        std::string remainder = text.substr(schemePos + 3);

        std::size_t fragmentPos = remainder.find('#');
        if (fragmentPos != std::string::npos) {
            url.m_fragment = decodeComponent_(SwString(remainder.substr(fragmentPos + 1)));
            remainder = remainder.substr(0, fragmentPos);
        }

        std::size_t queryPos = remainder.find('?');
        if (queryPos != std::string::npos) {
            url.m_rawQuery = SwString(remainder.substr(queryPos + 1));
            parseQuery_(url.m_rawQuery, url.m_query);
            remainder = remainder.substr(0, queryPos);
        }

        if (url.m_scheme == "file") {
            url.m_path = decodeComponent_(SwString(remainder));
            if (url.m_path.isEmpty()) {
                url.m_path = "/";
            }
            url.m_valid = true;
            return url;
        }

        std::string authority = remainder;
        std::string path = "/";
        const std::size_t slashPos = remainder.find('/');
        if (slashPos != std::string::npos) {
            authority = remainder.substr(0, slashPos);
            path = remainder.substr(slashPos);
        }

        const std::size_t atPos = authority.rfind('@');
        if (atPos != std::string::npos) {
            url.m_userInfo = decodeComponent_(SwString(authority.substr(0, atPos)));
            authority = authority.substr(atPos + 1);
        }

        if (!authority.empty() && authority.front() == '[') {
            const std::size_t closeBracket = authority.find(']');
            if (closeBracket != std::string::npos) {
                url.m_host = SwString(authority.substr(1, closeBracket - 1));
                if (closeBracket + 1 < authority.size() && authority[closeBracket + 1] == ':') {
                    url.m_port = parsePort_(authority.substr(closeBracket + 2));
                }
            } else {
                url.m_host = SwString(authority);
            }
        } else {
            const std::size_t colonPos = authority.rfind(':');
            const bool hasSingleColon =
                (colonPos != std::string::npos) && (authority.find(':') == colonPos);
            if (hasSingleColon) {
                url.m_host = decodeComponent_(SwString(authority.substr(0, colonPos)));
                url.m_port = parsePort_(authority.substr(colonPos + 1));
            } else {
                url.m_host = decodeComponent_(SwString(authority));
            }
        }

        url.m_path = decodeComponent_(SwString(path));
        if (url.m_path.isEmpty()) {
            url.m_path = "/";
        }
        if (url.m_port < 0) {
            url.m_port = defaultPortForScheme_(url.m_scheme);
        }
        url.m_valid = !url.m_scheme.isEmpty();
        return url;
    }

    bool isValid() const { return m_valid; }
    bool hasScheme() const { return !m_scheme.isEmpty(); }

    const SwString& original() const { return m_original; }
    const SwString& scheme() const { return m_scheme; }
    const SwString& userInfo() const { return m_userInfo; }
    const SwString& host() const { return m_host; }
    int port() const { return m_port; }
    const SwString& path() const { return m_path; }
    const SwString& rawQuery() const { return m_rawQuery; }
    const SwString& fragment() const { return m_fragment; }
    const SwMap<SwString, SwString>& queryItems() const { return m_query; }

    SwString queryValue(const SwString& key, const SwString& fallback = SwString()) const {
        const SwString normalizedKey = toLowerAscii_(key);
        if (!m_query.contains(normalizedKey)) {
            return fallback;
        }
        return m_query.value(normalizedKey);
    }

    bool hasQueryValue(const SwString& key) const {
        return m_query.contains(toLowerAscii_(key));
    }

    SwString pathWithQuery() const {
        SwString result = m_path.isEmpty() ? SwString("/") : m_path;
        if (!m_rawQuery.isEmpty()) {
            result += "?";
            result += m_rawQuery;
        }
        return result;
    }

    SwString authority() const {
        SwString result = m_host;
        if (m_port >= 0) {
            result += ":";
            result += SwString::number(m_port);
        }
        return result;
    }

    SwString toString() const {
        if (!m_original.isEmpty()) {
            return m_original;
        }
        if (m_scheme == "file") {
            return SwString("file://") + m_path;
        }
        SwString result = m_scheme;
        result += "://";
        if (!m_userInfo.isEmpty()) {
            result += m_userInfo;
            result += "@";
        }
        result += m_host;
        if (m_port >= 0) {
            result += ":";
            result += SwString::number(m_port);
        }
        result += m_path.isEmpty() ? SwString("/") : m_path;
        if (!m_rawQuery.isEmpty()) {
            result += "?";
            result += m_rawQuery;
        }
        if (!m_fragment.isEmpty()) {
            result += "#";
            result += m_fragment;
        }
        return result;
    }

private:
    static SwString toLowerAscii_(const SwString& value) {
        std::string lower = value.toStdString();
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return SwString(lower);
    }

    static int defaultPortForScheme_(const SwString& scheme) {
        if (scheme == "rtsp") {
            return 554;
        }
        if (scheme == "http") {
            return 80;
        }
        if (scheme == "https") {
            return 443;
        }
        return -1;
    }

    static int parsePort_(const std::string& text) {
        if (text.empty()) {
            return -1;
        }
        char* end = nullptr;
        const long value = std::strtol(text.c_str(), &end, 10);
        if (!end || *end != '\0' || value < 0 || value > 65535) {
            return -1;
        }
        return static_cast<int>(value);
    }

    static SwString decodeComponent_(const SwString& value) {
        const std::string text = value.toStdString();
        std::string decoded;
        decoded.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '%' && i + 2 < text.size()) {
                const char hi = text[i + 1];
                const char lo = text[i + 2];
                const auto hexToInt = [](char ch) -> int {
                    if (ch >= '0' && ch <= '9') {
                        return ch - '0';
                    }
                    if (ch >= 'a' && ch <= 'f') {
                        return 10 + (ch - 'a');
                    }
                    if (ch >= 'A' && ch <= 'F') {
                        return 10 + (ch - 'A');
                    }
                    return -1;
                };
                const int hiValue = hexToInt(hi);
                const int loValue = hexToInt(lo);
                if (hiValue >= 0 && loValue >= 0) {
                    decoded.push_back(static_cast<char>((hiValue << 4) | loValue));
                    i += 2;
                    continue;
                }
            }
            if (c == '+') {
                decoded.push_back(' ');
            } else {
                decoded.push_back(c);
            }
        }
        return SwString(decoded);
    }

    static void parseQuery_(const SwString& rawQuery, SwMap<SwString, SwString>& out) {
        const std::string query = rawQuery.toStdString();
        std::size_t start = 0;
        while (start <= query.size()) {
            const std::size_t end = query.find('&', start);
            const std::string pair = query.substr(start, end == std::string::npos
                                                             ? std::string::npos
                                                             : end - start);
            if (!pair.empty()) {
                const std::size_t equalPos = pair.find('=');
                const std::string rawKey = pair.substr(0, equalPos);
                const std::string rawValue =
                    (equalPos == std::string::npos) ? std::string() : pair.substr(equalPos + 1);
                const SwString key = toLowerAscii_(decodeComponent_(SwString(rawKey)));
                if (!key.isEmpty()) {
                    out.insert(key, decodeComponent_(SwString(rawValue)));
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    bool m_valid{false};
    SwString m_original{};
    SwString m_scheme{};
    SwString m_userInfo{};
    SwString m_host{};
    int m_port{-1};
    SwString m_path{};
    SwString m_rawQuery{};
    SwString m_fragment{};
    SwMap<SwString, SwString> m_query{};
};
