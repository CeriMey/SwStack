#ifndef SWQUICHTTP3SERVER_H
#define SWQUICHTTP3SERVER_H

#include "SwObject.h"
#include "SwMap.h"
#include "SwMutex.h"
#include "SwPointer.h"
#include "SwString.h"
#include "SwTimer.h"
#include "SwUdpSocket.h"
#include "SwVector.h"
#include "http/SwHttpRouter.h"
#include "http/SwHttpTypes.h"
#include "http3/SwHttp3Server.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicHandshakeServer.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwQuicRetry.h"
#include "quic/SwQuicServerCredential.h"
#include "quic/SwQuicSessionTicket.h"

#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

// End-to-end HTTP/3-over-QUIC server driver.
//
// It binds a single UDP socket and, per client, runs the full stack: the QUIC
// v1 + TLS 1.3 server handshake (`SwQuicHandshakeServer`) until 1-RTT keys are
// derived, then hands the application packet-number space to a
// `SwQuicConnection` and drives an `SwHttp3Server` over it, dispatching each
// request through an `SwHttpRouter` (or a request handler). This is the glue
// that turns an accepted QUIC connection into routed HTTP/3 requests -- the
// same route table can therefore serve HTTP/1.x (via SwHttpServer) and HTTP/3.
//
// It is poll-driven like `SwQuicServer`: bind with listen(), then either rely
// on the socket's readyRead signal or call poll() from an event loop.
class SwQuicHttp3Server : public SwObject {
    SW_OBJECT(SwQuicHttp3Server, SwObject)

private:
    struct Client_;
    struct AsyncCompletionCycle_;

public:
    typedef std::function<SwHttpResponse(const SwHttpRequest&)> RequestHandler;
    typedef std::function<void(const SwHttpRequest&,
                               const SwHttp3Server::ResponseCallback&)>
        AsyncRequestHandler;
    typedef SwHttp3Server::PendingBytesReserveHandler PendingBytesReserveHandler;
    typedef SwHttp3Server::PendingBytesReleaseHandler PendingBytesReleaseHandler;
    typedef SwMap<SwString, SwQuicServerCredential> CredentialMap;

    explicit SwQuicHttp3Server(SwObject* parent = nullptr)
        : SwObject(parent),
          m_router(nullptr),
          m_asyncCompletionCycle(new AsyncCompletionCycle_()) {
        m_tickTimer = new SwTimer(1, this);
        m_tickTimer->setSingleShot(true);
        SwObject::connect(&m_socket, &SwUdpSocket::readyRead, this, [this]() {
            if (!m_automaticPolling || !isListening()) {
                return;
            }
            SwString error;
            if (poll(0, &error) < 0 && !error.isEmpty()) {
                serverError(error);
            }
        });
        SwObject::connect(m_tickTimer, &SwTimer::timeout, this, [this]() {
            if (!m_automaticPolling || !isListening()) {
                return;
            }
            SwString error;
            if (poll(0, &error) < 0 && !error.isEmpty()) {
                serverError(error);
            }
        });
    }

    ~SwQuicHttp3Server() override {
        close();
    }

    // The credential (certificate chain + CertificateVerify signer) the server
    // presents to every client. Required before listen()/poll().
    void setCredential(const SwQuicServerCredential& credential) {
        m_credential = credential;
        m_namedCredentials.clear();
        advanceCredentialGeneration_();
        // Resumption PSKs authenticate the previous server identity. Never let
        // them survive a credential rotation.
        m_ticketStore.clear();
    }

    void setCredentials(const SwQuicServerCredential& defaultCredential,
                        const CredentialMap& credentialsByHost) {
        m_credential = defaultCredential;
        m_namedCredentials.clear();
        for (CredentialMap::const_iterator it = credentialsByHost.begin();
             it != credentialsByHost.end(); ++it) {
            const SwString host = it.key().trimmed().toLower();
            if (!host.isEmpty() && it.value().isValid()) {
                m_namedCredentials[host] = it.value();
            }
        }
        advanceCredentialGeneration_();
        m_ticketStore.clear();
    }

    // Route requests through an SwHttpRouter (shared with the HTTP/1.x server).
    void setRouter(SwHttpRouter* router) { m_router = router; }

    // Or dispatch through a plain handler (takes precedence only if no router).
    void setRequestHandler(const RequestHandler& handler) { m_requestHandler = handler; }
    void setAsyncRequestHandler(const AsyncRequestHandler& handler) {
        m_asyncRequestHandler = handler;
    }
    void setMaxClients(std::size_t maximum) { m_maxClients = maximum; }
    void setMaxPendingHandshakes(std::size_t maximum) {
        m_maxPendingHandshakes = maximum;
    }
    void setHandshakeTimeoutMs(std::uint64_t timeoutMs) {
        m_handshakeTimeoutMs = timeoutMs;
    }
    void setMaxSessionTickets(std::size_t maximum) {
        m_ticketStore.setMaxEntries(maximum);
    }
    void setHttpLimits(const SwHttpLimits& limits) { m_httpLimits = limits; }

    // Stateless address validation via Retry (RFC 9000 8.1.2). When enabled,
    // a new Initial arriving without a token is answered with a Retry packet
    // -- allocating NO server state -- once `pendingHandshakeThreshold`
    // handshakes are already in progress (0 = validate every new connection).
    // The address token binds the client address, the original DCID and the
    // Retry SCID under an HMAC key minted per listen(), and expires after
    // `tokenMaxAgeSeconds`. This is the anti-DoS gate against Initial floods:
    // an attacker must prove return routability before costing a handshake.
    void setAddressValidation(bool enabled,
                              std::size_t pendingHandshakeThreshold = 0,
                              std::uint64_t tokenMaxAgeSeconds = 30) {
        m_addressValidationEnabled = enabled;
        m_retryPendingThreshold = pendingHandshakeThreshold;
        m_retryTokenMaxAgeSeconds = tokenMaxAgeSeconds ? tokenMaxAgeSeconds : 30;
    }
    bool addressValidationEnabled() const { return m_addressValidationEnabled; }
    std::uint64_t retryPacketsSent() const { return m_retryPacketsSent; }
    // Optional cross-transport budget. Configure before listen(); the driver
    // still tracks its own H3 usage for diagnostics while the external gate can
    // combine that usage with TCP/TLS sessions.
    void setPendingRequestBudgetHandlers(
        const PendingBytesReserveHandler& reserveHandler,
        const PendingBytesReleaseHandler& releaseHandler) {
        if (m_pendingRequestBytesGlobal != 0) {
            return;
        }
        m_pendingBytesReserveHandler = reserveHandler;
        m_pendingBytesReleaseHandler = releaseHandler;
    }

