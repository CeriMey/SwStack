#include "SwCoreApplication.h"
#include "SwHttpServer.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "SwString.h"
#include "SwList.h"
#include "SwByteArray.h"
#include "SwMap.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwDebug.h"

#if defined(__has_include)
#if __has_include("SwHttpServerSelfTestConfig.h")
#include "SwHttpServerSelfTestConfig.h"
#endif
#endif

#ifndef SW_HTTP_SERVER_SELFTEST_STATIC_ROOT
#define SW_HTTP_SERVER_SELFTEST_STATIC_ROOT "exemples/35-HttpServerSelfTest/runtime/http_selftest_www"
#endif

#ifndef SW_HTTP_SERVER_SELFTEST_TEMP_ROOT
#define SW_HTTP_SERVER_SELFTEST_TEMP_ROOT "exemples/35-HttpServerSelfTest/runtime/http_selftest_tmp"
#endif

struct ParsedHttpResponse {
    bool valid = false;
    int statusCode = 0;
    SwMap<SwString, SwString> headers;
    SwByteArray body;
};

static const std::size_t kMultipartLargeFileBytes = 5 * 1024 * 1024;

static bool parseStatusLine(const SwByteArray& headerBytes, int& statusCodeOut) {
    statusCodeOut = 0;
    SwString headerString(headerBytes.toStdString());
    SwList<SwString> lines = headerString.split("\r\n");
    if (lines.isEmpty()) {
        return false;
    }

    SwString statusLine = lines[0].trimmed();
    if (!statusLine.startsWith("HTTP/1.")) {
        return false;
    }

    SwList<SwString> tokens = statusLine.split(' ');
    if (tokens.size() < 2) {
        return false;
    }

    bool ok = false;
    int code = tokens[1].toInt(&ok);
    if (!ok || code < 100 || code > 599) {
        return false;
    }

    statusCodeOut = code;
    return true;
}

static bool decodeChunkedBody(const SwByteArray& encoded, SwByteArray& decodedOut) {
    decodedOut.clear();
    int pos = 0;
    while (true) {
        int lineEnd = encoded.indexOf("\r\n", pos);
        if (lineEnd < 0) {
            return false;
        }
        SwString sizeLine(encoded.mid(pos, lineEnd - pos).toStdString());
        std::size_t chunkSize = 0;
        if (!swHttpParseHexSize(sizeLine, chunkSize)) {
            return false;
        }
        pos = lineEnd + 2;

        if (chunkSize == 0) {
            if (encoded.size() < static_cast<std::size_t>(pos + 2)) {
                return false;
            }
            if (encoded[pos] != '\r' || encoded[pos + 1] != '\n') {
                return false;
            }
            return true;
        }

        if (encoded.size() < static_cast<std::size_t>(pos) + chunkSize + 2) {
            return false;
        }
        decodedOut.append(encoded.constData() + pos, chunkSize);
        pos += static_cast<int>(chunkSize);
        if (encoded[pos] != '\r' || encoded[pos + 1] != '\n') {
            return false;
        }
        pos += 2;
    }
}

static bool parseResponse(const SwByteArray& raw, ParsedHttpResponse& parsed) {
    parsed = ParsedHttpResponse();
    int headerBoundary = raw.indexOf("\r\n\r\n");
    if (headerBoundary < 0) {
        return false;
    }

    SwByteArray headersPart = raw.left(headerBoundary);
    SwByteArray bodyPart = raw.mid(headerBoundary + 4);

    int statusCode = 0;
    if (!parseStatusLine(headersPart, statusCode)) {
        return false;
    }

    parsed.statusCode = statusCode;
    parsed.valid = true;

    SwString headerText(headersPart.toStdString());
    SwList<SwString> lines = headerText.split("\r\n");
    for (std::size_t i = 1; i < lines.size(); ++i) {
        SwString line = lines[i];
        if (line.isEmpty()) continue;
        int colon = line.indexOf(":");
        if (colon < 0) continue;
        SwString key = line.left(colon).trimmed().toLower();
        SwString value = line.mid(colon + 1).trimmed();
        parsed.headers[key] = value;
    }

    if (parsed.headers.contains("transfer-encoding") && parsed.headers["transfer-encoding"].toLower().contains("chunked")) {
        SwByteArray decoded;
        if (!decodeChunkedBody(bodyPart, decoded)) {
            return false;
        }
        parsed.body = decoded;
    } else {
        parsed.body = bodyPart;
    }

    return true;
}

