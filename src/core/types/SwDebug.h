/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
// Ensure <windows.h> (if included later) does not pull winsock.h.
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

class SwString; // forward declaration (for nicer streaming & traits)

enum class SwDebugLevel {
    Debug,
    Warning,
    Error
};

struct SwDebugContext {
    const char* file;
    int line;
    const char* function;
    SwDebugLevel level;
    const char* category; // optional logging category / module name
};

class SwDebugMessage; // forward declaration

class SwDebug {
public:
    static SwDebug& instance() {
        static SwDebug _instance;
        return _instance;
    }

    template<typename T>
    static void setAppName(const T& name) {
        instance().setAppName_(toStdString_(name));
    }

    template<typename T>
    static void setVersion(const T& version) {
        instance().setVersion_(toStdString_(version));
    }

    template<typename T>
    static void setRemoteEndpoint(const T& host, uint16_t port) {
        instance().setRemoteEndpoint_(toStdString_(host), port);
    }

    static void setConsoleEnabled(bool enabled) {
        instance().setConsoleEnabled_(enabled);
    }

    static void setFileEnabled(bool enabled) {
        instance().setFileEnabled_(enabled);
    }

    template<typename T>
    static void setFilePath(const T& filePath) {
        instance().setFilePath_(toStdString_(filePath));
    }

    template<typename T>
    static void setFilterRegex(const T& regex) {
        instance().setFilterRegex_(toStdString_(regex));
    }

    /**
     * Logging rules to enable or disable per-category output.
     *
     * Syntax (tokens separated by '\n' or ';'):
     *   - sw.core.iodevice.swprocess=true        (defaults to ".debug")
     *   - sw.core.*.debug=true
     *   - sw.core.*.warning=false
     *   - sw.core.*.error=true
     *   - *=false / *.debug=true
     */
    static void setLoggingRules(const char* rules) {
        instance().setLoggingRules_(rules);
    }

    static void setLoggingRules(const std::string& rules) {
        instance().setLoggingRules_(rules.c_str());
    }

    static void clearLoggingRules() {
        instance().clearLoggingRules_();
    }

    static void setDebugCategoryEnabled(const char* categoryPattern, bool enabled) {
        instance().addLoggingRule_(categoryPattern, categoryRuleMaskDebug_(), enabled);
    }

    static void setDebugCategoryEnabled(const std::string& categoryPattern, bool enabled) {
        instance().addLoggingRule_(categoryPattern.c_str(), categoryRuleMaskDebug_(), enabled);
    }

    static bool isCategoryEnabled(const char* categoryName, SwDebugLevel level) {
        return instance().isCategoryEnabled_(categoryName, level);
    }

    void logMessage(const SwDebugContext& ctx, const std::string& msg) {
        if (ctx.category && ctx.category[0] != '\0') {
            if (!isCategoryEnabled_(ctx.category, ctx.level)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string cleanedMsg = stripLeadingDecorations_(msg);
        const std::string timePrefix = formatLocalTimePrefix_(); // HHMMSS.UUUUUU

        const std::string payload = formatJsonLine_(ctx, cleanedMsg, timePrefix);

        std::string content;
        switch (ctx.level) {
        case SwDebugLevel::Debug:
            content += "[DEBUG] ";
            break;
        case SwDebugLevel::Warning:
            content += "[WARNING] ";
            break;
        case SwDebugLevel::Error:
            content += "[ERROR] ";
            break;
        }

        if (ctx.category && ctx.category[0] != '\0') {
            content += "[";
            content += ctx.category;
            content += "] ";
        }

#ifdef SW_DEBUG_INCLUDE_SOURCE
        content += std::string(ctx.file ? ctx.file : "") + ":" + std::to_string(ctx.line) + " ("
                   + std::string(ctx.function ? ctx.function : "") + ") ";
#endif
        content += cleanedMsg;

        if (m_filterEnabled) {
            try {
                if (!std::regex_search(content, m_filterRe)) {
                    return;
                }
            } catch (...) {
            }
        }

        const std::string line = "[" + timePrefix + "] " + content + "\n";

        if (m_consoleEnabled) {
            std::FILE* out = (ctx.level == SwDebugLevel::Debug) ? stdout : stderr;
            (void)std::fwrite(line.data(), 1, line.size(), out);
            (void)std::fflush(out);
        }

        if (m_fileEnabled && !m_filePath.empty()) {
            const std::string dir = directoryOfPath_(m_filePath);
            if (!dir.empty()) {
                (void)mkpath_(dir);
            }
            appendFile_(m_filePath, line);
        }

        sendRemoteIfNeeded_(payload);
    }

private:
    static std::string getenvString_(const char* name) {
        if (!name || !name[0]) {
            return std::string();
        }
#if defined(_WIN32)
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, name) != 0 || !value) {
            return std::string();
        }
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
#endif
    }

