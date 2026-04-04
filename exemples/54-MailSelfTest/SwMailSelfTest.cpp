#include "SwCoreApplication.h"
#include "SwCrypto.h"
#include "SwDebug.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwHttpApp.h"
#include "SwHttpClient.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwMailService.h"
#include "SwSslSocket.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"

#if defined(__has_include)
#if __has_include("SwMailSelfTestConfig.h")
#include "SwMailSelfTestConfig.h"
#endif
#endif

#ifndef SW_MAIL_SELFTEST_RUNTIME_DIR
#define SW_MAIL_SELFTEST_RUNTIME_DIR "exemples/54-MailSelfTest/runtime"
#endif

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace {

class MailSelfTestApplication_ : public SwCoreApplication {
public:
    using SwCoreApplication::SwCoreApplication;

    void pumpOnce(int maxWaitUs = 10 * 1000) {
        int sleepUs = processEvent(false);
        if (sleepUs != 0) {
            if (sleepUs < 0 || sleepUs > maxWaitUs) {
                sleepUs = maxWaitUs;
            }
            waitForWork(sleepUs);
        }
    }
};

static void pumpRuntimeOnce_(SwCoreApplication& app, int maxWaitUs = 10 * 1000) {
    static_cast<MailSelfTestApplication_&>(app).pumpOnce(maxWaitUs);
}

static uint16_t findFreePort_(uint16_t startPort, uint16_t endPort) {
    for (uint16_t port = startPort; port < endPort; ++port) {
        SwTcpServer probe;
        if (probe.listen(port)) {
            probe.close();
            return port;
        }
    }
    return 0;
}

static bool waitUntil_(SwCoreApplication& app, int timeoutMs, const std::function<bool()>& predicate) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        pumpRuntimeOnce_(app);
    }
    pumpRuntimeOnce_(app);
    return predicate();
}

static bool writeTextFile_(const SwString& path, const SwString& text) {
    SwFile file(path);
    if (!file.openBinary(SwFile::Write)) {
        return false;
    }
    const bool ok = file.write(SwByteArray(text.toStdString()));
    file.close();
    return ok;
}

static bool parseJsonDocument_(const SwByteArray& body, SwJsonDocument& outDocument) {
    SwString error;
    outDocument = SwJsonDocument::fromJson(body.toStdString(), error);
    return error.isEmpty();
}

struct ParsedHttpResponse_ {
    bool valid = false;
    int statusCode = 0;
    SwMap<SwString, SwString> headers;
    SwByteArray body;
};

static bool parseHttpResponse_(const SwByteArray& raw, ParsedHttpResponse_& parsed) {
    parsed = ParsedHttpResponse_();
    const int headerBoundary = raw.indexOf("\r\n\r\n");
    if (headerBoundary < 0) {
        return false;
    }

    const SwByteArray headersPart = raw.left(headerBoundary);
    parsed.body = raw.mid(headerBoundary + 4);

    const SwString headerText(headersPart.toStdString());
    const SwList<SwString> lines = headerText.split("\r\n");
    if (lines.isEmpty()) {
        return false;
    }

    const SwList<SwString> statusTokens = lines[0].split(' ');
    if (statusTokens.size() < 2) {
        return false;
    }

    bool ok = false;
    parsed.statusCode = statusTokens[1].toInt(&ok);
    if (!ok) {
        return false;
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const SwString line = lines[i];
        const int colon = line.indexOf(":");
        if (colon < 0) {
            continue;
        }
        parsed.headers[line.left(colon).trimmed().toLower()] = line.mid(colon + 1).trimmed();
    }

    parsed.valid = true;
    return true;
}

static bool generateEcPrivateKeyPem_(SwString& outPem) {
    outPem.clear();
    EVP_PKEY* pkey = EVP_PKEY_new();
    EC_KEY* ecKey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIO* bio = nullptr;
    if (!pkey || !ecKey || EC_KEY_generate_key(ecKey) != 1 || EVP_PKEY_assign_EC_KEY(pkey, ecKey) != 1) {
        if (pkey) EVP_PKEY_free(pkey);
        if (ecKey) EC_KEY_free(ecKey);
        return false;
    }
    ecKey = nullptr;

    bio = BIO_new(BIO_s_mem());
    if (!bio || PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        if (bio) BIO_free(bio);
        EVP_PKEY_free(pkey);
        return false;
    }

    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    if (!mem || !mem->data || mem->length == 0) {
        BIO_free(bio);
        EVP_PKEY_free(pkey);
        return false;
    }

    outPem = SwString(mem->data, static_cast<size_t>(mem->length));
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return true;
}

static bool buildServerCsrDer_(const SwString& privateKeyPem,
                               const SwList<SwString>& domains,
                               SwByteArray& outDer) {
    outDer.clear();
    if (domains.isEmpty()) {
        return false;
    }

    const SwString commonName = swMailDetail::normalizeDomain(domains.first());
    EVP_PKEY* pkey = nullptr;
    BIO* keyBio = BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size()));
    X509_REQ* req = nullptr;
    X509_NAME* name = nullptr;
    STACK_OF(X509_EXTENSION)* extensions = nullptr;
    X509_EXTENSION* sanExtension = nullptr;

    if (!keyBio) {
        return false;
    }
    pkey = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
    BIO_free(keyBio);
    if (!pkey) {
        return false;
    }

    req = X509_REQ_new();
    if (!req || X509_REQ_set_version(req, 0L) != 1 || X509_REQ_set_pubkey(req, pkey) != 1) {
        if (req) X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }

    name = X509_NAME_new();
    if (!name ||
        X509_NAME_add_entry_by_txt(name,
                                   "CN",
                                   MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>(commonName.data()),
                                   static_cast<int>(commonName.size()),
                                   -1,
                                   0) != 1 ||
        X509_REQ_set_subject_name(req, name) != 1) {
        if (name) X509_NAME_free(name);
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }
    X509_NAME_free(name);

    SwString sanValue;
    for (std::size_t i = 0; i < domains.size(); ++i) {
        const SwString domain = swMailDetail::normalizeDomain(domains[i]);
        if (domain.isEmpty()) {
            continue;
        }
        if (!sanValue.isEmpty()) {
            sanValue += ",";
        }
        sanValue += "DNS:" + domain;
    }

    extensions = sk_X509_EXTENSION_new_null();
    sanExtension = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, sanValue.data());
    if (!extensions || !sanExtension || sk_X509_EXTENSION_push(extensions, sanExtension) != 1 ||
        X509_REQ_add_extensions(req, extensions) != 1 || X509_REQ_sign(req, pkey, EVP_sha256()) <= 0) {
        if (sanExtension) X509_EXTENSION_free(sanExtension);
        if (extensions) sk_X509_EXTENSION_pop_free(extensions, X509_EXTENSION_free);
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }
    sanExtension = nullptr;
    sk_X509_EXTENSION_pop_free(extensions, X509_EXTENSION_free);

    const int derLength = i2d_X509_REQ(req, nullptr);
    if (derLength <= 0) {
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }

    SwByteArray der(static_cast<std::size_t>(derLength), '\0');
    unsigned char* cursor = reinterpret_cast<unsigned char*>(der.data());
    if (i2d_X509_REQ(req, &cursor) != derLength) {
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }

    outDer = der;
    X509_REQ_free(req);
    EVP_PKEY_free(pkey);
    return true;
}

