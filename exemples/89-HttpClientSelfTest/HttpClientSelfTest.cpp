#include "SwCoreApplication.h"
#include "SwHttpClient.h"
#include "SwHttpServer.h"
#include "SwTcpServer.h"
#include "SwTimer.h"

#include <cstddef>

class RawHttpResponseServer : public SwObject {
    SW_OBJECT(RawHttpResponseServer, SwObject)

public:
    explicit RawHttpResponseServer(SwObject* parent = nullptr)
        : SwObject(parent) {
        connect(&m_server, &SwTcpServer::newConnection, this,
                &RawHttpResponseServer::acceptPending_);
    }

    bool listen(uint16_t port) { return m_server.listen(port); }

private:
    void acceptPending_() {
        while (SwTcpSocket* socket = m_server.nextPendingConnection()) {
            socket->setParent(this);
            m_requests[socket] = SwByteArray();
            connect(socket, &SwIODevice::readyRead, this, [this, socket]() {
                SwByteArray& request = m_requests[socket];
                while (true) {
                    SwByteArray bytes = socket->read();
                    if (bytes.isEmpty()) {
                        break;
                    }
                    request.append(bytes);
                }
                if (request.indexOf("\r\n\r\n") < 0) {
                    return;
                }

                SwByteArray response;
                if (request.indexOf("GET /interim ") == 0) {
                    response = SwByteArray(
                        "HTTP/1.1 100 Continue\r\nX-Interim: yes\r\n\r\n"
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
                } else if (request.indexOf("GET /no-content ") == 0) {
                    response = SwByteArray(
                        "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
                } else if (request.indexOf("GET /not-modified ") == 0) {
                    response = SwByteArray(
                        "HTTP/1.1 304 Not Modified\r\nContent-Length: 123\r\n"
                        "Connection: close\r\n\r\n");
                } else {
                    response = SwByteArray(
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                        "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\nOK");
                }
                m_requests.remove(socket);
                socket->write(response);
                socket->close();
            });
            connect(socket, &SwAbstractSocket::disconnected, this, [this, socket]() {
                m_requests.remove(socket);
                socket->deleteLater();
            });
        }
    }

    SwTcpServer m_server;
    SwMap<SwTcpSocket*, SwByteArray> m_requests;
};

class HttpClientSelfTestRunner : public SwObject {
    SW_OBJECT(HttpClientSelfTestRunner, SwObject)

public:
    HttpClientSelfTestRunner(SwHttpClient* client,
                             const SwString& baseUrl,
                             const SwString& hostnameBaseUrl,
                             const SwString& rawBaseUrl,
                             SwCoreApplication* app)
        : m_client(client),
          m_baseUrl(baseUrl),
          m_hostnameBaseUrl(hostnameBaseUrl),
          m_rawBaseUrl(rawBaseUrl),
          m_app(app) {
        connect(m_client, &SwHttpClient::finished, this,
                &HttpClientSelfTestRunner::onFinished_);
        connect(m_client, &SwHttpClient::errorOccurred, this,
                &HttpClientSelfTestRunner::onError_);
    }

    void start() {
        if (!m_client->get(m_baseUrl + "/peer")) {
            fail_("first request did not start");
        }
    }

private:
    void onFinished_(const SwByteArray& body) {
        if (m_stage == 0) {
            m_firstPeer = SwString(body).trimmed();
            if (m_firstPeer.isEmpty()) {
                fail_("empty first peer id");
                return;
            }
            m_stage = 1;
            if (!m_client->get(m_baseUrl + "/peer")) {
                fail_("keep-alive request did not start");
            }
            return;
        }

        if (m_stage == 1) {
            const SwString secondPeer = SwString(body).trimmed();
            if (secondPeer != m_firstPeer) {
                fail_("connection was not reused");
                return;
            }
            m_stage = 2;
            m_streamedBytes = 0;
            m_client->setResponseSink([this](const char*, std::size_t size) {
                m_streamedBytes += size;
                return true;
            });
            if (!m_client->get(m_baseUrl + "/stream")) {
                fail_("streaming request did not start");
            }
            return;
        }

        if (m_stage == 2) {
            if (!body.isEmpty() || m_streamedBytes != 2 * 1024 * 1024) {
                fail_("streaming sink/body contract mismatch");
                return;
            }
            m_client->clearResponseSink();
            m_largeRequest.resize(6 * 1024 * 1024);
            for (std::size_t i = 0; i < m_largeRequest.size(); ++i) {
                m_largeRequest.data()[i] = static_cast<char>(i % 251);
            }
            m_stage = 3;
            if (!m_client->post(m_baseUrl + "/upload",
                                m_largeRequest,
                                "application/octet-stream")) {
                fail_("large progressive upload did not start");
            }
            return;
        }

        if (m_stage == 3) {
            if (SwString(body).trimmed() != "6291456") {
                fail_("large progressive upload was truncated");
                return;
            }
            m_largeRequest = SwByteArray();
            SwHttpClient::Timeouts timeouts = m_client->timeouts();
            timeouts.connectTimeoutMs = 2000;
            timeouts.headerTimeoutMs = 1000;
            m_client->setTimeouts(timeouts);
            m_stage = 4;
            if (!m_client->get(m_hostnameBaseUrl + "/peer")) {
                fail_("async hostname request did not start");
            }
            return;
        }

        if (m_stage == 4) {
            if (SwString(body).trimmed().isEmpty()) {
                fail_("async hostname response was empty");
                return;
            }
            m_stage = 5;
            m_reenteredSink = false;
            m_client->setResponseSink([this](const char*, std::size_t) {
                if (!m_reenteredSink) {
                    m_reenteredSink = true;
                    m_client->clearResponseSink();
                    m_stage = 6;
                    if (!m_client->get(m_baseUrl + "/peer")) {
                        fail_("reentrant sink request did not start");
                    }
                }
                return true;
            });
            if (!m_client->get(m_baseUrl + "/stream")) {
                fail_("reentrant streaming request did not start");
            }
            return;
        }

        if (m_stage == 6) {
            if (!m_reenteredSink || SwString(body).trimmed().isEmpty()) {
                fail_("reentrant sink generation guard failed");
                return;
            }
            m_stage = 7;
            if (!m_client->get(m_rawBaseUrl + "/interim")) {
                fail_("interim response request did not start");
            }
            return;
        }

        if (m_stage == 7) {
            if (m_client->statusCode() != 200 || body != SwByteArray("OK")) {
                fail_("100/final coalesced response mismatch");
                return;
            }
            m_stage = 8;
            if (!m_client->get(m_rawBaseUrl + "/no-content")) {
                fail_("204 request did not start");
            }
            return;
        }

        if (m_stage == 8) {
            if (m_client->statusCode() != 204 || !body.isEmpty()) {
                fail_("204 response body handling failed");
                return;
            }
            m_stage = 9;
            if (!m_client->get(m_rawBaseUrl + "/not-modified")) {
                fail_("304 request did not start");
            }
            return;
        }

        if (m_stage == 9) {
            if (m_client->statusCode() != 304 || !body.isEmpty()) {
                fail_("304 response body handling failed");
                return;
            }
            m_stage = 10;
            if (!m_client->get(m_rawBaseUrl + "/bad-framing")) {
                fail_("hostile framing request did not start");
            }
            return;
        }

        if (m_stage == 5) {
            fail_("cancelled streaming request completed after reentry");
            return;
        }

        if (m_stage == 11) {
            fail_("timeout request unexpectedly completed");
            return;
        }

        fail_("unexpected completion");
    }

    void startTimeoutCase_() {
            SwHttpClient::Timeouts timeouts = m_client->timeouts();
            timeouts.headerTimeoutMs = 50;
            m_client->setTimeouts(timeouts);
            m_stage = 11;
            if (!m_client->get(m_baseUrl + "/slow")) {
                fail_("timeout request did not start");
            }
    }

    void onError_(int error) {
        if (m_stage == 10 && error == -14) {
            startTimeoutCase_();
            return;
        }
        if (m_stage == 11 && error == -8) {
            m_app->exit(0);
            return;
        }
        fail_("unexpected client error");
    }

    void fail_(const SwString& reason) {
        swError() << "[HttpClientSelfTest] FAIL: " << reason;
        m_app->exit(1);
    }

    SwHttpClient* m_client = nullptr;
    SwString m_baseUrl;
    SwString m_hostnameBaseUrl;
    SwString m_rawBaseUrl;
    SwCoreApplication* m_app = nullptr;
    int m_stage = 0;
    SwString m_firstPeer;
    std::size_t m_streamedBytes = 0;
    SwByteArray m_largeRequest;
    bool m_reenteredSink = false;
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    SwHttpServer server;

    uint16_t port = 0;
    for (uint16_t candidate = 19620; candidate < 19720; ++candidate) {
        if (server.listen(candidate)) {
            port = candidate;
            break;
        }
    }
    if (port == 0) {
        return 1;
    }

    RawHttpResponseServer rawServer;
    uint16_t rawPort = 0;
    for (uint16_t candidate = 19720; candidate < 19820; ++candidate) {
        if (rawServer.listen(candidate)) {
            rawPort = candidate;
            break;
        }
    }
    if (rawPort == 0) {
        return 1;
    }

    server.addRoute("GET", "/peer", [](const SwHttpRequest& request) {
        SwHttpResponse response = swHttpTextResponse(
            200, SwString::number(static_cast<int>(request.peerPort)));
        response.closeConnection = false;
        return response;
    });
    server.addRoute("GET", "/stream", [](const SwHttpRequest&) {
        SwHttpResponse response;
        response.status = 200;
        response.reason = "OK";
        response.body.resize(2 * 1024 * 1024);
        response.closeConnection = false;
        return response;
    });
    server.addRoute("POST", "/upload", [](const SwHttpRequest& request) {
        SwHttpResponse response = swHttpTextResponse(
            200, SwString::number(static_cast<long long>(request.body.size())));
        response.closeConnection = false;
        return response;
    });
    server.addRouteAsync("GET", "/slow",
                         [](const SwHttpRequest&, const SwHttpRouteResponder&) {
                             // Intentionally never complete: the client deadline must fire.
                         });

    SwHttpClient client;
    HttpClientSelfTestRunner runner(
        &client,
        "http://127.0.0.1:" + SwString::number(static_cast<int>(port)),
        "http://localhost:" + SwString::number(static_cast<int>(port)),
        "http://127.0.0.1:" + SwString::number(static_cast<int>(rawPort)),
        &app);
    SwTimer watchdog(15000);
    watchdog.setSingleShot(true);
    SwObject::connect(&watchdog, &SwTimer::timeout, [&app]() { app.exit(2); });
    watchdog.start();
    runner.start();
    return app.exec();
}
