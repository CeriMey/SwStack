#pragma once

/**
 * @file src/media/rtsp/SwRtspAuth.h
 * @ingroup media
 * @brief Small header-only helpers for RTSP Basic and Digest authentication.
 */

#include "core/types/SwCrypto.h"
#include "core/types/SwString.h"
#include "media/rtsp/SwRtspHeaderUtils.h"

#include <cctype>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <string>

struct SwRtspAuthChallenge {
    enum class Scheme {
        None,
        Basic,
        Digest
    };

    Scheme scheme{Scheme::None};
    std::string realm{};
    std::string nonce{};
    std::string opaque{};
    std::string algorithm{};
    std::string qop{};
    bool stale{false};
};

namespace SwRtspAuth {

inline std::string selectPreferredDigestQop(const std::string& rawQop) {
    if (rawQop.empty()) {
        return std::string();
    }
    std::string token;
    std::istringstream iss(rawQop);
    while (std::getline(iss, token, ',')) {
        SwRtspHeaderUtils::trim(token);
        if (SwRtspHeaderUtils::toLowerCopy(token) == "auth") {
            return "auth";
        }
    }
    return std::string();
}

inline std::map<std::string, std::string> parseAuthParameters(const std::string& text) {
    std::map<std::string, std::string> params;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               (text[pos] == ',' || std::isspace(static_cast<unsigned char>(text[pos])))) {
            ++pos;
        }
        if (pos >= text.size()) {
            break;
        }

        const std::size_t keyStart = pos;
        while (pos < text.size() && text[pos] != '=' && text[pos] != ',') {
            ++pos;
        }
        std::string key = text.substr(keyStart, pos - keyStart);
        SwRtspHeaderUtils::trim(key);

        std::string value;
        if (pos < text.size() && text[pos] == '=') {
            ++pos;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            if (pos < text.size() && text[pos] == '"') {
                ++pos;
                while (pos < text.size()) {
                    const char ch = text[pos++];
                    if (ch == '"') {
                        break;
                    }
                    if (ch == '\\' && pos < text.size()) {
                        value.push_back(text[pos++]);
                        continue;
                    }
                    value.push_back(ch);
                }
            } else {
                const std::size_t valueStart = pos;
                while (pos < text.size() && text[pos] != ',') {
                    ++pos;
                }
                value = text.substr(valueStart, pos - valueStart);
                SwRtspHeaderUtils::trim(value);
            }
        }

        if (!key.empty()) {
            params[SwRtspHeaderUtils::toLowerCopy(key)] = value;
        }

        while (pos < text.size() && text[pos] != ',') {
            ++pos;
        }
    }
    return params;
}

inline bool supportsDigestAlgorithm(const std::string& algorithm) {
    const std::string lower = SwRtspHeaderUtils::toLowerCopy(algorithm);
    return lower.empty() || lower == "md5" || lower == "md5-sess" ||
           lower == "sha-256" || lower == "sha-256-sess";
}

inline SwRtspAuthChallenge parseChallengeValue(const std::string& value) {
    SwRtspAuthChallenge challenge;
    std::string trimmedValue = value;
    SwRtspHeaderUtils::trim(trimmedValue);
    const std::string lower = SwRtspHeaderUtils::toLowerCopy(trimmedValue);
    if (lower.rfind("basic", 0) == 0) {
        challenge.scheme = SwRtspAuthChallenge::Scheme::Basic;
        challenge.algorithm = "basic";
        const auto params = parseAuthParameters(trimmedValue.substr(5));
        auto it = params.find("realm");
        if (it != params.end()) {
            challenge.realm = it->second;
        }
        return challenge;
    }
    if (lower.rfind("digest", 0) != 0) {
        return challenge;
    }

    const auto params = parseAuthParameters(trimmedValue.substr(6));
    const auto paramValue = [&params](const char* key) -> std::string {
        const auto it = params.find(key ? std::string(key) : std::string());
        return (it != params.end()) ? it->second : std::string();
    };

    challenge.scheme = SwRtspAuthChallenge::Scheme::Digest;
    challenge.realm = paramValue("realm");
    challenge.nonce = paramValue("nonce");
    challenge.opaque = paramValue("opaque");
    {
        const std::string algorithm = paramValue("algorithm");
        challenge.algorithm = algorithm.empty() ? std::string("md5")
                                                : SwRtspHeaderUtils::toLowerCopy(algorithm);
    }
    const std::string rawQop = paramValue("qop");
    challenge.qop = selectPreferredDigestQop(rawQop);
    challenge.stale = SwRtspHeaderUtils::toLowerCopy(paramValue("stale")) == "true";
    if (challenge.realm.empty() || challenge.nonce.empty() ||
        !supportsDigestAlgorithm(challenge.algorithm) ||
        (!rawQop.empty() && challenge.qop.empty())) {
        return SwRtspAuthChallenge{};
    }
    return challenge;
}

