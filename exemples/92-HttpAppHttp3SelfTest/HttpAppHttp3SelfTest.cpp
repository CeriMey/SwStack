#include "SwCoreApplication.h"
#include "SwHttpApp.h"
#include "SwHttpClient.h"
#include "SwTimer.h"
#include "SwUdpSocket.h"
#include "core/io/http3/SwHttp3Client.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Http3Result {
    bool requestSucceeded = false;
    int status = 0;
    SwByteArray body;
    SwString protocol;
    SwString middleware;
    SwString query;
    SwString localPort;
    SwString error;
    bool multipartSucceeded = false;
    int multipartStatus = 0;
    SwByteArray multipartBody;
    SwString multipartError;
    bool fileSucceeded = false;
    int fileStatus = 0;
    SwByteArray fileBody;
    SwString fileError;
    bool chunkedSucceeded = false;
    int chunkedStatus = 0;
    SwByteArray chunkedBody;
    SwString chunkedLength;
    bool chunkedHeadersSanitized = false;
    SwString chunkedError;
    bool headFileSucceeded = false;
    int headFileStatus = 0;
    SwByteArray headFileBody;
    SwString headFileLength;
    SwString headFileError;
    bool largeFileSucceeded = false;
    int largeFileStatus = 0;
    SwByteArray largeFileBody;
    SwString largeFileError;
    bool asyncGuardSucceeded = false;
    int asyncGuardStatus = 0;
    SwByteArray asyncGuardBody;
    SwString asyncGuardHeader;
    SwString asyncGuardError;
    bool asyncRouteSucceeded = false;
    int asyncRouteStatus = 0;
    SwByteArray asyncRouteBody;
    SwString asyncRouteError;
    bool asyncTimeoutSucceeded = false;
    int asyncTimeoutStatus = 0;
    SwString asyncTimeoutAltSvc;
    SwString asyncTimeoutError;
    bool methodGuardSucceeded = false;
    int methodGuardStatus = 0;
    SwByteArray methodGuardBody;
    SwString methodGuardError;
    bool strictRequestValidationSucceeded = false;
    SwString strictRequestValidationError;
};

typedef std::vector<std::pair<SwByteArray, SwByteArray> > RawHeaderList;

bool requestIsRejectedAsMalformed(std::uint16_t port,
                                  const RawHeaderList& headers,
                                  const SwByteArray& body,
                                  const char* caseName,
                                  SwString& error,
                                  const SwByteArray& method = SwByteArray()) {
    SwHttp3Client client;
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false);
    for (std::size_t i = 0; i < headers.size(); ++i) {
        client.setRawHeader(headers[i].first, headers[i].second);
    }
    const SwByteArray effectiveMethod = method.isEmpty()
        ? (body.isEmpty() ? SwByteArray("GET") : SwByteArray("POST"))
        : method;
    if (!client.request(effectiveMethod,
                        "127.0.0.1", port, "/shared/rejected", body,
                        SwByteArray(), 15000) ||
        !client.waitForFinished()) {
        error = SwString(caseName) + SwString(": request failed: ") + client.errorString();
        return false;
    }
    if (client.statusCode() != 400) {
        error = SwString(caseName) + SwString(": expected 400, got ") +
                SwString::number(client.statusCode());
        return false;
    }
    return true;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[HttpAppHttp3SelfTest] FAIL " << message << std::endl;
    }
    return condition;
}

char largeFileByte(std::size_t offset) {
    return static_cast<char>((offset * 31U + 17U) & 0xffU);
}

bool createLargeFile(const SwString& path, std::size_t size) {
    std::ofstream output(path.toStdString().c_str(),
                         std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return false;
    std::vector<char> block(64U * 1024U);
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t count = (std::min)(block.size(), size - offset);
        for (std::size_t i = 0; i < count; ++i) {
            block[i] = largeFileByte(offset + i);
        }
        output.write(block.data(), static_cast<std::streamsize>(count));
        if (!output.good()) return false;
        offset += count;
    }
    output.close();
    return output.good();
}

struct TestFileGuard {
    explicit TestFileGuard(const SwString& value) : path(value) {}
    ~TestFileGuard() { std::remove(path.toStdString().c_str()); }
    SwString path;
};

} // namespace

