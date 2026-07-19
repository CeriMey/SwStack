#ifndef SWQUICHANDSHAKECLIENT_H
#define SWQUICHANDSHAKECLIENT_H

#include "SwMap.h"
#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicCertificateVerifier.h"
#include "quic/SwQuicClientHelloBuilder.h"
#include "quic/SwQuicClientInitialBuilder.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicFrameCodec.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicHybridKex.h"
#include "quic/SwQuicLimits.h"
#include "quic/SwQuicMlKem768.h"
#include "quic/SwQuicPacketCodec.h"
#include "quic/SwQuicPacketHeader.h"
#include "quic/SwQuicPacketKeys.h"
#include "quic/SwQuicPacketProtector.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwQuicRetry.h"
#include "quic/SwQuicSessionTicket.h"
#include "quic/SwQuicServerCredential.h"
#include "quic/SwQuicStream.h"
#include "quic/SwQuicTransportParameters.h"
#include "quic/SwQuicVarIntCodec.h"
#include "quic/SwQuicX25519.h"
#include "quic/SwTls13KeySchedule.h"

#include <algorithm>
#include <functional>
#include "quic/SwTls13Messages.h"

#include <cstdint>

// Client-side driver for the QUIC v1 + TLS 1.3 handshake (RFC 9000 / RFC 9001 /
// RFC 8446), cipher suite TLS_AES_128_GCM_SHA256, ALPN "h3". It is transport
// agnostic: start() returns the datagram to send, and processIncomingDatagram()
// consumes a received datagram and returns datagrams to send back. It drives the
// key schedule through Initial -> Handshake -> 1-RTT.
//
// Server authentication: the certificate chain is validated against the
// system trust store with hostname matching, and the CertificateVerify
// signature over the transcript is checked (SwQuicCertificateVerifier). The
// server Finished MAC is verified as well. setVerifyPeer(false) disables the
// PKI checks for loopback self-tests that use synthetic certificates.
// When CertificateRequest is received, a configured credential emits the TLS
// 1.3 client Certificate + CertificateVerify flight before client Finished.
class SwQuicHandshakeClient {
public:
    enum class State {
        Idle,
        WaitServerHello,
        WaitServerHandshake,
        Complete,
        Failed
    };

    SwQuicHandshakeClient()
        : m_state(State::Idle),
          m_verifyPeer(true),
          m_verifyChain(true),
          m_hasPeerTransportParameters(false),
          m_clientInitialPacketNumber(0),
          m_clientHandshakePacketNumber(0),
          m_handshakeComplete(false) {
        m_localTransportParameters.maxIdleTimeoutMs = 30000;
        m_localTransportParameters.maxUdpPayloadSize = SwQuicLimits::maximumUdpPayloadBytes();
        m_localTransportParameters.initialMaxData = 1048576;
        m_localTransportParameters.initialMaxStreamDataBidiLocal = 262144;
        m_localTransportParameters.initialMaxStreamDataBidiRemote = 262144;
        m_localTransportParameters.initialMaxStreamDataUni = 262144;
        m_localTransportParameters.initialMaxStreamsBidi = 100;
        m_localTransportParameters.initialMaxStreamsUni = 100;
        m_localTransportParameters.activeConnectionIdLimit = 4;
        m_localTransportParameters.maxDatagramFrameSize =
            SwQuicLimits::maximumDatagramFrameBytes();
        m_localTransportParameters.resetStreamAt = true;
    }

    State state() const { return m_state; }
    bool handshakeComplete() const { return m_handshakeComplete; }
    const SwString& errorString() const { return m_error; }
    const SwByteArray& serverCertificateDer() const { return m_serverCertificateDer; }
    const SwVector<SwByteArray>& serverCertificateChain() const {
        return m_serverCertificateChain;
    }
    // Empty until the server CertificateVerify and Finished have both been
    // authenticated. This avoids exposing an unauthenticated identity merely
    // because a Certificate message was received.
    const SwByteArray& authenticatedServerSubjectPublicKeyInfo() const {
        return m_authenticatedServerSpkiDer;
    }

    // Client credential used only when the server sends CertificateRequest.
    // SwQuicServerCredential is deliberately role-neutral at the wire level:
    // it is a DER chain plus a CertificateVerify signing callback.
    void setCredential(const SwQuicServerCredential& credential) { m_credential = credential; }
    bool hasCredential() const { return m_credential.isValid(); }

    // PKI verification toggle: keep it enabled against real servers; disable
    // it only for loopback tests with synthetic certificates.
    void setVerifyPeer(bool verify) { m_verifyPeer = verify; }
    bool verifyPeer() const { return m_verifyPeer; }

    // Chain-policy toggle, independent of the signature check. With a real but
    // self-signed peer (loopback server), set this false to still verify the
    // CertificateVerify signature while skipping the trusted-root requirement.
    void setVerifyCertificateChain(bool verify) { m_verifyChain = verify; }
    bool verifyCertificateChain() const { return m_verifyChain; }

    // Transitional X.509/SPKI delegated trust. The peer still sends an X.509
    // Certificate message; SwQuic strictly extracts and canonically re-encodes
    // its SubjectPublicKeyInfo, then gives ONLY that SPKI DER to the decision
    // callback instead of validating the X.509 chain. CertificateVerify still
    // proves possession. This is SPKI pinning, not RFC 7250 RawPublicKey wire
    // negotiation. A callback exception rejects the handshake.
    void setSubjectPublicKeyInfoVerifier(
            std::function<bool(const SwByteArray& spkiDer)> verifier) {
        m_subjectPublicKeyInfoVerifier = std::move(verifier);
    }
    bool hasSubjectPublicKeyInfoVerifier() const {
        return static_cast<bool>(m_subjectPublicKeyInfoVerifier);
    }

    // RFC 7250 is opt-in and fail-closed: both certificate_type extensions,
    // an Ed25519 RPK credential and the delegated SPKI verifier are required.
    void setRequireRawPublicKeys(bool required) { m_requireRawPublicKeys = required; }
    bool requiresRawPublicKeys() const { return m_requireRawPublicKeys; }
    void setRawPublicKeyVerifier(std::function<bool(const SwByteArray& spkiDer)> verifier) {
        m_requireRawPublicKeys = true;
        setSubjectPublicKeyInfoVerifier(std::move(verifier));
    }
    bool hasRawPublicKeyVerifier() const { return hasSubjectPublicKeyInfoVerifier(); }

    // Enable 0-RTT resumption: the next start() sends a resumption ClientHello
    // for this ticket and, if earlyData is non-empty, a 0-RTT packet carrying
    // it (typically an HTTP/3 request on stream 0). The caller must not resume
    // with a ticket whose allowsEarlyData() is false. start() rejects resumption
    // while an SPKI verifier is installed because tickets are not yet bound to
    // the authenticated peer identity.
    void setResumption(const SwQuicSessionTicket& ticket,
                       const SwByteArray& earlyData = SwByteArray()) {
        m_resuming = true;
        m_resumptionTicket = ticket;
        m_earlyData = earlyData;
    }
    bool isResuming() const { return m_resuming; }
    bool hasEarlyKeys() const { return m_hasEarlyKeys; }
    const SwQuicInitialKeys& earlyKeys() const { return m_earlyKeys; }
    // Next Application packet number after the 0-RTT packets: a 1-RTT
    // connection taking over must continue from here since 0-RTT and 1-RTT
    // share the Application packet-number space (RFC 9001 5.4).
    std::uint64_t clientEarlyPacketNumber() const { return m_clientEarlyPacketNumber; }
    // True once the server confirmed it accepted the offered 0-RTT (early_data
    // echoed in EncryptedExtensions); false means 0-RTT was rejected and the
    // early data must be resent in 1-RTT.
    bool earlyDataAccepted() const { return m_earlyDataAccepted; }

