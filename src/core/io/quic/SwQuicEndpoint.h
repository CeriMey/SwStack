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

#include "SwHash.h"
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
#include "quic/SwQuicVarIntCodec.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    using SendSink = std::function<void(const std::uint8_t* data, std::size_t len,
                                        const SwString& toAddr, std::uint16_t toPort)>;

    SwQuicConnectionHandle() = default;

signals:
    // Handshake handed off to the 1-RTT data plane: the connection is usable.
    DECLARE_SIGNAL_VOID(established);
    // One application QUIC DATAGRAM frame deciphered (RFC 9221) -> climbs as bytes.
    DECLARE_SIGNAL(datagramReceived, SwByteArray);
    // Contiguous stream bytes deciphered for a stream id.
    DECLARE_SIGNAL(streamData, std::uint64_t, SwByteArray);

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
        m_role = SwQuicConnection::Role::Client;
        m_path.handle = std::hash<SwString>{}(addrKey_(host, port));
        m_path.isCarrier = false;

        m_hsClient.reset(new SwQuicHandshakeClient());
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
        if (!m_hsClient || !m_hsClient->hasSubjectPublicKeyInfoVerifier()) {
            return false;
        }
        SwByteArray initial;
        SwString err;
        if (!m_hsClient->start(SwString(kServerName_()), initial, &err)) {
            return false;
        }
        m_localCid = m_hsClient->sourceConnectionId();
        SwVector<SwByteArray> out;
        out.push_back(initial);
        emit_(out);
        return true;
    }

    // ---- fabrique serveur : un handshake serveur par nouvelle conn cliente ----
    // No accept callback: the owner (SwQuicEndpoint) connects a slot to this
    // connection's established() signal and re-emits connectionAccepted(conn).
    void initServer(SendSink sink, const SwString& fromAddr, std::uint16_t fromPort,
                    const SwString& alpn, SwQuicAuthMode authMode,
                    const SwQuicServerCredential& credential) {
        m_sink = std::move(sink);
        m_peerAddr = fromAddr;
        m_peerPort = fromPort;
        m_alpn = alpn;
        m_authMode = authMode;
        m_role = SwQuicConnection::Role::Server;
        m_path.handle = std::hash<SwString>{}(addrKey_(fromAddr, fromPort));
        m_path.isCarrier = false;

        m_hsServer.reset(new SwQuicHandshakeServer());
        m_hsServer->setCredential(credential);
    }

    bool hasLocalCid() const { return !m_localCid.isEmpty(); }
    const SwQuicConnectionId& localCid() const { return m_localCid; }

    // Route un datagramme entrant : driver de handshake tant que non établi, puis
    // le SwQuicConnection après handoff.
    void handleIncoming(const SwByteArray& datagram) {
        if (!m_established) {
            driveHandshake_(datagram);
        } else {
            deliverEstablished_(datagram);
        }
    }

    // Fait avancer les timers (PTO/idle/ACK) puis flush. No-op tant que non établi.
    void onTick() {
        if (!m_established || !m_conn) return;
        const std::uint64_t now = nowMs_();
        if (m_conn->nextTimeoutMs(now) == 0) {
            m_conn->onTimeout(now);
        }
        flush_();
    }

    // ------------------------------------------------------- data-plane API ----

    bool isEstablished() const { return m_established; }
    const SwQuicPathHandle& path() const { return m_path; }
    const SwString& peerAddress() const { return m_peerAddr; }
    std::uint16_t peerPort() const { return m_peerPort; }

    void setCallbacks(SwQuicCallbacks cbs) { m_cbs = std::move(cbs); }

    bool sendDatagram(const std::uint8_t* data, std::size_t len) {
        if (!m_established || !m_conn) return false;
        SwString err;
        if (!m_conn->queueDatagramFrame(toSwBytes_(data, len), &err)) return false;
        flush_();
        return true;
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
        flush_();
        return true;
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
        }
    }