    // Event-loop integration. When enabled, UDP readiness drives packet work
    // and a one-shot timer is re-armed to the earliest QUIC deadline. Manual
    // poll() remains available for dedicated-loop users and existing tests.
    void setAutomaticPollingEnabled(bool enabled) {
        m_automaticPolling = enabled;
        if (!enabled) {
            if (m_tickTimer) m_tickTimer->stop();
            return;
        }
        armTimer_();
    }

    bool automaticPollingEnabled() const { return m_automaticPolling; }

    bool listen(const SwString& bindAddress, uint16_t port, SwString* error = nullptr) {
        if (m_polling) {
            setError_(error, SwString(
                "Cannot replace the QUIC listener from inside its poll callback"));
            return false;
        }
        close();
        beginAsyncCompletionCycle_();
        if (!m_credential.isValid()) {
            setError_(error, SwString("QUIC HTTP/3 server has no valid credential"));
            return false;
        }
        m_socket.setMaxDatagramSize(2048);
        m_socket.setMaxPendingDatagrams(2048);
        m_socket.setMaxReadBatchDatagrams(256);
        m_socket.setBatchReceive(true);

        if (!m_socket.bind(bindAddress, port,
                           SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint)) {
            // SwUdpSocket keeps its native descriptor alive after a failed
            // bind so callers may inspect/reuse it. A server listener must not
            // expose that unbound descriptor as an active HTTP/3 endpoint.
            const SwString bindError = m_socket.errorString();
            m_socket.close();
            setError_(error, bindError);
            return false;
        }
        // Fresh Retry-token key per listener: a token can never authorise
        // state on a listener other than the one that minted it.
        SwString keyError;
        if (!SwQuicRandom::fill(m_retryKey, 32, &keyError)) {
            m_socket.close();
            setError_(error, keyError);
            return false;
        }
        m_retryPacketsSent = 0;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    void close() {
        if (m_polling) {
            m_closePending = true;
            return;
        }
        closeNow_();
    }

    // Graceful shutdown (RFC 9114 5.2): GOAWAY every established session and
    // refuse new connections while the socket keeps draining in-flight work.
    // poll()/timers keep running; each connection is closed with H3_NO_ERROR
    // once its requests are answered and the responses are acknowledged.
    // Observe completion with isDrained(), then call close().
    void beginGracefulShutdown() {
        if (m_draining) {
            return;
        }
        m_draining = true;
        for (std::map<std::string, Client_>::iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            Client_& client = it->second;
            if (!client.established) {
                // A handshake completing mid-drain would open a session that
                // can never be served; fail it now so purge reclaims it.
                client.failed = true;
                continue;
            }
            if (!client.http3 || !client.connection) {
                continue;
            }
            SwString error;
            if (!client.http3->initiateGracefulShutdown(&error) ||
                !flushConnection_(client, &error)) {
                client.failed = true;
                if (!error.isEmpty()) serverError(error);
            }
        }
        advanceGracefulShutdown_();
        armTimer_();
    }

    bool gracefulShutdownInitiated() const { return m_draining; }

    // True once every session has finished its in-flight requests and had its
    // CONNECTION_CLOSE initiated. The remaining transport-level draining is
    // time-bounded; close() can follow safely.
    bool isDrained() const {
        if (!m_draining) {
            return false;
        }
        for (std::map<std::string, Client_>::const_iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            const Client_& client = it->second;
            if (client.failed || !client.established || !client.connection) {
                continue;
            }
            if (client.connection->state() == SwQuicConnection::State::Open) {
                return false;
            }
        }
        return true;
    }

    bool isListening() const { return m_socket.isOpen(); }
    uint16_t localPort() const { return m_socket.localPort(); }
    SwString localAddress() const { return m_socket.localAddress(); }
    std::size_t clientCount() const { return m_clients.size(); }
    std::size_t pendingRequestBytes() const { return m_pendingRequestBytesGlobal; }

    int poll(int timeoutMs = 0, SwString* error = nullptr) {
        if (!isListening()) {
            setError_(error, SwString("QUIC HTTP/3 server is not listening"));
            return -1;
        }
        // The socket's readyRead signal is wired to poll(), so draining it here
        // can re-enter poll() on the same state. Guard against that: a nested
        // call is a no-op (the outer call already owns the drain).
        if (m_polling) {
            if (error) {
                *error = SwString();
            }
            return 0;
        }
        PollScope_ scope(this);
        drainAsyncCompletions_(m_asyncCompletionCycle);
        purgeClients_(nowMs_());

        m_socket.pollPendingDatagrams(timeoutMs);

        int processed = 0;
        while (m_socket.hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            const SwByteArray datagram = m_socket.receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                continue;
            }

            SwString datagramError;
            if (!processDatagram_(datagram, sender, senderPort, &datagramError)) {
                emitClientRejected_(sender, senderPort, datagramError);
                continue;
            }
            ++processed;
            if (m_closePending) {
                break;
            }
        }

        if (!m_closePending) {
            fireTimers_();
            advanceGracefulShutdown_();
            purgeClients_(nowMs_());
            armTimer_();
        }

        if (error) {
            *error = SwString();
        }
        return processed;
    }