    SwDebug() {
        std::string rules = getenvString_("SW_LOGGING_RULES");
        if (rules.empty()) {
            rules = getenvString_("SW_DEBUG_RULES");
        }
        if (!rules.empty()) {
            setLoggingRules_(rules.c_str());
        }
    }

    ~SwDebug() {
        resetRemoteSocket_();
    }

    SwDebug(const SwDebug&) = delete;
    SwDebug& operator=(const SwDebug&) = delete;

    std::mutex m_mutex;
    std::mutex m_rulesMutex;

    struct CategoryRule {
        std::string pattern;
        unsigned char mask{0};
        bool enabled{false};
    };

    std::vector<CategoryRule> m_categoryRules;
    bool m_defaultCategoryDebugEnabled{false};
    bool m_defaultCategoryWarningEnabled{true};
    bool m_defaultCategoryErrorEnabled{true};

    std::string m_appName{"UnknownApp"};
    std::string m_version{"0.0.1"};

    bool m_consoleEnabled{true};

    bool m_fileEnabled{false};
    std::string m_filePath;

    bool m_filterEnabled{false};
    std::string m_filterRegex;
    std::regex m_filterRe;

    std::string m_remoteHost;
    uint16_t m_remotePort{0};
    bool m_remoteEnabled{false};

#if defined(_WIN32)
    using SocketHandle = SOCKET;
    static SocketHandle invalidSocket_() { return INVALID_SOCKET; }
    static bool socketValid_(SocketHandle s) { return s != INVALID_SOCKET; }
#else
    using SocketHandle = int;
    static SocketHandle invalidSocket_() { return -1; }
    static bool socketValid_(SocketHandle s) { return s >= 0; }
#endif

    SocketHandle m_remoteSocket{invalidSocket_()};
    bool m_remoteConnected{false};

    void setConsoleEnabled_(bool enabled) {
        m_consoleEnabled = enabled;
    }

    void setFileEnabled_(bool enabled) {
        m_fileEnabled = enabled;
    }

    void setFilePath_(std::string filePath) {
        m_filePath = std::move(filePath);
    }

    void setAppName_(std::string name) {
        m_appName = std::move(name);
    }

    void setVersion_(std::string version) {
        m_version = std::move(version);
    }

    void setFilterRegex_(std::string regex) {
        m_filterRegex = std::move(regex);
        m_filterEnabled = false;
        if (m_filterRegex.empty()) return;
        try {
            m_filterRe = std::regex(m_filterRegex);
            m_filterEnabled = true;
        } catch (...) {
            m_filterEnabled = false;
        }
    }

    void setRemoteEndpoint_(std::string host, uint16_t port) {
        m_remoteHost = std::move(host);
        m_remotePort = port;
        m_remoteEnabled = !m_remoteHost.empty() && m_remotePort != 0;
        resetRemoteSocket_();
    }

    static std::string toStdString_(const std::string& v) { return v; }
    static std::string toStdString_(const char* v) { return v ? std::string(v) : std::string(); }
    static std::string toStdString_(char* v) { return v ? std::string(v) : std::string(); }

    template<typename T>
    static auto toStdString_(const T& v) -> decltype(v.toStdString(), std::string()) {
        return v.toStdString();
    }

