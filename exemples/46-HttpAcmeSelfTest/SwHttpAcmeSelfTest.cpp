#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwHttpAcmeManager.h"
#include "SwHttpApp.h"
#include "SwHttpClient.h"
#include "SwTcpServer.h"
#include "SwTimer.h"

#include "SwHttpAcmeSelfTestConfig.h"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

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

static bool writeTextFile_(const SwString& path, const SwString& text) {
    SwFile file(path);
    if (!file.openBinary(SwFile::Write)) {
        return false;
    }
    const bool ok = file.write(SwByteArray(text.toStdString()));
    file.close();
    return ok;
}

static bool readTextFile_(const SwString& path, SwString& outText) {
    outText.clear();
    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        return false;
    }
    outText = file.readAll();
    file.close();
    return !outText.isEmpty();
}

static SwString base64UrlEncode_(const SwByteArray& bytes) {
    SwString encoded(bytes.toBase64().toStdString());
    encoded.replace("+", "-");
    encoded.replace("/", "_");
    while (encoded.endsWith("=")) {
        encoded.chop(1);
    }
    return encoded;
}

static SwByteArray base64UrlDecode_(const SwString& input) {
    SwString normalized = input;
    normalized.replace("-", "+");
    normalized.replace("_", "/");
    while ((normalized.size() % 4) != 0) {
        normalized += "=";
    }
    return SwByteArray::fromBase64(SwByteArray(normalized.toStdString()));
}

static bool parseJsonObject_(const SwByteArray& body, SwJsonObject& outObject) {
    outObject = SwJsonObject();
    SwString error;
    SwJsonDocument doc = SwJsonDocument::fromJson(body.toStdString(), error);
    if (!error.isEmpty() || !doc.isObject()) {
        return false;
    }
    outObject = doc.object();
    return true;
}

static bool parseJoseRequest_(const SwByteArray& body,
                              SwJsonObject& protectedObjectOut,
                              SwByteArray& payloadRawOut,
                              SwJsonObject& payloadObjectOut) {
    protectedObjectOut = SwJsonObject();
    payloadRawOut.clear();
    payloadObjectOut = SwJsonObject();

    SwJsonObject envelope;
    if (!parseJsonObject_(body, envelope)) {
        return false;
    }

    const SwByteArray protectedRaw = base64UrlDecode_(envelope.value("protected").toString().c_str());
    if (!parseJsonObject_(protectedRaw, protectedObjectOut)) {
        return false;
    }

    const SwString payloadB64 = envelope.value("payload").toString().c_str();
    if (!payloadB64.isEmpty()) {
        payloadRawOut = base64UrlDecode_(payloadB64);
        (void)parseJsonObject_(payloadRawOut, payloadObjectOut);
    }
    return true;
}

