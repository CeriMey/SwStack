#ifndef SWQUICENDPOINT_H
#define SWQUICENDPOINT_H

// SwQuicEndpoint — GENERIC socket-agnostic QUIC engine for SwStack.
//
// This is the proven VIGIL-MESH-2B meshq::QuicEndpoint transport (real QUIC v1 +
// TLS 1.3 handshake via SwQuicHandshakeClient <-> SwQuicHandshakeServer, then a
// key HANDOFF into a sans-IO SwQuicConnection for the data plane) promoted to a
// native SwStack class over SwByteArray/SwString. It carries NO application
// semantics: no VIGIL, no mesh, no 0x3F, no ports. The engine never touches a
// socket — it emits through a send-sink installed by the owner (SwQuicSocket)
// and is fed received datagrams through onUdpPacket(). Demultiplexing of any
// non-QUIC traffic happens above this class, before onUdpPacket() is ever called.
//
// Auth transition: SwQuicAuthMode::RawPublicKey currently wires the
// verifyPeerKey hook onto X.509/SPKI pinning. SwQuic extracts canonical SPKI DER
// from the peer certificate and hands only that SPKI to the decision hook. The
// PKI chain is not validated and CertificateVerify still proves possession.
// RFC 7250 RawPublicKey negotiation is not implemented by this mode yet.
//
// SIGNAL-DRIVEN NOTIFICATION: this layer no longer notifies through std::function
// callbacks. SwQuicConnectionHandle and SwQuicEndpoint are SwObjects and raise
// SwObject signals (DECLARE_SIGNAL) — "data on the UDP -> a signal climbs the
// layers". A datagram deciphered on a connection EMITS datagramReceived(bytes);
// the handshake completing EMITS established(); a fresh server connection accepted
// by listen() EMITS connectionAccepted(conn). The ONLY std::function that remains
// is verifyPeerKey — it is a DECISION (returns bool), not a notification.

#include "SwMap.h"
#include "SwVector.h"
#include "SwObject.h"
#include "SwByteArray.h"
#include "SwString.h"

#include "quic/SwQuicConnection.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicHandshakeClient.h"
#include "quic/SwQuicHandshakeServer.h"
#include "quic/SwQuicServerCredential.h"
#include "quic/SwQuicStatelessReset.h"
#include "quic/SwQuicVarIntCodec.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

// ------------------------------------------------------------------- public types

enum class SwQuicAuthMode {
    RawPublicKey  // transitional X.509/SPKI pinning; RFC 7250 wire support pending
};

// Opaque identifier of the network path a datagram arrived on. Generic: the
// engine fills handle from the peer 4-tuple; isCarrier is left to the owner.
struct SwQuicPathHandle {
    std::uint64_t handle = 0;
    bool isCarrier = false;
};

// The only per-connection hook left is a DECISION, not a notification. Data,
// stream, and establishment events are delivered as SwObject signals instead.
struct SwQuicCallbacks {
    // Delegated trust decision on the peer's presented key/cert (SPKI DER).
    // Return true to accept. Client-side only.
    std::function<bool(const SwByteArray& spkiDer)> verifyPeerKey;
};

// ------------------------------------------------------------- connection handle

// One QUIC connection. Owns its handshake driver until the 1-RTT keys are handed
// off to an internal SwQuicConnection, then exposes the data-plane API.
//
// It is an SwObject: it does not call back: it EMITS. established() fires once the
// handshake hands off; datagramReceived(bytes) fires per deciphered application
// datagram; streamData(id, bytes) fires per contiguous stream read. Owners connect
// slots with SwObject::connect(conn.get(), &SwQuicConnectionHandle::signal, ...).
class SwQuicConnectionHandle : public SwObject {
    SW_OBJECT(SwQuicConnectionHandle, SwObject)

public:
    // Return false only when the owner could neither send nor retain the
    // datagram in a bounded backpressure queue.
    using SendSink = std::function<bool(const std::uint8_t* data, std::size_t len,
                                        const SwString& toAddr, std::uint16_t toPort)>;

    SwQuicConnectionHandle() = default;

signals:
    // Handshake handed off to the 1-RTT data plane: the connection is usable.
    DECLARE_SIGNAL_VOID(established);
    // One application QUIC DATAGRAM frame deciphered (RFC 9221) -> climbs as bytes.
    DECLARE_SIGNAL(datagramReceived, SwByteArray);
    // Contiguous stream bytes deciphered for a stream id.
    DECLARE_SIGNAL(streamData, std::uint64_t, SwByteArray);
    DECLARE_SIGNAL(errorOccurred, const SwString&);
    DECLARE_SIGNAL_VOID(closed);
    // Internal/publicly harmless notification used by the socket wrapper to
    // re-arm its one-shot timer when PTO/ACK state changes.
    DECLARE_SIGNAL_VOID(timerChanged);

public:
    // ---- fabrique client : arme le handshake (l'Initial part via startClient_) ----
    void initClient(SendSink sink, const SwString& host, std::uint16_t port,
                    const SwString& alpn, SwQuicAuthMode authMode, SwQuicCallbacks cbs) {
        m_sink = std::move(sink);
        m_peerAddr = host;
        m_peerPort = port;
        m_alpn = alpn;
        m_authMode = authMode;
        m_cbs = std::move(cbs);
        resetLifecycle_();
        m_role = SwQuicConnection::Role::Client;
        m_path.handle = std::hash<SwString>{}(addrKey_(host, port));
        m_path.isCarrier = false;

        m_hsClient.reset(new SwQuicHandshakeClient());
        if (!m_hsClient->setApplicationProtocol(
                SwByteArray(alpn.data(), static_cast<std::size_t>(alpn.size())))) {
            fail_(SwString("QUIC ALPN must contain 1..255 bytes"));
            return;
        }
        m_hsClient->setVerifyPeer(true);
        m_hsClient->setVerifyCertificateChain(false); // trust delegated to the key (RPK)
        if (m_cbs.verifyPeerKey) {
            std::function<bool(const SwByteArray&)> vp = m_cbs.verifyPeerKey;
            m_hsClient->setSubjectPublicKeyInfoVerifier(
                [vp](const SwByteArray& spkiDer) -> bool { return vp(spkiDer); });
        }
    }

