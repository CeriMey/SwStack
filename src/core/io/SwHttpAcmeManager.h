#pragma once

#include "SwDir.h"
#include "SwFile.h"
#include "SwHttpApp.h"
#include "SwHttpClient.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwMutex.h"
#include "SwTimer.h"

#include <cstdio>
#include <ctime>
#include <functional>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

static constexpr const char* kSwLogCategory_SwHttpAcmeManager = "sw.core.io.swhttpacmemanager";

struct SwAcmeConfig {
    SwString directoryUrl = "https://acme-v02.api.letsencrypt.org/directory";
    SwString domain;
    SwList<SwString> subjectAlternativeNames;
    SwString contactEmail;
    SwString storageDir = "acme";
    SwString trustedCaFile;
    uint16_t httpPort = 80;
    uint16_t httpsPort = 443;
    int renewBeforeDays = 30;
    int retryDelayMs = 15 * 1000;
    int pollIntervalMs = 1000;
    int maxPollAttempts = 60;
};

inline SwList<SwString> swAcmeRequestedDomains(const SwAcmeConfig& config) {
    SwList<SwString> domains;
    const SwString primaryDomain = swMailDetail::normalizeDomain(config.domain);
    if (!primaryDomain.isEmpty()) {
        domains.append(primaryDomain);
    }
    for (std::size_t i = 0; i < config.subjectAlternativeNames.size(); ++i) {
        const SwString candidate = swMailDetail::normalizeDomain(config.subjectAlternativeNames[i]);
        if (!candidate.isEmpty() && !domains.contains(candidate)) {
            domains.append(candidate);
        }
    }
    return domains;
}

inline SwAcmeConfig swBuildAcmeConfig(const SwDomainTlsConfig& tlsConfig, const SwMailConfig* mailConfig = nullptr) {
    SwAcmeConfig config;
    config.directoryUrl = tlsConfig.acmeDirectoryUrl;
    config.domain = swMailDetail::normalizeDomain(tlsConfig.domain);
    config.subjectAlternativeNames = tlsConfig.subjectAlternativeNames;
    config.contactEmail = tlsConfig.contactEmail;
    config.storageDir = tlsConfig.storageDir;
    config.trustedCaFile = tlsConfig.trustedCaFile;
    config.httpPort = tlsConfig.httpPort;
    config.httpsPort = tlsConfig.httpsPort;

    if (mailConfig) {
        const SwString mailHost = swMailDetail::normalizeMailHost(mailConfig->mailHost, mailConfig->domain);
        if (!mailHost.isEmpty()) {
            bool exists = false;
            for (std::size_t i = 0; i < config.subjectAlternativeNames.size(); ++i) {
                if (swMailDetail::normalizeDomain(config.subjectAlternativeNames[i]) ==
                    swMailDetail::normalizeDomain(mailHost)) {
                    exists = true;
                    break;
                }
            }
            if (!exists && swMailDetail::normalizeDomain(mailHost) != config.domain) {
                config.subjectAlternativeNames.append(mailHost);
            }
        }
    }
    return config;
}

class SwHttpAcmeManager : public SwObject {
    SW_OBJECT(SwHttpAcmeManager, SwObject)

public:
    explicit SwHttpAcmeManager(const SwAcmeConfig& config = SwAcmeConfig(), SwObject* parent = nullptr);
    ~SwHttpAcmeManager() override;

    void setConfig(const SwAcmeConfig& config);
    const SwAcmeConfig& config() const;
    void setCertificateActivationHandler(const std::function<bool(const SwString&, const SwString&)>& handler);

    bool start(SwHttpApp* app);
    void stop();
    void renewNow();

    SwString certificatePath() const;
    SwString keyPath() const;

signals:
    DECLARE_SIGNAL(certificateUpdated, const SwString&, const SwString&)
    DECLARE_SIGNAL(errorOccurred, const SwString&)

private slots:
    void onRenewTimer_();

private:
    struct HttpResult {
        bool transportOk = false;
        int transportError = 0;
        int statusCode = 0;
        SwString reasonPhrase;
        SwMap<SwString, SwString> headers;
        SwString headersText;
        SwByteArray body;
    };

    struct JwkMaterial {
        SwJsonObject jwk;
        SwString thumbprint;
        bool valid = false;
    };

    void installPreRouteHandler_();
    bool handlePreRoute_(const SwHttpRequest& request, SwHttpResponse& response);
    bool isActiveOperation_(uint64_t serial) const;
    void completeOperationSuccess_(uint64_t serial);
    void failOperation_(uint64_t serial, const SwString& message);
    void scheduleRetry_();
    void schedulePoll_(uint64_t serial, const std::function<void()>& callback);
    void scheduleRenewFromStoredCertificate_();

    void dispatchHttpRequest_(SwHttpClient::Method method,
                              const SwString& url,
                              const SwByteArray& body,
                              const SwString& contentType,
                              const std::function<void(const HttpResult&)>& handler);
    void ensureNonceThen_(uint64_t serial, const std::function<void()>& callback);
    void postJose_(uint64_t serial,
                   const SwString& url,
                   const SwByteArray& payload,
                   bool useJwk,
                   const std::function<void(const HttpResult&)>& handler,
                   int badNonceRetries = 1);

    void loadDirectory_(uint64_t serial);
    void ensureAccount_(uint64_t serial);
    void createOrder_(uint64_t serial);
    void fetchAuthorization_(uint64_t serial);
    void acknowledgeChallenge_(uint64_t serial);
    void pollAuthorization_(uint64_t serial);
    void finalizeOrder_(uint64_t serial);
    void pollOrder_(uint64_t serial);
    void downloadCertificate_(uint64_t serial);

    bool installCertificate_(const SwString& certificatePem);
    bool enableHttpsFromStorage_();
    bool hasUsableStoredCertificate_();
    void resetOrderState_();
    void setChallenge_(const SwString& token, const SwString& keyAuthorization);
    void clearChallenge_();
    void updateNonceFromResult_(const HttpResult& result);
    bool isBadNonceResult_(const HttpResult& result) const;
    bool buildJoseBody_(const SwString& url,
                        const SwByteArray& payload,
                        bool useJwk,
                        SwString& outBody);
    bool parseJsonObject_(const SwByteArray& bytes, SwJsonObject& outObject) const;
    static SwString headerValue_(const SwMap<SwString, SwString>& headers, const SwString& key);
    static SwString acmeProblemDescription_(const SwJsonObject& object);

    bool ensureStorageDir_() const;
    SwString storageRoot_() const;
    SwString accountKeyFilePath_() const;
    SwString accountStateFilePath_() const;
    SwString certificateKeyFilePath_() const;
    SwString certificateFilePath_() const;
    SwString certificateMetaFilePath_() const;

    bool loadAccountState_();
    bool saveAccountState_() const;
    bool saveCertificateMetadata_(long long remainingSeconds) const;
    bool ensureAccountKeyPem_();
    bool ensureCertificateKeyPem_();

    static bool readTextFile_(const SwString& path, SwString& outText);
    static bool writeTextFileAtomic_(const SwString& path, const SwString& text);
    static void removeFile_(const SwString& filePath);
    static bool renameFileAtomic_(const SwString& sourcePath, const SwString& destinationPath);

