#pragma once

#include "SwByteArray.h"
#include "SwCrypto.h"
#include "SwDateTime.h"
#include "SwEmbeddedDb.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwMutex.h"
#include "SwString.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static constexpr const char* kSwLogCategory_SwMail = "sw.core.io.swmail";

struct SwDomainTlsConfig {
    enum Mode {
        Disabled = 0,
        Manual,
        Acme
    };

    Mode mode = Disabled;
    SwString domain;
    SwString mailHost;
    SwList<SwString> subjectAlternativeNames;
    SwString contactEmail;
    SwString acmeDirectoryUrl = "https://acme-v02.api.letsencrypt.org/directory";
    SwString storageDir = "acme";
    SwString trustedCaFile;
    SwString certPath;
    SwString keyPath;
    uint16_t httpPort = 80;
    uint16_t httpsPort = 443;
};

struct SwMailConfig {
    struct OutboundRelay {
        SwString host;
        uint16_t port = 0;
        SwString username;
        SwString password;
        SwString trustedCaFile;
        bool implicitTls = false;
        bool startTls = false;
    };

    SwString domain;
    SwString mailHost;
    SwString storageDir = "mail";
    SwEmbeddedDbOptions dbOptions;
    uint16_t smtpPort = 25;
    uint16_t submissionPort = 587;
    uint16_t imapsPort = 993;
    unsigned long long maxMessageBytes = 25ull * 1024ull * 1024ull;
    unsigned long long accountDefaultQuotaBytes = 1024ull * 1024ull * 1024ull;
    unsigned long long queueRetryBaseMs = 30ull * 1000ull;
    unsigned long long queueMaxAgeMs = 5ull * 24ull * 60ull * 60ull * 1000ull;
    unsigned long long sessionIdleTimeoutMs = 5ull * 60ull * 1000ull;
    int authThrottleWindowMs = 60 * 1000;
    int authThrottleMaxAttempts = 8;
    SwString adminRoutePrefix = "/api/admin/mail";
    bool enableDkimSigning = true;
    OutboundRelay outboundRelay;
};

struct SwMailAdminApiOptions {
    SwString routePrefix = "/api/admin/mail";
};

struct SwMailAccount {
    SwString address;
    SwString domain;
    SwString localPart;
    SwString passwordSalt;
    SwString passwordHash;
    bool active = true;
    bool canReceive = true;
    bool canSend = true;
    bool suspended = false;
    unsigned long long quotaBytes = 0;
    unsigned long long usedBytes = 0;
    SwString createdAt;
    SwString updatedAt;
};

struct SwMailAlias {
    SwString address;
    SwString domain;
    SwString localPart;
    SwList<SwString> targets;
    bool active = true;
    SwString createdAt;
    SwString updatedAt;
};

struct SwMailMailbox {
    SwString accountAddress;
    SwString name;
    unsigned long long uidNext = 1;
    unsigned long long totalCount = 0;
    unsigned long long unseenCount = 0;
    SwString createdAt;
    SwString updatedAt;
};

struct SwMailMessageEntry {
    SwString accountAddress;
    SwString mailboxName;
    unsigned long long uid = 0;
    SwList<SwString> flags;
    SwString internalDate;
    SwString subject;
    SwString from;
    SwList<SwString> to;
    SwList<SwString> cc;
    SwList<SwString> bcc;
    SwString messageId;
    unsigned long long sizeBytes = 0;
    SwByteArray rawMessage;
};

struct SwMailEnvelope {
    SwString mailFrom;
    SwList<SwString> rcptTo;
};

struct SwMailQueueItem {
    SwString id;
    SwMailEnvelope envelope;
    SwByteArray rawMessage;
    int attemptCount = 0;
    long long createdAtMs = 0;
    long long updatedAtMs = 0;
    long long nextAttemptAtMs = 0;
    long long expireAtMs = 0;
    SwString lastError;
    SwString dkimDomain;
    SwString dkimSelector;
    bool signedMessage = false;
};

struct SwMailDkimRecord {
    SwString domain;
    SwString selector;
    SwString privateKeyPem;
    SwString publicKeyTxt;
    SwString createdAt;
    SwString updatedAt;
};

struct SwMailMxRecord {
    int preference = 0;
    SwString exchange;
};

struct SwMailDnsTxtRecord {
    SwString value;
};