    // Émet l'Initial ; la suite du handshake se déroule via handleIncoming().
    bool startClientHandshake() {
        if (m_failed) return false;
        if (!m_hsClient || !m_hsClient->hasSubjectPublicKeyInfoVerifier()) {
            fail_(SwString("QUIC client requires a peer-key verifier"));
            return false;
        }
        SwVector<SwByteArray> initialFlight;
        SwString err;
        if (!m_hsClient->start(m_peerAddr, initialFlight, &err)) {
            fail_(err);
            return false;
        }
        m_localCid = m_hsClient->sourceConnectionId();
        rememberHandshakeFlight_(initialFlight);
        if (!emit_(initialFlight)) {
            fail_(SwString("QUIC UDP send queue is unavailable or full"));
            return false;
        }
        timerChanged();
        return true;
    }

    // ---- fabrique serveur : un handshake serveur par nouvelle conn cliente ----
    // No accept callback: the owner (SwQuicEndpoint) connects a slot to this
    // connection's established() signal and re-emits connectionAccepted(conn).
    void initServer(SendSink sink, const SwString& fromAddr, std::uint16_t fromPort,
                    const SwString& alpn, SwQuicAuthMode authMode,
                    const SwQuicServerCredential& credential,
                    SwQuicHandshakeServer::StatelessResetTokenProvider resetTokenProvider =
                        SwQuicHandshakeServer::StatelessResetTokenProvider()) {
        m_sink = std::move(sink);
        m_peerAddr = fromAddr;
        m_peerPort = fromPort;
        m_alpn = alpn;
        m_authMode = authMode;
        resetLifecycle_();
        m_role = SwQuicConnection::Role::Server;
        m_path.handle = std::hash<SwString>{}(addrKey_(fromAddr, fromPort));
        m_path.isCarrier = false;

        m_hsServer.reset(new SwQuicHandshakeServer());
        if (!m_hsServer->setApplicationProtocol(
                SwByteArray(alpn.data(), static_cast<std::size_t>(alpn.size())))) {
            fail_(SwString("QUIC ALPN must contain 1..255 bytes"));
            return;
        }
        m_hsServer->setCredential(credential);
        if (resetTokenProvider) {
            (void)m_hsServer->setStatelessResetTokenProvider(
                std::move(resetTokenProvider));
        }
    }

    bool hasLocalCid() const { return !m_localCid.isEmpty(); }
    const SwQuicConnectionId& localCid() const { return m_localCid; }

    // Route un datagramme entrant : driver de handshake tant que non établi, puis
    // le SwQuicConnection après handoff.
    void handleIncoming(const SwByteArray& datagram) {
        handleIncoming(datagram, m_peerAddr, m_peerPort);
    }

    void handleIncoming(const SwByteArray& datagram,
                        const SwString& fromAddr,
                        std::uint16_t fromPort) {
        if (m_failed || isClosed()) return;
        if (!m_established) {
            // QUIC endpoints cannot migrate during the handshake. The CID is
            // visible on the wire, so accepting a different source tuple here
            // would let a spoofed packet perturb or redirect handshake state.
            if (fromAddr != m_peerAddr || fromPort != m_peerPort) return;
            driveHandshake_(datagram);
        } else {
            deliverEstablished_(datagram, fromAddr, fromPort);
        }
    }

    void abort(const SwString& reason) { fail_(reason); }

    // Fait avancer les timers (PTO/idle/ACK) puis flush. No-op tant que non établi.
    void onTick() {
        const std::uint64_t now = nowMs_();
        if (!m_established) {
            if (!m_failed && m_handshakePtoDeadlineMs > 0 &&
                now >= m_handshakePtoDeadlineMs && !m_lastHandshakeFlight.empty()) {
                if (!emit_(m_lastHandshakeFlight)) {
                    fail_(SwString("QUIC UDP send queue is unavailable or full"));
                    return;
                }
                if (m_handshakePtoCount < 16) ++m_handshakePtoCount;
                armHandshakePto_(now);
                timerChanged();
            }
            return;
        }
        if (!m_conn) return;
        if (!serviceCandidateTimer_(now)) {
            return;
        }
        if (m_conn->nextTimeoutMs(now) == 0) {
            m_conn->onTimeout(now);
        }
        flush_();
        emitClosedIfNeeded_();
    }

    // ------------------------------------------------------- data-plane API ----

    bool isEstablished() const { return m_established; }
    bool isFailed() const { return m_failed; }
    bool isClosed() const {
        return m_failed || (m_conn && m_conn->state() == SwQuicConnection::State::Closed);
    }
    const SwString& errorString() const { return m_error; }
    std::uint64_t createdMs() const { return m_createdMs; }
    std::uint64_t lastActivityMs() const { return m_lastActivityMs; }
    std::int64_t nextTimeoutMs(std::uint64_t now) const {
        if (m_failed) return -1;
        if (!m_established) {
            if (m_handshakePtoDeadlineMs == 0) return -1;
            return m_handshakePtoDeadlineMs <= now
                ? 0
                : static_cast<std::int64_t>(m_handshakePtoDeadlineMs - now);
        }
        std::int64_t next = m_conn ? m_conn->nextTimeoutMs(now) : -1;
        if (m_candidateActive) {
            const std::uint64_t deadline =
                (std::min)(m_candidateRetryDeadlineMs, m_candidateExpiryMs);
            const std::int64_t candidate = deadline <= now
                                               ? 0
                                               : static_cast<std::int64_t>(deadline - now);
            if (next < 0 || candidate < next) {
                next = candidate;
            }
        }
        return next;
    }
    const SwQuicPathHandle& path() const { return m_path; }
    const SwString& peerAddress() const { return m_peerAddr; }
    std::uint16_t peerPort() const { return m_peerPort; }

