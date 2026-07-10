#ifndef SWQUICRETRY_H
#define SWQUICRETRY_H

#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "SwCrypto.h" // generateKeyedHashSHA256 (HMAC-SHA256 portable Win/Linux)
#include "quic/SwQuicPacketProtector.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// SwQuicRetry — construction/vérification GÉNÉRIQUE du paquet Retry QUIC v1 (RFC 9000 §17.2.5) avec
// Retry Integrity Tag (RFC 9001 §5.8), et jeton d'adresse SANS ÉTAT (HMAC-SHA256) pour la validation
// d'adresse serveur (RFC 9000 §8.1). Aucune policy applicative ici : le jeton lie l'adresse source,
// le connection id d'origine (ODCID) et un horodatage ; l'appelant choisit la clé serveur et la fenêtre.
class SwQuicRetry {
public:
    static const int kTokenTsBytes  = 8;
    static const int kTokenMacBytes = 16;
    static const int kAddressTokenVersion = 1;

    struct ParsedRetry {
        SwByteArray destinationConnectionId;
        SwByteArray sourceConnectionId;
        SwByteArray token;
    };

    struct AddressToken {
        SwByteArray originalDestinationConnectionId;
        SwByteArray retrySourceConnectionId;
        std::uint64_t issuedAtSeconds = 0;
    };

    // Construit un paquet Retry v1. DCID du Retry = SCID du client (pour qu'il le reconnaisse),
    // SCID du Retry = nouveau SCID serveur (deviendra le retry_source_connection_id). Le tag d'intégrité
    // est calculé sur le Retry Pseudo-Packet = ODCID_len ‖ ODCID ‖ (Retry sans tag).
    static bool buildRetryPacket(const SwByteArray& originalDcid,
                                 const SwByteArray& clientScid,
                                 const SwByteArray& serverScid,
                                 const SwByteArray& retryToken,
                                 SwByteArray& outPacket,
                                 SwString* error = nullptr) {
        if (clientScid.size() > 255 || serverScid.size() > 255 || originalDcid.size() > 255) {
            setError_(error, "Retry connection id too long");
            return false;
        }
        SwByteArray body;
        body.append(static_cast<char>(0xf0));            // long header + fixed bit + type Retry (0b11), unused=0
        appendU32_(body, 0x00000001u);                   // version QUIC v1
        body.append(static_cast<char>(static_cast<unsigned char>(clientScid.size())));
        body.append(clientScid);
        body.append(static_cast<char>(static_cast<unsigned char>(serverScid.size())));
        body.append(serverScid);
        body.append(retryToken);

        SwByteArray tag;
        if (!integrityTag_(originalDcid, body, tag, error)) {
            return false;
        }
        outPacket = body;
        outPacket.append(tag);
        return true;
    }

    // Vérifie l'integrity tag d'un paquet Retry reçu, connaissant l'ODCID choisi par le client.
    static bool verifyRetryPacket(const SwByteArray& retryPacket,
                                  const SwByteArray& originalDcid,
                                  SwString* error = nullptr) {
        ParsedRetry parsed;
        if (!parseRetryPacket(retryPacket, parsed, error)) return false;
        const int tagLen = static_cast<int>(SwQuicPacketProtector::kTagLength);
        const int bodyLen = static_cast<int>(retryPacket.size()) - tagLen;
        const SwByteArray body = retryPacket.mid(0, bodyLen);
        const SwByteArray tag  = retryPacket.mid(bodyLen, tagLen);
        SwByteArray expected;
        if (!integrityTag_(originalDcid, body, expected, error)) {
            return false;
        }
        if (!constTimeEq_(expected, tag)) {
            setError_(error, "Retry integrity tag mismatch");
            return false;
        }
        return true;
    }

