#include "SwCoreApplication.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "SwWebSocket.h"
#include "SwWebSocketServer.h"

#include <iostream>

static SwString wsUrl(const SwString& host, uint16_t port, const SwString& path = "/") {
    return "ws://" + host + ":" + SwString::number(static_cast<int>(port)) + path;
}

class HttpConnectTunnel : public SwObject {
    SW_OBJECT(HttpConnectTunnel, SwObject)

public:
    explicit HttpConnectTunnel(SwTcpSocket* client, SwObject* parent = nullptr)
        : SwObject(parent),
          client_(client) {
        if (client_) {
            client_->setParent(this);
            connect(client_, SIGNAL(readyRead), this, &HttpConnectTunnel::onClientReadyRead_);
            connect(client_, SIGNAL(disconnected), this, &HttpConnectTunnel::onClientDisconnected_);
        }
    }

private slots:
    void onClientReadyRead_() {
        if (!client_) {
            return;
        }

        while (true) {
            SwString chunk = client_->read();
            if (chunk.isEmpty()) {
                break;
            }
            clientBuffer_.append(chunk.data(), chunk.size());
        }

        if (!tunnelEstablished_) {
            const int boundary = clientBuffer_.indexOf("\r\n\r\n");
            if (boundary < 0) {
                return;
            }

            const SwString headerText(clientBuffer_.left(boundary));
            clientBuffer_.remove(0, boundary + 4);

            SwList<SwString> lines = headerText.split("\r\n");
            if (lines.isEmpty()) {
                closeBoth_();
                return;
            }

            SwString requestLine = lines[0].trimmed();
            SwString upper = requestLine.toUpper();
            if (!upper.startsWith("CONNECT ")) {
                closeBoth_();
                return;
            }

            int sp1 = requestLine.indexOf(" ");
            if (sp1 < 0) {
                closeBoth_();
                return;
            }
            int sp2 = requestLine.indexOf(" ", static_cast<size_t>(sp1 + 1));
            SwString hostPort = (sp2 >= 0)
                ? requestLine.mid(sp1 + 1, sp2 - (sp1 + 1))
                : requestLine.mid(sp1 + 1);
            hostPort = hostPort.trimmed();

            int colon = hostPort.indexOf(":");
            if (colon < 0) {
                closeBoth_();
                return;
            }

            const SwString host = hostPort.left(colon);
            const SwString portStr = hostPort.mid(colon + 1);
            bool ok = false;
            const int p = portStr.toInt(&ok);
            if (!ok || p <= 0 || p > 65535) {
                closeBoth_();
                return;
            }

            upstream_ = new SwTcpSocket(this);
            upstream_->useSsl(false);

            connect(upstream_, SIGNAL(connected), this, &HttpConnectTunnel::onUpstreamConnected_);
            connect(upstream_, SIGNAL(readyRead), this, &HttpConnectTunnel::onUpstreamReadyRead_);
            connect(upstream_, SIGNAL(disconnected), this, &HttpConnectTunnel::onUpstreamDisconnected_);
            connect(upstream_, SIGNAL(errorOccurred), this, &HttpConnectTunnel::onUpstreamError_);

            if (!upstream_->connectToHost(host, static_cast<uint16_t>(p))) {
                closeBoth_();
                return;
            }
            return;
        }

        flushClientToUpstream_();
    }

    void onClientDisconnected_() {
        closeBoth_();
    }

    void onUpstreamConnected_() {
        if (!client_ || !upstream_) {
            closeBoth_();
            return;
        }

        SwString response = "HTTP/1.1 200 Connection Established\r\n\r\n";
        client_->write(response);
        tunnelEstablished_ = true;

        flushClientToUpstream_();
        flushUpstreamToClient_();
    }

    void onUpstreamReadyRead_() {
        flushUpstreamToClient_();
    }

    void onUpstreamDisconnected_() {
        closeBoth_();
    }

    void onUpstreamError_(int) {
        closeBoth_();
    }

private:
    void flushClientToUpstream_() {
        if (!tunnelEstablished_ || !client_ || !upstream_) {
            return;
        }

        if (!clientBuffer_.isEmpty()) {
            upstream_->write(SwString(clientBuffer_));
            clientBuffer_.clear();
        }
    }