    // Peer transport parameters from EncryptedExtensions (RFC 9001 8.2),
    // ready to be applied to a SwQuicConnection.
    bool hasPeerTransportParameters() const { return m_hasPeerTransportParameters; }
    const SwQuicTransportParameters& peerTransportParameters() const {
        return m_peerTransportParameters;
    }
    const SwQuicTransportParameters& localTransportParameters() const {
        return m_localTransportParameters;
    }
    bool setLocalTransportParameters(const SwQuicTransportParameters& parameters) {
        if (m_state != State::Idle || parameters.hasOriginalDestinationConnectionId ||
            parameters.hasStatelessResetToken ||
            parameters.hasInitialSourceConnectionId ||
            parameters.hasRetrySourceConnectionId ||
            parameters.maxUdpPayloadSize <
                SwQuicLimits::minimumInitialUdpPayloadBytes() ||
            parameters.maxUdpPayloadSize >
                SwQuicLimits::maximumUdpPayloadBytes() ||
            parameters.activeConnectionIdLimit < 2 ||
            parameters.maxDatagramFrameSize >
                SwQuicLimits::maximumDatagramFrameBytes()) return false;
        SwByteArray encoded;
        SwString error;
        if (!parameters.encode(encoded, &error)) return false;
        m_localTransportParameters = parameters;
        return true;
    }
    const SwByteArray& negotiatedAlpn() const { return m_negotiatedAlpn; }
    bool setApplicationProtocol(const SwByteArray& protocol) {
        if (m_state != State::Idle || protocol.isEmpty() || protocol.size() > 255) return false;
        m_applicationProtocol = protocol;
        return true;
    }
    const SwByteArray& applicationProtocol() const { return m_applicationProtocol; }
    std::uint16_t negotiatedKeyExchangeGroup() const { return m_negotiatedKeyExchangeGroup; }
    bool setHybridKeyExchangeEnabled(bool enabled) {
        if (m_state != State::Idle) return false;
        m_enableHybridKeyExchange = enabled;
        return true;
    }
    bool hybridKeyExchangeEnabled() const { return m_enableHybridKeyExchange; }

    const SwByteArray& clientEphemeralPublicKey() const { return m_clientPublicKey; }
    const SwQuicInitialKeys& clientHandshakeKeys() const { return m_clientHandshakeKeys; }
    const SwQuicInitialKeys& serverHandshakeKeys() const { return m_serverHandshakeKeys; }
    const SwQuicInitialKeys& clientApplicationKeys() const { return m_clientApplicationKeys; }
    const SwQuicInitialKeys& serverApplicationKeys() const { return m_serverApplicationKeys; }
    const SwQuicConnectionId& destinationConnectionId() const { return m_serverConnectionId; }
    const SwQuicConnectionId& sourceConnectionId() const { return m_sourceConnectionId; }
    // DCID du tout premier Initial. Contrairement a destinationConnectionId(),
    // cette valeur ne change pas quand le serveur choisit son SCID.
    const SwQuicConnectionId& originalDestinationConnectionId() const {
        return m_originalDestinationConnectionId;
    }
    bool retryReceived() const { return m_retryReceived; }

    // RFC 8446 section 7.5. The exporter master secret never leaves the
    // handshake driver; callers can only derive labelled keying material.
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

    // Compatibility overload for callers that transport a whole QUIC flight
    // in one UDP datagram. New path-aware callers should use the vector form.
    bool start(const SwString& serverName,
               SwByteArray& outInitialDatagram,
               SwString* error = nullptr) {
        SwVector<SwByteArray> flight;
        if (!start(serverName, flight, error)) {
            outInitialDatagram.clear();
            return false;
        }
        outInitialDatagram.clear();
        for (std::size_t i = 0; i < flight.size(); ++i) {
            outInitialDatagram.append(flight[i]);
        }
        return true;
    }

    // Generates ephemeral key material and a bounded Initial flight. A hybrid
    // ClientHello spans multiple independently protected QUIC Initial packets.
    bool start(const SwString& serverName,
               SwVector<SwByteArray>& outInitialDatagrams,
               SwString* error = nullptr) {
        m_serverName = serverName;

        if (m_requireRawPublicKeys &&
            (!m_credential.isValid() ||
             m_credential.certificateType != SwQuicCertificateType::RawPublicKey ||
             m_credential.signatureScheme != 0x0807 ||
             !m_subjectPublicKeyInfoVerifier)) {
            outInitialDatagrams.clear();
            setError_(error,
                      "RFC 7250 requires an Ed25519 client RPK and peer verifier");
            return fail_(error);
        }

        if (m_resuming && (m_subjectPublicKeyInfoVerifier || m_requireRawPublicKeys)) {
            outInitialDatagrams.clear();
            setError_(error,
                      "TLS resumption is disabled with SPKI pinning until tickets are identity-bound");
            return fail_(error);
        }

        SwByteArray privateScalar;
        SecretGuard_ privateScalarGuard(privateScalar);
        if (!SwQuicRandom::fill(privateScalar, 32, error) ||
            !SwQuicRandom::fill(m_clientRandom, 32, error)) {
            return fail_(error);
        }
        m_clientPrivateKey = privateScalar;
        if (!SwQuicX25519::derivePublicKey(m_clientPrivateKey, m_clientPublicKey, error)) {
            return fail_(error);
        }
        if (m_enableHybridKeyExchange &&
            !SwQuicMlKem768::keyPair(m_clientMlKemPublicKey,
                                     m_clientMlKemPrivateKey, error)) {
            return fail_(error);
        }
        const SwByteArray* mlKemPublicKey = m_enableHybridKeyExchange
            ? &m_clientMlKemPublicKey : nullptr;

        SwByteArray dcidBytes;
        SwByteArray scidBytes;
        if (!SwQuicRandom::fill(dcidBytes, 8, error) ||
            !SwQuicRandom::fill(scidBytes, 8, error)) {
            return fail_(error);
        }
        if (!SwQuicConnectionId::fromBytes(dcidBytes, m_originalDestinationConnectionId, error) ||
            !SwQuicConnectionId::fromBytes(scidBytes, m_sourceConnectionId, error)) {
            return fail_(error);
        }
        // Until the server tells us its own connection ID we address it by the
        // random original DCID we chose.
        m_serverConnectionId = m_originalDestinationConnectionId;

        if (!SwQuicInitialSecrets::deriveV1(m_originalDestinationConnectionId,
                                            m_clientInitialKeys,
                                            m_serverInitialKeys,
                                            error)) {
            return fail_(error);
        }

        if (m_resuming) {
            if (m_applicationProtocol != SwByteArray("h3")) {
                setError_(error, "Custom ALPN resumption is not configured");
                return fail_(error);
            }
            // Resumption ClientHello with a PSK binder + early_data, and derive
            // the client early (0-RTT) keys (RFC 8446 4.2.11, RFC 9001 4.6).
            if (!SwQuicClientHelloBuilder::buildForHttp3Resumption(
                    serverName, m_sourceConnectionId, m_clientPublicKey, m_clientRandom,
                    m_resumptionTicket.ticket, m_resumptionTicket.ticketAgeAdd,
                    m_resumptionTicket.resumptionPsk, m_clientHelloMessage, m_earlyKeys,
                    error, &m_localTransportParameters, mlKemPublicKey)) {
                return fail_(error);
            }
            m_hasEarlyKeys = true;
        } else if (!SwQuicClientHelloBuilder::buildForAlpn(serverName,
                                                           m_sourceConnectionId,
                                                           m_clientPublicKey,
                                                           m_clientRandom,
                                                           m_applicationProtocol,
                                                           m_clientHelloMessage,
                                                           error,
                                                           m_requireRawPublicKeys,
                                                           &m_localTransportParameters,
                                                           mlKemPublicKey)) {
            return fail_(error);
        }

        SwQuicClientInitialBuilder::Options options;
        options.serverName = serverName;
        options.destinationConnectionId = m_originalDestinationConnectionId;
        options.sourceConnectionId = m_sourceConnectionId;
        options.packetNumber = m_clientInitialPacketNumber;
        if (!SwQuicClientInitialBuilder::buildFlightFromClientHello(
                options, m_clientHelloMessage, outInitialDatagrams,
                maximumInitialDatagramSize_(), error)) {
            return fail_(error);
        }
        m_clientInitialPacketNumber +=
            static_cast<std::uint64_t>(outInitialDatagrams.size());

        // Coalesce a 0-RTT packet carrying the early application data behind the
        // Initial, protected with the early keys.
        if (m_resuming && !m_earlyData.isEmpty()) {
            SwByteArray zeroRttPacket;
            if (!buildZeroRttPacket_(m_earlyData, zeroRttPacket, error)) {
                return fail_(error);
            }
            if (zeroRttPacket.size() > maximumInitialDatagramSize_()) {
                setError_(error, "0-RTT packet exceeds the QUIC path datagram cap");
                return fail_(error);
            }
            outInitialDatagrams.push_back(zeroRttPacket);
        }

        m_state = State::WaitServerHello;
        clearError_(error);
        return true;
    }