private:
    static const char* kServerName_() { return "swquic.node"; }

    static SwString addrKey_(const SwString& addr, std::uint16_t port) {
        return addr + ":" + SwString::number(static_cast<unsigned int>(port));
    }

    static SwByteArray toSwBytes_(const std::uint8_t* data, std::size_t len) {
        if (!data || len == 0) return SwByteArray();
        return SwByteArray(reinterpret_cast<const char*>(data), len);
    }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void emit_(const SwVector<SwByteArray>& out) {
        if (!m_sink) return;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const SwByteArray& d = out[i];
            if (d.isEmpty() || !d.constData()) continue;
            m_sink(reinterpret_cast<const std::uint8_t*>(d.constData()),
                   static_cast<std::size_t>(d.size()), m_peerAddr, m_peerPort);
        }
    }

    void driveHandshake_(const SwByteArray& datagram) {
        SwString err;
        SwVector<SwByteArray> out;
        if (m_role == SwQuicConnection::Role::Client) {
            if (!m_hsClient->processIncomingDatagram(datagram, out, &err)) return; // fail-closed
            emit_(out);
            if (m_hsClient->handshakeComplete() && !m_established) {
                handoffClient_();
            }
        } else {
            if (!m_hsServer->processIncomingDatagram(datagram, out, &err)) return; // fail-closed
            if (m_localCid.isEmpty() && !m_hsServer->serverConnectionId().isEmpty()) {
                m_localCid = m_hsServer->serverConnectionId();
            }
            emit_(out); // includes, at completion, the Handshake ACK + HANDSHAKE_DONE (1-RTT)
            if (m_hsServer->handshakeComplete() && !m_established) {
                handoffServer_();
            }
        }
    }

    void handoffClient_() {
        m_conn.reset(new SwQuicConnection(SwQuicConnection::Role::Client));
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
        established(); // SIGNAL: data plane ready (climbs to the owner's slot)
    }

    void handoffServer_() {
        m_conn.reset(new SwQuicConnection(SwQuicConnection::Role::Server));
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
        established(); // SIGNAL: server side accepted -> owner re-emits connectionAccepted
    }

    void deliverEstablished_(const SwByteArray& datagram) {
        SwString err;
        const std::uint64_t now = nowMs_();
        if (!m_conn->receiveDatagram(datagram, now, &err)) return; // fail-closed

        while (m_conn->pendingDatagramCount() > 0) {
            const SwByteArray dg = m_conn->takeDatagram();
            datagramReceived(dg); // SIGNAL: application datagram climbs as bytes
        }

        const SwVector<std::uint64_t> ids = m_conn->streams().streamIds();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint64_t id = ids[i];
            m_seenStreams.insert(id, true);
            const SwByteArray sd = m_conn->readStream(id);
            if (!sd.isEmpty()) {
                streamData(id, sd); // SIGNAL: contiguous stream bytes climb
            }
        }

        flush_(); // ACKs / MAX_DATA / pending responses
    }

    void flush_() {
        if (!m_conn) return;
        SwString err;
        SwVector<SwByteArray> out;
        m_conn->buildDatagrams(nowMs_(), out, &err);
        emit_(out);
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

    SwQuicConnection::Role m_role = SwQuicConnection::Role::Client;
    bool m_established = false;

    std::unique_ptr<SwQuicHandshakeClient> m_hsClient;
    std::unique_ptr<SwQuicHandshakeServer> m_hsServer;
    std::unique_ptr<SwQuicConnection> m_conn;
    SwQuicConnectionId m_localCid;

    std::uint64_t m_nextBidiStream = 0;
    std::uint64_t m_nextUniStream = 0;
    SwHash<std::uint64_t, bool> m_seenStreams;
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

public:
    void setSendSink(SendSink sink) { m_sink = std::move(sink); }

    // Delegated-trust DECISION hook (RFC 7250). Stays a std::function because it
    // returns bool — it is NOT a notification. Applied to every client connection.
    void setVerifyPeerKey(std::function<bool(const SwByteArray& spkiDer)> verifier) {
        m_verifyPeerKey = std::move(verifier);
    }

    std::shared_ptr<SwQuicConnectionHandle> connect(const SwString& host, std::uint16_t port,
                                                    const SwString& alpn,
                                                    SwQuicAuthMode authMode) {
        std::shared_ptr<SwQuicConnectionHandle> conn = std::make_shared<SwQuicConnectionHandle>();
        SwQuicCallbacks cbs;
        cbs.verifyPeerKey = m_verifyPeerKey; // the only surviving hook (a decision)
        conn->initClient(m_sink, host, port, alpn, authMode, std::move(cbs));
        const SwString peerKey = addrKey_(host, port);
        m_byPeer[peerKey] = conn;
        if (!conn->startClientHandshake()) {
            m_byPeer.erase(peerKey);
            return std::shared_ptr<SwQuicConnectionHandle>();
        }
        registerCid_(conn);
        return conn;
    }

    void listen(const SwString& alpn, SwQuicAuthMode authMode) {
        m_listening = true;
        m_listenAlpn = alpn;
        m_listenAuth = authMode;
        SwString err;
        SwQuicEcdsaCredential::createSelfSigned(SwString("swquic.node"), m_credential, &err);
    }

    // Feed one received UDP datagram already classified as QUIC by the owner.
    void onUdpPacket(const std::uint8_t* data, std::size_t len,
                     const SwString& fromAddr, std::uint16_t fromPort) {
        const SwByteArray dg = toSwBytes_(data, len);
        if (dg.isEmpty()) return;

        // 1) Route by source 4-tuple (deterministic for a stable path).
        const SwString peerK = addrKey_(fromAddr, fromPort);
        SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
            m_byPeer.find(peerK);
        if (it != m_byPeer.end()) {
            it->second->handleIncoming(dg);
            registerCid_(it->second);
            return;
        }

        // 2) Route by destination CID (survives an address change: migration).
        SwString dcid;
        if (extractDcid_(dg, dcid)) {
            SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator ci =
                m_byCid.find(dcid);
            if (ci != m_byCid.end()) {
                ci->second->handleIncoming(dg);
                return;
            }
        }

        // 3) New inbound connection: an Initial to a listening endpoint -> server role.
        if (m_listening && isLongHeaderInitial_(dg)) {
            std::shared_ptr<SwQuicConnectionHandle> conn =
                std::make_shared<SwQuicConnectionHandle>();
            conn->initServer(m_sink, fromAddr, fromPort, m_listenAlpn, m_listenAuth,
                             m_credential);
            // Re-emit the per-connection established() as the endpoint-level
            // connectionAccepted(conn). weak_ptr avoids a self-retaining cycle
            // (the slot lives on conn, which the endpoint already owns in m_byPeer).
            std::weak_ptr<SwQuicConnectionHandle> weak = conn;
            SwObject::connect(conn.get(), &SwQuicConnectionHandle::established, this,
                              [this, weak]() {
                                  if (std::shared_ptr<SwQuicConnectionHandle> sp = weak.lock()) {
                                      connectionAccepted(sp);
                                  }
                              });
            m_byPeer[peerK] = conn;
            conn->handleIncoming(dg); // handshake spans several datagrams; established() fires later
            registerCid_(conn);
            return;
        }

        // 4) Fail-closed: unroutable packet, dropped silently.
    }

    // Drive every connection's timers once (called from the owner's periodic tick).
    void onTick() {
        for (SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
                 m_byPeer.begin(); it != m_byPeer.end(); ++it) {
            it->second->onTick();
        }
    }

private:
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

    static bool isLongHeaderInitial_(const SwByteArray& dg) {
        if (dg.isEmpty()) return false;
        const std::uint8_t first = static_cast<std::uint8_t>(dg.constData()[0]);
        return (first & 0x80U) != 0 && (first & 0x30U) == 0x00U;
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

    SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>> m_byPeer;
    SwMap<SwString, std::shared_ptr<SwQuicConnectionHandle>> m_byCid;
};

#endif // SWQUICENDPOINT_H