    void flushUpstreamToClient_() {
        if (!tunnelEstablished_ || !client_ || !upstream_) {
            return;
        }

        while (true) {
            SwString chunk = upstream_->read();
            if (chunk.isEmpty()) {
                break;
            }
            client_->write(chunk);
        }
    }

    void closeBoth_() {
        if (closing_) {
            return;
        }
        closing_ = true;

        if (client_) {
            client_->disconnectAllSlots();
            client_->close();
            client_ = nullptr;
        }
        if (upstream_) {
            upstream_->disconnectAllSlots();
            upstream_->close();
            upstream_ = nullptr;
        }
        deleteLater();
    }

private:
    SwTcpSocket* client_ = nullptr;
    SwTcpSocket* upstream_ = nullptr;
    SwByteArray clientBuffer_;
    bool tunnelEstablished_ = false;
    bool closing_ = false;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    // WebSocket echo server (server-side SwWebSocketServer + SwWebSocket).
    SwWebSocketServer wsServer;
    uint16_t wsPort = 0;
    for (uint16_t p = 19100; p < 19200; ++p) {
        if (wsServer.listen(p)) {
            wsPort = p;
            break;
        }
    }
    if (wsPort == 0) {
        std::cerr << "[WebSocketAdvancedSelfTest] Failed to bind WebSocket server" << std::endl;
        return 1;
    }

    SwObject::connect(&wsServer, SIGNAL(newConnection), [&]() {
        while (SwWebSocket* ws = wsServer.nextPendingConnection()) {
            std::cerr << "[WebSocketAdvancedSelfTest] server accepted connection (pmd=" << (ws->isPerMessageDeflateNegotiated() ? 1 : 0) << ")\n";
            SwObject::connect(ws, SIGNAL(textMessageReceived), [ws](const SwString& msg) {
                ws->sendTextMessage(msg);
            });
            SwObject::connect(ws, SIGNAL(binaryMessageReceived), [ws](const SwByteArray& msg) {
                ws->sendBinaryMessage(msg);
            });
        }
    });

    // Redirect server: answers the initial handshake with HTTP 302 to the echo server.
    SwTcpServer redirectServer;
    uint16_t redirectPort = 0;
    for (uint16_t p = 19200; p < 19300; ++p) {
        if (redirectServer.listen(p)) {
            redirectPort = p;
            break;
        }
    }
    if (redirectPort == 0) {
        std::cerr << "[WebSocketAdvancedSelfTest] Failed to bind redirect server" << std::endl;
        return 1;
    }

    struct RedirectConn {
        SwTcpSocket* socket = nullptr;
        SwByteArray buffer;
    } redirectConn;

    SwObject::connect(&redirectServer, SIGNAL(newConnection), [&]() {
        if (redirectConn.socket) {
            return;
        }
        redirectConn.socket = redirectServer.nextPendingConnection();
        if (!redirectConn.socket) {
            return;
        }
        redirectConn.socket->setParent(&redirectServer);

        SwObject::connect(redirectConn.socket, SIGNAL(readyRead), [&]() {
            while (true) {
                SwString chunk = redirectConn.socket->read();
                if (chunk.isEmpty()) {
                    break;
                }
                redirectConn.buffer.append(chunk.data(), chunk.size());
            }

            const int boundary = redirectConn.buffer.indexOf("\r\n\r\n");
            if (boundary < 0) {
                return;
            }

            const SwString location = wsUrl("127.0.0.1", wsPort, "/");
            SwString response = "HTTP/1.1 302 Found\r\n";
            response += "Location: " + location + "\r\n";
            response += "Content-Length: 0\r\n";
            response += "\r\n";
            redirectConn.socket->write(response);

            SwTcpSocket* sock = redirectConn.socket;
            SwTimer::singleShot(200, [sock]() {
                if (sock) {
                    sock->close();
                }
            });
        });
    });

    // HTTP CONNECT proxy (tunnels to ws:// target).
    SwTcpServer proxyServer;
    uint16_t proxyPort = 0;
    for (uint16_t p = 19300; p < 19400; ++p) {
        if (proxyServer.listen(p)) {
            proxyPort = p;
            break;
        }
    }
    if (proxyPort == 0) {
        std::cerr << "[WebSocketAdvancedSelfTest] Failed to bind proxy server" << std::endl;
        return 1;
    }

