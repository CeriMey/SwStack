#ifndef SWTLS13KEYSCHEDULE_H
#define SWTLS13KEYSCHEDULE_H

/**
 * @file src/core/io/quic/SwTls13KeySchedule.h
 * @ingroup core_io_quic
 * @brief TLS 1.3 key schedule (RFC 8446 s7.1) for cipher suite TLS_AES_128_GCM_SHA256.
 *
 * This header implements the full TLS 1.3 key-derivation schedule restricted to the SHA-256
 * based cipher suite TLS_AES_128_GCM_SHA256 (32-byte secrets). It reuses the already-validated
 * crypto primitives of the stack:
 *   - HKDF-Expand-Label with an EMPTY context is delegated verbatim to
 *     SwQuicInitialSecrets::hkdfExpandLabel (validated against the RFC 9001 QUIC Initial vectors);
 *   - HKDF-Extract and the Finished MAC are HMAC-SHA256, taken from SwCrypto;
 *   - Transcript-Hash is SHA-256, taken from SwCrypto.
 *
 * The shared SwQuicInitialSecrets::hkdfExpandLabel hardcodes an empty HkdfLabel.context, which is
 * correct for the QUIC key/iv/hp labels and for the TLS "finished" key. Derive-Secret, however,
 * carries the Transcript-Hash as a NON-empty context, so this module builds that single case on top
 * of the same SwCrypto HMAC-SHA256 primitive (no reimplementation of SHA-256 or HMAC).
 *
 * Every fallible function returns bool and reports the reason through a trailing SwString* error.
 */

#include "SwByteArray.h"
#include "SwCrypto.h"
#include "SwString.h"
#include "quic/SwQuicInitialSecrets.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

class SwTls13KeySchedule {
public:
    /**
     * @brief Transcript-Hash(messages) = SHA-256(messages) for TLS_AES_128_GCM_SHA256.
     * @param messages Concatenated handshake messages.
     * @return The 32-byte SHA-256 digest.
     */
    static SwByteArray transcriptHash(const SwByteArray& messages) {
        const std::vector<unsigned char> digest =
            SwCrypto::generateHashSHA256(messages.toStdString());
        return SwByteArray(reinterpret_cast<const char*>(digest.data()), digest.size());
    }

    /**
     * @brief HKDF-Extract(salt, ikm) = HMAC-SHA256(salt, ikm).
     * @param salt HMAC key (the extract salt).
     * @param ikm Input key material (the HMAC message).
     * @param out Receives the 32-byte pseudo-random key.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool hkdfExtract(const SwByteArray& salt,
                            const SwByteArray& ikm,
                            SwByteArray& out,
                            SwString* err = nullptr) {
        try {
            out = hmacSha256_(salt, ikm);
        } catch (const std::exception& ex) {
            setError_(err, ex.what());
            return false;
        }
        clearError_(err);
        return true;
    }

    /**
     * @brief Derive-Secret(secret, label, transcriptHash) =
     *        HKDF-Expand-Label(secret, label, transcriptHash, 32).
     * @param secret The 32-byte input secret.
     * @param label ASCII label WITHOUT the "tls13 " prefix.
     * @param transcriptHash32 The 32-byte Transcript-Hash used as HkdfLabel.context.
     * @param out Receives the 32-byte derived secret.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool deriveSecret(const SwByteArray& secret,
                             const SwString& label,
                             const SwByteArray& transcriptHash32,
                             SwByteArray& out,
                             SwString* err = nullptr) {
        return expandLabelCtx_(secret, label, transcriptHash32, 32, out, err);
    }

    /**
     * @brief Early-Secret for the no-PSK case = HKDF-Extract(salt=0^32, ikm=0^32).
     * @param out Receives the 32-byte Early-Secret.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool earlySecret(SwByteArray& out, SwString* err = nullptr) {
        const SwByteArray zeros(32, '\0');
        return hkdfExtract(zeros, zeros, out, err);
    }

    /**
     * @brief Handshake-Secret = HKDF-Extract(Derive-Secret(early,"derived",Hash("")), ecdhe).
     * @param earlySecretIn The 32-byte Early-Secret.
     * @param ecdhe32 The 32-byte (EC)DHE shared secret.
     * @param out Receives the 32-byte Handshake-Secret.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool handshakeSecret(const SwByteArray& earlySecretIn,
                                const SwByteArray& ecdhe32,
                                SwByteArray& out,
                                SwString* err = nullptr) {
        SwByteArray derived;
        if (!deriveDerived_(earlySecretIn, derived, err)) {
            return false;
        }
        return hkdfExtract(derived, ecdhe32, out, err);
    }

    /**
     * @brief Master-Secret = HKDF-Extract(Derive-Secret(hs,"derived",Hash("")), 0^32).
     * @param handshakeSecretIn The 32-byte Handshake-Secret.
     * @param out Receives the 32-byte Master-Secret.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool masterSecret(const SwByteArray& handshakeSecretIn,
                             SwByteArray& out,
                             SwString* err = nullptr) {
        SwByteArray derived;
        if (!deriveDerived_(handshakeSecretIn, derived, err)) {
            return false;
        }
        const SwByteArray zeros(32, '\0');
        return hkdfExtract(derived, zeros, out, err);
    }

    /**
     * @brief client_handshake_traffic_secret = Derive-Secret(hs,"c hs traffic", ClientHello..ServerHello).
     */
    static bool clientHandshakeTrafficSecret(const SwByteArray& handshakeSecretIn,
                                             const SwByteArray& transcriptCHtoSH32,
                                             SwByteArray& out,
                                             SwString* err = nullptr) {
        return deriveSecret(handshakeSecretIn, SwString("c hs traffic"), transcriptCHtoSH32, out, err);
    }