    // Parses the clear-text Retry header and opaque token. Integrity is not
    // authenticated by this function; callers accepting a Retry MUST also call
    // verifyRetryPacket() with the first Initial's ODCID.
    static bool parseRetryPacket(const SwByteArray& retryPacket,
                                 ParsedRetry& out,
                                 SwString* error = nullptr) {
        out = ParsedRetry();
        const int tagLen = static_cast<int>(SwQuicPacketProtector::kTagLength);
        if (retryPacket.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
            retryPacket.size() < static_cast<std::size_t>(tagLen + 7) ||
            !retryPacket.constData()) {
            setError_(error, "Retry packet too short");
            return false;
        }
        const std::uint8_t first =
            static_cast<std::uint8_t>(retryPacket.constData()[0]);
        if ((first & 0xf0U) != 0xf0U || readU32_(retryPacket, 1) != 1U) {
            setError_(error, "Retry packet has an invalid QUIC v1 header");
            return false;
        }
        int offset = 5;
        const int dcidLength =
            static_cast<std::uint8_t>(retryPacket.constData()[offset++]);
        if (dcidLength > 20 || offset + dcidLength >= retryPacket.size()) {
            setError_(error, "Retry destination connection id is truncated");
            return false;
        }
        out.destinationConnectionId = retryPacket.mid(offset, dcidLength);
        offset += dcidLength;
        const int scidLength =
            static_cast<std::uint8_t>(retryPacket.constData()[offset++]);
        if (scidLength == 0 || scidLength > 20 ||
            offset + scidLength + tagLen > retryPacket.size()) {
            setError_(error, "Retry source connection id is invalid");
            return false;
        }
        out.sourceConnectionId = retryPacket.mid(offset, scidLength);
        offset += scidLength;
        const int tokenLength =
            static_cast<int>(retryPacket.size()) - tagLen - offset;
        if (tokenLength < 0) {
            setError_(error, "Retry token is truncated");
            return false;
        }
        out.token = retryPacket.mid(offset, tokenLength);
        clearError_(error);
        return true;
    }

    // Extrait le token d'un paquet Retry (les octets entre le SCID serveur et le tag d'intégrité).
    static bool extractToken(const SwByteArray& retryPacket, SwByteArray& outToken, SwString* error = nullptr) {
        ParsedRetry parsed;
        if (!parseRetryPacket(retryPacket, parsed, error)) return false;
        outToken = parsed.token;
        return true;
    }

    // Self-contained stateless address token:
    //   version(1) || issuedAt(8) || odcidLen(1) || ODCID ||
    //   retryScidLen(1) || retrySCID || HMAC-SHA256(key, domain ||
    //   len(address) || address || authenticated-body)[0..15].
    // The Retry SCID binding prevents a captured token from authorising state
    // allocation under an attacker-selected replacement DCID.
    static SwByteArray mintAddressToken(const SwByteArray& serverKey,
                                        const SwByteArray& clientAddress,
                                        const SwByteArray& originalDcid,
                                        const SwByteArray& retrySourceDcid,
                                        std::uint64_t nowSeconds) {
        if (serverKey.isEmpty() || originalDcid.isEmpty() ||
            originalDcid.size() > 20 || retrySourceDcid.isEmpty() ||
            retrySourceDcid.size() > 20 || clientAddress.size() > 65535) {
            return SwByteArray();
        }
        SwByteArray body;
        body.append(static_cast<char>(kAddressTokenVersion));
        appendU64_(body, nowSeconds);
        body.append(static_cast<char>(
            static_cast<std::uint8_t>(originalDcid.size())));
        body.append(originalDcid);
        body.append(static_cast<char>(
            static_cast<std::uint8_t>(retrySourceDcid.size())));
        body.append(retrySourceDcid);
        const SwByteArray mac = addressTokenMac_(serverKey, clientAddress, body);
        if (mac.size() < kTokenMacBytes) return SwByteArray();
        body.append(mac.mid(0, kTokenMacBytes));
        return body;
    }