    static unsigned char categoryRuleMaskDebug_() { return 0x01; }
    static unsigned char categoryRuleMaskWarning_() { return 0x02; }
    static unsigned char categoryRuleMaskError_() { return 0x04; }
    static unsigned char categoryRuleMaskAll_() { return 0x07; }

    static void trimAscii_(std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            ++start;
        }
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            --end;
        }
        if (start == 0 && end == s.size()) {
            return;
        }
        s = s.substr(start, end - start);
    }

    static std::string toLowerAsciiCopy_(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
        return out;
    }

    static bool parseBool_(const std::string& raw, bool& out) {
        std::string v = raw;
        trimAscii_(v);
        v = toLowerAsciiCopy_(v);
        if (v == "1" || v == "true" || v == "on" || v == "yes") {
            out = true;
            return true;
        }
        if (v == "0" || v == "false" || v == "off" || v == "no") {
            out = false;
            return true;
        }
        return false;
    }

    static bool globMatch_(const std::string& pattern, const char* text) {
        if (!text) {
            return false;
        }
        const char* p = pattern.c_str();
        const char* t = text;
        const char* star = nullptr;
        const char* starText = nullptr;

        while (*t) {
            if (*p == '*') {
                star = p++;
                starText = t;
                continue;
            }
            if (*p != '\0' && *p == *t) {
                ++p;
                ++t;
                continue;
            }
            if (star) {
                p = star + 1;
                ++starText;
                t = starText;
                continue;
            }
            return false;
        }

        while (*p == '*') {
            ++p;
        }
        return *p == '\0';
    }

    void clearLoggingRules_() {
        std::lock_guard<std::mutex> lock(m_rulesMutex);
        m_categoryRules.clear();
    }

    void addLoggingRule_(const char* categoryPattern, unsigned char mask, bool enabled) {
        if (!categoryPattern) {
            return;
        }
        std::string pattern(categoryPattern);
        trimAscii_(pattern);
        if (pattern.empty()) {
            return;
        }
        CategoryRule rule;
        rule.pattern = std::move(pattern);
        rule.mask = mask;
        rule.enabled = enabled;
        std::lock_guard<std::mutex> lock(m_rulesMutex);
        m_categoryRules.push_back(std::move(rule));
    }

    void setLoggingRules_(const char* rules) {
        std::vector<CategoryRule> parsed;
        if (rules && rules[0]) {
            std::string token;
            for (const char* p = rules;; ++p) {
                const char c = *p;
                if (c == '\0' || c == '\n' || c == ';') {
                    std::string line = token;
                    token.clear();
                    trimAscii_(line);
                    if (!line.empty() && line[0] != '#') {
                        const size_t eq = line.find('=');
                        if (eq != std::string::npos) {
                            std::string left = line.substr(0, eq);
                            std::string right = line.substr(eq + 1);
                            trimAscii_(left);
                            bool enabled = false;
                            if (!left.empty() && parseBool_(right, enabled)) {
                                unsigned char mask = categoryRuleMaskDebug_();
                                std::string pattern = left;
                                const size_t dot = left.rfind('.');
                                if (dot != std::string::npos) {
                                    const std::string suffixLower = toLowerAsciiCopy_(left.substr(dot + 1));
                                    if (suffixLower == "debug") {
                                        mask = categoryRuleMaskDebug_();
                                        pattern = left.substr(0, dot);
                                    } else if (suffixLower == "warning") {
                                        mask = categoryRuleMaskWarning_();
                                        pattern = left.substr(0, dot);
                                    } else if (suffixLower == "error" || suffixLower == "critical") {
                                        mask = categoryRuleMaskError_();
                                        pattern = left.substr(0, dot);
                                    } else if (suffixLower == "all") {
                                        mask = categoryRuleMaskAll_();
                                        pattern = left.substr(0, dot);
                                    }
                                }
                                trimAscii_(pattern);
                                if (!pattern.empty()) {
                                    CategoryRule rule;
                                    rule.pattern = std::move(pattern);
                                    rule.mask = mask;
                                    rule.enabled = enabled;
                                    parsed.push_back(std::move(rule));
                                }
                            }
                        }
                    }
                    if (c == '\0') {
                        break;
                    }
                    continue;
                }
                token.push_back(c);
            }
        }

        std::lock_guard<std::mutex> lock(m_rulesMutex);
        m_categoryRules.swap(parsed);
    }

    bool isCategoryEnabled_(const char* categoryName, SwDebugLevel level) {
        if (!categoryName || categoryName[0] == '\0') {
            return true;
        }

        const unsigned char bit = (level == SwDebugLevel::Debug)    ? categoryRuleMaskDebug_()
                                  : (level == SwDebugLevel::Warning) ? categoryRuleMaskWarning_()
                                                                     : categoryRuleMaskError_();

        bool enabled = (level == SwDebugLevel::Debug)    ? m_defaultCategoryDebugEnabled
                       : (level == SwDebugLevel::Warning) ? m_defaultCategoryWarningEnabled
                                                          : m_defaultCategoryErrorEnabled;

        std::lock_guard<std::mutex> lock(m_rulesMutex);
        for (size_t i = 0; i < m_categoryRules.size(); ++i) {
            const CategoryRule& rule = m_categoryRules[i];
            if ((rule.mask & bit) == 0) {
                continue;
            }
            if (globMatch_(rule.pattern, categoryName)) {
                enabled = rule.enabled;
            }
        }
        return enabled;
    }

    static std::string jsonEscape_(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (const unsigned char c : s) {
            switch (c) {
            case '\"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[7] = {0};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int)c);
                    out += buf;
                } else {
                    out += (char)c;
                }
                break;
            }
        }
        return out;
    }

    static std::string levelToString_(SwDebugLevel level) {
        switch (level) {
        case SwDebugLevel::Debug:
            return "DEBUG";
        case SwDebugLevel::Warning:
            return "WARNING";
        case SwDebugLevel::Error:
            return "ERROR";
        }
        return "UNKNOWN";
    }

    std::string formatJsonLine_(const SwDebugContext& ctx,
                                const std::string& cleanedMsg,
                                const std::string& timePrefix) const {
        std::string json = "{";
        json += "\"type\":\"log\"";
        json += ",\"level\":\"" + levelToString_(ctx.level) + "\"";
        if (ctx.category && ctx.category[0] != '\0') {
            json += ",\"category\":\"" + jsonEscape_(std::string(ctx.category)) + "\"";
        }
        json += ",\"appName\":\"" + jsonEscape_(m_appName) + "\"";
        json += ",\"version\":\"" + jsonEscape_(m_version) + "\"";
#ifdef SW_DEBUG_INCLUDE_METAINFOS
        json += ",\"timestamp\":\"" + jsonEscape_(timePrefix) + "\"";
#endif
#ifdef SW_DEBUG_INCLUDE_SOURCE
        json += ",\"file\":\"" + jsonEscape_(ctx.file ? std::string(ctx.file) : std::string()) + "\"";
        json += ",\"line\":" + std::to_string(ctx.line);
        json += ",\"function\":\"" + jsonEscape_(ctx.function ? std::string(ctx.function) : std::string()) + "\"";
#endif
        json += ",\"message\":\"" + jsonEscape_(cleanedMsg) + "\"";
        json += "}\n";
        return json;
    }

    static std::string stripLeadingDecorations_(std::string text) {
        auto isWs = [](char c) -> bool {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };

        while (!text.empty() && isWs(text[0])) {
            text.erase(text.begin());
        }

        auto looksLikeTimePrefixRaw = [](const std::string& v) -> bool {
            // Expected: "HHMMSS.UUUUUU" (6 digits '.' 6 digits)
            if (v.size() < 13) return false;
            for (size_t i = 0; i < 6; ++i) {
                const char c = v[i];
                if (c < '0' || c > '9') return false;
            }
            if (v[6] != '.') return false;
            for (size_t i = 7; i < 13; ++i) {
                const char c = v[i];
                if (c < '0' || c > '9') return false;
            }
            return true;
        };

        auto looksLikeTimePrefixBracketed = [&](const std::string& v) -> bool {
            // Expected: "[HHMMSS.UUUUUU]"
            if (v.size() < 15) return false;
            if (v[0] != '[') return false;
            if (v[14] != ']') return false;
            return looksLikeTimePrefixRaw(v.substr(1, 13));
        };

        if (looksLikeTimePrefixBracketed(text)) {
            text.erase(0, 15);
            while (!text.empty() && (text[0] == ' ' || text[0] == '\t')) {
                text.erase(text.begin());
            }
        } else if (looksLikeTimePrefixRaw(text)) {
            text.erase(0, 13);
            while (!text.empty() && (text[0] == ' ' || text[0] == '\t')) {
                text.erase(text.begin());
            }
        }

        const char* tags[] = {"[DEBUG]", "[WARNING]", "[ERROR]"};
        for (size_t i = 0; i < 3; ++i) {
            const std::string tag(tags[i]);
            if (text.rfind(tag, 0) != 0) continue;
            text.erase(0, tag.size());
            while (!text.empty() && (text[0] == ' ' || text[0] == '\t')) {
                text.erase(text.begin());
            }
            break;
        }

        return text;
    }

    static std::string formatLocalTimePrefix_() {
        auto now = std::chrono::system_clock::now();
        const auto nowUs =
            std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmStruct;
#if defined(_WIN32)
        localtime_s(&tmStruct, &t);
#else
        localtime_r(&t, &tmStruct);
#endif
        char timeBuf[32] = {0};
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d%02d%02d.%06lld",
                      tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec,
                      static_cast<long long>(nowUs % 1000000));
        return std::string(timeBuf);
    }

    static std::string directoryOfPath_(const std::string& filePath) {
        size_t pos = filePath.find_last_of("/\\");
        if (pos == std::string::npos) return std::string();
        if (pos == 0) return filePath.substr(0, 1);
        return filePath.substr(0, pos);
    }

    static bool createDir_(const std::string& path) {
#if defined(_WIN32)
        std::string native = path;
        for (char& c : native) {
            if (c == '/') c = '\\';
        }
        if (_mkdir(native.c_str()) == 0) return true;
        return errno == EEXIST;
#else
        if (::mkdir(path.c_str(), 0755) == 0) return true;
        return errno == EEXIST;
#endif
    }

    static bool mkpath_(const std::string& dir) {
        if (dir.empty()) return true;

        std::string path = dir;
        for (char& c : path) {
            if (c == '\\') c = '/';
        }

#if defined(_WIN32)
        // UNC path, don't try to create.
        if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
            return true;
        }
