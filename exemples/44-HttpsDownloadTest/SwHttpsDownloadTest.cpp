#include "SwByteArray.h"
#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwHttpApp.h"
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTimer.h"
#include "http/SwHttpContext.h"

#include <cstring>

static constexpr uint16_t kPort = 9443;

static SwList<std::size_t> payloadSizes_() {
    SwList<std::size_t> sizes;
    sizes.append(100 * 1024);
    sizes.append(256 * 1024);
    sizes.append(1024 * 1024);
    return sizes;
}

static SwByteArray buildPayload_(std::size_t size) {
    SwByteArray payload;
    payload.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        payload.data()[i] = static_cast<char>(i % 251);
    }
    return payload;
}

static bool verifyPayload_(const SwByteArray& body, std::size_t expectedSize) {
    if (body.size() != expectedSize) {
        return false;
    }
    for (std::size_t i = 0; i < expectedSize; ++i) {
        if (body.constData()[i] != static_cast<char>(i % 251)) {
            return false;
        }
    }
    return true;
}

class DownloadClient : public SwObject {
    SW_OBJECT(DownloadClient, SwObject)

public:
    DownloadClient(SwCoreApplication* app, const SwString& caFilePath, SwObject* parent = nullptr)
        : SwObject(parent),
          m_app(app),
          m_caFilePath(caFilePath),
          m_timeout(30000, this),
          m_sizes(payloadSizes_()) {
        connect(&m_timeout, &SwTimer::timeout, this, &DownloadClient::onTimeout_);
    }

    void start() {
        startNextCase_();
    }

private slots:
    void onConnected_() {
        if (!m_socket) {
            finish_(false);
            return;
        }
        SwString request =
            "GET /big?size=" + SwString::number(static_cast<long long>(m_expectedSize)) +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";
        if (!m_socket->write(request)) {
            swError() << "[DownloadClient] FAIL: unable to write request for size=" << m_expectedSize;
            finish_(false);
        }
    }

    void onReadyRead_() {
        if (!m_socket) {
            return;
        }
        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_responseRaw.append(chunk.data(), chunk.size());
        }

        const int boundary = m_responseRaw.indexOf("\r\n\r\n");
        if (boundary < 0) {
            return;
        }

        const SwByteArray body = m_responseRaw.mid(boundary + 4);
        if (body.size() < m_expectedSize) {
            return;
        }
        const int expectedBodyBytes = static_cast<int>(m_expectedSize);
        if (!verifyPayload_(body.left(expectedBodyBytes), m_expectedSize)) {
            swError() << "[DownloadClient] FAIL: payload mismatch for size=" << m_expectedSize
                      << " got=" << body.size();
            finish_(false);
            return;
        }

        swDebug() << "[DownloadClient] PASS size=" << m_expectedSize;
        cleanupSocket_();
        ++m_caseIndex;
        startNextCase_();
    }

    void onDisconnected_() {
        m_timeout.stop();

        const int boundary = m_responseRaw.indexOf("\r\n\r\n");
        if (boundary < 0) {
            swError() << "[DownloadClient] FAIL: no HTTP header boundary for size=" << m_expectedSize
                      << " raw=" << m_responseRaw.size();
            finish_(false);
            return;
        }

        SwByteArray headers = m_responseRaw.left(boundary);
        SwByteArray body = m_responseRaw.mid(boundary + 4);
        SwString headerText(headers.toStdString());

        swDebug() << "[DownloadClient] case size=" << m_expectedSize << " headers:\n" << headerText;

        if (!verifyPayload_(body, m_expectedSize)) {
            swError() << "[DownloadClient] FAIL: payload mismatch for size=" << m_expectedSize
                      << " got=" << body.size();
            finish_(false);
            return;
        }

        swDebug() << "[DownloadClient] PASS size=" << m_expectedSize;
        cleanupSocket_();
        ++m_caseIndex;
        startNextCase_();
    }

    void onTimeout_() {
        swError() << "[DownloadClient] FAIL: timeout for size=" << m_expectedSize
                  << " raw=" << m_responseRaw.size();
        finish_(false);
    }

private:
    void startNextCase_() {
        if (m_caseIndex >= m_sizes.size()) {
            swDebug() << "[DownloadClient] PASS all HTTPS download cases";
            finish_(true);
            return;
        }

        cleanupSocket_();
        m_responseRaw.clear();
        m_expectedSize = m_sizes[m_caseIndex];

        m_socket = new SwSslSocket(this);
        m_socket->setPeerHostName("localhost");
        m_socket->setTrustedCaFile(m_caFilePath);
        connect(m_socket, &SwSslSocket::encrypted, this, &DownloadClient::onConnected_);
        connect(m_socket, &SwTcpSocket::readyRead, this, &DownloadClient::onReadyRead_);
        connect(m_socket, &SwTcpSocket::disconnected, this, &DownloadClient::onDisconnected_);
        connect(m_socket, &SwTcpSocket::errorOccurred, this, [this](int err) {
            swError() << "[DownloadClient] socket error=" << err << " case size=" << m_expectedSize
                      << " raw=" << m_responseRaw.size();
        });
        connect(m_socket, &SwSslSocket::sslErrors, this, [this](const SwSslErrorList& errors) {
            if (!errors.isEmpty()) {
                swError() << "[DownloadClient] TLS error case size=" << m_expectedSize << " message=" << errors.first();
            }
        });

        m_timeout.setSingleShot(true);
        m_timeout.start(30000);

        swDebug() << "[DownloadClient] starting case size=" << m_expectedSize;
        if (!m_socket->connectToHostEncrypted("127.0.0.1", kPort)) {
            swError() << "[DownloadClient] FAIL: connectToHostEncrypted for size=" << m_expectedSize;
            finish_(false);
        }
    }

    void cleanupSocket_() {
        if (!m_socket) {
            return;
        }
        m_socket->disconnectAllSlots();
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    void finish_(bool success) {
        cleanupSocket_();
        m_timeout.stop();
        m_app->exit(success ? 0 : 1);
    }

    SwCoreApplication* m_app = nullptr;
    SwString m_caFilePath;
    SwSslSocket* m_socket = nullptr;
    SwByteArray m_responseRaw;
    SwTimer m_timeout;
    SwList<std::size_t> m_sizes;
    std::size_t m_caseIndex = 0;
    std::size_t m_expectedSize = 0;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    const SwString certPath = "exemples/test-assets/ssl/localhost_cert.pem";
    const SwString keyPath = "exemples/test-assets/ssl/localhost_key.pem";
    if (!SwIODevice::exists(certPath) || !SwIODevice::exists(keyPath)) {
        swError() << "[HttpsDownloadTest] Missing TLS test assets";
        return 1;
    }

    SwHttpApp httpApp;
    httpApp.get("/big", [](SwHttpContext& ctx) {
        const SwString sizeText = ctx.request().queryParams.value("size").trimmed();
        bool ok = false;
        const int size = sizeText.toInt(&ok);
        if (!ok || size <= 0) {
            ctx.text("invalid size", 400);
            return;
        }
        ctx.send(buildPayload_(static_cast<std::size_t>(size)), "application/octet-stream", 200);
    });

    if (!httpApp.listen(kPort, certPath, keyPath)) {
        swError() << "[HttpsDownloadTest] Failed to start HTTPS server";
        return 1;
    }
    DownloadClient client(&app, certPath);
    SwTimer::singleShot(300, &client, &DownloadClient::start);
    return app.exec();
}