static bool generateRootCa_(const SwString& commonName, SwString& certPemOut, SwString& keyPemOut) {
    certPemOut.clear();
    keyPemOut.clear();

    if (!generateEcPrivateKeyPem_(keyPemOut)) {
        return false;
    }

    BIO* keyBio = BIO_new_mem_buf(keyPemOut.data(), static_cast<int>(keyPemOut.size()));
    EVP_PKEY* pkey = keyBio ? PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr) : nullptr;
    X509* cert = nullptr;
    BIO* certBio = nullptr;
    if (keyBio) BIO_free(keyBio);
    if (!pkey) {
        return false;
    }

    cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        return false;
    }

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 7 * 24 * 60 * 60);
    X509_set_pubkey(cert, pkey);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name,
                               "CN",
                               MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(commonName.data()),
                               static_cast<int>(commonName.size()),
                               -1,
                               0);
    X509_set_issuer_name(cert, name);

    X509_EXTENSION* basicConstraints = X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, "critical,CA:TRUE");
    X509_EXTENSION* keyUsage = X509V3_EXT_conf_nid(nullptr, nullptr, NID_key_usage, "critical,keyCertSign,cRLSign");
    if (!basicConstraints || !keyUsage || X509_add_ext(cert, basicConstraints, -1) != 1 ||
        X509_add_ext(cert, keyUsage, -1) != 1 || X509_sign(cert, pkey, EVP_sha256()) <= 0) {
        if (basicConstraints) X509_EXTENSION_free(basicConstraints);
        if (keyUsage) X509_EXTENSION_free(keyUsage);
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }
    X509_EXTENSION_free(basicConstraints);
    X509_EXTENSION_free(keyUsage);

    certBio = BIO_new(BIO_s_mem());
    if (!certBio || PEM_write_bio_X509(certBio, cert) != 1) {
        if (certBio) BIO_free(certBio);
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(certBio, &mem);
    certPemOut = SwString(mem->data, static_cast<size_t>(mem->length));

    BIO_free(certBio);
    X509_free(cert);
    EVP_PKEY_free(pkey);
    return !certPemOut.isEmpty();
}

static bool signCertificateFromCsrDer_(const SwByteArray& csrDer,
                                       const SwString& caCertPem,
                                       const SwString& caKeyPem,
                                       long long serialNumber,
                                       SwString& certificatePemOut) {
    certificatePemOut.clear();

    const unsigned char* csrPtr = reinterpret_cast<const unsigned char*>(csrDer.constData());
    X509_REQ* req = d2i_X509_REQ(nullptr, &csrPtr, static_cast<long>(csrDer.size()));
    BIO* caCertBio = BIO_new_mem_buf(caCertPem.data(), static_cast<int>(caCertPem.size()));
    BIO* caKeyBio = BIO_new_mem_buf(caKeyPem.data(), static_cast<int>(caKeyPem.size()));
    X509* caCert = caCertBio ? PEM_read_bio_X509(caCertBio, nullptr, nullptr, nullptr) : nullptr;
    EVP_PKEY* caKey = caKeyBio ? PEM_read_bio_PrivateKey(caKeyBio, nullptr, nullptr, nullptr) : nullptr;
    X509* cert = nullptr;
    BIO* outBio = nullptr;

    if (caCertBio) BIO_free(caCertBio);
    if (caKeyBio) BIO_free(caKeyBio);
    if (!req || !caCert || !caKey) {
        if (req) X509_REQ_free(req);
        if (caCert) X509_free(caCert);
        if (caKey) EVP_PKEY_free(caKey);
        return false;
    }

    cert = X509_new();
    if (!cert) {
        X509_REQ_free(req);
        X509_free(caCert);
        EVP_PKEY_free(caKey);
        return false;
    }

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), static_cast<long>(serialNumber));
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 2 * 24 * 60 * 60);
    X509_set_issuer_name(cert, X509_get_subject_name(caCert));
    X509_set_subject_name(cert, X509_REQ_get_subject_name(req));

    EVP_PKEY* reqKey = X509_REQ_get_pubkey(req);
    if (!reqKey || X509_set_pubkey(cert, reqKey) != 1) {
        if (reqKey) EVP_PKEY_free(reqKey);
        X509_free(cert);
        X509_REQ_free(req);
        X509_free(caCert);
        EVP_PKEY_free(caKey);
        return false;
    }
    EVP_PKEY_free(reqKey);

    STACK_OF(X509_EXTENSION)* extensions = X509_REQ_get_extensions(req);
    if (extensions) {
        for (int i = 0; i < sk_X509_EXTENSION_num(extensions); ++i) {
            X509_EXTENSION* ext = sk_X509_EXTENSION_value(extensions, i);
            if (ext) {
                X509_add_ext(cert, ext, -1);
            }
        }
        sk_X509_EXTENSION_pop_free(extensions, X509_EXTENSION_free);
    }

    X509_EXTENSION* basicConstraints = X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, "CA:FALSE");
    X509_EXTENSION* keyUsage = X509V3_EXT_conf_nid(nullptr, nullptr, NID_key_usage, "digitalSignature,keyEncipherment");
    if (!basicConstraints || !keyUsage || X509_add_ext(cert, basicConstraints, -1) != 1 ||
        X509_add_ext(cert, keyUsage, -1) != 1 || X509_sign(cert, caKey, EVP_sha256()) <= 0) {
        if (basicConstraints) X509_EXTENSION_free(basicConstraints);
        if (keyUsage) X509_EXTENSION_free(keyUsage);
        X509_free(cert);
        X509_REQ_free(req);
        X509_free(caCert);
        EVP_PKEY_free(caKey);
        return false;
    }
    X509_EXTENSION_free(basicConstraints);
    X509_EXTENSION_free(keyUsage);

    outBio = BIO_new(BIO_s_mem());
    if (!outBio || PEM_write_bio_X509(outBio, cert) != 1 || PEM_write_bio_X509(outBio, caCert) != 1) {
        if (outBio) BIO_free(outBio);
        X509_free(cert);
        X509_REQ_free(req);
        X509_free(caCert);
        EVP_PKEY_free(caKey);
        return false;
    }

    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(outBio, &mem);
    certificatePemOut = SwString(mem->data, static_cast<size_t>(mem->length));

    BIO_free(outBio);
    X509_free(cert);
    X509_REQ_free(req);
    X509_free(caCert);
    EVP_PKEY_free(caKey);
    return !certificatePemOut.isEmpty();
}