    std::int64_t nextTimeoutMs() const { return nextTimeoutMs_(); }

signals:
    DECLARE_SIGNAL(requestServed, const SwHttpRequest&, const SwHttpResponse&)
    DECLARE_SIGNAL(clientRejected, const SwString&, uint16_t, const SwString&)
    DECLARE_SIGNAL(serverError, const SwString&)

private:
    class PollScope_ {
    public:
        explicit PollScope_(SwQuicHttp3Server* server) : m_server(server) {
            m_server->m_polling = true;
        }
        ~PollScope_() {
            m_server->m_polling = false;
            if (m_server->m_closePending) {
                m_server->closeNow_();
            }
        }
    private:
        SwQuicHttp3Server* m_server;
    };

    struct Client_ {
        SwQuicHandshakeServer handshake;
        std::unique_ptr<SwQuicConnection> connection;
        std::unique_ptr<SwHttp3Server> http3;
        SwString host;
        uint16_t port;
        bool credentialSet;
        std::uint64_t credentialGeneration;
        bool established;
        bool failed;
        std::uint64_t createdMs;
        std::uint64_t lastActivityMs;
        std::shared_ptr<std::atomic<std::size_t> > pendingAsyncResponses;

        Client_() : port(0), credentialSet(false), credentialGeneration(0),
                    established(false), failed(false),
                    createdMs(0), lastActivityMs(0),
                    pendingAsyncResponses(new std::atomic<std::size_t>(0)) {}
    };

    struct AsyncCompletion_ {
        std::string clientKey;
        std::uint64_t streamId = 0;
        std::shared_ptr<SwHttpResponse> response;
    };

    struct AsyncCompletionCycle_ {
        SwMutex mutex;
        std::deque<AsyncCompletion_> queue;
        std::atomic<std::size_t> queued{0};
        bool accepting = true;
    };

    std::int64_t nextTimeoutMs_() const {
        const std::uint64_t now = nowMs_();
        std::int64_t next = -1;
        bool pendingAsyncResponse = false;
        for (std::map<std::string, Client_>::const_iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            const Client_& client = it->second;
            if (client.pendingAsyncResponses &&
                client.pendingAsyncResponses->load(std::memory_order_acquire) > 0) {
                pendingAsyncResponse = true;
            }
            std::int64_t candidate = -1;
            if (client.failed) {
                candidate = 0;
            } else if (client.established && client.connection) {
                candidate = client.connection->nextTimeoutMs(now);
            } else if (m_handshakeTimeoutMs > 0) {
                const std::uint64_t deadline = client.createdMs + m_handshakeTimeoutMs;
                candidate = deadline <= now ? 0
                    : static_cast<std::int64_t>(deadline - now);
            }
            if (candidate >= 0 && (next < 0 || candidate < next)) next = candidate;
        }
        const std::shared_ptr<AsyncCompletionCycle_> cycle =
            m_asyncCompletionCycle;
        if (cycle &&
            (cycle->queued.load(std::memory_order_acquire) > 0 ||
             pendingAsyncResponse) &&
            (next < 0 || next > 25)) {
            // A reliable affinity post can still be rejected while its lane
            // is saturated. Keep a low-frequency fallback poll armed for as
            // long as an async request or queued completion exists.
            next = 25;
        }
        if (m_draining && (next < 0 || next > 25)) {
            // Drain progress (last ACK arriving, session becoming idle) is
            // observed from poll(); keep a fallback tick until fully drained.
            for (std::map<std::string, Client_>::const_iterator it = m_clients.begin();
                 it != m_clients.end(); ++it) {
                if (it->second.established && it->second.connection &&
                    it->second.connection->state() == SwQuicConnection::State::Open) {
                    next = 25;
                    break;
                }
            }
        }
        return next;
    }

    void armTimer_() {
        if (!m_automaticPolling || !m_tickTimer || !isListening()) {
            return;
        }
        const std::int64_t next = nextTimeoutMs_();
        m_tickTimer->stop();
        if (next < 0) {
            return;
        }
        std::int64_t delay = next <= 0 ? 1 : next;
        if (delay > static_cast<std::int64_t>((std::numeric_limits<int>::max)())) {
            delay = (std::numeric_limits<int>::max)();
        }
        m_tickTimer->start(static_cast<int>(delay));
    }

    void closeNow_() {
        m_closePending = false;
        m_draining = false;
        if (m_tickTimer) m_tickTimer->stop();
        invalidateAsyncCompletionCycle_();
        m_socket.close();
        m_clients.clear();
        m_pendingRequestBytesGlobal = 0;
        m_connectionIdIndex.clear();
        m_ticketStore.clear();
    }

    void beginAsyncCompletionCycle_() {
        m_asyncCompletionCycle.reset(new AsyncCompletionCycle_());
    }

    void invalidateAsyncCompletionCycle_() {
        const std::shared_ptr<AsyncCompletionCycle_> cycle =
            m_asyncCompletionCycle;
        if (!cycle) return;
        SwMutexLocker lock(&cycle->mutex);
        cycle->accepting = false;
        cycle->queue.clear();
        cycle->queued.store(0, std::memory_order_release);
    }

    void advanceCredentialGeneration_() {
        if (m_credentialGeneration ==
            (std::numeric_limits<std::uint64_t>::max)()) {
            m_credentialGeneration = 1;
        } else {
            ++m_credentialGeneration;
        }
        // Existing established sessions may finish normally with their
        // negotiated keys. Incomplete handshakes must not mint a ticket for a
        // credential identity that has just been replaced.
        for (std::map<std::string, Client_>::iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            if (!it->second.established) {
                it->second.failed = true;
            }
        }
    }

    static void setError_(SwString* error, const SwString& message) {
        if (error) {
            *error = message;
        }
    }

    bool reservePendingRequestBytes_(std::size_t bytes) {
        if (m_httpLimits.maxPendingRequestBytesGlobal > 0 &&
            (m_pendingRequestBytesGlobal > m_httpLimits.maxPendingRequestBytesGlobal ||
             bytes > m_httpLimits.maxPendingRequestBytesGlobal -
                         m_pendingRequestBytesGlobal)) {
            return false;
        }
        if (bytes > (std::numeric_limits<std::size_t>::max)() -
                        m_pendingRequestBytesGlobal) {
            return false;
        }
        if (m_pendingBytesReserveHandler && !m_pendingBytesReserveHandler(bytes)) {
            return false;
        }
        m_pendingRequestBytesGlobal += bytes;
        return true;
    }

