#pragma once

#include "SwMailCommon.h"

#include <openssl/rand.h>

#include <cctype>
#include <string>
#include <vector>

// Sender Rewriting Scheme (SRS0) : lors d'un forwarding d'alias vers une adresse
// externe, l'enveloppe MAIL FROM est réécrite vers le domaine forwardeur pour que
// SPF passe chez le destinataire final. L'adresse reste décodable (HMAC + timestamp)
// afin de re-router les bounces vers l'expéditeur d'origine.
// Format : srs0=<hash>=<tt>=<domaine-origine>=<local-origine>@<domaine-forwardeur>
class SwMailSrs {
public:
    static constexpr int kMaxAgeDays = 21;

    static SwString generateSecret() {
        unsigned char bytes[32];
        if (RAND_bytes(bytes, static_cast<int>(sizeof(bytes))) != 1) {
            return SwString(SwCrypto::hashSHA256(swMailDetail::generateId("srs-secret").toStdString()));
        }
        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.reserve(sizeof(bytes) * 2);
        for (std::size_t i = 0; i < sizeof(bytes); ++i) {
            out.push_back(kHex[(bytes[i] >> 4) & 0x0f]);
            out.push_back(kHex[bytes[i] & 0x0f]);
        }
        return SwString(out);
    }

    static bool isSrsAddress(const SwString& address) {
        return swMailDetail::canonicalAddress(address).startsWith("srs0=");
    }

    static SwString encode(const SwString& secret,
                           const SwString& originalSender,
                           const SwString& forwardDomain) {
        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(originalSender, localPart, domain)) {
            return SwString();
        }
        const SwString normalizedForwardDomain = swMailDetail::normalizeDomain(forwardDomain);
        if (secret.isEmpty() || normalizedForwardDomain.isEmpty()) {
            return SwString();
        }
        const SwString timestamp = encodeTimestamp_(currentDayIndex_());
        const SwString hash = computeHash_(secret, timestamp, domain, localPart);
        return "srs0=" + hash + "=" + timestamp + "=" + domain + "=" + localPart +
               "@" + normalizedForwardDomain;
    }

    static bool decode(const SwString& secret,
                       const SwString& srsAddress,
                       SwString* outOriginalSender,
                       SwString* outError = nullptr) {
        if (outOriginalSender) {
            outOriginalSender->clear();
        }
        if (outError) {
            outError->clear();
        }

        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(srsAddress, localPart, domain)) {
            if (outError) {
                *outError = "Invalid SRS address";
            }
            return false;
        }
        if (!localPart.startsWith("srs0=")) {
            if (outError) {
                *outError = "Not an SRS0 address";
            }
            return false;
        }

        // srs0=<hash>=<tt>=<domaine>=<local> — le local d'origine peut contenir '='
        const SwString payload = localPart.mid(5);
        const int firstSep = payload.indexOf("=");
        const int secondSep = firstSep < 0 ? -1 : payload.indexOf("=", firstSep + 1);
        const int thirdSep = secondSep < 0 ? -1 : payload.indexOf("=", secondSep + 1);
        if (firstSep <= 0 || secondSep <= firstSep + 1 || thirdSep <= secondSep + 1 ||
            thirdSep >= static_cast<int>(payload.size()) - 1) {
            if (outError) {
                *outError = "Malformed SRS0 address";
            }
            return false;
        }
        const SwString hash = payload.left(firstSep);
        const SwString timestamp = payload.mid(firstSep + 1, secondSep - firstSep - 1);
        const SwString originalDomain = payload.mid(secondSep + 1, thirdSep - secondSep - 1);
        const SwString originalLocal = payload.mid(thirdSep + 1);

        const int encodedDay = decodeTimestamp_(timestamp);
        if (encodedDay < 0) {
            if (outError) {
                *outError = "Invalid SRS timestamp";
            }
            return false;
        }
        const int today = static_cast<int>(currentDayIndex_() % 1024);
        int age = today - encodedDay;
        if (age < 0) {
            age += 1024;
        }
        if (age > kMaxAgeDays) {
            if (outError) {
                *outError = "SRS address expired";
            }
            return false;
        }

        const SwString expected = computeHash_(secret, timestamp, originalDomain, originalLocal);
        if (expected.toLower() != hash.toLower()) {
            if (outError) {
                *outError = "SRS hash mismatch";
            }
            return false;
        }

        if (outOriginalSender) {
            *outOriginalSender = originalLocal + "@" + originalDomain;
        }
        return true;
    }

private:
    static const char* base32Chars_() {
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    }

    static long long currentDayIndex_() {
        return swMailDetail::currentEpochMs() / 86400000LL;
    }

    static SwString encodeTimestamp_(long long dayIndex) {
        const char* chars = base32Chars_();
        const int value = static_cast<int>(dayIndex % 1024);
        std::string out;
        out.push_back(chars[(value >> 5) & 31]);
        out.push_back(chars[value & 31]);
        return SwString(out);
    }

    static int decodeTimestamp_(const SwString& timestamp) {
        if (timestamp.size() != 2) {
            return -1;
        }
        const int high = base32Index_(timestamp[0]);
        const int low = base32Index_(timestamp[1]);
        if (high < 0 || low < 0) {
            return -1;
        }
        return (high << 5) | low;
    }

    static int base32Index_(char c) {
        const char* chars = base32Chars_();
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (int i = 0; i < 32; ++i) {
            if (chars[i] == upper) {
                return i;
            }
        }
        return -1;
    }

    static SwString computeHash_(const SwString& secret,
                                 const SwString& timestamp,
                                 const SwString& domain,
                                 const SwString& localPart) {
        const std::string input = (timestamp.toLower() + "=" + domain.toLower() + "=" + localPart.toLower()).toStdString();
        const std::vector<unsigned char> mac = SwCrypto::generateKeyedHashSHA256(input, secret.toStdString());
        if (mac.size() < 3) {
            return SwString();
        }
        const unsigned int packed = (static_cast<unsigned int>(mac[0]) << 16) |
                                    (static_cast<unsigned int>(mac[1]) << 8) |
                                    static_cast<unsigned int>(mac[2]);
        const char* chars = base32Chars_();
        std::string out;
        out.push_back(chars[(packed >> 19) & 31]);
        out.push_back(chars[(packed >> 14) & 31]);
        out.push_back(chars[(packed >> 9) & 31]);
        out.push_back(chars[(packed >> 4) & 31]);
        return SwString(out);
    }
};