struct HttpRequestResult_ {
    bool completed = false;
    bool transportOk = false;
    int errorCode = 0;
    int statusCode = 0;
    SwMap<SwString, SwString> headers;
    SwByteArray body;
};

static bool performHttpRequest_(SwCoreApplication& app,
                                SwHttpClient::Method method,
                                const SwString& url,
                                const SwByteArray& body,
                                const SwString& contentType,
                                const SwMap<SwString, SwString>& extraHeaders,
                                HttpRequestResult_& outResult) {
    outResult = HttpRequestResult_();
    std::cout << "[MailSelfTest] HTTP request " << url.toStdString() << std::endl;
    SwHttpClient client;
    for (SwMap<SwString, SwString>::const_iterator it = extraHeaders.begin(); it != extraHeaders.end(); ++it) {
        client.setRawHeader(it.key(), it.value());
    }

    SwObject::connect(&client, &SwHttpClient::finished, [&](const SwByteArray&) {
        outResult.completed = true;
        outResult.transportOk = true;
        outResult.statusCode = client.statusCode();
        outResult.headers = client.responseHeaderMap();
        outResult.body = client.responseBody();
    });
    SwObject::connect(&client, &SwHttpClient::errorOccurred, [&](int errorCode) {
        outResult.completed = true;
        outResult.transportOk = false;
        outResult.errorCode = errorCode;
        outResult.statusCode = client.statusCode();
        outResult.headers = client.responseHeaderMap();
        outResult.body = client.responseBody();
    });

    bool started = false;
    switch (method) {
    case SwHttpClient::Method::Get:
        started = client.get(url);
        break;
    case SwHttpClient::Method::Head:
        started = client.head(url);
        break;
    case SwHttpClient::Method::Post:
        started = client.post(url, body, contentType.isEmpty() ? SwString("application/json") : contentType);
        break;
    case SwHttpClient::Method::Put:
        started = client.put(url, body, contentType.isEmpty() ? SwString("application/json") : contentType);
        break;
    case SwHttpClient::Method::Patch:
        started = client.patch(url, body, contentType.isEmpty() ? SwString("application/json") : contentType);
        break;
    case SwHttpClient::Method::Delete:
        started = client.del(url);
        break;
    }
    if (!started) {
        return false;
    }

    const bool completed = waitUntil_(app, 10000, [&]() { return outResult.completed; });
    client.abort();
    return completed && outResult.completed;
}

class SocketCapture_ : public SwObject {
    SW_OBJECT(SocketCapture_, SwObject)

public:
    explicit SocketCapture_(SwAbstractSocket* socket, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_socket(socket) {
        if (!m_socket) {
            return;
        }
        SwObject::connect(m_socket, &SwAbstractSocket::connected, [this]() { m_connected = true; });
        SwObject::connect(m_socket, &SwAbstractSocket::disconnected, [this]() {
            drain_();
            m_disconnected = true;
        });
        SwObject::connect(m_socket, &SwAbstractSocket::errorOccurred, [this](int errorCode) {
            drain_();
            m_errorCode = errorCode;
        });
        SwObject::connect(m_socket, &SwIODevice::readyRead, [this]() { drain_(); });
        SwSslSocket* sslSocket = dynamic_cast<SwSslSocket*>(m_socket);
        if (sslSocket) {
            SwObject::connect(sslSocket, &SwSslSocket::encrypted, [this]() { m_encrypted = true; });
        }
    }

    void clearBuffer() {
        m_buffer.clear();
    }

    const SwByteArray& buffer() const {
        return m_buffer;
    }

    SwString bufferText() const {
        return SwString(m_buffer.toStdString());
    }

    bool connected() const {
        return m_connected;
    }

    bool disconnected() const {
        return m_disconnected;
    }

    bool encrypted() const {
        return m_encrypted;
    }

    int errorCode() const {
        return m_errorCode;
    }

private:
    void drain_() {
        if (!m_socket) {
            return;
        }
        while (true) {
            const SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_buffer.append(chunk.toStdString());
        }
    }

    SwAbstractSocket* m_socket = nullptr;
    SwByteArray m_buffer;
    bool m_connected = false;
    bool m_disconnected = false;
    bool m_encrypted = false;
    int m_errorCode = 0;
};

static bool waitForSocketContains_(SwCoreApplication& app,
                                   SocketCapture_& capture,
                                   const SwString& expected,
                                   int timeoutMs) {
    return waitUntil_(app, timeoutMs, [&capture, &expected]() {
        return capture.bufferText().contains(expected) || capture.errorCode() != 0;
    }) && capture.bufferText().contains(expected);
}

static bool writeSocketText_(SwAbstractSocket& socket, const SwString& text) {
    return socket.write(text);
}

static SwString buildAuthPlainToken_(const SwString& username, const SwString& password) {
    std::string raw;
    raw.push_back('\0');
    raw += username.toStdString();
    raw.push_back('\0');
    raw += password.toStdString();
    return SwString(SwCrypto::base64Encode(raw));
}

class BlockingTestSocket_ {
public:
    explicit BlockingTestSocket_(SwCoreApplication& app)
        : m_app(app) {
    }

    ~BlockingTestSocket_() {
        close();
    }