struct SwMailMetrics {
    unsigned long long smtpSessions = 0;
    unsigned long long submissionSessions = 0;
    unsigned long long imapSessions = 0;
    unsigned long long inboundAccepted = 0;
    unsigned long long localDeliveries = 0;
    unsigned long long outboundQueued = 0;
    unsigned long long outboundDelivered = 0;
    unsigned long long outboundDeferred = 0;
    unsigned long long outboundFailed = 0;
    unsigned long long authFailures = 0;
};

namespace swMailDetail {

inline SwString trimAngleBrackets(const SwString& value) {
    SwString out = value.trimmed();
    if (out.startsWith("<") && out.endsWith(">") && out.size() >= 2) {
        out = out.mid(1, out.size() - 2).trimmed();
    }
    return out;
}

inline SwString stripSmtpPathDecorators(const SwString& value) {
    SwString out = trimAngleBrackets(value);
    if (out.startsWith("mailto:")) {
        out = out.mid(7);
    }
    return out.trimmed();
}

inline SwString normalizeMailboxName(const SwString& name) {
    SwString out = name.trimmed();
    if (out.isEmpty()) {
        return "INBOX";
    }
    if (out.toUpper() == "INBOX") {
        return "INBOX";
    }
    return out;
}

inline SwString canonicalAddress(const SwString& address) {
    SwString value = stripSmtpPathDecorators(address).trimmed().toLower();
    const int displayStart = value.lastIndexOf("<");
    const int displayEnd = value.lastIndexOf(">");
    if (displayStart >= 0 && displayEnd > displayStart) {
        value = value.mid(displayStart + 1, displayEnd - displayStart - 1).trimmed().toLower();
    }
    return value;
}

inline bool splitAddress(const SwString& address, SwString& localPart, SwString& domain) {
    const SwString canonical = canonicalAddress(address);
    const int atPos = canonical.indexOf("@");
    if (atPos <= 0 || atPos >= canonical.size() - 1) {
        return false;
    }
    localPart = canonical.left(atPos);
    domain = canonical.mid(atPos + 1);
    return !localPart.isEmpty() && !domain.isEmpty();
}

inline SwString paddedNumber(unsigned long long value, int width = 20) {
    std::ostringstream stream;
    stream.width(width);
    stream.fill('0');
    stream << value;
    return SwString(stream.str());
}

inline long long currentEpochMs() {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());
}

inline std::tm currentUtcTimeTm() {
    const std::time_t now = std::time(nullptr);
    std::tm utcTime {};
#if defined(_WIN32)
    gmtime_s(&utcTime, &now);
#else
    gmtime_r(&now, &utcTime);
#endif
    return utcTime;
}

inline SwString currentIsoTimestamp() {
    const std::tm utcTime = currentUtcTimeTm();
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(4) << (utcTime.tm_year + 1900)
           << "-" << std::setw(2) << (utcTime.tm_mon + 1)
           << "-" << std::setw(2) << utcTime.tm_mday
           << "T" << std::setw(2) << utcTime.tm_hour
           << ":" << std::setw(2) << utcTime.tm_min
           << ":" << std::setw(2) << utcTime.tm_sec
           << "Z";
    return SwString(stream.str());
}

inline SwString isoTimestampFromUnixSeconds(std::time_t utcSeconds) {
    std::tm utcTime {};
#if defined(_WIN32)
    gmtime_s(&utcTime, &utcSeconds);
#else
    gmtime_r(&utcSeconds, &utcTime);
#endif
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(4) << (utcTime.tm_year + 1900)
           << "-" << std::setw(2) << (utcTime.tm_mon + 1)
           << "-" << std::setw(2) << utcTime.tm_mday
           << "T" << std::setw(2) << utcTime.tm_hour
           << ":" << std::setw(2) << utcTime.tm_min
           << ":" << std::setw(2) << utcTime.tm_sec
           << "Z";
    return SwString(stream.str());
}

inline int monthIndexFromShortName(const SwString& month) {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const SwString normalized = month.trimmed().left(3).toLower();
    for (int i = 0; i < 12; ++i) {
        if (normalized == SwString(kMonths[i]).toLower()) {
            return i;
        }
    }
    return -1;
}

