#ifndef SWQUICHANDSHAKESERVER_H
#define SWQUICHANDSHAKESERVER_H

#include "SwHash.h"
#include "SwMap.h"
#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicClientHelloBuilder.h"
#include "quic/SwQuicCertificateVerifier.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicFrameCodec.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicLimits.h"
#include "quic/SwQuicPacketProtector.h"
#include "quic/SwQuicPacketKeys.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwQuicServerCredential.h"
#include "quic/SwQuicSessionTicket.h"
#include "quic/SwQuicStream.h"
#include "quic/SwQuicTransportParameters.h"
#include "quic/SwQuicVarIntCodec.h"
#include "quic/SwQuicX25519.h"
#include "quic/SwTls13KeySchedule.h"
#include "quic/SwTls13Messages.h"

#include <cstdint>
#include <functional>

// Server-side driver for the QUIC v1 + TLS 1.3 handshake (RFC 9000 / 9001 /
// 8446), cipher suite TLS_AES_128_GCM_SHA256, ALPN "h3". Transport agnostic:
// processIncomingDatagram() consumes a client datagram and appends datagrams to
// send back. It accepts the client Initial (ClientHello), answers with
// ServerHello + the encrypted Handshake flight (EncryptedExtensions,
// Certificate, CertificateVerify signed by the supplied credential, Finished),
// verifies the client Finished, and derives 1-RTT keys.
//
// The certificate + CertificateVerify signature come from a SwQuicServerCredential
// (see SwQuicEcdsaCredential::createSelfSigned for a self-contained one).
// Optional client authentication sends CertificateRequest and verifies the
// client's chain leaf, CertificateVerify, Finished and delegated SPKI policy.
class SwQuicHandshakeServer {
public:
    enum class State {
        Idle,
        WaitClientInitial,
        WaitClientFinished,
        Complete,
        Failed
    };

    // Selected after the complete ClientHello has been reassembled, but before
    // the server flight is constructed. This permits one UDP QUIC endpoint to
    // host protocols with different client-authentication policies (for
    // example an anonymous carrier ALPN and an mTLS fleet ALPN) without racing
    // two handshake engines on the same Initial.
    struct ApplicationProtocolPolicy {
        SwByteArray protocol;
        bool requireClientAuthentication = false;
        bool requireRawPublicKeys = false;
        std::function<bool(const SwByteArray&)> clientSpkiVerifier;
    };
    using ApplicationProtocolSelector = std::function<bool(
        const SwVector<SwByteArray>& offered, ApplicationProtocolPolicy& selected)>;

    SwQuicHandshakeServer()
        : m_state(State::Idle),
          m_serverInitialPacketNumber(0),
          m_serverHandshakePacketNumber(0),
          m_handshakeComplete(false),
          m_pendingClientInitialAck(false) {
        m_localParams.initialMaxData = 1048576;
        m_localParams.initialMaxStreamDataBidiLocal = 262144;
        m_localParams.initialMaxStreamDataBidiRemote = 262144;
        m_localParams.initialMaxStreamDataUni = 262144;
        m_localParams.initialMaxStreamsBidi = 100;
        m_localParams.initialMaxStreamsUni = 100;
        m_localParams.maxIdleTimeoutMs = 30000;
        m_localParams.maxUdpPayloadSize = SwQuicLimits::maximumUdpPayloadBytes();
        m_localParams.maxDatagramFrameSize = SwQuicLimits::maximumDatagramFrameBytes();
    }

    State state() const { return m_state; }
    bool handshakeComplete() const { return m_handshakeComplete; }
    const SwString& errorString() const { return m_error; }

    const SwQuicInitialKeys& serverHandshakeKeys() const { return m_serverHandshakeKeys; }
    const SwQuicInitialKeys& clientHandshakeKeys() const { return m_clientHandshakeKeys; }
    const SwQuicInitialKeys& serverApplicationKeys() const { return m_serverApplicationKeys; }
    const SwQuicInitialKeys& clientApplicationKeys() const { return m_clientApplicationKeys; }
    const SwQuicConnectionId& serverConnectionId() const { return m_serverConnectionId; }
    const SwQuicConnectionId& clientConnectionId() const { return m_clientConnectionId; }
    // DCID observe dans le premier Initial et authentifie indirectement par les
    // transport parameters QUIC standards envoyes au client.
    const SwQuicConnectionId& originalDestinationConnectionId() const {
        return m_originalDestinationConnectionId;
    }
    // Next 1-RTT packet number after the server confirmation flight: a
    // connection taking over the app space must continue from here so it never
    // reuses an AEAD nonce already spent on HANDSHAKE_DONE.
    std::uint64_t serverApplicationPacketNumber() const { return m_serverApplicationPacketNumber; }
    const SwByteArray& negotiatedAlpn() const { return m_negotiatedAlpn; }
    bool setApplicationProtocol(const SwByteArray& protocol) {
        if (m_state != State::Idle || protocol.isEmpty() || protocol.size() > 255) return false;
        m_applicationProtocol = protocol;
        m_applicationProtocolSelector = ApplicationProtocolSelector();
        return true;
    }
    bool setApplicationProtocolSelector(ApplicationProtocolSelector selector) {
        if (m_state != State::Idle || !selector) return false;
        m_applicationProtocolSelector = std::move(selector);
        return true;
    }
    const SwByteArray& applicationProtocol() const { return m_applicationProtocol; }
    bool hasPeerTransportParameters() const { return m_hasPeerParams; }
    const SwQuicTransportParameters& peerTransportParameters() const { return m_peerParams; }
    // Empty until the client CertificateVerify and Finished have both been
    // authenticated. A received Certificate message alone is not identity.
    const SwByteArray& authenticatedClientSubjectPublicKeyInfo() const {
        return m_authenticatedClientSpkiDer;
    }

    // RFC 8446 section 7.5. Keep exporter_master_secret private and expose
    // only labelled derivation after the peer Finished has authenticated.
    bool exportKeyingMaterial(const SwString& label,
                              const SwByteArray& context,
                              std::size_t length,
                              SwByteArray& out,
                              SwString* error = nullptr) const {
        if (!m_handshakeComplete || m_exporterMasterSecret.size() != 32) {
            out.clear();
            if (error) {
                *error = SwString("TLS exporter is unavailable before handshake completion");
            }
            return false;
        }
        return SwTls13KeySchedule::exportKeyingMaterial(
            m_exporterMasterSecret, label, context, length, out, error);
    }

    SwQuicTransportParameters& localTransportParameters() { return m_localParams; }
    const SwQuicTransportParameters& localTransportParameters() const { return m_localParams; }

    // Install the certificate + signing credential the server presents.
    void setCredential(const SwQuicServerCredential& credential) { m_credential = credential; }

    // Request and require TLS 1.3 client authentication. The verifier receives
    // only canonical DER SubjectPublicKeyInfo and is therefore suitable for a
    // pinned fleet CA/leaf policy. It runs before Finished, while the identity
    // accessor above remains empty until Finished authenticates the transcript.
    void setRequireClientAuthentication(bool required) { m_requireClientAuth = required; }
    bool requiresClientAuthentication() const { return m_requireClientAuth; }
    void setRequireRawPublicKeys(bool required) {
        m_requireRawPublicKeys = required;
        if (required) m_requireClientAuth = true;
    }
    bool requiresRawPublicKeys() const { return m_requireRawPublicKeys; }
    void setClientSubjectPublicKeyInfoVerifier(
            std::function<bool(const SwByteArray&)> verifier) {
        m_clientSpkiVerifier = std::move(verifier);
    }
    bool hasClientSubjectPublicKeyInfoVerifier() const {
        return static_cast<bool>(m_clientSpkiVerifier);
    }

    // Enable session resumption / 0-RTT: on a resumption ClientHello whose PSK
    // is in this store and whose binder verifies, the server accepts the PSK
    // (and the offered 0-RTT if the ticket allows early data).
    void setTicketStore(SwQuicTicketStore* store) { m_ticketStore = store; }

    bool isResuming() const { return m_resuming; }
    bool acceptedEarlyData() const { return m_acceptEarlyData; }
    const SwByteArray& receivedEarlyData() const { return m_receivedEarlyData; }
    // The 0-RTT STREAM frames, preserved so a driver can replay them into the
    // established connection (RFC 9001 4.6: early data resumes on 1-RTT streams).
    const SwVector<SwQuicFrame>& earlyStreamFrames() const { return m_earlyStreamFrames; }

