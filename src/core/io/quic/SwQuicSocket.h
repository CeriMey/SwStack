#include "SwPair.h"
#ifndef SWQUICSOCKET_H
#define SWQUICSOCKET_H

// SwQuicSocket — a real UDP socket + the generic SwQuicEndpoint engine + a dynamic
// DEMUX registry + the EVENT LOOP wiring, all in one SwObject.
//
// It owns a SwUdpSocket and a SwQuicEndpoint (both SwObjects). The endpoint's
// send-sink is wired to m_socket.writeDatagram, so the engine emits real datagrams
// without knowing about sockets. On the receive side the socket is drained on the
// readyRead SIGNAL (NO manual pump loop): each datagram is matched against a
// registry of registered byte-prefix handlers (onPrefix); an unmatched datagram is
// fed to the QUIC engine (onUdpPacket). A periodic SwTimer drives the engine's
// connection timers (onTick).
//
// Everything above the wire is SIGNAL-DRIVEN: "data on the UDP -> a signal climbs
// the layers". The caller reaches the engine through endpoint() to call
// connect()/listen() and to connect slots to its connectionAccepted signal, then
// to each connection's established/datagramReceived/streamData signals.
//
// The class is 100% generic: prefixes and their handlers are supplied by the
// caller as lambdas — there is no VIGIL/mesh knowledge and no hard-coded byte.

#include "SwVector.h"
#include "SwDequeue.h"
#include "SwObject.h"
#include "SwString.h"
#include "SwByteArray.h"
#include "SwUdpSocket.h"
#include "SwTimer.h"
#include "quic/SwQuicEndpoint.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

class SwQuicSocket : public SwObject {
    SW_OBJECT(SwQuicSocket, SwObject)

public:
    using PrefixHandler = std::function<void(const std::uint8_t* data, std::size_t len,
                                             const SwString& from, std::uint16_t port)>;

    explicit SwQuicSocket(SwObject* parent = nullptr)
        : SwObject(parent) {
        // The engine emits through the socket; it never touches the socket itself.
        m_endpoint.setSendSink([this](const std::uint8_t* data, std::size_t len,
                                      const SwString& toAddr, std::uint16_t toPort) -> bool {
            return sendOrQueue_(data, len, toAddr, toPort);
        });
        SwObject::connect(&m_endpoint, &SwQuicEndpoint::timerDeadlineChanged,
                          this, [this]() { armTimer_(); });
        SwObject::connect(&m_socket, &SwIODevice::readyWrite, this, [this]() {
            flushPendingSends_();
            armTimer_();
        });
    }

    ~SwQuicSocket() override {
        if (m_tickTimer) {
            m_tickTimer->stop();
            m_tickTimer->deleteLater();
            m_tickTimer = nullptr;
        }
        m_socket.close();
    }

    // Bind a real UDP socket and start the event loop wiring (D11): drain on
    // readyRead + a periodic tick for the QUIC timers. No manual while-loop.
    bool bind(const SwString& ip, std::uint16_t port) {
        // QUIC packets are currently capped at 1200 bytes by SwQuicConnection.
        // Keep a little headroom for future PMTU growth without preallocating
        // 64 KiB for every pending slot (2048 * 64 KiB was 128 MiB/socket).
        m_socket.setMaxDatagramSize(2048);
        m_socket.setMaxPendingDatagrams(2048);
        m_socket.setMaxReadBatchDatagrams(256);
        // Réception par lots recvmmsg (Linux) : ~x1,5 en débit de réception (mesuré), sûr depuis le
        // correctif de file O(1) de SwUdpSocket. No-op sur Windows (recvmmsg absent, chemin recvfrom).
        m_socket.setBatchReceive(true);

        if (!m_socket.bind(ip, port,
                           SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint)) {
            return false;
        }

        // EVENT-LOOP DRIVEN: every readiness edge drains the socket and dispatches.
        SwObject::connect(&m_socket, &SwUdpSocket::readyRead, this, [this]() {
            drainAndDispatch_();
        });

        // One-shot timer re-armed to the engine's earliest real deadline.
        if (!m_tickTimer) {
            m_tickTimer = new SwTimer(1, this);
            m_tickTimer->setSingleShot(true);
            SwObject::connect(m_tickTimer, &SwTimer::timeout, this, [this]() {
                m_tickTimer->stop();
                flushPendingSends_();
                m_endpoint.onTick();
                armTimer_();
            });
        }
        armTimer_();
        return true;
    }

    std::uint16_t localPort() const { return m_socket.localPort(); }
    SwString localAddress() const { return m_socket.localAddress(); }
    bool isOpen() const { return m_socket.isOpen(); }

    // Register a demux prefix: when an inbound datagram starts with these exact
    // bytes, its handler is invoked instead of the QUIC engine. First match wins.
    void onPrefix(const SwByteArray& prefix, PrefixHandler handler) {
        m_prefixes.push_back(SwMakePair(prefix, std::move(handler)));
    }

    // The generic engine, for connect()/listen()/setCallbacks from the caller.
    SwQuicEndpoint& endpoint() { return m_endpoint; }
    const SwQuicEndpoint& endpoint() const { return m_endpoint; }