inline bool parseImapInternalDate(const SwString& rawValue, SwString* outIsoTimestamp) {
    if (outIsoTimestamp) {
        outIsoTimestamp->clear();
    }

    const SwList<SwString> parts = rawValue.trimmed().split(' ');
    if (parts.size() != 3) {
        return false;
    }

    const SwList<SwString> dateParts = parts[0].split('-');
    const SwList<SwString> timeParts = parts[1].split(':');
    const SwString timezonePart = parts[2].trimmed();
    if (dateParts.size() != 3 || timeParts.size() != 3 || timezonePart.size() != 5) {
        return false;
    }

    auto parseDecimal = [](const SwString& text, bool* ok) -> int {
        if (ok) {
            *ok = false;
        }
        const std::string raw = text.trimmed().toStdString();
        if (raw.empty()) {
            return 0;
        }
        char* end = nullptr;
        const long value = std::strtol(raw.c_str(), &end, 10);
        if (!end || *end != '\0') {
            return 0;
        }
        if (ok) {
            *ok = true;
        }
        return static_cast<int>(value);
    };

    bool dayOk = false;
    bool yearOk = false;
    bool hourOk = false;
    bool minuteOk = false;
    bool secondOk = false;
    bool timezoneOk = false;
    const int day = parseDecimal(dateParts[0], &dayOk);
    const int year = parseDecimal(dateParts[2], &yearOk);
    const int hour = parseDecimal(timeParts[0], &hourOk);
    const int minute = parseDecimal(timeParts[1], &minuteOk);
    const int second = parseDecimal(timeParts[2], &secondOk);
    const char timezoneSign = timezonePart[0];
    const int timezone = parseDecimal(timezonePart.mid(1), &timezoneOk);
    if (!dayOk || !yearOk || !hourOk || !minuteOk || !secondOk || !timezoneOk ||
        (timezoneSign != '+' && timezoneSign != '-')) {
        return false;
    }

    const int monthIndex = monthIndexFromShortName(dateParts[1]);
    if (monthIndex < 0 || day < 1 || day > 31 || year < 1970 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return false;
    }

    const int timezoneHours = timezone / 100;
    const int timezoneMinutes = timezone % 100;
    if (timezoneHours < 0 || timezoneHours > 23 || timezoneMinutes < 0 || timezoneMinutes > 59) {
        return false;
    }

    std::tm utcTime {};
    utcTime.tm_year = year - 1900;
    utcTime.tm_mon = monthIndex;
    utcTime.tm_mday = day;
    utcTime.tm_hour = hour;
    utcTime.tm_min = minute;
    utcTime.tm_sec = second;
    utcTime.tm_isdst = -1;

#if defined(_WIN32)
    std::time_t utcSeconds = _mkgmtime(&utcTime);
#else
    std::time_t utcSeconds = timegm(&utcTime);
#endif
    if (utcSeconds == static_cast<std::time_t>(-1)) {
        return false;
    }

    const int offsetSeconds = (timezoneHours * 60 + timezoneMinutes) * 60;
    if (timezoneSign == '+') {
        utcSeconds -= offsetSeconds;
    } else {
        utcSeconds += offsetSeconds;
    }

    if (outIsoTimestamp) {
        *outIsoTimestamp = isoTimestampFromUnixSeconds(utcSeconds);
    }
    return true;
}