static SwString jwkThumbprint_(const SwJsonObject& jwk) {
    const SwString x = jwk.value("x").toString().c_str();
    const SwString y = jwk.value("y").toString().c_str();
    const SwString canonical =
        SwString("{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"") + x + "\",\"y\":\"" + y + "\"}";
    unsigned char hash[SHA256_DIGEST_LENGTH] = {0};
    SHA256(reinterpret_cast<const unsigned char*>(canonical.data()), static_cast<size_t>(canonical.size()), hash);
    return base64UrlEncode_(SwByteArray(reinterpret_cast<const char*>(hash), sizeof(hash)));
}

static bool generateEcPrivateKeyPem_(SwString& outPem);
static bool generateRootCa_(const SwString& commonName, SwString& certPemOut, SwString& keyPemOut);
static bool signCertificateFromCsrDer_(const SwByteArray& csrDer,
                                       const SwString& caCertPem,
                                       const SwString& caKeyPem,
                                       long long serialNumber,
                                       SwString& certificatePemOut);

class HttpAcmeSelfTestRunner : public SwObject {
    SW_OBJECT(HttpAcmeSelfTestRunner, SwObject)

public:
    explicit HttpAcmeSelfTestRunner(SwCoreApplication* app, SwObject* parent = nullptr);
    void start();

private slots:
    void onTimeout_();
    void onCertificateUpdated_(const SwString& certPath, const SwString& keyPath);
    void onManagerError_(const SwString& message);

private:
    struct OrderState {
        int id = 0;
        SwString token;
        SwString certificatePem;
        bool challengeAcknowledged = false;
        bool authorizationValid = false;
        bool finalized = false;
    };

    bool prepareRuntime_();
    void configureManagedApp_();
    void configureFakeAcmeServer_();
    bool sendJson_(SwHttpContext& ctx, int status, const SwJsonObject& object, const SwString& location = SwString());
    SwString acmeBaseUrl_() const;
    SwString nextNonce_();
    void scheduleChallengeVerification_();
    void verifyChallengeNow_();
    void probeHttp_(const std::function<void(bool)>& done);
    void probeHttps_(const std::function<void(bool)>& done);
    void finish_(bool success, const SwString& reason = SwString());

    SwCoreApplication* m_app = nullptr;
    SwHttpApp m_managedApp;
    SwHttpApp m_fakeAcmeApp;
    SwHttpAcmeManager m_manager;
    SwTimer m_timeout;

    SwString m_runtimeDir = SW_HTTP_ACME_SELFTEST_RUNTIME_DIR;
    SwString m_storageDir;
    SwString m_caCertPath;
    SwString m_caKeyPath;
    SwString m_caCertPem;
    SwString m_caKeyPem;

    uint16_t m_httpPort = 0;
    uint16_t m_httpsPort = 0;
    uint16_t m_acmePort = 0;

    int m_nonceCounter = 0;
    int m_orderCounter = 0;
    int m_certificateUpdates = 0;
    SwString m_accountThumbprint;
    SwString m_firstCertificatePem;
    OrderState m_order;
};

HttpAcmeSelfTestRunner::HttpAcmeSelfTestRunner(SwCoreApplication* app, SwObject* parent)
    : SwObject(parent)
    , m_app(app)
    , m_manager(SwAcmeConfig(), this)
    , m_timeout(60000, this) {
    connect(&m_timeout, &SwTimer::timeout, this, &HttpAcmeSelfTestRunner::onTimeout_);
    connect(&m_manager, &SwHttpAcmeManager::certificateUpdated, this, &HttpAcmeSelfTestRunner::onCertificateUpdated_);
    connect(&m_manager, &SwHttpAcmeManager::errorOccurred, this, &HttpAcmeSelfTestRunner::onManagerError_);
}

void HttpAcmeSelfTestRunner::start() {
    if (!prepareRuntime_()) {
        finish_(false, "runtime preparation failed");
        return;
    }

    configureFakeAcmeServer_();
    if (!m_fakeAcmeApp.listen(m_acmePort)) {
        finish_(false, "unable to start fake ACME server");
        return;
    }

    configureManagedApp_();

    SwAcmeConfig config;
    config.directoryUrl = acmeBaseUrl_() + "/directory";
    config.domain = "localhost";
    config.storageDir = m_storageDir;
    config.httpPort = m_httpPort;
    config.httpsPort = m_httpsPort;
    config.renewBeforeDays = 0;
    config.retryDelayMs = 1000;
    config.pollIntervalMs = 200;
    config.maxPollAttempts = 50;
    m_manager.setConfig(config);

    m_timeout.setSingleShot(true);
    m_timeout.start(60000);

    if (!m_manager.start(&m_managedApp)) {
        finish_(false, "unable to start ACME manager");
    }
}

void HttpAcmeSelfTestRunner::onTimeout_() {
    finish_(false, "timeout");
}

void HttpAcmeSelfTestRunner::onManagerError_(const SwString& message) {
    finish_(false, "manager error: " + message);
}

void HttpAcmeSelfTestRunner::onCertificateUpdated_(const SwString&, const SwString&) {
    ++m_certificateUpdates;

    if (m_certificateUpdates == 1) {
        if (!m_managedApp.isHttpListening() || !m_managedApp.isHttpsListening()) {
            finish_(false, "managed app is not listening on both transports");
            return;
        }
        if (!readTextFile_(m_manager.certificatePath(), m_firstCertificatePem)) {
            finish_(false, "unable to read first certificate");
            return;
        }

        probeHttp_([this](bool ok) {
            if (!ok) {
                finish_(false, "HTTP probe failed after initial issuance");
                return;
            }
            probeHttps_([this](bool httpsOk) {
                if (!httpsOk) {
                    finish_(false, "HTTPS probe failed after initial issuance");
                    return;
                }
                m_manager.renewNow();
            });
        });
        return;
    }

    if (m_certificateUpdates == 2) {
        SwString renewedCertificatePem;
        if (!readTextFile_(m_manager.certificatePath(), renewedCertificatePem)) {
            finish_(false, "unable to read renewed certificate");
            return;
        }
        if (renewedCertificatePem == m_firstCertificatePem) {
            finish_(false, "renewed certificate did not change");
            return;
        }

        probeHttps_([this](bool ok) {
            if (!ok) {
                finish_(false, "HTTPS probe failed after renewal");
                return;
            }
            finish_(true);
        });
        return;
    }
}

bool HttpAcmeSelfTestRunner::prepareRuntime_() {
    if (SwDir::exists(m_runtimeDir)) {
        SwDir::removeRecursively(m_runtimeDir);
    }
    if (!SwDir::mkpathAbsolute(m_runtimeDir)) {
        return false;
    }

    m_storageDir = m_runtimeDir + "/acme-storage";
    if (!SwDir::mkpathAbsolute(m_storageDir)) {
        return false;
    }

    m_caCertPath = m_runtimeDir + "/fake-ca-cert.pem";
    m_caKeyPath = m_runtimeDir + "/fake-ca-key.pem";
    if (!generateRootCa_("SwStack ACME Test CA", m_caCertPem, m_caKeyPem) ||
        !writeTextFile_(m_caCertPath, m_caCertPem) ||
        !writeTextFile_(m_caKeyPath, m_caKeyPem)) {
        return false;
    }

    m_httpPort = findFreePort_(19610, 19660);
    m_httpsPort = findFreePort_(19660, 19710);
    m_acmePort = findFreePort_(19710, 19760);
    return m_httpPort != 0 && m_httpsPort != 0 && m_acmePort != 0;
}

void HttpAcmeSelfTestRunner::configureManagedApp_() {
    m_managedApp.get("/hello", [](SwHttpContext& ctx) {
        const SwString body = (ctx.isTls() ? SwString("https:") : SwString("http:")) +
                              SwString::number(static_cast<int>(ctx.localPort()));
        ctx.text(body, 200);
    });

    m_managedApp.get("/.well-known/acme-challenge/:token", [](SwHttpContext& ctx) {
        ctx.text("route-should-not-win", 200);
    });
}

SwString HttpAcmeSelfTestRunner::acmeBaseUrl_() const {
    return "http://127.0.0.1:" + SwString::number(static_cast<int>(m_acmePort));
}

SwString HttpAcmeSelfTestRunner::nextNonce_() {
    ++m_nonceCounter;
    return "nonce-" + SwString::number(m_nonceCounter);
}

bool HttpAcmeSelfTestRunner::sendJson_(SwHttpContext& ctx, int status, const SwJsonObject& object, const SwString& location) {
    ctx.setHeader("replay-nonce", nextNonce_());
    if (!location.isEmpty()) {
        ctx.setHeader("location", location);
    }
    ctx.send(SwByteArray(SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact).toStdString()),
             "application/json",
             status);
    return true;
}