    void setCallbacks(SwQuicCallbacks cbs) { m_cbs = std::move(cbs); }

    bool sendDatagram(const std::uint8_t* data, std::size_t len) {
        if (!m_established || !m_conn) return false;
        SwString err;
        if (!m_conn->queueDatagramFrame(toSwBytes_(data, len), &err)) return false;
        const bool flushed = flush_();
        timerChanged();
        return flushed;
    }
    bool sendDatagram(const SwByteArray& data) {
        return sendDatagram(reinterpret_cast<const std::uint8_t*>(data.constData()),
                            static_cast<std::size_t>(data.size()));
    }

    std::uint64_t openStream(std::uint64_t type, bool bidi) {
        if (!m_conn) return 0;
        const std::uint64_t id = allocateStreamId_(bidi);
        SwByteArray prefix;
        SwString err;
        SwQuicVarIntCodec::encode(type, prefix, &err); // 1st varint of the stream = service type
        m_conn->sendStreamData(id, prefix, false, &err);
        flush_();
        return id;
    }

    bool sendStream(std::uint64_t streamId, const std::uint8_t* data, std::size_t len, bool fin) {
        if (!m_established || !m_conn) return false;
        SwString err;
        if (!m_conn->sendStreamData(streamId, toSwBytes_(data, len), fin, &err)) return false;
        const bool flushed = flush_();
        timerChanged();
        return flushed;
    }

    void resetStream(std::uint64_t streamId, std::uint64_t appErrorCode) {
        if (!m_conn) return;
        m_conn->queueFrame(SwQuicConnection::Level::Application,
                           SwQuicFrame::resetStream(streamId, appErrorCode, 0));
        flush_();
    }

    SwVector<std::uint8_t> exporter(const SwString& label, std::size_t len) const {
        if (!m_established) return SwVector<std::uint8_t>();
        SwByteArray material;
        SwString error;
        const SwByteArray emptyContext(static_cast<std::size_t>(0), '\0');
        const bool ok = m_role == SwQuicConnection::Role::Client
            ? m_hsClient && m_hsClient->exportKeyingMaterial(
                  label, emptyContext, len, material, &error)
            : m_hsServer && m_hsServer->exportKeyingMaterial(
                  label, emptyContext, len, material, &error);
        if (!ok || material.size() != len || (len != 0 && !material.constData())) {
            return SwVector<std::uint8_t>();
        }
        if (len == 0) return SwVector<std::uint8_t>();
        return SwVector<std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(material.constData()),
            reinterpret_cast<const std::uint8_t*>(material.constData()) + material.size());
    }

    void close(std::uint64_t appErrorCode) {
        if (m_conn) {
            m_conn->close(appErrorCode, SwString("closed"), true);
            flush_();
            timerChanged();
        }
    }