class HttpServerSelfTestRunner : public SwObject {
    SW_OBJECT(HttpServerSelfTestRunner, SwObject)
public:
    HttpServerSelfTestRunner(SwHttpServer* server, uint16_t port, SwCoreApplication* app, SwObject* parent = nullptr)
        : SwObject(parent),
          m_server(server),
          m_port(port),
          m_app(app),
          m_timeout(12000, this) {
        connect(&m_timeout, SIGNAL(timeout), this, &HttpServerSelfTestRunner::onCaseTimeout_);
    }

    void start() {
        if (!prepareStaticFixture_()) {
            return;
        }
        prepareMultipartPayload_();
        runNextCase_();
    }

private:
    SwHttpServer* m_server = nullptr;
    uint16_t m_port = 0;
    SwCoreApplication* m_app = nullptr;

    int m_caseIndex = 0;
    SwTcpSocket* m_client = nullptr;
    SwList<SwString> m_requestParts;
    std::size_t m_requestPartIndex = 0;
    SwByteArray m_responseRaw;
    SwByteArray m_staticFixture;
    SwByteArray m_multipartPayload;
    SwString m_staticRoot = SW_HTTP_SERVER_SELFTEST_STATIC_ROOT;
    SwString m_staticFileName = "http_selftest_large.bin";
    bool m_caseDone = false;

    SwTimer m_timeout;

private slots:
    void onCaseTimeout_() {
        fail_("timeout");
    }

    void onClientConnected_() {
        sendNextPart_();
    }

    void onClientReadyRead_() {
        if (!m_client) return;
        while (true) {
            SwString chunk = m_client->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_responseRaw.append(chunk.data(), chunk.size());
        }
    }

    void onClientDisconnected_() {
        if (m_caseDone) {
            return;
        }
        m_caseDone = true;
        m_timeout.stop();

        ParsedHttpResponse parsed;
        if (!parseResponse(m_responseRaw, parsed)) {
            swError() << "[HttpServerSelfTest] Case " << m_caseIndex
                      << " raw response size=" << m_responseRaw.size()
                      << " preview=" << SwString(m_responseRaw.left(256).toStdString());
            fail_("response parse failure");
            return;
        }

        if (!validateCase_(parsed)) {
            return;
        }

        cleanupClient_();
        ++m_caseIndex;
        SwTimer::singleShot(0, this, &HttpServerSelfTestRunner::runNextCase_);
    }

private:
    void runNextCase_() {
        if (m_caseIndex >= 11) {
            swDebug() << "[HttpServerSelfTest] PASS all cases";
            m_server->close();
            m_app->exit(0);
            return;
        }

        cleanupClient_();
        m_caseDone = false;
        m_responseRaw.clear();
        m_requestParts.clear();
        m_requestPartIndex = 0;

        if (!buildCaseRequest_(m_caseIndex, m_requestParts)) {
            fail_("invalid test case build");
            return;
        }

        m_client = new SwTcpSocket(this);
        connect(m_client, SIGNAL(connected), this, &HttpServerSelfTestRunner::onClientConnected_);
        connect(m_client, SIGNAL(readyRead), this, &HttpServerSelfTestRunner::onClientReadyRead_);
        connect(m_client, SIGNAL(disconnected), this, &HttpServerSelfTestRunner::onClientDisconnected_);
        connect(m_client, SIGNAL(errorOccurred), [this](int) {
            fail_("client error");
        });

        m_timeout.setSingleShot(true);
        m_timeout.start(12000);

        if (!m_client->connectToHost("127.0.0.1", m_port)) {
            fail_("connectToHost failed");
            return;
        }
    }