    // Consumes one received UDP datagram (which may coalesce several QUIC packets)
    // and appends any datagrams that should be sent in response.
    bool processIncomingDatagram(const SwByteArray& datagram,
                                 SwVector<SwByteArray>& outDatagrams,
                                 SwString* error = nullptr) {
        if (m_state == State::Failed) {
            setError_(error, "QUIC handshake client is in the failed state");
            return false;
        }

        std::size_t offset = 0;
        while (offset < datagram.size()) {
            const std::uint8_t firstByte = static_cast<std::uint8_t>(datagram.constData()[offset]);
            if ((firstByte & 0x80U) == 0) {
                // Short header (1-RTT). We cannot process application data until
                // the handshake is done; stop scanning this datagram.
                break;
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
                // RFC 9001 4.9.1: once Initial keys are discarded, Initial packets
                // are no longer processed. Stop scanning this datagram.
                if (m_initialKeysDiscarded) {
                    break;
                }
                if (!handleServerInitial_(*remaining, consumed, error)) {
                    return fail_(error);
                }
            } else if (longType == 0x20U) {
                if (!handleServerHandshake_(*remaining, consumed, error)) {
                    return fail_(error);
                }
            } else if (longType == 0x30U) {
                if (!handleRetry_(*remaining, consumed, outDatagrams, error)) {
                    return fail_(error);
                }
            } else {
                // A server never sends 0-RTT packets.
                setError_(error, "QUIC server sent an invalid 0-RTT packet");
                return fail_(error);
            }

            if (consumed == 0) {
                break;
            }
            offset += consumed;
        }

        // Acknowledge the server's Initial promptly: QUIC's anti-amplification
        // limit (RFC 9000 8.1) otherwise stalls the server's Handshake flight
        // until it has validated our address.
        if (m_pendingInitialAck && !m_receivedInitialPacketNumbers.empty()) {
            SwByteArray ackDatagram;
            if (buildAckOnlyDatagram_(false, m_clientInitialKeys, m_clientInitialPacketNumber,
                                      m_receivedInitialPacketNumbers, ackDatagram, error)) {
                outDatagrams.push_back(ackDatagram);
            }
            m_pendingInitialAck = false;
        }

        // If we now have the ServerHello, install handshake keys.
        if (m_state == State::WaitServerHello && !m_serverHandshakeTrafficSecret.isEmpty()) {
            m_state = State::WaitServerHandshake;
        }

        // If we have the full server flight, verify and finish.
        if (m_state == State::WaitServerHandshake && !m_handshakeComplete) {
            if (!tryCompleteHandshake_(outDatagrams, error)) {
                return fail_(error);
            }
        }

        // If the flight is still incomplete but we received Handshake packets,
        // acknowledge them so the server keeps sending the rest of its flight.
        if (!m_handshakeComplete && m_pendingHandshakeAck &&
            !m_receivedHandshakePacketNumbers.empty()) {
            SwByteArray ackDatagram;
            if (buildAckOnlyDatagram_(true, m_clientHandshakeKeys, m_clientHandshakePacketNumber,
                                      m_receivedHandshakePacketNumbers, ackDatagram, error)) {
                outDatagrams.push_back(ackDatagram);
            }
            m_pendingHandshakeAck = false;
        }

        clearError_(error);
        return true;
    }

private:
    // The Initial flight is emitted before some transports can push their
    // live path MTU into the established connection.  Use QUIC's universal
    // 1200-byte floor here so the first flight is valid on every admitted path.
    static std::size_t maximumInitialDatagramSize_() { return 1200; }

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

    void discardHandshakeSecrets_() noexcept {
        m_clientPrivateKey.secureClear();
        m_clientMlKemPrivateKey.secureClear();
        m_handshakeSecret.secureClear();
        m_clientHandshakeTrafficSecret.secureClear();
        m_serverHandshakeTrafficSecret.secureClear();
        secureClearKeys_(m_earlyKeys);
        m_hasEarlyKeys = false;
        m_resumptionTicket.resumptionPsk.secureClear();
    }