    static SwString base64UrlEncode_(const SwByteArray& bytes);
    static SwByteArray base64UrlDecode_(const SwString& input, bool* ok = nullptr);
    static bool generateEcPrivateKeyPem_(SwString& outPem);
    static EVP_PKEY* loadPrivateKeyFromPem_(const SwString& pem);
    static X509* loadCertificateFromPem_(const SwString& pem);
    static JwkMaterial buildJwkMaterialFromPrivateKeyPem_(const SwString& pem);
    static bool signEs256_(const SwString& privateKeyPem,
                           const SwString& signingInput,
                           SwByteArray& outSignature);
    static bool buildCsrDer_(const SwString& privateKeyPem,
                             const SwList<SwString>& domains,
                             SwByteArray& outDer);
    static bool isCertificatePairUsable_(const SwString& certificatePem, const SwString& privateKeyPem);
    static long long remainingCertificateLifetimeSeconds_(const SwString& certificatePem);
    int maxPollAttempts_() const;
    void emitError_(const SwString& message);

    SwAcmeConfig m_config;
    SwHttpApp* m_app = nullptr;
    bool m_started = false;
    bool m_preRouteInstalled = false;

    SwTimer m_renewTimer;
    std::function<bool(const SwString&, const SwString&)> m_certificateActivationHandler;
    bool m_operationInProgress = false;
    bool m_pendingRenew = false;
    uint64_t m_operationSerial = 0;
    uint64_t m_activeOperationSerial = 0;

    SwString m_newNonceUrl;
    SwString m_newAccountUrl;
    SwString m_newOrderUrl;

    SwString m_nonce;
    SwString m_accountUrl;
    SwString m_orderUrl;
    SwString m_authorizationUrl;
    SwList<SwString> m_authorizationUrls;
    std::size_t m_authorizationIndex = 0;
    SwString m_finalizeUrl;
    SwString m_certificateUrl;
    SwString m_challengeUrl;
    SwString m_challengeToken;
    int m_pollAttempts = 0;

    SwString m_accountKeyPem;
    SwString m_certificateKeyPem;

    mutable SwMutex m_challengeMutex;
    SwString m_activeChallengeToken;
    SwString m_activeChallengeKeyAuthorization;
};

inline SwHttpAcmeManager::SwHttpAcmeManager(const SwAcmeConfig& config, SwObject* parent)
    : SwObject(parent)
    , m_config(config)
    , m_renewTimer(this) {
    m_renewTimer.setSingleShot(true);
    connect(&m_renewTimer, &SwTimer::timeout, this, &SwHttpAcmeManager::onRenewTimer_);
}

inline SwHttpAcmeManager::~SwHttpAcmeManager() {
    stop();
}

inline void SwHttpAcmeManager::setConfig(const SwAcmeConfig& config) {
    m_config = config;
}

inline const SwAcmeConfig& SwHttpAcmeManager::config() const {
    return m_config;
}

inline void SwHttpAcmeManager::setCertificateActivationHandler(
    const std::function<bool(const SwString&, const SwString&)>& handler) {
    m_certificateActivationHandler = handler;
}

inline SwString SwHttpAcmeManager::certificatePath() const {
    return certificateFilePath_();
}

inline SwString SwHttpAcmeManager::keyPath() const {
    return certificateKeyFilePath_();
}

inline void SwHttpAcmeManager::onRenewTimer_() {
    if (!m_started) {
        return;
    }

    SwString certPem;
    if (!readTextFile_(certificateFilePath_(), certPem)) {
        renewNow();
        return;
    }

    const long long remainingSeconds = remainingCertificateLifetimeSeconds_(certPem);
    const long long renewBeforeSeconds = static_cast<long long>(m_config.renewBeforeDays) * 24LL * 60LL * 60LL;
    if (remainingSeconds > renewBeforeSeconds) {
        scheduleRenewFromStoredCertificate_();
        return;
    }
    renewNow();
}

inline void SwHttpAcmeManager::installPreRouteHandler_() {
    if (m_preRouteInstalled || !m_app) {
        return;
    }

    SwHttpAcmeManager* self = this;
    m_app->addPreRouteHandler([self](const SwHttpRequest& request, SwHttpResponse& response) {
        if (!SwObject::isLive(self)) {
            return false;
        }
        return self->handlePreRoute_(request, response);
    });
    m_preRouteInstalled = true;
}

inline bool SwHttpAcmeManager::handlePreRoute_(const SwHttpRequest& request, SwHttpResponse& response) {
    static const SwString kPrefix = "/.well-known/acme-challenge/";
    SwString challengeSource = request.path;
    int prefixPos = challengeSource.indexOf(kPrefix);
    if (prefixPos < 0) {
        challengeSource = request.target;
        prefixPos = challengeSource.indexOf(kPrefix);
    }
    if (prefixPos < 0) {
        return false;
    }

    if (!m_started) {
        return false;
    }
    if (request.method.toUpper() != "GET" && request.method.toUpper() != "HEAD") {
        response = swHttpTextResponse(405, "Method Not Allowed");
        response.closeConnection = !request.keepAlive;
        return true;
    }

    SwMutexLocker locker(&m_challengeMutex);
    SwString token = challengeSource.mid(prefixPos + static_cast<int>(kPrefix.size()));
    const int queryPos = token.indexOf("?");
    if (queryPos >= 0) {
        token = token.left(queryPos);
    }
    if (token.endsWith("/")) {
        token.chop(1);
    }
    if (m_activeChallengeToken.isEmpty()) {
        return false;
    }
    if (token != m_activeChallengeToken) {
        return false;
    }

    response = swHttpTextResponse(200, m_activeChallengeKeyAuthorization);
    response.headers["content-type"] = "text/plain";
    response.closeConnection = !request.keepAlive;
    if (request.method.toUpper() == "HEAD") {
        response.headOnly = true;
        response.body.clear();
    }
    return true;
}

inline bool SwHttpAcmeManager::isActiveOperation_(uint64_t serial) const {
    return m_started && m_app && serial != 0 && serial == m_activeOperationSerial;
}

inline bool SwHttpAcmeManager::start(SwHttpApp* app) {
    if (!app) {
        emitError_("SwHttpAcmeManager requires a valid SwHttpApp instance");
        return false;
    }
    if (m_started && m_app == app) {
        return true;
    }

    stop();
    if (m_config.domain.trimmed().isEmpty() || m_config.storageDir.trimmed().isEmpty() ||
        m_config.directoryUrl.trimmed().isEmpty() || m_config.httpPort == 0 || m_config.httpsPort == 0) {
        emitError_("Invalid ACME configuration");
        return false;
    }
    if (!ensureStorageDir_()) {
        emitError_("Unable to create ACME storage directory: " + storageRoot_());
        return false;
    }

    m_app = app;
    m_started = true;
    installPreRouteHandler_();
    loadAccountState_();
    ensureAccountKeyPem_();

    if (!m_app->listenHttp(m_config.httpPort)) {
        m_started = false;
        m_app = nullptr;
        emitError_("Unable to listen on ACME HTTP port " + SwString::number(static_cast<int>(m_config.httpPort)));
        return false;
    }

    if (hasUsableStoredCertificate_()) {
        if (enableHttpsFromStorage_()) {
            scheduleRenewFromStoredCertificate_();
        } else {
            scheduleRetry_();
        }
        return true;
    }

    renewNow();
    return true;
}

inline void SwHttpAcmeManager::stop() {
    m_started = false;
    m_app = nullptr;
    m_operationInProgress = false;
    m_pendingRenew = false;
    m_activeOperationSerial = 0;
    ++m_operationSerial;
    m_renewTimer.stop();
    clearChallenge_();
    resetOrderState_();
}

inline void SwHttpAcmeManager::renewNow() {
    if (!m_started || !m_app) {
        return;
    }
    if (m_operationInProgress) {
        m_pendingRenew = true;
        return;
    }

    m_pendingRenew = false;
    m_operationInProgress = true;
    clearChallenge_();
    resetOrderState_();
    ++m_operationSerial;
    m_activeOperationSerial = m_operationSerial;
    loadDirectory_(m_activeOperationSerial);
}

