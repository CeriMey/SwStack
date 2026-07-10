#ifndef SWQUICRETRY_H
#define SWQUICRETRY_H

#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "SwCrypto.h" // generateKeyedHashSHA256 (HMAC-SHA256 portable Win/Linux)
#include "quic/SwQuicPacketProtector.h"

#include <cstdint>
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
        const int tagLen = static_cast<int>(SwQuicPacketProtector::kTagLength);
        if (retryPacket.size() < tagLen + 7) { // 1 (1er octet) + 4 (version) + 1 + 1 (2 CID len) minimum
            setError_(error, "Retry packet too short");
            return false;
        }
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

    // Extrait le token d'un paquet Retry (les octets entre le SCID serveur et le tag d'intégrité).
    static bool extractToken(const SwByteArray& retryPacket, SwByteArray& outToken, SwString* error = nullptr) {
        const int tagLen = static_cast<int>(SwQuicPacketProtector::kTagLength);
        if (retryPacket.size() < tagLen + 7) { setError_(error, "Retry packet too short"); return false; }
        int off = 1 + 4; // 1er octet + version
        if (off >= retryPacket.size()) { setError_(error, "Retry truncated"); return false; }
        const int dcidLen = static_cast<unsigned char>(retryPacket.constData()[off]); off += 1 + dcidLen;
        if (off >= retryPacket.size()) { setError_(error, "Retry truncated (dcid)"); return false; }
        const int scidLen = static_cast<unsigned char>(retryPacket.constData()[off]); off += 1 + scidLen;
        const int tokenLen = static_cast<int>(retryPacket.size()) - tagLen - off;
        if (tokenLen < 0) { setError_(error, "Retry truncated (token)"); return false; }
        outToken = retryPacket.mid(off, tokenLen);
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
};

#endif // SWQUICRETRY_H
