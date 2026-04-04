#pragma once

#include "SwByteArray.h"
#include "SwCrypto.h"
#include "SwEmbeddedDb.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwMutex.h"
#include "SwString.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

static constexpr const char* kSwLogCategory_SwHttpAuth = "sw.core.io.swhttpauth";

struct SwHttpAuthMailTemplate {
    SwString subject;
    SwString textBody;
    SwString htmlBody;

    bool isEmpty() const {
        return subject.trimmed().isEmpty() && textBody.trimmed().isEmpty() && htmlBody.trimmed().isEmpty();
    }
};

struct SwHttpAuthMailConfig {
    SwString fromAddress;
    SwHttpAuthMailTemplate verificationTemplate;
    SwHttpAuthMailTemplate resetPasswordTemplate;
    SwString verificationUrlTemplate;
    SwString resetPasswordUrlTemplate;
};

struct SwHttpAuthConfig {
    SwString routePrefix = "/api/auth";
    SwString storageDir = "auth";
    SwEmbeddedDbOptions dbOptions;
    SwString sessionCookieName = "sw_auth";
    unsigned long long sessionTtlMs = 7ull * 24ull * 60ull * 60ull * 1000ull;
    int passwordMinLength = 8;
    bool requireVerifiedEmailForLogin = true;
    bool verificationRequired = true;
    unsigned long long verificationCodeTtlMs = 15ull * 60ull * 1000ull;
    unsigned long long resetCodeTtlMs = 15ull * 60ull * 1000ull;
    SwString publicBaseUrl;
    SwHttpAuthMailConfig mail;
};

struct SwHttpAuthApiOptions {
    SwString routePrefix = "/api/auth";
};

struct SwHttpAuthAccount {
    SwString accountId;
    SwString subjectId;
    SwString email;
    SwString passwordHash;
    SwString emailVerifiedAt;
    bool suspended = false;
    SwString createdAt;
    SwString updatedAt;
};

struct SwHttpAuthSession {
    SwString sessionId;
    SwString accountId;
    SwString tokenHash;
    long long expiresAtMs = 0;
    SwString userAgent;
    bool viaTls = false;
    SwString createdAt;
    SwString updatedAt;
};

struct SwHttpAuthChallenge {
    SwString challengeId;
    SwString purpose;
    SwString accountId;
    SwString code;
    SwString tokenHash;
    long long expiresAtMs = 0;
    SwString consumedAt;
    SwString createdAt;
    SwString updatedAt;
};

struct SwHttpAuthOutgoingMail {
    SwString purpose;
    SwString accountId;
    SwString email;
    SwString fromAddress;
    SwList<SwString> to;
    SwString subject;
    SwString textBody;
    SwString htmlBody;
    SwString code;
    SwString url;
};

struct SwHttpAuthIdentity {
    bool authenticated = false;
    bool emailVerified = false;
    SwHttpAuthAccount account;
    SwHttpAuthSession session;
    SwJsonValue subject;
};

using SwHttpAuthRegisterSubjectHook =
    std::function<bool(const SwString&, const SwJsonValue&, SwString&, SwJsonValue&, SwString&)>;
using SwHttpAuthLoadSubjectHook =
    std::function<bool(const SwString&, SwJsonValue&, SwString&)>;
using SwHttpAuthDeliverMailHook =
    std::function<bool(const SwHttpAuthOutgoingMail&, SwString&)>;
using SwHttpAuthLifecycleHook =
    std::function<void(const SwHttpAuthAccount&, const SwJsonValue&)>;

struct SwHttpAuthHooks {
    SwHttpAuthRegisterSubjectHook registerSubject;
    SwHttpAuthLoadSubjectHook loadSubject;
    SwHttpAuthDeliverMailHook deliverMail;
    SwHttpAuthLifecycleHook onEmailVerified;
    SwHttpAuthLifecycleHook onPasswordChanged;
};

namespace swHttpAuthDetail {

inline SwString normalizeEmail(const SwString& email) {
    SwString value = email.trimmed().toLower();
    const int displayStart = value.lastIndexOf("<");
    const int displayEnd = value.lastIndexOf(">");
    if (displayStart >= 0 && displayEnd > displayStart) {
        value = value.mid(displayStart + 1, displayEnd - displayStart - 1).trimmed().toLower();
    }
    return value;
}

inline bool splitEmail(const SwString& email, SwString& outLocalPart, SwString& outDomain) {
    outLocalPart.clear();
    outDomain.clear();
    const SwString normalized = normalizeEmail(email);
    const int atPos = normalized.indexOf("@");
    if (atPos <= 0 || atPos >= normalized.size() - 1) {
        return false;
    }
    if (normalized.indexOf("@", atPos + 1) >= 0) {
        return false;
    }
    outLocalPart = normalized.left(atPos);
    outDomain = normalized.mid(atPos + 1);
    return !outLocalPart.isEmpty() && !outDomain.isEmpty() && !outDomain.contains(" ");
}

inline long long currentEpochMs() {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());
}