inline void SwHttpAcmeManager::completeOperationSuccess_(uint64_t serial) {
    if (!isActiveOperation_(serial)) {
        return;
    }

    m_operationInProgress = false;
    m_activeOperationSerial = 0;
    resetOrderState_();
    clearChallenge_();
    scheduleRenewFromStoredCertificate_();
    if (m_pendingRenew) {
        m_pendingRenew = false;
        SwHttpAcmeManager* self = this;
        SwTimer::singleShot(0, [self]() {
            if (SwObject::isLive(self) && self->m_started) {
                self->renewNow();
            }
        });
    }
}

inline void SwHttpAcmeManager::failOperation_(uint64_t serial, const SwString& message) {
    if (!isActiveOperation_(serial) && !(serial == 0 && m_started)) {
        return;
    }

    m_operationInProgress = false;
    m_activeOperationSerial = 0;
    resetOrderState_();
    clearChallenge_();
    emitError_(message);
    scheduleRetry_();
}

inline void SwHttpAcmeManager::scheduleRetry_() {
    if (!m_started) {
        return;
    }

    const int delayMs = (m_config.retryDelayMs > 0) ? m_config.retryDelayMs : 1000;
    SwHttpAcmeManager* self = this;
    SwTimer::singleShot(delayMs, [self]() {
        if (SwObject::isLive(self) && self->m_started) {
            self->renewNow();
        }
    });
}

inline void SwHttpAcmeManager::schedulePoll_(uint64_t serial, const std::function<void()>& callback) {
    const int delayMs = (m_config.pollIntervalMs > 0) ? m_config.pollIntervalMs : 1000;
    SwHttpAcmeManager* self = this;
    SwTimer::singleShot(delayMs, [self, serial, callback]() {
        if (!SwObject::isLive(self) || !self->isActiveOperation_(serial)) {
            return;
        }
        if (callback) {
            callback();
        }
    });
}

inline void SwHttpAcmeManager::scheduleRenewFromStoredCertificate_() {
    if (!m_started) {
        return;
    }

    SwString certPem;
    if (!readTextFile_(certificateFilePath_(), certPem)) {
        scheduleRetry_();
        return;
    }

    const long long remainingSeconds = remainingCertificateLifetimeSeconds_(certPem);
    if (remainingSeconds <= 0) {
        renewNow();
        return;
    }

    long long renewBeforeSeconds = static_cast<long long>(m_config.renewBeforeDays) * 24LL * 60LL * 60LL;
    if (renewBeforeSeconds < 0) {
        renewBeforeSeconds = 0;
    }

    long long delaySeconds = remainingSeconds - renewBeforeSeconds;
    if (delaySeconds <= 0) {
        SwHttpAcmeManager* self = this;
        SwTimer::singleShot(0, [self]() {
            if (SwObject::isLive(self) && self->m_started) {
                self->renewNow();
            }
        });
        return;
    }

    long long delayMs = delaySeconds * 1000LL;
    const long long maxDelayMs = 2147483647LL;
    if (delayMs > maxDelayMs) {
        delayMs = maxDelayMs;
    }
    if (delayMs < 1) {
        delayMs = 1;
    }

    m_renewTimer.stop();
    m_renewTimer.start(static_cast<int>(delayMs));
}

inline void SwHttpAcmeManager::dispatchHttpRequest_(SwHttpClient::Method method,
                                                    const SwString& url,
                                                    const SwByteArray& body,
                                                    const SwString& contentType,
                                                    const std::function<void(const HttpResult&)>& handler) {
    SwHttpClient* client = new SwHttpClient(this);
    if (!m_config.trustedCaFile.isEmpty()) {
        client->setTrustedCaFile(m_config.trustedCaFile);
    }

    auto finish = [this, client, handler](bool transportOk, int transportError) {
        HttpResult result;
        result.transportOk = transportOk;
        result.transportError = transportError;
        if (transportOk) {
            result.statusCode = client->statusCode();
            result.reasonPhrase = client->reasonPhrase();
            result.headersText = client->responseHeaders();
            result.headers = client->responseHeaderMap();
            result.body = client->responseBody();
        }

        client->disconnectAllSlots();
        client->deleteLater();
        if (handler) {
            handler(result);
        }
    };

    connect(client, &SwHttpClient::finished, [finish](const SwByteArray&) { finish(true, 0); });
    connect(client, &SwHttpClient::errorOccurred, [finish](int err) { finish(false, err); });
    (void)client->request(method, url, body, contentType);
}

inline void SwHttpAcmeManager::ensureNonceThen_(uint64_t serial, const std::function<void()>& callback) {
    if (!isActiveOperation_(serial)) {
        return;
    }
    if (!m_nonce.isEmpty()) {
        if (callback) {
            callback();
        }
        return;
    }

    dispatchHttpRequest_(SwHttpClient::Method::Head, m_newNonceUrl, SwByteArray(), SwString(),
                         [this, serial, callback](const HttpResult& result) {
                             if (!isActiveOperation_(serial)) {
                                 return;
                             }
                             if (!result.transportOk) {
                                 failOperation_(serial, "ACME newNonce request failed");
                                 return;
                             }
                             updateNonceFromResult_(result);
                             if (m_nonce.isEmpty()) {
                                 failOperation_(serial, "ACME server did not return a replay nonce");
                                 return;
                             }
                             if (callback) {
                                 callback();
                             }
                         });
}

inline void SwHttpAcmeManager::postJose_(uint64_t serial,
                                         const SwString& url,
                                         const SwByteArray& payload,
                                         bool useJwk,
                                         const std::function<void(const HttpResult&)>& handler,
                                         int badNonceRetries) {
    if (!isActiveOperation_(serial)) {
        return;
    }
    if (!ensureAccountKeyPem_()) {
        failOperation_(serial, "Unable to initialize ACME account key");
        return;
    }

    ensureNonceThen_(serial, [this, serial, url, payload, useJwk, handler, badNonceRetries]() {
        if (!isActiveOperation_(serial)) {
            return;
        }

        SwString joseBody;
        if (!buildJoseBody_(url, payload, useJwk, joseBody)) {
            failOperation_(serial, "Unable to build ACME JWS request");
            return;
        }

        dispatchHttpRequest_(SwHttpClient::Method::Post,
                             url,
                             SwByteArray(joseBody.data(), static_cast<size_t>(joseBody.size())),
                             "application/jose+json",
                             [this, serial, url, payload, useJwk, handler, badNonceRetries](const HttpResult& result) {
                                 if (!isActiveOperation_(serial)) {
                                     return;
                                 }
                                 updateNonceFromResult_(result);
                                 if (isBadNonceResult_(result) && badNonceRetries > 0) {
                                     m_nonce.clear();
                                     postJose_(serial, url, payload, useJwk, handler, badNonceRetries - 1);
                                     return;
                                 }
                                 if (handler) {
                                     handler(result);
                                 }
                             });
    });
}

