#include "SwByteArray.h"
#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwIODevice.h"
#include "SwSslServer.h"
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTimer.h"

static constexpr uint16_t kPort = 9444;
static constexpr std::size_t kRequestSize = 256 * 1024;
static constexpr std::size_t kResponseSize = 1024 * 1024;

static SwByteArray buildPayload_(std::size_t size, int seed) {
    SwByteArray payload;
    payload.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        payload.data()[i] = static_cast<char>((i + static_cast<std::size_t>(seed)) % 251);
    }
    return payload;
}

static bool verifyPayload_(const SwByteArray& payload, int seed) {
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (payload.constData()[i] != static_cast<char>((i + static_cast<std::size_t>(seed)) % 251)) {
            return false;
        }
    }
    return true;
}

class ServerPeer : public SwObject {
    SW_OBJECT(ServerPeer, SwObject)

public:
    explicit ServerPeer(SwSslSocket* socket, SwObject* parent = nullptr)
        : SwObject(parent),
          m_socket(socket),
          m_response(buildPayload_(kResponseSize, 19)) {
        m_socket->setParent(this);
        connect(m_socket, &SwTcpSocket::readyRead, this, &ServerPeer::onReadyRead_);
        connect(m_socket, &SwTcpSocket::writeFinished, this, &ServerPeer::onWriteFinished_);
        connect(m_socket, &SwTcpSocket::disconnected, this, &ServerPeer::onDisconnected_);
        connect(m_socket, &SwTcpSocket::errorOccurred, this, &ServerPeer::onError_);

        onReadyRead_();
    }

private slots:
    void onReadyRead_() {
        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_request.append(chunk.data(), chunk.size());
        }

        if (m_replied || m_request.size() < kRequestSize) {
            return;
        }

        if (m_request.size() != kRequestSize || !verifyPayload_(m_request, 7)) {
            swError() << "[SslSocketSelfTest][Server] request verification failed size=" << m_request.size();
            m_socket->close();
            return;
        }

        m_replied = true;
        if (!m_socket->write(SwString(m_response))) {
            swError() << "[SslSocketSelfTest][Server] write failed";
            m_socket->close();
        }
    }

    void onWriteFinished_() {
        SW_UNUSED(m_replied)
    }

    void onDisconnected_() {
        deleteLater();
    }

    void onError_(int err) {
        swError() << "[SslSocketSelfTest][Server] socket error=" << err;
        m_socket->close();
    }

private:
    SwSslSocket* m_socket = nullptr;
    SwByteArray m_request;
    SwByteArray m_response;
    bool m_replied = false;
};

class ClientRunner : public SwObject {
    SW_OBJECT(ClientRunner, SwObject)

public:
    explicit ClientRunner(SwCoreApplication* app, const SwString& caFilePath, SwObject* parent = nullptr)
        : SwObject(parent),
          m_app(app),
          m_caFilePath(caFilePath),
          m_request(buildPayload_(kRequestSize, 7)),
          m_expectedResponse(buildPayload_(kResponseSize, 19)),
          m_timeout(30000, this) {
        connect(&m_timeout, &SwTimer::timeout, this, &ClientRunner::onTimeout_);
    }

    void start() {
        m_socket = new SwSslSocket(this);
        m_socket->setPeerHostName("localhost");
        m_socket->setTrustedCaFile(m_caFilePath);
        connect(m_socket, &SwSslSocket::encrypted, this, &ClientRunner::onConnected_);
        connect(m_socket, &SwTcpSocket::readyRead, this, &ClientRunner::onReadyRead_);
        connect(m_socket, &SwTcpSocket::disconnected, this, &ClientRunner::onDisconnected_);
        connect(m_socket, &SwTcpSocket::errorOccurred, this, &ClientRunner::onError_);
        connect(m_socket, &SwSslSocket::sslErrors, this, &ClientRunner::onSslErrors_);
        m_timeout.setSingleShot(true);
        m_timeout.start(30000);
        if (!m_socket->connectToHostEncrypted("127.0.0.1", kPort)) {
            finish_(false);
        }
    }

private slots:
    void onConnected_() {
        if (!m_socket->write(SwString(m_request))) {
            swError() << "[SslSocketSelfTest][Client] initial write failed";
            finish_(false);
        }
    }

    void onReadyRead_() {
        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_response.append(chunk.data(), chunk.size());
        }

        if (m_response.size() < kResponseSize) {
            return;
        }
        if (m_response.size() != kResponseSize || m_response != m_expectedResponse) {
            swError() << "[SslSocketSelfTest][Client] response verification failed size=" << m_response.size();
            finish_(false);
            return;
        }
        swDebug() << "[SslSocketSelfTest] PASS request=" << kRequestSize << " response=" << kResponseSize;
        finish_(true);
    }

    void onDisconnected_() {
        if (m_response.size() != kResponseSize || m_response != m_expectedResponse) {
            swError() << "[SslSocketSelfTest][Client] response verification failed size=" << m_response.size();
            finish_(false);
            return;
        }
        swDebug() << "[SslSocketSelfTest] PASS request=" << kRequestSize << " response=" << kResponseSize;
        finish_(true);
    }

    void onTimeout_() {
        swError() << "[SslSocketSelfTest][Client] timeout";
        finish_(false);
    }

    void onError_(int err) {
        swError() << "[SslSocketSelfTest][Client] socket error=" << err;
    }

    void onSslErrors_(const SwSslErrorList& errors) {
        if (!errors.isEmpty()) {
            swError() << "[SslSocketSelfTest][Client] TLS error=" << errors.first();
        }
    }

private:
    void finish_(bool success) {
        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_timeout.stop();
        m_app->exit(success ? 0 : 1);
    }

    SwCoreApplication* m_app = nullptr;
    SwString m_caFilePath;
    SwSslSocket* m_socket = nullptr;
    SwByteArray m_request;
    SwByteArray m_expectedResponse;
    SwByteArray m_response;
    SwTimer m_timeout;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    const SwString certPath = "exemples/test-assets/ssl/localhost_cert.pem";
    const SwString keyPath = "exemples/test-assets/ssl/localhost_key.pem";
    if (!SwIODevice::exists(certPath) || !SwIODevice::exists(keyPath)) {
        swError() << "[SslSocketSelfTest] Missing TLS test assets";
        return 1;
    }

    SwSslServer server;
    if (!server.setLocalCredentials(certPath, keyPath) || !server.listen(kPort)) {
        swError() << "[SslSocketSelfTest] Failed to start TLS server";
        return 1;
    }

    SwObject::connect(&server, &SwTcpServer::newConnection, [&server]() {
        while (SwSslSocket* socket = server.nextPendingConnection()) {
            new ServerPeer(socket, &server);
        }
    });

    ClientRunner client(&app, certPath);
    SwTimer::singleShot(200, &client, &ClientRunner::start);
    return app.exec();
}