inline bool selectChallenge(const std::string& header, SwRtspAuthChallenge& outChallenge) {
    const auto challenges = SwRtspHeaderUtils::headerValues(header, "WWW-Authenticate:");
    SwRtspAuthChallenge fallback;
    for (const auto& challengeValue : challenges) {
        const SwRtspAuthChallenge challenge = parseChallengeValue(challengeValue);
        if (challenge.scheme == SwRtspAuthChallenge::Scheme::Digest) {
            outChallenge = challenge;
            return true;
        }
        if (challenge.scheme == SwRtspAuthChallenge::Scheme::Basic &&
            fallback.scheme == SwRtspAuthChallenge::Scheme::None) {
            fallback = challenge;
        }
    }
    if (fallback.scheme != SwRtspAuthChallenge::Scheme::None) {
        outChallenge = fallback;
        return true;
    }
    return false;
}

inline std::string escapeQuotedHeaderValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

inline std::string randomHexToken(std::size_t bytes = 8) {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes; ++i) {
        oss << std::setw(2) << dist(rng);
    }
    return oss.str();
}

inline std::string hashForAuth(const std::string& algorithm, const std::string& value) {
    try {
        const std::string lower = SwRtspHeaderUtils::toLowerCopy(algorithm);
        if (lower == "sha-256" || lower == "sha-256-sess") {
            return SwCrypto::hashSHA256(value);
        }
        return SwCrypto::hashMD5(value);
    } catch (...) {
        return std::string();
    }
}

inline std::string buildAuthorizationHeader(const SwRtspAuthChallenge& challenge,
                                            const std::string& userName,
                                            const std::string& password,
                                            const std::string& method,
                                            const std::string& url,
                                            uint32_t& nonceCount) {
    if (userName.empty()) {
        return std::string();
    }
    if (challenge.scheme == SwRtspAuthChallenge::Scheme::Basic) {
        return "Basic " + SwString(userName + ":" + password).toBase64().toStdString();
    }
    if (challenge.scheme != SwRtspAuthChallenge::Scheme::Digest) {
        return std::string();
    }

    const std::string algorithm = challenge.algorithm.empty() ? std::string("md5")
                                                              : challenge.algorithm;
    std::string cnonce = randomHexToken();
    std::ostringstream ncStream;
    ncStream << std::hex << std::setw(8) << std::setfill('0') << (++nonceCount);
    const std::string nc = ncStream.str();

    std::string ha1 = hashForAuth(algorithm, userName + ":" + challenge.realm + ":" + password);
    if (ha1.empty()) {
        return std::string();
    }
    if (algorithm == "md5-sess" || algorithm == "sha-256-sess") {
        ha1 = hashForAuth(algorithm, ha1 + ":" + challenge.nonce + ":" + cnonce);
        if (ha1.empty()) {
            return std::string();
        }
    }

    const std::string ha2 = hashForAuth(algorithm, method + ":" + url);
    if (ha2.empty()) {
        return std::string();
    }

    std::string response;
    if (!challenge.qop.empty()) {
        response = hashForAuth(algorithm,
                               ha1 + ":" + challenge.nonce + ":" + nc + ":" + cnonce + ":" +
                                   challenge.qop + ":" + ha2);
    } else {
        response = hashForAuth(algorithm, ha1 + ":" + challenge.nonce + ":" + ha2);
    }
    if (response.empty()) {
        return std::string();
    }

    std::ostringstream auth;
    auth << "Digest username=\"" << escapeQuotedHeaderValue(userName)
         << "\", realm=\"" << escapeQuotedHeaderValue(challenge.realm)
         << "\", nonce=\"" << escapeQuotedHeaderValue(challenge.nonce)
         << "\", uri=\"" << escapeQuotedHeaderValue(url)
         << "\", response=\"" << response << "\"";
    if (!challenge.algorithm.empty()) {
        auth << ", algorithm=" << challenge.algorithm;
    }
    if (!challenge.opaque.empty()) {
        auth << ", opaque=\"" << escapeQuotedHeaderValue(challenge.opaque) << "\"";
    }
    if (!challenge.qop.empty()) {
        auth << ", qop=" << challenge.qop
             << ", nc=" << nc
             << ", cnonce=\"" << cnonce << "\"";
    }
    return auth.str();
}

} // namespace SwRtspAuth