inline void SwHttpAcmeManager::loadDirectory_(uint64_t serial) {
    dispatchHttpRequest_(SwHttpClient::Method::Get, m_config.directoryUrl, SwByteArray(), SwString(),
                         [this, serial](const HttpResult& result) {
                             if (!isActiveOperation_(serial)) {
                                 return;
                             }
                             if (!result.transportOk || result.statusCode < 200 || result.statusCode >= 300) {
                                 failOperation_(serial, "Unable to load ACME directory");
                                 return;
                             }

                             SwJsonObject object;
                             if (!parseJsonObject_(result.body, object)) {
                                 failOperation_(serial, "Invalid ACME directory JSON");
                                 return;
                             }

                             m_newNonceUrl = object.value("newNonce").toString().c_str();
                             m_newAccountUrl = object.value("newAccount").toString().c_str();
                             m_newOrderUrl = object.value("newOrder").toString().c_str();
                             if (m_newNonceUrl.isEmpty() || m_newAccountUrl.isEmpty() || m_newOrderUrl.isEmpty()) {
                                 failOperation_(serial, "ACME directory is missing mandatory endpoints");
                                 return;
                             }

                             m_nonce.clear();
                             ensureAccount_(serial);
                         });
}

inline void SwHttpAcmeManager::ensureAccount_(uint64_t serial) {
    if (!isActiveOperation_(serial)) {
        return;
    }
    if (!m_accountUrl.isEmpty()) {
        createOrder_(serial);
        return;
    }

    SwJsonObject payload;
    payload["termsOfServiceAgreed"] = true;
    if (!m_config.contactEmail.trimmed().isEmpty()) {
        SwJsonArray contacts;
        contacts.append(SwJsonValue(("mailto:" + m_config.contactEmail.trimmed()).toStdString()));
        payload["contact"] = contacts;
    }

    const SwString payloadJson = SwJsonDocument(payload).toJson(SwJsonDocument::JsonFormat::Compact);
    postJose_(serial, m_newAccountUrl, SwByteArray(payloadJson.toStdString()), true,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode != 200 && result.statusCode != 201)) {
                      SwString detail = "ACME account creation failed";
                      if (!result.transportOk) {
                          detail += " (transport error " + SwString::number(result.transportError) + ")";
                      } else {
                          detail += " (HTTP " + SwString::number(result.statusCode) + ")";
                          if (!result.body.isEmpty()) {
                              detail += " body=" + SwString::fromUtf8(result.body.constData(), static_cast<int>(result.body.size())).left(512);
                          }
                      }
                      failOperation_(serial, detail);
                      return;
                  }

                  const SwString location = headerValue_(result.headers, "location");
                  if (location.isEmpty()) {
                      failOperation_(serial, "ACME account response did not provide an account URL");
                      return;
                  }
                  m_accountUrl = location;
                  saveAccountState_();
                  createOrder_(serial);
              });
}

inline void SwHttpAcmeManager::createOrder_(uint64_t serial) {
    SwJsonArray identifiers;
    const SwList<SwString> domains = swAcmeRequestedDomains(m_config);
    if (domains.isEmpty()) {
        failOperation_(serial, "ACME configuration is missing a domain");
        return;
    }
    for (std::size_t i = 0; i < domains.size(); ++i) {
        SwJsonObject identifier;
        identifier["type"] = "dns";
        identifier["value"] = domains[i].toStdString();
        identifiers.append(identifier);
    }

    SwJsonObject payload;
    payload["identifiers"] = identifiers;

    const SwString payloadJson = SwJsonDocument(payload).toJson(SwJsonDocument::JsonFormat::Compact);
    postJose_(serial, m_newOrderUrl, SwByteArray(payloadJson.toStdString()), false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode != 200 && result.statusCode != 201)) {
                      SwString detail = "ACME newOrder request failed";
                      if (!result.transportOk) {
                          detail += " (transport error " + SwString::number(result.transportError) + ")";
                      } else {
                          detail += " (HTTP " + SwString::number(result.statusCode) + ")";
                          if (!result.body.isEmpty()) {
                              detail += " body=" + SwString::fromUtf8(result.body.constData(), static_cast<int>(result.body.size())).left(512);
                          }
                      }
                      failOperation_(serial, detail);
                      return;
                  }

                  SwJsonObject object;
                  if (!parseJsonObject_(result.body, object)) {
                      failOperation_(serial, "Invalid ACME order JSON");
                      return;
                  }

                  m_orderUrl = headerValue_(result.headers, "location");
                  m_finalizeUrl = object.value("finalize").toString().c_str();
                  m_certificateUrl = object.contains("certificate") ? object.value("certificate").toString().c_str() : SwString();

                  const SwJsonArray authorizations = object.value("authorizations").toArray();
                  if (m_orderUrl.isEmpty() || m_finalizeUrl.isEmpty() || authorizations.isEmpty()) {
                      failOperation_(serial, "ACME order response is incomplete");
                      return;
                  }

                  m_authorizationUrls.clear();
                  for (std::size_t i = 0; i < authorizations.size(); ++i) {
                      const SwString authorizationUrl = authorizations[i].toString().c_str();
                      if (!authorizationUrl.isEmpty()) {
                          m_authorizationUrls.append(authorizationUrl);
                      }
                  }
                  if (m_authorizationUrls.isEmpty()) {
                      failOperation_(serial, "ACME order did not return usable authorizations");
                      return;
                  }
                  m_authorizationIndex = 0;
                  m_authorizationUrl = m_authorizationUrls[0];
                  fetchAuthorization_(serial);
              });
}

inline void SwHttpAcmeManager::fetchAuthorization_(uint64_t serial) {
    postJose_(serial, m_authorizationUrl, SwByteArray(), false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode < 200 || result.statusCode >= 300)) {
                      failOperation_(serial, "ACME authorization lookup failed");
                      return;
                  }

                  SwJsonObject object;
                  if (!parseJsonObject_(result.body, object)) {
                      failOperation_(serial, "Invalid ACME authorization JSON");
                      return;
                  }

                  const SwString authStatus = object.value("status").toString().c_str();
                  if (authStatus == "valid") {
                      clearChallenge_();
                      if (m_authorizationIndex + 1 < m_authorizationUrls.size()) {
                          ++m_authorizationIndex;
                          m_authorizationUrl = m_authorizationUrls[m_authorizationIndex];
                          fetchAuthorization_(serial);
                          return;
                      }
                      finalizeOrder_(serial);
                      return;
                  }

                  const SwJsonArray challenges = object.value("challenges").toArray();
                  SwString challengeUrl;
                  SwString token;
                  for (size_t i = 0; i < challenges.size(); ++i) {
                      const SwJsonObject challenge = challenges[i].toObject();
                      if (challenge.value("type").toString() == "http-01") {
                          challengeUrl = challenge.value("url").toString().c_str();
                          token = challenge.value("token").toString().c_str();
                          break;
                      }
                  }

                  if (challengeUrl.isEmpty() || token.isEmpty()) {
                      failOperation_(serial, "ACME authorization did not offer an HTTP-01 challenge");
                      return;
                  }

                  m_challengeUrl = challengeUrl;
                  m_challengeToken = token;

                  const JwkMaterial jwk = buildJwkMaterialFromPrivateKeyPem_(m_accountKeyPem);
                  if (!jwk.valid) {
                      failOperation_(serial, "Unable to derive ACME account thumbprint");
                      return;
                  }

                  setChallenge_(token, token + "." + jwk.thumbprint);
                  acknowledgeChallenge_(serial);
              });
}

inline void SwHttpAcmeManager::acknowledgeChallenge_(uint64_t serial) {
    const SwString payloadJson = SwJsonDocument(SwJsonObject()).toJson(SwJsonDocument::JsonFormat::Compact);
    postJose_(serial,
              m_challengeUrl,
              SwByteArray(payloadJson.data(), static_cast<size_t>(payloadJson.size())),
              false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode < 200 || result.statusCode >= 300)) {
                      failOperation_(serial, "ACME challenge acknowledgement failed");
                      return;
                  }

                  m_pollAttempts = 0;
                  schedulePoll_(serial, [this, serial]() { pollAuthorization_(serial); });
              });
}