inline SwString smtpDateNow() {
    static const char* kWeekDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::tm utcTime = currentUtcTimeTm();
    std::ostringstream stream;
    stream << kWeekDays[std::max(0, std::min(6, utcTime.tm_wday))]
           << ", "
           << std::setfill('0') << std::setw(2) << utcTime.tm_mday
           << " "
           << kMonths[std::max(0, std::min(11, utcTime.tm_mon))]
           << " "
           << std::setw(4) << (utcTime.tm_year + 1900)
           << " "
           << std::setw(2) << utcTime.tm_hour
           << ":" << std::setw(2) << utcTime.tm_min
           << ":" << std::setw(2) << utcTime.tm_sec
           << " +0000";
    return SwString(stream.str());
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

inline SwString defaultMailHost(const SwString& domain) {
    const SwString normalized = domain.trimmed().toLower();
    if (normalized.isEmpty()) {
        return SwString();
    }
    return "mail." + normalized;
}

inline SwString normalizeDomain(const SwString& domain) {
    return domain.trimmed().toLower();
}

inline SwString normalizeMailHost(const SwString& host, const SwString& domain) {
    const SwString normalized = host.trimmed().toLower();
    return normalized.isEmpty() ? defaultMailHost(domain) : normalized;
}

inline SwString messageIdDomain(const SwMailConfig& config) {
    const SwString host = normalizeMailHost(config.mailHost, config.domain);
    return host.isEmpty() ? normalizeDomain(config.domain) : host;
}

inline SwString generateMessageId(const SwMailConfig& config) {
    return "<" + generateId("msg") + "@" + messageIdDomain(config) + ">";
}

inline SwString makePasswordSalt() {
    return SwString(SwCrypto::hashSHA256(generateId("salt").toStdString())).left(24);
}

inline SwString hashPassword(const SwString& salt, const SwString& password) {
    return SwString(SwCrypto::hashSHA256((salt + ":" + password).toStdString()));
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

inline SwJsonArray toJsonArray(const SwList<SwString>& values) {
    SwJsonArray array;
    for (std::size_t i = 0; i < values.size(); ++i) {
        array.append(values[i].toStdString());
    }
    return array;
}

inline SwList<SwString> fromJsonStringArray(const SwJsonValue& value) {
    SwList<SwString> out;
    if (!value.isArray()) {
        return out;
    }
    const SwJsonArray array = value.toArray();
    for (std::size_t i = 0; i < array.size(); ++i) {
        out.append(SwString(array[i].toString()));
    }
    return out;
}

inline SwString keyForDomainScoped(const SwString& prefix, const SwString& domain, const SwString& localPart) {
    return prefix + "/" + normalizeDomain(domain) + "/" + localPart.trimmed().toLower();
}

inline SwString accountKey(const SwString& address) {
    return "acct/" + canonicalAddress(address);
}

inline SwString aliasKey(const SwString& domain, const SwString& localPart) {
    return keyForDomainScoped("alias", domain, localPart);
}

inline SwString mailboxKey(const SwString& accountAddress, const SwString& mailboxName) {
    return "mbx/" + canonicalAddress(accountAddress) + "/" + normalizeMailboxName(mailboxName);
}

inline SwString messageKey(const SwString& accountAddress,
                           const SwString& mailboxName,
                           unsigned long long uid) {
    return "msg/" + canonicalAddress(accountAddress) + "/" + normalizeMailboxName(mailboxName) + "/" +
           paddedNumber(uid);
}

inline SwString queueKey(const SwString& id) {
    return "queue/" + id.trimmed();
}

inline SwString dkimKey(const SwString& domain, const SwString& selector) {
    return "dkim/" + normalizeDomain(domain) + "/" + selector.trimmed().toLower();
}

inline SwString queueDueSecondaryKey(long long dueEpochMs, const SwString& id) {
    return paddedNumber(static_cast<unsigned long long>(std::max<long long>(0, dueEpochMs)), 20) + "\x1f" + id;
}

inline SwString messagesSecondaryKey(const SwString& accountAddress,
                                     const SwString& mailboxName,
                                     unsigned long long uid) {
    return canonicalAddress(accountAddress) + "\x1f" + normalizeMailboxName(mailboxName) + "\x1f" +
           paddedNumber(uid);
}

inline SwString mailboxesSecondaryKey(const SwString& accountAddress, const SwString& mailboxName) {
    const SwString normalizedMailbox = mailboxName.trimmed();
    if (normalizedMailbox.isEmpty()) {
        return canonicalAddress(accountAddress) + "\x1f";
    }
    return canonicalAddress(accountAddress) + "\x1f" + normalizeMailboxName(normalizedMailbox);
}

inline SwString accountsSecondaryKey(const SwString& domain, const SwString& address) {
    return normalizeDomain(domain) + "\x1f" + canonicalAddress(address);
}

inline SwString aliasesSecondaryKey(const SwString& domain, const SwString& localPart) {
    return normalizeDomain(domain) + "\x1f" + localPart.trimmed().toLower();
}

inline SwString dkimSecondaryKey(const SwString& domain, const SwString& selector) {
    return normalizeDomain(domain) + "\x1f" + selector.trimmed().toLower();
}

inline SwString toLineEndingCrlf(const SwString& text) {
    std::string input = text.toStdString();
    std::string out;
    out.reserve(input.size() + 16);
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '\r') {
            out.push_back('\r');
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                out.push_back('\n');
                ++i;
            } else {
                out.push_back('\n');
            }
        } else if (c == '\n') {
            out.push_back('\r');
            out.push_back('\n');
        } else {
            out.push_back(c);
        }
    }
    return SwString(out);
}

