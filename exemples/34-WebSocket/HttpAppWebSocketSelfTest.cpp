#include "SwCoreApplication.h"
#include "platform/SwPlatformSelector.h"
#include "SwHttpApp.h"
#include "SwTimer.h"
#include "SwDebug.h"

class HttpAppWebSocketSelfTestRunner : public SwObject {
    SW_OBJECT(HttpAppWebSocketSelfTestRunner, SwObject)

public:
    explicit HttpAppWebSocketSelfTestRunner(SwCoreApplication* app, SwObject* parent = nullptr)
        : SwObject(parent),
          m_app(app),
          m_timeout(12000, this) {
        SwObject::connect(&m_timeout, &SwTimer::timeout, [this]() {
            fail_("timeout");
        });
    }

    void start() {
        runNextCase_();
    }

private:
    struct TestCase {
        SwString name;
        SwString room;
        SwString greeting;
        SwString url;
        uint16_t port = 0;
        bool secure = false;
    };

    SwCoreApplication* m_app = nullptr;
    SwHttpApp* m_httpApp = nullptr;
    SwWebSocket* m_client = nullptr;
    SwTimer m_timeout;

    int m_caseIndex = 0;
    bool m_greetingReceived = false;
    bool m_echoReceived = false;
    bool m_serverPerMessageDeflate = false;
    TestCase m_activeCase;

private:
    void runNextCase_() {
        resetCaseState_();

        if (m_caseIndex == 0) {
            TestCase plain;
            plain.name = "ws";
            plain.room = "alpha";
            plain.greeting = "ready:alpha";
            plain.url = "ws://127.0.0.1:19534/ws/alpha";
            plain.port = 19534;
            plain.secure = false;
            startCase_(plain);
            return;
        }

        const SwString certPath = "exemples/test-assets/ssl/localhost_cert.pem";
        const SwString keyPath = "exemples/test-assets/ssl/localhost_key.pem";
        if (m_caseIndex == 1 && swFilePlatform().isFile(certPath) && swFilePlatform().isFile(keyPath)) {
            TestCase secure;
            secure.name = "wss";
            secure.room = "secure";
            secure.greeting = "ready:secure";
            secure.url = "wss://localhost:19535/ws/secure";
            secure.port = 19535;
            secure.secure = true;
            startCase_(secure);
            return;
        }

        swDebug() << "[HttpAppWebSocketSelfTest] all cases passed";
        m_app->quit();
    }

    void startCase_(const TestCase& testCase) {
        m_activeCase = testCase;
        setupServer_(testCase);
        setupClient_(testCase);
        m_timeout.start();
    }

    void setupServer_(const TestCase& testCase) {
        if (m_httpApp) {
            m_httpApp->close();
            delete m_httpApp;
            m_httpApp = nullptr;
        }

        m_httpApp = new SwHttpApp(this);
        SwHttpApp::SwHttpWebSocketRouteOptions wsOptions;
        wsOptions.supportedSubprotocols = SwList<SwString>{ SwString("json"), SwString("chat") };
        wsOptions.enablePerMessageDeflate = true;

        m_httpApp->ws("/ws/:room", [this, testCase](SwWebSocket* ws, const SwHttpRequest& request) {
            if (!ws) {
                fail_("null websocket");
                return;
            }
            if (request.pathParams.value("room") != testCase.room) {
                fail_("path param mismatch");
                return;
            }
            if (request.isTls != testCase.secure) {
                fail_("tls flag mismatch");
                return;
            }
            if (ws->subprotocol() != "chat") {
                fail_("subprotocol negotiation mismatch");
                return;
            }

            m_serverPerMessageDeflate = ws->isPerMessageDeflateNegotiated();
            ws->sendTextMessage(testCase.greeting);

            SwObject::connect(ws, &SwWebSocket::textMessageReceived, [this, ws](const SwString& message) {
                ws->sendTextMessage("echo:" + message);
            });
        }, wsOptions);

        bool listenOk = false;
        if (testCase.secure) {
            const SwString certPath = "exemples/test-assets/ssl/localhost_cert.pem";
            const SwString keyPath = "exemples/test-assets/ssl/localhost_key.pem";
            listenOk = m_httpApp->listenHttps(testCase.port, certPath, keyPath);
        } else {
            listenOk = m_httpApp->listenHttp(testCase.port);
        }

        if (!listenOk) {
            fail_("server listen failed");
            return;
        }
    }

    void setupClient_(const TestCase& testCase) {
        if (m_client) {
            m_client->abort();
            m_client->deleteLater();
        }

        m_client = new SwWebSocket(this);
        m_client->setRequestedSubprotocols(SwList<SwString>{ SwString("chat"), SwString("binary") });
        if (testCase.secure) {
            m_client->setTrustedCaFile("exemples/test-assets/ssl/localhost_cert.pem");
        }

        SwObject::connect(m_client, &SwWebSocket::errorOccurred, [this](int err) {
            fail_(SwString("client error ") + SwString::number(err));
        });
        SwObject::connect(m_client, &SwWebSocket::connected, [this]() {
            if (!m_client || m_client->subprotocol() != "chat") {
                fail_("client subprotocol mismatch");
                return;
            }
            if (!m_client->isPerMessageDeflateNegotiated()) {
                fail_("client permessage-deflate not negotiated");
            }
        });
        SwObject::connect(m_client, &SwWebSocket::textMessageReceived, [this](const SwString& message) {
            if (!m_greetingReceived) {
                if (message != m_activeCase.greeting) {
                    fail_("unexpected greeting");
                    return;
                }
                if (!m_serverPerMessageDeflate) {
                    fail_("server permessage-deflate not negotiated");
                    return;
                }
                m_greetingReceived = true;
                m_client->sendTextMessage("hello");
                return;
            }

            if (!m_echoReceived) {
                if (message != "echo:hello") {
                    fail_("unexpected echo");
                    return;
                }
                m_echoReceived = true;
                m_client->close();
                return;
            }

            fail_("unexpected extra websocket message");
        });
        SwObject::connect(m_client, &SwWebSocket::disconnected, [this]() {
            if (!m_echoReceived) {
                fail_("disconnected before echo");
                return;
            }

            m_timeout.stop();
            ++m_caseIndex;
            SwTimer::singleShot(10, [this]() {
                runNextCase_();
            });
        });

        m_client->open(testCase.url);
    }

    void resetCaseState_() {
        m_greetingReceived = false;
        m_echoReceived = false;
        m_serverPerMessageDeflate = false;
    }

    void fail_(const SwString& reason) {
        m_timeout.stop();
        swError() << "[HttpAppWebSocketSelfTest] Case " << m_activeCase.name << " failed: " << reason;
        m_app->exit(1);
    }
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    HttpAppWebSocketSelfTestRunner runner(&app);
    SwTimer::singleShot(0, [&runner]() {
        runner.start();
    });

    return app.exec();
}