#endif

        std::string current;
        size_t i = 0;

#if defined(_WIN32)
        if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
            current = path.substr(0, 2);
            i = 2;
            if (i < path.size() && path[i] == '/') {
                current += '/';
                ++i;
            }
        } else if (!path.empty() && path[0] == '/') {
            current = "/";
            i = 1;
        }
#else
        if (!path.empty() && path[0] == '/') {
            current = "/";
            i = 1;
        }
#endif

        while (i < path.size()) {
            while (i < path.size() && path[i] == '/') ++i;
            size_t start = i;
            while (i < path.size() && path[i] != '/') ++i;
            if (start == i) break;
            const std::string part = path.substr(start, i - start);
            if (part.empty()) continue;

            if (!current.empty() && current.back() != '/') current += '/';
            current += part;

            if (!createDir_(current)) return false;
        }

        return true;
    }

    static void appendFile_(const std::string& filePath, const std::string& data) {
        if (filePath.empty()) return;
#if defined(_WIN32)
        std::string native = filePath;
        for (char& c : native) {
            if (c == '/') c = '\\';
        }
        std::FILE* f = nullptr;
        (void)fopen_s(&f, native.c_str(), "ab");
#else
        std::FILE* f = std::fopen(filePath.c_str(), "ab");
#endif
        if (!f) return;
        (void)std::fwrite(data.data(), 1, data.size(), f);
        (void)std::fclose(f);
    }