    bool processIncomingDatagram(const SwByteArray& datagram,
                                 SwVector<SwByteArray>& outDatagrams,
                                 SwString* error = nullptr) {
        if (m_state == State::Failed) {
            setError_(error, "QUIC handshake server is in the failed state");
            return false;
        }
        if (!m_credential.isValid()) {
            setError_(error, "QUIC handshake server has no valid credential");
            return fail_(error);
        }
        if (m_requireClientAuth && !m_clientSpkiVerifier) {
            setError_(error, "Client authentication requires a client SPKI verifier");
            return fail_(error);
        }
        if (m_requireRawPublicKeys &&
            (m_credential.certificateType != SwQuicCertificateType::RawPublicKey ||
             m_credential.signatureScheme != 0x0807 ||
             m_credential.certificateChain.size() != 1)) {
            setError_(error,
                      "RFC 7250 server requires one Ed25519 raw-public-key credential");
            return fail_(error);
        }

        // A datagram carrying an Initial packet must be at least 1200 bytes;
        // discard smaller ones (RFC 9000 14.1). This, with the amplification
        // limit, prevents the server from being an off-path reflector.
        if (!datagram.isEmpty() &&
            (static_cast<std::uint8_t>(datagram.constData()[0]) & 0x80U) != 0 &&
            (static_cast<std::uint8_t>(datagram.constData()[0]) & 0x30U) == 0x00U &&
            datagram.size() < 1200) {
            clearError_(error);
            return true; // silently discarded
        }
        m_bytesReceivedFromClient += static_cast<std::uint64_t>(datagram.size());

        std::size_t offset = 0;
        while (offset < datagram.size()) {
            const std::uint8_t firstByte =
                static_cast<std::uint8_t>(datagram.constData()[offset]);
            if ((firstByte & 0x80U) == 0) {
                break; // short header 1-RTT: handshake driver stops here
            }

            const std::uint8_t longType = static_cast<std::uint8_t>(firstByte & 0x30U);
            SwByteArray remainingStorage;
            const SwByteArray* remaining = &datagram;
            if (offset > 0) {
                remainingStorage = datagram.mid(
                    static_cast<int>(offset),
                    static_cast<int>(datagram.size() - offset));
                remaining = &remainingStorage;
            }
            std::size_t consumed = 0;

            if (longType == 0x00U) {
                if (!handleClientInitial_(*remaining, consumed, error)) {
                    return fail_(error);
                }
            } else if (longType == 0x20U) {
                if (!handleClientHandshake_(*remaining, consumed, error)) {
                    return fail_(error);
                }
            } else if (longType == 0x10U) {
                // 0-RTT (RFC 9001 4.6): decrypt with the early keys if we
                // accepted the PSK; otherwise skip it (0-RTT was rejected).
                if (!handleClientZeroRtt_(*remaining, consumed, error)) {
                    return fail_(error);
                }
            } else {
                setError_(error, "QUIC Retry packet is not supported by this server");
                return fail_(error);
            }

            if (consumed == 0) {
                break;
            }
            offset += consumed;
        }

        // Once the ClientHello is complete, emit the server flight exactly once.
        if (m_state == State::WaitClientInitial && !m_clientHelloMessage.isEmpty()) {
            if (!emitServerFlight_(outDatagrams, error)) {
                return fail_(error);
            }
            m_state = State::WaitClientFinished;
        }

        // After the client Finished verifies, acknowledge the client's
        // Handshake packet and confirm the handshake with HANDSHAKE_DONE in a
        // 1-RTT packet, exactly once (RFC 9000 13.1 / RFC 9001 4.1.2).
        if (m_state == State::WaitClientFinished && m_handshakeComplete) {
            if (!emitHandshakeConfirmation_(outDatagrams, error)) {
                return fail_(error);
            }
            discardCompletedHandshakeSecrets_();
            m_state = State::Complete;
        }

        clearError_(error);
        return true;
    }

private:
    class SecretGuard_ {
    public:
        explicit SecretGuard_(SwByteArray& secret) : m_secret(secret) {}
        ~SecretGuard_() { m_secret.secureClear(); }

    private:
        SecretGuard_(const SecretGuard_&) = delete;
        SecretGuard_& operator=(const SecretGuard_&) = delete;
        SwByteArray& m_secret;
    };

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
    bool fail_(SwString* error) {
        discardFailedSecrets_();
        m_state = State::Failed;
        if (error && !error->isEmpty()) {
            m_error = *error;
        }
        return false;
    }

    static void secureClearKeys_(SwQuicInitialKeys& keys) noexcept {
        keys.secret.secureClear();
        keys.key.secureClear();
        keys.iv.secureClear();
        keys.headerProtectionKey.secureClear();
    }

    void discardCompletedHandshakeSecrets_() noexcept {
        m_handshakeSecret.secureClear();
        m_clientHandshakeTrafficSecret.secureClear();
        m_serverHandshakeTrafficSecret.secureClear();
        m_masterSecret.secureClear();
        m_resumptionPsk.secureClear();
        secureClearKeys_(m_clientInitialKeys);
        secureClearKeys_(m_serverInitialKeys);
        secureClearKeys_(m_earlyKeys);
        m_hasEarlyKeys = false;
    }

    void discardFailedSecrets_() noexcept {
        discardCompletedHandshakeSecrets_();
        m_exporterMasterSecret.secureClear();
        m_resumptionMasterSecret.secureClear();
        m_clientApplicationTrafficSecret.secureClear();
        m_serverApplicationTrafficSecret.secureClear();
        secureClearKeys_(m_clientHandshakeKeys);
        secureClearKeys_(m_serverHandshakeKeys);
        secureClearKeys_(m_clientApplicationKeys);
        secureClearKeys_(m_serverApplicationKeys);
    }

    static SwByteArray rawMessage_(std::uint8_t type, const SwByteArray& body) {
        SwByteArray out;
        out.append(static_cast<char>(type));
        out.append(static_cast<char>((body.size() >> 16) & 0xffU));
        out.append(static_cast<char>((body.size() >> 8) & 0xffU));
        out.append(static_cast<char>(body.size() & 0xffU));
        out.append(body);
        return out;
    }

    static void appendU16_(SwByteArray& out, std::uint16_t value) {
        out.append(static_cast<char>((value >> 8) & 0xffU));
        out.append(static_cast<char>(value & 0xffU));
    }
    static void appendU24_(SwByteArray& out, std::uint32_t value) {
        out.append(static_cast<char>((value >> 16) & 0xffU));
        out.append(static_cast<char>((value >> 8) & 0xffU));
        out.append(static_cast<char>(value & 0xffU));
    }

    // ------------------------------------------------------------ Initial ---