    void releasePendingRequestBytes_(std::size_t bytes) {
        const std::size_t released = bytes > m_pendingRequestBytesGlobal
            ? m_pendingRequestBytesGlobal
            : bytes;
        m_pendingRequestBytesGlobal -= released;
        if (released > 0 && m_pendingBytesReleaseHandler) {
            m_pendingBytesReleaseHandler(released);
        }
    }

    static std::string endpointKey_(const SwString& host, uint16_t port) {
        return host.toStdString() + ":" + SwString::number(static_cast<int>(port)).toStdString();
    }

    static std::string connectionMapKey_(const std::string& initialDestinationId) {
        return std::string("cid:", 4) + initialDestinationId;
    }

    static std::string connectionIdKey_(const SwByteArray& connectionId) {
        return std::string(connectionId.constData(),
                           connectionId.constData() + connectionId.size());
    }

    static bool extractDestinationConnectionId_(const SwByteArray& datagram,
                                                std::string& outConnectionId) {
        outConnectionId.clear();
        if (datagram.isEmpty()) return false;
        const std::uint8_t first = static_cast<std::uint8_t>(datagram.constData()[0]);
        if ((first & 0x80U) != 0) {
            if (datagram.size() < 6) return false;
            const std::size_t length =
                static_cast<std::uint8_t>(datagram.constData()[5]);
            if (length == 0 || length > SwQuicConnectionId::kMaxLength ||
                static_cast<std::size_t>(datagram.size()) < 6 + length) return false;
            outConnectionId.assign(datagram.constData() + 6,
                                   datagram.constData() + 6 + length);
            return true;
        }

        // This driver issues fixed-size 8-byte server CIDs.
        const std::size_t length = 8;
        if (static_cast<std::size_t>(datagram.size()) < 1 + length) return false;
        outConnectionId.assign(datagram.constData() + 1,
                               datagram.constData() + 1 + length);
        return true;
    }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static bool isValidInitial_(const SwByteArray& datagram) {
        if (datagram.size() < 1200) return false;
        const std::uint8_t first = static_cast<std::uint8_t>(datagram.constData()[0]);
        if ((first & 0xc0U) != 0xc0U || (first & 0x30U) != 0) return false;
        if (static_cast<std::uint8_t>(datagram.constData()[1]) != 0 ||
            static_cast<std::uint8_t>(datagram.constData()[2]) != 0 ||
            static_cast<std::uint8_t>(datagram.constData()[3]) != 0 ||
            static_cast<std::uint8_t>(datagram.constData()[4]) != 1) return false;
        const std::size_t dcidLength =
            static_cast<std::uint8_t>(datagram.constData()[5]);
        if (dcidLength < 8 || dcidLength > SwQuicConnectionId::kMaxLength ||
            static_cast<std::size_t>(datagram.size()) < 7 + dcidLength) return false;
        const std::size_t scidOffset = 6 + dcidLength;
        const std::size_t scidLength =
            static_cast<std::uint8_t>(datagram.constData()[scidOffset]);
        return scidLength <= SwQuicConnectionId::kMaxLength &&
               static_cast<std::size_t>(datagram.size()) >= 7 + dcidLength + scidLength;
    }

    // Extracts the source connection id and the token from a client Initial
    // whose outer geometry isValidInitial_() already vetted.
    static bool parseInitialScidAndToken_(const SwByteArray& datagram,
                                          SwByteArray& outScid,
                                          SwByteArray& outToken) {
        outScid = SwByteArray();
        outToken = SwByteArray();
        const std::size_t dcidLength =
            static_cast<std::uint8_t>(datagram.constData()[5]);
        std::size_t offset = 6 + dcidLength;
        const std::size_t scidLength =
            static_cast<std::uint8_t>(datagram.constData()[offset]);
        ++offset;
        if (static_cast<std::size_t>(datagram.size()) < offset + scidLength) {
            return false;
        }
        outScid = datagram.mid(static_cast<int>(offset),
                               static_cast<int>(scidLength));
        offset += scidLength;
        std::uint64_t tokenLength = 0;
        if (!SwQuicVarIntCodec::decode(datagram, offset, tokenLength, nullptr) ||
            tokenLength > static_cast<std::uint64_t>(datagram.size()) - offset) {
            return false;
        }
        if (tokenLength > 0) {
            outToken = datagram.mid(static_cast<int>(offset),
                                    static_cast<int>(tokenLength));
        }
        return true;
    }

    std::size_t pendingHandshakeCount_() const {
        std::size_t count = 0;
        for (std::map<std::string, Client_>::const_iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            if (!it->second.established && !it->second.failed) ++count;
        }
        return count;
    }

    void purgeClients_(std::uint64_t now) {
        for (std::map<std::string, Client_>::iterator it = m_clients.begin();
             it != m_clients.end();) {
            Client_& client = it->second;
            const bool handshakeExpired = !client.established &&
                m_handshakeTimeoutMs > 0 &&
                now >= client.createdMs + m_handshakeTimeoutMs;
            const bool connectionClosed = client.established && client.connection &&
                client.connection->state() == SwQuicConnection::State::Closed;
            if (client.failed || handshakeExpired || connectionClosed) {
                const std::string doomedKey = it->first;
                std::map<std::string, Client_>::iterator doomed = it;
                ++it;
                m_clients.erase(doomed);
                for (std::map<std::string, std::string>::iterator cidIt =
                         m_connectionIdIndex.begin();
                     cidIt != m_connectionIdIndex.end();) {
                    if (cidIt->second == doomedKey) {
                        std::map<std::string, std::string>::iterator doomedCid = cidIt;
                        ++cidIt;
                        m_connectionIdIndex.erase(doomedCid);
                    } else {
                        ++cidIt;
                    }
                }
            } else {
                ++it;
            }
        }
    }