#if defined(_WIN32)
    static void ensureWinsock_() {
        static std::once_flag once;
        std::call_once(once, []() {
            WSADATA wsaData;
            (void)WSAStartup(MAKEWORD(2, 2), &wsaData);
        });
    }
#endif

    void resetRemoteSocket_() {
        m_remoteConnected = false;
        if (!socketValid_(m_remoteSocket)) {
            m_remoteSocket = invalidSocket_();
            return;
        }
#if defined(_WIN32)
        (void)closesocket(m_remoteSocket);
#else
        (void)::close(m_remoteSocket);
#endif
        m_remoteSocket = invalidSocket_();
    }

    bool ensureRemoteSocket_() {
        if (!m_remoteEnabled || m_remoteHost.empty() || m_remotePort == 0) {
            return false;
        }

        if (m_remoteConnected && socketValid_(m_remoteSocket)) {
            return true;
        }

        resetRemoteSocket_();

#if defined(_WIN32)
        ensureWinsock_();
#endif

        const std::string portStr = std::to_string(m_remotePort);
        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = nullptr;
        const int rc = getaddrinfo(m_remoteHost.c_str(), portStr.c_str(), &hints, &result);
        if (rc != 0 || !result) {
            return false;
        }

        SocketHandle sock = invalidSocket_();
        for (addrinfo* ai = result; ai; ai = ai->ai_next) {
            SocketHandle s = (SocketHandle)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (!socketValid_(s)) {
                continue;
            }

            if (::connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
                sock = s;
                break;
            }

#if defined(_WIN32)
            (void)closesocket(s);
#else
            (void)::close(s);
#endif
        }

        freeaddrinfo(result);

        if (!socketValid_(sock)) {
            return false;
        }

        m_remoteSocket = sock;
        m_remoteConnected = true;
        return true;
    }

    static bool writeAll_(SocketHandle s, const std::string& payload) {
        const char* data = payload.data();
        size_t remaining = payload.size();
        while (remaining > 0) {
#if defined(_WIN32)
            const int sent = ::send(s, data, (int)remaining, 0);
            if (sent == SOCKET_ERROR || sent <= 0) {
                return false;
            }
#else
            const ssize_t sent = ::send(s, data, remaining, 0);
            if (sent <= 0) {
                return false;
            }
#endif
            data += sent;
            remaining -= (size_t)sent;
        }
        return true;
    }

    void sendRemoteIfNeeded_(const std::string& payload) {
        if (!m_remoteEnabled || m_remoteHost.empty() || m_remotePort == 0) {
            return;
        }

        if (!ensureRemoteSocket_()) {
            return;
        }

        if (!writeAll_(m_remoteSocket, payload)) {
            resetRemoteSocket_();
        }
    }

    friend class SwDebugMessage;
};

