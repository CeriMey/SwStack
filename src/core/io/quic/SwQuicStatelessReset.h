/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 *
 * Licensed under the Apache License, Version 2.0.
 ***************************************************************************************************/

#pragma once

// Stateless Reset (RFC 9000 section 10.3).
//
// Quand un serveur recoit un paquet court dont le CID ne correspond plus a
// aucun etat (idle timeout purge, redemarrage), le jeter en silence laisse le
// pair retransmettre dans le vide jusqu'a son propre timeout. Le Stateless
// Reset est la reponse sans etat : un datagramme d'apparence aleatoire dont
// les 16 derniers octets sont un jeton derive du CID via une cle statique.
// Le pair, qui a appris ce jeton dans les transport parameters, reconnait la
// fin de connexion immediatement et re-numerote au lieu de pendre.

#include "SwByteArray.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwTls13KeySchedule.h"

#include <cstddef>
#include <cstdint>

class SwQuicStatelessReset {
public:
    // RFC 9000 10.3 : jeton de 16 octets en fin de datagramme.
    static constexpr std::size_t kTokenLength = 16;
    // Plus petit datagramme reconnaissable comme reset : 1 octet d'en-tete,
    // >= 4 octets imprevisibles, 16 octets de jeton.
    static constexpr std::size_t kMinResetLength = 21;
    // Un reset DOIT etre plus court que le paquet declencheur (casse les
    // boucles reset<->reset) : en dessous de 22 octets on ne repond pas.
    static constexpr std::size_t kMinTriggerLength = kMinResetLength + 1;
    // Borne haute pour ne pas refleter un gros datagramme (l'amplification
    // reste < 1x par construction, la borne borne aussi le cout CPU/reseau).
    static constexpr std::size_t kMaxResetLength = 60;

    // Jeton statique deterministe : HKDF-Extract(cle, CID) = HMAC-SHA256,
    // tronque a 16 octets. La cle (>= 16 octets aleatoires) appartient a
    // l'endpoint ; le meme couple (cle, CID) redonne toujours le meme jeton,
    // ce qui permet de repondre APRES avoir perdu l'etat de la connexion.
    static SwByteArray deriveToken(const SwByteArray& staticKey,
                                   const SwByteArray& connectionIdBytes) {
        SwByteArray digest;
        if (staticKey.isEmpty() || connectionIdBytes.isEmpty() ||
            !SwTls13KeySchedule::hkdfExtract(staticKey, connectionIdBytes, digest) ||
            digest.size() < kTokenLength) {
            return SwByteArray();
        }
        return digest.left(static_cast<int>(kTokenLength));
    }

    // Construit le datagramme de reset pour un declencheur de triggerLength
    // octets. Refuse (false) si le declencheur est trop court pour repondre
    // plus court que lui, ou si le jeton n'a pas la bonne taille.
    static bool buildPacket(const SwByteArray& token,
                            std::size_t triggerLength,
                            SwByteArray& outPacket) {
        outPacket = SwByteArray();
        if (token.size() != static_cast<int>(kTokenLength) ||
            triggerLength < kMinTriggerLength) {
            return false;
        }
        std::size_t length = triggerLength - 1;
        if (length > kMaxResetLength) {
            // Taille legerement variable pour ne pas signer les resets.
            const SwByteArray jitter = SwQuicRandom::bytes(1);
            const std::size_t spread = jitter.isEmpty()
                ? 0
                : static_cast<std::uint8_t>(jitter.constData()[0]) %
                      (kMaxResetLength - kMinResetLength + 1);
            length = kMinResetLength + spread;
        }
        if (length < kMinResetLength) {
            length = kMinResetLength;
        }

        SwByteArray packet = SwQuicRandom::bytes(length);
        if (packet.size() != static_cast<int>(length)) {
            return false;
        }
        // Forme d'un short header 1-RTT : bit fixe 0b01, le reste aleatoire.
        packet[0] = static_cast<char>(
            0x40U | (static_cast<std::uint8_t>(packet.constData()[0]) & 0x3fU));
        for (std::size_t index = 0; index < kTokenLength; ++index) {
            packet[static_cast<int>(length - kTokenLength + index)] =
                token.constData()[index];
        }
        outPacket = packet;
        return true;
    }

    // Reconnaissance cote recepteur : les 16 derniers octets du datagramme
    // valent-ils ce jeton ? Comparaison a temps constant — le jeton est un
    // secret partage, une comparaison paresseuse serait un oracle.
    static bool matchesToken(const SwByteArray& datagram, const SwByteArray& token) {
        if (token.size() != static_cast<int>(kTokenLength) ||
            datagram.size() < static_cast<int>(kMinResetLength)) {
            return false;
        }
        const char* tail =
            datagram.constData() + datagram.size() - static_cast<int>(kTokenLength);
        unsigned char difference = 0;
        for (std::size_t index = 0; index < kTokenLength; ++index) {
            difference |= static_cast<unsigned char>(
                tail[index] ^ token.constData()[index]);
        }
        return difference == 0;
    }
};
