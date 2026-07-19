#ifndef SWQUICCLIENTHELLOBUILDER_H
#define SWQUICCLIENTHELLOBUILDER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicHybridKex.h"
#include "quic/SwQuicLimits.h"
#include "quic/SwQuicPacketKeys.h"
#include "quic/SwQuicTransportParameters.h"
#include "quic/SwQuicVarIntCodec.h"
#include "quic/SwTls13KeySchedule.h"

#include <cstdint>
class SwQuicClientHelloBuilder {
public:
    // Deterministic probe variant: fixed test X25519 key + deterministic random.
    static bool buildForHttp3(const SwString& serverName,
                              const SwQuicConnectionId& initialSourceConnectionId,
                              SwByteArray& outClientHello,
                              SwString* error = nullptr) {
        SwByteArray random;
        buildDeterministicRandom_(random);
        return buildForHttp3(serverName,
                             initialSourceConnectionId,
                             x25519TestPublicKey_(),
                             random,
                             outClientHello,
                             error);
    }

    // Real-key variant: caller supplies the 32-byte X25519 public key and the
    // 32-byte ClientHello random from its own key schedule.
    static bool buildForHttp3(const SwString& serverName,
                              const SwQuicConnectionId& initialSourceConnectionId,
                              const SwByteArray& x25519Public32,
                              const SwByteArray& random32,
                              SwByteArray& outClientHello,
                              SwString* error = nullptr) {
        return buildForAlpn(serverName, initialSourceConnectionId, x25519Public32,
                            random32, SwByteArray("h3"), outClientHello, error);
    }