private:
    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static SwString addrKey_(const SwString& addr, std::uint16_t port) {
        return addr + ":" + SwString::number(static_cast<unsigned int>(port));
    }

    static SwByteArray toSwBytes_(const std::uint8_t* data, std::size_t len) {
        if (!data || len == 0) return SwByteArray();
        return SwByteArray(reinterpret_cast<const char*>(data), len);
    }

    void resetLifecycle_() {
        m_established = false;
        m_failed = false;
        m_closedEmitted = false;
        m_error = SwString();
        m_createdMs = nowMs_();
        m_lastActivityMs = m_createdMs;
        m_handshakePtoCount = 0;
        m_handshakePtoDeadlineMs = 0;
        m_lastHandshakeFlight.clear();
        m_conn.reset();
        m_localCid = SwQuicConnectionId();
        clearCandidatePath_(false);
    }

    void fail_(const SwString& error) {
        if (m_failed) return;
        m_failed = true;
        m_error = error.isEmpty() ? SwString("QUIC connection failed") : error;
        errorOccurred(m_error);
        timerChanged();
    }

    void armHandshakePto_(std::uint64_t now) {
        std::uint64_t delay = 500;
        const std::size_t shifts = m_handshakePtoCount < 5 ? m_handshakePtoCount : 5;
        delay <<= shifts;
        m_handshakePtoDeadlineMs = now + delay;
    }

    void rememberHandshakeFlight_(const SwVector<SwByteArray>& out) {
        if (out.empty()) return;
        m_lastHandshakeFlight = out;
        m_handshakePtoCount = 0;
        armHandshakePto_(nowMs_());
    }

    bool emit_(const SwVector<SwByteArray>& out) {
        if (!m_sink) return out.empty();
        for (std::size_t i = 0; i < out.size(); ++i) {
            const SwByteArray& d = out[i];
            if (d.isEmpty() || !d.constData()) continue;
            if (!m_sink(reinterpret_cast<const std::uint8_t*>(d.constData()),
                        static_cast<std::size_t>(d.size()), m_peerAddr, m_peerPort)) {
                return false;
            }
        }
        return true;
    }

    void driveHandshake_(const SwByteArray& datagram) {
        SwString err;
        SwVector<SwByteArray> out;
        if (m_role == SwQuicConnection::Role::Client) {
            if (!m_hsClient->processIncomingDatagram(datagram, out, &err)) {
                fail_(err);
                return;
            }
            m_lastActivityMs = nowMs_();
            rememberHandshakeFlight_(out);
            if (!emit_(out)) {
                fail_(SwString("QUIC UDP send queue is unavailable or full"));
                return;
            }
            if (m_hsClient->handshakeComplete() && !m_established) {
                handoffClient_();
            }
        } else {
            if (!m_hsServer->processIncomingDatagram(datagram, out, &err)) {
                fail_(err);
                return;
            }
            m_lastActivityMs = nowMs_();
            if (m_localCid.isEmpty() && !m_hsServer->serverConnectionId().isEmpty()) {
                m_localCid = m_hsServer->serverConnectionId();
            }
            rememberHandshakeFlight_(out);
            if (!emit_(out)) { // includes Handshake ACK + HANDSHAKE_DONE
                fail_(SwString("QUIC UDP send queue is unavailable or full"));
                return;
            }
            if (m_hsServer->handshakeComplete() && !m_established) {
                handoffServer_();
            }
        }
        timerChanged();
    }

    void handoffClient_() {
        m_conn.reset(new SwQuicConnection(SwQuicConnection::Role::Client));
        m_conn->applyLocalTransportParameters(m_hsClient->localTransportParameters());
        if (m_hsClient->hasPeerTransportParameters()) {
            m_conn->applyPeerTransportParameters(m_hsClient->peerTransportParameters());
        }
        m_conn->setLocalConnectionId(m_hsClient->sourceConnectionId());
        m_conn->setPeerConnectionId(m_hsClient->destinationConnectionId());
        m_conn->setLevelKeys(SwQuicConnection::Level::Application,
                             m_hsClient->serverApplicationKeys(),
                             m_hsClient->clientApplicationKeys());
        m_conn->setHandshakeConfirmed(true);
        m_conn->setNextTxPacketNumber(SwQuicConnection::Level::Application,
                                      m_hsClient->clientEarlyPacketNumber());
        m_localCid = m_hsClient->sourceConnectionId();
        m_established = true;
        m_lastHandshakeFlight.clear();
        m_handshakePtoDeadlineMs = 0;
        established(); // SIGNAL: data plane ready (climbs to the owner's slot)
        timerChanged();
    }

    void handoffServer_() {
        m_conn.reset(new SwQuicConnection(SwQuicConnection::Role::Server));
        m_conn->applyLocalTransportParameters(m_hsServer->localTransportParameters());
        if (m_hsServer->hasPeerTransportParameters()) {
            m_conn->applyPeerTransportParameters(m_hsServer->peerTransportParameters());
        }
        m_conn->setLocalConnectionId(m_hsServer->serverConnectionId());
        m_conn->setPeerConnectionId(m_hsServer->clientConnectionId());
        m_conn->setLevelKeys(SwQuicConnection::Level::Application,
                             m_hsServer->clientApplicationKeys(),
                             m_hsServer->serverApplicationKeys());
        m_conn->setHandshakeConfirmed(true);
        m_conn->setNextTxPacketNumber(SwQuicConnection::Level::Application,
                                      m_hsServer->serverApplicationPacketNumber());
        m_localCid = m_hsServer->serverConnectionId();
        m_established = true;
        m_lastHandshakeFlight.clear();
        m_handshakePtoDeadlineMs = 0;
        established(); // SIGNAL: server side accepted -> owner re-emits connectionAccepted
        timerChanged();
    }

    void deliverEstablished_(const SwByteArray& datagram,
                             const SwString& fromAddr,
                             std::uint16_t fromPort) {
        SwString err;
        const std::uint64_t now = nowMs_();
        SwQuicConnection::PathControlEvents pathEvents;
        bool authenticated = false;
        std::uint64_t authenticatedBytes = 0;
        if (!m_conn->receiveDatagramWithPathEvents(datagram, now, pathEvents,
                                                   &err, &authenticated,
                                                   &authenticatedBytes)) {
            fail_(err);
            return;
        }
        if (!authenticated) return;
        m_lastActivityMs = now;

        const bool activePath = fromAddr == m_peerAddr && fromPort == m_peerPort;
        if (!activePath) {
            if (!processCandidatePath_(pathEvents, authenticatedBytes,
                                       fromAddr, fromPort, now)) {
                return;
            }
        } else {
            for (std::size_t i = 0; i < pathEvents.challenges.size(); ++i) {
                std::size_t ignored = 0;
                if (!sendPathControlTo_(true, pathEvents.challenges[i],
                                        fromAddr, fromPort,
                                        (std::numeric_limits<std::uint64_t>::max)(),
                                        now, ignored)) {
                    return;
                }
            }
        }

        while (m_conn->pendingDatagramCount() > 0) {
            SwByteArray dg = m_conn->takeDatagram();
            datagramReceived(std::move(dg)); // SIGNAL: application datagram climbs as bytes
        }

        const SwVector<std::uint64_t> ids = m_conn->takeTouchedStreamIds();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint64_t id = ids[i];
            SwByteArray sd = m_conn->readStream(id);
            if (!sd.isEmpty()) {
                streamData(id, std::move(sd)); // SIGNAL: contiguous stream bytes climb
            }
        }

        flush_(); // ACKs / MAX_DATA use the currently validated active tuple
        emitClosedIfNeeded_();
        timerChanged();
    }

    static std::uint64_t saturatingTriple_(std::uint64_t value) {
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        return value > maximum / 3 ? maximum : value * 3;
    }

    static void saturatingAdd_(std::uint64_t& target, std::uint64_t value) {
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        target = value > maximum - target ? maximum : target + value;
    }

    std::uint64_t candidateSendBudget_() const {
        const std::uint64_t permitted = saturatingTriple_(m_candidateBytesReceived);
        return m_candidateBytesSent >= permitted ? 0 : permitted - m_candidateBytesSent;
    }

    bool sendPathControlTo_(bool response,
                            const SwByteArray& data,
                            const SwString& address,
                            std::uint16_t port,
                            std::uint64_t maxWireBytes,
                            std::uint64_t now,
                            std::size_t& sentBytes) {
        sentBytes = 0;
        SwString err;
        SwByteArray packet;
        if (!m_conn->buildPathControlDatagram(response, data, now, maxWireBytes,
                                              packet, &err)) {
            fail_(err);
            return false;
        }
        if (packet.isEmpty()) {
            return true;
        }
        if (!m_sink ||
            !m_sink(reinterpret_cast<const std::uint8_t*>(packet.constData()),
                    static_cast<std::size_t>(packet.size()), address, port)) {
            fail_(SwString("QUIC UDP send queue is unavailable or full"));
            return false;
        }
        sentBytes = static_cast<std::size_t>(packet.size());
        return true;
    }

    bool sendCandidatePathControl_(bool response,
                                   const SwByteArray& data,
                                   std::uint64_t now) {
        if (!m_candidateActive) return true;
        std::size_t sent = 0;
        if (!sendPathControlTo_(response, data, m_candidateAddr, m_candidatePort,
                                candidateSendBudget_(), now, sent)) {
            return false;
        }
        saturatingAdd_(m_candidateBytesSent, static_cast<std::uint64_t>(sent));
        return true;
    }

    bool sendCandidateChallenge_(std::uint64_t now) {
        if (!m_candidateActive || now < m_candidateRetryDeadlineMs) return true;
        if (m_candidateChallengeAttempts >= 3 || now >= m_candidateExpiryMs) {
            clearCandidatePath_(true);
            return true;
        }
        const std::uint64_t sentBefore = m_candidateBytesSent;
        if (!sendCandidatePathControl_(false, m_candidateChallenge, now)) {
            return false;
        }
        if (m_candidateBytesSent != sentBefore) {
            ++m_candidateChallengeAttempts;
        }
        m_candidateRetryDeadlineMs = now + (250ULL << m_candidateChallengeAttempts);
        return true;
    }

    bool processCandidatePath_(const SwQuicConnection::PathControlEvents& events,
                               std::uint64_t authenticatedBytes,
                               const SwString& address,
                               std::uint16_t port,
                               std::uint64_t now) {
        SwString err;
        if (!m_candidateActive || address != m_candidateAddr || port != m_candidatePort) {
            clearCandidatePath_(true);
            m_candidateActive = true;
            m_candidateAddr = address;
            m_candidatePort = port;
            m_candidateBytesReceived = authenticatedBytes;
            m_candidateStartedMs = now;
            m_candidateRetryDeadlineMs = now;
            m_candidateExpiryMs = now + 3000;
            if (!m_conn->beginPathValidation(now, m_candidateChallenge, &err)) {
                clearCandidatePath_(false);
                fail_(err);
                return false;
            }
        } else {
            saturatingAdd_(m_candidateBytesReceived, authenticatedBytes);
        }

        if (!sendCandidateChallenge_(now)) return false;
        for (std::size_t i = 0; i < events.challenges.size(); ++i) {
            if (!sendCandidatePathControl_(true, events.challenges[i], now)) {
                return false;
            }
        }
        for (std::size_t i = 0; i < events.responses.size(); ++i) {
            if (!m_conn->matchesPathResponse(events.responses[i])) continue;
            m_conn->commitPathMigration();
            m_peerAddr = address;
            m_peerPort = port;
            m_path.handle = std::hash<SwString>{}(addrKey_(address, port));
            clearCandidatePath_(false);
            break;
        }
        return true;
    }

    bool serviceCandidateTimer_(std::uint64_t now) {
        if (!m_candidateActive) return true;
        if (now >= m_candidateExpiryMs) {
            clearCandidatePath_(true);
            return true;
        }
        return sendCandidateChallenge_(now);
    }

    void clearCandidatePath_(bool cancelConnectionValidation) {
        if (cancelConnectionValidation && m_conn) {
            m_conn->cancelPathValidation();
        }
        m_candidateActive = false;
        m_candidateAddr.clear();
        m_candidatePort = 0;
        m_candidateChallenge.clear();
        m_candidateBytesReceived = 0;
        m_candidateBytesSent = 0;
        m_candidateStartedMs = 0;
        m_candidateRetryDeadlineMs = 0;
        m_candidateExpiryMs = 0;
        m_candidateChallengeAttempts = 0;
    }

    bool flush_() {
        if (!m_conn) return false;
        SwString err;
        SwVector<SwByteArray> out;
        if (!m_conn->buildDatagrams(nowMs_(), out, &err)) {
            fail_(err);
            return false;
        }
        if (!emit_(out)) {
            fail_(SwString("QUIC UDP send queue is unavailable or full"));
            return false;
        }
        return true;
    }

    void emitClosedIfNeeded_() {
        if (!m_closedEmitted && m_conn &&
            m_conn->state() == SwQuicConnection::State::Closed) {
            m_closedEmitted = true;
            closed();
            timerChanged();
        }
    }

    std::uint64_t allocateStreamId_(bool bidi) {
        const std::uint64_t roleBit = (m_role == SwQuicConnection::Role::Client) ? 0U : 1U;
        const std::uint64_t dirBit = bidi ? 0U : 2U;
        std::uint64_t& counter = bidi ? m_nextBidiStream : m_nextUniStream;
        const std::uint64_t id = (counter << 2) | dirBit | roleBit;
        ++counter;
        return id;
    }

    SendSink m_sink;
    SwString m_peerAddr;
    std::uint16_t m_peerPort = 0;
    SwString m_alpn;
    SwQuicAuthMode m_authMode = SwQuicAuthMode::RawPublicKey;
    SwQuicCallbacks m_cbs;
    SwQuicPathHandle m_path;
    bool m_candidateActive = false;
    SwString m_candidateAddr;
    std::uint16_t m_candidatePort = 0;
    SwByteArray m_candidateChallenge;
    std::uint64_t m_candidateBytesReceived = 0;
    std::uint64_t m_candidateBytesSent = 0;
    std::uint64_t m_candidateStartedMs = 0;
    std::uint64_t m_candidateRetryDeadlineMs = 0;
    std::uint64_t m_candidateExpiryMs = 0;
    std::size_t m_candidateChallengeAttempts = 0;

    SwQuicConnection::Role m_role = SwQuicConnection::Role::Client;
    bool m_established = false;
    bool m_failed = false;
    bool m_closedEmitted = false;
    SwString m_error;
    std::uint64_t m_createdMs = 0;
    std::uint64_t m_lastActivityMs = 0;
    std::size_t m_handshakePtoCount = 0;
    std::uint64_t m_handshakePtoDeadlineMs = 0;
    SwVector<SwByteArray> m_lastHandshakeFlight;

    std::unique_ptr<SwQuicHandshakeClient> m_hsClient;
    std::unique_ptr<SwQuicHandshakeServer> m_hsServer;
    std::unique_ptr<SwQuicConnection> m_conn;
    SwQuicConnectionId m_localCid;

    std::uint64_t m_nextBidiStream = 0;
    std::uint64_t m_nextUniStream = 0;
};

