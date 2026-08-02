// Full HTTP/3 client <-> server round trip, both this stack's own classes, over
// a real loopback UDP socket:
//   SwHttp3Client  ->  SwQuicHttp3Server (SwHttpRouter)
// The server runs on a background thread polling its UDP socket; the client is
// the event-driven SwHttp3Client running in the main thread's SwCoreApplication
// loop: GET starts, finished() checks it and chains the POST from the slot,
// errorOccurred() fails the test. This validates the complete HTTP/3 path end
// to end (handshake, 1-RTT, QPACK, routing, response parsing) through the
// signal/slot driver: socket readyRead + SwTimer, no polling in the client.

#include "SwCoreApplication.h"
#include "SwTimer.h"
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

} // namespace

int main() {
    SwCoreApplication app;
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("localhost"),
                                                             credential, &error),
                     "credential generation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return 1;
    }

    SwHttpRouter router;
    router.addRoute(SwString("GET"), SwString("/hello"),
                    [](const SwHttpRequest& request) -> SwHttpResponse {
                        (void)request;
                        SwHttpResponse response;
                        response.status = 200;
                        response.headers[SwString("content-type")] = SwString("text/plain");
                        response.body = SwByteArray("hello over http3");
                        return response;
                    });
    router.addRoute(SwString("POST"), SwString("/echo"),
                    [](const SwHttpRequest& request) -> SwHttpResponse {
                        SwHttpResponse response;
                        response.status = 201;
                        response.headers[SwString("content-type")] = SwString("application/octet-stream");
                        response.body = request.body; // echo the posted body
                        return response;
                    });

    SwQuicHttp3Server server;
    server.setCredential(credential);
    server.setRouter(&router);
    if (!requireTrue(server.listen(SwString("127.0.0.1"), 0, &error), "server listen failed")) {
        std::cerr << error.toStdString() << std::endl;
        return 1;
    }
    // Force stateless address validation (RFC 9000 8.1.2): every new
    // connection must survive a Retry round trip before the server allocates
    // handshake state. Both requests below therefore exercise the full
    // Retry -> token -> validated-handshake path end to end.
    server.setAddressValidation(true, 0);
    const uint16_t port = server.localPort();

    // Poll the server on a background thread while the client's event loop runs.
    std::atomic<bool> stop(false);
    std::thread serverThread([&server, &stop]() {
        SwString pollError;
        while (!stop.load()) {
            server.poll(5, &pollError);
        }
    });

    bool ok = true;
    int step = 0;
    const SwByteArray payload("payload-1234567890");

    SwHttp3Client client;
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false); // self-signed peer

    SwObject::connect(&client, &SwHttp3Client::finished, &client,
                      [&](const SwByteArray& body) {
        if (step == 0) {
            ok = requireTrue(client.statusCode() == 200, "GET status is not 200") && ok;
            ok = requireTrue(body == SwByteArray("hello over http3"), "GET body mismatch") && ok;
            if (!ok) {
                app.exit(1);
                return;
            }
            step = 1;
            // The client is reusable as soon as the previous request settled:
            // chain the POST directly from the finished slot.
            if (!requireTrue(client.post(SwString("127.0.0.1"), port, SwString("/echo"),
                                         payload, SwByteArray("application/octet-stream"),
                                         10000),
                             "client POST did not start")) {
                std::cerr << "post_error=" << client.errorString().toStdString() << std::endl;
                ok = false;
                app.exit(1);
            }
            return;
        }
        ok = requireTrue(client.statusCode() == 201, "POST status is not 201") && ok;
        ok = requireTrue(body == payload, "POST echo body mismatch") && ok;
        app.exit(ok ? 0 : 1);
    });
    SwObject::connect(&client, &SwHttp3Client::errorOccurred, &client,
                      [&](const SwString& message) {
        std::cerr << "FAIL: request " << step << " failed: "
                  << message.toStdString() << std::endl;
        ok = false;
        app.exit(1);
    });

    SwTimer watchdog(30000);
    watchdog.setSingleShot(true);
    SwObject::connect(&watchdog, &SwTimer::timeout, [&app]() { app.exit(2); });
    watchdog.start();

    if (!requireTrue(client.get(SwString("127.0.0.1"), port, SwString("/hello"), 10000),
                     "client GET did not start")) {
        std::cerr << "get_error=" << client.errorString().toStdString() << std::endl;
        stop.store(true);
        serverThread.join();
        return 1;
    }

    const int result = app.exec();
    stop.store(true);
    serverThread.join();

    if (result == 2) {
        std::cerr << "FAIL: test timed out" << std::endl;
    }
    if (result != 0 || !ok) {
        return 1;
    }
    // Each request opened its own connection, and address validation was
    // mandatory: the server must have issued one Retry per connection.
    if (!requireTrue(server.retryPacketsSent() >= 2,
                     "server did not issue a Retry per new connection")) {
        return 1;
    }
    std::cout << "Http3ClientServerSelfTest passed" << std::endl;
    return 0;
}