    bool connectPlain(const SwString& host, uint16_t port, SwString& outError) {
        outError.clear();
        close();

#if defined(_WIN32)
        static bool winsockReady = false;
        if (!winsockReady) {
            WSADATA wsaData {};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                outError = "WSAStartup failed";
                return false;
            }
            winsockReady = true;
        }
#endif

        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        const std::string portString = std::to_string(static_cast<unsigned int>(port));
        if (::getaddrinfo(host.toStdString().c_str(), portString.c_str(), &hints, &result) != 0 || !result) {
            outError = "getaddrinfo failed";
            return false;
        }

        for (struct addrinfo* it = result; it; it = it->ai_next) {
            m_socketFd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
#if defined(_WIN32)
            if (m_socketFd == INVALID_SOCKET) {
#else
            if (m_socketFd < 0) {
#endif
                continue;
            }
            if (::connect(m_socketFd, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
                break;
            }
            closeSocket_();
        }
        ::freeaddrinfo(result);

        if (!isOpen_()) {
            outError = "connect failed";
            return false;
        }
        setNonBlocking_();
        return true;
    }

    bool startTls(const SwString& peerHost, const SwString& trustedCaFile, SwString& outError) {
        outError.clear();
        if (!isOpen_()) {
            outError = "socket not open";
            return false;
        }

        m_sslCtx = SSL_CTX_new(TLS_client_method());
        if (!m_sslCtx) {
            outError = "SSL_CTX_new failed";
            return false;
        }
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_PEER, nullptr);
        if (!trustedCaFile.isEmpty() &&
            SSL_CTX_load_verify_locations(m_sslCtx, trustedCaFile.toStdString().c_str(), nullptr) != 1) {
            outError = "load_verify_locations failed";
            return false;
        }

        m_ssl = SSL_new(m_sslCtx);
        if (!m_ssl) {
            outError = "SSL_new failed";
            return false;
        }
        (void)SSL_set_tlsext_host_name(m_ssl, peerHost.toStdString().c_str());
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        (void)SSL_set1_host(m_ssl, peerHost.toStdString().c_str());
#endif
        SSL_set_fd(m_ssl, static_cast<int>(m_socketFd));

        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        while (std::chrono::steady_clock::now() < deadline) {
            pumpRuntimeOnce_(m_app);
            const int rc = SSL_connect(m_ssl);
            if (rc == 1) {
                return true;
            }
            const int sslError = SSL_get_error(m_ssl, rc);
            if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            char buffer[256] = {0};
            const unsigned long errorCode = ERR_get_error();
            if (errorCode != 0) {
                ERR_error_string_n(errorCode, buffer, sizeof(buffer));
                outError = SwString(buffer);
            } else {
                outError = "SSL_connect failed";
            }
            return false;
        }
        outError = "SSL_connect timeout";
        return false;
    }

    bool writeText(const SwString& text, SwString& outError) {
        return writeBytes(SwByteArray(text.toStdString()), outError);
    }

    bool writeBytes(const SwByteArray& bytes, SwString& outError) {
        outError.clear();
        const char* data = bytes.constData();
        std::size_t remaining = bytes.size();
        while (remaining > 0) {
            int written = 0;
            if (m_ssl) {
                written = SSL_write(m_ssl, data, static_cast<int>(remaining));
                if (written <= 0) {
                    const int sslError = SSL_get_error(m_ssl, written);
                    if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
                        pumpRuntimeOnce_(m_app);
                        continue;
                    }
                    outError = "SSL_write failed";
                    return false;
                }
            } else {
                written = ::send(m_socketFd, data, static_cast<int>(remaining), 0);
                if (written <= 0) {
#if defined(_WIN32)
                    const int socketError = WSAGetLastError();
                    if (socketError == WSAEWOULDBLOCK) {
#else
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
#endif
                        pumpRuntimeOnce_(m_app);
                        continue;
                    }
                    outError = "send failed";
                    return false;
                }
            }
            data += written;
            remaining -= static_cast<std::size_t>(written);
        }
        return true;
    }

    bool readUntilContains(const SwString& token, int timeoutMs, SwString& outText, SwString& outError) {
        outText.clear();
        outError.clear();
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            pumpRuntimeOnce_(m_app);
            char buffer[4096];
            int received = 0;
            if (m_ssl) {
                received = SSL_read(m_ssl, buffer, sizeof(buffer));
                if (received <= 0) {
                    const int sslError = SSL_get_error(m_ssl, received);
                    if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
                        outText = SwString(m_buffer.toStdString());
                        if (outText.contains(token)) {
                            return true;
                        }
                        pumpRuntimeOnce_(m_app);
                        continue;
                    }
                    if (sslError == SSL_ERROR_ZERO_RETURN) {
                        break;
                    }
                    outError = "SSL_read failed";
                    return false;
                }
            } else {
                received = ::recv(m_socketFd, buffer, sizeof(buffer), 0);
                if (received < 0) {
#if defined(_WIN32)
                    const int socketError = WSAGetLastError();
                    if (socketError == WSAEWOULDBLOCK) {
#else
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
#endif
                        outText = SwString(m_buffer.toStdString());
                        if (outText.contains(token)) {
                            return true;
                        }
                        pumpRuntimeOnce_(m_app);
                        continue;
                    }
                    outError = "recv failed";
                    return false;
                }
                if (received == 0) {
                    break;
                }
            }

            if (received > 0) {
                m_buffer.append(buffer, static_cast<std::size_t>(received));
                outText = SwString(m_buffer.toStdString());
                if (outText.contains(token)) {
                    return true;
                }
            }
        }
        outText = SwString(m_buffer.toStdString());
        outError = "read timeout";
        return outText.contains(token);
    }

    void clearBuffer() {
        m_buffer.clear();
    }

    SwString bufferText() const {
        return SwString(m_buffer.toStdString());
    }

    void close() {
        if (m_ssl) {
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        if (m_sslCtx) {
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
        }
        closeSocket_();
        m_buffer.clear();
    }

private:
    bool isOpen_() const {
#if defined(_WIN32)
        return m_socketFd != INVALID_SOCKET;
#else
        return m_socketFd >= 0;
#endif
    }

    void setNonBlocking_() {
#if defined(_WIN32)
        u_long nonBlocking = 1;
        (void)::ioctlsocket(m_socketFd, FIONBIO, &nonBlocking);
#else
        const int flags = ::fcntl(m_socketFd, F_GETFL, 0);
        if (flags >= 0) {
            (void)::fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK);
        }
#endif
    }

    void closeSocket_() {
#if defined(_WIN32)
        if (m_socketFd != INVALID_SOCKET) {
            ::closesocket(m_socketFd);
            m_socketFd = INVALID_SOCKET;
        }
#else
        if (m_socketFd >= 0) {
            ::close(m_socketFd);
            m_socketFd = -1;
        }
#endif
    }

    SwCoreApplication& m_app;
    SwByteArray m_buffer;
    SSL_CTX* m_sslCtx = nullptr;
    SSL* m_ssl = nullptr;