// ---------------------------------------------------------------------- endpoint

// The generic QUIC engine. Routes incoming datagrams to their connection by peer
// 4-tuple then by destination connection ID (survives migration), and mints a
// server-role connection for a fresh Initial while listening.
//
// It is an SwObject. When listen() accepts a fresh server connection (its
// handshake completes) it EMITS connectionAccepted(conn); the owner connects a
// slot to wire that connection's own datagramReceived/streamData signals.
class SwQuicEndpoint : public SwObject {
    SW_OBJECT(SwQuicEndpoint, SwObject)

public:
    using SendSink = SwQuicConnectionHandle::SendSink;

    explicit SwQuicEndpoint(SwObject* parent = nullptr) : SwObject(parent) {}

signals:
    // A fresh inbound connection finished its handshake and is ready.
    DECLARE_SIGNAL(connectionAccepted, std::shared_ptr<SwQuicConnectionHandle>);
    DECLARE_SIGNAL(connectionRejected, const SwString&, std::uint16_t, const SwString&);
    DECLARE_SIGNAL_VOID(timerDeadlineChanged);

public:
    void setSendSink(SendSink sink) { m_sink = std::move(sink); }
    void setMaxConnections(std::size_t maximum) { m_maxConnections = maximum; }
    void setMaxPendingHandshakes(std::size_t maximum) {
        m_maxPendingHandshakes = maximum;
    }
    void setHandshakeTimeoutMs(std::uint64_t timeoutMs) {
        m_handshakeTimeoutMs = timeoutMs;
    }
    std::size_t connectionCount() const { return m_connections.size(); }