inline SwString isoTimestampFromUnixSeconds(std::time_t utcSeconds) {
    std::tm utcTime {};
#if defined(_WIN32)
    gmtime_s(&utcTime, &utcSeconds);
#else
    gmtime_r(&utcSeconds, &utcTime);
#endif
    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(4) << (utcTime.tm_year + 1900)
           << "-" << std::setw(2) << (utcTime.tm_mon + 1)
           << "-" << std::setw(2) << utcTime.tm_mday
           << "T" << std::setw(2) << utcTime.tm_hour
           << ":" << std::setw(2) << utcTime.tm_min
           << ":" << std::setw(2) << utcTime.tm_sec
           << "Z";
    return SwString(stream.str());
}

inline SwString currentIsoTimestamp() {
    return isoTimestampFromUnixSeconds(std::time(nullptr));
}

inline SwString normalizeRoutePrefix(const SwString& routePrefix) {
    SwString normalized = routePrefix.trimmed();
    if (normalized.isEmpty()) {
        normalized = "/api/auth";
    }
    if (!normalized.startsWith("/")) {
        normalized.prepend("/");
    }
    while (normalized.size() > 1 && normalized.endsWith("/")) {
        normalized.chop(1);
    }
    return normalized;
}

inline SwString normalizeBaseUrl(const SwString& baseUrl) {
    SwString normalized = baseUrl.trimmed();
    while (normalized.size() > 1 && normalized.endsWith("/")) {
        normalized.chop(1);
    }
    return normalized;
}

inline SwString generateId(const SwString& prefix) {
    static SwMutex mutex;
    static unsigned long long counter = 0;
    unsigned long long localCounter = 0;
    {
        SwMutexLocker locker(&mutex);
        ++counter;
        localCounter = counter;
    }
    return prefix + "-" + SwString::number(currentEpochMs()) + "-" + SwString::number(localCounter);
}

inline bool fillRandomBytes(std::vector<unsigned char>& outBytes, std::size_t byteCount) {
    outBytes.assign(byteCount, 0);
    if (byteCount == 0) {
        return true;
    }
    return RAND_bytes(outBytes.data(), static_cast<int>(byteCount)) == 1;
}

inline SwString hexEncodeBytes(const std::vector<unsigned char>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char value = bytes[i];
        encoded.push_back(digits[(value >> 4) & 0x0f]);
        encoded.push_back(digits[value & 0x0f]);
    }
    return SwString(encoded);
}

inline SwString randomHexToken(std::size_t byteCount = 32) {
    std::vector<unsigned char> bytes;
    if (!fillRandomBytes(bytes, byteCount)) {
        return SwString();
    }
    return hexEncodeBytes(bytes);
}

inline SwString hashSha256(const SwString& value) {
    return SwString(SwCrypto::hashSHA256(value.toStdString()));
}

inline bool isModernPasswordHash(const SwString& storedHash) {
    return storedHash.startsWith("pbkdf2_sha256$");
}

inline SwString makeLegacySha1PasswordHash(const SwString& password) {
    const std::vector<unsigned char> sha = SwCrypto::generateHashSHA1(password.toStdString());
    return SwString(SwCrypto::base64Encode(sha));
}

inline SwString makePasswordHash(const SwString& password, int iterations = 120000) {
    if (password.isEmpty()) {
        return SwString();
    }
    std::vector<unsigned char> saltBytes;
    if (!fillRandomBytes(saltBytes, 16)) {
        return SwString();
    }

    SwByteArray derived;
    derived.resize(32);
    const std::string passwordUtf8 = password.toStdString();
    if (PKCS5_PBKDF2_HMAC(passwordUtf8.c_str(),
                          static_cast<int>(passwordUtf8.size()),
                          saltBytes.data(),
                          static_cast<int>(saltBytes.size()),
                          iterations,
                          EVP_sha256(),
                          static_cast<int>(derived.size()),
                          reinterpret_cast<unsigned char*>(derived.data())) != 1) {
        return SwString();
    }

    const SwByteArray saltArray(reinterpret_cast<const char*>(saltBytes.data()), saltBytes.size());
    return "pbkdf2_sha256$" +
           SwString::number(iterations) +
           "$" + SwString(saltArray.toBase64().toStdString()) +
           "$" + SwString(derived.toBase64().toStdString());
}

inline bool constantTimeEquals(const SwByteArray& lhs, const SwByteArray& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<unsigned char>(lhs.at(i) ^ rhs.at(i));
    }
    return diff == 0;
}