#if defined(_WIN32)
    SOCKET m_socketFd = INVALID_SOCKET;
#else
    int m_socketFd = -1;
#endif
};

class MailSelfTestRunner : public SwObject {
    SW_OBJECT(MailSelfTestRunner, SwObject)

public:
    explicit MailSelfTestRunner(SwCoreApplication* app, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_app(app)
        , m_timeout(45000, this) {
        SwObject::connect(&m_timeout, &SwTimer::timeout, [this]() { finish_(false, "global timeout"); });
    }

    bool startServerOnly(const SwString& infoPath = SwString()) {
        if (!prepareRuntime_()) {
            return false;
        }
        if (!configureServer_()) {
            return false;
        }
        if (!infoPath.isEmpty()) {
            SwJsonObject info;
            info["httpPort"] = static_cast<long long>(m_httpPort);
            info["smtpPort"] = static_cast<long long>(m_smtpPort);
            info["submissionPort"] = static_cast<long long>(m_submissionPort);
            info["imapsPort"] = static_cast<long long>(m_imapsPort);
            info["runtimeDir"] = m_runtimeDir.toStdString();
            info["caPath"] = m_caPath.toStdString();
            if (!writeTextFile_(infoPath, SwJsonDocument(info).toJson(SwJsonDocument::JsonFormat::Compact))) {
                return false;
            }
        }
        return true;
    }

    void start() {
        m_timeout.setSingleShot(true);
        m_timeout.start(45000);
        std::cout << "[MailSelfTest] prepareRuntime" << std::endl;
        if (!prepareRuntime_()) {
            finish_(false, "runtime preparation failed");
            return;
        }
        std::cout << "[MailSelfTest] configureServer" << std::endl;
        if (!configureServer_()) {
            finish_(false, "server configuration failed");
            return;
        }
        std::cout << "[MailSelfTest] admin smoke" << std::endl;
        if (!runAdminSmoke_()) {
            finish_(false, "admin API smoke failed");
            return;
        }
        std::cout << "[MailSelfTest] inbound SMTP smoke" << std::endl;
        if (!runInboundSmtpSmoke_()) {
            finish_(false, "SMTP inbound smoke failed");
            return;
        }
        std::cout << "[MailSelfTest] submission smoke" << std::endl;
        if (!runSubmissionSmoke_()) {
            finish_(false, "SMTP submission smoke failed");
            return;
        }
        std::cout << "[MailSelfTest] IMAP smoke" << std::endl;
        if (!runImapSmoke_()) {
            finish_(false, "IMAPS smoke failed");
            return;
        }
        std::cout << "[MailSelfTest] queue and metrics smoke" << std::endl;
        if (!runQueueAndMetricsSmoke_()) {
            finish_(false, "queue/metrics smoke failed");
            return;
        }
        finish_(true, "PASS");
    }

private:
    bool prepareRuntime_() {
        m_runtimeDir = swDirPlatform().absolutePath(SW_MAIL_SELFTEST_RUNTIME_DIR);
        m_runtimeDir += "/run-" + SwString::number(swMailDetail::currentEpochMs());
        if (!SwDir::mkpathAbsolute(m_runtimeDir)) {
            std::cerr << "[MailSelfTest] mkpath runtime failed: " << m_runtimeDir.toStdString() << std::endl;
            return false;
        }

        m_httpPort = findFreePort_(18100, 18200);
        m_smtpPort = findFreePort_(18200, 18300);
        m_submissionPort = findFreePort_(18300, 18400);
        m_imapsPort = findFreePort_(18400, 18500);
        if (!m_httpPort || !m_smtpPort || !m_submissionPort || !m_imapsPort) {
            return false;
        }

        m_storageDir = m_runtimeDir + "/mail-storage";
        m_certPath = m_runtimeDir + "/server-cert.pem";
        m_keyPath = m_runtimeDir + "/server-key.pem";
        m_caPath = m_runtimeDir + "/root-ca.pem";
        if (!SwDir::mkpathAbsolute(m_storageDir)) {
            std::cerr << "[MailSelfTest] mkpath storage failed: " << m_storageDir.toStdString() << std::endl;
            return false;
        }

        SwString caCertPem;
        SwString caKeyPem;
        SwString serverKeyPem;
        SwString serverCertPem;
        if (!generateRootCa_("SwMailSelfTest Root CA", caCertPem, caKeyPem)) {
            std::cerr << "[MailSelfTest] generateRootCa failed" << std::endl;
            return false;
        }
        if (!generateEcPrivateKeyPem_(serverKeyPem)) {
            std::cerr << "[MailSelfTest] generateEcPrivateKeyPem failed" << std::endl;
            return false;
        }

        SwList<SwString> domains;
        domains.append("localhost");
        domains.append("mail.localhost");
        SwByteArray csrDer;
        if (!buildServerCsrDer_(serverKeyPem, domains, csrDer)) {
            std::cerr << "[MailSelfTest] buildServerCsrDer failed" << std::endl;
            return false;
        }
        if (!signCertificateFromCsrDer_(csrDer, caCertPem, caKeyPem, 100, serverCertPem)) {
            std::cerr << "[MailSelfTest] signCertificateFromCsrDer failed" << std::endl;
            return false;
        }

        const bool writeCaOk = writeTextFile_(m_caPath, caCertPem);
        const bool writeKeyOk = writeTextFile_(m_keyPath, serverKeyPem);
        const bool writeCertOk = writeTextFile_(m_certPath, serverCertPem);
        if (!writeCaOk || !writeKeyOk || !writeCertOk) {
            std::cerr << "[MailSelfTest] write files failed ca=" << writeCaOk
                      << " key=" << writeKeyOk
                      << " cert=" << writeCertOk << std::endl;
            return false;
        }
        return true;
    }

    bool configureServer_() {
        SwMailConfig mailConfig;
        mailConfig.domain = "localhost";
        mailConfig.storageDir = m_storageDir;
        mailConfig.smtpPort = m_smtpPort;
        mailConfig.submissionPort = m_submissionPort;
        mailConfig.imapsPort = m_imapsPort;
        mailConfig.queueRetryBaseMs = 60ull * 1000ull;
        mailConfig.queueMaxAgeMs = 24ull * 60ull * 60ull * 1000ull;

        SwDomainTlsConfig tlsConfig;
        tlsConfig.mode = SwDomainTlsConfig::Manual;
        tlsConfig.domain = "localhost";
        tlsConfig.certPath = m_certPath;
        tlsConfig.keyPath = m_keyPath;

        SwHttpAuthConfig authConfig;
        authConfig.storageDir = m_runtimeDir + "/auth";
        authConfig.publicBaseUrl = baseUrl_();
        authConfig.mail.fromAddress = "auth@localhost";
        authConfig.mail.verificationTemplate.subject = "Verify your account";
        authConfig.mail.verificationTemplate.textBody = "Code: {{code}}\nURL: {{url}}\n";
        authConfig.mail.resetPasswordTemplate.subject = "Reset your password";
        authConfig.mail.resetPasswordTemplate.textBody = "Code: {{code}}\nURL: {{url}}\n";

        SwHttpAuthHooks authHooks;
        authHooks.registerSubject = [this](const SwString& email,
                                           const SwJsonValue& payload,
                                           SwString& outSubjectId,
                                           SwJsonValue& outSubjectView,
                                           SwString& outError) {
            outError.clear();
            outSubjectId = "subject-" + SwString::number(static_cast<long long>(m_subjectViews.size() + 1));
            SwJsonObject subject;
            subject["subjectId"] = outSubjectId.toStdString();
            subject["email"] = email.toStdString();
            subject["payload"] = payload;
            outSubjectView = SwJsonValue(subject);
            m_subjectViews[outSubjectId] = outSubjectView;
            return true;
        };
        authHooks.loadSubject = [this](const SwString& subjectId, SwJsonValue& outSubjectView, SwString& outError) {
            outError.clear();
            if (!m_subjectViews.contains(subjectId)) {
                outError = "Subject not found";
                return false;
            }
            outSubjectView = m_subjectViews.value(subjectId);
            return true;
        };
        authHooks.deliverMail = [this](const SwHttpAuthOutgoingMail& mail, SwString& outError) {
            outError.clear();
            m_authMailByPurpose[mail.purpose] = mail;
            return true;
        };

        m_httpApp.setMailConfig(mailConfig);
        m_httpApp.setDomainTlsConfig(tlsConfig);
        m_httpApp.setAuthConfig(authConfig);
        m_httpApp.mountAuthApi(SwHttpAuthApiOptions(), authHooks);
        m_httpApp.mountMailAdminApi(SwMailAdminApiOptions(), [](SwHttpContext&) { return true; });
        m_httpApp.get("/debug/auth-mail/:purpose", [this](SwHttpContext& ctx) {
            const SwString purpose = ctx.pathValue("purpose");
            if (!m_authMailByPurpose.contains(purpose)) {
                SwJsonObject error;
                error["error"] = "Mail not found";
                ctx.json(SwJsonValue(error), 404);
                return;
            }
            const SwHttpAuthOutgoingMail mail = m_authMailByPurpose.value(purpose);
            SwJsonObject object;
            object["purpose"] = mail.purpose.toStdString();
            object["email"] = mail.email.toStdString();
            object["code"] = mail.code.toStdString();
            object["url"] = mail.url.toStdString();
            object["subject"] = mail.subject.toStdString();
            ctx.json(SwJsonValue(object));
        });
        if (!m_httpApp.listen(m_httpPort)) {
            std::cerr << "[MailSelfTest] HTTP listen failed on " << m_httpPort << std::endl;
            return false;
        }
        if (!m_httpApp.startMail()) {
            std::cerr << "[MailSelfTest] startMail failed" << std::endl;
            return false;
        }
        if (!m_httpApp.startAuth()) {
            std::cerr << "[MailSelfTest] startAuth failed" << std::endl;
            return false;
        }
        return m_httpApp.mailService() != nullptr;
    }

    bool runAdminSmoke_() {
        SwMailService* service = m_httpApp.mailService();
        if (!service || !service->hasTlsCredentials()) {
            return false;
        }

        SwMailAccount account;
        if (!service->createAccount("alice@localhost", "secret123", &account).ok()) {
            return false;
        }

        SwMailAlias alias;
        alias.address = "sales@localhost";
        alias.domain = "localhost";
        alias.localPart = "sales";
        alias.targets.append("alice@localhost");
        alias.active = true;
        if (!service->upsertAlias(alias).ok()) {
            return false;
        }

        return service->listAccounts().size() == 1 &&
               service->listAliases().size() == 1;
    }

    bool runAuthSmoke_() {
        SwMap<SwString, SwString> headers;
        auto postJson = [&](const SwString& path,
                            const SwJsonObject& object,
                            HttpRequestResult_& result,
                            const SwMap<SwString, SwString>& requestHeaders = SwMap<SwString, SwString>()) {
            const SwString json = SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact);
            return performHttpRequest_(*m_app,
                                       SwHttpClient::Method::Post,
                                       baseUrl_() + path,
                                       SwByteArray(json.toStdString()),
                                       "application/json",
                                       requestHeaders,
                                       result);
        };
        auto getNoBody = [&](const SwString& path,
                             HttpRequestResult_& result,
                             const SwMap<SwString, SwString>& requestHeaders = SwMap<SwString, SwString>()) {
            return performHttpRequest_(*m_app,
                                       SwHttpClient::Method::Get,
                                       baseUrl_() + path,
                                       SwByteArray(),
                                       SwString(),
                                       requestHeaders,
                                       result);
        };
        auto parseObject = [](const HttpRequestResult_& result, SwJsonObject& outObject) {
            SwJsonDocument document;
            if (!parseJsonDocument_(result.body, document) || !document.isObject()) {
                return false;
            }
            outObject = document.object();
            return true;
        };

        HttpRequestResult_ result;
        SwJsonObject registerBody;
        registerBody["email"] = "bob@localhost";
        registerBody["password"] = "secret123";
        SwJsonObject payload;
        payload["displayName"] = "Bob";
        registerBody["payload"] = payload;
        if (!postJson("/api/auth/register", registerBody, result) || result.statusCode != 201) {
            return false;
        }
        if (!m_authMailByPurpose.contains("verify_email")) {
            return false;
        }

        SwJsonObject loginBody;
        loginBody["email"] = "bob@localhost";
        loginBody["password"] = "secret123";
        if (!postJson("/api/auth/login", loginBody, result) || result.statusCode != 403) {
            return false;
        }

        SwJsonObject verifyBody;
        verifyBody["code"] = m_authMailByPurpose.value("verify_email").code.toStdString();
        if (!postJson("/api/auth/email/verify", verifyBody, result) || result.statusCode != 200) {
            return false;
        }

        if (!postJson("/api/auth/login", loginBody, result) || result.statusCode != 200) {
            return false;
        }
        SwJsonObject loginObject;
        if (!parseObject(result, loginObject)) {
            return false;
        }
        const SwString bearerToken = loginObject.value("token").toString().c_str();
        const SwString setCookie = result.headers.value("set-cookie");
        if (bearerToken.isEmpty() || setCookie.isEmpty()) {
            return false;
        }
        const int cookieSep = setCookie.indexOf(";");
        const SwString cookieHeader = cookieSep >= 0 ? setCookie.left(cookieSep) : setCookie;

        SwMap<SwString, SwString> bearerHeaders;
        bearerHeaders["authorization"] = "Bearer " + bearerToken;
        if (!getNoBody("/api/auth/me", result, bearerHeaders) || result.statusCode != 200) {
            return false;
        }
        SwJsonObject meObject;
        if (!parseObject(result, meObject)) {
            return false;
        }
        if (!meObject.contains("subject") ||
            meObject.value("subject").toObject().value("payload").toObject().value("displayName").toString() != "Bob") {
            return false;
        }

        SwMap<SwString, SwString> cookieHeaders;
        cookieHeaders["cookie"] = cookieHeader;
        if (!getNoBody("/api/auth/me", result, cookieHeaders) || result.statusCode != 200) {
            return false;
        }

        SwJsonObject forgotBody;
        forgotBody["email"] = "bob@localhost";
        if (!postJson("/api/auth/password/forgot", forgotBody, result) || result.statusCode != 200) {
            return false;
        }
        if (!m_authMailByPurpose.contains("reset_password")) {
            return false;
        }

        SwJsonObject resetBody;
        resetBody["code"] = m_authMailByPurpose.value("reset_password").code.toStdString();
        resetBody["newPassword"] = "secret456";
        if (!postJson("/api/auth/password/reset", resetBody, result) || result.statusCode != 200) {
            return false;
        }

        if (!postJson("/api/auth/login", loginBody, result) || result.statusCode != 401) {
            return false;
        }

        loginBody["password"] = "secret456";
        if (!postJson("/api/auth/login", loginBody, result) || result.statusCode != 200) {
            return false;
        }
        if (!parseObject(result, loginObject)) {
            return false;
        }
        const SwString bearerToken2 = loginObject.value("token").toString().c_str();
        if (bearerToken2.isEmpty()) {
            return false;
        }

        SwJsonObject changeBody;
        changeBody["currentPassword"] = "secret456";
        changeBody["newPassword"] = "secret789";
        bearerHeaders["authorization"] = "Bearer " + bearerToken2;
        if (!postJson("/api/auth/password/change", changeBody, result, bearerHeaders) || result.statusCode != 200) {
            return false;
        }

        loginBody["password"] = "secret789";
        if (!postJson("/api/auth/login", loginBody, result) || result.statusCode != 200) {
            return false;
        }

        SwMap<SwString, SwString> logoutHeaders;
        logoutHeaders["cookie"] = cookieHeader;
        if (!postJson("/api/auth/logout", SwJsonObject(), result, logoutHeaders) || result.statusCode != 200) {
            return false;
        }
        if (!getNoBody("/api/auth/me", result, logoutHeaders) || result.statusCode != 401) {
            return false;
        }

        return true;
    }