    // Delegated-trust DECISION hook (RFC 7250). Stays a std::function because it
    // returns bool — it is NOT a notification. Applied to every client connection.
    void setVerifyPeerKey(std::function<bool(const SwByteArray& spkiDer)> verifier) {
        m_verifyPeerKey = std::move(verifier);
    }

    std::shared_ptr<SwQuicConnectionHandle> connect(const SwString& host, std::uint16_t port,
                                                    const SwString& alpn,
                                                    SwQuicAuthMode authMode) {
        purgeExpired_(nowMs_());
        if (alpn.empty() || alpn.size() > 255) {
            connectionRejected(host, port, SwString("QUIC ALPN must contain 1..255 bytes"));
            return std::shared_ptr<SwQuicConnectionHandle>();
        }
        if (m_maxConnections > 0 && m_connections.size() >= m_maxConnections) {
            connectionRejected(host, port, SwString("QUIC connection limit reached"));
            return std::shared_ptr<SwQuicConnectionHandle>();
        }
        std::shared_ptr<SwQuicConnectionHandle> conn = std::make_shared<SwQuicConnectionHandle>();
        wireConnection_(conn, false);
        SwQuicCallbacks cbs;
        cbs.verifyPeerKey = m_verifyPeerKey; // the only surviving hook (a decision)
        conn->initClient(m_sink, host, port, alpn, authMode, std::move(cbs));
        const SwString peerKey = addrKey_(host, port);
        m_byPeer[peerKey] = conn;
        m_connections.push_back(conn);
        if (!conn->startClientHandshake()) {
            m_byPeer.erase(peerKey);
            purgeExpired_(nowMs_());
            return std::shared_ptr<SwQuicConnectionHandle>();
        }
        registerCid_(conn);
        timerDeadlineChanged();
        return conn;
    }

    void listen(const SwString& alpn, SwQuicAuthMode authMode) {
        m_listening = true;
        m_listenAlpn = alpn;
        m_listenAuth = authMode;
        if (alpn.empty() || alpn.size() > 255) {
            connectionRejected(SwString(), 0,
                               SwString("QUIC ALPN must contain 1..255 bytes"));
            m_listening = false;
            return;
        }
        SwString err;
        if (!SwQuicEcdsaCredential::createSelfSigned(
                SwString("swquic.node"), m_credential, &err)) {
            connectionRejected(SwString(), 0, err);
            m_listening = false;
        }
    }

    // Feed one received UDP datagram already classified as QUIC by the owner.
    void onUdpPacket(const std::uint8_t* data, std::size_t len,
                     const SwString& fromAddr, std::uint16_t fromPort) {
        const SwByteArray dg = toSwBytes_(data, len);
        onUdpPacket(dg, fromAddr, fromPort);
    }

    // Owning socket adapters already hold an SwByteArray. Keep that storage
    // through header parsing/AEAD instead of copying the datagram a second time.
    void onUdpPacket(const SwByteArray& dg,
                     const SwString& fromAddr, std::uint16_t fromPort) {
        if (dg.isEmpty()) return;
        purgeExpired_(nowMs_());

        const SwString peerK = addrKey_(fromAddr, fromPort);
        SwString dcid;
        if (extractDcid_(dg, dcid)) {
            SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator ci =
                m_byCid.find(dcid);
            if (ci != m_byCid.end()) {
                ci->second->handleIncoming(dg, fromAddr, fromPort);
                registerCid_(ci->second);
                registerPeer_(ci->second);
                timerDeadlineChanged();
                return;
            }
        }

        // A tuple fallback is only for packets whose CID cannot be extracted.
        // Never let it override CID routing: multiple connections can share one
        // UDP 4-tuple.
        if (dcid.empty()) {
            SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
                m_byPeer.find(peerK);
            if (it != m_byPeer.end()) {
                it->second->handleIncoming(dg, fromAddr, fromPort);
                registerCid_(it->second);
                timerDeadlineChanged();
                return;
            }
        }

        // Allocate state only for a structurally valid, minimum-sized Initial.
        if (m_listening && isValidInitial_(dg)) {
            if ((m_maxConnections > 0 && m_connections.size() >= m_maxConnections) ||
                (m_maxPendingHandshakes > 0 &&
                 pendingHandshakeCount_() >= m_maxPendingHandshakes)) {
                connectionRejected(fromAddr, fromPort,
                                   SwString("QUIC pending-handshake limit reached"));
                return;
            }
            std::shared_ptr<SwQuicConnectionHandle> conn =
                std::make_shared<SwQuicConnectionHandle>();
            wireConnection_(conn, true);
            conn->initServer(m_sink, fromAddr, fromPort, m_listenAlpn, m_listenAuth,
                             m_credential,
                             [this](const SwByteArray& cidBytes) {
                                 return statelessResetTokenForCid(cidBytes);
                             });
            m_byPeer[peerK] = conn;
            m_connections.push_back(conn);
            // The client's original DCID remains a routing alias for Initial
            // retransmissions until this connection is purged.
            if (!dcid.empty()) m_byCid[dcid] = conn;
            conn->handleIncoming(dg, fromAddr, fromPort);
            registerCid_(conn);
            timerDeadlineChanged();
            return;
        }

        // 4) Paquet non routable. Un short header d'assez de taille recoit un
        // Stateless Reset (RFC 9000 10.3) : sans lui, un pair dont on a purge
        // l'etat (idle timeout) retransmet dans le vide jusqu'a son propre
        // timeout. Long headers et miettes restent droppes fail-closed.
        maybeSendStatelessReset_(dg, dcid, fromAddr, fromPort);
    }