template<typename T, typename = void>
struct SwDebugIsIterable : std::false_type {};

template<typename...>
struct SwDebugVoid {
    typedef void type;
};

template<typename... Ts>
using SwDebugVoidT = typename SwDebugVoid<Ts...>::type;

template<typename T>
struct SwDebugIsIterable<T, SwDebugVoidT<decltype(std::declval<T>().begin()),
                                         decltype(std::declval<T>().end())>> : std::true_type {};

template<typename T>
struct SwDebugIsStdPairImpl : std::false_type {};

template<typename A, typename B>
struct SwDebugIsStdPairImpl<std::pair<A, B>> : std::true_type {};

template<typename T>
using SwDecayed = typename std::decay<T>::type;

template<typename T>
struct SwDebugIsStdPair : SwDebugIsStdPairImpl<SwDecayed<T>> {};

class SwDebugMessage {
public:
    SwDebugMessage(const SwDebugContext& ctx) : m_ctx(ctx) {}

    template<typename T,
             typename std::enable_if<
                 (!SwDebugIsIterable<T>::value ||
                  std::is_same<T, std::string>::value ||
                  std::is_same<T, SwString>::value ||
                  std::is_convertible<T, const char*>::value) &&
                     !SwDebugIsStdPair<T>::value,
                 int>::type = 0>
    SwDebugMessage& operator<<(const T& value) {
        m_stream << value;
        return *this;
    }

    template<typename First, typename Second>
    SwDebugMessage& operator<<(const std::pair<First, Second>& value) {
        *this << "(" << value.first << ", " << value.second << ")";
        return *this;
    }