    bool processDatagram_(const SwByteArray& datagram,
                          const SwString& sender,
                          uint16_t senderPort,
                          SwString* error) {
        const std::string endpointKey = endpointKey_(sender, senderPort);
        std::string connectionKey;
        std::string destinationConnectionId;
        const bool hasConnectionId =
            extractDestinationConnectionId_(datagram, destinationConnectionId);
        if (hasConnectionId) {
            const std::map<std::string, std::string>::const_iterator indexed =
                m_connectionIdIndex.find(destinationConnectionId);
            if (indexed != m_connectionIdIndex.end()) connectionKey = indexed->second;
        }
        const bool newConnection = connectionKey.empty();
        if (newConnection) {
            connectionKey = hasConnectionId
                ? connectionMapKey_(destinationConnectionId)
                : std::string("peer:") + endpointKey;
        }
        const std::string& key = connectionKey;
        std::map<std::string, Client_>::iterator it = m_clients.find(key);
        if (it == m_clients.end()) {
            if (m_draining) {
                setError_(error, SwString("QUIC HTTP/3 server is draining (graceful shutdown)"));
                return false;
            }
            if (!isValidInitial_(datagram)) {
                setError_(error, SwString("Unroutable QUIC packet is not a valid Initial"));
                return false;
            }

            // Stateless address validation (RFC 9000 8.1.2): under handshake
            // pressure a tokenless Initial gets a Retry and allocates nothing;
            // a token-bearing Initial must authenticate before costing state.
            bool retryValidated = false;
            SwByteArray retryOdcidBytes;
            SwByteArray retryScidBytes;
            if (m_addressValidationEnabled) {
                SwByteArray clientScid;
                SwByteArray token;
                if (!parseInitialScidAndToken_(datagram, clientScid, token)) {
                    setError_(error, SwString("QUIC Initial token parsing failed"));
                    return false;
                }
                const SwByteArray addressBytes(endpointKey.data(),
                                               static_cast<int>(endpointKey.size()));
                const SwByteArray incomingDcid(destinationConnectionId.data(),
                                               static_cast<int>(destinationConnectionId.size()));
                const std::uint64_t nowSeconds = nowMs_() / 1000;
                if (token.isEmpty()) {
                    if (pendingHandshakeCount_() >= m_retryPendingThreshold) {
                        // Mint the 8-byte Retry SCID the client must come back
                        // with; the token binds it, so this path stays stateless.
                        SwByteArray mintedScid;
                        SwString retryError;
                        if (!SwQuicRandom::fill(mintedScid, 8, &retryError)) {
                            setError_(error, retryError);
                            return false;
                        }
                        const SwByteArray retryToken = SwQuicRetry::mintAddressToken(
                            m_retryKey, addressBytes, incomingDcid, mintedScid,
                            nowSeconds);
                        SwByteArray retryPacket;
                        if (retryToken.isEmpty() ||
                            !SwQuicRetry::buildRetryPacket(incomingDcid, clientScid,
                                                           mintedScid, retryToken,
                                                           retryPacket, &retryError)) {
                            setError_(error, retryError.isEmpty()
                                ? SwString("QUIC Retry token minting failed") : retryError);
                            return false;
                        }
                        if (m_socket.writeDatagramCached(
                                retryPacket.constData(),
                                static_cast<int64_t>(retryPacket.size()),
                                sender, senderPort) !=
                            static_cast<int64_t>(retryPacket.size())) {
                            setError_(error, m_socket.errorString());
                            return false;
                        }
                        ++m_retryPacketsSent;
                        if (error) {
                            *error = SwString();
                        }
                        return true; // no state allocated for this Initial
                    }
                } else {
                    SwQuicRetry::AddressToken validated;
                    if (!SwQuicRetry::validateAddressToken(
                            m_retryKey, addressBytes, incomingDcid, token,
                            nowSeconds, m_retryTokenMaxAgeSeconds, validated)) {
                        setError_(error, SwString("QUIC Retry token validation failed"));
                        return false;
                    }
                    retryValidated = true;
                    retryOdcidBytes = validated.originalDestinationConnectionId;
                    retryScidBytes = validated.retrySourceConnectionId;
                }
            }

            if ((m_maxClients > 0 && m_clients.size() >= m_maxClients) ||
                (m_maxPendingHandshakes > 0 &&
                 pendingHandshakeCount_() >= m_maxPendingHandshakes)) {
                setError_(error, SwString("QUIC HTTP/3 pending-handshake limit reached"));
                return false;
            }
            it = m_clients.insert(std::make_pair(key, Client_())).first;
            it->second.createdMs = nowMs_();
            it->second.lastActivityMs = it->second.createdMs;
            if (hasConnectionId) m_connectionIdIndex[destinationConnectionId] = key;
            if (retryValidated) {
                // The handshake must know the pre-Retry ODCID and the Retry
                // SCID: they feed the authenticating transport parameters
                // (RFC 9000 18.2) and select the Initial key generation.
                SwQuicConnectionId odcid;
                SwQuicConnectionId retryScid;
                SwString cidError;
                if (!SwQuicConnectionId::fromBytes(retryOdcidBytes, odcid, &cidError) ||
                    !SwQuicConnectionId::fromBytes(retryScidBytes, retryScid, &cidError) ||
                    !it->second.handshake.setRetryContext(odcid, retryScid)) {
                    it->second.failed = true;
                    setError_(error, cidError.isEmpty()
                        ? SwString("QUIC Retry context installation failed") : cidError);
                    return false;
                }
            }
        }
        Client_& client = it->second;
        const bool addressChanged =
            !newConnection && (client.host != sender || client.port != senderPort);
        // QUIC forbids migrating during the handshake. More importantly, the
        // Initial DCID is observable and cannot authenticate a source tuple.
        if (addressChanged && !client.established) {
            setError_(error, SwString("QUIC migration attempted before handshake completion"));
            return false;
        }
        if (!client.credentialSet) {
            client.handshake.setCredential(m_credential);
            client.handshake.setCredentialSelector(
                [this](const SwString& serverName,
                       SwQuicServerCredential& selected) -> bool {
                    const SwString normalized = serverName.trimmed().toLower();
                    CredentialMap::const_iterator it = m_namedCredentials.find(normalized);
                    if (it != m_namedCredentials.end()) {
                        selected = it.value();
                        return selected.isValid();
                    }
                    selected = m_credential;
                    return selected.isValid();
                });
            client.handshake.setTicketStore(&m_ticketStore); // enable 0-RTT
            client.credentialSet = true;
            client.credentialGeneration = m_credentialGeneration;
        }
        if (newConnection) {
            client.host = sender;
            client.port = senderPort;
        }

        if (!client.established) {
            SwVector<SwByteArray> outgoing;
            if (!client.handshake.processIncomingDatagram(datagram, outgoing, error)) {
                client.failed = true;
                return false;
            }
            client.lastActivityMs = nowMs_();
            if (!client.handshake.serverConnectionId().isEmpty()) {
                m_connectionIdIndex[connectionIdKey_(
                    client.handshake.serverConnectionId().bytes())] = key;
            }
            if (!sendDatagrams_(outgoing, client, error)) {
                client.failed = true;
                return false;
            }
            if (client.handshake.handshakeComplete()) {
                if (client.credentialGeneration != m_credentialGeneration) {
                    setError_(error, SwString(
                        "QUIC credential changed during the pending handshake"));
                    client.failed = true;
                    return false;
                }
                if (!establishConnection_(key, client, error)) {
                    client.failed = true;
                    return false;
                }
            }
            return true;
        }

        // Established: 1-RTT application datagrams drive the HTTP/3 session.
        const std::uint64_t now = nowMs_();
        bool authenticated = false;
        if (!client.connection->receiveDatagram(datagram, now, error, &authenticated)) {
            client.failed = true;
            return false;
        }
        if (!authenticated) return true;
        client.lastActivityMs = now;
        if (addressChanged) {
            if (!client.connection->onPeerAddressChanged(
                    now, static_cast<std::uint64_t>(datagram.size()), error)) {
                client.failed = true;
                return false;
            }
            client.host = sender;
            client.port = senderPort;
        }
        const SwVector<std::uint64_t> touched =
            client.connection->takeTouchedStreamIds();
        if (!client.http3->pumpStreams(touched, error)) {
            client.failed = true;
            return false;
        }
        if (!flushConnection_(client, error)) {
            client.failed = true;
            return false;
        }
        return true;
    }