    // Jeton statique du CID : deterministe pour toute la vie du process, donc
    // valable meme apres la purge de la connexion qui a emis ce CID.
    SwByteArray statelessResetTokenForCid(const SwByteArray& cidBytes) const {
        return SwQuicStatelessReset::deriveToken(m_statelessResetKey, cidBytes);
    }

    // Purge test/ops : oublie toutes les connexions sans le moindre paquet de
    // fermeture — simule exactement la perte d'etat (idle purge, restart).
    std::size_t dropAllConnectionsSilently() {
        const std::size_t dropped = m_connections.size();
        m_byPeer.clear();
        m_byCid.clear();
        m_connections.clear();
        timerDeadlineChanged();
        return dropped;
    }

    void onTick() {
        const std::uint64_t now = nowMs_();
        for (std::size_t i = 0; i < m_connections.size(); ++i) {
            if (m_connections[i]->nextTimeoutMs(now) == 0) {
                m_connections[i]->onTick();
            }
        }
        purgeExpired_(nowMs_());
        timerDeadlineChanged();
    }

    std::int64_t nextTimeoutMs() const {
        const std::uint64_t now = nowMs_();
        std::int64_t next = -1;
        for (std::size_t i = 0; i < m_connections.size(); ++i) {
            const std::shared_ptr<SwQuicConnectionHandle>& conn = m_connections[i];
            std::int64_t candidate = conn->nextTimeoutMs(now);
            if (!conn->isEstablished() && m_handshakeTimeoutMs > 0) {
                const std::uint64_t deadline = conn->createdMs() + m_handshakeTimeoutMs;
                const std::int64_t expiry = deadline <= now
                    ? 0
                    : static_cast<std::int64_t>(deadline - now);
                if (candidate < 0 || expiry < candidate) candidate = expiry;
            }
            if (candidate >= 0 && (next < 0 || candidate < next)) next = candidate;
        }
        return next;
    }

private:
    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static SwString addrKey_(const SwString& addr, std::uint16_t port) {
        return addr + ":" + SwString::number(static_cast<unsigned int>(port));
    }

    static SwString cidKey_(const SwQuicConnectionId& cid) {
        const SwByteArray& b = cid.bytes();
        return SwString(b.constData() ? b.constData() : "", static_cast<std::size_t>(b.size()));
    }

    static SwByteArray toSwBytes_(const std::uint8_t* data, std::size_t len) {
        if (!data || len == 0) return SwByteArray();
        return SwByteArray(reinterpret_cast<const char*>(data), len);
    }

    // Extract the DCID (the CID the peer put in destination = OUR local CID) to
    // route the inbound even if the source address changed (migration).
    static bool extractDcid_(const SwByteArray& dg, SwString& outDcid) {
        if (dg.isEmpty()) return false;
        const std::uint8_t first = static_cast<std::uint8_t>(dg.constData()[0]);
        if ((first & 0x80U) != 0) { // long header: byte0(1) version(4) dcidLen(1) dcid(...)
            if (dg.size() < 6) return false;
            const std::size_t dcidLen = static_cast<std::uint8_t>(dg.constData()[5]);
            if (static_cast<std::size_t>(dg.size()) < 6 + dcidLen) return false;
            outDcid.assign(dg.constData() + 6, dcidLen);
            return true;
        }
        // short header (1-RTT): DCID = the 8 bytes after the header byte.
        if (dg.size() < 1 + 8) return false;
        outDcid.assign(dg.constData() + 1, 8);
        return true;
    }

    static bool isValidInitial_(const SwByteArray& dg) {
        if (dg.size() < 1200) return false;
        const std::uint8_t first = static_cast<std::uint8_t>(dg.constData()[0]);
        if ((first & 0xc0U) != 0xc0U || (first & 0x30U) != 0x00U) return false;
        if (static_cast<std::uint8_t>(dg.constData()[1]) != 0 ||
            static_cast<std::uint8_t>(dg.constData()[2]) != 0 ||
            static_cast<std::uint8_t>(dg.constData()[3]) != 0 ||
            static_cast<std::uint8_t>(dg.constData()[4]) != 1) return false;
        const std::size_t dcidLength = static_cast<std::uint8_t>(dg.constData()[5]);
        if (dcidLength < 8 || dcidLength > SwQuicConnectionId::kMaxLength ||
            static_cast<std::size_t>(dg.size()) < 7 + dcidLength) return false;
        const std::size_t scidLengthOffset = 6 + dcidLength;
        const std::size_t scidLength =
            static_cast<std::uint8_t>(dg.constData()[scidLengthOffset]);
        return scidLength <= SwQuicConnectionId::kMaxLength &&
               static_cast<std::size_t>(dg.size()) >= 7 + dcidLength + scidLength;
    }

