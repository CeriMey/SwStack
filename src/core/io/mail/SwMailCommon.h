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
#include <functional>
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
    // Lot 2 : authentification du courrier entrant (chemin port 25 uniquement).
    // Tag = évalue + ajoute Authentication-Results sans jamais rejeter (défaut).
    // Enforce = peut rejeter, mais seulement sur policy DMARC explicite.
    enum class InboundAuthMode { Off = 0, Tag, Enforce };

    struct OutboundRelay {
        SwString host;
        uint16_t port = 0;
        SwString username;
        SwString password;
        SwString trustedCaFile;
        bool implicitTls = false;
        bool startTls = false;
        // Expéditeurs d'enveloppe qui empruntent le relais : adresse complète ou
        // « @domaine ». Vide : tout le courrier sortant. Un relais transactionnel
        // n'accepte que les domaines authentifiés chez lui ; le reste (domaines
        // des clients, transferts SRS, avis de non-remise) part en direct vers
        // les MX du destinataire.
        SwList<SwString> senderAddresses;
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
    // --- Lot 2 inbound auth ---
    InboundAuthMode inboundAuthMode = InboundAuthMode::Tag;
    bool inboundDmarcEnforceReject = true;  // verrou de sécurité ; un échec SPF/DKIM seul ne rejette JAMAIS
    int inboundAuthDnsTimeoutMs = 2500;
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

struct SwMailResolvedForward {
    SwString aliasAddress;
    SwString target;
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
    SwString authResults;  // Lot 2 : valeur Authentication-Results persistée (sans le nom de champ)
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
    unsigned long long forwardedMessages = 0;
    unsigned long long forwardLoopsDropped = 0;
    unsigned long long srsBouncesRouted = 0;
    unsigned long long inboundSpfFail = 0;
    unsigned long long inboundDkimPass = 0;
    unsigned long long inboundDmarcPass = 0;
    unsigned long long inboundDmarcReject = 0;
};

// Lot 2 : événement émis par SwMailService vers l'hôte (in-process). L'hôte VIGIL
// branchera dessus ses webhooks workspace, notifications et activity log.
struct SwMailEvent {
    enum class Type {
        MessageReceived,
        MessageForwarded,
        DeliverySucceeded,
        DeliveryDeferred,
        DeliveryFailed,
        BounceReceived
    };
    Type type = Type::MessageReceived;
    SwString account;
    SwString messageId;
    SwString from;
    SwString to;
    SwString detail;
    SwString error;
    long long timestampMs = 0;

    static SwString typeName(Type t) {
        switch (t) {
            case Type::MessageReceived: return "message.received";
            case Type::MessageForwarded: return "message.forwarded";
            case Type::DeliverySucceeded: return "delivery.succeeded";
            case Type::DeliveryDeferred: return "delivery.deferred";
            case Type::DeliveryFailed: return "delivery.failed";
            case Type::BounceReceived: return "bounce.received";
        }
        return "unknown";
    }
};

// Contenu d'un message sortant, en UTF-8 brut. L'encodage des en-têtes
// (RFC 2047) et des corps (quoted-printable) appartient à
// swMailDetail::composeMessage : un appelant ne pré-encode jamais rien.
struct SwMailComposeRequest {
    SwString fromName;
    SwString fromAddress;
    SwList<SwString> to;
    SwList<SwString> cc;
    SwList<SwString> bcc;
    // L'en-tête Bcc n'existe que dans la copie « Envoyés » de l'expéditeur.
    bool includeBccHeader = false;
    SwString subject;
    SwString textBody;
    SwString htmlBody;
    // Vide : généré depuis la configuration mail.
    SwString messageId;
    // RFC 3834 : message émis par un automate, auquel aucun répondeur ne répond.
    bool autoSubmitted = false;
};

namespace swMailDetail {

inline SwString trimAngleBrackets(const SwString& value) {
    SwString out = value.trimmed();
    if (out.startsWith("<") && out.endsWith(">") && out.size() >= 2) {
        out = out.mid(1, static_cast<int>(out.size() - 2)).trimmed();
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
    const std::size_t displayStart = value.lastIndexOf("<");
    const std::size_t displayEnd = value.lastIndexOf(">");
    if (displayStart != std::string::npos && displayEnd != std::string::npos && displayEnd > displayStart) {
        value = value.mid(static_cast<int>(displayStart + 1),
                          static_cast<int>(displayEnd - displayStart - 1)).trimmed().toLower();
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
        array.append(values[i]);
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

// RFC 5322 §2.2 : un en-tête ne transporte que de l'US-ASCII imprimable. Un
// texte libre y perd ses retours chariot et caractères de contrôle, pour
// qu'aucune valeur ne puisse ouvrir un second en-tête.
inline std::string headerTextSingleLine(const SwString& value) {
    const std::string input = value.toStdString();
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        out.push_back((c < 32 || c == 127) ? ' ' : static_cast<char>(c));
    }
    std::size_t begin = 0;
    while (begin < out.size() && out[begin] == ' ') {
        ++begin;
    }
    std::size_t end = out.size();
    while (end > begin && out[end - 1] == ' ') {
        --end;
    }
    return out.substr(begin, end - begin);
}

inline bool isPrintableAscii(const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 32 || c > 126) {
            return false;
        }
    }
    return true;
}

// RFC 2047 : texte non ASCII d'un en-tête en encoded-words « B » UTF-8. Un
// encoded-word fait au plus 75 caractères et ne coupe jamais une séquence
// UTF-8. 39 octets donnent 52 caractères base64 + 12 d'enveloppe : la première
// ligne tient sous 76 caractères même derrière « Subject: ». Les mots sont
// repliés un par ligne ; le blanc entre deux encoded-words disparaît au décodage.
inline SwString encodeHeaderText(const SwString& value) {
    const std::string text = headerTextSingleLine(value);
    if (isPrintableAscii(text)) {
        return SwString(text);
    }
    static const std::size_t kChunkBytes = 39;
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = std::min(text.size(), pos + kChunkBytes);
        while (end > pos && end < text.size() &&
               (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
        if (end == pos) {
            // UTF-8 invalide : aucune frontière de caractère dans la fenêtre.
            end = std::min(text.size(), pos + kChunkBytes);
        }
        if (!out.empty()) {
            out += "\r\n ";
        }
        out += "=?UTF-8?B?" + SwCrypto::base64Encode(text.substr(pos, end - pos)) + "?=";
        pos = end;
    }
    return SwString(out);
}

// Décode un encoded-word RFC 2047 débutant à `start`. Seuls les jeux convertibles
// sans table (UTF-8, US-ASCII, Latin-1 et ses proches) sont décodés ; tout autre
// reste tel quel dans l'en-tête plutôt que d'être affiché faux.
inline bool decodeEncodedWord(const std::string& input,
                              std::size_t start,
                              std::string& outDecoded,
                              std::size_t& outConsumed) {
    const std::size_t charsetEnd = input.find('?', start + 2);
    if (charsetEnd == std::string::npos || charsetEnd + 2 >= input.size() || input[charsetEnd + 2] != '?') {
        return false;
    }
    const char encoding = static_cast<char>(std::toupper(static_cast<unsigned char>(input[charsetEnd + 1])));
    const std::size_t payloadStart = charsetEnd + 3;
    const std::size_t payloadEnd = input.find("?=", payloadStart);
    if (payloadEnd == std::string::npos) {
        return false;
    }
    std::string charset = input.substr(start + 2, charsetEnd - start - 2);
    for (std::size_t i = 0; i < charset.size(); ++i) {
        charset[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(charset[i])));
    }
    const std::size_t languageTag = charset.find('*');  // RFC 2231 : « charset*langue »
    if (languageTag != std::string::npos) {
        charset.erase(languageTag);
    }
    const std::string payload = input.substr(payloadStart, payloadEnd - payloadStart);
    if (payload.find(' ') != std::string::npos || payload.find('\t') != std::string::npos) {
        return false;
    }

    std::string bytes;
    if (encoding == 'B') {
        const std::vector<unsigned char> raw = SwCrypto::base64Decode(payload);
        bytes.assign(raw.begin(), raw.end());
    } else if (encoding == 'Q') {
        for (std::size_t i = 0; i < payload.size(); ++i) {
            const char c = payload[i];
            if (c == '_') {
                bytes.push_back(' ');
            } else if (c == '=' && i + 2 < payload.size() &&
                       std::isxdigit(static_cast<unsigned char>(payload[i + 1])) &&
                       std::isxdigit(static_cast<unsigned char>(payload[i + 2]))) {
                bytes.push_back(static_cast<char>(std::strtol(payload.substr(i + 1, 2).c_str(), nullptr, 16)));
                i += 2;
            } else {
                bytes.push_back(c);
            }
        }
    } else {
        return false;
    }

    std::string decoded;
    if (charset == "utf-8" || charset == "utf8" || charset == "us-ascii" || charset == "ascii") {
        decoded = bytes;
    } else if (charset == "iso-8859-1" || charset == "latin1" || charset == "iso-8859-15" ||
               charset == "windows-1252" || charset == "cp1252") {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(bytes[i]);
            if (c < 0x80) {
                decoded.push_back(static_cast<char>(c));
            } else {
                decoded.push_back(static_cast<char>(0xC0 | (c >> 6)));
                decoded.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
    } else {
        return false;
    }
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(decoded[i]);
        if (c < 32 || c == 127) {
            decoded[i] = ' ';
        }
    }
    outDecoded = decoded;
    outConsumed = payloadEnd + 2 - start;
    return true;
}

// Inverse d'encodeHeaderText pour l'affichage d'un en-tête déjà déplié
// (parseHeaders). RFC 2047 §6.2 : le blanc qui sépare deux encoded-words
// n'appartient pas au texte.
inline SwString decodeHeaderText(const SwString& value) {
    const std::string input = value.toStdString();
    std::string out;
    std::string gap;
    bool afterEncodedWord = false;
    std::size_t i = 0;
    while (i < input.size()) {
        if (input.compare(i, 2, "=?") == 0) {
            std::string decoded;
            std::size_t consumed = 0;
            if (decodeEncodedWord(input, i, decoded, consumed)) {
                gap.clear();
                out += decoded;
                i += consumed;
                afterEncodedWord = true;
                continue;
            }
        }
        const char c = input[i];
        if (afterEncodedWord && (c == ' ' || c == '\t')) {
            gap.push_back(c);
        } else {
            out += gap;
            gap.clear();
            out.push_back(c);
            afterEncodedWord = false;
        }
        ++i;
    }
    out += gap;
    return SwString(out);
}

// RFC 5322 §3.4 : « nom affiché <adresse> ». Un nom ASCII voyage en
// quoted-string, un nom accentué en encoded-words suivis de l'adresse repliée.
inline SwString formatMailboxHeader(const SwString& displayName, const SwString& address) {
    const std::string angleAddress = "<" + canonicalAddress(address).toStdString() + ">";
    const std::string name = headerTextSingleLine(displayName);
    if (name.empty()) {
        return SwString(angleAddress);
    }
    if (!isPrintableAscii(name)) {
        return SwString(encodeHeaderText(SwString(name)).toStdString() + "\r\n " + angleAddress);
    }
    std::string quoted = "\"";
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '"' || name[i] == '\\') {
            quoted.push_back('\\');
        }
        quoted.push_back(name[i]);
    }
    quoted.push_back('"');
    return SwString(quoted + " " + angleAddress);
}