    /**
     * @brief server_handshake_traffic_secret = Derive-Secret(hs,"s hs traffic", ClientHello..ServerHello).
     */
    static bool serverHandshakeTrafficSecret(const SwByteArray& handshakeSecretIn,
                                             const SwByteArray& transcriptCHtoSH32,
                                             SwByteArray& out,
                                             SwString* err = nullptr) {
        return deriveSecret(handshakeSecretIn, SwString("s hs traffic"), transcriptCHtoSH32, out, err);
    }

    /**
     * @brief client_application_traffic_secret_0 =
     *        Derive-Secret(master,"c ap traffic", ClientHello..server Finished).
     */
    static bool clientApplicationTrafficSecret(const SwByteArray& masterSecretIn,
                                               const SwByteArray& transcriptCHtoServerFinished32,
                                               SwByteArray& out,
                                               SwString* err = nullptr) {
        return deriveSecret(masterSecretIn, SwString("c ap traffic"), transcriptCHtoServerFinished32, out, err);
    }

    /**
     * @brief server_application_traffic_secret_0 =
     *        Derive-Secret(master,"s ap traffic", ClientHello..server Finished).
     */
    static bool serverApplicationTrafficSecret(const SwByteArray& masterSecretIn,
                                               const SwByteArray& transcriptCHtoServerFinished32,
                                               SwByteArray& out,
                                               SwString* err = nullptr) {
        return deriveSecret(masterSecretIn, SwString("s ap traffic"), transcriptCHtoServerFinished32, out, err);
    }