    void discardFailedSecrets_() noexcept {
        discardHandshakeSecrets_();
        m_exporterMasterSecret.secureClear();
        m_resumptionMasterSecret.secureClear();
        m_clientApplicationTrafficSecret.secureClear();
        m_serverApplicationTrafficSecret.secureClear();
        secureClearKeys_(m_clientInitialKeys);
        secureClearKeys_(m_serverInitialKeys);
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

    // ------------------------------------------------------------ Initial ---

    bool handleRetry_(const SwByteArray& packet,
                      std::size_t& consumed,
                      SwVector<SwByteArray>& outDatagrams,
                      SwString* error) {
        consumed = 0;
        const auto discard = [&]() {
            // Retry has no length field and is always the final/only packet in
            // its datagram. RFC 9000 requires invalid, late and additional
            // Retry packets to be discarded, not turned into an off-path DoS.
            consumed = static_cast<std::size_t>(packet.size());
            clearError_(error);
            return true;
        };
        if (m_state != State::WaitServerHello || m_retryReceived ||
            m_initialKeysDiscarded || !m_serverInitialCrypto.isEmpty() ||
            !m_receivedInitialPacketNumbers.empty() ||
            !m_serverHandshakeTrafficSecret.isEmpty()) {
            return discard();
        }

        SwQuicRetry::ParsedRetry parsed;
        if (!SwQuicRetry::parseRetryPacket(packet, parsed, error) ||
            !SwQuicRetry::verifyRetryPacket(
                packet, m_originalDestinationConnectionId.bytes(), error)) {
            return discard();
        }
        if (!(parsed.destinationConnectionId == m_sourceConnectionId.bytes())) {
            return discard();
        }
        if (parsed.sourceConnectionId.isEmpty() || parsed.token.isEmpty() ||
            parsed.sourceConnectionId == m_serverConnectionId.bytes()) {
            return discard();
        }

        SwQuicConnectionId retrySource;
        if (!SwQuicConnectionId::fromBytes(
                parsed.sourceConnectionId, retrySource, error)) return discard();

        // RFC 9000 17.2.5: replace the destination CID and derive fresh
        // Initial keys. Packet numbers in every space keep increasing after
        // Retry. In particular, 0-RTT and 1-RTT share the Application Data
        // packet-number space and the 0-RTT keys do not change.
        secureClearKeys_(m_clientInitialKeys);
        secureClearKeys_(m_serverInitialKeys);
        m_serverConnectionId = retrySource;
        m_retrySourceConnectionId = retrySource;
        m_retryToken = parsed.token;
        m_retryReceived = true;
        m_receivedInitialPacketNumbers.clear();
        m_pendingInitialAck = false;
        if (!SwQuicInitialSecrets::deriveV1(m_serverConnectionId,
                                            m_clientInitialKeys,
                                            m_serverInitialKeys,
                                            error)) {
            return false;
        }

        SwQuicClientInitialBuilder::Options options;
        options.serverName = m_serverName;
        options.destinationConnectionId = m_serverConnectionId;
        options.sourceConnectionId = m_sourceConnectionId;
        options.token = m_retryToken;
        options.packetNumber = m_clientInitialPacketNumber;
        SwVector<SwByteArray> retriedInitials;
        if (!SwQuicClientInitialBuilder::buildFlightFromClientHello(
                options, m_clientHelloMessage, retriedInitials,
                maximumInitialDatagramSize_(), error)) {
            return false;
        }
        m_clientInitialPacketNumber +=
            static_cast<std::uint64_t>(retriedInitials.size());

        if (m_resuming && !m_earlyData.isEmpty()) {
            SwByteArray zeroRttPacket;
            if (!buildZeroRttPacket_(m_earlyData, zeroRttPacket, error)) return false;
            if (zeroRttPacket.size() > maximumInitialDatagramSize_()) {
                setError_(error, "Retried 0-RTT packet exceeds the QUIC path datagram cap");
                return false;
            }
            retriedInitials.push_back(zeroRttPacket);
        }
        for (std::size_t i = 0; i < retriedInitials.size(); ++i) {
            outDatagrams.push_back(retriedInitials[i]);
        }
        consumed = static_cast<std::size_t>(packet.size());
        clearError_(error);
        return true;
    }

    bool handleServerInitial_(const SwByteArray& packet,
                              std::size_t& consumed,
                              SwString* error) {
        SwQuicPacketHeader header;
        SwByteArray payload;
        if (!SwQuicPacketProtector::unprotectInitial(m_serverInitialKeys, packet, header, payload,
                                                     &consumed, error)) {
            return false;
        }
        // The server's Source Connection ID becomes our destination for later packets.
        if (!header.sourceConnectionId().isEmpty()) {
            m_serverConnectionId = header.sourceConnectionId();
        }
        m_receivedInitialPacketNumbers.insert(header.packetNumber(), true);
        m_pendingInitialAck = true;

        SwVector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(payload, frames, error)) {
            return false;
        }
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::Crypto) {
                if (!appendCrypto_(m_serverInitialCrypto, m_serverInitialCryptoReassembly,
                                   frames[i], error)) {
                    return false;
                }
            } else if (frames[i].type() == SwQuicFrame::Type::ConnectionClose) {
                setError_(error, "Server closed the connection during the Initial flight");
                return false;
            }
        }

        return tryParseServerHello_(error);
    }

    bool tryParseServerHello_(SwString* error) {
        if (!m_serverHandshakeTrafficSecret.isEmpty()) {
            return true;  // already processed
        }

        SwVector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(m_serverInitialCrypto, messages, error)) {
            // Not enough bytes yet is reported as truncation; treat as "wait".
            clearError_(error);
            return true;
        }
        if (messages.empty() || messages[0].type != 0x02) {
            return true;  // wait for more
        }

        SwTls13Messages::ServerHello serverHello;
        if (!SwTls13Messages::parseServerHello(messages[0].body, serverHello, error)) {
            return false;
        }
        if (serverHello.isHelloRetryRequest) {
            setError_(error, "Server sent HelloRetryRequest (key-share group change not supported)");
            return false;
        }
        if (serverHello.cipherSuite != 0x1301) {
            setError_(error, "Server selected a cipher suite other than TLS_AES_128_GCM_SHA256");
            return false;
        }
        // Whether the server accepted our PSK. If we resumed but the server
        // did NOT select the PSK, the schedule must fall back to a non-PSK
        // Early-Secret so it matches the server's (RFC 8446 4.2.11 / 2.2).
        m_pskAccepted = m_resuming && serverHello.selectedPreSharedKey;
        // The server must select TLS 1.3 via supported_versions (RFC 8446
        // 4.2.1); QUIC has no lower TLS version to fall back to.
        if (!serverHello.selectedTls13) {
            setError_(error, "ServerHello did not negotiate TLS 1.3 via supported_versions");
            return false;
        }
        // Derive the selected classical or hybrid key-exchange secret. For the
        // hybrid group the TLS input is ML-KEM shared_secret || X25519 shared_secret.
        SwByteArray keyExchangeSecret;
        SecretGuard_ keyExchangeSecretGuard(keyExchangeSecret);
        if (serverHello.selectedGroup == SwQuicHybridKex::x25519MlKem768Group()) {
            SwByteArray mlKemCiphertext;
            SwByteArray serverX25519Public;
            SwByteArray mlKemSecret;
            SecretGuard_ mlKemSecretGuard(mlKemSecret);
            SwByteArray x25519Secret;
            SecretGuard_ x25519SecretGuard(x25519Secret);
            if (!SwQuicHybridKex::splitServerKeyShare(
                    serverHello.serverKeyShare, mlKemCiphertext,
                    serverX25519Public, error) ||
                !SwQuicMlKem768::decapsulate(m_clientMlKemPrivateKey,
                                             mlKemCiphertext,
                                             mlKemSecret, error) ||
                !SwQuicX25519::computeSharedSecret(m_clientPrivateKey,
                                                   serverX25519Public,
                                                   x25519Secret, error) ||
                !SwQuicHybridKex::combineSecrets(mlKemSecret, x25519Secret,
                                                 keyExchangeSecret, error)) {
                return false;
            }
        } else if (serverHello.selectedGroup == SwQuicHybridKex::x25519Group()) {
            if (serverHello.serverKeyShare.size() !=
                    SwQuicHybridKex::x25519PublicKeySize() ||
                !SwQuicX25519::computeSharedSecret(m_clientPrivateKey,
                                                   serverHello.serverKeyShare,
                                                   keyExchangeSecret, error)) {
                if (error && error->isEmpty()) {
                    setError_(error, "ServerHello carries an invalid X25519 key share");
                }
                return false;
            }
        } else {
            setError_(error, "ServerHello selected an unsupported key-exchange group");
            return false;
        }
        m_negotiatedKeyExchangeGroup = serverHello.selectedGroup;
        m_clientPrivateKey.secureClear();
        m_clientMlKemPrivateKey.secureClear();

        m_serverHelloMessage = rawMessage_(0x02, messages[0].body);
        SwByteArray transcriptChSh;
        transcriptChSh.append(m_clientHelloMessage);
        transcriptChSh.append(m_serverHelloMessage);
        const SwByteArray thChSh = SwTls13KeySchedule::transcriptHash(transcriptChSh);

        // Seed the Early-Secret with the PSK only if the server actually
        // selected it; otherwise the server ran a full handshake and we must
        // match with a zero Early-Secret (RFC 8446 7.1 / 2.2).
        SwByteArray earlySecret;
        SecretGuard_ earlySecretGuard(earlySecret);
        const bool earlyOk = m_pskAccepted
            ? SwTls13KeySchedule::earlySecretWithPsk(m_resumptionTicket.resumptionPsk,
                                                     earlySecret, error)
            : SwTls13KeySchedule::earlySecret(earlySecret, error);
        if (!earlyOk ||
            !SwTls13KeySchedule::handshakeSecret(earlySecret, keyExchangeSecret,
                                                 m_handshakeSecret, error) ||
            !SwTls13KeySchedule::clientHandshakeTrafficSecret(m_handshakeSecret, thChSh,
                                                              m_clientHandshakeTrafficSecret, error) ||
            !SwTls13KeySchedule::serverHandshakeTrafficSecret(m_handshakeSecret, thChSh,
                                                              m_serverHandshakeTrafficSecret, error)) {
            return false;
        }
        if (!SwQuicPacketKeys::deriveAes128(m_clientHandshakeTrafficSecret, m_clientHandshakeKeys, error) ||
            !SwQuicPacketKeys::deriveAes128(m_serverHandshakeTrafficSecret, m_serverHandshakeKeys, error)) {
            return false;
        }

        clearError_(error);
        return true;
    }

    // ---------------------------------------------------------- Handshake ---

    bool handleServerHandshake_(const SwByteArray& packet,
                                std::size_t& consumed,
                                SwString* error) {
        if (m_serverHandshakeKeys.key.isEmpty()) {
            // Handshake packet arrived (coalesced) before we processed the
            // ServerHello in this same datagram is impossible because Initial
            // precedes Handshake; if packet keys are missing we cannot read it yet.
            // Do not use the traffic secret as this readiness flag: that secret
            // is securely discarded once all required packet keys are derived.
            setError_(error, "Handshake packet received before handshake keys were installed");
            return false;
        }

        SwQuicPacketHeader header;
        SwByteArray payload;
        if (!SwQuicPacketProtector::unprotectHandshake(m_serverHandshakeKeys, packet, header, payload,
                                                       &consumed, error)) {
            return false;
        }
        m_receivedHandshakePacketNumbers.insert(header.packetNumber(), true);
        m_pendingHandshakeAck = true;

        SwVector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(payload, frames, error)) {
            return false;
        }
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::Crypto) {
                if (!appendCrypto_(m_serverHandshakeCrypto, m_serverHandshakeCryptoReassembly,
                                   frames[i], error)) {
                    return false;
                }
            } else if (frames[i].type() == SwQuicFrame::Type::ConnectionClose) {
                setError_(error, "Server closed the connection during the Handshake flight");
                return false;
            }
        }

        clearError_(error);
        return true;
    }

    bool processEncryptedExtensions_(const SwByteArray& eeBody, SwString* error) {
        SwTls13Messages::EncryptedExtensions extensions;
        if (!SwTls13Messages::parseEncryptedExtensions(eeBody, extensions, error)) {
            return false;
        }
        if (extensions.hasAlpn) {
            m_negotiatedAlpn = extensions.alpnProtocol;
        }
        if (extensions.acceptedEarlyData) {
            m_earlyDataAccepted = true; // server confirmed our 0-RTT (RFC 9001 4.6)
        }
        if (m_requireRawPublicKeys &&
            (!extensions.hasClientCertificateType ||
             !extensions.hasServerCertificateType ||
             extensions.clientCertificateType != 2 ||
             extensions.serverCertificateType != 2)) {
            setError_(error,
                      "Server did not select mutual RFC 7250 RawPublicKey");
            return false;
        }
        // quic_transport_parameters is mandatory in EncryptedExtensions
        // (RFC 9001 8.2); its absence is a missing_extension (0x016d).
        if (!extensions.hasTransportParameters) {
            setError_(error, "EncryptedExtensions missing quic_transport_parameters (RFC 9001 8.2)");
            return false;
        }
        if (!SwQuicTransportParameters::decode(extensions.transportParameters,
                                               m_peerTransportParameters, error)) {
            return false;
        }
        m_hasPeerTransportParameters = true;
        // RFC 9000 7.3: the server's original_destination_connection_id must
        // equal the DCID of our first Initial, and initial_source_connection_id
        // must equal the SCID of the server's Initial. Absence or mismatch is a
        // TRANSPORT_PARAMETER_ERROR.
        if (!m_peerTransportParameters.hasOriginalDestinationConnectionId ||
            !(m_peerTransportParameters.originalDestinationConnectionId ==
              m_originalDestinationConnectionId.bytes())) {
            setError_(error, "Server original_destination_connection_id mismatch (RFC 9000 7.3)");
            return false;
        }
        if (!m_peerTransportParameters.hasInitialSourceConnectionId ||
            !(m_peerTransportParameters.initialSourceConnectionId ==
              m_serverConnectionId.bytes())) {
            setError_(error, "Server initial_source_connection_id mismatch (RFC 9000 7.3)");
            return false;
        }
        if (m_retryReceived) {
            if (!m_peerTransportParameters.hasRetrySourceConnectionId ||
                !(m_peerTransportParameters.retrySourceConnectionId ==
                  m_retrySourceConnectionId.bytes())) {
                setError_(error,
                          "Server retry_source_connection_id mismatch (RFC 9000 7.3)");
                return false;
            }
        } else if (m_peerTransportParameters.hasRetrySourceConnectionId) {
            setError_(error,
                      "Server sent retry_source_connection_id without a Retry");
            return false;
        }
        return true;
    }

    // Build a coalesced 0-RTT (long header type 0x10) packet carrying the early
    // application data as a STREAM frame on stream 0, protected with the early
    // keys (RFC 9001 4.6).
    bool buildZeroRttPacket_(const SwByteArray& earlyData,
                             SwByteArray& outPacket,
                             SwString* error) {
        SwVector<SwQuicFrame> frames;
        frames.push_back(SwQuicFrame::stream(0, 0, earlyData, true));
        SwByteArray payload;
        if (!SwQuicFrameCodec::encodeFrames(frames, payload, error)) {
            return false;
        }
        while (payload.size() < 4) {
            payload.append(static_cast<char>(0));
        }

        const std::uint8_t pnLen = 4;
        const std::uint64_t packetNumber = m_clientEarlyPacketNumber;
        SwByteArray header;
        header.append(static_cast<char>(0xd0U | (pnLen - 1U)));
        header.append(static_cast<char>(0));
        header.append(static_cast<char>(0));
        header.append(static_cast<char>(0));
        header.append(static_cast<char>(1)); // version 1
        header.append(static_cast<char>(m_serverConnectionId.size()));
        header.append(m_serverConnectionId.bytes());
        header.append(static_cast<char>(m_sourceConnectionId.size()));
        header.append(m_sourceConnectionId.bytes());
        const std::uint64_t lengthField =
            static_cast<std::uint64_t>(payload.size() + pnLen +
                                       SwQuicPacketProtector::kTagLength);
        if (!SwQuicVarIntCodec::encode(lengthField, header, error)) {
            return false;
        }
        for (std::uint8_t i = 0; i < pnLen; ++i) {
            const std::uint8_t shift = static_cast<std::uint8_t>((pnLen - 1 - i) * 8);
            header.append(static_cast<char>((packetNumber >> shift) & 0xffU));
        }

        if (!SwQuicPacketProtector::protectLongHeader(m_earlyKeys, packetNumber, pnLen,
                                                      header, payload, outPacket, error)) {
            return false;
        }
        ++m_clientEarlyPacketNumber;
        return true;
    }

    bool tryCompleteHandshake_(SwVector<SwByteArray>& outDatagrams, SwString* error) {
        SwVector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(m_serverHandshakeCrypto, messages, error)) {
            clearError_(error);
            return true;  // wait for more Handshake CRYPTO
        }

        // We need EncryptedExtensions(8), Certificate(11), CertificateVerify(15)
        // and Finished(20). Locate the Finished; everything before it is the
        // transcript context for its verify_data.
        int finishedIndex = -1;
        int encryptedExtensionsIndex = -1;
        int certificateRequestIndex = -1;
        int certificateIndex = -1;
        int certificateVerifyIndex = -1;
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].type == 0x14) {
                finishedIndex = static_cast<int>(i);
                break;
            }
            if (messages[i].type == 0x08) {
                if (encryptedExtensionsIndex >= 0) {
                    setError_(error, "Duplicate EncryptedExtensions message");
                    return false;
                }
                encryptedExtensionsIndex = static_cast<int>(i);
                if (!processEncryptedExtensions_(messages[i].body, error)) {
                    return false;
                }
            } else if (messages[i].type == 0x0d) {
                if (certificateRequestIndex >= 0) {
                    setError_(error, "Duplicate CertificateRequest message");
                    return false;
                }
                certificateRequestIndex = static_cast<int>(i);
                if (!parseCertificateRequest_(messages[i].body, error)) return false;
                m_serverRequestedClientCertificate = true;
            } else if (messages[i].type == 0x0b) {
                certificateIndex = static_cast<int>(i);
                SwByteArray leaf;
                if (SwTls13Messages::extractLeafCertificate(messages[i].body, leaf, nullptr)) {
                    m_serverCertificateDer = leaf;
                }
                SwTls13Messages::extractCertificateChain(messages[i].body,
                                                         m_serverCertificateChain, nullptr);
            } else if (messages[i].type == 0x0f) {
                certificateVerifyIndex = static_cast<int>(i);
            } else {
                setError_(error, "Unexpected TLS message in the server Handshake flight");
                return false;
            }
        }
        if (finishedIndex < 0) {
            clearError_(error);
            return true;  // Finished not received yet
        }
        if (static_cast<std::size_t>(finishedIndex + 1) != messages.size() ||
            encryptedExtensionsIndex != 0) {
            setError_(error, "Invalid TLS server Handshake message ordering");
            return false;
        }
        if (m_pskAccepted) {
            if (certificateRequestIndex >= 0 || certificateIndex >= 0 ||
                certificateVerifyIndex >= 0 || finishedIndex != 1) {
                setError_(error, "Invalid PSK server Handshake message ordering");
                return false;
            }
        } else {
            const int expectedCertificateIndex = certificateRequestIndex >= 0 ? 2 : 1;
            if ((certificateRequestIndex >= 0 && certificateRequestIndex != 1) ||
                certificateIndex != expectedCertificateIndex ||
                certificateVerifyIndex != certificateIndex + 1 ||
                finishedIndex != certificateVerifyIndex + 1) {
                setError_(error, "Invalid certificate server Handshake message ordering");
                return false;
            }
            if (m_requireRawPublicKeys && certificateRequestIndex != 1) {
                setError_(error,
                          "Mutual RFC 7250 requires CertificateRequest");
                return false;
            }
        }

        // Transcript up to (not including) the server Finished; on the way,
        // capture the hash at the point CertificateVerify signs: everything up
        // to and including the Certificate message (RFC 8446 4.4.3).
        SwByteArray transcriptToCertVerify;
        transcriptToCertVerify.append(m_clientHelloMessage);
        transcriptToCertVerify.append(m_serverHelloMessage);
        SwByteArray thSignedByCertVerify;
        for (int i = 0; i < finishedIndex; ++i) {
            if (i == certificateVerifyIndex) {
                thSignedByCertVerify = SwTls13KeySchedule::transcriptHash(transcriptToCertVerify);
            }
            transcriptToCertVerify.append(rawMessage_(messages[i].type, messages[i].body));
        }
        const SwByteArray thToCertVerify =
            SwTls13KeySchedule::transcriptHash(transcriptToCertVerify);

        // Also cover a verifier installed after start(): an already offered
        // PSK must never turn into an authentication bypass for SPKI policy.
        if (m_pskAccepted && m_subjectPublicKeyInfoVerifier) {
            setError_(error,
                      "Server selected PSK resumption while SPKI pinning is active");
            return false;
        }

        // Server authentication. RFC 7250 consumes one SPKI directly and has
        // no X.509 fallback; the other modes keep the existing chain/SPKI path.
        SwByteArray delegatedSpkiDer;
        if (m_verifyPeer && !m_pskAccepted) {
            if (certificateIndex < 0 || certificateVerifyIndex < 0 ||
                m_serverCertificateChain.empty()) {
                setError_(error, "Server did not send a certificate chain to verify");
                return false;
            }
            if (m_requireRawPublicKeys) {
                if (m_serverCertificateChain.size() != 1) {
                    setError_(error, "Invalid RFC 7250 server Certificate payload");
                    return false;
                }
                delegatedSpkiDer = m_serverCertificateDer;
                SwByteArray rawNodeId;
                if (!SwQuicCertificateVerifier::extractEd25519RawPublicKey(
                        delegatedSpkiDer, rawNodeId, error) || rawNodeId.size() != 32) {
                    return false;
                }
            } else if (m_subjectPublicKeyInfoVerifier) {
                if (!SwQuicCertificateVerifier::extractSubjectPublicKeyInfo(
                        m_serverCertificateDer, delegatedSpkiDer, error)) {
                    return false;
                }
            } else if (m_verifyChain &&
                       !SwQuicCertificateVerifier::verifyServerChain(m_serverCertificateChain,
                                                                     m_serverName, error)) {
                return false;
            }

            std::uint16_t signatureScheme = 0;
            SwByteArray signature;
            if (!SwTls13Messages::parseCertificateVerify(
                    messages[certificateVerifyIndex].body, signatureScheme, signature, error)) {
                return false;
            }
            // RFC 8446 4.4.3: the CertificateVerify scheme MUST be one we
            // advertised (signature_algorithms) and valid for TLS 1.3
            // CertificateVerify. We offer only these supported schemes.
            if (m_requireRawPublicKeys && signatureScheme != 0x0807) {
                setError_(error, "Server RPK CertificateVerify is not ed25519");
                return false;
            }
            if (!m_requireRawPublicKeys && signatureScheme != 0x0403 &&
                signatureScheme != 0x0503 &&
                signatureScheme != 0x0804 && signatureScheme != 0x0805 &&
                signatureScheme != 0x0806 && signatureScheme != 0x0807 &&
                signatureScheme != 0x0809 && signatureScheme != 0x080a &&
                signatureScheme != 0x080b) {
                setError_(error, "Server CertificateVerify uses a non-offered signature scheme");
                return false;
            }
            const bool signatureValid = m_requireRawPublicKeys
                ? SwQuicCertificateVerifier::verifyRawPublicKeyCertificateVerify(
                      m_serverCertificateDer, signatureScheme, signature,
                      thSignedByCertVerify, error)
                : SwQuicCertificateVerifier::verifyCertificateVerify(
                      m_serverCertificateDer, signatureScheme, signature,
                      thSignedByCertVerify, error);
            if (!signatureValid) {
                return false;
            }
        }

        // ALPN must equal the exact opaque protocol configured by the caller.
        // application protocol (RFC 9001 8.1 / RFC 7301 3.2). A missing or
        // mismatched ALPN is a fatal no_application_protocol.
        if (m_negotiatedAlpn != m_applicationProtocol) {
            setError_(error, "ALPN negotiation failed: server selected another protocol");
            return false;
        }

        // Verify the server Finished MAC.
        SwByteArray serverVerifyData;
        if (!SwTls13Messages::parseFinishedVerifyData(messages[finishedIndex].body,
                                                      serverVerifyData, error)) {
            return false;
        }
        SwByteArray serverFinishedKey;
        SecretGuard_ serverFinishedKeyGuard(serverFinishedKey);
        SwByteArray expectedServerVerifyData;
        if (!SwTls13KeySchedule::finishedKey(m_serverHandshakeTrafficSecret, serverFinishedKey, error) ||
            !SwTls13KeySchedule::verifyData(serverFinishedKey, thToCertVerify,
                                            expectedServerVerifyData, error)) {
            return false;
        }
        if (!(serverVerifyData == expectedServerVerifyData)) {
            setError_(error, "Server Finished verify_data mismatch (handshake authentication failed)");
            return false;
        }

        // External trust policy observes the peer identity only after both
        // CertificateVerify and Finished authenticated the complete flight.
        if (m_subjectPublicKeyInfoVerifier && !m_pskAccepted) {
            try {
                if (!m_subjectPublicKeyInfoVerifier(delegatedSpkiDer)) {
                    setError_(error, "Server SPKI rejected by delegated trust verifier");
                    return false;
                }
            } catch (...) {
                setError_(error, "Server SPKI verifier raised an exception");
                return false;
            }
        }

        // Publish the authenticated server identity only after its Finished
        // has covered the certificate-bearing transcript.
        if (m_verifyPeer && !m_pskAccepted && !m_serverCertificateDer.isEmpty()) {
            if (m_requireRawPublicKeys) {
                m_authenticatedServerSpkiDer = m_serverCertificateDer;
            } else if (!SwQuicCertificateVerifier::extractSubjectPublicKeyInfo(
                           m_serverCertificateDer,
                           m_authenticatedServerSpkiDer, error)) {
                return false;
            }
        }

        // Transcript including the server Finished: drives the client Finished,
        // the client/server application (1-RTT) secrets.
        SwByteArray transcriptToServerFinished = transcriptToCertVerify;
        transcriptToServerFinished.append(rawMessage_(messages[finishedIndex].type,
                                                      messages[finishedIndex].body));
        const SwByteArray thToServerFinished =
            SwTls13KeySchedule::transcriptHash(transcriptToServerFinished);

        // 1-RTT application keys.
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

        // If requested, authenticate the client before Finished. The client
        // CertificateVerify signs the transcript through its Certificate with
        // the distinct RFC 8446 client context string.
        SwByteArray clientFlightPrefix;
        SwByteArray transcriptToClientFinished = transcriptToServerFinished;
        if (m_serverRequestedClientCertificate) {
            if (!m_credential.isValid()) {
                setError_(error, "Server requested a client certificate but none is configured");
                return false;
            }
            bool signatureSchemeRequested = false;
            for (std::size_t i = 0; i < m_requestedClientSignatureSchemes.size(); ++i) {
                if (m_requestedClientSignatureSchemes[i] == m_credential.signatureScheme) {
                    signatureSchemeRequested = true;
                    break;
                }
            }
            if (!signatureSchemeRequested) {
                setError_(error,
                          "Client credential signature scheme was not requested by the server");
                return false;
            }
            SwByteArray certificateBody;
            buildClientCertificateBody_(certificateBody);
            const SwByteArray certificateMessage = rawMessage_(0x0b, certificateBody);
            transcriptToClientFinished.append(certificateMessage);

            const SwByteArray thToClientCertificateVerify =
                SwTls13KeySchedule::transcriptHash(transcriptToClientFinished);
            SwByteArray certificateVerifyBody;
            if (!buildClientCertificateVerifyBody_(thToClientCertificateVerify,
                                                   certificateVerifyBody, error)) {
                return false;
            }
            const SwByteArray certificateVerifyMessage =
                rawMessage_(0x0f, certificateVerifyBody);
            transcriptToClientFinished.append(certificateVerifyMessage);
            clientFlightPrefix.append(certificateMessage);
            clientFlightPrefix.append(certificateVerifyMessage);
        }

        // Build the client Finished over the complete client-auth transcript.
        SwByteArray clientFinishedKey;
        SecretGuard_ clientFinishedKeyGuard(clientFinishedKey);
        SwByteArray clientVerifyData;
        if (!SwTls13KeySchedule::finishedKey(m_clientHandshakeTrafficSecret, clientFinishedKey, error) ||
            !SwTls13KeySchedule::verifyData(
                clientFinishedKey,
                SwTls13KeySchedule::transcriptHash(transcriptToClientFinished),
                                            clientVerifyData, error)) {
            return false;
        }
        const SwByteArray clientFinishedMessage = rawMessage_(0x14, clientVerifyData);

        // resumption_master_secret over the transcript through the client
        // Finished, for issuing/accepting session tickets (RFC 8446 7.1).
        transcriptToClientFinished.append(clientFinishedMessage);
        const SwByteArray thToClientFinished =
            SwTls13KeySchedule::transcriptHash(transcriptToClientFinished);
        if (!SwTls13KeySchedule::resumptionMasterSecret(masterSecret, thToClientFinished,
                                                        m_resumptionMasterSecret, error)) {
            return false;
        }

        clientFlightPrefix.append(clientFinishedMessage);
        SwVector<SwByteArray> handshakeDatagrams;
        if (!buildClientHandshakeFlight_(clientFlightPrefix, handshakeDatagrams, error)) {
            return false;
        }
        for (std::size_t i = 0; i < handshakeDatagrams.size(); ++i) {
            outDatagrams.push_back(handshakeDatagrams[i]);
        }

        // RFC 9001 4.9.1: the client MUST discard its Initial keys as soon as it
        // first sends a Handshake packet (this flight). Drop the key material and
        // any pending Initial ACK so no further Initial packet is emitted or read.
        discardInitialKeys_();
        discardHandshakeSecrets_();

        m_handshakeComplete = true;
        m_state = State::Complete;
        clearError_(error);
        return true;
    }

    // RFC 9001 4.9.1 / 4.9: zeroise the Initial secrets and stop all Initial-space
    // activity. After this, received Initial packets are ignored.
    void discardInitialKeys_() {
        m_initialKeysDiscarded = true;
        secureClearKeys_(m_clientInitialKeys);
        secureClearKeys_(m_serverInitialKeys);
        m_pendingInitialAck = false;
        m_receivedInitialPacketNumbers.clear();
    }