    // Generic QUIC ClientHello with one explicit ALPN. The protocol is an
    // opaque TLS ALPN identifier (1..255 bytes), not an HTTP/3 alias.
    static bool buildForAlpn(const SwString& serverName,
                             const SwQuicConnectionId& initialSourceConnectionId,
                             const SwByteArray& x25519Public32,
                             const SwByteArray& random32,
                             const SwByteArray& applicationProtocol,
                             SwByteArray& outClientHello,
                             SwString* error = nullptr,
                             bool rawPublicKey = false,
                             const SwQuicTransportParameters* localParameters = nullptr,
                             const SwByteArray* mlKemPublicKey = nullptr) {
        const SwString& host = serverName;
        if (host.empty() || host.size() > 255) {
            setError_(error, "Invalid QUIC TLS server name");
            return false;
        }
        if (x25519Public32.size() != 32) {
            setError_(error, "X25519 public key must be 32 bytes");
            return false;
        }
        if (random32.size() != 32) {
            setError_(error, "ClientHello random must be 32 bytes");
            return false;
        }
        if (applicationProtocol.isEmpty() || applicationProtocol.size() > 255) {
            setError_(error, "QUIC ALPN must contain 1..255 bytes");
            return false;
        }

        SwByteArray body;
        appendU16_(body, 0x0303);
        body.append(random32);
        appendU8_(body, 0);

        SwByteArray cipherSuites;
        appendU16_(cipherSuites, 0x1301);
        appendU16_(cipherSuites, 0x1302);
        appendU16_(cipherSuites, 0x1303);
        appendU16_(body, static_cast<std::uint16_t>(cipherSuites.size()));
        body.append(cipherSuites);

        appendU8_(body, 1);
        appendU8_(body, 0);

        SwByteArray extensions;

        SwByteArray sni;
        SwByteArray sniList;
        appendU8_(sniList, 0);
        appendU16_(sniList, static_cast<std::uint16_t>(host.size()));
        sniList.append(host.constData(), host.size());
        appendU16_(sni, static_cast<std::uint16_t>(sniList.size()));
        sni.append(sniList);
        appendExtension_(extensions, 0x0000, sni);

        SwByteArray supportedGroups;
        SwByteArray groupList;
        if (mlKemPublicKey) {
            if (mlKemPublicKey->size() != SwQuicHybridKex::mlKemPublicKeySize()) {
                setError_(error, "ML-KEM-768 public key must be 1184 bytes");
                return false;
            }
            appendU16_(groupList, SwQuicHybridKex::x25519MlKem768Group());
        }
        appendU16_(groupList, 0x001d);
        appendU16_(groupList, 0x0017);
        appendU16_(supportedGroups, static_cast<std::uint16_t>(groupList.size()));
        supportedGroups.append(groupList);
        appendExtension_(extensions, 0x000a, supportedGroups);

        SwByteArray signatureAlgorithms;
        SwByteArray signatureList;
        if (rawPublicKey) {
            appendU16_(signatureList, 0x0807); // ed25519
        } else {
            appendU16_(signatureList, 0x0403); // ecdsa_secp256r1_sha256
            appendU16_(signatureList, 0x0503); // ecdsa_secp384r1_sha384
            appendU16_(signatureList, 0x0807); // ed25519
            appendU16_(signatureList, 0x0804); // rsa_pss_rsae_sha256
            appendU16_(signatureList, 0x0805); // rsa_pss_rsae_sha384
            appendU16_(signatureList, 0x0806); // rsa_pss_rsae_sha512
            appendU16_(signatureList, 0x0809); // rsa_pss_pss_sha256
            appendU16_(signatureList, 0x080a); // rsa_pss_pss_sha384
            appendU16_(signatureList, 0x080b); // rsa_pss_pss_sha512
            appendU16_(signatureList, 0x0401); // rsa_pkcs1_sha256 (certificate signatures)
        }
        appendU16_(signatureAlgorithms, static_cast<std::uint16_t>(signatureList.size()));
        signatureAlgorithms.append(signatureList);
        appendExtension_(extensions, 0x000d, signatureAlgorithms);

        if (rawPublicKey) {
            // RFC 7250 + TLS 1.3: offer only RawPublicKey(2) in both
            // directions. Omitting X509 makes downgrade/fallback impossible.
            SwByteArray certificateTypes;
            appendU8_(certificateTypes, 1);
            appendU8_(certificateTypes, 2);
            appendExtension_(extensions, 0x0013, certificateTypes);
            appendExtension_(extensions, 0x0014, certificateTypes);
        }

        SwByteArray alpn;
        SwByteArray protocolList;
        appendU8_(protocolList, static_cast<std::uint8_t>(applicationProtocol.size()));
        protocolList.append(applicationProtocol);
        appendU16_(alpn, static_cast<std::uint16_t>(protocolList.size()));
        alpn.append(protocolList);
        appendExtension_(extensions, 0x0010, alpn);

        SwByteArray supportedVersions;
        appendU8_(supportedVersions, 2);
        appendU16_(supportedVersions, 0x0304);
        appendExtension_(extensions, 0x002b, supportedVersions);

        SwByteArray keyShare;
        SwByteArray keyShareEntries;
        const SwByteArray x25519 = x25519Public32;
        if (mlKemPublicKey) {
            SwByteArray hybridShare;
            if (!SwQuicHybridKex::buildClientKeyShare(
                    *mlKemPublicKey, x25519, hybridShare, error)) {
                return false;
            }
            appendU16_(keyShareEntries, SwQuicHybridKex::x25519MlKem768Group());
            appendU16_(keyShareEntries, static_cast<std::uint16_t>(hybridShare.size()));
            keyShareEntries.append(hybridShare);
        }
        appendU16_(keyShareEntries, 0x001d);
        appendU16_(keyShareEntries, static_cast<std::uint16_t>(x25519.size()));
        keyShareEntries.append(x25519);
        appendU16_(keyShare, static_cast<std::uint16_t>(keyShareEntries.size()));
        keyShare.append(keyShareEntries);
        appendExtension_(extensions, 0x0033, keyShare);

        SwByteArray transportParameters;
        if (!encodeTransportParameters_(initialSourceConnectionId,
                                        localParameters,
                                        transportParameters, error)) {
            return false;
        }
        appendExtension_(extensions, 0x0039, transportParameters);

        appendU16_(body, static_cast<std::uint16_t>(extensions.size()));
        body.append(extensions);

        outClientHello.clear();
        appendU8_(outClientHello, 0x01);
        appendU24_(outClientHello, static_cast<std::uint32_t>(body.size()));
        outClientHello.append(body);

        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Resumption ClientHello for 0-RTT (RFC 8446 4.2.11, RFC 9001 4.6). Builds
    // the same ClientHello as buildForHttp3 plus psk_key_exchange_modes,
    // early_data, and a pre_shared_key extension (which MUST be last) carrying
    // the ticket identity and a real PSK binder. Also derives the client early
    // (0-RTT) packet keys from the PSK and the ClientHello transcript.
    static bool buildForHttp3Resumption(const SwString& serverName,
                                        const SwQuicConnectionId& initialSourceConnectionId,
                                        const SwByteArray& x25519Public32,
                                        const SwByteArray& random32,
                                        const SwByteArray& ticket,
                                        std::uint32_t obfuscatedTicketAge,
                                        const SwByteArray& resumptionPsk,
                                        SwByteArray& outClientHello,
                                        SwQuicInitialKeys& outEarlyKeys,
                                        SwString* error = nullptr,
                                        const SwQuicTransportParameters* localParameters = nullptr,
                                        const SwByteArray* mlKemPublicKey = nullptr) {
        const SwString& host = serverName;
        if (host.empty() || host.size() > 255) {
            setError_(error, "Invalid QUIC TLS server name");
            return false;
        }
        if (x25519Public32.size() != 32 || random32.size() != 32) {
            setError_(error, "Invalid X25519 key or ClientHello random");
            return false;
        }
        if (resumptionPsk.size() != 32) {
            setError_(error, "Resumption PSK must be 32 bytes (SHA-256)");
            return false;
        }

        SwByteArray body;
        appendU16_(body, 0x0303);
        body.append(random32);
        appendU8_(body, 0);

        SwByteArray cipherSuites;
        appendU16_(cipherSuites, 0x1301);
        appendU16_(body, static_cast<std::uint16_t>(cipherSuites.size()));
        body.append(cipherSuites);

        appendU8_(body, 1);
        appendU8_(body, 0);

        SwByteArray extensions;
        if (!appendClientExtensions_(host, initialSourceConnectionId,
                                     x25519Public32, mlKemPublicKey, localParameters,
                                     extensions, error)) return false;

        // psk_key_exchange_modes: psk_dhe_ke(1).
        SwByteArray pskModes;
        appendU8_(pskModes, 1);   // ke_modes length
        appendU8_(pskModes, 1);   // psk_dhe_ke
        appendExtension_(extensions, 0x002d, pskModes);

        // early_data: empty in a ClientHello (signals 0-RTT intent).
        appendExtension_(extensions, 0x002a, SwByteArray());

        // pre_shared_key MUST be the last extension. Emit it with a zero
        // placeholder binder so all length fields are final, hash the truncated
        // ClientHello, then patch the real binder in.
        SwByteArray identities;
        SwByteArray identityList;
        appendU16_(identityList, static_cast<std::uint16_t>(ticket.size()));
        identityList.append(ticket);
        appendU32_(identityList, obfuscatedTicketAge);
        appendU16_(identities, static_cast<std::uint16_t>(identityList.size()));
        identities.append(identityList);

        SwByteArray placeholderBinders;
        appendU16_(placeholderBinders, 33);          // binders list length
        appendU8_(placeholderBinders, 32);           // binder length
        for (int i = 0; i < 32; ++i) {
            placeholderBinders.append(static_cast<char>(0));
        }

        SwByteArray pskExtension;
        pskExtension.append(identities);
        pskExtension.append(placeholderBinders);
        appendExtension_(extensions, 0x0029, pskExtension);

        appendU16_(body, static_cast<std::uint16_t>(extensions.size()));
        body.append(extensions);

        SwByteArray clientHello;
        appendU8_(clientHello, 0x01);
        appendU24_(clientHello, static_cast<std::uint32_t>(body.size()));
        clientHello.append(body);

        // Binder over the ClientHello truncated to remove the whole binders list.
        // Derive the strip length from the bytes we actually emitted rather than a
        // magic 35, so it stays correct if the binders list ever grows (RFC 8446
        // 4.2.11.2). pre_shared_key is the last extension, so its binders are the
        // message tail.
        const std::size_t binderStrip = placeholderBinders.size();
        SwByteArray earlySecret;
        SwByteArray binderKey;
        SwByteArray binderFinishedKey;
        if (!SwTls13KeySchedule::earlySecretWithPsk(resumptionPsk, earlySecret, error) ||
            !SwTls13KeySchedule::resumptionBinderKey(earlySecret, binderKey, error) ||
            !SwTls13KeySchedule::finishedKey(binderKey, binderFinishedKey, error)) {
            return false;
        }
        if (clientHello.size() < binderStrip) {
            setError_(error, "Resumption ClientHello is shorter than its binder tail");
            return false;
        }
        const SwByteArray truncated =
            clientHello.mid(0, static_cast<int>(clientHello.size() - binderStrip));
        const SwByteArray truncatedHash = SwTls13KeySchedule::transcriptHash(truncated);
        SwByteArray binder;
        if (!SwTls13KeySchedule::verifyData(binderFinishedKey, truncatedHash, binder, error)) {
            return false;
        }
        if (binder.size() != 32) {
            setError_(error, "PSK binder is not 32 bytes");
            return false;
        }
        for (std::size_t i = 0; i < 32; ++i) {
            clientHello[clientHello.size() - 32 + i] = binder[static_cast<int>(i)];
        }

        // client_early_traffic_secret over the full ClientHello -> 0-RTT keys.
        const SwByteArray fullHash = SwTls13KeySchedule::transcriptHash(clientHello);
        SwByteArray earlyTrafficSecret;
        if (!SwTls13KeySchedule::clientEarlyTrafficSecret(earlySecret, fullHash,
                                                          earlyTrafficSecret, error) ||
            !SwQuicPacketKeys::deriveAes128(earlyTrafficSecret, outEarlyKeys, error)) {
            return false;
        }

        outClientHello = clientHello;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Recompute the PSK binder a server expects over a received resumption
    // ClientHello message, for the given PSK. Used to verify the client binder.
    // truncationLength is the number of trailing bytes forming the PskBinderEntry
    // list (2-byte length prefix + list body) that must be stripped before the
    // transcript hash (RFC 8446 4.2.11.2). Pass 0 to assume a single 32-byte
    // binder (35 = 2 + 1 + 32), which is all this library's client ever sends.
    static bool computeExpectedBinder(const SwByteArray& clientHelloMessage,
                                      const SwByteArray& resumptionPsk,
                                      SwByteArray& outBinder,
                                      SwString* error = nullptr,
                                      std::size_t truncationLength = 0) {
        const std::size_t strip = (truncationLength != 0) ? truncationLength : 35;
        if (clientHelloMessage.size() < strip) {
            setError_(error, "Resumption ClientHello is shorter than its binder tail");
            return false;
        }
        SwByteArray earlySecret;
        SwByteArray binderKey;
        SwByteArray binderFinishedKey;
        if (!SwTls13KeySchedule::earlySecretWithPsk(resumptionPsk, earlySecret, error) ||
            !SwTls13KeySchedule::resumptionBinderKey(earlySecret, binderKey, error) ||
            !SwTls13KeySchedule::finishedKey(binderKey, binderFinishedKey, error)) {
            return false;
        }
        const SwByteArray truncated =
            clientHelloMessage.mid(0, static_cast<int>(clientHelloMessage.size() - strip));
        const SwByteArray truncatedHash = SwTls13KeySchedule::transcriptHash(truncated);
        return SwTls13KeySchedule::verifyData(binderFinishedKey, truncatedHash, outBinder, error);
    }

    // Derive the client early (0-RTT) packet keys a server uses to decrypt
    // 0-RTT packets, from the PSK and the received full ClientHello message.
    static bool deriveServerEarlyKeys(const SwByteArray& clientHelloMessage,
                                      const SwByteArray& resumptionPsk,
                                      SwQuicInitialKeys& outEarlyKeys,
                                      SwString* error = nullptr) {
        SwByteArray earlySecret;
        SwByteArray earlyTrafficSecret;
        const SwByteArray fullHash = SwTls13KeySchedule::transcriptHash(clientHelloMessage);
        return SwTls13KeySchedule::earlySecretWithPsk(resumptionPsk, earlySecret, error) &&
               SwTls13KeySchedule::clientEarlyTrafficSecret(earlySecret, fullHash,
                                                            earlyTrafficSecret, error) &&
               SwQuicPacketKeys::deriveAes128(earlyTrafficSecret, outEarlyKeys, error);
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void appendU8_(SwByteArray& out, std::uint8_t value) {
        out.append(static_cast<char>(value));
    }

    static void appendU16_(SwByteArray& out, std::uint16_t value) {
        appendU8_(out, static_cast<std::uint8_t>((value >> 8) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>(value & 0xffU));
    }

    static void appendU24_(SwByteArray& out, std::uint32_t value) {
        appendU8_(out, static_cast<std::uint8_t>((value >> 16) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>((value >> 8) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>(value & 0xffU));
    }

    static void appendU32_(SwByteArray& out, std::uint32_t value) {
        appendU8_(out, static_cast<std::uint8_t>((value >> 24) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>((value >> 16) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>((value >> 8) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>(value & 0xffU));
    }

    // The common ClientHello extensions shared by the fresh and resumption
    // handshakes: SNI, supported_groups, signature_algorithms, ALPN,
    // supported_versions, key_share (X25519MLKEM768 when supplied, plus
    // X25519 fallback), and QUIC transport parameters.
    static bool appendClientExtensions_(const SwString& host,
                                        const SwQuicConnectionId& initialSourceConnectionId,
                                        const SwByteArray& x25519Public32,
                                        const SwByteArray* mlKemPublicKey,
                                        const SwQuicTransportParameters* localParameters,
                                        SwByteArray& extensions,
                                        SwString* error) {
        SwByteArray sni;
        SwByteArray sniList;
        appendU8_(sniList, 0);
        appendU16_(sniList, static_cast<std::uint16_t>(host.size()));
        sniList.append(host.constData(), host.size());
        appendU16_(sni, static_cast<std::uint16_t>(sniList.size()));
        sni.append(sniList);
        appendExtension_(extensions, 0x0000, sni);

        SwByteArray supportedGroups;
        SwByteArray groupList;
        if (mlKemPublicKey) {
            if (mlKemPublicKey->size() != SwQuicHybridKex::mlKemPublicKeySize()) {
                setError_(error, "ML-KEM-768 public key must be 1184 bytes");
                return false;
            }
            appendU16_(groupList, SwQuicHybridKex::x25519MlKem768Group());
        }
        appendU16_(groupList, 0x001d);
        appendU16_(groupList, 0x0017);
        appendU16_(supportedGroups, static_cast<std::uint16_t>(groupList.size()));
        supportedGroups.append(groupList);
        appendExtension_(extensions, 0x000a, supportedGroups);

        SwByteArray signatureAlgorithms;
        SwByteArray signatureList;
        appendU16_(signatureList, 0x0403); // ecdsa_secp256r1_sha256
        appendU16_(signatureList, 0x0503); // ecdsa_secp384r1_sha384
        appendU16_(signatureList, 0x0807); // ed25519
        appendU16_(signatureList, 0x0804); // rsa_pss_rsae_sha256
        appendU16_(signatureList, 0x0805); // rsa_pss_rsae_sha384
        appendU16_(signatureList, 0x0806); // rsa_pss_rsae_sha512
        appendU16_(signatureList, 0x0809); // rsa_pss_pss_sha256
        appendU16_(signatureList, 0x080a); // rsa_pss_pss_sha384
        appendU16_(signatureList, 0x080b); // rsa_pss_pss_sha512
        appendU16_(signatureList, 0x0401); // rsa_pkcs1_sha256 (certificate signatures)
        appendU16_(signatureAlgorithms, static_cast<std::uint16_t>(signatureList.size()));
        signatureAlgorithms.append(signatureList);
        appendExtension_(extensions, 0x000d, signatureAlgorithms);

        SwByteArray alpn;
        SwByteArray protocolList;
        appendU8_(protocolList, 2);
        protocolList.append("h3", 2);
        appendU16_(alpn, static_cast<std::uint16_t>(protocolList.size()));
        alpn.append(protocolList);
        appendExtension_(extensions, 0x0010, alpn);

        SwByteArray supportedVersions;
        appendU8_(supportedVersions, 2);
        appendU16_(supportedVersions, 0x0304);
        appendExtension_(extensions, 0x002b, supportedVersions);

        SwByteArray keyShare;
        SwByteArray keyShareEntries;
        if (mlKemPublicKey) {
            SwByteArray hybridShare;
            if (!SwQuicHybridKex::buildClientKeyShare(
                    *mlKemPublicKey, x25519Public32, hybridShare, error)) {
                return false;
            }
            appendU16_(keyShareEntries, SwQuicHybridKex::x25519MlKem768Group());
            appendU16_(keyShareEntries, static_cast<std::uint16_t>(hybridShare.size()));
            keyShareEntries.append(hybridShare);
        }
        appendU16_(keyShareEntries, 0x001d);
        appendU16_(keyShareEntries, static_cast<std::uint16_t>(x25519Public32.size()));
        keyShareEntries.append(x25519Public32);
        appendU16_(keyShare, static_cast<std::uint16_t>(keyShareEntries.size()));
        keyShare.append(keyShareEntries);
        appendExtension_(extensions, 0x0033, keyShare);

        SwByteArray transportParameters;
        if (!encodeTransportParameters_(initialSourceConnectionId,
                                        localParameters,
                                        transportParameters, error)) return false;
        appendExtension_(extensions, 0x0039, transportParameters);
        return true;
    }

    static SwQuicTransportParameters defaultTransportParameters_() {
        SwQuicTransportParameters parameters;
        parameters.maxIdleTimeoutMs = 30000;
        parameters.maxUdpPayloadSize = SwQuicLimits::maximumUdpPayloadBytes();
        parameters.initialMaxData = 1048576;
        parameters.initialMaxStreamDataBidiLocal = 262144;
        parameters.initialMaxStreamDataBidiRemote = 262144;
        parameters.initialMaxStreamDataUni = 262144;
        parameters.initialMaxStreamsBidi = 100;
        parameters.initialMaxStreamsUni = 100;
        parameters.activeConnectionIdLimit = 4;
        parameters.maxDatagramFrameSize =
            SwQuicLimits::maximumDatagramFrameBytes();
        parameters.resetStreamAt = true;
        return parameters;
    }

    static bool encodeTransportParameters_(
            const SwQuicConnectionId& initialSourceConnectionId,
            const SwQuicTransportParameters* localParameters,
            SwByteArray& out, SwString* error) {
        SwQuicTransportParameters parameters = localParameters
            ? *localParameters : defaultTransportParameters_();
        // These fields are server-only or generated by this ClientHello.
        parameters.hasOriginalDestinationConnectionId = false;
        parameters.originalDestinationConnectionId.clear();
        parameters.hasStatelessResetToken = false;
        parameters.statelessResetToken.clear();
        parameters.hasRetrySourceConnectionId = false;
        parameters.retrySourceConnectionId.clear();
        parameters.hasInitialSourceConnectionId = true;
        parameters.initialSourceConnectionId = initialSourceConnectionId.bytes();
        return parameters.encode(out, error);
    }

    static void appendExtension_(SwByteArray& extensions,
                                 std::uint16_t type,
                                 const SwByteArray& data) {
        appendU16_(extensions, type);
        appendU16_(extensions, static_cast<std::uint16_t>(data.size()));
        extensions.append(data);
    }

    static bool appendTransportParameter_(SwByteArray& out,
                                          std::uint64_t id,
                                          const SwByteArray& value,
                                          SwString* error) {
        return SwQuicVarIntCodec::encode(id, out, error) &&
               SwQuicVarIntCodec::encode(static_cast<std::uint64_t>(value.size()), out, error) &&
               (out.append(value), true);
    }

    static bool appendVarIntTransportParameter_(SwByteArray& out,
                                                std::uint64_t id,
                                                std::uint64_t value,
                                                SwString* error) {
        SwByteArray encoded;
        if (!SwQuicVarIntCodec::encode(value, encoded, error)) {
            return false;
        }
        return appendTransportParameter_(out, id, encoded, error);
    }

    static SwByteArray x25519TestPublicKey_() {
        return SwByteArray::fromHex(
            SwByteArray("8520f0098930a754748b7ddcb43ef75a"
                        "0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    }

    static void buildDeterministicRandom_(SwByteArray& out) {
        out.clear();
        const SwByteArray seed("swstack-quic-clienthello-random-01");
        for (std::size_t i = 0; i < 32; ++i) {
            out.append(seed[i % seed.size()]);
        }
    }
};

#endif