    /**
     * @brief exporter_master_secret = Derive-Secret(Master-Secret, "exp master",
     *        Transcript-Hash(ClientHello..server Finished)). RFC 8446 section 7.5.
     * @param masterSecretIn The 32-byte Master-Secret.
     * @param transcriptCHtoServerFinished32 Transcript-Hash up to and including server Finished.
     * @param out Receives the 32-byte exporter_master_secret.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool exporterMasterSecret(const SwByteArray& masterSecretIn,
                                     const SwByteArray& transcriptCHtoServerFinished32,
                                     SwByteArray& out,
                                     SwString* err = nullptr) {
        return deriveSecret(masterSecretIn, SwString("exp master"), transcriptCHtoServerFinished32, out, err);
    }

    /**
     * @brief TLS-Exporter(label, context, length) per RFC 8446 section 7.5:
     *          secret = Derive-Secret(exporter_master_secret, label, "")
     *          out    = HKDF-Expand-Label(secret, "exporter", Transcript-Hash(context), length)
     *
     * Note: the second step uses a NON-empty context (Transcript-Hash(context)), so it goes
     * through expandLabelCtx_ rather than the empty-context SwQuicInitialSecrets::hkdfExpandLabel.
     *
     * @param exporterMasterSecretIn 32-byte exporter_master_secret (see exporterMasterSecret()).
     * @param label ASCII exporter label WITHOUT the "tls13 " prefix (application-defined,
     *              e.g. an "EXPORTER-<purpose>" string per RFC 8446 section 7.5).
     * @param context Caller-supplied context value (may be empty); hashed with SHA-256.
     * @param length Desired number of exported bytes.
     * @param out Receives the exported keying material.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool exportKeyingMaterial(const SwByteArray& exporterMasterSecretIn,
                                     const SwString& label,
                                     const SwByteArray& context,
                                     std::size_t length,
                                     SwByteArray& out,
                                     SwString* err = nullptr) {
        const SwByteArray emptyHash = transcriptHash(SwByteArray(size_t(0), '\0'));
        SwByteArray secret;
        if (!deriveSecret(exporterMasterSecretIn, label, emptyHash, secret, err)) {
            return false;
        }
        const SwByteArray contextHash = transcriptHash(context);
        return expandLabelCtx_(secret, SwString("exporter"), contextHash, length, out, err);
    }

    /**
     * @brief finished_key = HKDF-Expand-Label(traffic_secret, "finished", "", 32).
     *
     * The context is empty here, so this delegates verbatim to the validated shared helper.
     */
    static bool finishedKey(const SwByteArray& trafficSecret,
                            SwByteArray& out,
                            SwString* err = nullptr) {
        return SwQuicInitialSecrets::hkdfExpandLabel(trafficSecret, SwString("finished"), 32, out, err);
    }

    /**
     * @brief verify_data = HMAC-SHA256(finished_key, Transcript-Hash(...)).
     * @param finishedKeyIn The 32-byte finished key.
     * @param transcriptHash32 The Transcript-Hash over the handshake up to (not incl.) this Finished.
     * @param out Receives the 32-byte verify_data.
     * @param err Optional error out-param.
     * @return true on success; false otherwise.
     */
    static bool verifyData(const SwByteArray& finishedKeyIn,
                           const SwByteArray& transcriptHash32,
                           SwByteArray& out,
                           SwString* err = nullptr) {
        try {
            out = hmacSha256_(finishedKeyIn, transcriptHash32);
        } catch (const std::exception& ex) {
            setError_(err, ex.what());
            return false;
        }
        clearError_(err);
        return true;
    }

    // ---- resumption / 0-RTT (RFC 8446 s4.6.1, s7.1; RFC 9001 s4.6) --------

    /**
     * @brief Early-Secret from a resumption PSK = HKDF-Extract(0^32, PSK).
     */
    static bool earlySecretWithPsk(const SwByteArray& psk,
                                   SwByteArray& out,
                                   SwString* err = nullptr) {
        const SwByteArray zeros(32, '\0');
        return hkdfExtract(zeros, psk, out, err);
    }

    /**
     * @brief binder_key = Derive-Secret(Early-Secret, "res binder", "").
     * The PSK binder is HMAC(HKDF-Expand-Label(binder_key,"finished","",32),
     * Transcript-Hash(Truncate(ClientHello))): use finishedKey() + verifyData().
     */
    static bool resumptionBinderKey(const SwByteArray& earlySecretIn,
                                    SwByteArray& out,
                                    SwString* err = nullptr) {
        const SwByteArray emptyHash = transcriptHash(SwByteArray(size_t(0), '\0'));
        return deriveSecret(earlySecretIn, SwString("res binder"), emptyHash, out, err);
    }

    /**
     * @brief client_early_traffic_secret =
     *        Derive-Secret(Early-Secret, "c e traffic", ClientHello).
     * @param transcriptClientHello32 Transcript-Hash of the full ClientHello.
     */
    static bool clientEarlyTrafficSecret(const SwByteArray& earlySecretIn,
                                         const SwByteArray& transcriptClientHello32,
                                         SwByteArray& out,
                                         SwString* err = nullptr) {
        return deriveSecret(earlySecretIn, SwString("c e traffic"),
                            transcriptClientHello32, out, err);
    }

