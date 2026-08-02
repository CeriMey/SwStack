#ifndef SWHTTP3CLIENT_H
#define SWHTTP3CLIENT_H

#include "SwByteArray.h"
#include "SwCoreApplication.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwString.h"
#include "SwTimer.h"
#include "SwUdpSocket.h"
#include "SwVector.h"
#include "http3/SwHttp3Connection.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicHandshakeClient.h"
#include "quic/SwQuicSessionTicket.h"
#include "quic/SwTls13Messages.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

// Event-driven HTTP/3 client (RFC 9114 over QUIC v1 + TLS 1.3).
//
// It is the QUIC counterpart of SwHttpClient and follows the same SwObject
// conventions: get()/post()/request() start the request and return whether it
// started, then finished(body) or errorOccurred(message) settles it. Results
// (statusCode(), responseBody(), responseHeaders(), sessionTicket()) stay
// readable until the next request. The client owns one SwUdpSocket per
// request; with an SwCoreApplication event loop running, incoming datagrams
// arrive through the socket's readyRead signal and the QUIC timers (PTO
// retransmissions, overall deadline) through a single-shot SwTimer.
//
// On a thread without an event loop, start the request the same way and call
// waitForFinished(): it drives the exact same state machine by polling the
// socket, so blocking harnesses keep a synchronous request path.
//
// ALPN is "h3"; QPACK is static-table only (always interoperable). For a
// self-signed peer, call setVerifyCertificateChain(false) to keep verifying the
// CertificateVerify signature while not requiring a trusted root.
class SwHttp3Client : public SwObject {
    SW_OBJECT(SwHttp3Client, SwObject)

public:
    explicit SwHttp3Client(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwHttp3Client() override {
        abort();
        drainRetiredSockets_();
    }

    void setVerifyPeer(bool verify) { m_verifyPeer = verify; }
    void setVerifyCertificateChain(bool verify) { m_verifyChain = verify; }

    // Resume with a previously captured ticket: the next request is sent as
    // 0-RTT early data if the ticket permits it.
    void setResumptionTicket(const SwQuicSessionTicket& ticket) {
        m_resumptionTicket = ticket;
        m_haveResumptionTicket = true;
    }
    // A NewSessionTicket captured from the last request (valid() when present),
    // usable with setResumptionTicket() for a subsequent 0-RTT request.
    bool hasSessionTicket() const { return m_receivedTicket.valid; }
    const SwQuicSessionTicket& sessionTicket() const { return m_receivedTicket; }
    // Whether the last request's early data was accepted as 0-RTT.
    bool earlyDataAccepted() const { return m_earlyDataAccepted; }

    int statusCode() const { return m_statusCode; }
    const SwByteArray& responseBody() const { return m_responseBody; }
    SwString responseBodyAsString() const { return SwString(m_responseBody.toStdString()); }
    const SwMap<SwString, SwString>& responseHeaders() const { return m_responseHeaders; }
    const SwString& errorString() const { return m_errorString; }
    bool isRunning() const { return m_running; }

    // Extra request headers applied to every request until cleared.
    void setRawHeader(const SwByteArray& name, const SwByteArray& value) {
        m_requestHeaders.push_back(std::make_pair(name, value));
    }
    void clearRawHeaders() { m_requestHeaders.clear(); }

    bool get(const SwString& host, uint16_t port, const SwString& path,
             int timeoutMs = 10000) {
        return request(SwByteArray("GET"), host, port, path, SwByteArray(),
                       SwByteArray(), timeoutMs);
    }

    bool post(const SwString& host, uint16_t port, const SwString& path,
              const SwByteArray& body,
              const SwByteArray& contentType = SwByteArray("application/json"),
              int timeoutMs = 10000) {
        return request(SwByteArray("POST"), host, port, path, body, contentType,
                       timeoutMs);
    }

    // Starts the request; returns false (and emits errorOccurred) when it could
    // not start. A request already in flight is silently aborted first.
    bool request(const SwByteArray& method, const SwString& host, uint16_t port,
                 const SwString& path, const SwByteArray& body,
                 const SwByteArray& contentType, int timeoutMs = 10000) {
        abort();
        // Free any socket retired by a previous no-event-loop request now that
        // we are on a safe frame (outside every signal emission).
        drainRetiredSockets_();
        ++m_generation;

        m_statusCode = 0;
        m_responseBody = SwByteArray();
        m_responseHeaders.clear();
        m_errorString = SwString();
        m_responseStream = SwByteArray();
        m_earlyDataAccepted = false;
        m_succeeded = false;

        // Event-loop mode iff this thread runs an SwCoreApplication. It decides
        // how the socket is serviced and must stay fixed for the request.
        m_eventLoopMode = (SwCoreApplication::instance(false) != nullptr);

        m_socket = new SwUdpSocket(this);
        m_socket->setMaxPendingDatagrams(64);
        if (!m_eventLoopMode) {
            // No loop here: SwObject affinity falls back to the framework's
            // shared worker thread, which runs its OWN loop. If it were allowed
            // to service the fd, its readyRead -> step_ would run concurrently
            // with waitForFinished()'s step_ on this thread and corrupt the
            // shared state (torn m_connection/m_responseStream, double teardown).
            // Silence its notifications and drive purely by polling instead.
            m_socket->setReadNotificationsEnabled(false);
        }
        // Bind the wildcard matching the target's address family: an IPv6-only
        // or IPv6-preferred peer (e.g. Google) is unreachable from an IPv4-only
        // socket. The send path resolves the same host, so the families agree.
        SwUdpSocket::ResolvedAddress resolvedTarget;
        const bool targetIsV6 =
            m_socket->resolveHostAddress(host, port, resolvedTarget) &&
            resolvedTarget.family == AF_INET6;
        if (!m_socket->bind(targetIsV6 ? SwString("::") : SwString("0.0.0.0"), 0)) {
            return startFailed_(m_socket->errorString());
        }
        // Only wire readyRead when the loop lives on THIS thread, so step_()
        // has a single driver.
        if (m_eventLoopMode) {
            connect(m_socket, &SwIODevice::readyRead, this, &SwHttp3Client::onSocketReadyRead_);
        }

        // Build the HTTP/3 request first: when resuming it is sent as 0-RTT
        // early data inside the handshake's very first flight.
        SwHttp3Connection::Request h3Request;
        h3Request.method = method;
        h3Request.scheme = SwByteArray("https");
        h3Request.authority = SwByteArray(host.toStdString());
        h3Request.path = SwByteArray(path.toStdString());
        h3Request.extraHeaders = m_requestHeaders;
        if (!contentType.isEmpty()) {
            h3Request.extraHeaders.push_back(std::make_pair(SwByteArray("content-type"),
                                                            contentType));
        }
        h3Request.body = body;

        SwString error;
        if (!SwHttp3Connection::buildRequest(h3Request, m_requestStream, &error)) {
            return startFailed_(error);
        }

        m_handshake.reset(new SwQuicHandshakeClient());
        m_handshake->setVerifyPeer(m_verifyPeer);
        m_handshake->setVerifyCertificateChain(m_verifyChain);
        // Only send 0-RTT when we have a ticket that allows early data AND the
        // request fits within the server's remembered max_early_data_size
        // (RFC 9001 4.6). Otherwise resume without early data / fall back to
        // a full 1-RTT request.
        m_resuming = m_haveResumptionTicket &&
                     m_resumptionTicket.allowsEarlyData() &&
                     static_cast<std::uint64_t>(m_requestStream.size()) <=
                         m_resumptionTicket.serverInitialMaxData &&
                     m_requestStream.size() <= kMaxSingleFlightEarlyRequestBytes_();
        if (m_resuming) {
            m_handshake->setResumption(m_resumptionTicket, m_requestStream);
        }

        // A hybrid (X25519MLKEM768) ClientHello spans several independently
        // protected Initial datagrams. They MUST go out as separate UDP
        // datagrams: concatenating them exceeds the receiver's per-datagram
        // buffer and the QUIC max_udp_payload_size.
        SwVector<SwByteArray> initialFlight;
        if (!m_handshake->start(host, initialFlight, &error)) {
            return startFailed_(error);
        }

        m_host = host;
        m_port = port;
        m_timeoutMs = timeoutMs;
        for (std::size_t i = 0; i < initialFlight.size(); ++i) {
            if (!sendDatagram_(initialFlight[i], error)) {
                return startFailed_(error);
            }
        }

        m_deadlineAt = nowMs_() + static_cast<std::uint64_t>(timeoutMs);
        m_phase = Phase::Handshaking;
        m_running = true;
        armTimer_();
        return true;
    }

    // Silently cancels any in-flight request: no signal is emitted and the
    // previous results are left untouched.
    void abort() {
        teardown_();
    }

    // Synchronous bridge for threads without an event loop: polls the socket
    // and drives the state machine until finished/errorOccurred has been
    // emitted, then returns whether the request succeeded. Must not be called
    // from within this client's own signal handlers.
    bool waitForFinished() {
        // Must not be re-entered from a signal handler; it also would not be
        // meaningful (the request it was waiting on has already settled).
        if (m_inWait || m_inStep) {
            return false;
        }
        m_inWait = true;
        // Synchronous drive: teardown_ must free the socket on THIS thread (no
        // exec() runs to pump a deleteLater()), which it does by retiring it for
        // the drain below.
        m_synchronousDrive = true;
        // Drive only THIS request: if a finished/errorOccurred slot chains a new
        // one, m_generation moves on and we stop rather than reporting the
        // wrong request's result.
        const std::uint64_t gen = m_generation;
        while (m_running && m_generation == gen) {
            if (m_socket) {
                m_socket->pollPendingDatagrams(5);
            }
            step_();
        }
        // We are back on waitForFinished()'s own frame, fully outside any socket
        // signal emission: it is now safe to free sockets retired during step_.
        m_synchronousDrive = false;
        drainRetiredSockets_();
        m_inWait = false;
        return (m_settledGeneration == gen) && m_settledOk;
    }

signals:
    DECLARE_SIGNAL(finished, const SwByteArray&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private slots:
    void onSocketReadyRead_() { step_(); }
    void onTimer_() { step_(); }

private:
    enum class Phase { Idle, Handshaking, Response };
    enum class Outcome { Pending, Failed, Done };

    // Clears the re-entrancy flag on every exit path, including an exception
    // thrown out of advance_() (e.g. std::bad_alloc) -- without this the client
    // would latch m_inStep=true and ignore every later step_() forever.
    struct InStepGuard_ {
        explicit InStepGuard_(bool& flag) : m_flag(flag) {}
        ~InStepGuard_() { m_flag = false; }
        bool& m_flag;
    };

    // The current handshake API coalesces one 0-RTT packet behind the
    // minimum-sized Initial in a single UDP datagram. Keep that first flight
    // below the QUIC drivers' 2048-byte receive slots; larger requests are
    // sent after the handshake as ordinary packetised 1-RTT stream data.
    static std::size_t kMaxSingleFlightEarlyRequestBytes_() { return 700; }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // One turn of the state machine, triggered by readyRead, the QUIC timer or
    // waitForFinished(). Signals are emitted only after the request state has
    // been fully torn down, so a finished/errorOccurred handler may immediately
    // start the next request on this same client.
    void step_() {
        if (!m_running || m_inStep) {
            return;
        }
        // m_inStep stays asserted across the settling signal below (so a slot
        // cannot re-enter step_/waitForFinished) and is cleared on every exit,
        // including an exception, by the guard.
        m_inStep = true;
        const InStepGuard_ inStepGuard(m_inStep);
        SwString error;
        const Outcome outcome = advance_(error);
        if (outcome == Outcome::Failed) {
            teardown_();
            m_errorString = error;
            settle_(false);
            emit errorOccurred(m_errorString);
        } else if (outcome == Outcome::Done) {
            teardown_();
            SwString parseError;
            if (!finishResponse_(parseError)) {
                m_errorString = parseError;
                settle_(false);
                emit errorOccurred(m_errorString);
            } else {
                settle_(true);
                emit finished(m_responseBody);
            }
        }
    }

    // Records the outcome of the current request BEFORE the settling signal is
    // emitted, so waitForFinished() reports this request's result even if a slot
    // synchronously chains another one (which bumps m_generation).
    void settle_(bool ok) {
        m_succeeded = ok;
        m_settledOk = ok;
        m_settledGeneration = m_generation;
    }

    Outcome advance_(SwString& error) {
        // Drain every queued datagram into the current stage.
        while (m_socket && m_socket->hasPendingDatagrams()) {
            SwString from;
            uint16_t fromPort = 0;
            const SwByteArray datagram = m_socket->receiveDatagram(&from, &fromPort);
            if (datagram.isEmpty()) {
                continue;
            }
            if (m_phase == Phase::Handshaking) {
                SwVector<SwByteArray> replies;
                if (!m_handshake->processIncomingDatagram(datagram, replies, &error)) {
                    return Outcome::Failed;
                }
                for (std::size_t i = 0; i < replies.size(); ++i) {
                    if (!sendDatagram_(replies[i], error)) {
                        return Outcome::Failed;
                    }
                }
            } else {
                if (!m_connection->receiveDatagram(datagram, nowMs_(), &error)) {
                    return Outcome::Failed;
                }
            }
        }

        if (m_phase == Phase::Handshaking && m_handshake->handshakeComplete()) {
            if (!setupConnection_(error)) {
                return Outcome::Failed;
            }
            m_phase = Phase::Response;
            // The response gets its own full timeout window, exactly like the
            // former blocking driver gave it.
            m_deadlineAt = nowMs_() + static_cast<std::uint64_t>(m_timeoutMs);
        }

        if (m_phase == Phase::Response) {
            m_responseStream.append(m_connection->readStream(0));
            const SwQuicStream* stream0 = m_connection->streams().stream(0);
            if (!m_responseStream.isEmpty() && stream0 && stream0->isReceiveComplete()) {
                // Capture a NewSessionTicket delivered on the 1-RTT CRYPTO
                // stream, for future 0-RTT.
                captureSessionTicket_();
                return Outcome::Done;
            }

            // Drive the timers so a PTO retransmits the request if it (or the
            // response) was lost or reordered ahead of the server being ready,
            // then flush ACKs / retransmissions / probes back to the server.
            if (m_connection->nextTimeoutMs(nowMs_()) == 0) {
                m_connection->onTimeout(nowMs_());
            }
            SwVector<SwByteArray> pending;
            SwString flushError;
            if (m_connection->buildDatagrams(nowMs_(), pending, &flushError)) {
                for (std::size_t i = 0; i < pending.size(); ++i) {
                    if (!sendDatagram_(pending[i], error)) {
                        return Outcome::Failed;
                    }
                }
            }
        }

        if (nowMs_() >= m_deadlineAt) {
            error = (m_phase == Phase::Handshaking)
                ? SwString("QUIC/TLS handshake did not complete before timeout")
                : SwString("HTTP/3 response did not complete before timeout");
            return Outcome::Failed;
        }
        armTimer_();
        return Outcome::Pending;
    }

    // 1-RTT connection over the derived application keys, client control
    // stream, and the request itself (unless it already went out as accepted
    // 0-RTT early data; on 0-RTT rejection the request is resent here).
    bool setupConnection_(SwString& error) {
        m_connection.reset(new SwQuicConnection(SwQuicConnection::Role::Client));
        m_connection->applyLocalTransportParameters(m_handshake->localTransportParameters());
        m_connection->setLocalConnectionId(m_handshake->sourceConnectionId());
        m_connection->setPeerConnectionId(m_handshake->destinationConnectionId());
        m_connection->setLevelKeys(SwQuicConnection::Level::Application,
                                   m_handshake->serverApplicationKeys(),
                                   m_handshake->clientApplicationKeys());
        m_connection->setHandshakeConfirmed(true);
        if (m_handshake->hasPeerTransportParameters()) {
            m_connection->applyPeerTransportParameters(m_handshake->peerTransportParameters());
        }
        // 0-RTT and 1-RTT share the Application packet-number space: continue
        // the 1-RTT numbers after any 0-RTT packets already sent (RFC 9001 5.4).
        m_connection->setNextTxPacketNumber(SwQuicConnection::Level::Application,
                                            m_handshake->clientEarlyPacketNumber());

        m_earlyDataAccepted = m_resuming && m_handshake->earlyDataAccepted();
        if (m_earlyDataAccepted) {
            m_connection->registerLocalStream(0);
        }

        // RFC 9114 6.2.1: each side MUST open a control stream and send its
        // SETTINGS as the first frame. Open the client control stream
        // (client-initiated unidirectional, id 2) before the request.
        {
            SwHttp3Frame::SettingList settings;
            settings.push_back(std::make_pair(SwHttp3Frame::settingQpackMaxTableCapacity(),
                                              std::uint64_t(0)));
            settings.push_back(std::make_pair(SwHttp3Frame::settingQpackBlockedStreams(),
                                              std::uint64_t(0)));
            settings.push_back(std::make_pair(SwHttp3Frame::settingMaxFieldSectionSize(),
                                              std::uint64_t(65536)));
            SwByteArray controlStream;
            if (!SwHttp3Connection::buildControlStream(settings, controlStream, &error)) {
                return false;
            }
            if (!m_connection->sendStreamData(2, controlStream, false, &error)) {
                return false;
            }
        }

        if (!m_earlyDataAccepted) {
            if (!m_connection->sendStreamData(0, m_requestStream, true, &error)) {
                return false;
            }
        }

        SwVector<SwByteArray> outgoing;
        if (!m_connection->buildDatagrams(nowMs_(), outgoing, &error)) {
            return false;
        }
        for (std::size_t i = 0; i < outgoing.size(); ++i) {
            if (!sendDatagram_(outgoing[i], error)) {
                return false;
            }
        }
        return true;
    }

    bool sendDatagram_(const SwByteArray& datagram, SwString& error) {
        if (m_socket->writeDatagramCached(datagram.constData(),
                                          static_cast<int64_t>(datagram.size()),
                                          m_host, m_port) !=
            static_cast<int64_t>(datagram.size())) {
            error = m_socket->errorString();
            return false;
        }
        return true;
    }

    // Wake-up point for event-loop use: the earlier of the overall deadline and
    // the connection's next QUIC timer. On a thread without an
    // SwCoreApplication no timer is armed -- waitForFinished() paces us there.
    void armTimer_() {
        // No loop on this thread -> no timer; waitForFinished() paces us.
        if (!m_eventLoopMode) {
            return;
        }
        if (!m_timer) {
            m_timer = new SwTimer(this);
            m_timer->setSingleShot(true);
            connect(m_timer, &SwTimer::timeout, this, &SwHttp3Client::onTimer_);
        }
        const std::uint64_t now = nowMs_();
        std::int64_t delayMs = (m_deadlineAt > now)
            ? static_cast<std::int64_t>(m_deadlineAt - now) : 1;
        if (m_phase == Phase::Response && m_connection) {
            const std::int64_t quicTimeout = m_connection->nextTimeoutMs(now);
            if (quicTimeout >= 0 && quicTimeout < delayMs) {
                delayMs = quicTimeout;
            }
        }
        if (delayMs < 1) {
            delayMs = 1;
        }
        const std::int64_t maxDelay =
            static_cast<std::int64_t>((std::numeric_limits<int>::max)());
        if (delayMs > maxDelay) {
            delayMs = maxDelay;
        }
        m_timer->stop();
        m_timer->start(static_cast<int>(delayMs));
    }

    bool startFailed_(const SwString& message) {
        teardown_();
        m_errorString = message;
        emit errorOccurred(m_errorString);
        return false;
    }

    void teardown_() {
        m_running = false;
        m_phase = Phase::Idle;
        if (m_timer) {
            m_timer->stop();
        }
        if (m_socket) {
            SwUdpSocket* socket = m_socket;
            m_socket = nullptr;
            // Sever callbacks first: in the event-loop path teardown_() runs
            // from inside the socket's own readyRead slot, so nothing must call
            // back into us afterwards.
            socket->disconnectAllSlots();
            socket->setParent(nullptr);
            if (m_synchronousDrive && m_eventLoopMode) {
                // An SwCoreApplication lives on this thread, but we are driving
                // it synchronously (waitForFinished(), no exec()): deleteLater()
                // would enqueue a deferred delete that nothing ever pumps -> the
                // socket leaks. Its affinity is THIS thread, so retire it and
                // free it deterministically at the waitForFinished() tail,
                // outside the signal frame.
                retireSocket_(socket);
            } else {
                if (m_eventLoopMode) {
                    // Our own thread owns the loop: release the fd now, and let
                    // deleteLater() post a same-thread deferred delete that runs
                    // after the current signal unwinds.
                    socket->close();
                }
                // deleteLater() frees the socket on its affinity thread (this
                // loop, or the shared fallback loop when this thread has none),
                // serialized with any pending notification -- never freed out
                // from under an in-flight callback. ~SwUdpSocket close()s the fd.
                // Only if no thread can take the task do we retire it.
                if (!socket->deleteLater()) {
                    retireSocket_(socket);
                }
            }
        }
        m_handshake.reset();
        m_connection.reset();
        m_requestStream = SwByteArray();
    }

    void retireSocket_(SwUdpSocket* socket) {
        m_retiredSockets.push_back(socket);
    }

    // Frees every retired socket. Call ONLY from a frame that is outside any
    // retired socket's signal emission (they are already disconnected, so they
    // cannot call back once here).
    void drainRetiredSockets_() {
        std::vector<SwUdpSocket*> sockets;
        sockets.swap(m_retiredSockets);
        for (std::size_t i = 0; i < sockets.size(); ++i) {
            sockets[i]->close();
            delete sockets[i];
        }
    }

    // Parse NewSessionTicket messages from the 1-RTT CRYPTO stream and store
    // the resulting resumption ticket (RFC 8446 4.6.1).
    void captureSessionTicket_() {
        const SwByteArray appCrypto =
            m_connection->cryptoData(SwQuicConnection::Level::Application);
        if (appCrypto.isEmpty() || m_receivedTicket.valid) {
            return;
        }
        SwVector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(appCrypto, messages, nullptr)) {
            return;
        }
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].type == 0x04) { // NewSessionTicket
                SwQuicSessionTicket ticket;
                if (m_handshake->processNewSessionTicket(messages[i].body, SwByteArray(),
                                                         ticket, nullptr)) {
                    m_receivedTicket = ticket;
                    return;
                }
            }
        }
    }

    bool finishResponse_(SwString& error) {
        SwHttp3Connection::Response response;
        if (!SwHttp3Connection::parseResponse(m_responseStream, response, &error)) {
            return false;
        }
        m_statusCode = SwString(response.status.toStdString()).toInt();
        m_responseBody = response.body;
        for (std::size_t i = 0; i < response.headers.size(); ++i) {
            m_responseHeaders[SwString(response.headers[i].first.toStdString())] =
                SwString(response.headers[i].second.toStdString());
        }
        return true;
    }

    bool m_verifyPeer = true;
    bool m_verifyChain = true;
    std::vector<std::pair<SwByteArray, SwByteArray> > m_requestHeaders;

    // 0-RTT resumption.
    bool m_haveResumptionTicket = false;
    SwQuicSessionTicket m_resumptionTicket;
    SwQuicSessionTicket m_receivedTicket;
    bool m_earlyDataAccepted = false;

    // Last settled request.
    int m_statusCode = 0;
    SwByteArray m_responseBody;
    SwMap<SwString, SwString> m_responseHeaders;
    SwString m_errorString;
    bool m_succeeded = false;
    // Per-request identity + settled outcome, so waitForFinished() can report
    // the specific request it drove even across a slot-chained follow-up.
    std::uint64_t m_generation = 0;
    std::uint64_t m_settledGeneration = 0;
    bool m_settledOk = false;
    bool m_inWait = false;
    // Sockets awaiting deterministic same-thread deletion (no-event-loop path).
    std::vector<SwUdpSocket*> m_retiredSockets;

    // In-flight request state.
    SwUdpSocket* m_socket = nullptr;
    SwTimer* m_timer = nullptr;
    std::unique_ptr<SwQuicHandshakeClient> m_handshake;
    std::unique_ptr<SwQuicConnection> m_connection;
    SwByteArray m_requestStream;
    SwByteArray m_responseStream;
    SwString m_host;
    uint16_t m_port = 0;
    int m_timeoutMs = 0;
    bool m_resuming = false;
    std::uint64_t m_deadlineAt = 0;
    Phase m_phase = Phase::Idle;
    bool m_running = false;
    bool m_inStep = false;
    // Fixed per request: whether this thread drives its own event loop. Governs
    // socket-notification wiring, timer arming, and teardown disposal.
    bool m_eventLoopMode = false;
    // True only while a waitForFinished() drive loop is active on this thread.
    bool m_synchronousDrive = false;
};

#endif