    SwObject::connect(&proxyServer, SIGNAL(newConnection), [&]() {
        while (SwTcpSocket* client = proxyServer.nextPendingConnection()) {
            auto* tunnel = new HttpConnectTunnel(client, &proxyServer);
            (void)tunnel;
        }
    });

    SwTimer timeout(15000);
    timeout.setSingleShot(true);
    SwObject::connect(&timeout, SIGNAL(timeout), [&]() {
        std::cerr << "[WebSocketAdvancedSelfTest] Timeout" << std::endl;
        app.exit(1);
    });
    timeout.start();

    int testIndex = 0;
    SwTimer nextTest(0);
    nextTest.setSingleShot(true);

    auto runTest = [&]() {
        SwWebSocket* ws = new SwWebSocket();

        const SwString testName = (testIndex == 0) ? "permessage-deflate"
                                 : (testIndex == 1) ? "redirect"
                                 : (testIndex == 2) ? "proxy"
                                                    : "done";

        if (testName == "done") {
            app.exit(0);
            return;
        }

        const SwString url = (testIndex == 0)
            ? wsUrl("127.0.0.1", wsPort, "/")
            : (testIndex == 1)
                ? wsUrl("127.0.0.1", redirectPort, "/")
                : wsUrl("127.0.0.1", wsPort, "/");

        if (testIndex == 2) {
            SwWebSocket::ProxySettings proxy;
            proxy.type = SwWebSocket::HttpProxy;
            proxy.host = "127.0.0.1";
            proxy.port = proxyPort;
            ws->setProxy(proxy);
        }

        const SwString expected = "hello-" + testName;
        std::cerr << "[WebSocketAdvancedSelfTest] start " << testName.toStdString()
                  << " url=" << url.toStdString() << "\n";

        SwObject::connect(ws, SIGNAL(errorOccurred), [&, ws, testName](int err) {
            std::cerr << "[WebSocketAdvancedSelfTest] " << testName.toStdString()
                      << " errorOccurred: " << err << std::endl;
            ws->deleteLater();
            app.exit(1);
        });

        SwObject::connect(ws, SIGNAL(connected), [&, ws, testName, expected]() {
            std::cerr << "[WebSocketAdvancedSelfTest] " << testName.toStdString()
                      << " connected (pmd=" << (ws->isPerMessageDeflateNegotiated() ? 1 : 0)
                      << " ext=\"" << ws->extensions().toStdString() << "\")\n";
            if (testName == "permessage-deflate") {
                if (!ws->isPerMessageDeflateNegotiated() ||
                    !ws->extensions().toLower().contains("permessage-deflate", Sw::CaseInsensitive)) {
                    std::cerr << "[WebSocketAdvancedSelfTest] permessage-deflate not negotiated" << std::endl;
                    ws->deleteLater();
                    app.exit(1);
                    return;
                }
            }
            ws->sendTextMessage(expected);
        });

        SwObject::connect(ws, SIGNAL(textMessageReceived), [&, ws, testName, expected](const SwString& msg) {
            std::cerr << "[WebSocketAdvancedSelfTest] " << testName.toStdString()
                      << " got echo\n";
            if (msg != expected) {
                std::cerr << "[WebSocketAdvancedSelfTest] " << testName.toStdString()
                          << " unexpected echo: " << msg.toStdString() << std::endl;
                ws->deleteLater();
                app.exit(1);
                return;
            }
            ws->close();
        });

        SwObject::connect(ws, SIGNAL(disconnected), [&, ws, testName]() {
            std::cerr << "[WebSocketAdvancedSelfTest] " << testName.toStdString()
                      << " disconnected code=" << ws->closeCode() << "\n";
            if (ws->closeCode() != 1000) {
                std::cerr << "[WebSocketAdvancedSelfTest] " << testName.toStdString()
                          << " close code mismatch: " << ws->closeCode() << std::endl;
                ws->deleteLater();
                app.exit(1);
                return;
            }
            ws->deleteLater();
            ++testIndex;
            nextTest.start(0);
        });

        ws->open(url);
    };

    SwObject::connect(&nextTest, SIGNAL(timeout), [&]() { nextTest.stop(); runTest(); });
    nextTest.start(0);
    return app.exec();
}