    static bool validateAddressToken(const SwByteArray& serverKey,
                                     const SwByteArray& clientAddress,
                                     const SwByteArray& expectedRetrySourceDcid,
                                     const SwByteArray& token,
                                     std::uint64_t nowSeconds,
                                     std::uint64_t maxAgeSeconds,
                                     AddressToken& out) {
        out = AddressToken();
        const int minimum = 1 + kTokenTsBytes + 1 + 1 + 1 + 1 + kTokenMacBytes;
        if (serverKey.isEmpty() ||
            token.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
            token.size() < static_cast<std::size_t>(minimum) ||
            clientAddress.size() > 65535 || !token.constData()) return false;
        const int bodyLength = static_cast<int>(token.size()) - kTokenMacBytes;
        const SwByteArray body = token.mid(0, bodyLength);
        const SwByteArray suppliedMac = token.mid(bodyLength, kTokenMacBytes);
        const SwByteArray expectedMac =
            addressTokenMac_(serverKey, clientAddress, body).mid(0, kTokenMacBytes);
        // Authenticate the complete variable-length body before exposing its
        // ODCID or timestamp. Equality is constant-time for equal-size tags.
        if (!constTimeEq_(expectedMac, suppliedMac)) return false;

        int offset = 0;
        if (static_cast<std::uint8_t>(body.constData()[offset++]) !=
            kAddressTokenVersion) return false;
        if (offset + kTokenTsBytes > body.size()) return false;
        const SwByteArray timestamp = body.mid(offset, kTokenTsBytes);
        offset += kTokenTsBytes;
        const std::uint64_t issuedAt = readU64_(timestamp);
        if (nowSeconds < issuedAt || nowSeconds - issuedAt > maxAgeSeconds) return false;

        if (offset >= body.size()) return false;
        const int odcidLength =
            static_cast<std::uint8_t>(body.constData()[offset++]);
        if (odcidLength == 0 || odcidLength > 20 ||
            offset + odcidLength >= body.size()) return false;
        const SwByteArray odcid = body.mid(offset, odcidLength);
        offset += odcidLength;
        const int retryLength =
            static_cast<std::uint8_t>(body.constData()[offset++]);
        if (retryLength == 0 || retryLength > 20 ||
            offset + retryLength != body.size()) return false;
        const SwByteArray retrySource = body.mid(offset, retryLength);
        if (!constTimeEq_(retrySource, expectedRetrySourceDcid)) return false;

        out.originalDestinationConnectionId = odcid;
        out.retrySourceConnectionId = retrySource;
        out.issuedAtSeconds = issuedAt;
        return true;
    }

    // --- Jeton d'adresse sans état ---
    // token = ts(8, big-endian) ‖ HMAC-SHA256(serverKey, clientAddr ‖ ODCID ‖ ts)[0..15].
    static SwByteArray mintToken(const SwByteArray& serverKey,
                                 const SwByteArray& clientAddr,
                                 const SwByteArray& originalDcid,
                                 uint64_t nowS) {
        SwByteArray ts; appendU64_(ts, nowS);
        SwByteArray token = ts;
        token.append(tokenMac_(serverKey, clientAddr, originalDcid, ts).mid(0, kTokenMacBytes));
        return token;
    }