    template<typename Container,
             typename std::enable_if<
                 SwDebugIsIterable<Container>::value &&
                     !std::is_same<Container, std::string>::value &&
                     !std::is_same<Container, SwString>::value &&
                     !std::is_convertible<Container, const char*>::value,
                 int>::type = 0>
    SwDebugMessage& operator<<(const Container& container) {
        m_stream << "[";
        bool first = true;
        for (const auto& element : container) {
            if (!first) {
                m_stream << ", ";
            }
            first = false;
            *this << element;
        }
        m_stream << "]";
        return *this;
    }

    ~SwDebugMessage() {
        SwDebug::instance().logMessage(m_ctx, m_stream.str());
    }

private:
    SwDebugContext m_ctx;
    std::ostringstream m_stream;
};

#define swDebug() SwDebugMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Debug, nullptr})
#define swWarning() SwDebugMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Warning, nullptr})
#define swError() SwDebugMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Error, nullptr})

// Category/module logging.
// Note: the category should be a const char* with static lifetime (string literal or macro).
#define swCDebug(category)                                                                                           \
    for (const char* SW_DEBUG_CONCAT(_swCat_, __LINE__) = (category); SW_DEBUG_CONCAT(_swCat_, __LINE__) != nullptr;  \
         SW_DEBUG_CONCAT(_swCat_, __LINE__) = nullptr)                                                                \
        for (bool SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__) =                                                         \
                 SwDebug::isCategoryEnabled(SW_DEBUG_CONCAT(_swCat_, __LINE__), SwDebugLevel::Debug);                 \
             SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__);                                                               \
             SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__) = false)                                                       \
            SwDebugMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Debug, SW_DEBUG_CONCAT(_swCat_, __LINE__)})

#define swCWarning(category)                                                                                          \
    for (const char* SW_DEBUG_CONCAT(_swCat_, __LINE__) = (category); SW_DEBUG_CONCAT(_swCat_, __LINE__) != nullptr;  \
         SW_DEBUG_CONCAT(_swCat_, __LINE__) = nullptr)                                                                \
        for (bool SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__) =                                                         \
                 SwDebug::isCategoryEnabled(SW_DEBUG_CONCAT(_swCat_, __LINE__), SwDebugLevel::Warning);               \
             SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__);                                                               \
             SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__) = false)                                                       \
            SwDebugMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Warning, SW_DEBUG_CONCAT(_swCat_, __LINE__)})

#define swCError(category)                                                                                            \
    for (const char* SW_DEBUG_CONCAT(_swCat_, __LINE__) = (category); SW_DEBUG_CONCAT(_swCat_, __LINE__) != nullptr;  \
         SW_DEBUG_CONCAT(_swCat_, __LINE__) = nullptr)                                                                \
        for (bool SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__) =                                                         \
                 SwDebug::isCategoryEnabled(SW_DEBUG_CONCAT(_swCat_, __LINE__), SwDebugLevel::Error);                 \
             SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__);                                                               \
             SW_DEBUG_CONCAT(_swCatEnabled_, __LINE__) = false)                                                       \
            SwDebugMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Error, SW_DEBUG_CONCAT(_swCat_, __LINE__)})

// Aliases (explicit "module" naming).
#define swDebugM(category) swCDebug(category)
#define swWarningM(category) swCWarning(category)
#define swErrorM(category) swCError(category)

class SwDebugScopedTimer {
public:
    SwDebugScopedTimer(const SwDebugContext& ctx, std::string reason, long long maxMs)
        : m_ctx(ctx),
          m_reason(std::move(reason)),
          m_maxMs(maxMs),
          m_start(std::chrono::steady_clock::now()) {}

    ~SwDebugScopedTimer() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - m_start)
                           .count();
        if (m_maxMs <= 0 || elapsed >= m_maxMs) {
            SwDebugMessage msg(m_ctx);
            msg << "[timer] " << m_reason << " took " << elapsed << " ms";
            if (m_maxMs > 0) {
                msg << " (limit=" << m_maxMs << " ms)";
            }
        }
    }

private:
    SwDebugContext m_ctx;
    std::string m_reason;
    long long m_maxMs;
    std::chrono::steady_clock::time_point m_start;
};

