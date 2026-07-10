#ifndef SWQUICHTTP3SERVER_H
#define SWQUICHTTP3SERVER_H

#include "SwObject.h"
#include "SwString.h"
#include "SwUdpSocket.h"
#include "SwVector.h"
#include "http/SwHttpRouter.h"
#include "http/SwHttpTypes.h"
#include "http3/SwHttp3Server.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicHandshakeServer.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwQuicServerCredential.h"
#include "quic/SwQuicSessionTicket.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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

public:
    typedef std::function<SwHttpResponse(const SwHttpRequest&)> RequestHandler;

    explicit SwQuicHttp3Server(SwObject* parent = nullptr)
        : SwObject(parent),
          m_router(nullptr) {
        // Note: poll() is driven explicitly by the owner (event-loop timer or a
        // dedicated loop). We deliberately do NOT auto-connect the socket's
        // readyRead to poll() -- draining the socket inside poll() re-emits
        // readyRead, which would re-enter poll() (a fiber may even run it on
        // another thread) and corrupt per-client state.
    }

    ~SwQuicHttp3Server() override {
        close();
    }

    // The credential (certificate chain + CertificateVerify signer) the server
    // presents to every client. Required before listen()/poll().
    void setCredential(const SwQuicServerCredential& credential) { m_credential = credential; }

    // Route requests through an SwHttpRouter (shared with the HTTP/1.x server).
    void setRouter(SwHttpRouter* router) { m_router = router; }

    // Or dispatch through a plain handler (takes precedence only if no router).
    void setRequestHandler(const RequestHandler& handler) { m_requestHandler = handler; }
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

    bool listen(const SwString& bindAddress, uint16_t port, SwString* error = nullptr) {
        close();
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
            setError_(error, m_socket.errorString());
            return false;
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    void close() {
        m_socket.close();
        m_clients.clear();
        m_connectionIdIndex.clear();
    }

    bool isListening() const { return m_socket.isOpen(); }
    uint16_t localPort() const { return m_socket.localPort(); }
    SwString localAddress() const { return m_socket.localAddress(); }
    std::size_t clientCount() const { return m_clients.size(); }

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
        m_polling = true;
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
        }

        fireTimers_();
        purgeClients_(nowMs_());

        m_polling = false;
        if (error) {
            *error = SwString();
        }
        return processed;
    }

    std::int64_t nextTimeoutMs() const { return nextTimeoutMs_(); }

signals:
    DECLARE_SIGNAL(requestServed, const SwHttpRequest&, const SwHttpResponse&)
    DECLARE_SIGNAL(clientRejected, const SwString&, uint16_t, const SwString&)

private:
    struct Client_ {
        SwQuicHandshakeServer handshake;
        std::unique_ptr<SwQuicConnection> connection;
        std::unique_ptr<SwHttp3Server> http3;
        SwString host;
        uint16_t port;
        bool credentialSet;
        bool established;
        bool failed;
        std::uint64_t createdMs;
        std::uint64_t lastActivityMs;

        Client_() : port(0), credentialSet(false), established(false), failed(false),
                    createdMs(0), lastActivityMs(0) {}
    };

    std::int64_t nextTimeoutMs_() const {
        const std::uint64_t now = nowMs_();
        std::int64_t next = -1;
        for (std::map<std::string, Client_>::const_iterator it = m_clients.begin();
             it != m_clients.end(); ++it) {
            const Client_& client = it->second;
            std::int64_t candidate = -1;
            if (client.established && client.connection) {
                candidate = client.connection->nextTimeoutMs(now);
            } else if (m_handshakeTimeoutMs > 0) {
                const std::uint64_t deadline = client.createdMs + m_handshakeTimeoutMs;
                candidate = deadline <= now ? 0
                    : static_cast<std::int64_t>(deadline - now);
            }
            if (candidate >= 0 && (next < 0 || candidate < next)) next = candidate;
        }
        return next;
    }

    static void setError_(SwString* error, const SwString& message) {
        if (error) {
            *error = message;
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
            if (!isValidInitial_(datagram)) {
                setError_(error, SwString("Unroutable QUIC packet is not a valid Initial"));
                return false;
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
            client.handshake.setTicketStore(&m_ticketStore); // enable 0-RTT
            client.credentialSet = true;
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
                if (!establishConnection_(client, error)) {
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

    bool establishConnection_(Client_& client, SwString* error) {
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
        client.http3->setRequestHandler(makeRequestHandler_());
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

    SwHttp3Server::RequestHandler makeRequestHandler_() {
        SwHttpRouter* router = m_router;
        RequestHandler handler = m_requestHandler;
        SwQuicHttp3Server* self = this;
        return [router, handler, self](const SwHttpRequest& request) -> SwHttpResponse {
            SwHttpResponse response;
            if (router) {
                if (!router->route(request, response)) {
                    response = swHttpTextResponse(404, SwString("Not Found"));
                }
            } else if (handler) {
                response = handler(request);
            } else {
                response = swHttpTextResponse(404, SwString("Not Found"));
            }
            self->requestServed(request, response);
            return response;
        };
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
    SwHttpRouter* m_router;
    RequestHandler m_requestHandler;
    std::map<std::string, Client_> m_clients;
    std::map<std::string, std::string> m_connectionIdIndex;
    SwQuicTicketStore m_ticketStore; // 0-RTT resumption tickets
    bool m_polling = false;
    std::size_t m_maxClients = 4096;
    std::size_t m_maxPendingHandshakes = 1024;
    std::uint64_t m_handshakeTimeoutMs = 10000;
};

#endif