void HttpAcmeSelfTestRunner::configureFakeAcmeServer_() {
    m_fakeAcmeApp.get("/directory", [this](SwHttpContext& ctx) {
        SwJsonObject object;
        object["newNonce"] = (acmeBaseUrl_() + "/new-nonce").toStdString();
        object["newAccount"] = (acmeBaseUrl_() + "/new-account").toStdString();
        object["newOrder"] = (acmeBaseUrl_() + "/new-order").toStdString();
        sendJson_(ctx, 200, object);
    });

    m_fakeAcmeApp.head("/new-nonce", [this](SwHttpContext& ctx) {
        ctx.setHeader("replay-nonce", nextNonce_());
        ctx.noContent(200);
    });

    m_fakeAcmeApp.post("/new-account", [this](SwHttpContext& ctx) {
        SwJsonObject protectedObject;
        SwByteArray payloadRaw;
        SwJsonObject payloadObject;
        if (!parseJoseRequest_(ctx.request().body, protectedObject, payloadRaw, payloadObject)) {
            ctx.text("invalid-jose", 400);
            return;
        }

        if (protectedObject.contains("jwk")) {
            m_accountThumbprint = jwkThumbprint_(protectedObject.value("jwk").toObject());
        }

        SwJsonObject object;
        object["status"] = "valid";
        sendJson_(ctx, 201, object, acmeBaseUrl_() + "/account/1");
    });

    m_fakeAcmeApp.post("/new-order", [this](SwHttpContext& ctx) {
        ++m_orderCounter;
        m_order = OrderState();
        m_order.id = m_orderCounter;
        m_order.token = "token-" + SwString::number(m_order.id);

        SwJsonArray authzs;
        authzs.append(SwJsonValue((acmeBaseUrl_() + "/authz/" + SwString::number(m_order.id)).toStdString()));

        SwJsonObject object;
        object["status"] = "pending";
        object["authorizations"] = authzs;
        object["finalize"] = (acmeBaseUrl_() + "/finalize/" + SwString::number(m_order.id)).toStdString();
        sendJson_(ctx, 201, object, acmeBaseUrl_() + "/order/" + SwString::number(m_order.id));
    });

    m_fakeAcmeApp.post("/authz/:id(int)", [this](SwHttpContext& ctx) {
        const int id = ctx.request().pathParams.value("id").toInt();
        if (id != m_order.id) {
            ctx.text("not-found", 404);
            return;
        }

        SwJsonObject challenge;
        challenge["type"] = "http-01";
        challenge["url"] = (acmeBaseUrl_() + "/challenge/" + SwString::number(m_order.id)).toStdString();
        challenge["token"] = m_order.token.toStdString();
        challenge["status"] = m_order.authorizationValid ? "valid" : "pending";

        SwJsonArray challenges;
        challenges.append(challenge);

        SwJsonObject object;
        object["status"] = m_order.authorizationValid ? "valid" : "pending";
        object["challenges"] = challenges;
        sendJson_(ctx, 200, object);
    });

    m_fakeAcmeApp.post("/challenge/:id(int)", [this](SwHttpContext& ctx) {
        const int id = ctx.request().pathParams.value("id").toInt();
        if (id != m_order.id) {
            ctx.text("not-found", 404);
            return;
        }

        m_order.challengeAcknowledged = true;
        scheduleChallengeVerification_();

        SwJsonObject object;
        object["status"] = "pending";
        sendJson_(ctx, 200, object);
    });

    m_fakeAcmeApp.post("/finalize/:id(int)", [this](SwHttpContext& ctx) {
        const int id = ctx.request().pathParams.value("id").toInt();
        if (id != m_order.id) {
            ctx.text("not-found", 404);
            return;
        }
        if (!m_order.authorizationValid) {
            ctx.text("authz-not-valid", 400);
            return;
        }

        SwJsonObject protectedObject;
        SwByteArray payloadRaw;
        SwJsonObject payloadObject;
        if (!parseJoseRequest_(ctx.request().body, protectedObject, payloadRaw, payloadObject)) {
            ctx.text("invalid-jose", 400);
            return;
        }

        const SwByteArray csrDer = base64UrlDecode_(payloadObject.value("csr").toString().c_str());
        if (!signCertificateFromCsrDer_(csrDer,
                                        m_caCertPem,
                                        m_caKeyPem,
                                        static_cast<long long>(m_order.id),
                                        m_order.certificatePem)) {
            ctx.text("csr-sign-failed", 500);
            return;
        }

        m_order.finalized = true;
        SwJsonObject object;
        object["status"] = "valid";
        object["certificate"] = (acmeBaseUrl_() + "/cert/" + SwString::number(m_order.id)).toStdString();
        sendJson_(ctx, 200, object);
    });

    m_fakeAcmeApp.post("/order/:id(int)", [this](SwHttpContext& ctx) {
        const int id = ctx.request().pathParams.value("id").toInt();
        if (id != m_order.id) {
            ctx.text("not-found", 404);
            return;
        }

        SwJsonObject object;
        object["status"] = m_order.finalized ? "valid" : "pending";
        if (m_order.finalized) {
            object["certificate"] = (acmeBaseUrl_() + "/cert/" + SwString::number(m_order.id)).toStdString();
        }
        sendJson_(ctx, 200, object);
    });

    m_fakeAcmeApp.post("/cert/:id(int)", [this](SwHttpContext& ctx) {
        const int id = ctx.request().pathParams.value("id").toInt();
        if (id != m_order.id || m_order.certificatePem.isEmpty()) {
            ctx.text("not-found", 404);
            return;
        }
        ctx.setHeader("replay-nonce", nextNonce_());
        ctx.send(SwByteArray(m_order.certificatePem.toStdString()), "application/pem-certificate-chain", 200);
    });
}