    void wireConnection_(const std::shared_ptr<SwQuicConnectionHandle>& conn,
                         bool acceptedConnection) {
        std::weak_ptr<SwQuicConnectionHandle> weak = conn;
        SwObject::connect(conn.get(), &SwQuicConnectionHandle::timerChanged, this,
                          [this]() { timerDeadlineChanged(); });
        SwObject::connect(conn.get(), &SwQuicConnectionHandle::errorOccurred, this,
                          [this, weak](const SwString& error) {
                              if (std::shared_ptr<SwQuicConnectionHandle> sp = weak.lock()) {
                                  connectionRejected(sp->peerAddress(), sp->peerPort(), error);
                              }
                          });
        SwObject::connect(conn.get(), &SwQuicConnectionHandle::closed, this,
                          [this]() { timerDeadlineChanged(); });
        if (acceptedConnection) {
            SwObject::connect(conn.get(), &SwQuicConnectionHandle::established, this,
                              [this, weak]() {
                                  if (std::shared_ptr<SwQuicConnectionHandle> sp = weak.lock()) {
                                      connectionAccepted(sp);
                                  }
                              });
        }
    }

    std::size_t pendingHandshakeCount_() const {
        std::size_t count = 0;
        for (std::size_t i = 0; i < m_connections.size(); ++i) {
            if (!m_connections[i]->isEstablished() &&
                !m_connections[i]->isFailed()) ++count;
        }
        return count;
    }

    void maybeSendStatelessReset_(const SwByteArray& trigger,
                                  const SwString& dcid,
                                  const SwString& fromAddr,
                                  std::uint16_t fromPort) {
        if (!m_listening || !m_sink || dcid.empty()) return;
        // Short header seulement : un long header non routable est du bruit de
        // handshake, jamais le symptome d'un etat purge.
        if (trigger.size() <
                static_cast<int>(SwQuicStatelessReset::kMinTriggerLength) ||
            (static_cast<std::uint8_t>(trigger.constData()[0]) & 0x80U) != 0) {
            return;
        }
        // Garde-fou anti-reflexion : budget global par seconde. Le reset est
        // deja strictement plus court que son declencheur (amplification < 1).
        const std::uint64_t now = nowMs_();
        if (now - m_statelessResetWindowStartMs >= 1000) {
            m_statelessResetWindowStartMs = now;
            m_statelessResetsInWindow = 0;
        }
        if (m_statelessResetsInWindow >= kMaxStatelessResetsPerSecond) return;

        const SwByteArray cidBytes(dcid.data(),
                                   static_cast<std::size_t>(dcid.size()));
        SwByteArray reset;
        if (!SwQuicStatelessReset::buildPacket(
                statelessResetTokenForCid(cidBytes),
                static_cast<std::size_t>(trigger.size()), reset)) {
            return;
        }
        if (m_sink(reinterpret_cast<const std::uint8_t*>(reset.constData()),
                   static_cast<std::size_t>(reset.size()), fromAddr, fromPort)) {
            ++m_statelessResetsInWindow;
        }
    }

    void eraseIndexes_(const std::shared_ptr<SwQuicConnectionHandle>& conn) {
        for (SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
                 m_byPeer.begin(); it != m_byPeer.end();) {
            if (it->second == conn) {
                SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator doomed = it;
                ++it;
                m_byPeer.erase(doomed);
            } else {
                ++it;
            }
        }
        for (SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
                 m_byCid.begin(); it != m_byCid.end();) {
            if (it->second == conn) {
                SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator doomed = it;
                ++it;
                m_byCid.erase(doomed);
            } else {
                ++it;
            }
        }
    }

    void purgeExpired_(std::uint64_t now) {
        for (std::size_t i = 0; i < m_connections.size();) {
            const std::shared_ptr<SwQuicConnectionHandle> conn = m_connections[i];
            if (!conn->isEstablished() && !conn->isFailed() &&
                m_handshakeTimeoutMs > 0 &&
                now >= conn->createdMs() + m_handshakeTimeoutMs) {
                conn->abort(SwString("QUIC handshake timed out"));
            }
            if (conn->isFailed() || conn->isClosed()) {
                eraseIndexes_(conn);
                m_connections.erase(m_connections.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    void registerPeer_(const std::shared_ptr<SwQuicConnectionHandle>& conn) {
        for (SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
                 m_byPeer.begin(); it != m_byPeer.end();) {
            if (it->second == conn) {
                SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator doomed = it;
                ++it;
                m_byPeer.erase(doomed);
            } else {
                ++it;
            }
        }
        m_byPeer[addrKey_(conn->peerAddress(), conn->peerPort())] = conn;
    }

    void registerCid_(const std::shared_ptr<SwQuicConnectionHandle>& conn) {
        if (conn->hasLocalCid()) {
            m_byCid[cidKey_(conn->localCid())] = conn;
        }
    }

    SendSink m_sink;
    bool m_listening = false;
    SwString m_listenAlpn;
    SwQuicAuthMode m_listenAuth = SwQuicAuthMode::RawPublicKey;
    std::function<bool(const SwByteArray& spkiDer)> m_verifyPeerKey; // X.509/SPKI decision hook
    SwQuicServerCredential m_credential;

    // Stateless reset (RFC 9000 10.3) : cle statique du process — les jetons
    // derives survivent a la purge des connexions, c'est tout l'interet.
    static constexpr std::size_t kMaxStatelessResetsPerSecond = 32;
    SwByteArray m_statelessResetKey = SwQuicRandom::bytes(32);
    std::uint64_t m_statelessResetWindowStartMs = 0;
    std::size_t m_statelessResetsInWindow = 0;

    SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>> m_byPeer;
    SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>> m_byCid;
    SwVector<std::shared_ptr<SwQuicConnectionHandle>> m_connections;
    std::size_t m_maxConnections = 4096;
    std::size_t m_maxPendingHandshakes = 1024;
    std::uint64_t m_handshakeTimeoutMs = 10000;
};

#endif // SWQUICENDPOINT_H