inline void SwHttpAcmeManager::pollAuthorization_(uint64_t serial) {
    postJose_(serial, m_authorizationUrl, SwByteArray(), false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode < 200 || result.statusCode >= 300)) {
                      failOperation_(serial, "ACME authorization polling failed");
                      return;
                  }

                  SwJsonObject object;
                  if (!parseJsonObject_(result.body, object)) {
                      failOperation_(serial, "Invalid ACME authorization polling JSON");
                      return;
                  }

                  const SwString authStatus = object.value("status").toString().c_str();
                  if (authStatus == "valid") {
                      clearChallenge_();
                      if (m_authorizationIndex + 1 < m_authorizationUrls.size()) {
                          ++m_authorizationIndex;
                          m_authorizationUrl = m_authorizationUrls[m_authorizationIndex];
                          fetchAuthorization_(serial);
                          return;
                      }
                      finalizeOrder_(serial);
                      return;
                  }
                  if (authStatus == "invalid") {
                      clearChallenge_();
                      const SwString problem = acmeProblemDescription_(object);
                      failOperation_(serial, problem.isEmpty() ? SwString("ACME authorization became invalid")
                                                              : SwString("ACME authorization became invalid: ") +
                                                                    problem);
                      return;
                  }

                  ++m_pollAttempts;
                  if (m_pollAttempts >= maxPollAttempts_()) {
                      clearChallenge_();
                      failOperation_(serial, "ACME authorization polling timed out");
                      return;
                  }

                  schedulePoll_(serial, [this, serial]() { pollAuthorization_(serial); });
              });
}

inline void SwHttpAcmeManager::finalizeOrder_(uint64_t serial) {
    if (!ensureCertificateKeyPem_()) {
        failOperation_(serial, "Unable to initialize certificate private key");
        return;
    }

    SwByteArray csrDer;
    if (!buildCsrDer_(m_certificateKeyPem, swAcmeRequestedDomains(m_config), csrDer)) {
        failOperation_(serial, "Unable to build certificate signing request");
        return;
    }

    SwJsonObject payload;
    payload["csr"] = base64UrlEncode_(csrDer).toStdString();
    const SwString payloadJson = SwJsonDocument(payload).toJson(SwJsonDocument::JsonFormat::Compact);
    postJose_(serial,
              m_finalizeUrl,
              SwByteArray(payloadJson.data(), static_cast<size_t>(payloadJson.size())),
              false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode < 200 || result.statusCode >= 300)) {
                      failOperation_(serial, "ACME order finalization failed");
                      return;
                  }

                  SwJsonObject object;
                  if (!parseJsonObject_(result.body, object)) {
                      failOperation_(serial, "Invalid ACME finalize response JSON");
                      return;
                  }
                  if (object.contains("certificate")) {
                      m_certificateUrl = object.value("certificate").toString().c_str();
                  }

                  m_pollAttempts = 0;
                  schedulePoll_(serial, [this, serial]() { pollOrder_(serial); });
              });
}

inline void SwHttpAcmeManager::pollOrder_(uint64_t serial) {
    postJose_(serial, m_orderUrl, SwByteArray(), false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode < 200 || result.statusCode >= 300)) {
                      failOperation_(serial, "ACME order polling failed");
                      return;
                  }

                  SwJsonObject object;
                  if (!parseJsonObject_(result.body, object)) {
                      failOperation_(serial, "Invalid ACME order polling JSON");
                      return;
                  }

                  const SwString orderStatus = object.value("status").toString().c_str();
                  if (object.contains("certificate")) {
                      m_certificateUrl = object.value("certificate").toString().c_str();
                  }

                  if (orderStatus == "valid" && !m_certificateUrl.isEmpty()) {
                      downloadCertificate_(serial);
                      return;
                  }
                  if (orderStatus == "invalid") {
                      const SwString problem = acmeProblemDescription_(object);
                      failOperation_(serial, problem.isEmpty() ? SwString("ACME order became invalid")
                                                              : SwString("ACME order became invalid: ") + problem);
                      return;
                  }

                  ++m_pollAttempts;
                  if (m_pollAttempts >= maxPollAttempts_()) {
                      failOperation_(serial, "ACME order polling timed out");
                      return;
                  }

                  schedulePoll_(serial, [this, serial]() { pollOrder_(serial); });
              });
}

inline void SwHttpAcmeManager::downloadCertificate_(uint64_t serial) {
    if (m_certificateUrl.isEmpty()) {
        failOperation_(serial, "ACME certificate URL is empty");
        return;
    }

    postJose_(serial, m_certificateUrl, SwByteArray(), false,
              [this, serial](const HttpResult& result) {
                  if (!isActiveOperation_(serial)) {
                      return;
                  }
                  if (!result.transportOk || (result.statusCode < 200 || result.statusCode >= 300)) {
                      failOperation_(serial, "ACME certificate download failed");
                      return;
                  }

                  const SwString certificatePem(result.body.toStdString());
                  if (!installCertificate_(certificatePem)) {
                      failOperation_(serial, "Failed to persist or activate downloaded certificate");
                      return;
                  }

                  emit certificateUpdated(certificateFilePath_(), certificateKeyFilePath_());
                  completeOperationSuccess_(serial);
              });
}

inline bool SwHttpAcmeManager::installCertificate_(const SwString& certificatePem) {
    if (!ensureCertificateKeyPem_() || !isCertificatePairUsable_(certificatePem, m_certificateKeyPem)) {
        return false;
    }

    const long long remainingSeconds = remainingCertificateLifetimeSeconds_(certificatePem);
    if (remainingSeconds <= 0) {
        return false;
    }
    if (!writeTextFileAtomic_(certificateFilePath_(), certificatePem) ||
        !writeTextFileAtomic_(certificateKeyFilePath_(), m_certificateKeyPem) ||
        !saveCertificateMetadata_(remainingSeconds) || !m_app) {
        return false;
    }

    if (m_certificateActivationHandler) {
        return m_certificateActivationHandler(certificateFilePath_(), certificateKeyFilePath_());
    }
    if (m_app->isHttpsListening() && m_app->httpsPort() == m_config.httpsPort) {
        return m_app->reloadHttpsCredentials(m_config.httpsPort, certificateFilePath_(), certificateKeyFilePath_());
    }
    return m_app->listenHttps(m_config.httpsPort, certificateFilePath_(), certificateKeyFilePath_());
}

inline bool SwHttpAcmeManager::enableHttpsFromStorage_() {
    if (!m_app) {
        return false;
    }
    if (m_certificateActivationHandler) {
        return m_certificateActivationHandler(certificateFilePath_(), certificateKeyFilePath_());
    }
    if (m_app->isHttpsListening() && m_app->httpsPort() == m_config.httpsPort) {
        return m_app->reloadHttpsCredentials(m_config.httpsPort, certificateFilePath_(), certificateKeyFilePath_());
    }
    return m_app->listenHttps(m_config.httpsPort, certificateFilePath_(), certificateKeyFilePath_());
}

inline bool SwHttpAcmeManager::hasUsableStoredCertificate_() {
    SwString certPem;
    SwString keyPem;
    return readTextFile_(certificateFilePath_(), certPem) &&
           readTextFile_(certificateKeyFilePath_(), keyPem) &&
           isCertificatePairUsable_(certPem, keyPem);
}

inline void SwHttpAcmeManager::resetOrderState_() {
    m_nonce.clear();
    m_orderUrl.clear();
    m_authorizationUrl.clear();
    m_authorizationUrls.clear();
    m_authorizationIndex = 0;
    m_finalizeUrl.clear();
    m_certificateUrl.clear();
    m_challengeUrl.clear();
    m_challengeToken.clear();
    m_pollAttempts = 0;
}