class SwDebugScopedTimerUs {
public:
    SwDebugScopedTimerUs(const SwDebugContext& ctx, std::string reason, long long maxUs)
        : m_ctx(ctx),
          m_reason(std::move(reason)),
          m_maxUs(maxUs),
          m_start(std::chrono::steady_clock::now()) {}

    SwDebugScopedTimerUs(std::string reason, long long maxUs)
        : SwDebugScopedTimerUs({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Debug, nullptr}, std::move(reason), maxUs) {}

    ~SwDebugScopedTimerUs() {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - m_start)
                           .count();
        if (m_maxUs <= 0 || elapsed >= m_maxUs) {
            SwDebugMessage msg(m_ctx);
            msg << "[timer] " << m_reason << " took " << elapsed << " us";
            if (m_maxUs > 0) {
                msg << " (limit=" << m_maxUs << " us)";
            }
        }
    }

private:
    SwDebugContext m_ctx;
    std::string m_reason;
    long long m_maxUs;
    std::chrono::steady_clock::time_point m_start;
};

using ScopedTimerUs = SwDebugScopedTimerUs;

class SwDebugTimedMessage {
public:
    SwDebugTimedMessage(const SwDebugContext& ctx, long long maxMs)
        : m_ctx(ctx), m_maxMs(maxMs), m_start(std::chrono::steady_clock::now()) {}

    SwDebugTimedMessage(const SwDebugContext& ctx, std::string reason, long long maxMs)
        : m_ctx(ctx), m_reason(std::move(reason)), m_maxMs(maxMs), m_start(std::chrono::steady_clock::now()) {}

    template<typename T>
    SwDebugTimedMessage& operator<<(const T& v) {
        m_stream << v;
        return *this;
    }

    ~SwDebugTimedMessage() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - m_start)
                           .count();
        if (m_maxMs > 0 && elapsed < m_maxMs) {
            return;
        }
        SwDebugMessage msg(m_ctx);
        msg << "[timer] ";
        if (!m_reason.empty()) {
            msg << m_reason << " ";
        }
        msg << m_stream.str() << " elapsed=" << elapsed << " ms";
        if (m_maxMs > 0) {
            msg << " limit=" << m_maxMs << " ms";
        }
    }

private:
    SwDebugContext m_ctx;
    std::string m_reason;
    long long m_maxMs;
    std::chrono::steady_clock::time_point m_start;
    std::ostringstream m_stream;
};

#define SW_DEBUG_TIMED_SEL(_1, _2, NAME, ...) NAME
#define SW_DEBUG_TIMED1(maxMs) SwDebugTimedMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Debug, nullptr}, (long long)(maxMs))
#define SW_DEBUG_TIMED2(reason, maxMs) SwDebugTimedMessage({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Debug, nullptr}, std::string(reason), (long long)(maxMs))
#define swDebugTimed(...) SW_DEBUG_TIMED_SEL(__VA_ARGS__, SW_DEBUG_TIMED2, SW_DEBUG_TIMED1)(__VA_ARGS__)

#define SW_DEBUG_CONCAT_INNER(a, b) a##b
#define SW_DEBUG_CONCAT(a, b) SW_DEBUG_CONCAT_INNER(a, b)

#define SW_DEBUG_SCOPED_TIMER_US_SEL(_1, _2, NAME, ...) NAME
#define SW_DEBUG_SCOPED_TIMER_US2(reason, maxUs) SwDebugScopedTimerUs SW_DEBUG_CONCAT(_swDebugScopedTimerUs_, __LINE__)({__FILE__, __LINE__, __FUNCTION__, SwDebugLevel::Debug, nullptr}, std::string(reason), (long long)(maxUs))
#define SW_DEBUG_SCOPED_TIMER_US1(reason) SW_DEBUG_SCOPED_TIMER_US2(reason, 0)
#define swScopedTimerUs(...) SW_DEBUG_SCOPED_TIMER_US_SEL(__VA_ARGS__, SW_DEBUG_SCOPED_TIMER_US2, SW_DEBUG_SCOPED_TIMER_US1)(__VA_ARGS__)