    bool handleClientInitial_(const SwByteArray& packet,
                              std::size_t& consumed,
                              SwString* error) {
        if (m_state == State::Idle) {
            m_state = State::WaitClientInitial;
        }

        // The client's original DCID is the AEAD input for the Initial secrets;
        // it is the cleartext DCID in the packet header.
        SwQuicConnectionId packetDcid;
        if (!peekInitialDcid_(packet, packetDcid, error)) {
            return false;
        }
        if (m_originalDestinationConnectionId.isEmpty()) {
            m_originalDestinationConnectionId = packetDcid;
            if (!SwQuicInitialSecrets::deriveV1(m_originalDestinationConnectionId,
                                                m_clientInitialKeys, m_serverInitialKeys, error)) {
                return false;
            }
        }

        SwQuicPacketHeader header;
        SwByteArray payload;
        const std::uint64_t* largest =
            m_haveClientInitialLargest ? &m_clientInitialLargest : nullptr;
        if (!SwQuicPacketProtector::unprotectInitial(m_clientInitialKeys, packet, header, payload,
                                                     &consumed, error, largest)) {
            return false;
        }

        if (!header.sourceConnectionId().isEmpty()) {
            m_clientConnectionId = header.sourceConnectionId();
        }
        recordClientInitialPn_(header.packetNumber());

        SwVector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(payload, frames, error)) {
            return false;
        }
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::Crypto) {
                if (!m_clientInitialCryptoReassembly.receive(frames[i].offset(),
                                                             frames[i].data(), false, error)) {
                    return false;
                }
                const SwByteArray contiguous = m_clientInitialCryptoReassembly.readContiguous();
                if (!contiguous.isEmpty()) {
                    m_clientInitialCrypto.append(contiguous);
                }
            }
        }

        return tryParseClientHello_(error);
    }

    // Decrypt a coalesced 0-RTT packet with the early keys and collect its
    // early application data. Only reachable after the ClientHello (which
    // derives the early keys) has been parsed in the same datagram.
    bool handleClientZeroRtt_(const SwByteArray& packet, std::size_t& consumed,
                              SwString* error) {
        if (!m_hasEarlyKeys) {
            // 0-RTT not accepted (no PSK/early keys): skip it, keep going.
            return skipLongHeaderNoToken_(packet, consumed, error);
        }
        SwQuicPacketHeader header;
        SwByteArray payload;
        const std::uint64_t* largest =
            m_haveClientEarlyLargest ? &m_clientEarlyLargest : nullptr;
        if (!SwQuicPacketProtector::unprotectZeroRtt(m_earlyKeys, packet, header, payload,
                                                     &consumed, error, largest)) {
            return false;
        }
        // Discard a duplicate 0-RTT packet (RFC 9000 12.3 / RFC 9001 9.2): a
        // replayed packet must not be re-counted or re-processed, else a
        // replay flood could trip the max_early_data_size abort (DoS).
        if (m_clientEarlyPns.contains(header.packetNumber())) {
            clearError_(error);
            return true;
        }
        m_clientEarlyPns.insert(header.packetNumber(), true);
        if (!m_haveClientEarlyLargest || header.packetNumber() > m_clientEarlyLargest) {
            m_clientEarlyLargest = header.packetNumber();
            m_haveClientEarlyLargest = true;
        }

        SwVector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(payload, frames, error)) {
            return false;
        }
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::Stream) {
                // Enforce max_early_data_size: reject a client that sends more
                // 0-RTT than the ticket allowed (RFC 8446 4.6.1).
                m_earlyDataBytesReceived +=
                    static_cast<std::uint64_t>(frames[i].data().size());
                if (m_earlyDataBytesReceived > m_maxEarlyDataSize) {
                    setError_(error, "0-RTT early data exceeds max_early_data_size");
                    return false;
                }
                m_receivedEarlyData.append(frames[i].data());
                m_earlyStreamFrames.push_back(frames[i]); // preserved for replay
            }
        }
        clearError_(error);
        return true;
    }

    // Skip a token-less long-header packet using its cleartext length.
    static bool skipLongHeaderNoToken_(const SwByteArray& packet, std::size_t& consumed,
                                       SwString* error) {
        std::size_t offset = 1 + 4;
        if (offset >= packet.size()) {
            setError_(error, "QUIC long header packet is truncated");
            return false;
        }
        const std::uint8_t dcidLength = static_cast<std::uint8_t>(packet.constData()[offset]);
        offset += 1 + dcidLength;
        if (offset >= packet.size()) {
            setError_(error, "QUIC long header packet is truncated");
            return false;
        }
        const std::uint8_t scidLength = static_cast<std::uint8_t>(packet.constData()[offset]);
        offset += 1 + scidLength;
        std::uint64_t payloadLength = 0;
        if (!SwQuicVarIntCodec::decode(packet, offset, payloadLength, error) ||
            payloadLength > static_cast<std::uint64_t>(packet.size() - offset)) {
            setError_(error, "QUIC long header payload is truncated");
            return false;
        }
        consumed = offset + static_cast<std::size_t>(payloadLength);
        return true;
    }

    bool tryParseClientHello_(SwString* error) {
        if (!m_clientHelloMessage.isEmpty()) {
            return true; // already parsed
        }

        SwVector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(m_clientInitialCrypto, messages, error)) {
            clearError_(error);
            return true; // wait for more CRYPTO
        }
        if (messages.empty() || messages[0].type != 0x01) {
            return true;
        }

        SwTls13Messages::ClientHello clientHello;
        if (!SwTls13Messages::parseClientHello(messages[0].body, clientHello, error)) {
            return false;
        }
        if (!clientHello.offersAes128GcmSha256) {
            setError_(error, "Client did not offer TLS_AES_128_GCM_SHA256");
            return false;
        }
        if (clientHello.clientX25519Public.size() != 32) {
            setError_(error, "ClientHello has no X25519 key share");
            return false;
        }
        // ALPN: QUIC requires a negotiated application protocol (RFC 9001 8.1).
        // The client MUST offer ALPN and it must include "h3"; otherwise abort
        // with no_application_protocol rather than assume h3.
        if (!clientHello.hasAlpn) {
            setError_(error, "Client did not offer the configured ALPN (no_application_protocol)");
            return false;
        }
        if (m_applicationProtocolSelector) {
            ApplicationProtocolPolicy policy;
            if (!m_applicationProtocolSelector(clientHello.alpnProtocols, policy) ||
                policy.protocol.isEmpty() || policy.protocol.size() > 255 ||
                !offersProtocol_(clientHello.alpnProtocols, policy.protocol)) {
                setError_(error,
                          "Client did not offer an accepted ALPN (no_application_protocol)");
                return false;
            }
            m_applicationProtocol = policy.protocol;
            m_requireRawPublicKeys = policy.requireRawPublicKeys;
            m_requireClientAuth = policy.requireClientAuthentication ||
                                  policy.requireRawPublicKeys;
            m_clientSpkiVerifier = std::move(policy.clientSpkiVerifier);
        } else if (!offersProtocol_(clientHello.alpnProtocols, m_applicationProtocol)) {
            setError_(error, "Client did not offer the configured ALPN (no_application_protocol)");
            return false;
        }
        if (m_requireClientAuth && !m_clientSpkiVerifier) {
            setError_(error, "Selected ALPN requires a client SPKI verifier");
            return false;
        }
        if (m_requireRawPublicKeys &&
            (m_credential.certificateType != SwQuicCertificateType::RawPublicKey ||
             m_credential.signatureScheme != 0x0807 ||
             m_credential.certificateChain.size() != 1)) {
            setError_(error,
                      "RFC 7250 server requires one Ed25519 raw-public-key credential");
            return false;
        }
        // quic_transport_parameters is mandatory (RFC 9001 8.2).
        if (!clientHello.hasTransportParameters) {
            setError_(error, "ClientHello is missing quic_transport_parameters (RFC 9001 8.2)");
            return false;
        }
        if (m_requireRawPublicKeys &&
            (!clientHello.hasClientCertificateTypes ||
             !clientHello.hasServerCertificateTypes ||
             clientHello.clientCertificateTypes.size() != 1 ||
             clientHello.serverCertificateTypes.size() != 1 ||
             clientHello.clientCertificateTypes.front() != 2 ||
             clientHello.serverCertificateTypes.front() != 2)) {
            setError_(error,
                      "Client did not exclusively offer mutual RFC 7250 RawPublicKey");
            return false;
        }

        m_clientHelloMessage = rawMessage_(0x01, messages[0].body);
        m_clientRandomSessionId = clientHello.legacySessionId;
        m_clientX25519Public = clientHello.clientX25519Public;

        if (!SwQuicTransportParameters::decode(clientHello.transportParameters,
                                               m_peerParams, error)) {
            return false;
        }
        m_hasPeerParams = true;
        // RFC 9000 7.3: the client's initial_source_connection_id is mandatory
        // and MUST match the SCID of the Initial that carried this ClientHello;
        // absence or mismatch is a TRANSPORT_PARAMETER_ERROR.
        if (!m_peerParams.hasInitialSourceConnectionId) {
            setError_(error, "Client omitted initial_source_connection_id (RFC 9000 7.3)");
            return false;
        }
        if (!(m_peerParams.initialSourceConnectionId == m_clientConnectionId.bytes())) {
            setError_(error, "Client initial_source_connection_id mismatch (RFC 9000 7.3)");
            return false;
        }

        // 0-RTT resumption: if the client offered a known ticket with a valid
        // binder and early_data, accept the PSK and derive the early keys so
        // the coalesced 0-RTT packets can be decrypted (RFC 8446 4.2.11).
        // Client-authenticated resumptions need tickets bound to the client
        // identity. Until that binding is represented, require a full mTLS
        // handshake and ignore offered PSKs fail-closed.
        if (!m_requireClientAuth && m_ticketStore && clientHello.hasPreSharedKey) {
            const SwQuicTicketStore::Entry entry =
                m_ticketStore->lookup(clientHello.pskIdentity);
            if (entry.found) {
                SwByteArray expectedBinder;
                SecretGuard_ expectedBinderGuard(expectedBinder);
                if (SwQuicClientHelloBuilder::computeExpectedBinder(
                        m_clientHelloMessage, entry.resumptionPsk, expectedBinder, nullptr,
                        clientHello.pskBindersTotalLength) &&
                    expectedBinder == clientHello.pskBinder) {
                    m_resuming = true;
                    m_resumptionPsk = entry.resumptionPsk;
                    m_acceptEarlyData = clientHello.offersEarlyData && entry.maxEarlyDataSize > 0;
                    // The 0-RTT volume is bounded by our advertised
                    // initial_max_data (RFC 9001 4.6.1), not the ticket's
                    // early_data value (which is only a 0-RTT-enabled flag).
                    m_maxEarlyDataSize = m_localParams.initialMaxData;
                    m_ticketStore->consume(clientHello.pskIdentity); // single-use: anti-replay
                    if (m_acceptEarlyData) {
                        SwQuicClientHelloBuilder::deriveServerEarlyKeys(
                            m_clientHelloMessage, m_resumptionPsk, m_earlyKeys, nullptr);
                        m_hasEarlyKeys = true;
                    }
                }
            }
        }

        // RFC 8446 4.4.2.2: when authenticating with a certificate (i.e. not a
        // PSK resumption, which sends no CertificateVerify), the server MUST sign
        // only with a scheme the client advertised in signature_algorithms. Its
        // absence is missing_extension; a credential the client will not accept
        // is handshake_failure.
        if (!m_resuming) {
            if (clientHello.signatureSchemes.empty()) {
                setError_(error,
                          "ClientHello missing signature_algorithms (RFC 8446 4.4.2.2)");
                return false;
            }
            bool schemeOffered = false;
            for (std::size_t i = 0; i < clientHello.signatureSchemes.size(); ++i) {
                if (clientHello.signatureSchemes[i] == m_credential.signatureScheme) {
                    schemeOffered = true;
                    break;
                }
            }
            if (!schemeOffered) {
                setError_(error,
                          "Client does not accept the server signature scheme (handshake_failure)");
                return false;
            }
        }

        clearError_(error);
        return true;
    }

    static bool offersProtocol_(const SwVector<SwByteArray>& protocols,
                                const SwByteArray& target) {
        for (std::size_t i = 0; i < protocols.size(); ++i) {
            if (protocols[i] == target) {
                return true;
            }
        }
        return false;
    }

    // ---------------------------------------------------- server flight ---

    bool emitServerFlight_(SwVector<SwByteArray>& outDatagrams, SwString* error) {
        // Server ephemeral key + ECDHE.
        SwByteArray serverPrivate;
        SecretGuard_ serverPrivateGuard(serverPrivate);
        if (!SwQuicRandom::fill(serverPrivate, 32, error) ||
            !SwQuicRandom::fill(m_serverRandom, 32, error)) {
            return false;
        }
        SwByteArray serverPublic;
        if (!SwQuicX25519::derivePublicKey(serverPrivate, serverPublic, error)) {
            return false;
        }
        SwByteArray ecdhe;
        SecretGuard_ ecdheGuard(ecdhe);
        if (!SwQuicX25519::computeSharedSecret(serverPrivate, m_clientX25519Public, ecdhe, error)) {
            return false;
        }
        serverPrivate.secureClear();

        // The server chooses its own connection ID (the client's future DCID).
        SwByteArray scidBytes;
        if (!SwQuicRandom::fill(scidBytes, 8, error) ||
            !SwQuicConnectionId::fromBytes(scidBytes, m_serverConnectionId, error)) {
            return false;
        }

        // ServerHello.
        SwByteArray serverHelloBody;
        buildServerHelloBody_(serverPublic, serverHelloBody);
        m_serverHelloMessage = rawMessage_(0x02, serverHelloBody);

        // Handshake secrets from transcript ClientHello || ServerHello.
        SwByteArray transcriptChSh;
        transcriptChSh.append(m_clientHelloMessage);
        transcriptChSh.append(m_serverHelloMessage);
        const SwByteArray thChSh = SwTls13KeySchedule::transcriptHash(transcriptChSh);

        // When resuming, seed the Early-Secret with the resumption PSK so the
        // whole schedule matches the client's (RFC 8446 7.1).
        SwByteArray earlySecret;
        SecretGuard_ earlySecretGuard(earlySecret);
        const bool earlyOk = m_resuming
            ? SwTls13KeySchedule::earlySecretWithPsk(m_resumptionPsk, earlySecret, error)
            : SwTls13KeySchedule::earlySecret(earlySecret, error);
        if (!earlyOk ||
            !SwTls13KeySchedule::handshakeSecret(earlySecret, ecdhe, m_handshakeSecret, error) ||
            !SwTls13KeySchedule::clientHandshakeTrafficSecret(m_handshakeSecret, thChSh,
                                                              m_clientHandshakeTrafficSecret, error) ||
            !SwTls13KeySchedule::serverHandshakeTrafficSecret(m_handshakeSecret, thChSh,
                                                              m_serverHandshakeTrafficSecret, error) ||
            !SwQuicPacketKeys::deriveAes128(m_clientHandshakeTrafficSecret, m_clientHandshakeKeys, error) ||
            !SwQuicPacketKeys::deriveAes128(m_serverHandshakeTrafficSecret, m_serverHandshakeKeys, error)) {
            return false;
        }

        // EncryptedExtensions (ALPN h3 + server transport parameters).
        SwByteArray encryptedExtensionsBody;
        if (!buildEncryptedExtensionsBody_(encryptedExtensionsBody, error)) {
            return false;
        }
        const SwByteArray encryptedExtensions = rawMessage_(0x08, encryptedExtensionsBody);
        m_negotiatedAlpn = m_applicationProtocol;

        // On PSK resumption the server authenticates via the PSK and MUST NOT
        // send Certificate or CertificateVerify (RFC 8446 2.2 / 4.4.2). The
        // Finished then covers ClientHello..EncryptedExtensions only.
        SwByteArray certificateRequest;
        SwByteArray certificate;
        SwByteArray certificateVerify;
        SwByteArray transcriptToCertVerify;
        transcriptToCertVerify.append(m_clientHelloMessage);
        transcriptToCertVerify.append(m_serverHelloMessage);
        transcriptToCertVerify.append(encryptedExtensions);

        if (!m_resuming) {
            if (m_requireClientAuth) {
                SwByteArray certificateRequestBody;
                buildCertificateRequestBody_(certificateRequestBody);
                certificateRequest = rawMessage_(0x0d, certificateRequestBody);
                transcriptToCertVerify.append(certificateRequest);
            }
            SwByteArray certificateBody;
            buildCertificateBody_(certificateBody);
            certificate = rawMessage_(0x0b, certificateBody);

            // CertificateVerify signs the transcript up to and incl. Certificate.
            SwByteArray transcriptToCert = transcriptToCertVerify;
            transcriptToCert.append(certificate);
            const SwByteArray thToCert = SwTls13KeySchedule::transcriptHash(transcriptToCert);

            SwByteArray certVerifyBody;
            if (!buildCertificateVerifyBody_(thToCert, certVerifyBody, error)) {
                return false;
            }
            certificateVerify = rawMessage_(0x0f, certVerifyBody);

            transcriptToCertVerify.append(certificate);
            transcriptToCertVerify.append(certificateVerify);
        }

        // Server Finished over the transcript up to (not incl.) the Finished.
        const SwByteArray thToCertVerify =
            SwTls13KeySchedule::transcriptHash(transcriptToCertVerify);

        SwByteArray serverFinishedKey;
        SecretGuard_ serverFinishedKeyGuard(serverFinishedKey);
        SwByteArray serverVerifyData;
        if (!SwTls13KeySchedule::finishedKey(m_serverHandshakeTrafficSecret, serverFinishedKey, error) ||
            !SwTls13KeySchedule::verifyData(serverFinishedKey, thToCertVerify, serverVerifyData, error)) {
            return false;
        }
        const SwByteArray serverFinished = rawMessage_(0x14, serverVerifyData);

        // 1-RTT keys over transcript up to and including server Finished.
        SwByteArray transcriptToServerFinished = transcriptToCertVerify;
        transcriptToServerFinished.append(serverFinished);
        const SwByteArray thToServerFinished =
            SwTls13KeySchedule::transcriptHash(transcriptToServerFinished);
        // Kept for verifying the client Finished and issuing session tickets.
        m_transcriptHashServerFinished = thToServerFinished;
        m_transcriptToServerFinished = transcriptToServerFinished;

        SwByteArray masterSecret;
        SecretGuard_ masterSecretGuard(masterSecret);
        if (!SwTls13KeySchedule::masterSecret(m_handshakeSecret, masterSecret, error) ||
            !SwTls13KeySchedule::exporterMasterSecret(masterSecret, thToServerFinished,
                                                      m_exporterMasterSecret, error) ||
            !SwTls13KeySchedule::clientApplicationTrafficSecret(masterSecret, thToServerFinished,
                                                               m_clientApplicationTrafficSecret, error) ||
            !SwTls13KeySchedule::serverApplicationTrafficSecret(masterSecret, thToServerFinished,
                                                               m_serverApplicationTrafficSecret, error) ||
            !SwQuicPacketKeys::deriveAes128(m_clientApplicationTrafficSecret, m_clientApplicationKeys, error) ||
            !SwQuicPacketKeys::deriveAes128(m_serverApplicationTrafficSecret, m_serverApplicationKeys, error)) {
            return false;
        }
        m_masterSecret = masterSecret; // for the resumption_master_secret

        // Build the datagram: Initial { ACK + CRYPTO(ServerHello) } coalesced
        // with Handshake { CRYPTO(EE || Cert || CertVerify || Finished) }.
        // The Handshake packet is built first so the Initial can be padded with
        // PADDING frames to bring the ack-eliciting datagram to 1200 bytes
        // (RFC 9000 14.1).
        SwByteArray handshakeCrypto;
        handshakeCrypto.append(encryptedExtensions);
        if (!m_resuming) {
            if (!certificateRequest.isEmpty()) handshakeCrypto.append(certificateRequest);
            handshakeCrypto.append(certificate);       // omitted on PSK resumption
            handshakeCrypto.append(certificateVerify); // omitted on PSK resumption
        }
        handshakeCrypto.append(serverFinished);
        SwVector<SwByteArray> handshakePackets;
        if (!buildServerHandshakePackets_(handshakeCrypto, handshakePackets, error)) {
            return false;
        }

        // The Initial coalesces with the first Handshake packet; pad it so that
        // first ack-eliciting datagram reaches 1200 bytes (RFC 9000 14.1).
        const std::size_t firstHsSize =
            handshakePackets.empty() ? 0 : handshakePackets[0].size();
        std::size_t minInitialPlaintext = 0;
        if (firstHsSize < 1200) {
            minInitialPlaintext = 1200 - firstHsSize;
        }
        SwByteArray initialPacket;
        if (!buildServerInitialPacket_(m_serverHelloMessage, minInitialPlaintext,
                                       initialPacket, error)) {
            return false;
        }

        // Datagram 1: Initial + first Handshake packet. Further Handshake
        // packets go in their own datagrams (each already < 1200 bytes).
        SwVector<SwByteArray> datagrams;
        SwByteArray first = initialPacket;
        if (!handshakePackets.empty()) {
            first.append(handshakePackets[0]);
        }
        datagrams.push_back(first);
        for (std::size_t i = 1; i < handshakePackets.size(); ++i) {
            datagrams.push_back(handshakePackets[i]);
        }

        // Anti-amplification: the whole flight must not exceed 3x the bytes
        // received from the still-unvalidated client address (RFC 9000 8.1).
        std::uint64_t flightBytes = 0;
        for (std::size_t i = 0; i < datagrams.size(); ++i) {
            flightBytes += static_cast<std::uint64_t>(datagrams[i].size());
        }
        if (m_bytesSentToClient + flightBytes > 3 * m_bytesReceivedFromClient) {
            setError_(error, "QUIC server flight would exceed the 3x anti-amplification limit");
            return false;
        }
        m_bytesSentToClient += flightBytes;
        for (std::size_t i = 0; i < datagrams.size(); ++i) {
            outDatagrams.push_back(datagrams[i]);
        }

        clearError_(error);
        return true;
    }

    void buildServerHelloBody_(const SwByteArray& serverPublic, SwByteArray& outBody) {
        outBody.clear();
        appendU16_(outBody, 0x0303);
        outBody.append(m_serverRandom);
        outBody.append(static_cast<char>(m_clientRandomSessionId.size()));
        outBody.append(m_clientRandomSessionId);
        appendU16_(outBody, 0x1301);          // cipher suite
        outBody.append(static_cast<char>(0)); // legacy_compression_method

        SwByteArray extensions;
        // supported_versions: selected 0x0304.
        SwByteArray selectedVersion;
        appendU16_(selectedVersion, 0x0304);
        appendExtension_(extensions, 0x002b, selectedVersion);
        // key_share: server KeyShareEntry x25519.
        SwByteArray keyShare;
        appendU16_(keyShare, 0x001d);
        appendU16_(keyShare, static_cast<std::uint16_t>(serverPublic.size()));
        keyShare.append(serverPublic);
        appendExtension_(extensions, 0x0033, keyShare);
        // pre_shared_key: selected_identity 0 (we only offered one PSK).
        if (m_resuming) {
            SwByteArray selectedIdentity;
            appendU16_(selectedIdentity, 0);
            appendExtension_(extensions, 0x0029, selectedIdentity);
        }

        appendU16_(outBody, static_cast<std::uint16_t>(extensions.size()));
        outBody.append(extensions);
    }

    bool buildEncryptedExtensionsBody_(SwByteArray& outBody, SwString* error) {
        SwByteArray extensions;

        // ALPN: the exact single protocol selected for this listener.
        SwByteArray alpn;
        SwByteArray protocolList;
        protocolList.append(static_cast<char>(m_applicationProtocol.size()));
        protocolList.append(m_applicationProtocol);
        appendU16_(alpn, static_cast<std::uint16_t>(protocolList.size()));
        alpn.append(protocolList);
        appendExtension_(extensions, 0x0010, alpn);

        if (m_requireRawPublicKeys) {
            // TLS 1.3 returns the RFC 7250 selections in EncryptedExtensions.
            // Both directions are fixed to RawPublicKey(2), with no X.509
            // alternative offered or selected.
            SwByteArray selectedRawPublicKey;
            selectedRawPublicKey.append(static_cast<char>(2));
            appendExtension_(extensions, 0x0013, selectedRawPublicKey);
            appendExtension_(extensions, 0x0014, selectedRawPublicKey);
        }

        // early_data (empty) accepts the client's 0-RTT (RFC 9001 4.6).
        if (m_acceptEarlyData) {
            appendExtension_(extensions, 0x002a, SwByteArray());
        }

        // QUIC transport parameters, including the RFC 9001 8.2 server-only
        // original_destination_connection_id and initial_source_connection_id.
        SwQuicTransportParameters params = m_localParams;
        params.originalDestinationConnectionId = m_originalDestinationConnectionId.bytes();
        params.hasOriginalDestinationConnectionId = true;
        params.initialSourceConnectionId = m_serverConnectionId.bytes();
        params.hasInitialSourceConnectionId = true;

        SwByteArray transportParameters;
        if (!params.encode(transportParameters, error)) {
            return false;
        }
        appendExtension_(extensions, 0x0039, transportParameters);

        outBody.clear();
        appendU16_(outBody, static_cast<std::uint16_t>(extensions.size()));
        outBody.append(extensions);
        clearError_(error);
        return true;
    }

    void buildCertificateRequestBody_(SwByteArray& outBody) const {
        outBody.clear();
        outBody.append(static_cast<char>(0)); // certificate_request_context

        SwByteArray signatureSchemes;
        if (m_requireRawPublicKeys) {
            appendU16_(signatureSchemes, 2);
            appendU16_(signatureSchemes, 0x0807); // ed25519
        } else {
            appendU16_(signatureSchemes, 6);
            appendU16_(signatureSchemes, 0x0403); // ecdsa_secp256r1_sha256
            appendU16_(signatureSchemes, 0x0804); // rsa_pss_rsae_sha256
            appendU16_(signatureSchemes, 0x0805); // rsa_pss_rsae_sha384
        }
        SwByteArray extensions;
        appendExtension_(extensions, 0x000d, signatureSchemes);
        appendU16_(outBody, static_cast<std::uint16_t>(extensions.size()));
        outBody.append(extensions);
    }

    void buildCertificateBody_(SwByteArray& outBody) {
        outBody.clear();
        outBody.append(static_cast<char>(0)); // certificate_request_context (empty)

        SwByteArray certList;
        for (std::size_t i = 0; i < m_credential.certificateChain.size(); ++i) {
            const SwByteArray& cert = m_credential.certificateChain[i];
            appendU24_(certList, static_cast<std::uint32_t>(cert.size()));
            certList.append(cert);
            appendU16_(certList, 0); // per-entry extensions (empty)
        }
        appendU24_(outBody, static_cast<std::uint32_t>(certList.size()));
        outBody.append(certList);
    }

    bool buildCertificateVerifyBody_(const SwByteArray& transcriptHashToCert,
                                     SwByteArray& outBody,
                                     SwString* error) {
        SwByteArray content;
        for (int i = 0; i < 64; ++i) {
            content.append(static_cast<char>(0x20));
        }
        content.append("TLS 1.3, server CertificateVerify");
        content.append(static_cast<char>(0));
        content.append(transcriptHashToCert);

        SwByteArray signature;
        if (!m_credential.sign(content, signature, error)) {
            return false;
        }

        outBody.clear();
        appendU16_(outBody, m_credential.signatureScheme);
        appendU16_(outBody, static_cast<std::uint16_t>(signature.size()));
        outBody.append(signature);
        clearError_(error);
        return true;
    }

    bool buildServerInitialPacket_(const SwByteArray& serverHelloMessage,
                                   std::size_t minPlaintextSize,
                                   SwByteArray& outPacket,
                                   SwString* error) {
        SwVector<SwQuicFrame> frames;
        if (m_pendingClientInitialAck && !m_clientInitialPns.empty()) {
            frames.push_back(buildAckFrame_(m_clientInitialPns));
            m_pendingClientInitialAck = false;
        }
        frames.push_back(SwQuicFrame::crypto(0, serverHelloMessage));

        SwByteArray plaintext;
        if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) {
            return false;
        }

        // PADDING frames (a run of 0x00 bytes) to expand the coalesced
        // datagram to 1200 bytes (RFC 9000 14.1).
        while (plaintext.size() < minPlaintextSize) {
            plaintext.append(static_cast<char>(0));
        }

        const std::uint8_t pnLen = 4;
        const std::uint64_t protectedLength =
            static_cast<std::uint64_t>(plaintext.size() + SwQuicPacketProtector::kTagLength);
        SwByteArray headerBytes;
        buildLongHeader_(0xc0, protectedLength, pnLen, m_serverInitialPacketNumber, true, headerBytes);

        if (!SwQuicPacketProtector::protectLongHeader(m_serverInitialKeys,
                                                      m_serverInitialPacketNumber, pnLen,
                                                      headerBytes, plaintext, outPacket, error)) {
            return false;
        }
        ++m_serverInitialPacketNumber;
        clearError_(error);
        return true;
    }

    // Fragment the Handshake CRYPTO stream across as many Handshake packets as
    // needed, each with an increasing CRYPTO offset, so a real (multi-KB)
    // certificate chain stays under the path MTU (RFC 9000 19.6). The peer
    // reassembles out-of-order/multi-packet CRYPTO via SwQuicStream.
    bool buildServerHandshakePackets_(const SwByteArray& handshakeCrypto,
                                      SwVector<SwByteArray>& outPackets,
                                      SwString* error) {
        outPackets.clear();
        const std::size_t chunkBytes = 1000; // leaves room for header + tag < 1200
        std::uint64_t offset = 0;
        const std::size_t total = static_cast<std::size_t>(handshakeCrypto.size());

        do {
            const std::size_t remaining = total - static_cast<std::size_t>(offset);
            const std::size_t take = remaining < chunkBytes ? remaining : chunkBytes;
            const SwByteArray chunk =
                handshakeCrypto.mid(static_cast<int>(offset), static_cast<int>(take));

            SwVector<SwQuicFrame> frames;
            frames.push_back(SwQuicFrame::crypto(offset, chunk));
            SwByteArray plaintext;
            if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) {
                return false;
            }
            while (plaintext.size() < 4) {
                plaintext.append(static_cast<char>(0)); // header-protection sample room
            }

            const std::uint8_t pnLen = 4;
            SwByteArray headerBytes;
            buildLongHeader_(0xe0,
                             static_cast<std::uint64_t>(plaintext.size() +
                                                        SwQuicPacketProtector::kTagLength),
                             pnLen, m_serverHandshakePacketNumber, false, headerBytes);
            SwByteArray packet;
            if (!SwQuicPacketProtector::protectLongHeader(m_serverHandshakeKeys,
                                                          m_serverHandshakePacketNumber, pnLen,
                                                          headerBytes, plaintext, packet, error)) {
                return false;
            }
            ++m_serverHandshakePacketNumber;
            outPackets.push_back(packet);
            offset += take;
        } while (offset < total);

        clearError_(error);
        return true;
    }

    void buildLongHeader_(std::uint8_t firstByteBase,
                          std::uint64_t protectedPayloadLength,
                          std::uint8_t pnLen,
                          std::uint64_t packetNumber,
                          bool isInitial,
                          SwByteArray& outHeader) {
        outHeader.clear();
        outHeader.append(static_cast<char>(firstByteBase | (pnLen - 1U)));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(1)); // version 1
        outHeader.append(static_cast<char>(m_clientConnectionId.size()));
        outHeader.append(m_clientConnectionId.bytes());
        outHeader.append(static_cast<char>(m_serverConnectionId.size()));
        outHeader.append(m_serverConnectionId.bytes());
        SwString ignored;
        if (isInitial) {
            SwQuicVarIntCodec::encode(0, outHeader, &ignored); // token length 0
        }
        SwQuicVarIntCodec::encode(protectedPayloadLength + pnLen, outHeader, &ignored);
        for (std::uint8_t i = 0; i < pnLen; ++i) {
            const std::uint8_t shift = static_cast<std::uint8_t>((pnLen - 1 - i) * 8);
            outHeader.append(static_cast<char>((packetNumber >> shift) & 0xffU));
        }
    }

    // ------------------------------------------------ client Handshake ---

    bool handleClientHandshake_(const SwByteArray& packet,
                                std::size_t& consumed,
                                SwString* error) {
        if (m_serverHandshakeTrafficSecret.isEmpty()) {
            setError_(error, "Client Handshake packet received before handshake keys exist");
            return false;
        }

        SwQuicPacketHeader header;
        SwByteArray payload;
        const std::uint64_t* largest =
            m_haveClientHandshakeLargest ? &m_clientHandshakeLargest : nullptr;
        if (!SwQuicPacketProtector::unprotectHandshake(m_clientHandshakeKeys, packet, header, payload,
                                                       &consumed, error, largest)) {
            return false;
        }
        recordClientHandshakePn_(header.packetNumber());

        SwVector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(payload, frames, error)) {
            return false;
        }
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::Crypto) {
                if (!m_clientHandshakeCryptoReassembly.receive(frames[i].offset(),
                                                              frames[i].data(), false, error)) {
                    return false;
                }
                const SwByteArray contiguous = m_clientHandshakeCryptoReassembly.readContiguous();
                if (!contiguous.isEmpty()) {
                    m_clientHandshakeCrypto.append(contiguous);
                }
            }
        }

        return tryVerifyClientFinished_(error);
    }

    bool tryVerifyClientFinished_(SwString* error) {
        SwVector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(m_clientHandshakeCrypto, messages, error)) {
            clearError_(error);
            return true; // wait for more
        }

        int certificateIndex = -1;
        int certificateVerifyIndex = -1;
        int finishedIndex = -1;
        SwVector<SwByteArray> clientCertificateChain;
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].type == 0x14) {
                finishedIndex = static_cast<int>(i);
                break;
            } else if (messages[i].type == 0x0b) {
                certificateIndex = static_cast<int>(i);
                if (messages[i].body.isEmpty() || !messages[i].body.constData() ||
                    static_cast<std::uint8_t>(messages[i].body.constData()[0]) != 0) {
                    setError_(error, "Client Certificate request_context mismatch");
                    return false;
                }
                if (!SwTls13Messages::extractCertificateChain(
                        messages[i].body, clientCertificateChain, error)) {
                    return false;
                }
            } else if (messages[i].type == 0x0f) {
                certificateVerifyIndex = static_cast<int>(i);
            } else {
                setError_(error, "Unexpected TLS message in the client Handshake flight");
                return false;
            }
        }
        if (finishedIndex < 0) {
            clearError_(error);
            return true;
        }
        if (static_cast<std::size_t>(finishedIndex + 1) != messages.size()) {
            setError_(error, "TLS messages follow the client Finished");
            return false;
        }

        SwByteArray transcriptBeforeFinished = m_transcriptToServerFinished;
        SwByteArray transcriptHashForClientCertificateVerify;
        for (int i = 0; i < finishedIndex; ++i) {
            if (i == certificateVerifyIndex) {
                transcriptHashForClientCertificateVerify =
                    SwTls13KeySchedule::transcriptHash(transcriptBeforeFinished);
            }
            transcriptBeforeFinished.append(rawMessage_(messages[i].type, messages[i].body));
        }

        SwByteArray verifiedClientSpki;
        if (m_requireClientAuth) {
            if (certificateIndex < 0 || certificateVerifyIndex < 0 ||
                clientCertificateChain.empty() || certificateIndex != 0 ||
                certificateVerifyIndex != 1 || finishedIndex != 2) {
                setError_(error, "Client certificate authentication is required");
                return false;
            }
            const SwByteArray& leaf = clientCertificateChain.front();
            if (m_requireRawPublicKeys) {
                SwByteArray rawNodeId;
                if (clientCertificateChain.size() != 1 ||
                    !SwQuicCertificateVerifier::extractEd25519RawPublicKey(
                        leaf, rawNodeId, error) || rawNodeId.size() != 32) {
                    return false;
                }
                verifiedClientSpki = leaf;
            } else if (!SwQuicCertificateVerifier::extractSubjectPublicKeyInfo(
                           leaf, verifiedClientSpki, error)) {
                return false;
            }
            std::uint16_t signatureScheme = 0;
            SwByteArray signature;
            if (!SwTls13Messages::parseCertificateVerify(
                    messages[certificateVerifyIndex].body,
                    signatureScheme, signature, error)) {
                return false;
            }
            if (m_requireRawPublicKeys && signatureScheme != 0x0807) {
                setError_(error, "Client RPK CertificateVerify is not ed25519");
                return false;
            }
            if (!m_requireRawPublicKeys && signatureScheme != 0x0403 &&
                signatureScheme != 0x0804 && signatureScheme != 0x0805) {
                setError_(error, "Client CertificateVerify uses an unrequested signature scheme");
                return false;
            }
            const bool signatureValid = m_requireRawPublicKeys
                ? SwQuicCertificateVerifier::verifyRawPublicKeyCertificateVerify(
                      leaf, signatureScheme, signature,
                      transcriptHashForClientCertificateVerify, error, false)
                : SwQuicCertificateVerifier::verifyCertificateVerify(
                      leaf, signatureScheme, signature,
                      transcriptHashForClientCertificateVerify, error, false);
            if (!signatureValid) {
                return false;
            }
        } else if (certificateIndex >= 0 || certificateVerifyIndex >= 0) {
            setError_(error, "Client sent unsolicited certificate authentication messages");
            return false;
        }

        SwByteArray clientVerifyData;
        if (!SwTls13Messages::parseFinishedVerifyData(messages[finishedIndex].body,
                                                      clientVerifyData, error)) {
            return false;
        }

        SwByteArray clientFinishedKey;
        SecretGuard_ clientFinishedKeyGuard(clientFinishedKey);
        SwByteArray expected;
        if (!SwTls13KeySchedule::finishedKey(m_clientHandshakeTrafficSecret, clientFinishedKey, error) ||
            !SwTls13KeySchedule::verifyData(
                clientFinishedKey,
                SwTls13KeySchedule::transcriptHash(transcriptBeforeFinished),
                                            expected, error)) {
            return false;
        }
        if (!(clientVerifyData == expected)) {
            setError_(error, "Client Finished verify_data mismatch");
            return false;
        }

        // Invoke external identity policy only after CertificateVerify and
        // Finished authenticated the whole client flight.
        if (m_requireClientAuth) {
            try {
                if (!m_clientSpkiVerifier(verifiedClientSpki)) {
                    setError_(error, "Client SPKI rejected by server policy");
                    return false;
                }
            } catch (...) {
                setError_(error, "Client SPKI verifier raised an exception");
                return false;
            }
        }

        // resumption_master_secret over the transcript through the client
        // Finished (RFC 8446 7.1), for issuing session tickets.
        SwByteArray transcriptToClientFinished = transcriptBeforeFinished;
        transcriptToClientFinished.append(rawMessage_(0x14, clientVerifyData));
        const SwByteArray thToClientFinished =
            SwTls13KeySchedule::transcriptHash(transcriptToClientFinished);
        if (!SwTls13KeySchedule::resumptionMasterSecret(m_masterSecret, thToClientFinished,
                                                        m_resumptionMasterSecret, error)) {
            return false;
        }

        m_handshakeComplete = true;
        if (m_requireClientAuth) {
            m_authenticatedClientSpkiDer = verifiedClientSpki;
        }
        clearError_(error);
        return true;
    }

