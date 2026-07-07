#ifndef SWQUICCLIENTHELLOBUILDER_H
#define SWQUICCLIENTHELLOBUILDER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicPacketKeys.h"
#include "quic/SwQuicVarIntCodec.h"
#include "quic/SwTls13KeySchedule.h"

#include <cstdint>
#include <string>

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
        const std::string host = serverName.toStdString();
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
        sniList.append(host);
        appendU16_(sni, static_cast<std::uint16_t>(sniList.size()));
        sni.append(sniList);
        appendExtension_(extensions, 0x0000, sni);

        SwByteArray supportedGroups;
        SwByteArray groupList;
        appendU16_(groupList, 0x001d);
        appendU16_(groupList, 0x0017);
        appendU16_(supportedGroups, static_cast<std::uint16_t>(groupList.size()));
        supportedGroups.append(groupList);
        appendExtension_(extensions, 0x000a, supportedGroups);

        SwByteArray signatureAlgorithms;
        SwByteArray signatureList;
        appendU16_(signatureList, 0x0403);
        appendU16_(signatureList, 0x0804);
        appendU16_(signatureList, 0x0805);
        appendU16_(signatureList, 0x0401);
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
        const SwByteArray x25519 = x25519Public32;
        appendU16_(keyShareEntries, 0x001d);
        appendU16_(keyShareEntries, static_cast<std::uint16_t>(x25519.size()));
        keyShareEntries.append(x25519);
        appendU16_(keyShare, static_cast<std::uint16_t>(keyShareEntries.size()));
        keyShare.append(keyShareEntries);
        appendExtension_(extensions, 0x0033, keyShare);

        SwByteArray transportParameters;
        if (!appendVarIntTransportParameter_(transportParameters, 0x01, 30000, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x03, 1200, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x04, 1048576, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x05, 262144, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x06, 262144, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x07, 262144, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x08, 100, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x09, 100, error) ||
            !appendVarIntTransportParameter_(transportParameters, 0x0e, 4, error) ||
            !appendTransportParameter_(transportParameters,
                                       0x0f,
                                       initialSourceConnectionId.bytes(),
                                       error)) {
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
                                        SwString* error = nullptr) {
        const std::string host = serverName.toStdString();
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
        appendClientExtensions_(host, initialSourceConnectionId, x25519Public32, extensions);

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
    // supported_versions, key_share (x25519), and QUIC transport parameters.
    static void appendClientExtensions_(const std::string& host,
                                        const SwQuicConnectionId& initialSourceConnectionId,
                                        const SwByteArray& x25519Public32,
                                        SwByteArray& extensions) {
        SwByteArray sni;
        SwByteArray sniList;
        appendU8_(sniList, 0);
        appendU16_(sniList, static_cast<std::uint16_t>(host.size()));
        sniList.append(host);
        appendU16_(sni, static_cast<std::uint16_t>(sniList.size()));
        sni.append(sniList);
        appendExtension_(extensions, 0x0000, sni);

        SwByteArray supportedGroups;
        SwByteArray groupList;
        appendU16_(groupList, 0x001d);
        appendU16_(groupList, 0x0017);
        appendU16_(supportedGroups, static_cast<std::uint16_t>(groupList.size()));
        supportedGroups.append(groupList);
        appendExtension_(extensions, 0x000a, supportedGroups);

        SwByteArray signatureAlgorithms;
        SwByteArray signatureList;
        appendU16_(signatureList, 0x0403);
        appendU16_(signatureList, 0x0804);
        appendU16_(signatureList, 0x0805);
        appendU16_(signatureList, 0x0401);
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
        appendU16_(keyShareEntries, 0x001d);
        appendU16_(keyShareEntries, static_cast<std::uint16_t>(x25519Public32.size()));
        keyShareEntries.append(x25519Public32);
        appendU16_(keyShare, static_cast<std::uint16_t>(keyShareEntries.size()));
        keyShare.append(keyShareEntries);
        appendExtension_(extensions, 0x0033, keyShare);

        SwByteArray transportParameters;
        SwString ignoredError;
        appendVarIntTransportParameter_(transportParameters, 0x01, 30000, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x03, 1200, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x04, 1048576, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x05, 262144, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x06, 262144, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x07, 262144, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x08, 100, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x09, 100, &ignoredError);
        appendVarIntTransportParameter_(transportParameters, 0x0e, 4, &ignoredError);
        appendTransportParameter_(transportParameters, 0x0f,
                                  initialSourceConnectionId.bytes(), &ignoredError);
        appendExtension_(extensions, 0x0039, transportParameters);
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