inline bool verifyPasswordHash(const SwString& storedHash, const SwString& password) {
    if (!isModernPasswordHash(storedHash)) {
        const SwByteArray lhs(storedHash.toUtf8());
        const SwByteArray rhs(makeLegacySha1PasswordHash(password).toUtf8());
        return constantTimeEquals(lhs, rhs);
    }

    const SwList<SwString> parts = storedHash.split('$');
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") {
        return false;
    }

    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    if (!ok || iterations <= 0) {
        return false;
    }

    const SwByteArray salt = SwByteArray::fromBase64(SwByteArray(parts[2].toStdString()));
    const SwByteArray expected = SwByteArray::fromBase64(SwByteArray(parts[3].toStdString()));
    if (salt.isEmpty() || expected.isEmpty()) {
        return false;
    }

    SwByteArray derived;
    derived.resize(expected.size());
    const std::string passwordUtf8 = password.toStdString();
    if (PKCS5_PBKDF2_HMAC(passwordUtf8.c_str(),
                          static_cast<int>(passwordUtf8.size()),
                          reinterpret_cast<const unsigned char*>(salt.constData()),
                          static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          static_cast<int>(derived.size()),
                          reinterpret_cast<unsigned char*>(derived.data())) != 1) {
        return false;
    }

    return constantTimeEquals(expected, derived);
}

inline SwString generateChallengeCode(std::size_t length = 8) {
    static const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::vector<unsigned char> bytes;
    if (!fillRandomBytes(bytes, length)) {
        return SwString();
    }
    SwString code;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        code += kAlphabet[bytes[i] % 32];
    }
    return code;
}

inline SwString parseCookieValue(const SwString& cookieHeader, const SwString& cookieName) {
    const SwList<SwString> parts = cookieHeader.split(';');
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const SwString part = parts[i].trimmed();
        const int equals = part.indexOf('=');
        if (equals <= 0) {
            continue;
        }
        const SwString key = part.left(equals).trimmed();
        if (key == cookieName) {
            return part.mid(equals + 1).trimmed();
        }
    }
    return SwString();
}

inline SwString extractBearerToken(const SwString& authorizationHeader) {
    const SwString trimmed = authorizationHeader.trimmed();
    if (!trimmed.toLower().startsWith("bearer ")) {
        return SwString();
    }
    return trimmed.mid(7).trimmed();
}

inline SwString buildSessionCookie(const SwString& cookieName,
                                   const SwString& token,
                                   long long maxAgeSeconds,
                                   bool secure) {
    SwString cookie = cookieName + "=" + token + "; Path=/; HttpOnly; SameSite=Lax";
    if (secure) {
        cookie += "; Secure";
    }
    if (maxAgeSeconds >= 0) {
        cookie += "; Max-Age=" + SwString::number(maxAgeSeconds);
    }
    return cookie;
}

inline SwJsonArray toJsonArray(const SwList<SwString>& values) {
    SwJsonArray array;
    for (std::size_t i = 0; i < values.size(); ++i) {
        array.append(values[i].toStdString());
    }
    return array;
}

inline SwString accountKey(const SwString& accountId) {
    return "auth/account/" + accountId;
}

inline SwString accountsByEmailSecondaryKey(const SwString& email) {
    return normalizeEmail(email);
}

inline SwString sessionsByAccountSecondaryKey(const SwString& accountId, const SwString& sessionId) {
    return accountId + "\x1f" + sessionId;
}

inline SwString challengeByAccountPurposeSecondaryKey(const SwString& accountId,
                                                      const SwString& purpose,
                                                      const SwString& challengeId) {
    return accountId + "\x1f" + purpose + "\x1f" + challengeId;
}

inline SwString challengeByTokenSecondaryKey(const SwString& tokenHash) {
    return tokenHash;
}

inline SwString challengeByCodeSecondaryKey(const SwString& purpose,
                                            const SwString& code,
                                            const SwString& challengeId) {
    return purpose + "\x1f" + code + "\x1f" + challengeId;
}

inline SwByteArray jsonToBytes(const SwJsonObject& object) {
    return SwByteArray(SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact).toStdString());
}

inline bool parseJsonObject(const SwByteArray& bytes, SwJsonObject& outObject) {
    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(bytes.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        return false;
    }
    outObject = document.object();
    return true;
}

inline SwString replaceTemplatePlaceholders(const SwString& source,
                                            const SwString& code,
                                            const SwString& url) {
    SwString rendered = source;
    rendered.replace("{{code}}", code);
    rendered.replace("{{url}}", url);
    return rendered;
}

inline SwString replaceUrlTemplateToken(const SwString& source, const SwString& token) {
    SwString rendered = source;
    rendered.replace("{{token}}", token);
    return rendered;
}

inline SwString safeJsonString(const SwJsonValue& value) {
    return SwString(value.toJsonString());
}

} // namespace swHttpAuthDetail
