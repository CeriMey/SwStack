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
// Auth: SwQuicAuthMode::RawPublicKey wires SwQuicCallbacks::verifyPeerKey onto
// SwQuicHandshakeClient::setRawPublicKeyVerifier (RFC 7250 delegated trust): the
// key/cert bytes the server presents are handed to the caller's lambda, which
// decides membership. The PKI chain is NOT validated; the CertificateVerify
// proof-of-possession still is.

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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// ------------------------------------------------------------------- public types

enum class SwQuicAuthMode {
    RawPublicKey  // RFC 7250 delegated trust via verifyPeerKey (the only mode)
};

// Opaque identifier of the network path a datagram arrived on. Generic: the
// engine fills handle from the peer 4-tuple; isCarrier is left to the owner.
struct SwQuicPathHandle {
    std::uint64_t handle = 0;
    bool isCarrier = false;
};

// Per-connection application callbacks. All optional.
struct SwQuicCallbacks {
    // Unreliable QUIC DATAGRAM frame received (RFC 9221).
    std::function<void(const SwQuicPathHandle& path,
                       const std::uint8_t* data, std::size_t len)> onDatagram;
    // Contiguous stream bytes received.
    std::function<void(std::uint64_t streamId,
                       const std::uint8_t* data, std::size_t len)> onStreamData;
    // A peer-visible stream first appeared (dir: 0 = bidi, 1 = uni).
    std::function<void(std::uint64_t streamId, int dir)> onStreamOpen;
    // Delegated trust decision on the peer's presented key/cert (SPKI DER).
    // Return true to accept. Client-side only.
    std::function<bool(const SwByteArray& spkiDer)> verifyPeerKey;
};

// ------------------------------------------------------------- connection handle