public:
    // resumption_master_secret, available once the handshake completes.
    const SwByteArray& resumptionMasterSecret() const { return m_resumptionMasterSecret; }

    // Turn a NewSessionTicket (received in 1-RTT) into stored resumption state:
    // both peers derive the same PSK from resumption_master_secret and the
    // ticket nonce (RFC 8446 4.6.1). serverTransportParams should be the params
    // this connection negotiated, remembered for future 0-RTT.
    bool processNewSessionTicket(const SwByteArray& newSessionTicketBody,
                                 const SwByteArray& serverTransportParams,
                                 SwQuicSessionTicket& outTicket,
                                 SwString* error = nullptr) {
        SwTls13Messages::NewSessionTicket nst;
        if (!SwTls13Messages::parseNewSessionTicket(newSessionTicketBody, nst, error)) {
            return false;
        }
        // RFC 9001 4.6.1: a NewSessionTicket early_data extension MUST carry
        // max_early_data_size == 0xffffffff in QUIC; any other value is a
        // PROTOCOL_VIOLATION.
        if (nst.hasEarlyData && nst.maxEarlyDataSize != 0xffffffffu) {
            setError_(error, "NewSessionTicket max_early_data_size must be 0xffffffff (RFC 9001 4.6.1)");
            return false;
        }
        SwByteArray psk;
        SecretGuard_ pskGuard(psk);
        if (!SwTls13KeySchedule::resumptionPsk(m_resumptionMasterSecret, nst.ticketNonce,
                                               psk, error)) {
            return false;
        }
        outTicket = SwQuicSessionTicket();
        outTicket.ticket = nst.ticket;
        outTicket.resumptionPsk = psk;
        outTicket.ticketAgeAdd = nst.ticketAgeAdd;
        outTicket.ticketLifetimeS = nst.ticketLifetime;
        outTicket.maxEarlyDataSize = nst.hasEarlyData ? nst.maxEarlyDataSize : 0;
        // Remember the server's initial_max_data as the real 0-RTT volume bound.
        outTicket.serverInitialMaxData = m_peerTransportParameters.initialMaxData;
        outTicket.serverTransportParams = serverTransportParams;
        outTicket.valid = true;
        clearError_(error);
        return true;
    }

