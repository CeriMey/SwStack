// End-to-end 0-RTT over a real loopback UDP socket, through this stack's own
// HTTP/3 client and server:
//   request 1  -> full handshake; SwQuicHttp3Server issues a NewSessionTicket
//                 on the 1-RTT CRYPTO stream, SwHttp3Client captures it.
//   request 2  -> SwHttp3Client resumes with that ticket: the HTTP/3 request
//                 is sent as 0-RTT early data, the server accepts it, routes it
//                 during the handshake, and answers.
// The event-driven client is used through waitForFinished(), its synchronous
// bridge for threads that run no event loop (this main thread).

#include "core/io/http/SwHttpRouter.h"
#include "core/io/http3/SwHttp3Client.h"
#include "core/io/http3/SwQuicHttp3Server.h"
#include "core/io/quic/SwQuicServerCredential.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool testZeroRttOverUdp() {
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("localhost"),
                                                             credential, &error),
                     "credential generation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwHttpRouter router;
    router.addRoute(SwString("GET"), SwString("/state"),
                    [](const SwHttpRequest& request) -> SwHttpResponse {
                        (void)request;
                        SwHttpResponse response;
                        response.status = 200;
                        response.body = SwByteArray("state-ok");
                        return response;
                    });
    router.addRoute(SwString("POST"), SwString("/echo"),
                    [](const SwHttpRequest& request) -> SwHttpResponse {
                        SwHttpResponse response;
                        response.status = 201;
                        response.body = request.body;
                        return response;
                    });

    SwQuicHttp3Server server;
    server.setCredential(credential);
    server.setRouter(&router);
    if (!requireTrue(server.listen(SwString("127.0.0.1"), 0, &error), "server listen failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    const uint16_t port = server.localPort();

    std::atomic<bool> stop(false);
    std::thread serverThread([&server, &stop]() {
        SwString pollError;
        while (!stop.load()) {
            server.poll(5, &pollError);
        }
    });

    bool ok = true;
    SwQuicSessionTicket ticket;

    // Request 1: full handshake, captures a session ticket.
    {
        SwHttp3Client client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false);
        const bool got = client.get(SwString("127.0.0.1"), port, SwString("/state"),
                                    10000) &&
                         client.waitForFinished();
        if (!requireTrue(got, "request 1 (1-RTT) failed") ||
            !requireTrue(client.statusCode() == 200, "request 1 status is not 200") ||
            !requireTrue(client.responseBody() == SwByteArray("state-ok"), "request 1 body mismatch") ||
            !requireTrue(client.hasSessionTicket(), "client did not capture a session ticket")) {
            std::cerr << "req1_error=" << client.errorString().toStdString() << std::endl;
            ok = false;
        } else {
            ticket = client.sessionTicket();
        }
    }

    // Request 2: resume with the ticket -> the request rides as 0-RTT early data.
    if (ok) {
        SwHttp3Client client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false);
        client.setResumptionTicket(ticket);
        const bool got = client.get(SwString("127.0.0.1"), port, SwString("/state"),
                                    10000) &&
                         client.waitForFinished();
        if (!requireTrue(got, "request 2 (0-RTT) failed") ||
            !requireTrue(client.earlyDataAccepted(), "server did not accept 0-RTT early data") ||
            !requireTrue(client.statusCode() == 200, "request 2 status is not 200") ||
            !requireTrue(client.responseBody() == SwByteArray("state-ok"), "request 2 body mismatch")) {
            std::cerr << "req2_error=" << client.errorString().toStdString() << std::endl;
            ok = false;
        }
    }

    // Request 3: resume with the now-consumed single-use ticket. The server
    // declines the PSK (anti-replay), so 0-RTT is NOT accepted and the client
    // falls back to a full 1-RTT request, which still succeeds. The 20 KB body
    // also exercises multi-packet, congestion-controlled 1-RTT.
    if (ok) {
        SwHttp3Client client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false);
        client.setResumptionTicket(ticket);
        const SwByteArray bigBody(20000, 'x');
        const bool got = client.post(SwString("127.0.0.1"), port, SwString("/echo"),
                                     bigBody, SwByteArray("application/octet-stream"),
                                     10000) &&
                         client.waitForFinished();
        if (!requireTrue(got, "request 3 (oversized) failed") ||
            !requireTrue(!client.earlyDataAccepted(),
                         "oversized request must not be sent as 0-RTT") ||
            !requireTrue(client.statusCode() == 201, "request 3 status is not 201") ||
            !requireTrue(client.responseBody() == bigBody, "request 3 echo body mismatch")) {
            std::cerr << "req3_error=" << client.errorString().toStdString() << std::endl;
            ok = false;
        }
    }

    stop.store(true);
    serverThread.join();
    return ok;
}

} // namespace

int main() {
    if (!testZeroRttOverUdp()) {
        return 1;
    }
    std::cout << "Http3ZeroRttClientServerSelfTest passed" << std::endl;
    return 0;
}