    bool runInboundSmtpSmoke_() {
        BlockingTestSocket_ socket(*m_app);
        SwString ioError;
        SwString response;
        std::cout << "[MailSelfTest] SMTP connect start" << std::endl;
        if (!socket.connectPlain("127.0.0.1", m_smtpPort, ioError)) {
            return false;
        }
        std::cout << "[MailSelfTest] SMTP connected" << std::endl;
        if (!socket.readUntilContains("220", 5000, response, ioError)) {
            return false;
        }
        std::cout << "[MailSelfTest] SMTP greeting received" << std::endl;

        socket.clearBuffer();
        if (!socket.writeText("EHLO inbound.local\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        if (!socket.writeText("MAIL FROM:<sender@example.net>\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        if (!socket.writeText("RCPT TO:<sales@localhost>\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        if (!socket.writeText("DATA\r\n", ioError) || !socket.readUntilContains("354", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();

        const SwString message =
            "Subject: Inbound alias smoke\r\n"
            "From: Sender <sender@example.net>\r\n"
            "To: Sales <sales@localhost>\r\n"
            "\r\n"
            "Inbound delivery through alias.\r\n"
            ".\r\n";
        if (!socket.writeText(message, ioError) || !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        (void)socket.writeText("QUIT\r\n", ioError);

        return waitUntil_(*m_app, 5000, [this]() {
            SwMailService* service = m_httpApp.mailService();
            return service && service->listMessages("alice@localhost", "INBOX").size() == 1;
        });
    }

    bool runSubmissionSmoke_() {
        BlockingTestSocket_ socket(*m_app);
        SwString ioError;
        SwString response;
        if (!socket.connectPlain("127.0.0.1", m_submissionPort, ioError)) {
            return false;
        }
        if (!socket.readUntilContains("220", 5000, response, ioError)) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("EHLO submit.local\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        if (!socket.bufferText().contains("STARTTLS")) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("STARTTLS\r\n", ioError) || !socket.readUntilContains("220", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        if (!socket.startTls("localhost", m_caPath, ioError)) {
            return false;
        }
        if (!socket.writeText("EHLO submit.local\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        if (!socket.bufferText().contains("AUTH")) {
            return false;
        }

        socket.clearBuffer();
        const SwString token = buildAuthPlainToken_("alice@localhost", "secret123");
        if (!socket.writeText("AUTH PLAIN " + token + "\r\n", ioError) ||
            !socket.readUntilContains("235", 5000, response, ioError)) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("MAIL FROM:<alice@localhost>\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        if (!socket.writeText("RCPT TO:<alice@localhost>\r\n", ioError) ||
            !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();
        if (!socket.writeText("DATA\r\n", ioError) || !socket.readUntilContains("354", 5000, response, ioError)) {
            return false;
        }
        socket.clearBuffer();

        const SwString message =
            "Subject: Submission smoke\r\n"
            "From: Alice <alice@localhost>\r\n"
            "To: Alice <alice@localhost>\r\n"
            "\r\n"
            "Authenticated submission body.\r\n"
            ".\r\n";
        if (!socket.writeText(message, ioError) || !socket.readUntilContains("250", 5000, response, ioError)) {
            return false;
        }
        (void)socket.writeText("QUIT\r\n", ioError);

        if (!waitUntil_(*m_app, 5000, [this]() {
                SwMailService* service = m_httpApp.mailService();
                return service &&
                       service->listMessages("alice@localhost", "INBOX").size() >= 2 &&
                       service->listMessages("alice@localhost", "Sent").size() >= 1;
            })) {
            return false;
        }

        SwMailService* service = m_httpApp.mailService();
        if (!service) {
            return false;
        }
        const SwList<SwMailMessageEntry> inboxMessages = service->listMessages("alice@localhost", "INBOX");
        if (inboxMessages.size() < 2) {
            return false;
        }
        return SwString(inboxMessages.last().rawMessage.toStdString()).contains("DKIM-Signature:");
    }

    bool runImapSmoke_() {
        BlockingTestSocket_ socket(*m_app);
        SwString ioError;
        SwString response;
        if (!socket.connectPlain("127.0.0.1", m_imapsPort, ioError) ||
            !socket.startTls("localhost", m_caPath, ioError)) {
            return false;
        }
        if (!socket.readUntilContains("* OK", 5000, response, ioError)) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a1 LOGIN alice@localhost secret123\r\n", ioError) ||
            !socket.readUntilContains("a1 OK", 5000, response, ioError)) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a2 LIST \"\" \"*\"\r\n", ioError) ||
            !socket.readUntilContains("a2 OK", 5000, response, ioError) ||
            !socket.bufferText().contains("* LIST")) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a3 SELECT INBOX\r\n", ioError) ||
            !socket.readUntilContains("a3 OK", 5000, response, ioError) ||
            !socket.bufferText().contains("* FLAGS")) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a4 UID FETCH 1:* (FLAGS BODY[])\r\n", ioError) ||
            !socket.readUntilContains("a4 OK UID FETCH completed", 5000, response, ioError)) {
            return false;
        }
        const SwString fetchText = socket.bufferText();
        if (!fetchText.contains("Inbound alias smoke") || !fetchText.contains("Submission smoke")) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a5 UID STORE 1:* +FLAGS.SILENT (\\\\Seen)\r\n", ioError) ||
            !socket.readUntilContains("a5 OK", 5000, response, ioError)) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a6 UID SEARCH ALL\r\n", ioError) ||
            !socket.readUntilContains("a6 OK", 5000, response, ioError)) {
            return false;
        }

        socket.clearBuffer();
        if (!socket.writeText("a7 LOGOUT\r\n", ioError) || !socket.readUntilContains("* BYE", 5000, response, ioError)) {
            return false;
        }
        return true;
    }