int main(int argc, char** argv) {
    SwCoreApplication core(argc, argv);
    SwHttpApp app;
    const SwString certificatePath(SW_HTTP3_TEST_CERT_PATH);
    const SwString privateKeyPath(SW_HTTP3_TEST_KEY_PATH);
    const SwString largeFilePath("sw_http3_large_streaming_selftest.bin");
    static const std::size_t kLargeFileBytes =
        (17U * 1024U * 1024U) + 321U;
    TestFileGuard largeFileGuard(largeFilePath);
    if (!expect(createLargeFile(largeFilePath, kLargeFileBytes),
                "could not create the large HTTP/3 streaming fixture")) {
        return 1;
    }

    std::atomic<int> middlewareCalls(0);
    std::atomic<int> handlerCalls(0);
    std::atomic<int> asyncPreRouteCalls(0);
    std::atomic<int> asyncGuardBlocks(0);
    std::atomic<int> asyncRouteCalls(0);
    std::atomic<int> asyncTimeoutCalls(0);
    std::atomic<int> methodGuardBlocks(0);
    std::atomic<int> methodGuardRouteCalls(0);
    SwHttpTimeouts timeouts;
    timeouts.routeTimeoutMs = 100;
    app.setTimeouts(timeouts);
    app.addPreRouteHandler(
        [&methodGuardBlocks](const SwHttpRequest& request,
                             SwHttpResponse& response) {
            if (request.path != SwString("/method-guard") ||
                request.method != SwString("POST")) {
                return false;
            }
            ++methodGuardBlocks;
            response = swHttpTextResponse(
                403, SwString("blocked normalized POST method"));
            return true;
        });
    app.addPreRouteHandlerAsync(
        [&asyncPreRouteCalls, &asyncGuardBlocks](
            const SwHttpRequest& request,
            const SwHttpPreRouteAsyncResponder& respond) {
            ++asyncPreRouteCalls;
            if (request.path != SwString("/async-guard")) {
                respond(false, SwHttpResponse());
                return;
            }
            ++asyncGuardBlocks;
            std::thread([respond]() {
                SwHttpResponse response =
                    swHttpTextResponse(403, SwString("blocked by async guard"));
                response.headers["x-async-guard"] = "applied";
                respond(true, response);
            }).detach();
        });
    app.use([&middlewareCalls](SwHttpContext& context, const SwHttpApp::SwHttpNext& next) {
        ++middlewareCalls;
        context.setLocal("shared-middleware", "yes");
        next();
    });
    app.get("/shared/:value", [&handlerCalls](SwHttpContext& context) {
        ++handlerCalls;
        context.text(SwString("shared:") + context.pathValue("value"));
        context.setHeader("x-request-protocol", context.protocol());
        context.setHeader("x-shared-middleware", context.localValue("shared-middleware"));
        context.setHeader("x-query-value", context.queryValue("source"));
        context.setHeader("x-local-port",
                          SwString::number(static_cast<int>(context.localPort())));
    });
    app.post("/multipart", [&handlerCalls](SwHttpContext& context) {
        ++handlerCalls;
        context.text(context.formValue("message"));
    });
    app.get("/certificate", [&handlerCalls, certificatePath](SwHttpContext& context) {
        ++handlerCalls;
        if (!context.sendFile(certificatePath, "application/x-pem-file")) {
            context.text("file unavailable", 500);
        }
    });
    app.get("/chunks", [&handlerCalls](SwHttpContext& context) {
        ++handlerCalls;
        SwHttpResponse& response = context.response();
        response.status = 200;
        response.reason = "OK";
        response.headers["content-type"] = "text/plain";
        response.headers["content-length"] = "999";
        response.headers["bad field"] = "must-not-be-sent";
        response.headers["x-injected"] = "safe\r\ninjected: no";
        response.useChunkedTransfer = true;
        response.body = SwByteArray("body-before-");
        response.chunkedParts.append(SwByteArray("chunk-"));
        response.chunkedParts.append(SwByteArray("over-http3"));
        context.setHandled();
    });
    app.get("/head-file-no-read", [&handlerCalls](SwHttpContext& context) {
        ++handlerCalls;
        SwHttpResponse& response = context.response();
        response.status = 200;
        response.reason = "OK";
        response.headers["content-type"] = "application/octet-stream";
        response.headers["content-length"] = "123";
        response.hasFile = true;
        response.filePath = "__sw_http3_missing__/head-must-not-open.bin";
        response.fileLength = 123;
        context.setHandled();
    });
    app.get("/large-file", [&handlerCalls, largeFilePath](SwHttpContext& context) {
        ++handlerCalls;
        if (!context.sendFile(largeFilePath, "application/octet-stream")) {
            context.text("large file unavailable", 500);
        }
    });
    app.get("/async-guard", [&handlerCalls](SwHttpContext& context) {
        ++handlerCalls; // A call here means the asynchronous guard was bypassed.
        context.text("guard bypassed", 200);
    });
    app.post("/method-guard", [&methodGuardRouteCalls](SwHttpContext& context) {
        ++methodGuardRouteCalls; // A call means lowercase :method bypassed the guard.
        context.text("method guard bypassed", 200);
    });
    app.server().addRouteAsync(
        "GET", "/async-route",
        [&asyncRouteCalls](const SwHttpRequest& request,
                           const SwHttpRouteResponder& respond) {
            ++asyncRouteCalls;
            const SwString protocol = request.protocol;
            std::thread([respond, protocol]() {
                respond(swHttpTextResponse(
                    202, SwString("async route over ") + protocol));
            }).detach();
        });
    app.server().addRouteAsync(
        "GET", "/async-timeout",
        [&asyncTimeoutCalls](const SwHttpRequest&,
                             const SwHttpRouteResponder& respond) {
            ++asyncTimeoutCalls;
            (void)respond; // Deliberately unresolved: the shared route timer must answer 504.
        });
    app.get("/protocol", [](SwHttpContext& context) {
        context.text(context.protocol());
    });

    // Manual interoperability mode used to probe the server with an external
    // browser: HttpAppHttp3SelfTest --serve <port>.
    if (argc == 3 && std::string(argv[1]) == "--serve") {
        SwObject::connect(&app.http3Server(), &SwQuicHttp3Server::clientRejected,
                          [](const SwString& host, std::uint16_t peerPort,
                             const SwString& reason) {
                              std::cerr << "HTTP3_CLIENT_REJECTED "
                                        << host.toStdString() << ':' << peerPort << ' '
                                        << reason.toStdString() << std::endl;
                          });
        SwObject::connect(&app.http3Server(), &SwQuicHttp3Server::serverError,
                          [](const SwString& error) {
                              std::cerr << "HTTP3_SERVER_ERROR "
                                        << error.toStdString() << std::endl;
                          });
        SwObject::connect(&app.http3Server(), &SwQuicHttp3Server::requestServed,
                          [](const SwHttpRequest& request, const SwHttpResponse& response) {
                              std::cerr << "HTTP3_REQUEST_SERVED "
                                        << request.method.toStdString() << ' '
                                        << request.path.toStdString() << ' '
                                        << response.status << std::endl;
                          });
        const int requestedPort = std::atoi(argv[2]);
        if (requestedPort <= 0 || requestedPort > 65535 ||
            !app.listenHttps("127.0.0.1", static_cast<std::uint16_t>(requestedPort),
                             certificatePath, privateKeyPath)) {
            std::cerr << app.lastHttp3Error().toStdString() << std::endl;
            return 1;
        }
        std::cout << "HTTP3_BROWSER_TEST_READY " << app.httpsPort() << std::endl;
        SwTimer shutdown(60000);
        shutdown.setSingleShot(true);
        SwObject::connect(&shutdown, &SwTimer::timeout, [&core]() { core.exit(0); });
        shutdown.start();
        return core.exec();
    }

    // listenHttps() is additive with a plaintext listener. If QUIC cannot bind
    // after TCP/TLS succeeded, rollback must close only the failed HTTPS half;
    // the pre-existing HTTP listener and dispatch state stay intact.
    if (!expect(app.listenHttp("127.0.0.1", 0),
                "plaintext listener setup for rollback test failed")) {
        return 1;
    }
    const std::uint16_t originalHttpPort = app.httpPort();
    SwUdpSocket occupiedUdpPort;
    if (!expect(occupiedUdpPort.bind(
                    "127.0.0.1", 0, SwUdpSocket::DontShareAddress),
                "could not reserve a UDP port for rollback test")) {
        app.close();
        return 1;
    }
    const std::uint16_t blockedHttp3Port = occupiedUdpPort.localPort();
    if (!expect(!app.listenHttps("127.0.0.1", blockedHttp3Port,
                                 certificatePath, privateKeyPath),
                "HTTPS unexpectedly ignored an exclusive UDP collision") ||
        !expect(app.isHttpListening() && app.httpPort() == originalHttpPort,
                "failed HTTP/3 bind closed the existing HTTP listener") ||
        !expect(!app.isHttpsListening() && !app.isHttp3Listening(),
                "failed dual-transport bind left a partial secure listener")) {
        app.close();
        return 1;
    }
    occupiedUdpPort.close();

    if (!expect(app.listenHttps("127.0.0.1", 0, certificatePath, privateKeyPath),
                "transparent HTTPS + HTTP/3 listen failed")) {
        std::cerr << app.lastHttp3Error().toStdString() << std::endl;
        return 1;
    }

    const std::uint16_t port = app.httpsPort();
    bool ok = expect(port != 0, "HTTPS selected an invalid port") &&
              expect(app.isHttpsListening(), "HTTPS listener is not active") &&
              expect(app.isHttp3Listening(), "HTTP/3 listener is not active") &&
              expect(app.http3Port() == port,
                     "HTTPS/TCP and HTTP/3/UDP do not share the same port");
    if (!ok) {
        app.close();
        return 1;
    }

    Http3Result http3;
    std::atomic<bool> http3Done(false);
    std::thread http3Thread([&http3, &http3Done, port]() {
        SwHttp3Client client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false); // self-signed test CA
        http3.requestSucceeded = client.get(
            "127.0.0.1", port, "/shared/demo%20value?source=http3", 15000) &&
            client.waitForFinished();
        http3.status = client.statusCode();
        http3.body = client.responseBody();
        http3.protocol = client.responseHeaders().value("x-request-protocol");
        http3.middleware = client.responseHeaders().value("x-shared-middleware");
        http3.query = client.responseHeaders().value("x-query-value");
        http3.localPort = client.responseHeaders().value("x-local-port");
        http3.error = client.errorString();

        const SwByteArray multipartBody(
            "--sw-http3-boundary\r\n"
            "Content-Disposition: form-data; name=\"message\"\r\n\r\n"
            "multipart-over-http3\r\n"
            "--sw-http3-boundary--\r\n");
        SwHttp3Client multipartClient;
        multipartClient.setVerifyPeer(true);
        multipartClient.setVerifyCertificateChain(false);
        http3.multipartSucceeded = multipartClient.post(
            "127.0.0.1", port, "/multipart", multipartBody,
            SwByteArray("multipart/form-data; boundary=sw-http3-boundary"),
            15000) && multipartClient.waitForFinished();
        http3.multipartError = multipartClient.errorString();
        http3.multipartStatus = multipartClient.statusCode();
        http3.multipartBody = multipartClient.responseBody();

        SwHttp3Client fileClient;
        fileClient.setVerifyPeer(true);
        fileClient.setVerifyCertificateChain(false);
        http3.fileSucceeded = fileClient.get(
            "127.0.0.1", port, "/certificate", 15000) &&
            fileClient.waitForFinished();
        http3.fileError = fileClient.errorString();
        http3.fileStatus = fileClient.statusCode();
        http3.fileBody = fileClient.responseBody();

        SwHttp3Client chunkedClient;
        chunkedClient.setVerifyPeer(true);
        chunkedClient.setVerifyCertificateChain(false);
        http3.chunkedSucceeded = chunkedClient.get(
            "127.0.0.1", port, "/chunks", 15000) &&
            chunkedClient.waitForFinished();
        http3.chunkedError = chunkedClient.errorString();
        http3.chunkedStatus = chunkedClient.statusCode();
        http3.chunkedBody = chunkedClient.responseBody();
        http3.chunkedLength =
            chunkedClient.responseHeaders().value("content-length");
        http3.chunkedHeadersSanitized =
            !chunkedClient.responseHeaders().contains("bad field") &&
            !chunkedClient.responseHeaders().contains("x-injected");

        SwHttp3Client headFileClient;
        headFileClient.setVerifyPeer(true);
        headFileClient.setVerifyCertificateChain(false);
        http3.headFileSucceeded = headFileClient.request(
            SwByteArray("HEAD"), "127.0.0.1", port, "/head-file-no-read",
            SwByteArray(), SwByteArray(), 15000) &&
            headFileClient.waitForFinished();
        http3.headFileError = headFileClient.errorString();
        http3.headFileStatus = headFileClient.statusCode();
        http3.headFileBody = headFileClient.responseBody();
        http3.headFileLength = headFileClient.responseHeaders().value("content-length");

        SwHttp3Client largeFileClient;
        largeFileClient.setVerifyPeer(true);
        largeFileClient.setVerifyCertificateChain(false);
        http3.largeFileSucceeded = largeFileClient.get(
            "127.0.0.1", port, "/large-file", 120000) &&
            largeFileClient.waitForFinished();
        http3.largeFileError = largeFileClient.errorString();
        http3.largeFileStatus = largeFileClient.statusCode();
        http3.largeFileBody = largeFileClient.responseBody();

        SwHttp3Client asyncGuardClient;
        asyncGuardClient.setVerifyPeer(true);
        asyncGuardClient.setVerifyCertificateChain(false);
        http3.asyncGuardSucceeded = asyncGuardClient.get(
            "127.0.0.1", port, "/async-guard", 15000) &&
            asyncGuardClient.waitForFinished();
        http3.asyncGuardError = asyncGuardClient.errorString();
        http3.asyncGuardStatus = asyncGuardClient.statusCode();
        http3.asyncGuardBody = asyncGuardClient.responseBody();
        http3.asyncGuardHeader =
            asyncGuardClient.responseHeaders().value("x-async-guard");

        SwHttp3Client asyncRouteClient;
        asyncRouteClient.setVerifyPeer(true);
        asyncRouteClient.setVerifyCertificateChain(false);
        http3.asyncRouteSucceeded = asyncRouteClient.get(
            "127.0.0.1", port, "/async-route", 15000) &&
            asyncRouteClient.waitForFinished();
        http3.asyncRouteError = asyncRouteClient.errorString();
        http3.asyncRouteStatus = asyncRouteClient.statusCode();
        http3.asyncRouteBody = asyncRouteClient.responseBody();

        SwHttp3Client asyncTimeoutClient;
        asyncTimeoutClient.setVerifyPeer(true);
        asyncTimeoutClient.setVerifyCertificateChain(false);
        http3.asyncTimeoutSucceeded = asyncTimeoutClient.get(
            "127.0.0.1", port, "/async-timeout", 15000) &&
            asyncTimeoutClient.waitForFinished();
        http3.asyncTimeoutError = asyncTimeoutClient.errorString();
        http3.asyncTimeoutStatus = asyncTimeoutClient.statusCode();
        http3.asyncTimeoutAltSvc =
            asyncTimeoutClient.responseHeaders().value("alt-svc");

        // HTTP/1.x normalizes methods before pre-route authorization. Send a
        // lowercase HTTP/3 pseudo-header and prove the common POST guard sees
        // the same normalized value before the router performs its own match.
        SwHttp3Client methodGuardClient;
        methodGuardClient.setVerifyPeer(true);
        methodGuardClient.setVerifyCertificateChain(false);
        http3.methodGuardSucceeded = methodGuardClient.request(
            SwByteArray("post"), "127.0.0.1", port, "/method-guard",
            SwByteArray(), SwByteArray(), 15000) &&
            methodGuardClient.waitForFinished();
        http3.methodGuardError = methodGuardClient.errorString();
        http3.methodGuardStatus = methodGuardClient.statusCode();
        http3.methodGuardBody = methodGuardClient.responseBody();

        // RFC 9114 request-field validation is exercised through the actual
        // QUIC/H3 path. None of these malformed requests may reach SwHttpApp.
        bool strictValidation = true;
        RawHeaderList malformedHeaders;
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("X-Uppercase"), SwByteArray("value")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "uppercase field name",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("bad field"), SwByteArray("value")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "non-token field name",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("x-before-pseudo"), SwByteArray("value")));
        malformedHeaders.push_back(
            std::make_pair(SwByteArray(":path"), SwByteArray("/other")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "pseudo-header ordering",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray(":method"), SwByteArray("GET")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "duplicate pseudo-header",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("host"), SwByteArray("other.example")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "authority/host conflict",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("content-length"), SwByteArray("0")));
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("content-length"), SwByteArray("0")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "duplicate content-length",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("content-length"), SwByteArray("1x")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "non-numeric content-length",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(
            std::make_pair(SwByteArray("content-length"), SwByteArray("2")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray("x"), "content-length mismatch",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        malformedHeaders.push_back(std::make_pair(
            SwByteArray("x-injected"), SwByteArray("safe\r\ninjected: yes")));
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "control in field value",
            http3.strictRequestValidationError);

        malformedHeaders.clear();
        strictValidation = strictValidation && requestIsRejectedAsMalformed(
            port, malformedHeaders, SwByteArray(), "non-token method",
            http3.strictRequestValidationError, SwByteArray("PO ST"));
        http3.strictRequestValidationSucceeded = strictValidation;
        http3Done.store(true, std::memory_order_release);
    });

    SwHttpClient http1Client;
    http1Client.setTrustedCaFile(certificatePath);
    bool http1Done = false;
    bool http1Succeeded = false;
    SwObject::connect(&http1Client, &SwHttpClient::finished,
                      [&http1Done, &http1Succeeded](const SwByteArray&) {
                          http1Succeeded = true;
                          http1Done = true;
                      });
    SwObject::connect(&http1Client, &SwHttpClient::errorOccurred,
                      [&http1Done](int) { http1Done = true; });

    SwHttpClient http1ChunkedClient;
    http1ChunkedClient.setTrustedCaFile(certificatePath);
    bool http1ChunkedDone = false;
    bool http1ChunkedSucceeded = false;
    SwByteArray http1ChunkedBody;
    SwObject::connect(
        &http1ChunkedClient, &SwHttpClient::finished,
        [&http1ChunkedDone, &http1ChunkedSucceeded,
         &http1ChunkedBody](const SwByteArray& body) {
            http1ChunkedSucceeded = true;
            http1ChunkedBody = body;
            http1ChunkedDone = true;
        });
    SwObject::connect(&http1ChunkedClient, &SwHttpClient::errorOccurred,
                      [&http1ChunkedDone](int) { http1ChunkedDone = true; });

    const SwString httpsUrl =
        SwString("https://127.0.0.1:") + SwString::number(static_cast<int>(port)) +
        SwString("/shared/demo%20value?source=http1");
    if (!expect(http1Client.get(httpsUrl), "HTTPS request did not start")) {
        app.close();
        http3Thread.join();
        return 1;
    }
    const SwString httpsChunkedUrl =
        SwString("https://127.0.0.1:") + SwString::number(static_cast<int>(port)) +
        SwString("/chunks");
    if (!expect(http1ChunkedClient.get(httpsChunkedUrl),
                "HTTPS chunked request did not start")) {
        app.close();
        http3Thread.join();
        return 1;
    }

    SwTimer completionCheck(5);
    SwObject::connect(&completionCheck, &SwTimer::timeout, [&]() {
        if (http1Done && http1ChunkedDone &&
            http3Done.load(std::memory_order_acquire)) {
            core.exit(0);
        }
    });
    completionCheck.start();

    SwTimer watchdog(150000);
    watchdog.setSingleShot(true);
    SwObject::connect(&watchdog, &SwTimer::timeout, [&core]() { core.exit(2); });
    watchdog.start();

    const int eventLoopResult = core.exec();
    completionCheck.stop();
    watchdog.stop();
    http3Thread.join();

    ok = expect(eventLoopResult == 0, "test timed out") && ok;
    ok = expect(http1Done && http1Succeeded, "HTTPS/1.1 request failed") && ok;
    ok = expect(http1Client.statusCode() == 200, "HTTPS/1.1 status mismatch") && ok;
    ok = expect(http1Client.responseBody() == SwByteArray("shared:demo value"),
                "HTTPS/1.1 body mismatch") && ok;
    ok = expect(http1Client.responseHeader("x-request-protocol") == "HTTP/1.1",
                "HTTPS route did not identify HTTP/1.1") && ok;
    ok = expect(http1Client.responseHeader("x-shared-middleware") == "yes",
                "HTTPS route missed shared middleware") && ok;
    ok = expect(http1Client.responseHeader("x-query-value") == "http1",
                "HTTPS query parsing mismatch") && ok;
    const SwString expectedPort = SwString::number(static_cast<int>(port));
    ok = expect(http1Client.responseHeader("x-local-port") == expectedPort,
                "HTTPS local-port metadata mismatch") && ok;
    ok = expect(http1ChunkedSucceeded,
                "HTTPS/1.1 chunked compatibility request failed") && ok;
    ok = expect(http1ChunkedBody == SwByteArray("chunk-over-http3"),
                "HTTPS/1.1 chunked payload semantics changed") && ok;
    const SwString altSvc = http1Client.responseHeader("alt-svc");
    ok = expect(altSvc.contains(SwString("h3=\":") + expectedPort + SwString("\"")),
                "HTTPS response does not advertise HTTP/3 with Alt-Svc") && ok;

    ok = expect(http3.requestSucceeded, "HTTP/3 request failed") && ok;
    if (!http3.requestSucceeded) {
        std::cerr << "HTTP/3 error: " << http3.error.toStdString() << std::endl;
    }
    ok = expect(http3.status == 200, "HTTP/3 status mismatch") && ok;
    ok = expect(http3.body == SwByteArray("shared:demo value"), "HTTP/3 body mismatch") && ok;
    ok = expect(http3.protocol == "HTTP/3", "HTTP/3 route did not identify HTTP/3") && ok;
    ok = expect(http3.middleware == "yes", "HTTP/3 route missed shared middleware") && ok;
    ok = expect(http3.query == "http3", "HTTP/3 query parsing mismatch") && ok;
    ok = expect(http3.localPort == expectedPort, "HTTP/3 local-port metadata mismatch") && ok;
    ok = expect(http3.multipartSucceeded, "HTTP/3 multipart request failed") && ok;
    if (!http3.multipartSucceeded) {
        std::cerr << "HTTP/3 multipart error: "
                  << http3.multipartError.toStdString() << std::endl;
    }
    ok = expect(http3.multipartStatus == 200, "HTTP/3 multipart status mismatch") && ok;
    ok = expect(http3.multipartBody == SwByteArray("multipart-over-http3"),
                "HTTP/3 multipart form parsing mismatch") && ok;
    ok = expect(http3.fileSucceeded, "HTTP/3 file response failed") && ok;
    if (!http3.fileSucceeded) {
        std::cerr << "HTTP/3 file error: " << http3.fileError.toStdString() << std::endl;
    }
    ok = expect(http3.fileStatus == 200, "HTTP/3 file status mismatch") && ok;
    ok = expect(SwString(http3.fileBody).contains("-----BEGIN CERTIFICATE-----"),
                "HTTP/3 file response body mismatch") && ok;
    ok = expect(http3.chunkedSucceeded, "HTTP/3 chunked response failed") && ok;
    if (!http3.chunkedSucceeded) {
        std::cerr << "HTTP/3 chunked error: "
                  << http3.chunkedError.toStdString() << std::endl;
    }
    ok = expect(http3.chunkedStatus == 200, "HTTP/3 chunked status mismatch") && ok;
    ok = expect(http3.chunkedBody == SwByteArray("chunk-over-http3"),
                "HTTP/3 chunked payload differs from HTTPS/1.1") && ok;
    ok = expect(http3.chunkedLength == SwString("16"),
                "HTTP/3 did not replace a stale Content-Length") && ok;
    ok = expect(http3.chunkedHeadersSanitized,
                "HTTP/3 emitted an invalid response field") && ok;
    ok = expect(http3.headFileSucceeded, "HTTP/3 HEAD file response failed") && ok;
    if (!http3.headFileSucceeded) {
        std::cerr << "HTTP/3 HEAD file error: "
                  << http3.headFileError.toStdString() << std::endl;
    }
    ok = expect(http3.headFileStatus == 200,
                "HTTP/3 HEAD attempted to open the response file") && ok;
    ok = expect(http3.headFileBody.isEmpty(), "HTTP/3 HEAD emitted file bytes") && ok;
    ok = expect(http3.headFileLength == "123",
                "HTTP/3 HEAD lost the representation content length") && ok;
    ok = expect(http3.largeFileSucceeded,
                "HTTP/3 large streamed file request failed") && ok;
    if (!http3.largeFileSucceeded) {
        std::cerr << "HTTP/3 large file error: "
                  << http3.largeFileError.toStdString() << std::endl;
    }
    ok = expect(http3.largeFileStatus == 200,
                "HTTP/3 large streamed file status mismatch") && ok;
    ok = expect(http3.largeFileBody.size() == kLargeFileBytes,
                "HTTP/3 large streamed file length mismatch") && ok;
    if (http3.largeFileBody.size() == kLargeFileBytes) {
        bool contentMatches = true;
        for (std::size_t i = 0; i < kLargeFileBytes; ++i) {
            if (http3.largeFileBody[i] != largeFileByte(i)) {
                contentMatches = false;
                break;
            }
        }
        ok = expect(contentMatches,
                    "HTTP/3 large streamed file content mismatch") && ok;
    }
    ok = expect(http3.asyncGuardSucceeded,
                "HTTP/3 asynchronous guard response failed") && ok;
    if (!http3.asyncGuardSucceeded) {
        std::cerr << "HTTP/3 async guard error: "
                  << http3.asyncGuardError.toStdString() << std::endl;
    }
    ok = expect(http3.asyncGuardStatus == 403,
                "HTTP/3 bypassed the asynchronous pre-route guard") && ok;
    ok = expect(http3.asyncGuardBody == SwByteArray("blocked by async guard"),
                "HTTP/3 asynchronous guard body mismatch") && ok;
    ok = expect(http3.asyncGuardHeader == "applied",
                "HTTP/3 asynchronous guard header missing") && ok;
    ok = expect(http3.asyncRouteSucceeded,
                "HTTP/3 asynchronous route response failed") && ok;
    if (!http3.asyncRouteSucceeded) {
        std::cerr << "HTTP/3 async route error: "
                  << http3.asyncRouteError.toStdString() << std::endl;
    }
    ok = expect(http3.asyncRouteStatus == 202,
                "HTTP/3 asynchronous route status mismatch") && ok;
    ok = expect(http3.asyncRouteBody == SwByteArray("async route over HTTP/3"),
                "HTTP/3 asynchronous route body mismatch") && ok;
    ok = expect(http3.asyncTimeoutSucceeded,
                "HTTP/3 asynchronous route timeout response failed") && ok;
    if (!http3.asyncTimeoutSucceeded) {
        std::cerr << "HTTP/3 async timeout error: "
                  << http3.asyncTimeoutError.toStdString() << std::endl;
    }
    ok = expect(http3.asyncTimeoutStatus == 504,
                "HTTP/3 did not apply the shared asynchronous route timeout") && ok;
    ok = expect(http3.asyncTimeoutAltSvc.contains(
                    SwString("h3=\":") + expectedPort + SwString("\"")),
                "HTTP/3 timeout response missed the shared response filter") && ok;
    ok = expect(http3.methodGuardSucceeded,
                "HTTP/3 lowercase method guard response failed") && ok;
    if (!http3.methodGuardSucceeded) {
        std::cerr << "HTTP/3 method guard error: "
                  << http3.methodGuardError.toStdString() << std::endl;
    }
    ok = expect(http3.methodGuardStatus == 403,
                "lowercase HTTP/3 :method bypassed a POST pre-route guard") && ok;
    ok = expect(http3.methodGuardBody ==
                    SwByteArray("blocked normalized POST method"),
                "HTTP/3 method guard body mismatch") && ok;
    ok = expect(http3.strictRequestValidationSucceeded,
                "HTTP/3 accepted a malformed request field section") && ok;
    if (!http3.strictRequestValidationSucceeded) {
        std::cerr << "HTTP/3 strict validation error: "
                  << http3.strictRequestValidationError.toStdString() << std::endl;
    }
    ok = expect(asyncPreRouteCalls.load() == 11,
                "HTTP transports did not share the asynchronous pre-route pipeline") && ok;
    ok = expect(asyncGuardBlocks.load() == 1,
                "asynchronous guard did not run exactly once") && ok;
    ok = expect(asyncRouteCalls.load() == 1,
                "asynchronous HTTP/3 route did not run exactly once") && ok;
    ok = expect(asyncTimeoutCalls.load() == 1,
                "asynchronous HTTP/3 timeout route did not run exactly once") && ok;
    ok = expect(methodGuardBlocks.load() == 1 &&
                    methodGuardRouteCalls.load() == 0,
                "HTTP/3 method normalization did not run before pre-route authorization") && ok;
    ok = expect(middlewareCalls.load() == 8,
                "middleware was not shared exactly eight times") && ok;
    ok = expect(handlerCalls.load() == 8,
                "route handler was not shared exactly eight times") && ok;
    const SwHttpServerMetrics metrics = app.metricsSnapshot();
    ok = expect(metrics.totalRequests == 12 && metrics.totalResponses == 12,
                "HTTP/1.1 and HTTP/3 did not share server metrics") && ok;

    ok = expect(app.listen("127.0.0.1", 0),
                "switching from HTTPS/HTTP3 to plaintext HTTP failed") && ok;
    ok = expect(app.isHttpListening(),
                "plaintext replacement listener is not active") && ok;
    ok = expect(!app.isHttpsListening(),
                "HTTPS listener survived plaintext replacement") && ok;
    ok = expect(!app.isHttp3Listening(),
                "HTTP/3 listener survived plaintext replacement") && ok;
    app.close();
    ok = expect(!app.isHttpListening(), "HTTP listener survived close()") && ok;

    if (ok) {
        std::cout << "HttpAppHttp3SelfTest passed" << std::endl;
        return 0;
    }
    return 1;
}