void HttpAcmeSelfTestRunner::scheduleChallengeVerification_() {
    HttpAcmeSelfTestRunner* self = this;
    SwTimer::singleShot(100, [self]() {
        if (SwObject::isLive(self)) {
            self->verifyChallengeNow_();
        }
    });
}

void HttpAcmeSelfTestRunner::verifyChallengeNow_() {
    SwHttpClient* client = new SwHttpClient(this);
    connect(client, &SwHttpClient::finished, [this, client](const SwByteArray&) {
        const SwString expected = m_order.token + "." + m_accountThumbprint;
        if (client->statusCode() == 200 && client->responseBodyAsString() == expected) {
            m_order.authorizationValid = true;
        }
        client->deleteLater();
    });
    connect(client, &SwHttpClient::errorOccurred, [this, client](int) {
        client->deleteLater();
    });
    (void)client->get("http://127.0.0.1:" +
                      SwString::number(static_cast<int>(m_httpPort)) +
                      "/.well-known/acme-challenge/" + m_order.token);
}

void HttpAcmeSelfTestRunner::probeHttp_(const std::function<void(bool)>& done) {
    SwHttpClient* client = new SwHttpClient(this);
    connect(client, &SwHttpClient::finished, [this, client, done](const SwByteArray&) {
        const bool ok = client->statusCode() == 200 &&
                        client->responseBodyAsString() == ("http:" + SwString::number(static_cast<int>(m_httpPort)));
        client->deleteLater();
        done(ok);
    });
    connect(client, &SwHttpClient::errorOccurred, [client, done](int) {
        client->deleteLater();
        done(false);
    });
    (void)client->get("http://127.0.0.1:" + SwString::number(static_cast<int>(m_httpPort)) + "/hello");
}