private:

    bool parseCertificateRequest_(const SwByteArray& body, SwString* error) {
        const std::size_t size = static_cast<std::size_t>(body.size());
        if (size < 3 || !body.constData()) {
            setError_(error, "TLS CertificateRequest is truncated");
            return false;
        }
        const std::uint8_t* data =
            reinterpret_cast<const std::uint8_t*>(body.constData());
        const std::size_t contextLength = data[0];
        if (contextLength != 0 || 1 + contextLength + 2 > size) {
            setError_(error, "Unsupported TLS CertificateRequest context");
            return false;
        }
        std::size_t pos = 1 + contextLength;
        const std::size_t extensionsLength =
            (static_cast<std::size_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        if (extensionsLength != size - pos) {
            setError_(error, "TLS CertificateRequest extensions length mismatch");
            return false;
        }

        m_requestedClientSignatureSchemes.clear();
        bool foundSignatureAlgorithms = false;
        while (pos < size) {
            if (size - pos < 4) {
                setError_(error, "TLS CertificateRequest extension is truncated");
                return false;
            }
            const std::uint16_t type =
                static_cast<std::uint16_t>((data[pos] << 8) | data[pos + 1]);
            const std::size_t length =
                (static_cast<std::size_t>(data[pos + 2]) << 8) | data[pos + 3];
            pos += 4;
            if (length > size - pos) {
                setError_(error, "TLS CertificateRequest extension payload is truncated");
                return false;
            }
            if (type == 0x000d) {
                if (foundSignatureAlgorithms || length < 4) {
                    setError_(error, "Invalid CertificateRequest signature_algorithms");
                    return false;
                }
                foundSignatureAlgorithms = true;
                const std::size_t listLength =
                    (static_cast<std::size_t>(data[pos]) << 8) | data[pos + 1];
                if (listLength != length - 2 || listLength == 0 || (listLength & 1U) != 0) {
                    setError_(error, "Invalid CertificateRequest signature scheme list");
                    return false;
                }
                for (std::size_t item = pos + 2; item < pos + length; item += 2) {
                    m_requestedClientSignatureSchemes.push_back(
                        static_cast<std::uint16_t>((data[item] << 8) | data[item + 1]));
                }
            }
            pos += length;
        }
        if (!foundSignatureAlgorithms) {
            setError_(error, "CertificateRequest is missing signature_algorithms");
            return false;
        }
        if (m_requireRawPublicKeys &&
            (m_requestedClientSignatureSchemes.size() != 1 ||
             m_requestedClientSignatureSchemes.front() != 0x0807)) {
            setError_(error,
                      "RPK CertificateRequest must select only ed25519");
            return false;
        }
        return true;
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

    void buildClientCertificateBody_(SwByteArray& outBody) const {
        outBody.clear();
        outBody.append(static_cast<char>(0)); // certificate_request_context
        SwByteArray certificateList;
        for (std::size_t i = 0; i < m_credential.certificateChain.size(); ++i) {
            const SwByteArray& certificate = m_credential.certificateChain[i];
            appendU24_(certificateList, static_cast<std::uint32_t>(certificate.size()));
            certificateList.append(certificate);
            appendU16_(certificateList, 0); // per-certificate extensions
        }
        appendU24_(outBody, static_cast<std::uint32_t>(certificateList.size()));
        outBody.append(certificateList);
    }

    bool buildClientCertificateVerifyBody_(const SwByteArray& transcriptHash,
                                           SwByteArray& outBody,
                                           SwString* error) const {
        SwByteArray content;
        for (int i = 0; i < 64; ++i) content.append(static_cast<char>(0x20));
        content.append("TLS 1.3, client CertificateVerify");
        content.append(static_cast<char>(0));
        content.append(transcriptHash);

        SwByteArray signature;
        if (!m_credential.sign(content, signature, error) || signature.isEmpty() ||
            signature.size() > 0xffffU) {
            if (error && error->isEmpty()) {
                *error = SwString("Client CertificateVerify signing failed");
            }
            return false;
        }
        outBody.clear();
        appendU16_(outBody, m_credential.signatureScheme);
        appendU16_(outBody, static_cast<std::uint16_t>(signature.size()));
        outBody.append(signature);
        return true;
    }

    // Builds the client's Handshake flight. A persistent certificate chain may
    // span multiple MTU-sized CRYPTO frames; offsets form one TLS byte stream.
    bool buildClientHandshakeFlight_(const SwByteArray& handshakeCrypto,
                                     SwVector<SwByteArray>& outDatagrams,
                                     SwString* error) {
        outDatagrams.clear();
        if (handshakeCrypto.isEmpty()) {
            setError_(error, "Client Handshake flight is empty");
            return false;
        }
        const std::size_t kMaxCryptoPerPacket = 900;
        std::size_t offset = 0;
        bool first = true;
        while (offset < static_cast<std::size_t>(handshakeCrypto.size())) {
            const std::size_t remaining = static_cast<std::size_t>(handshakeCrypto.size()) - offset;
            const std::size_t take = (std::min)(remaining, kMaxCryptoPerPacket);
            SwVector<SwQuicFrame> frames;
            if (first && !m_receivedHandshakePacketNumbers.empty()) {
                SwMap<std::uint64_t, bool>::const_iterator largestIt =
                    m_receivedHandshakePacketNumbers.end();
                --largestIt;
                frames.push_back(SwQuicFrame::ack(largestIt.key(), 0, 0));
            }
            frames.push_back(SwQuicFrame::crypto(
                static_cast<std::uint64_t>(offset),
                handshakeCrypto.mid(static_cast<int>(offset), static_cast<int>(take))));

            SwByteArray plaintext;
            if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) return false;
            const std::uint8_t pnLen = 2;
            const std::uint64_t protectedLength =
                static_cast<std::uint64_t>(plaintext.size() + SwQuicPacketProtector::kTagLength);
            SwByteArray header;
            if (!buildHandshakeHeader_(protectedLength, pnLen, m_clientHandshakePacketNumber,
                                       header, error)) return false;
            SwByteArray datagram;
            if (!SwQuicPacketProtector::protectLongHeader(
                    m_clientHandshakeKeys, m_clientHandshakePacketNumber, pnLen,
                    header, plaintext, datagram, error)) return false;
            ++m_clientHandshakePacketNumber;
            outDatagrams.push_back(datagram);
            offset += take;
            first = false;
        }
        clearError_(error);
        return true;
    }

    // Builds a small ACK-only Initial or Handshake datagram acknowledging the
    // contiguous run of highest received packet numbers.
    bool buildAckOnlyDatagram_(bool handshakeLevel,
                               const SwQuicInitialKeys& keys,
                               std::uint64_t& packetNumberCounter,
                               const SwMap<std::uint64_t, bool>& received,
                               SwByteArray& outDatagram,
                               SwString* error) {
        SwMap<std::uint64_t, bool>::const_iterator largestIt = received.end();
        --largestIt;
        const std::uint64_t largest = largestIt.key();
        std::uint64_t low = largest;
        while (low > 0 && received.contains(low - 1)) {
            --low;
        }
        SwVector<SwQuicFrame> frames;
        frames.push_back(SwQuicFrame::ack(largest, 0, largest - low));

        SwByteArray plaintext;
        if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) {
            return false;
        }

        const std::uint8_t pnLen = 2;
        const std::uint64_t protectedLength =
            static_cast<std::uint64_t>(plaintext.size() + SwQuicPacketProtector::kTagLength);
        SwByteArray header;
        if (handshakeLevel) {
            if (!buildHandshakeHeader_(protectedLength, pnLen, packetNumberCounter, header, error)) {
                return false;
            }
        } else {
            if (!buildInitialHeader_(protectedLength, pnLen, packetNumberCounter, header, error)) {
                return false;
            }
        }

        if (!SwQuicPacketProtector::protectLongHeader(keys, packetNumberCounter, pnLen, header,
                                                      plaintext, outDatagram, error)) {
            return false;
        }
        ++packetNumberCounter;
        return true;
    }

    bool buildInitialHeader_(std::uint64_t protectedPayloadLength,
                             std::uint8_t pnLen,
                             std::uint64_t packetNumber,
                             SwByteArray& outHeader,
                             SwString* error) {
        outHeader.clear();
        outHeader.append(static_cast<char>(0xc0U | (pnLen - 1U)));   // long, fixed, Initial, pnLen
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(1));                       // version 1
        outHeader.append(static_cast<char>(m_serverConnectionId.size()));
        outHeader.append(m_serverConnectionId.bytes());
        outHeader.append(static_cast<char>(m_sourceConnectionId.size()));
        outHeader.append(m_sourceConnectionId.bytes());
        if (!SwQuicVarIntCodec::encode(0, outHeader, error)) {        // token length
            return false;
        }
        if (!SwQuicVarIntCodec::encode(protectedPayloadLength + pnLen, outHeader, error)) {
            return false;
        }
        for (std::uint8_t i = 0; i < pnLen; ++i) {
            const std::uint8_t shift = static_cast<std::uint8_t>((pnLen - 1 - i) * 8);
            outHeader.append(static_cast<char>((packetNumber >> shift) & 0xffU));
        }
        return true;
    }

    bool buildHandshakeHeader_(std::uint64_t protectedPayloadLength,
                               std::uint8_t pnLen,
                               std::uint64_t packetNumber,
                               SwByteArray& outHeader,
                               SwString* error) {
        outHeader.clear();
        outHeader.append(static_cast<char>(0xe0U | (pnLen - 1U)));   // long, fixed, Handshake, pnLen
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(0));
        outHeader.append(static_cast<char>(1));                       // version 1
        outHeader.append(static_cast<char>(m_serverConnectionId.size()));
        outHeader.append(m_serverConnectionId.bytes());
        outHeader.append(static_cast<char>(m_sourceConnectionId.size()));
        outHeader.append(m_sourceConnectionId.bytes());
        if (!SwQuicVarIntCodec::encode(protectedPayloadLength + pnLen, outHeader, error)) {
            return false;
        }
        for (std::uint8_t i = 0; i < pnLen; ++i) {
            const std::uint8_t shift = static_cast<std::uint8_t>((pnLen - 1 - i) * 8);
            outHeader.append(static_cast<char>((packetNumber >> shift) & 0xffU));
        }
        return true;
    }

    // Out-of-order CRYPTO reassembly: fragments buffer in an SwQuicStream and
    // only the contiguous prefix is appended to the handshake transcript
    // buffer, so reordered or duplicated frames are handled transparently.
    bool appendCrypto_(SwByteArray& buffer,
                       SwQuicStream& reassembly,
                       const SwQuicFrame& frame,
                       SwString* error) {
        if (!reassembly.receive(frame.offset(), frame.data(), false, error)) {
            return false;
        }
        const SwByteArray contiguous = reassembly.readContiguous();
        if (!contiguous.isEmpty()) {
            buffer.append(contiguous);
        }
        return true;
    }

    State m_state;
    SwString m_serverName;
    SwString m_error;

    SwByteArray m_clientPrivateKey;
    SwByteArray m_clientPublicKey;
    SwByteArray m_clientMlKemPrivateKey;
    SwByteArray m_clientMlKemPublicKey;
    SwByteArray m_clientRandom;
    std::uint16_t m_negotiatedKeyExchangeGroup = 0;
    bool m_enableHybridKeyExchange = true;

    SwQuicConnectionId m_originalDestinationConnectionId;
    SwQuicConnectionId m_sourceConnectionId;
    SwQuicConnectionId m_serverConnectionId;
    SwQuicConnectionId m_retrySourceConnectionId;
    SwByteArray m_retryToken;
    bool m_retryReceived = false;

    SwQuicInitialKeys m_clientInitialKeys;
    SwQuicInitialKeys m_serverInitialKeys;
    SwQuicInitialKeys m_clientHandshakeKeys;
    SwQuicInitialKeys m_serverHandshakeKeys;
    SwQuicInitialKeys m_clientApplicationKeys;
    SwQuicInitialKeys m_serverApplicationKeys;

    SwByteArray m_clientHelloMessage;
    SwByteArray m_serverHelloMessage;
    SwByteArray m_serverInitialCrypto;
    SwQuicStream m_serverInitialCryptoReassembly;
    SwByteArray m_serverHandshakeCrypto;
    SwQuicStream m_serverHandshakeCryptoReassembly;

    bool m_verifyPeer;
    bool m_verifyChain;
    bool m_requireRawPublicKeys = false;
    SwQuicServerCredential m_credential;
    bool m_serverRequestedClientCertificate = false;
    SwVector<std::uint16_t> m_requestedClientSignatureSchemes;
    std::function<bool(const SwByteArray&)> m_subjectPublicKeyInfoVerifier;
    SwByteArray m_exporterMasterSecret;
    SwByteArray m_resumptionMasterSecret;

    // 0-RTT resumption state.
    bool m_resuming = false;
    SwQuicSessionTicket m_resumptionTicket;
    SwByteArray m_earlyData;
    SwQuicInitialKeys m_earlyKeys;
    bool m_hasEarlyKeys = false;
    bool m_earlyDataAccepted = false;
    bool m_pskAccepted = false;
    std::uint64_t m_clientEarlyPacketNumber = 0;

    SwQuicTransportParameters m_peerTransportParameters;
    SwQuicTransportParameters m_localTransportParameters;
    bool m_hasPeerTransportParameters;
    SwByteArray m_applicationProtocol{SwByteArray("h3")};
    SwByteArray m_negotiatedAlpn;
    SwVector<SwByteArray> m_serverCertificateChain;

    SwByteArray m_handshakeSecret;
    SwByteArray m_clientHandshakeTrafficSecret;
    SwByteArray m_serverHandshakeTrafficSecret;
    SwByteArray m_clientApplicationTrafficSecret;
    SwByteArray m_serverApplicationTrafficSecret;
    SwByteArray m_serverCertificateDer;
    SwByteArray m_authenticatedServerSpkiDer;

    std::uint64_t m_clientInitialPacketNumber;
    std::uint64_t m_clientHandshakePacketNumber;
    SwMap<std::uint64_t, bool> m_receivedInitialPacketNumbers;
    SwMap<std::uint64_t, bool> m_receivedHandshakePacketNumbers;
    bool m_pendingInitialAck = false;
    bool m_pendingHandshakeAck = false;
    bool m_initialKeysDiscarded = false;

    bool m_handshakeComplete;

public:
    // RFC 9001 4.9.1: a client discards its Initial keys as soon as it first sends
    // a Handshake packet. Exposed for tests validating that invariant.
    bool initialKeysDiscarded() const { return m_initialKeysDiscarded; }
#if defined(SW_QUIC_ENABLE_SECRET_LIFETIME_TEST_HOOKS)
    bool transientSecretsDiscardedForTest() const {
        return m_clientPrivateKey.isEmpty() &&
               m_clientMlKemPrivateKey.isEmpty() &&
               m_handshakeSecret.isEmpty() &&
               m_clientHandshakeTrafficSecret.isEmpty() &&
               m_serverHandshakeTrafficSecret.isEmpty() &&
               m_earlyKeys.secret.isEmpty() &&
               m_earlyKeys.key.isEmpty() &&
               m_earlyKeys.iv.isEmpty() &&
               m_earlyKeys.headerProtectionKey.isEmpty() &&
               !m_hasEarlyKeys &&
               m_resumptionTicket.resumptionPsk.isEmpty();
    }
#endif
};

#endif
