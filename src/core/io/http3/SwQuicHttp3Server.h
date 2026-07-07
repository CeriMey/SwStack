#ifndef SWQUICHTTP3SERVER_H
#define SWQUICHTTP3SERVER_H

#include "SwObject.h"
#include "SwString.h"
#include "SwUdpSocket.h"
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

    bool listen(const SwString& bindAddress, uint16_t port, SwString* error = nullptr) {
        close();
        if (!m_credential.isValid()) {
            setError_(error, SwString("QUIC HTTP/3 server has no valid credential"));
            return false;
        }
        m_socket.setMaxDatagramSize(65536);
        m_socket.setMaxPendingDatagrams(2048);
        m_socket.setMaxReadBatchDatagrams(256);

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

        m_polling = false;
        if (error) {
            *error = SwString();
        }
        return processed;
    }

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

        Client_() : port(0), credentialSet(false), established(false) {}
    };

    static void setError_(SwString* error, const SwString& message) {
        if (error) {
            *error = message;
        }
    }

    static std::string endpointKey_(const SwString& host, uint16_t port) {
        return host.toStdString() + ":" + SwString::number(static_cast<int>(port)).toStdString();
    }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    bool processDatagram_(const SwByteArray& datagram,
                          const SwString& sender,
                          uint16_t senderPort,
                          SwString* error) {
        const std::string key = endpointKey_(sender, senderPort);
        std::map<std::string, Client_>::iterator it = m_clients.find(key);
        if (it == m_clients.end()) {
            it = m_clients.insert(std::make_pair(key, Client_())).first;
        }
        Client_& client = it->second;
        if (!client.credentialSet) {
            client.handshake.setCredential(m_credential);
            client.handshake.setTicketStore(&m_ticketStore); // enable 0-RTT
            client.credentialSet = true;
        }
        client.host = sender;
        client.port = senderPort;

        if (!client.established) {
            std::vector<SwByteArray> outgoing;
            if (!client.handshake.processIncomingDatagram(datagram, outgoing, error)) {
                return false;
            }
            if (!sendDatagrams_(outgoing, client, error)) {
                return false;
            }
            if (client.handshake.handshakeComplete()) {
                if (!establishConnection_(client, error)) {
                    return false;
                }
            }
            return true;
        }

        // Established: 1-RTT application datagrams drive the HTTP/3 session.
        if (!client.connection->receiveDatagram(datagram, nowMs_(), error)) {
            return false;
        }
        if (!client.http3->pump(error)) {
            return false;
        }
        return flushConnection_(client, error);
    }

    bool establishConnection_(Client_& client, SwString* error) {
        client.connection.reset(new SwQuicConnection(SwQuicConnection::Role::Server));
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
            const std::vector<SwQuicFrame>& early = client.handshake.earlyStreamFrames();
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
        if (!client.http3->pump(error)) {
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
            flushConnection_(client, &ignored);
        }
    }

    bool flushConnection_(Client_& client, SwString* error) {
        std::vector<SwByteArray> outgoing;
        if (!client.connection->buildDatagrams(nowMs_(), outgoing, error)) {
            return false;
        }
        return sendDatagrams_(outgoing, client, error);
    }

    bool sendDatagrams_(const std::vector<SwByteArray>& datagrams,
                        const Client_& client,
                        SwString* error) {
        for (std::size_t i = 0; i < datagrams.size(); ++i) {
            const int64_t sent = m_socket.writeDatagram(datagrams[i].constData(),
                                                        static_cast<int64_t>(datagrams[i].size()),
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
    SwQuicTicketStore m_ticketStore; // 0-RTT resumption tickets
    bool m_polling = false;
};

#endif