    /**
     * @brief resumption_master_secret =
     *        Derive-Secret(Master-Secret, "res master", ClientHello..client Finished).
     */
    static bool resumptionMasterSecret(const SwByteArray& masterSecretIn,
                                       const SwByteArray& transcriptClientFinished32,
                                       SwByteArray& out,
                                       SwString* err = nullptr) {
        return deriveSecret(masterSecretIn, SwString("res master"),
                            transcriptClientFinished32, out, err);
    }

    /**
     * @brief PSK for a ticket =
     *        HKDF-Expand-Label(resumption_master_secret, "resumption", ticket_nonce, 32).
     * Unlike Derive-Secret the context is the ticket_nonce, not a transcript hash.
     */
    static bool resumptionPsk(const SwByteArray& resumptionMasterSecretIn,
                              const SwByteArray& ticketNonce,
                              SwByteArray& out,
                              SwString* err = nullptr) {
        return expandLabelCtx_(resumptionMasterSecretIn, SwString("resumption"),
                               ticketNonce, 32, out, err);
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void clearError_(SwString* error) {
        if (error) {
            *error = SwString();
        }
    }

    /// HMAC-SHA256(key, data). SwCrypto takes (data, key) — mind the order.
    static SwByteArray hmacSha256_(const SwByteArray& key, const SwByteArray& data) {
        const std::vector<unsigned char> digest =
            SwCrypto::generateKeyedHashSHA256(data.toStdString(), key.toStdString());
        return SwByteArray(reinterpret_cast<const char*>(digest.data()), digest.size());
    }

    static void appendU16_(SwByteArray& out, std::uint16_t value) {
        out.append(static_cast<char>((value >> 8) & 0xffU));
        out.append(static_cast<char>(value & 0xffU));
    }

    /// Derive-Secret(secret, "derived", Hash("")) — used to bridge Extract stages.
    static bool deriveDerived_(const SwByteArray& secret, SwByteArray& out, SwString* err) {
        const SwByteArray emptyHash = transcriptHash(SwByteArray(size_t(0), '\0'));
        return deriveSecret(secret, SwString("derived"), emptyHash, out, err);
    }

    /**
     * @brief HKDF-Expand-Label carrying a (possibly non-empty) context.
     *
     * Builds the RFC 8446 HkdfLabel:
     *   struct { uint16 length; opaque label<7..255>; opaque context<0..255>; }
     * with label = "tls13 " + @p label, then runs HKDF-Expand (HMAC-SHA256) to @p outputLength.
     * The HMAC primitive is reused from SwCrypto; SHA-256/HMAC are not reimplemented.
     */
    static bool expandLabelCtx_(const SwByteArray& secret,
                                const SwString& label,
                                const SwByteArray& context,
                                std::size_t outputLength,
                                SwByteArray& out,
                                SwString* err) {
        if (outputLength > static_cast<std::size_t>(255) * 32) {
            setError_(err, "HKDF output length is too large");
            return false;
        }

        const SwString fullLabel = SwString("tls13 ") + label;
        if (fullLabel.size() > 255) {
            setError_(err, "HKDF label is too long");
            return false;
        }
        if (context.size() > 255) {
            setError_(err, "HKDF context is too long");
            return false;
        }

        try {
            SwByteArray info;
            appendU16_(info, static_cast<std::uint16_t>(outputLength));
            info.append(static_cast<char>(fullLabel.size()));
            info.append(fullLabel.toStdString());
            info.append(static_cast<char>(context.size()));
            if (context.size() > 0) {
                info.append(context.constData(), context.size());
            }

            out.clear();
            SwByteArray previous;
            std::uint8_t counter = 1;
            while (out.size() < outputLength) {
                SwByteArray block;
                block.append(previous);
                block.append(info);
                block.append(static_cast<char>(counter));

                previous = hmacSha256_(secret, block);
                const std::size_t remaining = outputLength - out.size();
                const std::size_t toCopy =
                    remaining < previous.size() ? remaining : previous.size();
                out.append(previous.constData(), toCopy);
                ++counter;
            }
        } catch (const std::exception& ex) {
            setError_(err, ex.what());
            return false;
        }

        clearError_(err);
        return true;
    }
};

#endif // SWTLS13KEYSCHEDULE_H