inline SwByteArray ensureMessageEnvelopeHeaders(const SwMailConfig& config,
                                                const SwByteArray& rawInput,
                                                const SwString& fromAddress,
                                                const SwList<SwString>& recipients) {
    SwString raw = toLineEndingCrlf(SwString(rawInput.toStdString()));
    const SwString lower = raw.toLower();
    if (lower.indexOf("\r\nmessage-id:") < 0 && !lower.startsWith("message-id:")) {
        raw = "Message-Id: " + generateMessageId(config) + "\r\n" + raw;
    }
    if (lower.indexOf("\r\ndate:") < 0 && !lower.startsWith("date:")) {
        raw = "Date: " + smtpDateNow() + "\r\n" + raw;
    }
    if (lower.indexOf("\r\nfrom:") < 0 && !lower.startsWith("from:") && !fromAddress.isEmpty()) {
        raw = "From: <" + canonicalAddress(fromAddress) + ">\r\n" + raw;
    }
    if (lower.indexOf("\r\nto:") < 0 && !lower.startsWith("to:") && !recipients.isEmpty()) {
        SwString joined;
        for (std::size_t i = 0; i < recipients.size(); ++i) {
            if (!joined.isEmpty()) {
                joined += ", ";
            }
            joined += "<" + canonicalAddress(recipients[i]) + ">";
        }
        raw = "To: " + joined + "\r\n" + raw;
    }
    if (raw.indexOf("\r\n\r\n") < 0) {
        raw += "\r\n";
    }
    return SwByteArray(raw.toStdString());
}

inline SwMap<SwString, SwString> parseHeaders(const SwByteArray& rawMessage) {
    SwMap<SwString, SwString> headers;
    const std::string raw = rawMessage.toStdString();
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    const std::size_t textEnd = (headerEnd == std::string::npos) ? raw.size() : headerEnd;

    std::string currentName;
    std::string currentValue;
    std::size_t pos = 0;
    while (pos < textEnd) {
        std::size_t lineEnd = raw.find("\r\n", pos);
        if (lineEnd == std::string::npos || lineEnd > textEnd) {
            lineEnd = textEnd;
        }
        const std::string line = raw.substr(pos, lineEnd - pos);
        pos = (lineEnd >= textEnd) ? textEnd : lineEnd + 2;

        if (line.empty()) {
            break;
        }
        if (!currentName.empty() && (line[0] == ' ' || line[0] == '\t')) {
            currentValue += " " + SwString(line).trimmed().toStdString();
            continue;
        }
        if (!currentName.empty()) {
            headers[SwString(currentName).toLower()] = SwString(currentValue).trimmed();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            currentName.clear();
            currentValue.clear();
            continue;
        }
        currentName = line.substr(0, colon);
        currentValue = line.substr(colon + 1);
    }

    if (!currentName.empty()) {
        headers[SwString(currentName).toLower()] = SwString(currentValue).trimmed();
    }
    return headers;
}

inline SwList<SwString> parseAddressListHeader(const SwString& value) {
    SwList<SwString> out;
    std::string current;
    const std::string input = value.toStdString();
    bool inQuotes = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '"') {
            inQuotes = !inQuotes;
            current.push_back(c);
            continue;
        }
        if (c == ',' && !inQuotes) {
            const SwString canonical = canonicalAddress(SwString(current));
            if (!canonical.isEmpty()) {
                out.append(canonical);
            }
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    const SwString canonical = canonicalAddress(SwString(current));
    if (!canonical.isEmpty()) {
        out.append(canonical);
    }
    return out;
}

inline SwString headerValue(const SwByteArray& rawMessage, const SwString& key) {
    const SwMap<SwString, SwString> headers = parseHeaders(rawMessage);
    return headers.value(key.toLower());
}

inline SwList<SwString> defaultMailboxNames() {
    SwList<SwString> names;
    names.append("INBOX");
    names.append("Sent");
    names.append("Drafts");
    names.append("Trash");
    names.append("Junk");
    return names;
}

inline SwList<SwString> normalizeRecipients(const SwList<SwString>& input) {
    SwList<SwString> out;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const SwString canonical = canonicalAddress(input[i]);
        if (canonical.isEmpty()) {
            continue;
        }
        bool exists = false;
        for (std::size_t j = 0; j < out.size(); ++j) {
            if (out[j] == canonical) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            out.append(canonical);
        }
    }
    return out;
}

inline SwString dotStuffMessage(const SwByteArray& raw) {
    const std::string input = toLineEndingCrlf(SwString(raw.toStdString())).toStdString();
    std::string out;
    out.reserve(input.size() + 32);
    bool lineStart = true;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (lineStart && c == '.') {
            out.push_back('.');
        }
        out.push_back(c);
        if (c == '\n') {
            lineStart = true;
        } else if (c != '\r') {
            lineStart = false;
        }
    }
    if (out.size() < 2 || out.substr(out.size() - 2) != "\r\n") {
        out += "\r\n";
    }
    out += ".\r\n";
    return SwString(out);
}

} // namespace swMailDetail