// One QUIC connection. Owns its handshake driver until the 1-RTT keys are handed
// off to an internal SwQuicConnection, then exposes the data-plane API.
class SwQuicConnectionHandle
    : public std::enable_shared_from_this<SwQuicConnectionHandle> {
public:
    using SendSink = std::function<void(const std::uint8_t* data, std::size_t len,
                                        const SwString& toAddr, std::uint16_t toPort)>;
    using OnAccept = std::function<void(std::shared_ptr<SwQuicConnectionHandle>)>;

    SwQuicConnectionHandle() = default;

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
        m_path.handle = std::hash<std::string>{}(addrKey_(host, port));
        m_path.isCarrier = false;

        m_hsClient.reset(new SwQuicHandshakeClient());
        m_hsClient->setVerifyPeer(true);
        m_hsClient->setVerifyCertificateChain(false); // trust delegated to the key (RPK)
        if (m_cbs.verifyPeerKey) {
            std::function<bool(const SwByteArray&)> vp = m_cbs.verifyPeerKey;
            m_hsClient->setRawPublicKeyVerifier(
                [vp](const SwByteArray& certOrSpki) -> bool { return vp(certOrSpki); });
        }
    }

    // Émet l'Initial ; la suite du handshake se déroule via handleIncoming().
    bool startClientHandshake() {
        SwByteArray initial;
        SwString err;
        if (!m_hsClient->start(SwString(kServerName_()), initial, &err)) {
            return false;
        }
        m_localCid = m_hsClient->sourceConnectionId();
        std::vector<SwByteArray> out;
        out.push_back(initial);
        emit_(out);
        return true;
    }

    // ---- fabrique serveur : un handshake serveur par nouvelle conn cliente ----
    void initServer(SendSink sink, const SwString& fromAddr, std::uint16_t fromPort,
                    const SwString& alpn, SwQuicAuthMode authMode,
                    const SwQuicServerCredential& credential, OnAccept onAccept) {
        m_sink = std::move(sink);
        m_peerAddr = fromAddr;
        m_peerPort = fromPort;
        m_alpn = alpn;
        m_authMode = authMode;
        m_onAccept = std::move(onAccept);
        m_role = SwQuicConnection::Role::Server;
        m_path.handle = std::hash<std::string>{}(addrKey_(fromAddr, fromPort));
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

    // TLS-Exporter (RFC 8446 7.5). Same jalon limitation as the meshq engine: the
    // SwQuicHandshake* drivers do not yet surface exporter_master_secret, so this
    // returns empty (fail-closed). Wire it to SwTls13KeySchedule once exposed.
    std::vector<std::uint8_t> exporter(const SwString& /*label*/, std::size_t /*len*/) const {
        return std::vector<std::uint8_t>();
    }

    void close(std::uint64_t appErrorCode) {
        if (m_conn) {
            m_conn->close(appErrorCode, SwString("closed"), true);
            flush_();
        }
    }

private:
    static const char* kServerName_() { return "swquic.node"; }

    static std::string addrKey_(const SwString& addr, std::uint16_t port) {
        return addr.toStdString() + ":" + std::to_string(port);
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

    void emit_(const std::vector<SwByteArray>& out) {
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
        std::vector<SwByteArray> out;
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
                if (m_onAccept) m_onAccept(shared_from_this());
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
    }

    void deliverEstablished_(const SwByteArray& datagram) {
        SwString err;
        const std::uint64_t now = nowMs_();
        if (!m_conn->receiveDatagram(datagram, now, &err)) return; // fail-closed

        while (m_conn->pendingDatagramCount() > 0) {
            const SwByteArray dg = m_conn->takeDatagram();
            if (m_cbs.onDatagram) {
                m_cbs.onDatagram(m_path,
                                 reinterpret_cast<const std::uint8_t*>(dg.constData()),
                                 static_cast<std::size_t>(dg.size()));
            }
        }

        const std::vector<std::uint64_t> ids = m_conn->streams().streamIds();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint64_t id = ids[i];
            if (m_seenStreams.insert(id).second && m_cbs.onStreamOpen) {
                m_cbs.onStreamOpen(id, (id & 0x2U) ? 1 : 0);
            }
            const SwByteArray sd = m_conn->readStream(id);
            if (!sd.isEmpty() && m_cbs.onStreamData) {
                m_cbs.onStreamData(id,
                                   reinterpret_cast<const std::uint8_t*>(sd.constData()),
                                   static_cast<std::size_t>(sd.size()));
            }
        }

        flush_(); // ACKs / MAX_DATA / pending responses
    }

    void flush_() {
        if (!m_conn) return;
        SwString err;
        std::vector<SwByteArray> out;
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
    OnAccept m_onAccept;
    SwQuicPathHandle m_path;

    SwQuicConnection::Role m_role = SwQuicConnection::Role::Client;
    bool m_established = false;

    std::unique_ptr<SwQuicHandshakeClient> m_hsClient;
    std::unique_ptr<SwQuicHandshakeServer> m_hsServer;
    std::unique_ptr<SwQuicConnection> m_conn;
    SwQuicConnectionId m_localCid;

    std::uint64_t m_nextBidiStream = 0;
    std::uint64_t m_nextUniStream = 0;
    std::set<std::uint64_t> m_seenStreams;
};

// ---------------------------------------------------------------------- endpoint

// The generic QUIC engine. Routes incoming datagrams to their connection by peer
// 4-tuple then by destination connection ID (survives migration), and mints a
// server-role connection for a fresh Initial while listening.
class SwQuicEndpoint {
public:
    using SendSink = SwQuicConnectionHandle::SendSink;

    void setSendSink(SendSink sink) { m_sink = std::move(sink); }

    std::shared_ptr<SwQuicConnectionHandle> connect(const SwString& host, std::uint16_t port,
                                                    const SwString& alpn,
                                                    SwQuicAuthMode authMode,
                                                    SwQuicCallbacks cbs) {
        std::shared_ptr<SwQuicConnectionHandle> conn = std::make_shared<SwQuicConnectionHandle>();
        conn->initClient(m_sink, host, port, alpn, authMode, std::move(cbs));
        m_byPeer[addrKey_(host, port)] = conn;
        conn->startClientHandshake(); // emits the Initial; handshake proceeds via onUdpPacket()
        registerCid_(conn);
        return conn;
    }

    void listen(const SwString& alpn, SwQuicAuthMode authMode,
                std::function<void(std::shared_ptr<SwQuicConnectionHandle>)> onAccept) {
        m_listening = true;
        m_listenAlpn = alpn;
        m_listenAuth = authMode;
        m_onAccept = std::move(onAccept);
        SwString err;
        SwQuicEcdsaCredential::createSelfSigned(SwString("swquic.node"), m_credential, &err);
    }

    // Feed one received UDP datagram already classified as QUIC by the owner.
    void onUdpPacket(const std::uint8_t* data, std::size_t len,
                     const SwString& fromAddr, std::uint16_t fromPort) {
        const SwByteArray dg = toSwBytes_(data, len);
        if (dg.isEmpty()) return;

        // 1) Route by source 4-tuple (deterministic for a stable path).
        const std::string peerK = addrKey_(fromAddr, fromPort);
        std::map<std::string, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
            m_byPeer.find(peerK);
        if (it != m_byPeer.end()) {
            it->second->handleIncoming(dg);
            registerCid_(it->second);
            return;
        }

        // 2) Route by destination CID (survives an address change: migration).
        std::string dcid;
        if (extractDcid_(dg, dcid)) {
            std::map<std::string, std::shared_ptr<SwQuicConnectionHandle>>::iterator ci =
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
                             m_credential, m_onAccept);
            m_byPeer[peerK] = conn;
            conn->handleIncoming(dg);
            registerCid_(conn);
            return;
        }

        // 4) Fail-closed: unroutable packet, dropped silently.
    }

    // Drive every connection's timers once (called from the owner's periodic tick).
    void onTick() {
        for (std::map<std::string, std::shared_ptr<SwQuicConnectionHandle>>::iterator it =
                 m_byPeer.begin(); it != m_byPeer.end(); ++it) {
            it->second->onTick();
        }
    }

private:
    static std::string addrKey_(const SwString& addr, std::uint16_t port) {
        return addr.toStdString() + ":" + std::to_string(port);
    }

    static std::string cidKey_(const SwQuicConnectionId& cid) {
        const SwByteArray& b = cid.bytes();
        return std::string(b.constData() ? b.constData() : "", static_cast<std::size_t>(b.size()));
    }

    static SwByteArray toSwBytes_(const std::uint8_t* data, std::size_t len) {
        if (!data || len == 0) return SwByteArray();
        return SwByteArray(reinterpret_cast<const char*>(data), len);
    }

    // Extract the DCID (the CID the peer put in destination = OUR local CID) to
    // route the inbound even if the source address changed (migration).
    static bool extractDcid_(const SwByteArray& dg, std::string& outDcid) {
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
    std::function<void(std::shared_ptr<SwQuicConnectionHandle>)> m_onAccept;
    SwQuicServerCredential m_credential;

    std::map<std::string, std::shared_ptr<SwQuicConnectionHandle>> m_byPeer;
    std::map<std::string, std::shared_ptr<SwQuicConnectionHandle>> m_byCid;
};

#endif // SWQUICENDPOINT_H
