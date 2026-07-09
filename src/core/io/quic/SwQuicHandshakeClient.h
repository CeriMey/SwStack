#ifndef SWQUICHANDSHAKECLIENT_H
#define SWQUICHANDSHAKECLIENT_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicCertificateVerifier.h"
#include "quic/SwQuicClientHelloBuilder.h"
#include "quic/SwQuicClientInitialBuilder.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicFrameCodec.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicPacketCodec.h"
#include "quic/SwQuicPacketHeader.h"
#include "quic/SwQuicPacketKeys.h"
#include "quic/SwQuicPacketProtector.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwQuicSessionTicket.h"
#include "quic/SwQuicStream.h"
#include "quic/SwQuicTransportParameters.h"
#include "quic/SwQuicVarIntCodec.h"
#include "quic/SwQuicX25519.h"
#include "quic/SwTls13KeySchedule.h"

#include <functional>
#include "quic/SwTls13Messages.h"

#include <cstdint>
#include <set>
#include <vector>

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
    }

    State state() const { return m_state; }
    bool handshakeComplete() const { return m_handshakeComplete; }
    const SwString& errorString() const { return m_error; }
    const SwByteArray& serverCertificateDer() const { return m_serverCertificateDer; }
    const std::vector<SwByteArray>& serverCertificateChain() const {
        return m_serverCertificateChain;
    }

    // PKI verification toggle: keep it enabled against real servers; disable
    // it only for loopback tests with synthetic certificates.
    void setVerifyPeer(bool verify) { m_verifyPeer = verify; }
    bool verifyPeer() const { return m_verifyPeer; }

    // Chain-policy toggle, independent of the signature check. With a real but
    // self-signed peer (loopback server), set this false to still verify the
    // CertificateVerify signature while skipping the trusted-root requirement.
    void setVerifyCertificateChain(bool verify) { m_verifyChain = verify; }
    bool verifyCertificateChain() const { return m_verifyChain; }

    // Confiance déléguée par clé (RFC 7250 / politique applicative) : si un vérificateur est installé,
    // la décision de confiance sur le certificat/clé publique du serveur lui est déléguée (p. ex.
    // « cette clé est-elle un membre connu ? ») AU LIEU de la validation de chaîne X.509. La preuve de
    // possession (CertificateVerify) reste exigée. Cette pile ne connaît pas la politique : elle
    // fournit les octets présentés (serverCertificateDer) et honore la décision retournée.
    void setRawPublicKeyVerifier(std::function<bool(const SwByteArray& certificateOrSpki)> verifier) {
        m_rawPublicKeyVerifier = std::move(verifier);
    }
    bool hasRawPublicKeyVerifier() const { return static_cast<bool>(m_rawPublicKeyVerifier); }

    // Enable 0-RTT resumption: the next start() sends a resumption ClientHello
    // for this ticket and, if earlyData is non-empty, a 0-RTT packet carrying
    // it (typically an HTTP/3 request on stream 0). The caller must not resume
    // with a ticket whose allowsEarlyData() is false.
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
    const SwByteArray& negotiatedAlpn() const { return m_negotiatedAlpn; }

    const SwByteArray& clientEphemeralPublicKey() const { return m_clientPublicKey; }
    const SwQuicInitialKeys& clientHandshakeKeys() const { return m_clientHandshakeKeys; }
    const SwQuicInitialKeys& serverHandshakeKeys() const { return m_serverHandshakeKeys; }
    const SwQuicInitialKeys& clientApplicationKeys() const { return m_clientApplicationKeys; }
    const SwQuicInitialKeys& serverApplicationKeys() const { return m_serverApplicationKeys; }
    const SwQuicConnectionId& destinationConnectionId() const { return m_serverConnectionId; }
    const SwQuicConnectionId& sourceConnectionId() const { return m_sourceConnectionId; }

    // Generates the ephemeral key material and connection IDs, builds the TLS
    // ClientHello and the protected QUIC Initial datagram to send first.
    bool start(const SwString& serverName,
               SwByteArray& outInitialDatagram,
               SwString* error = nullptr) {
        m_serverName = serverName;

        SwByteArray privateScalar;
        if (!SwQuicRandom::fill(privateScalar, 32, error) ||
            !SwQuicRandom::fill(m_clientRandom, 32, error)) {
            return fail_(error);
        }
        m_clientPrivateKey = privateScalar;
        if (!SwQuicX25519::derivePublicKey(m_clientPrivateKey, m_clientPublicKey, error)) {
            return fail_(error);
        }

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
            // Resumption ClientHello with a PSK binder + early_data, and derive
            // the client early (0-RTT) keys (RFC 8446 4.2.11, RFC 9001 4.6).
            if (!SwQuicClientHelloBuilder::buildForHttp3Resumption(
                    serverName, m_sourceConnectionId, m_clientPublicKey, m_clientRandom,
                    m_resumptionTicket.ticket, m_resumptionTicket.ticketAgeAdd,
                    m_resumptionTicket.resumptionPsk, m_clientHelloMessage, m_earlyKeys,
                    error)) {
                return fail_(error);
            }
            m_hasEarlyKeys = true;
        } else if (!SwQuicClientHelloBuilder::buildForHttp3(serverName,
                                                            m_sourceConnectionId,
                                                            m_clientPublicKey,
                                                            m_clientRandom,
                                                            m_clientHelloMessage,
                                                            error)) {
            return fail_(error);
        }

        SwQuicClientInitialBuilder::Options options;
        options.serverName = serverName;
        options.destinationConnectionId = m_originalDestinationConnectionId;
        options.sourceConnectionId = m_sourceConnectionId;
        options.packetNumber = m_clientInitialPacketNumber;
        if (!SwQuicClientInitialBuilder::buildFromClientHello(options,
                                                              m_clientHelloMessage,
                                                              outInitialDatagram,
                                                              error)) {
            return fail_(error);
        }
        ++m_clientInitialPacketNumber;

        // Coalesce a 0-RTT packet carrying the early application data behind the
        // Initial, protected with the early keys.
        if (m_resuming && !m_earlyData.isEmpty()) {
            SwByteArray zeroRttPacket;
            if (!buildZeroRttPacket_(m_earlyData, zeroRttPacket, error)) {
                return fail_(error);
            }
            outInitialDatagram.append(zeroRttPacket);
        }

        m_state = State::WaitServerHello;
        clearError_(error);
        return true;
    }

    // Consumes one received UDP datagram (which may coalesce several QUIC packets)
    // and appends any datagrams that should be sent in response.
    bool processIncomingDatagram(const SwByteArray& datagram,
                                 std::vector<SwByteArray>& outDatagrams,
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
            const SwByteArray remaining =
                datagram.mid(static_cast<int>(offset), static_cast<int>(datagram.size() - offset));

            std::size_t consumed = 0;
            if (longType == 0x00U) {
                // RFC 9001 4.9.1: once Initial keys are discarded, Initial packets
                // are no longer processed. Stop scanning this datagram.
                if (m_initialKeysDiscarded) {
                    break;
                }
                if (!handleServerInitial_(remaining, consumed, error)) {
                    return fail_(error);
                }
            } else if (longType == 0x20U) {
                if (!handleServerHandshake_(remaining, consumed, error)) {
                    return fail_(error);
                }
            } else {
                // Retry (0x30) or 0-RTT (0x10): not handled in this client.
                setError_(error, "QUIC Retry or 0-RTT packet is not supported by this client");
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
        m_state = State::Failed;
        if (error && !error->isEmpty()) {
            m_error = *error;
        }
        return false;
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
        m_receivedInitialPacketNumbers.insert(header.packetNumber());
        m_pendingInitialAck = true;

        std::vector<SwQuicFrame> frames;
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

        std::vector<SwTls13Messages::HandshakeMessage> messages;
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
        if (serverHello.serverX25519Public.size() != 32) {
            setError_(error, "ServerHello does not carry an X25519 key share");
            return false;
        }

        // ECDHE and the handshake key schedule (transcript = ClientHello || ServerHello).
        SwByteArray ecdhe;
        if (!SwQuicX25519::computeSharedSecret(m_clientPrivateKey, serverHello.serverX25519Public,
                                               ecdhe, error)) {
            return false;
        }

        m_serverHelloMessage = rawMessage_(0x02, messages[0].body);
        SwByteArray transcriptChSh;
        transcriptChSh.append(m_clientHelloMessage);
        transcriptChSh.append(m_serverHelloMessage);
        const SwByteArray thChSh = SwTls13KeySchedule::transcriptHash(transcriptChSh);

        // Seed the Early-Secret with the PSK only if the server actually
        // selected it; otherwise the server ran a full handshake and we must
        // match with a zero Early-Secret (RFC 8446 7.1 / 2.2).
        SwByteArray earlySecret;
        const bool earlyOk = m_pskAccepted
            ? SwTls13KeySchedule::earlySecretWithPsk(m_resumptionTicket.resumptionPsk,
                                                     earlySecret, error)
            : SwTls13KeySchedule::earlySecret(earlySecret, error);
        if (!earlyOk ||
            !SwTls13KeySchedule::handshakeSecret(earlySecret, ecdhe, m_handshakeSecret, error) ||
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
        if (m_serverHandshakeTrafficSecret.isEmpty()) {
            // Handshake packet arrived (coalesced) before we processed the
            // ServerHello in this same datagram is impossible because Initial
            // precedes Handshake; if keys are missing we simply cannot read it yet.
            setError_(error, "Handshake packet received before handshake keys were installed");
            return false;
        }

        SwQuicPacketHeader header;
        SwByteArray payload;
        if (!SwQuicPacketProtector::unprotectHandshake(m_serverHandshakeKeys, packet, header, payload,
                                                       &consumed, error)) {
            return false;
        }
        m_receivedHandshakePacketNumbers.insert(header.packetNumber());
        m_pendingHandshakeAck = true;

        std::vector<SwQuicFrame> frames;
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
        return true;
    }

    // Build a coalesced 0-RTT (long header type 0x10) packet carrying the early
    // application data as a STREAM frame on stream 0, protected with the early
    // keys (RFC 9001 4.6).
    bool buildZeroRttPacket_(const SwByteArray& earlyData,
                             SwByteArray& outPacket,
                             SwString* error) {
        std::vector<SwQuicFrame> frames;
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

    bool tryCompleteHandshake_(std::vector<SwByteArray>& outDatagrams, SwString* error) {
        std::vector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(m_serverHandshakeCrypto, messages, error)) {
            clearError_(error);
            return true;  // wait for more Handshake CRYPTO
        }

        // We need EncryptedExtensions(8), Certificate(11), CertificateVerify(15)
        // and Finished(20). Locate the Finished; everything before it is the
        // transcript context for its verify_data.
        int finishedIndex = -1;
        int certificateIndex = -1;
        int certificateVerifyIndex = -1;
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].type == 0x14) {
                finishedIndex = static_cast<int>(i);
                break;
            }
            if (messages[i].type == 0x08) {
                if (!processEncryptedExtensions_(messages[i].body, error)) {
                    return false;
                }
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
            }
        }
        if (finishedIndex < 0) {
            clearError_(error);
            return true;  // Finished not received yet
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

        // Server authentication (PKI): chain to a trusted root with hostname
        // match, then the CertificateVerify signature over the transcript. On
        // PSK resumption the server authenticates via the PSK and sends no
        // Certificate/CertificateVerify (RFC 8446 2.2), so this is skipped.
        if (m_verifyPeer && !m_pskAccepted) {
            if (certificateIndex < 0 || certificateVerifyIndex < 0 ||
                m_serverCertificateChain.empty()) {
                setError_(error, "Server did not send a certificate chain to verify");
                return false;
            }
            if (m_rawPublicKeyVerifier) {
                // Confiance déléguée : la clé/cert présentée doit être acceptée (ex. appartenance
                // netmap). La chaîne PKI n'est PAS validée ; la preuve de possession l'est ci-dessous.
                if (!m_rawPublicKeyVerifier(m_serverCertificateDer)) {
                    setError_(error, "Server public key rejected by raw-public-key verifier");
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
            // CertificateVerify. We offer only these three.
            if (signatureScheme != 0x0403 && signatureScheme != 0x0804 &&
                signatureScheme != 0x0805) {
                setError_(error, "Server CertificateVerify uses a non-offered signature scheme");
                return false;
            }
            if (!SwQuicCertificateVerifier::verifyCertificateVerify(m_serverCertificateDer,
                                                                    signatureScheme,
                                                                    signature,
                                                                    thSignedByCertVerify,
                                                                    error)) {
                return false;
            }
        }

        // ALPN must have resolved to "h3": QUIC requires a negotiated
        // application protocol (RFC 9001 8.1 / RFC 7301 3.2). A missing or
        // mismatched ALPN is a fatal no_application_protocol.
        if (!(m_negotiatedAlpn == SwByteArray("h3"))) {
            setError_(error, "ALPN negotiation failed: server did not select h3");
            return false;
        }

        // Verify the server Finished MAC.
        SwByteArray serverVerifyData;
        if (!SwTls13Messages::parseFinishedVerifyData(messages[finishedIndex].body,
                                                      serverVerifyData, error)) {
            return false;
        }
        SwByteArray serverFinishedKey;
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

        // Transcript including the server Finished: drives the client Finished,
        // the client/server application (1-RTT) secrets.
        SwByteArray transcriptToServerFinished = transcriptToCertVerify;
        transcriptToServerFinished.append(rawMessage_(messages[finishedIndex].type,
                                                      messages[finishedIndex].body));
        const SwByteArray thToServerFinished =
            SwTls13KeySchedule::transcriptHash(transcriptToServerFinished);

        // 1-RTT application keys.
        SwByteArray masterSecret;
        if (!SwTls13KeySchedule::masterSecret(m_handshakeSecret, masterSecret, error) ||
            !SwTls13KeySchedule::clientApplicationTrafficSecret(masterSecret, thToServerFinished,
                                                               m_clientApplicationTrafficSecret, error) ||
            !SwTls13KeySchedule::serverApplicationTrafficSecret(masterSecret, thToServerFinished,
                                                               m_serverApplicationTrafficSecret, error) ||
            !SwQuicPacketKeys::deriveAes128(m_clientApplicationTrafficSecret, m_clientApplicationKeys, error) ||
            !SwQuicPacketKeys::deriveAes128(m_serverApplicationTrafficSecret, m_serverApplicationKeys, error)) {
            return false;
        }

        // Build the client Finished and send it in a Handshake packet.
        SwByteArray clientFinishedKey;
        SwByteArray clientVerifyData;
        if (!SwTls13KeySchedule::finishedKey(m_clientHandshakeTrafficSecret, clientFinishedKey, error) ||
            !SwTls13KeySchedule::verifyData(clientFinishedKey, thToServerFinished,
                                            clientVerifyData, error)) {
            return false;
        }
        const SwByteArray clientFinishedMessage = rawMessage_(0x14, clientVerifyData);

        // resumption_master_secret over the transcript through the client
        // Finished, for issuing/accepting session tickets (RFC 8446 7.1).
        SwByteArray transcriptToClientFinished = transcriptToServerFinished;
        transcriptToClientFinished.append(clientFinishedMessage);
        const SwByteArray thToClientFinished =
            SwTls13KeySchedule::transcriptHash(transcriptToClientFinished);
        if (!SwTls13KeySchedule::resumptionMasterSecret(masterSecret, thToClientFinished,
                                                        m_resumptionMasterSecret, error)) {
            return false;
        }

        SwByteArray handshakeDatagram;
        if (!buildClientHandshakeFlight_(clientFinishedMessage, handshakeDatagram, error)) {
            return false;
        }
        outDatagrams.push_back(handshakeDatagram);

        // RFC 9001 4.9.1: the client MUST discard its Initial keys as soon as it
        // first sends a Handshake packet (this flight). Drop the key material and
        // any pending Initial ACK so no further Initial packet is emitted or read.
        discardInitialKeys_();

        m_handshakeComplete = true;
        m_state = State::Complete;
        clearError_(error);
        return true;
    }

    // RFC 9001 4.9.1 / 4.9: zeroise the Initial secrets and stop all Initial-space
    // activity. After this, received Initial packets are ignored.
    void discardInitialKeys_() {
        m_initialKeysDiscarded = true;
        m_clientInitialKeys = SwQuicInitialKeys();
        m_serverInitialKeys = SwQuicInitialKeys();
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

    // Builds a datagram with the client's Handshake-level ACK + CRYPTO(Finished).
    bool buildClientHandshakeFlight_(const SwByteArray& clientFinishedMessage,
                                     SwByteArray& outDatagram,
                                     SwString* error) {
        std::vector<SwQuicFrame> frames;
        if (!m_receivedHandshakePacketNumbers.empty()) {
            const std::uint64_t largest = *m_receivedHandshakePacketNumbers.rbegin();
            frames.push_back(SwQuicFrame::ack(largest, 0, 0));
        }
        frames.push_back(SwQuicFrame::crypto(0, clientFinishedMessage));

        SwByteArray plaintext;
        if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) {
            return false;
        }

        const std::uint8_t pnLen = 2;
        const std::uint64_t protectedLength =
            static_cast<std::uint64_t>(plaintext.size() + SwQuicPacketProtector::kTagLength);
        SwByteArray header;
        if (!buildHandshakeHeader_(protectedLength, pnLen, m_clientHandshakePacketNumber,
                                   header, error)) {
            return false;
        }

        if (!SwQuicPacketProtector::protectLongHeader(m_clientHandshakeKeys,
                                                      m_clientHandshakePacketNumber,
                                                      pnLen,
                                                      header,
                                                      plaintext,
                                                      outDatagram,
                                                      error)) {
            return false;
        }
        ++m_clientHandshakePacketNumber;
        clearError_(error);
        return true;
    }

    // Builds a small ACK-only Initial or Handshake datagram acknowledging the
    // contiguous run of highest received packet numbers.
    bool buildAckOnlyDatagram_(bool handshakeLevel,
                               const SwQuicInitialKeys& keys,
                               std::uint64_t& packetNumberCounter,
                               const std::set<std::uint64_t>& received,
                               SwByteArray& outDatagram,
                               SwString* error) {
        const std::uint64_t largest = *received.rbegin();
        std::uint64_t low = largest;
        while (received.find(low - 1) != received.end() && low > 0) {
            --low;
        }
        std::vector<SwQuicFrame> frames;
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
    SwByteArray m_clientRandom;

    SwQuicConnectionId m_originalDestinationConnectionId;
    SwQuicConnectionId m_sourceConnectionId;
    SwQuicConnectionId m_serverConnectionId;

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
    std::function<bool(const SwByteArray&)> m_rawPublicKeyVerifier;
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
    bool m_hasPeerTransportParameters;
    SwByteArray m_negotiatedAlpn;
    std::vector<SwByteArray> m_serverCertificateChain;

    SwByteArray m_handshakeSecret;
    SwByteArray m_clientHandshakeTrafficSecret;
    SwByteArray m_serverHandshakeTrafficSecret;
    SwByteArray m_clientApplicationTrafficSecret;
    SwByteArray m_serverApplicationTrafficSecret;
    SwByteArray m_serverCertificateDer;

    std::uint64_t m_clientInitialPacketNumber;
    std::uint64_t m_clientHandshakePacketNumber;
    std::set<std::uint64_t> m_receivedInitialPacketNumbers;
    std::set<std::uint64_t> m_receivedHandshakePacketNumbers;
    bool m_pendingInitialAck = false;
    bool m_pendingHandshakeAck = false;
    bool m_initialKeysDiscarded = false;

    bool m_handshakeComplete;

public:
    // RFC 9001 4.9.1: a client discards its Initial keys as soon as it first sends
    // a Handshake packet. Exposed for tests validating that invariant.
    bool initialKeysDiscarded() const { return m_initialKeysDiscarded; }
};

#endif