inline void SwHttpAcmeManager::setChallenge_(const SwString& token, const SwString& keyAuthorization) {
    SwMutexLocker locker(&m_challengeMutex);
    m_activeChallengeToken = token;
    m_activeChallengeKeyAuthorization = keyAuthorization;
}

inline void SwHttpAcmeManager::clearChallenge_() {
    SwMutexLocker locker(&m_challengeMutex);
    m_activeChallengeToken.clear();
    m_activeChallengeKeyAuthorization.clear();
}

inline void SwHttpAcmeManager::updateNonceFromResult_(const HttpResult& result) {
    const SwString replayNonce = headerValue_(result.headers, "replay-nonce");
    if (!replayNonce.isEmpty()) {
        m_nonce = replayNonce;
    }
}

inline bool SwHttpAcmeManager::isBadNonceResult_(const HttpResult& result) const {
    return result.statusCode == 400 && !result.body.isEmpty() && SwString(result.body.toStdString()).contains("badNonce");
}

inline bool SwHttpAcmeManager::parseJsonObject_(const SwByteArray& bytes, SwJsonObject& outObject) const {
    outObject = SwJsonObject();
    SwString error;
    SwJsonDocument doc = SwJsonDocument::fromJson(bytes.toStdString(), error);
    if (!error.isEmpty() || !doc.isObject()) {
        return false;
    }
    outObject = doc.object();
    return true;
}

inline SwString SwHttpAcmeManager::headerValue_(const SwMap<SwString, SwString>& headers, const SwString& key) {
    return headers.value(key.toLower(), SwString());
}

inline SwString SwHttpAcmeManager::acmeProblemDescription_(const SwJsonObject& object) {
    SwString description;

    const SwJsonObject identifier = object.value("identifier").toObject();
    const SwString identifierType = identifier.value("type").toString().c_str();
    const SwString identifierValue = identifier.value("value").toString().c_str();
    if (!identifierType.isEmpty() || !identifierValue.isEmpty()) {
        description += "identifier=";
        if (!identifierType.isEmpty()) {
            description += identifierType + ":";
        }
        description += identifierValue;
    }

    const SwJsonObject topError = object.value("error").toObject();
    if (!topError.isEmpty()) {
        const SwString errorType = topError.value("type").toString().c_str();
        const SwString errorDetail = topError.value("detail").toString().c_str();
        if (!errorType.isEmpty()) {
            if (!description.isEmpty()) {
                description += " ";
            }
            description += "errorType=" + errorType;
        }
        if (!errorDetail.isEmpty()) {
            if (!description.isEmpty()) {
                description += " ";
            }
            description += "errorDetail=" + errorDetail;
        }
    }

    const SwJsonArray challenges = object.value("challenges").toArray();
    for (size_t i = 0; i < challenges.size(); ++i) {
        const SwJsonObject challenge = challenges[i].toObject();
        const SwString challengeStatus = challenge.value("status").toString().c_str();
        if (challengeStatus != "invalid") {
            continue;
        }

        const SwString challengeType = challenge.value("type").toString().c_str();
        const SwString challengeUrl = challenge.value("url").toString().c_str();
        const SwJsonObject challengeError = challenge.value("error").toObject();
        const SwString challengeErrorType = challengeError.value("type").toString().c_str();
        const SwString challengeErrorDetail = challengeError.value("detail").toString().c_str();

        if (!challengeType.isEmpty()) {
            if (!description.isEmpty()) {
                description += " ";
            }
            description += "challengeType=" + challengeType;
        }
        if (!challengeUrl.isEmpty()) {
            if (!description.isEmpty()) {
                description += " ";
            }
            description += "challengeUrl=" + challengeUrl;
        }
        if (!challengeErrorType.isEmpty()) {
            if (!description.isEmpty()) {
                description += " ";
            }
            description += "challengeErrorType=" + challengeErrorType;
        }
        if (!challengeErrorDetail.isEmpty()) {
            if (!description.isEmpty()) {
                description += " ";
            }
            description += "challengeErrorDetail=" + challengeErrorDetail;
        }
        break;
    }

    return description;
}

inline bool SwHttpAcmeManager::ensureStorageDir_() const {
    return SwDir::mkpathAbsolute(storageRoot_(), false);
}

inline SwString SwHttpAcmeManager::storageRoot_() const {
    SwString path = m_config.storageDir.trimmed();
    if (path.isEmpty()) {
        return path;
    }

#if defined(_WIN32)
    path.replace("/", "\\");
    if (path.endsWith("\\")) {
        path.chop(1);
    }
    const bool isAbsolute = path.size() > 1 && path[1] == ':';
#else
    if (path.endsWith("/")) {
        path.chop(1);
    }
    const bool isAbsolute = path.startsWith("/");
#endif

    if (!isAbsolute) {
        path = swDirPlatform().absolutePath(path);
    }

#if defined(_WIN32)
    path.replace("/", "\\");
#endif
    return path;
}

#if defined(_WIN32)
#define SW_ACME_SEP_ "\\"
#else
#define SW_ACME_SEP_ "/"
#endif

inline SwString SwHttpAcmeManager::accountKeyFilePath_() const {
    return storageRoot_() + SW_ACME_SEP_ "account_key.pem";
}
inline SwString SwHttpAcmeManager::accountStateFilePath_() const {
    return storageRoot_() + SW_ACME_SEP_ "account.json";
}
inline SwString SwHttpAcmeManager::certificateKeyFilePath_() const {
    return storageRoot_() + SW_ACME_SEP_ "certificate_key.pem";
}
inline SwString SwHttpAcmeManager::certificateFilePath_() const {
    return storageRoot_() + SW_ACME_SEP_ "certificate.pem";
}
inline SwString SwHttpAcmeManager::certificateMetaFilePath_() const {
    return storageRoot_() + SW_ACME_SEP_ "certificate_meta.json";
}

inline bool SwHttpAcmeManager::loadAccountState_() {
    SwString json;
    if (!readTextFile_(accountStateFilePath_(), json)) {
        m_accountUrl.clear();
        return true;
    }

    SwString error;
    SwJsonDocument doc = SwJsonDocument::fromJson(json.toStdString(), error);
    if (!error.isEmpty() || !doc.isObject()) {
        m_accountUrl.clear();
        return false;
    }

    const SwJsonObject object = doc.object();
    const SwString storedDirectory = object.value("directoryUrl").toString().c_str();
    if (!storedDirectory.isEmpty() && storedDirectory != m_config.directoryUrl) {
        m_accountUrl.clear();
        return true;
    }

    m_accountUrl = object.value("accountUrl").toString().c_str();
    return true;
}

inline bool SwHttpAcmeManager::saveAccountState_() const {
    SwJsonObject object;
    object["directoryUrl"] = m_config.directoryUrl.toStdString();
    object["accountUrl"] = m_accountUrl.toStdString();
    object["domain"] = m_config.domain.trimmed().toStdString();
    object["contactEmail"] = m_config.contactEmail.trimmed().toStdString();
    return writeTextFileAtomic_(accountStateFilePath_(), SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact));
}