public:
    const SwByteArray& resumptionMasterSecret() const { return m_resumptionMasterSecret; }

    // Issue a NewSessionTicket: derive the PSK from resumption_master_secret and
    // the nonce, store (ticket -> PSK, early-data limit, transport params) in
    // the ticket store for later 0-RTT, and return the NST message body to send
    // to the client in 1-RTT (RFC 8446 4.6.1).
    bool issueNewSessionTicket(SwQuicTicketStore& store,
                               const SwByteArray& ticket,
                               const SwByteArray& ticketNonce,
                               std::uint32_t ticketLifetimeS,
                               std::uint32_t ticketAgeAdd,
                               std::uint32_t maxEarlyDataSize,
                               SwByteArray& outNewSessionTicketBody,
                               SwString* error = nullptr) {
        SwByteArray psk;
        SecretGuard_ pskGuard(psk);
        if (!SwTls13KeySchedule::resumptionPsk(m_resumptionMasterSecret, ticketNonce, psk, error)) {
            return false;
        }
        SwByteArray serverParams;
        SwQuicTransportParameters params = m_localParams;
        params.hasInitialSourceConnectionId = true;
        params.initialSourceConnectionId = m_serverConnectionId.bytes();
        if (!params.encode(serverParams, error)) {
            return false;
        }
        store.store(ticket, psk, maxEarlyDataSize, serverParams);
        SwTls13Messages::buildNewSessionTicketBody(ticketLifetimeS, ticketAgeAdd, ticketNonce,
                                                   ticket, maxEarlyDataSize,
                                                   outNewSessionTicketBody);
        clearError_(error);
        return true;
    }

