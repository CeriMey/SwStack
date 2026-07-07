// Full HTTP/3 client <-> server round trip, both this stack's own classes, over
// a real loopback UDP socket:
//   SwHttp3Client  ->  SwQuicHttp3Server (SwHttpRouter)
// The server runs on a background thread polling its UDP socket; the client
// issues a blocking GET and a POST. This validates the complete HTTP/3 path end
// to end: handshake, 1-RTT, QPACK, request routing, and response parsing.

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

bool testClientServerRoundTrip() {
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("localhost"),
                                                             credential, &error),
                     "credential generation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
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
        return false;
    }
    const uint16_t port = server.localPort();

    // Poll the server on a background thread while the client blocks.
    std::atomic<bool> stop(false);
    std::thread serverThread([&server, &stop]() {
        SwString pollError;
        while (!stop.load()) {
            server.poll(5, &pollError);
        }
    });

    bool ok = true;

    // GET /hello
    {
        SwHttp3Client client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false); // self-signed peer
        SwString requestError;
        const bool got = client.get(SwString("127.0.0.1"), port, SwString("/hello"),
                                    10000, &requestError);
        if (!requireTrue(got, "client GET failed") ||
            !requireTrue(client.statusCode() == 200, "GET status is not 200") ||
            !requireTrue(client.responseBody() == SwByteArray("hello over http3"),
                         "GET body mismatch")) {
            std::cerr << "get_error=" << requestError.toStdString() << std::endl;
            ok = false;
        }
    }

    // POST /echo
    if (ok) {
        SwHttp3Client client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false);
        SwString requestError;
        const SwByteArray payload("payload-1234567890");
        const bool posted = client.post(SwString("127.0.0.1"), port, SwString("/echo"),
                                        payload, SwByteArray("application/octet-stream"),
                                        10000, &requestError);
        if (!requireTrue(posted, "client POST failed") ||
            !requireTrue(client.statusCode() == 201, "POST status is not 201") ||
            !requireTrue(client.responseBody() == payload, "POST echo body mismatch")) {
            std::cerr << "post_error=" << requestError.toStdString() << std::endl;
            ok = false;
        }
    }

    stop.store(true);
    serverThread.join();
    return ok;
}

} // namespace

int main() {
    if (!testClientServerRoundTrip()) {
        return 1;
    }
    std::cout << "Http3ClientServerSelfTest passed" << std::endl;
    return 0;
}