    // Send a raw datagram out of this socket (e.g. a demux-prefixed packet).
    std::int64_t writeDatagram(const SwByteArray& payload,
                               const SwString& host, std::uint16_t port) {
        return m_socket.writeDatagram(payload.constData(),
                                      static_cast<std::int64_t>(payload.size()), host, port);
    }

private:
    // Drain the socket in one readiness edge and dispatch each datagram through
    // the demux, then to the QUIC engine. Driven by readyRead — no manual pump.
    void drainAndDispatch_() {
        m_socket.pollPendingDatagrams(0);
        while (m_socket.hasPendingDatagrams()) {
            SwString from;
            std::uint16_t port = 0;
            const SwByteArray dg = m_socket.receiveDatagram(&from, &port);
            if (dg.isEmpty()) continue;

            const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(dg.constData());
            const std::size_t len = static_cast<std::size_t>(dg.size());

            if (dispatchPrefix_(dg, data, len, from, port)) {
                continue; // routed to a registered prefix handler
            }
            m_endpoint.onUdpPacket(dg, from, port); // otherwise: QUIC, no second copy
        }
        flushPendingSends_();
        armTimer_();
    }

    void armTimer_() {
        if (!m_tickTimer) return;
        std::int64_t next = m_endpoint.nextTimeoutMs();
        m_tickTimer->stop();
        if (next < 0) return;
        std::int64_t delay = next <= 0 ? 1 : next;
        if (delay > static_cast<std::int64_t>((std::numeric_limits<int>::max)())) {
            delay = (std::numeric_limits<int>::max)();
        }
        m_tickTimer->start(static_cast<int>(delay));
    }

    struct PendingSend_ {
        SwByteArray datagram;
        SwString host;
        std::uint16_t port = 0;
    };

    static std::size_t kMaxPendingSendDatagrams_() { return 2048; }
    static std::size_t kMaxPendingSendBytes_() { return 4U * 1024U * 1024U; }

    bool queuePendingSend_(const std::uint8_t* data, std::size_t len,
                           const SwString& host, std::uint16_t port) {
        if (!data || len == 0 ||
            m_pendingSends.size() >= kMaxPendingSendDatagrams_() ||
            m_pendingSendBytes > kMaxPendingSendBytes_() ||
            len > kMaxPendingSendBytes_() - m_pendingSendBytes) {
            return false;
        }
        PendingSend_ pending;
        pending.datagram = SwByteArray(reinterpret_cast<const char*>(data), len);
        pending.host = host;
        pending.port = port;
        m_pendingSendBytes += len;
        m_pendingSends.push_back(std::move(pending));
        m_socket.setWriteNotificationsEnabled(true);
        return true;
    }

    bool sendOrQueue_(const std::uint8_t* data, std::size_t len,
                      const SwString& host, std::uint16_t port) {
        if (!m_pendingSends.empty()) return queuePendingSend_(data, len, host, port);
        const std::int64_t sent = m_socket.writeDatagramCached(
            reinterpret_cast<const char*>(data), static_cast<std::int64_t>(len), host, port);
        if (sent == static_cast<std::int64_t>(len)) return true;
        if (m_socket.lastDatagramIoStatus() == SwUdpSocket::DatagramIoStatus::WouldBlock) {
            return queuePendingSend_(data, len, host, port);
        }
        return false;
    }

    void flushPendingSends_() {
        while (!m_pendingSends.empty()) {
            PendingSend_& pending = m_pendingSends.front();
            const std::int64_t sent = m_socket.writeDatagramCached(
                pending.datagram.constData(),
                static_cast<std::int64_t>(pending.datagram.size()),
                pending.host, pending.port);
            if (sent != static_cast<std::int64_t>(pending.datagram.size()) &&
                m_socket.lastDatagramIoStatus() == SwUdpSocket::DatagramIoStatus::WouldBlock) {
                break;
            }
            m_pendingSendBytes -= static_cast<std::size_t>(pending.datagram.size());
            m_pendingSends.pop_front();
            // A hard send error is treated as packet loss. QUIC loss recovery
            // retransmits ack-eliciting data; retaining it here would spin.
        }
        m_socket.setWriteNotificationsEnabled(!m_pendingSends.empty());
    }

    bool dispatchPrefix_(const SwByteArray& dg, const std::uint8_t* data, std::size_t len,
                         const SwString& from, std::uint16_t port) {
        for (std::size_t i = 0; i < m_prefixes.size(); ++i) {
            const SwByteArray& prefix = m_prefixes[i].first;
            if (matchesPrefix_(dg, prefix)) {
                if (m_prefixes[i].second) {
                    m_prefixes[i].second(data, len, from, port);
                }
                return true;
            }
        }
        return false;
    }

    static bool matchesPrefix_(const SwByteArray& dg, const SwByteArray& prefix) {
        if (prefix.isEmpty()) return false;
        if (dg.size() < prefix.size()) return false;
        const char* d = dg.constData();
        const char* p = prefix.constData();
        for (int i = 0; i < prefix.size(); ++i) {
            if (d[i] != p[i]) return false;
        }
        return true;
    }

    SwUdpSocket m_socket;
    SwQuicEndpoint m_endpoint;
    SwVector<SwPair<SwByteArray, PrefixHandler>> m_prefixes;
    SwDequeue<PendingSend_> m_pendingSends;
    std::size_t m_pendingSendBytes = 0;
    SwTimer* m_tickTimer = nullptr;
};

#endif // SWQUICSOCKET_H