    bool runQueueAndMetricsSmoke_() {
        SwMailService* service = m_httpApp.mailService();
        if (!service) {
            return false;
        }

        SwMailQueueItem queueItem;
        queueItem.id = "selftest-queue";
        queueItem.envelope.mailFrom = "alice@localhost";
        queueItem.envelope.rcptTo.append("remote@example.invalid");
        queueItem.rawMessage = SwByteArray(
            "Subject: Queued remote smoke\r\n"
            "From: Alice <alice@localhost>\r\n"
            "To: Remote <remote@example.invalid>\r\n"
            "\r\n"
            "Queued body.\r\n");
        queueItem.createdAtMs = swMailDetail::currentEpochMs();
        queueItem.updatedAtMs = queueItem.createdAtMs;
        queueItem.nextAttemptAtMs = queueItem.createdAtMs + 30 * 60 * 1000;
        queueItem.expireAtMs = queueItem.createdAtMs + 24 * 60 * 60 * 1000;
        if (!service->store().storeQueueItem(queueItem).ok()) {
            return false;
        }

        const SwMailMetrics metrics = service->metricsSnapshot();
        return !service->listQueueItems().isEmpty() &&
               !service->listDkimRecords().isEmpty() &&
               metrics.inboundAccepted >= 2 &&
               metrics.imapSessions >= 1 &&
               metrics.submissionSessions >= 1;
    }