inline bool SwHttpAcmeManager::saveCertificateMetadata_(long long remainingSeconds) const {
    const time_t now = time(nullptr);
    const time_t notAfter = now + static_cast<time_t>(remainingSeconds);
    long long renewBeforeSeconds = static_cast<long long>(m_config.renewBeforeDays) * 24LL * 60LL * 60LL;
    if (renewBeforeSeconds < 0) {
        renewBeforeSeconds = 0;
    }

    SwJsonObject object;
    object["domain"] = m_config.domain.trimmed().toStdString();
    object["subjectAlternativeNames"] = swMailDetail::toJsonArray(m_config.subjectAlternativeNames);
    object["issuedAtEpoch"] = static_cast<long long>(now);
    object["notAfterEpoch"] = static_cast<long long>(notAfter);
    object["renewAtEpoch"] = static_cast<long long>(notAfter - renewBeforeSeconds);
    object["renewBeforeDays"] = static_cast<long long>(m_config.renewBeforeDays);
    return writeTextFileAtomic_(certificateMetaFilePath_(), SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact));
}

inline bool SwHttpAcmeManager::ensureAccountKeyPem_() {
    if (!m_accountKeyPem.isEmpty()) {
        return true;
    }
    if (readTextFile_(accountKeyFilePath_(), m_accountKeyPem)) {
        return true;
    }
    if (!generateEcPrivateKeyPem_(m_accountKeyPem)) {
        swCError(kSwLogCategory_SwHttpAcmeManager) << "generateEcPrivateKeyPem_ failed for " << accountKeyFilePath_();
        m_accountKeyPem.clear();
        return false;
    }
    if (!writeTextFileAtomic_(accountKeyFilePath_(), m_accountKeyPem)) {
        swCError(kSwLogCategory_SwHttpAcmeManager) << "writeTextFileAtomic_ failed for " << accountKeyFilePath_();
        m_accountKeyPem.clear();
        return false;
    }
    if (!m_accountUrl.isEmpty()) {
        m_accountUrl.clear();
        saveAccountState_();
    }
    return true;
}

inline bool SwHttpAcmeManager::ensureCertificateKeyPem_() {
    if (!m_certificateKeyPem.isEmpty()) {
        return true;
    }
    if (readTextFile_(certificateKeyFilePath_(), m_certificateKeyPem)) {
        return true;
    }
    if (!generateEcPrivateKeyPem_(m_certificateKeyPem) ||
        !writeTextFileAtomic_(certificateKeyFilePath_(), m_certificateKeyPem)) {
        m_certificateKeyPem.clear();
        return false;
    }
    return true;
}

inline bool SwHttpAcmeManager::readTextFile_(const SwString& path, SwString& outText) {
    outText.clear();
    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        return false;
    }
    outText = file.readAll();
    file.close();
    return !outText.isEmpty();
}

inline bool SwHttpAcmeManager::writeTextFileAtomic_(const SwString& path, const SwString& text) {
    const SwString tempPath = path + ".tmp";
    SwFile file(tempPath);
    if (!file.openBinary(SwFile::Write)) {
        return false;
    }
    if (!file.write(SwByteArray(text.data(), static_cast<size_t>(text.size())))) {
        file.close();
        removeFile_(tempPath);
        return false;
    }
    file.close();
    if (renameFileAtomic_(tempPath, path)) {
        return true;
    }

    SwFile directFile(path);
    if (!directFile.openBinary(SwFile::Write)) {
        removeFile_(tempPath);
        return false;
    }
    const bool directOk = directFile.write(SwByteArray(text.data(), static_cast<size_t>(text.size())));
    directFile.close();
    removeFile_(tempPath);
    return directOk;
}

inline void SwHttpAcmeManager::removeFile_(const SwString& filePath) {
#if defined(_WIN32)
    (void)_wremove(filePath.toStdWString().c_str());
#else
    (void)::remove(filePath.toStdString().c_str());
#endif
}