    bool establishConnection_(const std::string& clientKey,
                              Client_& client,
                              SwString* error) {
        client.connection.reset(new SwQuicConnection(SwQuicConnection::Role::Server));
        client.connection->applyLocalTransportParameters(
            client.handshake.localTransportParameters());
        client.connection->setLocalConnectionId(client.handshake.serverConnectionId());
        client.connection->setPeerConnectionId(client.handshake.clientConnectionId());
        // rx = decrypt the client's 1-RTT, tx = encrypt ours.
        client.connection->setLevelKeys(SwQuicConnection::Level::Application,
                                        client.handshake.clientApplicationKeys(),
                                        client.handshake.serverApplicationKeys());
        client.connection->setHandshakeConfirmed(true);
        // Continue the 1-RTT packet numbers after HANDSHAKE_DONE so no AEAD
        // nonce is reused.
        client.connection->setNextTxPacketNumber(
            SwQuicConnection::Level::Application,
            client.handshake.serverApplicationPacketNumber());
        if (client.handshake.hasPeerTransportParameters()) {
            client.connection->applyPeerTransportParameters(
                client.handshake.peerTransportParameters());
        }

        client.http3.reset(new SwHttp3Server(client.connection.get()));
        client.http3->setLimits(m_httpLimits);
        client.http3->setPendingRequestBudgetHandlers(
            [this](std::size_t bytes) -> bool {
                return reservePendingRequestBytes_(bytes);
            },
            [this](std::size_t bytes) {
                releasePendingRequestBytes_(bytes);
            });
        if (m_asyncRequestHandler) {
            client.http3->setAsyncRequestHandler(makeAsyncRequestHandler_(&client));
            client.http3->setAsyncResponseReadyHandler(
                makeAsyncResponseReadyHandler_(clientKey));
        } else {
            client.http3->setRequestHandler(makeRequestHandler_(&client));
        }
        if (!client.http3->start(error)) {
            return false;
        }
        client.established = true;

        // Replay any 0-RTT early data (STREAM frames the handshake decrypted
        // before this connection existed) so the HTTP/3 request routes now.
        if (client.handshake.acceptedEarlyData()) {
            const SwVector<SwQuicFrame>& early = client.handshake.earlyStreamFrames();
            for (std::size_t i = 0; i < early.size(); ++i) {
                if (!client.connection->injectEarlyStream(early[i].streamId(),
                                                          early[i].offset(),
                                                          early[i].data(),
                                                          early[i].fin(), error)) {
                    return false;
                }
            }
        }

        // Issue a NewSessionTicket for future 0-RTT, sent as a 1-RTT CRYPTO
        // message (RFC 8446 4.6.1).
        if (!issueSessionTicket_(client, error)) {
            return false;
        }

        // Route any replayed early request, then flush (SETTINGS, response,
        // NewSessionTicket).
        const SwVector<std::uint64_t> touched =
            client.connection->takeTouchedStreamIds();
        if (!client.http3->pumpStreams(touched, error)) {
            return false;
        }
        return flushConnection_(client, error);
    }