// Liste d'adresses repliée avant 76 caractères : RFC 5322 §2.1.1 borne une
// ligne à 998 octets, qu'une longue liste de destinataires dépasse.
inline SwString formatAddressListHeader(const SwList<SwString>& addresses) {
    std::string out;
    std::size_t lineLength = 5;  // « Bcc: », le plus long des noms d'en-tête concernés
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        const std::string entry = "<" + canonicalAddress(addresses[i]).toStdString() + ">";
        if (i > 0) {
            if (lineLength + 2 + entry.size() > 76) {
                out += ",\r\n ";
                lineLength = 1;
            } else {
                out += ", ";
                lineLength += 2;
            }
        }
        out += entry;
        lineLength += entry.size();
    }
    return SwString(out);
}

// RFC 2045 §6.7. Le corps sort en ASCII pur et en lignes de 76 caractères au
// plus : ni l'extension SMTP 8BITMIME ni la borne de 998 octets par ligne
// (qu'un HTML d'éditeur riche, écrit sur une seule ligne, dépasse) ne sont
// alors requises du serveur destinataire.
inline SwString encodeQuotedPrintable(const SwString& text) {
    static const char* kHex = "0123456789ABCDEF";
    const std::string input = toLineEndingCrlf(text).toStdString();
    std::string out;
    out.reserve(input.size() + input.size() / 8);
    std::size_t lineLength = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '\r' && i + 1 < input.size() && input[i + 1] == '\n') {
            out += "\r\n";
            lineLength = 0;
            ++i;
            continue;
        }
        const bool endOfLine = (i + 1 == input.size()) || input[i + 1] == '\r';
        const bool literal = (c >= 33 && c <= 126 && c != '=') ||
                             ((c == ' ' || c == '\t') && !endOfLine);
        const std::size_t width = literal ? 1 : 3;
        if (lineLength + width > 75) {
            out += "=\r\n";
            lineLength = 0;
        }
        if (literal) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('=');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
        lineLength += width;
    }
    return SwString(out);
}