void HttpAcmeSelfTestRunner::probeHttps_(const std::function<void(bool)>& done) {
    SwHttpClient* client = new SwHttpClient(this);
    client->setTrustedCaFile(m_caCertPath);
    connect(client, &SwHttpClient::finished, [this, client, done](const SwByteArray&) {
        const bool ok = client->statusCode() == 200 &&
                        client->responseBodyAsString() == ("https:" + SwString::number(static_cast<int>(m_httpsPort)));
        client->deleteLater();
        done(ok);
    });
    connect(client, &SwHttpClient::errorOccurred, [client, done](int) {
        client->deleteLater();
        done(false);
    });
    (void)client->get("https://localhost:" + SwString::number(static_cast<int>(m_httpsPort)) + "/hello");
}

void HttpAcmeSelfTestRunner::finish_(bool success, const SwString& reason) {
    if (!reason.isEmpty()) {
        if (success) {
            swDebug() << "[HttpAcmeSelfTest] " << reason;
        } else {
            swError() << "[HttpAcmeSelfTest] " << reason;
        }
    }
    m_timeout.stop();
    m_manager.stop();
    m_fakeAcmeApp.close();
    m_managedApp.close();
    m_app->exit(success ? 0 : 1);
}

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);
    HttpAcmeSelfTestRunner runner(&app);
    SwTimer::singleShot(0, &runner, &HttpAcmeSelfTestRunner::start);
    return app.exec();
}

static bool generateEcPrivateKeyPem_(SwString& outPem) {
    outPem.clear();
    EVP_PKEY* pkey = EVP_PKEY_new();
    EC_KEY* ecKey = nullptr;
    BIO* bio = nullptr;
    if (!pkey) {
        return false;
    }

    ecKey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!ecKey || EC_KEY_generate_key(ecKey) != 1 || EVP_PKEY_assign_EC_KEY(pkey, ecKey) != 1) {
        if (ecKey) EC_KEY_free(ecKey);
        EVP_PKEY_free(pkey);
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
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(commonName.data()),
                               static_cast<int>(commonName.size()), -1, 0);
    X509_set_issuer_name(cert, name);

    X509_EXTENSION* basicConstraints = X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, "critical,CA:TRUE");
    X509_EXTENSION* keyUsage = X509V3_EXT_conf_nid(nullptr, nullptr, NID_key_usage, "critical,keyCertSign,cRLSign");
    if (!basicConstraints || !keyUsage ||
        X509_add_ext(cert, basicConstraints, -1) != 1 ||
        X509_add_ext(cert, keyUsage, -1) != 1 ||
        X509_sign(cert, pkey, EVP_sha256()) <= 0) {
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
    if (!basicConstraints || !keyUsage ||
        X509_add_ext(cert, basicConstraints, -1) != 1 ||
        X509_add_ext(cert, keyUsage, -1) != 1 ||
        X509_sign(cert, caKey, EVP_sha256()) <= 0) {
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