    SwString baseUrl_() const {
        return "http://127.0.0.1:" + SwString::number(m_httpPort);
    }

    void finish_(bool success, const SwString& detail) {
        m_timeout.stop();
        m_httpApp.stopAuth();
        m_httpApp.stopMail();
        m_httpApp.close();
        if (success) {
            std::cout << "[MailSelfTest] PASS " << detail.toStdString() << std::endl;
            m_app->exit(0);
            return;
        }
        std::cerr << "[MailSelfTest] FAIL " << detail.toStdString() << std::endl;
        m_app->exit(1);
    }

    SwCoreApplication* m_app = nullptr;
    SwHttpApp m_httpApp;
    SwTimer m_timeout;

    SwString m_runtimeDir;
    SwString m_storageDir;
    SwString m_certPath;
    SwString m_keyPath;
    SwString m_caPath;
    uint16_t m_httpPort = 0;
    uint16_t m_smtpPort = 0;
    uint16_t m_submissionPort = 0;
    uint16_t m_imapsPort = 0;
    SwMap<SwString, SwJsonValue> m_subjectViews;
    SwMap<SwString, SwHttpAuthOutgoingMail> m_authMailByPurpose;
};

} // namespace

int main(int argc, char* argv[]) {
    MailSelfTestApplication_ app(argc, argv);
    MailSelfTestRunner runner(&app);
    bool serverOnly = false;
    SwString infoPath;
    for (int i = 1; i < argc; ++i) {
        const SwString arg = argv[i];
        if (arg == "--server-only") {
            serverOnly = true;
        } else if (arg == "--write-info" && i + 1 < argc) {
            infoPath = argv[++i];
        }
    }
    if (serverOnly) {
        if (!runner.startServerOnly(infoPath)) {
            std::cerr << "[MailSelfTest] FAIL server-only startup" << std::endl;
            return 1;
        }
        std::cout << "[MailSelfTest] server-only ready" << std::endl;
        return app.exec();
    }
    SwTimer::singleShot(0, [&runner]() { runner.start(); });
    return app.exec();
}