    bool buildCaseRequest_(int index, SwList<SwString>& partsOut) const {
        if (index == 0) {
            partsOut.append("GET /selftest HTTP/1.1\r\nHost: 127.0.0.1\r\nCon");
            partsOut.append("nection: close\r\n\r\n");
            return true;
        }
        if (index == 1) {
            partsOut.append("GET /users/42/profile?verbose=1 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            return true;
        }
        if (index == 2) {
            partsOut.append("POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
            partsOut.append("5\r\nhello\r\n");
            partsOut.append("6\r\n world\r\n0\r\n\r\n");
            return true;
        }
        if (index == 3) {
            partsOut.append("GET /static/http_selftest_large.bin HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=100-199\r\nConnection: close\r\n\r\n");
            return true;
        }
        if (index == 4) {
            partsOut.append("HEAD /static/http_selftest_large.bin HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            return true;
        }
        if (index == 5) {
            partsOut.append("GET /chunked HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            return true;
        }
        if (index == 6) {
            return buildMultipartUploadRequest_(partsOut);
        }
        if (index == 7) {
            partsOut.append("POST /selftest HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            return true;
        }
        if (index == 8) {
            partsOut.append("GET /selftest/ HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            return true;
        }
        if (index == 9) {
            partsOut.append("GET /uuid/550e8400-e29b-41d4-a716-446655440000 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            return true;
        }
        if (index == 10) {
            partsOut.append("GET /uuid/not-a-uuid HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            return true;
        }
        return false;
    }

    bool buildMultipartUploadRequest_(SwList<SwString>& partsOut) const {
        const SwString boundary = "----SwStackBoundary7MA4YWxkTrZu0gW";

        SwByteArray body;
        body.append("--" + boundary.toStdString() + "\r\n");
        body.append("Content-Disposition: form-data; name=\"meta\"\r\n");
        body.append("\r\n");
        body.append("alpha");
        body.append("\r\n");

        body.append("--" + boundary.toStdString() + "\r\n");
        body.append("Content-Disposition: form-data; name=\"file\"; filename=\"big.bin\"\r\n");
        body.append("Content-Type: application/octet-stream\r\n");
        body.append("\r\n");
        body.append(m_multipartPayload);
        body.append("\r\n");
        body.append("--" + boundary.toStdString() + "--\r\n");

        SwString headers;
        headers += "POST /upload HTTP/1.1\r\n";
        headers += "Host: 127.0.0.1\r\n";
        headers += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
        headers += "Content-Length: " + SwString::number(static_cast<long long>(body.size())) + "\r\n";
        headers += "Connection: close\r\n";
        headers += "\r\n";

        partsOut.append(headers);

        int pos = 0;
        const int chunk = 64 * 1024;
        while (pos < static_cast<int>(body.size())) {
            int take = static_cast<int>(body.size()) - pos;
            if (take > chunk) {
                take = chunk;
            }
            partsOut.append(SwString::fromLatin1(body.constData() + pos, static_cast<size_t>(take)));
            pos += take;
        }

        return true;
    }

    bool validateCase_(const ParsedHttpResponse& parsed) {
        swDebug() << "[HttpServerSelfTest] Case " << m_caseIndex
                  << " status=" << parsed.statusCode
                  << " bodySize=" << parsed.body.size();
        for (auto it = parsed.headers.begin(); it != parsed.headers.end(); ++it) {
            swDebug() << "[HttpServerSelfTest]   hdr " << it.key() << "=" << it.value();
        }

        if (m_caseIndex == 0) {
            if (parsed.statusCode != 200 || parsed.body != SwByteArray("OK")) {
                swError() << "[HttpServerSelfTest] expected status=200 body=OK got status="
                          << parsed.statusCode << " body=" << SwString(parsed.body.toStdString());
                fail_("case 0 failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 1) {
            if (parsed.statusCode != 200 || parsed.body != SwByteArray("user:42")) {
                swError() << "[HttpServerSelfTest] expected status=200 body=user:42 got status="
                          << parsed.statusCode << " body=" << SwString(parsed.body.toStdString());
                fail_("case 1 failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 2) {
            if (parsed.statusCode != 200 || parsed.body != SwByteArray("hello world")) {
                swError() << "[HttpServerSelfTest] expected status=200 body=hello world got status="
                          << parsed.statusCode << " body=" << SwString(parsed.body.toStdString());
                fail_("case 2 failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 3) {
            if (parsed.statusCode != 206) {
                swError() << "[HttpServerSelfTest] case3 expected status=206 got " << parsed.statusCode;
                fail_("case 3 status failed");
                return false;
            }
            if (parsed.body.size() != 100) {
                swError() << "[HttpServerSelfTest] case3 expected body size=100 got " << parsed.body.size();
                fail_("case 3 length failed");
                return false;
            }
            SwByteArray expected = m_staticFixture.mid(100, 100);
            if (parsed.body != expected) {
                swError() << "[HttpServerSelfTest] case3 body mismatch";
                fail_("case 3 body mismatch");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 4) {
            if (parsed.statusCode != 200) {
                swError() << "[HttpServerSelfTest] case4 expected status=200 got " << parsed.statusCode;
                fail_("case 4 status failed");
                return false;
            }
            if (!parsed.body.isEmpty()) {
                swError() << "[HttpServerSelfTest] case4 expected empty body got size " << parsed.body.size();
                fail_("case 4 body should be empty");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 5) {
            if (parsed.statusCode != 200 || parsed.body != SwByteArray("abc")) {
                swError() << "[HttpServerSelfTest] case5 expected status=200 body=abc got status="
                          << parsed.statusCode << " body=" << SwString(parsed.body.toStdString());
                fail_("case 5 failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 6) {
            SwString expectedBody = "multipart-ok:" + SwString::number(static_cast<long long>(kMultipartLargeFileBytes)) + ":alpha";
            if (parsed.statusCode != 200 || parsed.body != SwByteArray(expectedBody.toStdString())) {
                swError() << "[HttpServerSelfTest] case6 expected status=200 body=" << expectedBody
                          << " got status=" << parsed.statusCode
                          << " body=" << SwString(parsed.body.toStdString());
                fail_("case 6 failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 7) {
            if (parsed.statusCode != 405) {
                swError() << "[HttpServerSelfTest] case7 expected status=405 got " << parsed.statusCode;
                fail_("case 7 status failed");
                return false;
            }
            const SwString allowHeader = parsed.headers.value("allow", SwString());
            if (!allowHeader.contains("GET")) {
                swError() << "[HttpServerSelfTest] case7 expected Allow header to contain GET, got " << allowHeader;
                fail_("case 7 allow failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 8) {
            if (parsed.statusCode != 308) {
                swError() << "[HttpServerSelfTest] case8 expected status=308 got " << parsed.statusCode;
                fail_("case 8 status failed");
                return false;
            }
            const SwString location = parsed.headers.value("location", SwString());
            if (location != "/selftest") {
                swError() << "[HttpServerSelfTest] case8 expected location=/selftest got " << location;
                fail_("case 8 location failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 9) {
            if (parsed.statusCode != 200 || parsed.body != SwByteArray("uuid-ok")) {
                swError() << "[HttpServerSelfTest] case9 expected status=200 body=uuid-ok got status="
                          << parsed.statusCode << " body=" << SwString(parsed.body.toStdString());
                fail_("case 9 failed");
                return false;
            }
            return true;
        }
        if (m_caseIndex == 10) {
            if (parsed.statusCode != 404) {
                swError() << "[HttpServerSelfTest] case10 expected status=404 got " << parsed.statusCode;
                fail_("case 10 failed");
                return false;
            }
            return true;
        }
        return false;
    }

    void prepareMultipartPayload_() {
        m_multipartPayload.resize(kMultipartLargeFileBytes);
        for (std::size_t i = 0; i < m_multipartPayload.size(); ++i) {
            m_multipartPayload[i] = static_cast<char>('a' + (i % 26));
        }
        swDebug() << "[HttpServerSelfTest] multipart payload bytes=" << m_multipartPayload.size();
    }

    void sendNextPart_() {
        if (!m_client) {
            fail_("missing client");
            return;
        }
        if (m_requestPartIndex >= m_requestParts.size()) {
            return;
        }

        m_client->write(m_requestParts[m_requestPartIndex]);
        ++m_requestPartIndex;
        if (m_requestPartIndex < m_requestParts.size()) {
            SwTimer::singleShot(10, [this]() { sendNextPart_(); });
        }
    }

    bool prepareStaticFixture_() {
        SwString absRoot = swDirPlatform().absolutePath(m_staticRoot);
        if (!SwDir::mkpathAbsolute(absRoot, true)) {
            fail_("unable to create static fixture directory");
            return false;
        }

        m_staticFixture.resize(4096);
        for (std::size_t i = 0; i < m_staticFixture.size(); ++i) {
            m_staticFixture[i] = static_cast<char>('A' + (i % 26));
        }

        SwString filePath = absRoot + "/" + m_staticFileName;
        swDebug() << "[HttpServerSelfTest] static root=" << absRoot;
        swDebug() << "[HttpServerSelfTest] static file=" << filePath;
        SwFile file(filePath);
        if (!file.openBinary(SwFile::Write)) {
            fail_("unable to prepare static fixture");
            return false;
        }
        if (!file.write(m_staticFixture)) {
            file.close();
            fail_("unable to write static fixture");
            return false;
        }
        file.close();
        if (!swFilePlatform().isFile(filePath)) {
            fail_("fixture file not visible after write");
            return false;
        }
        return true;
    }

    void cleanupClient_() {
        if (!m_client) {
            return;
        }
        m_client->disconnectAllSlots();
        m_client->close();
        m_client->deleteLater();
        m_client = nullptr;
    }

    void fail_(const SwString& reason) {
        swError() << "[HttpServerSelfTest] FAIL: " << reason;
        cleanupClient_();
        m_timeout.stop();
        m_server->close();
        m_app->exit(1);
    }
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwHttpServer server(nullptr);
    SwHttpLimits limits;
    limits.maxBodyBytes = 12 * 1024 * 1024;
    limits.maxChunkSize = 4 * 1024 * 1024;
    limits.maxMultipartParts = 32;
    limits.maxMultipartPartHeadersBytes = 8 * 1024;
    limits.enableMultipartFileStreaming = true;
    limits.multipartTempDirectory = SW_HTTP_SERVER_SELFTEST_TEMP_ROOT;
    server.setLimits(limits);
    server.setTrailingSlashPolicy(SwHttpRouter::TrailingSlashPolicy::RedirectToNoSlash);

    uint16_t port = 0;
    for (uint16_t p = 19500; p < 19600; ++p) {
        if (server.listen(p)) {
            port = p;
            break;
        }
    }
    if (port == 0) {
        swError() << "[HttpServerSelfTest] Failed to bind HTTP server test port";
        return 1;
    }

    server.addRoute("GET", "/selftest", [](const SwHttpRequest& request) {
        SwHttpResponse response = swHttpTextResponse(200, "OK");
        response.closeConnection = !request.keepAlive;
        return response;
    });

    server.addNamedRoute("user-profile", "GET", "/users/:id(int)/profile", [](const SwHttpRequest& request) {
        SwString userId = request.pathParams.contains("id") ? request.pathParams["id"] : SwString("none");
        SwHttpResponse response = swHttpTextResponse(200, "user:" + userId);
        response.closeConnection = !request.keepAlive;
        return response;
    });

    server.addRoute("GET", "/uuid/:id(uuid)", [](const SwHttpRequest& request) {
        SwHttpResponse response = swHttpTextResponse(200, "uuid-ok");
        response.closeConnection = !request.keepAlive;
        return response;
    });

    server.addRoute("POST", "/echo", [](const SwHttpRequest& request) {
        SwHttpResponse response;
        response.status = 200;
        response.reason = swHttpStatusReason(200);
        response.headers["content-type"] = "text/plain; charset=utf-8";
        response.body = request.body;
        response.closeConnection = !request.keepAlive;
        return response;
    });

    server.addRoute("GET", "/chunked", [](const SwHttpRequest& request) {
        SwHttpResponse response;
        response.status = 200;
        response.reason = swHttpStatusReason(200);
        response.headers["content-type"] = "text/plain; charset=utf-8";
        response.useChunkedTransfer = true;
        response.chunkedParts.append(SwByteArray("a"));
        response.chunkedParts.append(SwByteArray("b"));
        response.chunkedParts.append(SwByteArray("c"));
        response.closeConnection = !request.keepAlive;
        return response;
    });

    server.addRoute("POST", "/upload", [](const SwHttpRequest& request) {
        if (!request.isMultipartFormData) {
            SwHttpResponse response = swHttpTextResponse(400, "not-multipart");
            response.closeConnection = !request.keepAlive;
            return response;
        }

        swDebug() << "[HttpServerSelfTest] upload parts=" << request.multipartParts.size();
        SwString meta = request.formFields.value("meta", SwString());
        long long fileBytes = -1;
        for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
            swDebug() << "[HttpServerSelfTest] upload part[" << static_cast<int>(i) << "] name=" << part.name
                      << " file=" << part.fileName
                      << " bytes=" << part.sizeBytes
                      << " onDisk=" << part.storedOnDisk
                      << " tempPath=" << part.tempFilePath;
            if (part.name == "file") {
                if (part.storedOnDisk && !part.tempFilePath.isEmpty()) {
                    fileBytes = static_cast<long long>(part.sizeBytes);
                } else {
                    fileBytes = static_cast<long long>(part.data.size());
                }
            }
        }

        if (fileBytes < 0) {
            SwHttpResponse response = swHttpTextResponse(400, "missing-file");
            response.closeConnection = !request.keepAlive;
            return response;
        }

        SwHttpResponse response = swHttpTextResponse(200, "multipart-ok:" + SwString::number(fileBytes) + ":" + meta);
        response.closeConnection = !request.keepAlive;
        return response;
    });

    SwHttpStaticOptions staticOptions;
    staticOptions.enableRange = true;
    staticOptions.ioChunkBytes = 1024;
    staticOptions.cacheControl = "no-cache";
    server.mountStatic("/static", SW_HTTP_SERVER_SELFTEST_STATIC_ROOT, staticOptions);

    {
        SwMap<SwString, SwString> params;
        params["id"] = "42";
        SwString built;
        if (!server.buildUrl("user-profile", params, built) || built != "/users/42/profile") {
            swError() << "[HttpServerSelfTest] FAIL: named route reverse URL mismatch, got " << built;
            return 1;
        }
    }

    HttpServerSelfTestRunner runner(&server, port, &app);
    runner.start();
    return app.exec();
}