    bool issueSessionTicket_(Client_& client, SwString* error) {
        SwByteArray ticketBytes;
        SwByteArray ticketNonce;
        if (!SwQuicRandom::fill(ticketBytes, 24, error) ||
            !SwQuicRandom::fill(ticketNonce, 8, error)) {
            return false;
        }
        // Deterministic-free age addend: derive from the ticket bytes so we do
        // not need a clock in this sans-IO layer.
        std::uint32_t ageAdd = 0;
        for (int i = 0; i < 4 && i < ticketBytes.size(); ++i) {
            ageAdd = (ageAdd << 8) | static_cast<std::uint8_t>(ticketBytes.constData()[i]);
        }
        // In QUIC the early_data extension MUST advertise max_early_data_size
        // == 0xffffffff to enable 0-RTT (RFC 9001 4.6.1); the actual 0-RTT
        // volume is bounded by initial_max_data, not this field.
        SwByteArray nstBody;
        if (!client.handshake.issueNewSessionTicket(m_ticketStore, ticketBytes, ticketNonce,
                                                    7200, ageAdd, 0xffffffffu, nstBody, error)) {
            return false;
        }
        // Wrap the body into a NewSessionTicket handshake message (type 0x04)
        // and send it on the 1-RTT CRYPTO stream.
        SwByteArray message;
        message.append(static_cast<char>(0x04));
        message.append(static_cast<char>((nstBody.size() >> 16) & 0xffU));
        message.append(static_cast<char>((nstBody.size() >> 8) & 0xffU));
        message.append(static_cast<char>(nstBody.size() & 0xffU));
        message.append(nstBody);
        client.connection->queueFrame(SwQuicConnection::Level::Application,
                                      SwQuicFrame::crypto(0, message));
        if (error) {
            *error = SwString();
        }
        return true;
    }

    SwHttp3Server::RequestHandler makeRequestHandler_(Client_* client) {
        SwHttpRouter* router = m_router;
        RequestHandler handler = m_requestHandler;
        SwQuicHttp3Server* self = this;
        return [router, handler, self, client](const SwHttpRequest& request) -> SwHttpResponse {
            SwHttpRequest enrichedRequest = request;
            enrichedRequest.isTls = true;
            enrichedRequest.localPort = self->localPort();
            if (client) {
                enrichedRequest.peerAddress = client->host;
                enrichedRequest.peerPort = client->port;
            }
            SwHttpResponse response;
            try {
                if (router) {
                    if (!router->route(enrichedRequest, response)) {
                        response = swHttpTextResponse(404, SwString("Not Found"));
                    }
                } else if (handler) {
                    response = handler(enrichedRequest);
                } else {
                    response = swHttpTextResponse(404, SwString("Not Found"));
                }
            } catch (const std::exception&) {
                response = swHttpTextResponse(500, SwString("Internal Server Error"));
            } catch (...) {
                response = swHttpTextResponse(500, SwString("Internal Server Error"));
            }
            self->requestServed(enrichedRequest, response);
            return response;
        };
    }

    SwHttp3Server::AsyncRequestHandler makeAsyncRequestHandler_(Client_* client) {
        AsyncRequestHandler handler = m_asyncRequestHandler;
        SwQuicHttp3Server* self = this;
        const std::shared_ptr<std::atomic<std::size_t> > pending =
            client ? client->pendingAsyncResponses
                   : std::shared_ptr<std::atomic<std::size_t> >(
                         new std::atomic<std::size_t>(0));
        return [handler, self, client, pending](
                   const SwHttpRequest& request,
                   const SwHttp3Server::ResponseCallback& complete) {
            SwHttpRequest enrichedRequest = request;
            enrichedRequest.isTls = true;
            enrichedRequest.localPort = self->localPort();
            if (client) {
                enrichedRequest.peerAddress = client->host;
                enrichedRequest.peerPort = client->port;
            }
            const std::shared_ptr<std::atomic<bool> > completedOnce(
                new std::atomic<bool>(false));
            pending->fetch_add(1, std::memory_order_acq_rel);
            const SwHttp3Server::ResponseCallback guardedComplete =
                [pending, completedOnce, complete](SwHttpResponse response) mutable {
                    if (completedOnce->exchange(true, std::memory_order_acq_rel)) {
                        return;
                    }
                    try {
                        if (complete) {
                            complete(std::move(response));
                        }
                    } catch (...) {
                        // The session may have closed concurrently. The cycle
                        // accounting still has to reach zero.
                    }
                    pending->fetch_sub(1, std::memory_order_acq_rel);
                };
            if (!handler) {
                guardedComplete(swHttpTextResponse(404, SwString("Not Found")));
                return;
            }
            try {
                handler(enrichedRequest, guardedComplete);
            } catch (const std::exception&) {
                guardedComplete(swHttpTextResponse(500, SwString("Internal Server Error")));
            } catch (...) {
                guardedComplete(swHttpTextResponse(500, SwString("Internal Server Error")));
            }
        };
    }

    SwHttp3Server::AsyncResponseReadyHandler makeAsyncResponseReadyHandler_(
        const std::string& clientKey) {
        SwPointer<SwQuicHttp3Server> self(this);
        const std::shared_ptr<AsyncCompletionCycle_> cycle =
            m_asyncCompletionCycle;
        return [self, cycle, clientKey](std::uint64_t streamId,
                                        SwHttpResponse response) mutable {
            if (!cycle) return;
            AsyncCompletion_ completion;
            completion.clientKey = clientKey;
            completion.streamId = streamId;
            try {
                completion.response.reset(
                    new SwHttpResponse(std::move(response)));
            } catch (...) {
                return;
            }
            {
                SwMutexLocker lock(&cycle->mutex);
                if (!cycle->accepting) return;
                cycle->queue.push_back(std::move(completion));
                cycle->queued.fetch_add(1, std::memory_order_release);
            }

            if (!self) return;
            ThreadHandle* affinity = self->threadHandle();
            if (!affinity || ThreadHandle::currentThread() == affinity) {
                self->drainAsyncCompletions_(cycle);
                return;
            }
            if (!ThreadHandle::postTaskOnLaneReliableIfLive(
                    affinity,
                    [self, cycle]() {
                        if (self) self->drainAsyncCompletions_(cycle);
                    },
                    SwFiberLane::Control)) {
                // The request/queue counters keep the fallback timer armed;
                // poll() will drain this exact response once affinity work is
                // accepted again.
            }
        };
    }