    // Valide un token (HMAC en temps constant + fenêtre d'âge [0, maxAgeS]).
    static bool validateToken(const SwByteArray& serverKey,
                              const SwByteArray& clientAddr,
                              const SwByteArray& originalDcid,
                              const SwByteArray& token,
                              uint64_t nowS,
                              uint64_t maxAgeS) {
        if (token.size() != kTokenTsBytes + kTokenMacBytes) return false;
        const SwByteArray ts  = token.mid(0, kTokenTsBytes);
        const SwByteArray mac = token.mid(kTokenTsBytes, kTokenMacBytes);
        const SwByteArray expected = tokenMac_(serverKey, clientAddr, originalDcid, ts).mid(0, kTokenMacBytes);
        if (!constTimeEq_(expected, mac)) return false;
        const uint64_t tsVal = readU64_(ts);
        if (nowS < tsVal) return false;             // horodatage futur -> rejet
        if (nowS - tsVal > maxAgeS) return false;   // expiré
        return true;
    }

private:
    static bool integrityTag_(const SwByteArray& originalDcid, const SwByteArray& retryBodyNoTag,
                              SwByteArray& outTag, SwString* error) {
        SwByteArray pseudo;
        pseudo.append(static_cast<char>(static_cast<unsigned char>(originalDcid.size())));
        pseudo.append(originalDcid);
        pseudo.append(retryBodyNoTag);
        return SwQuicPacketProtector::computeRetryIntegrityTag(pseudo, outTag, error);
    }

    static SwByteArray tokenMac_(const SwByteArray& key, const SwByteArray& addr,
                                 const SwByteArray& odcid, const SwByteArray& ts) {
        SwByteArray data = addr; data.append(odcid); data.append(ts);
        const SwString dataStr(data.constData(), static_cast<size_t>(data.size()));
        const SwString keyStr(key.constData(), static_cast<size_t>(key.size()));
        const SwVector<unsigned char> d = SwCrypto::generateKeyedHashSHA256(dataStr, keyStr);
        return SwByteArray(reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()));
    }

    static SwByteArray addressTokenMac_(const SwByteArray& key,
                                        const SwByteArray& address,
                                        const SwByteArray& body) {
        static const char domain[] = "SwQuic Retry address token v1";
        SwByteArray data(domain, sizeof(domain) - 1);
        data.append(static_cast<char>((address.size() >> 8) & 0xff));
        data.append(static_cast<char>(address.size() & 0xff));
        data.append(address);
        data.append(body);
        const SwString dataString(data.constData(), static_cast<size_t>(data.size()));
        const SwString keyString(key.constData(), static_cast<size_t>(key.size()));
        const SwVector<unsigned char> digest =
            SwCrypto::generateKeyedHashSHA256(dataString, keyString);
        if (digest.empty()) return SwByteArray();
        return SwByteArray(reinterpret_cast<const char*>(digest.data()),
                           static_cast<int>(digest.size()));
    }

    static void appendU32_(SwByteArray& out, uint32_t v) {
        out.append(static_cast<char>((v >> 24) & 0xFF));
        out.append(static_cast<char>((v >> 16) & 0xFF));
        out.append(static_cast<char>((v >> 8) & 0xFF));
        out.append(static_cast<char>(v & 0xFF));
    }
    static void appendU64_(SwByteArray& out, uint64_t v) {
        for (int i = 7; i >= 0; --i) out.append(static_cast<char>((v >> (i * 8)) & 0xFF));
    }
    static uint64_t readU64_(const SwByteArray& in) {
        uint64_t v = 0;
        for (int i = 0; i < in.size() && i < 8; ++i)
            v = (v << 8) | static_cast<unsigned char>(in.constData()[i]);
        return v;
    }
    static std::uint32_t readU32_(const SwByteArray& in, int offset) {
        if (offset < 0 || offset + 4 > in.size()) return 0;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 8) |
                static_cast<std::uint8_t>(in.constData()[offset + i]);
        }
        return value;
    }
    static bool constTimeEq_(const SwByteArray& a, const SwByteArray& b) {
        if (a.size() != b.size()) return false;
        unsigned char acc = 0;
        for (int i = 0; i < a.size(); ++i)
            acc |= static_cast<unsigned char>(a.constData()[i]) ^ static_cast<unsigned char>(b.constData()[i]);
        return acc == 0;
    }
    static void setError_(SwString* error, const char* msg) {
        if (error) *error = SwString(msg);
    }
    static void clearError_(SwString* error) {
        if (error) *error = SwString();
    }
};

#endif // SWQUICRETRY_H