inline bool SwHttpAcmeManager::renameFileAtomic_(const SwString& sourcePath, const SwString& destinationPath) {
#if defined(_WIN32)
    if (::MoveFileExW(sourcePath.toStdWString().c_str(),
                      destinationPath.toStdWString().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    removeFile_(destinationPath);
    return ::MoveFileExW(sourcePath.toStdWString().c_str(),
                         destinationPath.toStdWString().c_str(),
                         MOVEFILE_WRITE_THROUGH) != 0;
#else
    return ::rename(sourcePath.toStdString().c_str(), destinationPath.toStdString().c_str()) == 0;
#endif
}

inline SwString SwHttpAcmeManager::base64UrlEncode_(const SwByteArray& bytes) {
    SwString encoded(bytes.toBase64());
    encoded.replace("+", "-");
    encoded.replace("/", "_");
    while (encoded.endsWith("=")) {
        encoded.chop(1);
    }
    return encoded;
}

inline SwByteArray SwHttpAcmeManager::base64UrlDecode_(const SwString& input, bool* ok) {
    SwString normalized = input;
    normalized.replace("-", "+");
    normalized.replace("_", "/");
    while ((normalized.size() % 4) != 0) {
        normalized += "=";
    }
    if (ok) {
        *ok = true;
    }
    return SwByteArray::fromBase64(SwByteArray(normalized.data(), static_cast<size_t>(normalized.size())));
}

inline bool SwHttpAcmeManager::buildJoseBody_(const SwString& url,
                                              const SwByteArray& payload,
                                              bool useJwk,
                                              SwString& outBody) {
    if (m_nonce.isEmpty() || !ensureAccountKeyPem_() || (!useJwk && m_accountUrl.isEmpty())) {
        return false;
    }

    const JwkMaterial jwkMaterial = buildJwkMaterialFromPrivateKeyPem_(m_accountKeyPem);
    if (!jwkMaterial.valid) {
        return false;
    }

    SwJsonObject protectedHeader;
    protectedHeader["alg"] = "ES256";
    protectedHeader["nonce"] = m_nonce.toStdString();
    protectedHeader["url"] = url.toStdString();
    if (useJwk) {
        protectedHeader["jwk"] = jwkMaterial.jwk;
    } else {
        protectedHeader["kid"] = m_accountUrl.toStdString();
    }

    const SwString protectedJson = SwJsonDocument(protectedHeader).toJson(SwJsonDocument::JsonFormat::Compact);
    const SwString protectedB64 = base64UrlEncode_(SwByteArray(protectedJson.data(), static_cast<size_t>(protectedJson.size())));
    const SwString payloadB64 = payload.isEmpty() ? SwString() : base64UrlEncode_(payload);
    const SwString signingInput = protectedB64 + "." + payloadB64;

    SwByteArray signature;
    if (!signEs256_(m_accountKeyPem, signingInput, signature)) {
        return false;
    }

    SwJsonObject body;
    body["protected"] = protectedB64.toStdString();
    body["payload"] = payloadB64.toStdString();
    body["signature"] = base64UrlEncode_(signature).toStdString();
    outBody = SwJsonDocument(body).toJson(SwJsonDocument::JsonFormat::Compact);
    return true;
}

inline int SwHttpAcmeManager::maxPollAttempts_() const {
    return (m_config.maxPollAttempts > 0) ? m_config.maxPollAttempts : 60;
}

inline void SwHttpAcmeManager::emitError_(const SwString& message) {
    swCError(kSwLogCategory_SwHttpAcmeManager) << message;
    emit errorOccurred(message);
}

inline bool SwHttpAcmeManager::generateEcPrivateKeyPem_(SwString& outPem) {
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
    return !outPem.isEmpty();
}

inline EVP_PKEY* SwHttpAcmeManager::loadPrivateKeyFromPem_(const SwString& pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) {
        return nullptr;
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return pkey;
}

inline X509* SwHttpAcmeManager::loadCertificateFromPem_(const SwString& pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) {
        return nullptr;
    }
    X509* certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return certificate;
}

inline SwHttpAcmeManager::JwkMaterial SwHttpAcmeManager::buildJwkMaterialFromPrivateKeyPem_(const SwString& pem) {
    JwkMaterial material;
    EVP_PKEY* pkey = loadPrivateKeyFromPem_(pem);
    if (!pkey) {
        return material;
    }

    EC_KEY* ecKey = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ecKey) {
        EVP_PKEY_free(pkey);
        return material;
    }

    const EC_GROUP* group = EC_KEY_get0_group(ecKey);
    const EC_POINT* point = EC_KEY_get0_public_key(ecKey);
    BIGNUM* x = BN_new();
    BIGNUM* y = BN_new();
    if (!group || !point || !x || !y || EC_POINT_get_affine_coordinates_GFp(group, point, x, y, nullptr) != 1) {
        if (x) BN_free(x);
        if (y) BN_free(y);
        EC_KEY_free(ecKey);
        EVP_PKEY_free(pkey);
        return material;
    }

    unsigned char xRaw[32] = {0};
    unsigned char yRaw[32] = {0};
    if (BN_bn2binpad(x, xRaw, 32) != 32 || BN_bn2binpad(y, yRaw, 32) != 32) {
        BN_free(x);
        BN_free(y);
        EC_KEY_free(ecKey);
        EVP_PKEY_free(pkey);
        return material;
    }

    const SwString xB64 = base64UrlEncode_(SwByteArray(reinterpret_cast<const char*>(xRaw), sizeof(xRaw)));
    const SwString yB64 = base64UrlEncode_(SwByteArray(reinterpret_cast<const char*>(yRaw), sizeof(yRaw)));
    material.jwk["kty"] = "EC";
    material.jwk["crv"] = "P-256";
    material.jwk["x"] = xB64.toStdString();
    material.jwk["y"] = yB64.toStdString();

    const SwString canonical = SwString("{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"") +
                               xB64 + "\",\"y\":\"" + yB64 + "\"}";
    unsigned char hash[SHA256_DIGEST_LENGTH] = {0};
    SHA256(reinterpret_cast<const unsigned char*>(canonical.data()), static_cast<size_t>(canonical.size()), hash);
    material.thumbprint = base64UrlEncode_(SwByteArray(reinterpret_cast<const char*>(hash), sizeof(hash)));
    material.valid = true;

    BN_free(x);
    BN_free(y);
    EC_KEY_free(ecKey);
    EVP_PKEY_free(pkey);
    return material;
}

inline bool SwHttpAcmeManager::signEs256_(const SwString& privateKeyPem,
                                          const SwString& signingInput,
                                          SwByteArray& outSignature) {
    outSignature.clear();
    EVP_PKEY* pkey = loadPrivateKeyFromPem_(privateKeyPem);
    EVP_MD_CTX* mdctx = nullptr;
    if (!pkey) {
        return false;
    }

    mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    size_t derLength = 0;
    bool ok = EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
              EVP_DigestSignUpdate(mdctx, signingInput.data(), static_cast<size_t>(signingInput.size())) == 1 &&
              EVP_DigestSignFinal(mdctx, nullptr, &derLength) == 1;
    if (!ok || derLength == 0) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    SwByteArray der(derLength, '\0');
    ok = EVP_DigestSignFinal(mdctx, reinterpret_cast<unsigned char*>(der.data()), &derLength) == 1;
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    if (!ok || derLength == 0) {
        return false;
    }
    der.resize(derLength);

    const unsigned char* derPtr = reinterpret_cast<const unsigned char*>(der.constData());
    ECDSA_SIG* ecdsa = d2i_ECDSA_SIG(nullptr, &derPtr, static_cast<long>(der.size()));
    if (!ecdsa) {
        return false;
    }

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(ecdsa, &r, &s);
    unsigned char rawSignature[64] = {0};
    const bool result = r && s &&
                        BN_bn2binpad(r, rawSignature, 32) == 32 &&
                        BN_bn2binpad(s, rawSignature + 32, 32) == 32;
    ECDSA_SIG_free(ecdsa);
    if (!result) {
        return false;
    }

    outSignature = SwByteArray(reinterpret_cast<const char*>(rawSignature), sizeof(rawSignature));
    return true;
}

inline bool SwHttpAcmeManager::buildCsrDer_(const SwString& privateKeyPem,
                                            const SwList<SwString>& domains,
                                            SwByteArray& outDer) {
    outDer.clear();
    if (domains.isEmpty()) {
        return false;
    }
    const SwString primaryDomain = swMailDetail::normalizeDomain(domains.first());
    if (primaryDomain.isEmpty()) {
        return false;
    }
    EVP_PKEY* pkey = loadPrivateKeyFromPem_(privateKeyPem);
    X509_REQ* req = nullptr;
    X509_NAME* name = nullptr;
    STACK_OF(X509_EXTENSION)* extensions = nullptr;
    X509_EXTENSION* sanExtension = nullptr;
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
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>(primaryDomain.data()),
                                   static_cast<int>(primaryDomain.size()), -1, 0) != 1 ||
        X509_REQ_set_subject_name(req, name) != 1) {
        if (name) X509_NAME_free(name);
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }
    X509_NAME_free(name);

    extensions = sk_X509_EXTENSION_new_null();
    SwString sanValue;
    for (std::size_t i = 0; i < domains.size(); ++i) {
        const SwString candidate = swMailDetail::normalizeDomain(domains[i]);
        if (candidate.isEmpty()) {
            continue;
        }
        if (!sanValue.isEmpty()) {
            sanValue += ",";
        }
        sanValue += "DNS:" + candidate;
    }
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

    const int length = i2d_X509_REQ(req, nullptr);
    if (length <= 0) {
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }

    SwByteArray der(static_cast<size_t>(length), '\0');
    unsigned char* ptr = reinterpret_cast<unsigned char*>(der.data());
    if (i2d_X509_REQ(req, &ptr) != length) {
        X509_REQ_free(req);
        EVP_PKEY_free(pkey);
        return false;
    }

    outDer = der;
    X509_REQ_free(req);
    EVP_PKEY_free(pkey);
    return true;
}

inline bool SwHttpAcmeManager::isCertificatePairUsable_(const SwString& certificatePem, const SwString& privateKeyPem) {
    X509* certificate = loadCertificateFromPem_(certificatePem);
    EVP_PKEY* pkey = loadPrivateKeyFromPem_(privateKeyPem);
    if (!certificate || !pkey) {
        if (certificate) X509_free(certificate);
        if (pkey) EVP_PKEY_free(pkey);
        return false;
    }

    const bool keyMatch = X509_check_private_key(certificate, pkey) == 1;
    const long long remainingSeconds = remainingCertificateLifetimeSeconds_(certificatePem);
    X509_free(certificate);
    EVP_PKEY_free(pkey);
    return keyMatch && remainingSeconds > 0;
}

inline long long SwHttpAcmeManager::remainingCertificateLifetimeSeconds_(const SwString& certificatePem) {
    X509* certificate = loadCertificateFromPem_(certificatePem);
    ASN1_TIME* now = nullptr;
    if (!certificate) {
        return -1;
    }

    const ASN1_TIME* notAfter = X509_get0_notAfter(certificate);
    now = ASN1_TIME_set(nullptr, time(nullptr));
    if (!notAfter || !now) {
        if (now) ASN1_TIME_free(now);
        X509_free(certificate);
        return -1;
    }

    int days = 0;
    int seconds = 0;
    const int diffOk = ASN1_TIME_diff(&days, &seconds, now, notAfter);
    ASN1_TIME_free(now);
    X509_free(certificate);
    if (diffOk != 1) {
        return -1;
    }
    return static_cast<long long>(days) * 24LL * 60LL * 60LL + static_cast<long long>(seconds);
}