    void drainAsyncCompletions_(
        const std::shared_ptr<AsyncCompletionCycle_>& cycle) {
        if (!cycle || cycle != m_asyncCompletionCycle) return;
        while (true) {
            AsyncCompletion_ completion;
            {
                SwMutexLocker lock(&cycle->mutex);
                if (!cycle->accepting || cycle->queue.empty()) break;
                completion = std::move(cycle->queue.front());
                cycle->queue.pop_front();
                cycle->queued.fetch_sub(1, std::memory_order_acq_rel);
            }
            deliverAsyncCompletion_(completion);
            if (m_closePending || cycle != m_asyncCompletionCycle) break;
        }
        advanceGracefulShutdown_();
        armTimer_();
    }

    void deliverAsyncCompletion_(AsyncCompletion_& completion) {
        if (!completion.response) return;
        std::map<std::string, Client_>::iterator it =
            m_clients.find(completion.clientKey);
        if (it == m_clients.end() || !it->second.http3 ||
            !it->second.connection) {
            return;
        }

        SwHttpRequest completedRequest;
        SwString error;
        if (!it->second.http3->completeAsyncResponse(
                completion.streamId, *completion.response,
                &completedRequest, &error)) {
            it->second.failed = true;
            if (!error.isEmpty()) serverError(error);
            return;
        }
        // An empty protocol identifies a stale/duplicate completion after a
        // route timeout, request reset, or client purge.
        if (completedRequest.protocol.isEmpty()) return;

        requestServed(completedRequest, *completion.response);
        if (m_closePending) return;
        it = m_clients.find(completion.clientKey);
        if (it == m_clients.end() || !it->second.connection) return;
        if (!flushConnection_(it->second, &error)) {
            it->second.failed = true;
            if (!error.isEmpty()) serverError(error);
        }
    }

    // During a graceful shutdown, close each session once its HTTP/3 work is
    // done AND the transport confirmed delivery of every queued byte (GOAWAY,
    // final responses). CONNECTION_CLOSE with H3_NO_ERROR then ends it.
    void advanceGracefulShutdown_() {
        if (!m_draining) {
            return;
        }
        for (std::map<std::string, Client_>::iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            Client_& client = it->second;
            if (!client.established || client.failed ||
                !client.connection || !client.http3) {
                continue;
            }
            if (client.connection->state() != SwQuicConnection::State::Open) {
                continue;
            }
            if (!client.http3->isDrained() ||
                client.connection->hasUnacknowledgedSendData()) {
                continue;
            }
            client.connection->close(0x100 /* H3_NO_ERROR */,
                                     SwString("graceful shutdown"));
            SwString error;
            if (!flushConnection_(client, &error)) {
                client.failed = true;
                if (!error.isEmpty()) serverError(error);
            }
        }
    }

    // Drive per-connection QUIC timers (PTO retransmission, idle) for every
    // established client, then flush any resulting datagrams.
    void fireTimers_() {
        const std::uint64_t now = nowMs_();
        for (std::map<std::string, Client_>::iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            Client_& client = it->second;
            if (!client.established || !client.connection) {
                continue;
            }
            if (client.connection->nextTimeoutMs(now) == 0) {
                client.connection->onTimeout(now);
            }
            SwString ignored;
            if (!flushConnection_(client, &ignored)) client.failed = true;
        }
    }

    bool flushConnection_(Client_& client, SwString* error) {
        if (client.http3 && !client.http3->pumpResponseStreams(error)) {
            return false;
        }
        SwVector<SwByteArray> outgoing;
        if (!client.connection->buildDatagrams(nowMs_(), outgoing, error)) {
            return false;
        }
        return sendDatagrams_(outgoing, client, error);
    }

    bool sendDatagrams_(const SwVector<SwByteArray>& datagrams,
                        const Client_& client,
                        SwString* error) {
        for (std::size_t i = 0; i < datagrams.size(); ++i) {
            const int64_t sent = m_socket.writeDatagramCached(
                datagrams[i].constData(), static_cast<int64_t>(datagrams[i].size()),
                client.host, client.port);
            if (sent != static_cast<int64_t>(datagrams[i].size())) {
                setError_(error, m_socket.errorString());
                return false;
            }
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    void emitClientRejected_(const SwString& sender, uint16_t senderPort, const SwString& reason) {
        clientRejected(sender, senderPort, reason);
    }

    SwUdpSocket m_socket;
    SwQuicServerCredential m_credential;
    CredentialMap m_namedCredentials;
    SwHttpRouter* m_router;
    RequestHandler m_requestHandler;
    AsyncRequestHandler m_asyncRequestHandler;
    PendingBytesReserveHandler m_pendingBytesReserveHandler;
    PendingBytesReleaseHandler m_pendingBytesReleaseHandler;
    // Shared by all live H3 sessions; declared before m_clients so it remains
    // alive while their destructors return outstanding budget charges.
    std::size_t m_pendingRequestBytesGlobal = 0;
    std::map<std::string, Client_> m_clients;
    std::map<std::string, std::string> m_connectionIdIndex;
    SwQuicTicketStore m_ticketStore; // 0-RTT resumption tickets
    std::uint64_t m_credentialGeneration = 0;
    std::shared_ptr<AsyncCompletionCycle_> m_asyncCompletionCycle;
    SwTimer* m_tickTimer = nullptr;
    bool m_polling = false;
    bool m_closePending = false;
    bool m_automaticPolling = false;
    bool m_draining = false;
    bool m_addressValidationEnabled = false;
    std::size_t m_retryPendingThreshold = 0;
    std::uint64_t m_retryTokenMaxAgeSeconds = 30;
    std::uint64_t m_retryPacketsSent = 0;
    SwByteArray m_retryKey;
    std::size_t m_maxClients = 4096;
    std::size_t m_maxPendingHandshakes = 1024;
    std::uint64_t m_handshakeTimeoutMs = 10000;
    SwHttpLimits m_httpLimits;
};

#endif
