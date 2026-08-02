// End-to-end HTTP/3-over-QUIC across a real loopback UDP socket:
//   SwUdpSocket (client) <-> SwQuicHttp3Server (server driver)
// The client runs the real QUIC v1 + TLS 1.3 handshake (SwQuicHandshakeClient)
// against the driver's SwQuicHandshakeServer, then opens a 1-RTT QUIC
// connection and issues an HTTP/3 GET; the driver routes it through an
// SwHttpRouter and answers. This proves the whole stack -- handshake, key
// hand-off to a SwQuicConnection, SwHttp3Server, and the router bridge -- works
// over a socket, not just in memory.

#include "core/io/SwUdpSocket.h"
#include "core/io/http/SwHttpRouter.h"
#include "core/io/http3/SwHttp3Connection.h"
#include "core/io/http3/SwQuicHttp3Server.h"
#include "core/io/quic/SwQuicConnection.h"
#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicServerCredential.h"
#include "core/types/SwVector.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool testHttp3ServerOverUdp() {
    SwString error;

    // Server: self-signed ECDSA credential + a router with one GET route.
    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("localhost"),
                                                             credential, &error),
                     "credential generation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwHttpRouter router;
    router.addRoute(SwString("GET"), SwString("/api/state"),
                    [](const SwHttpRequest& request) -> SwHttpResponse {
                        (void)request;
                        SwHttpResponse response;
                        response.status = 200;
                        response.headers[SwString("content-type")] = SwString("application/json");
                        response.body = SwByteArray("{\"ok\":true}");
                        return response;
                    });

    SwQuicHttp3Server server;
    server.setCredential(credential);
    server.setRouter(&router);
    if (!requireTrue(server.listen(SwString("127.0.0.1"), 0, &error),
                     "server listen failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    const uint16_t serverPort = server.localPort();

    // Client socket + handshake driver (verify the real signature, but accept
    // the self-signed cert).
    SwUdpSocket clientSocket;
    clientSocket.setMaxPendingDatagrams(64);
    if (!requireTrue(clientSocket.bind(SwString("127.0.0.1"), 0), "client bind failed")) {
        return false;
    }

    SwQuicHandshakeClient handshake;
    handshake.setVerifyPeer(true);
    handshake.setVerifyCertificateChain(false);

    // The hybrid ClientHello flight spans several Initial datagrams; each one
    // must travel as its own UDP datagram.
    SwVector<SwByteArray> clientInitialFlight;
    if (!requireTrue(handshake.start(SwString("localhost"), clientInitialFlight, &error),
                     "client handshake start failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    for (std::size_t i = 0; i < clientInitialFlight.size(); ++i) {
        clientSocket.writeDatagram(clientInitialFlight[i].constData(),
                                   static_cast<int64_t>(clientInitialFlight[i].size()),
                                   SwString("127.0.0.1"), serverPort);
    }

    // Drive the handshake to completion over UDP.
    const std::uint64_t deadline = nowMs() + 10000;
    while (!handshake.handshakeComplete() && nowMs() < deadline) {
        server.poll(5, &error);
        clientSocket.pollPendingDatagrams(5);
        while (clientSocket.hasPendingDatagrams()) {
            SwString from;
            uint16_t fromPort = 0;
            const SwByteArray datagram = clientSocket.receiveDatagram(&from, &fromPort);
            if (datagram.isEmpty()) {
                continue;
            }
            SwVector<SwByteArray> replies;
            if (!requireTrue(handshake.processIncomingDatagram(datagram, replies, &error),
                             "client handshake processing failed")) {
                std::cerr << "client_error=" << handshake.errorString().toStdString() << std::endl;
                return false;
            }
            for (std::size_t i = 0; i < replies.size(); ++i) {
                clientSocket.writeDatagram(replies[i].constData(),
                                           static_cast<int64_t>(replies[i].size()),
                                           SwString("127.0.0.1"), serverPort);
            }
        }
    }

    if (!requireTrue(handshake.handshakeComplete(), "client handshake did not complete") ||
        !requireTrue(handshake.negotiatedAlpn() == SwByteArray("h3"), "ALPN is not h3")) {
        return false;
    }

    // Build the client's 1-RTT connection over the derived application keys.
    SwQuicConnection client(SwQuicConnection::Role::Client);
    client.setLocalConnectionId(handshake.sourceConnectionId());
    client.setPeerConnectionId(handshake.destinationConnectionId());
    client.setLevelKeys(SwQuicConnection::Level::Application,
                        handshake.serverApplicationKeys(),   // rx: decrypt server
                        handshake.clientApplicationKeys());  // tx: encrypt client
    client.setHandshakeConfirmed(true);

    // Send an HTTP/3 GET on client-initiated bidi stream 0.
    SwHttp3Connection::Request request;
    request.method = SwByteArray("GET");
    request.scheme = SwByteArray("https");
    request.authority = SwByteArray("localhost");
    request.path = SwByteArray("/api/state");
    SwByteArray requestStream;
    if (!requireTrue(SwHttp3Connection::buildRequest(request, requestStream, &error),
                     "client request build failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(client.sendStreamData(0, requestStream, true, &error),
                     "client request queue failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwVector<SwByteArray> clientOut;
    if (!requireTrue(client.buildDatagrams(nowMs(), clientOut, &error),
                     "client request build datagrams failed")) {
        return false;
    }
    for (std::size_t i = 0; i < clientOut.size(); ++i) {
        clientSocket.writeDatagram(clientOut[i].constData(),
                                   static_cast<int64_t>(clientOut[i].size()),
                                   SwString("127.0.0.1"), serverPort);
    }

    // Pump until the full response stream (0) is received. readStream()
    // consumes bytes, so accumulate across iterations and parse only once the
    // stream's FIN has been seen.
    SwHttp3Connection::Response response;
    SwByteArray responseStream;
    bool haveResponse = false;
    const std::uint64_t requestDeadline = nowMs() + 10000;
    while (!haveResponse && nowMs() < requestDeadline) {
        server.poll(5, &error);
        clientSocket.pollPendingDatagrams(5);
        while (clientSocket.hasPendingDatagrams()) {
            SwString from;
            uint16_t fromPort = 0;
            const SwByteArray datagram = clientSocket.receiveDatagram(&from, &fromPort);
            if (datagram.isEmpty()) {
                continue;
            }
            if (!requireTrue(client.receiveDatagram(datagram, nowMs(), &error),
                             "client receive datagram failed")) {
                std::cerr << error.toStdString() << std::endl;
                return false;
            }
        }

        responseStream.append(client.readStream(0));
        const SwQuicStream* stream0 = client.streams().stream(0);
        if (!responseStream.isEmpty() && stream0 && stream0->isReceiveComplete()) {
            if (!requireTrue(SwHttp3Connection::parseResponse(responseStream, response, &error),
                             "client response parse failed")) {
                std::cerr << error.toStdString() << std::endl;
                return false;
            }
            haveResponse = true;
        }

        // Drive timers so a PTO retransmits the request if it was dropped by
        // the server before it finished establishing the connection.
        const std::int64_t t = client.nextTimeoutMs(nowMs());
        if (t == 0) {
            client.onTimeout(nowMs());
        }
        SwVector<SwByteArray> pending;
        if (client.buildDatagrams(nowMs(), pending, &error)) {
            for (std::size_t i = 0; i < pending.size(); ++i) {
                clientSocket.writeDatagram(pending[i].constData(),
                                           static_cast<int64_t>(pending[i].size()),
                                           SwString("127.0.0.1"), serverPort);
            }
        }
    }

    return requireTrue(haveResponse, "no HTTP/3 response received before timeout") &&
           requireTrue(response.status == SwByteArray("200"), "response status is not 200") &&
           requireTrue(response.body == SwByteArray("{\"ok\":true}"), "response body mismatch");
}

} // namespace

int main() {
    if (!testHttp3ServerOverUdp()) {
        return 1;
    }
    std::cout << "Http3ServerOverUdpSelfTest passed" << std::endl;
    return 0;
}