private:

    // Handshake-space ACK of the client Finished + a 1-RTT HANDSHAKE_DONE.
    bool emitHandshakeConfirmation_(SwVector<SwByteArray>& outDatagrams, SwString* error) {
        if (m_confirmationSent) {
            clearError_(error);
            return true;
        }

        SwByteArray datagram;

        // Acknowledge the client's Handshake packet(s) (RFC 9000 13.2.1) so the
        // client stops its PTO retransmissions.
        if (!m_clientHandshakePns.empty()) {
            SwVector<SwQuicFrame> ackFrames;
            ackFrames.push_back(buildAckFrame_(m_clientHandshakePns));
            SwByteArray ackPlaintext;
            if (!SwQuicFrameCodec::encodeFrames(ackFrames, ackPlaintext, error)) {
                return false;
            }
            const std::uint8_t pnLen = 4;
            SwByteArray header;
            buildLongHeader_(0xe0,
                             static_cast<std::uint64_t>(ackPlaintext.size() +
                                                        SwQuicPacketProtector::kTagLength),
                             pnLen, m_serverHandshakePacketNumber, false, header);
            SwByteArray ackPacket;
            if (!SwQuicPacketProtector::protectLongHeader(m_serverHandshakeKeys,
                                                          m_serverHandshakePacketNumber, pnLen,
                                                          header, ackPlaintext, ackPacket, error)) {
                return false;
            }
            ++m_serverHandshakePacketNumber;
            datagram.append(ackPacket);
        }

        // HANDSHAKE_DONE in a 1-RTT short-header packet (RFC 9000 19.20).
        SwVector<SwQuicFrame> appFrames;
        appFrames.push_back(SwQuicFrame::handshakeDone());
        SwByteArray appPlaintext;
        if (!SwQuicFrameCodec::encodeFrames(appFrames, appPlaintext, error)) {
            return false;
        }
        while (appPlaintext.size() < 4) {
            appPlaintext.append(static_cast<char>(0)); // room for the HP sample
        }
        SwByteArray appPacket;
        if (!SwQuicPacketProtector::protectShortHeader1Rtt(m_serverApplicationKeys,
                                                           m_clientConnectionId,
                                                           m_serverApplicationPacketNumber,
                                                           4, false, false,
                                                           appPlaintext, appPacket, error)) {
            return false;
        }
        ++m_serverApplicationPacketNumber;
        datagram.append(appPacket);

        outDatagrams.push_back(datagram);
        m_confirmationSent = true;
        clearError_(error);
        return true;
    }

    // ---------------------------------------------------------- helpers ---

    static void appendExtension_(SwByteArray& extensions,
                                 std::uint16_t type,
                                 const SwByteArray& data) {
        appendU16_(extensions, type);
        appendU16_(extensions, static_cast<std::uint16_t>(data.size()));
        extensions.append(data);
    }

    static bool peekInitialDcid_(const SwByteArray& packet,
                                 SwQuicConnectionId& outDcid,
                                 SwString* error) {
        if (packet.size() < 6) {
            setError_(error, "QUIC Initial packet is too short for its DCID");
            return false;
        }
        const std::size_t dcidLength = static_cast<std::uint8_t>(packet.constData()[5]);
        if (packet.size() < 6 + dcidLength) {
            setError_(error, "QUIC Initial DCID is truncated");
            return false;
        }
        const SwByteArray dcidBytes = packet.mid(6, static_cast<int>(dcidLength));
        return SwQuicConnectionId::fromBytes(dcidBytes, outDcid, error);
    }

    SwQuicFrame buildAckFrame_(const SwMap<std::uint64_t, bool>& received) {
        SwMap<std::uint64_t, bool>::const_iterator largestIt = received.end();
        --largestIt;
        const std::uint64_t largest = largestIt.key();
        std::uint64_t low = largest;
        while (low > 0 && received.contains(low - 1)) {
            --low;
        }
        return SwQuicFrame::ack(largest, 0, largest - low);
    }

    void recordClientInitialPn_(std::uint64_t pn) {
        m_clientInitialPns.insert(pn, true);
        m_pendingClientInitialAck = true;
        if (!m_haveClientInitialLargest || pn > m_clientInitialLargest) {
            m_clientInitialLargest = pn;
            m_haveClientInitialLargest = true;
        }
    }

    void recordClientHandshakePn_(std::uint64_t pn) {
        m_clientHandshakePns.insert(pn, true);
        if (!m_haveClientHandshakeLargest || pn > m_clientHandshakeLargest) {
            m_clientHandshakeLargest = pn;
            m_haveClientHandshakeLargest = true;
        }
    }

    State m_state;
    SwString m_error;
    SwQuicServerCredential m_credential;
    bool m_requireClientAuth = false;
    bool m_requireRawPublicKeys = false;
    std::function<bool(const SwByteArray&)> m_clientSpkiVerifier;
    SwByteArray m_authenticatedClientSpkiDer;
    SwQuicTransportParameters m_localParams;
    SwQuicTransportParameters m_peerParams;
    bool m_hasPeerParams = false;
    SwByteArray m_applicationProtocol{SwByteArray("h3")};
    ApplicationProtocolSelector m_applicationProtocolSelector;

    SwQuicConnectionId m_originalDestinationConnectionId;
    SwQuicConnectionId m_clientConnectionId;
    SwQuicConnectionId m_serverConnectionId;

    SwQuicInitialKeys m_clientInitialKeys;
    SwQuicInitialKeys m_serverInitialKeys;
    SwQuicInitialKeys m_clientHandshakeKeys;
    SwQuicInitialKeys m_serverHandshakeKeys;
    SwQuicInitialKeys m_clientApplicationKeys;
    SwQuicInitialKeys m_serverApplicationKeys;

    SwByteArray m_serverRandom;
    SwByteArray m_clientRandomSessionId;
    SwByteArray m_clientX25519Public;

    SwByteArray m_clientInitialCrypto;
    SwQuicStream m_clientInitialCryptoReassembly;
    SwByteArray m_clientHandshakeCrypto;
    SwQuicStream m_clientHandshakeCryptoReassembly;

    SwByteArray m_clientHelloMessage;
    SwByteArray m_serverHelloMessage;
    SwByteArray m_handshakeSecret;
    SwByteArray m_clientHandshakeTrafficSecret;
    SwByteArray m_serverHandshakeTrafficSecret;
    SwByteArray m_clientApplicationTrafficSecret;
    SwByteArray m_serverApplicationTrafficSecret;
    SwByteArray m_exporterMasterSecret;
    SwByteArray m_transcriptHashServerFinished;
    SwByteArray m_negotiatedAlpn;

    std::uint64_t m_serverInitialPacketNumber;
    std::uint64_t m_serverHandshakePacketNumber;
    SwMap<std::uint64_t, bool> m_clientInitialPns;
    bool m_handshakeComplete;
    bool m_pendingClientInitialAck;
    std::uint64_t m_clientInitialLargest = 0;
    bool m_haveClientInitialLargest = false;
    std::uint64_t m_clientHandshakeLargest = 0;
    bool m_haveClientHandshakeLargest = false;
    SwMap<std::uint64_t, bool> m_clientHandshakePns;
    std::uint64_t m_serverApplicationPacketNumber = 0;
    bool m_confirmationSent = false;
    std::uint64_t m_bytesReceivedFromClient = 0;
    std::uint64_t m_bytesSentToClient = 0;
    SwByteArray m_masterSecret;
    SwByteArray m_transcriptToServerFinished;
    SwByteArray m_resumptionMasterSecret;

    // 0-RTT resumption state.
    SwQuicTicketStore* m_ticketStore = nullptr;
    bool m_resuming = false;
    bool m_acceptEarlyData = false;
    SwByteArray m_resumptionPsk;
    SwQuicInitialKeys m_earlyKeys;
    bool m_hasEarlyKeys = false;
    SwByteArray m_receivedEarlyData;
    SwVector<SwQuicFrame> m_earlyStreamFrames;
    std::uint64_t m_maxEarlyDataSize = 0;
    std::uint64_t m_earlyDataBytesReceived = 0;
    std::uint64_t m_clientEarlyLargest = 0;
    bool m_haveClientEarlyLargest = false;
    SwHash<std::uint64_t, bool> m_clientEarlyPns; // 0-RTT replay dedup

#if defined(SW_QUIC_ENABLE_SECRET_LIFETIME_TEST_HOOKS)
public:
    bool transientSecretsDiscardedForTest() const {
        return m_handshakeSecret.isEmpty() &&
               m_clientHandshakeTrafficSecret.isEmpty() &&
               m_serverHandshakeTrafficSecret.isEmpty() &&
               m_masterSecret.isEmpty() &&
               m_resumptionPsk.isEmpty() &&
               m_clientInitialKeys.secret.isEmpty() &&
               m_serverInitialKeys.secret.isEmpty() &&
               m_earlyKeys.secret.isEmpty();
    }
#endif
};

#endif