// Un fragment (« <p>…</p> ») devient un document complet : une partie
// text/html sans balise <html> est un marqueur classique des filtres antispam.
inline SwString htmlDocumentFromFragment(const SwString& html) {
    if (html.toLower().indexOf("<html") >= 0) {
        return html;
    }
    return SwString("<!DOCTYPE html>\r\n<html>\r\n<head>\r\n<meta charset=\"utf-8\">\r\n"
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\r\n"
                    "</head>\r\n<body>\r\n") +
           html + SwString("\r\n</body>\r\n</html>\r\n");
}

inline std::string mimeTextPart(const std::string& contentType, const SwString& body) {
    std::string part = "Content-Type: " + contentType + "; charset=utf-8\r\n"
                       "Content-Transfer-Encoding: quoted-printable\r\n\r\n";
    part += encodeQuotedPrintable(body).toStdString();
    if (part.size() < 2 || part.compare(part.size() - 2, 2, "\r\n") != 0) {
        part += "\r\n";
    }
    return part;
}

// Unique fabricant des messages sortants. La frontière « =_ » ne peut pas
// apparaître dans un corps quoted-printable, où « = » précède toujours deux
// chiffres hexadécimaux ou une fin de ligne.
inline SwByteArray composeMessage(const SwMailConfig& config, const SwMailComposeRequest& request) {
    std::string message;
    message += "From: " + formatMailboxHeader(request.fromName, request.fromAddress).toStdString() + "\r\n";
    message += "To: " +
               (request.to.isEmpty() ? std::string("undisclosed-recipients:;")
                                     : formatAddressListHeader(request.to).toStdString()) +
               "\r\n";
    if (!request.cc.isEmpty()) {
        message += "Cc: " + formatAddressListHeader(request.cc).toStdString() + "\r\n";
    }
    if (request.includeBccHeader && !request.bcc.isEmpty()) {
        message += "Bcc: " + formatAddressListHeader(request.bcc).toStdString() + "\r\n";
    }
    message += "Subject: " + encodeHeaderText(request.subject).toStdString() + "\r\n";
    message += "Date: " + smtpDateNow().toStdString() + "\r\n";
    const SwString messageId =
        request.messageId.trimmed().isEmpty() ? generateMessageId(config) : request.messageId.trimmed();
    message += "Message-ID: " + messageId.toStdString() + "\r\n";
    if (request.autoSubmitted) {
        message += "Auto-Submitted: auto-generated\r\n";
    }
    message += "MIME-Version: 1.0\r\n";

    if (request.htmlBody.trimmed().isEmpty()) {
        message += mimeTextPart("text/plain", request.textBody);
    } else {
        const std::string boundary =
            "=_sw_" + SwString(SwCrypto::hashSHA256(generateId("boundary").toStdString())).left(24).toStdString();
        message += "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n\r\n";
        message += "--" + boundary + "\r\n" + mimeTextPart("text/plain", request.textBody);
        message += "--" + boundary + "\r\n" + mimeTextPart("text/html", htmlDocumentFromFragment(request.htmlBody));
        message += "--" + boundary + "--\r\n";
    }
    return SwByteArray(message);
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

// Supprime tous les champs d'en-tête nommés `name` (en-tête + lignes de continuation),
// en préservant le corps. Sert à retirer les Authentication-Results forgés avant
// d'insérer le nôtre (RFC 8601 §5 : anti-forgery).
inline SwByteArray removeHeaderFields(const SwByteArray& rawMessage, const SwString& name) {
    const std::string raw = rawMessage.toStdString();
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return rawMessage;  // pas de séparation en-têtes/corps : on ne touche à rien
    }
    const std::string headerBlock = raw.substr(0, headerEnd);
    const std::string rest = raw.substr(headerEnd);  // "\r\n\r\n" + corps
    const std::string prefix = name.trimmed().toLower().toStdString() + ":";

    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (true) {
        const std::size_t e = headerBlock.find("\r\n", pos);
        if (e == std::string::npos) {
            lines.push_back(headerBlock.substr(pos));
            break;
        }
        lines.push_back(headerBlock.substr(pos, e - pos));
        pos = e + 2;
    }

    std::string out;
    bool dropping = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const bool continuation = !line.empty() && (line[0] == ' ' || line[0] == '\t');
        if (!continuation) {
            std::string head = line.substr(0, std::min(line.size(), prefix.size()));
            for (std::size_t k = 0; k < head.size(); ++k) {
                head[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(head[k])));
            }
            dropping = (head == prefix);
        }
        if (!dropping) {
            if (!out.empty()) {
                out += "\r\n";
            }
            out += line;
        }
    }
    return SwByteArray(out + rest);
}

inline SwList<SwString> headerOccurrences(const SwByteArray& rawMessage, const SwString& key) {
    SwList<SwString> values;
    const std::string raw = rawMessage.toStdString();
    const std::string prefix = key.toLower().toStdString() + ":";
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    const std::size_t textEnd = (headerEnd == std::string::npos) ? raw.size() : headerEnd;

    std::string currentValue;
    bool collecting = false;
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
        if (collecting && (line[0] == ' ' || line[0] == '\t')) {
            currentValue += " " + SwString(line).trimmed().toStdString();
            continue;
        }
        if (collecting) {
            values.append(SwString(currentValue).trimmed());
            collecting = false;
            currentValue.clear();
        }
        if (line.size() >= prefix.size() &&
            SwString(line.substr(0, prefix.size())).toLower().toStdString() == prefix) {
            currentValue = line.substr(prefix.size());
            collecting = true;
        }
    }
    if (collecting) {
        values.append(SwString(currentValue).trimmed());
    }
    return values;
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
