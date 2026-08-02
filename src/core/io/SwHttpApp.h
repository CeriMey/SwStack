#pragma once

/**
 * @file src/core/io/SwHttpApp.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpApp in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP app interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwHttpApp.
 *
 * HTTP-facing declarations in this header are intended to make incremental request processing and
 * response generation explicit enough for production hardening and testing.
 *
 * HTTP-facing declarations in this area are designed around non-blocking IO, incremental parsing,
 * bounded buffering, and a clear separation between transport work and higher-level request
 * handling.
 *
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwHttpServer.h"
#include "SwMailService.h"
#include "SwWebSocket.h"
#include "http3/SwQuicHttp3Server.h"
#include "quic/SwQuicServerCredential.h"

#include <utility>
#include "auth/SwHttpAuthService.h"
#include "http/SwHttpContext.h"
#include "http/SwHttpSeo.h"

#include <algorithm>
#include <functional>
#include <chrono>
#include <exception>

class SwHttpApp {
public:
    using SwHttpHandler = std::function<void(SwHttpContext&)>;
    using SwHttpNext = std::function<void()>;
    using SwHttpMiddleware = std::function<void(SwHttpContext&, const SwHttpNext&)>;
    using SwHttpRecoveryHandler = std::function<void(SwHttpContext&, const SwString&)>;
    using SwHttpWebSocketHandler = std::function<void(SwWebSocket*, const SwHttpRequest&)>;
    using SwMailAdminGuard = std::function<bool(SwHttpContext&)>;

    struct SwHttpRouteOptions {
        SwString name;
        int softTimeoutMs = 0;
        bool timeoutOverridesResponse = false;
    };

    struct SwHttpWebSocketRouteOptions {
        SwString name;
        SwList<SwString> supportedSubprotocols;
        bool enablePerMessageDeflate = true;
    };

    struct SwHttp3Config {
        // HTTPS listeners expose HTTP/1.x over TCP and HTTP/3 over QUIC/UDP on
        // the same numeric port by default. Set enabled=false for a deliberate
        // TCP-only deployment.
        bool enabled = true;
        bool advertise = true;
        int altSvcMaxAgeSeconds = 86400;
        std::size_t maxClients = 4096;
        std::size_t maxPendingHandshakes = 1024;
        std::uint64_t handshakeTimeoutMs = 10000;
    };

    /**
     * @brief Constructs a `SwHttpApp` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwHttpApp(SwObject* parent = nullptr)
        : m_server(parent),
          m_recoveryHandler(defaultRecoveryHandler_()) {
        m_http3Server.setPendingRequestBudgetHandlers(
            [this](std::size_t bytes) {
                return m_server.tryReservePendingRequestBytes(bytes);
            },
            [this](std::size_t bytes) {
                m_server.releasePendingRequestBytes(bytes);
            });
        m_http3Server.setAsyncRequestHandler(
            [this](const SwHttpRequest& request,
                   const SwHttp3Server::ResponseCallback& complete) {
                m_server.dispatchRequest(request, complete);
            });
        m_server.router().addResponseFilter(
            [this](const SwHttpRequest& request, SwHttpResponse& response) {
                addHttp3DiscoveryHeader_(request, response);
            });
        SwObject::connect(&m_http3Server, &SwQuicHttp3Server::serverError,
                          [this](const SwString& error) {
                              m_lastHttp3Error = error;
                          });
    }

    /**
     * @brief Constructs an HTTP application with SEO discovery mounted before site routes.
     * @param seoConfig Canonical site identity and crawler policy.
     * @param parent Optional parent object that owns the underlying server.
     *
     * @details Derived site classes should prefer this constructor so the private-route noindex
     * middleware is present before their API, administration and authentication routes are added.
     */
    explicit SwHttpApp(const SwHttpSeoConfig& seoConfig, SwObject* parent = nullptr)
        : SwHttpApp(parent) {
        SwString error;
        if (!mountSeo(seoConfig, &error)) {
            swError() << "[SwHttpApp] Failed to mount SEO discovery: " << error;
        }
    }

    ~SwHttpApp() {
        close();
        // Routes and response filters capture SwHttpApp state (including the
        // HTTP/3 discovery configuration). Drain every in-flight callback
        // before any of those members or optional services are destroyed.
        m_server.shutdownDispatching();
        stopAuth();
        stopMail();
    }

    /**
     * @brief Starts listening for incoming traffic.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool listen(uint16_t port) {
        // SwHttpServer::listen() replaces every TCP listener. Mirror that
        // lifecycle for UDP so switching an app back to plaintext cannot leave
        // an orphaned HTTP/3 origin serving the previous certificate/routes.
        m_http3Server.setAutomaticPollingEnabled(false);
        m_http3Server.close();
        return m_server.listen(port);
    }

    bool listen(const SwString& bindAddress, uint16_t port) {
        m_http3Server.setAutomaticPollingEnabled(false);
        m_http3Server.close();
        return m_server.listen(bindAddress, port);
    }

    /**
     * @brief Starts listening for incoming HTTPS traffic.
     * @param port Local port used by the operation (typically 443).
     * @param certPath Path to the PEM certificate file.
     * @param keyPath Path to the PEM private key file.
     * @return `true` on success; otherwise `false`.
     */
    bool listen(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        return listenSecureTransports_(SwString(), port, certPath, keyPath, true);
    }

    bool listen(const SwString& bindAddress,
                uint16_t port,
                const SwString& certPath,
                const SwString& keyPath) {
        return listenSecureTransports_(bindAddress, port, certPath, keyPath, true);
    }

    bool listenHttp(uint16_t port) {
        return m_server.listenHttp(port);
    }

    bool listenHttp(const SwString& bindAddress, uint16_t port) {
        return m_server.listenHttp(bindAddress, port);
    }

    bool listenHttps(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        const bool ok = listenSecureTransports_(SwString(), port, certPath, keyPath, false);
        if (ok && m_mailService) {
            SwString ignored;
            (void)m_mailService->reloadTlsCredentials(certPath, keyPath, &ignored);
        }
        return ok;
    }

    bool listenHttps(const SwString& bindAddress,
                     uint16_t port,
                     const SwString& certPath,
                     const SwString& keyPath) {
        const bool ok = listenSecureTransports_(bindAddress, port, certPath, keyPath, false);
        if (ok && m_mailService) {
            SwString ignored;
            (void)m_mailService->reloadTlsCredentials(certPath, keyPath, &ignored);
        }
        return ok;
    }

    bool listenHttps(uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        const bool ok = listenSecureTransports_(SwString(), port, credentials);
        if (ok) {
            syncMailTlsCredentials_(credentials);
        }
        return ok;
    }

    bool listenHttps(const SwString& bindAddress,
                     uint16_t port,
                     const SwList<SwTlsCredentialEntry>& credentials) {
        const bool ok = listenSecureTransports_(bindAddress, port, credentials);
        if (ok) {
            syncMailTlsCredentials_(credentials);
        }
        return ok;
    }

    bool reloadHttpsCredentials(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        SwQuicServerCredential credential;
        if (m_http3Config.enabled &&
            !SwQuicPemCredential::load(certPath, keyPath, credential, &m_lastHttp3Error)) {
            return false;
        }
        const bool ok = m_server.reloadHttpsCredentials(port, certPath, keyPath);
        if (ok) {
            if (m_http3Config.enabled) {
                setHttp3Credential(credential);
                m_http3AdvertisedHost = SwString();
                if (!synchronizeHttp3ListenerAfterReload_()) {
                    m_server.closeHttpsListener();
                    return false;
                }
            }
            if (m_mailService) {
                SwString ignored;
                (void)m_mailService->reloadTlsCredentials(certPath, keyPath, &ignored);
            }
        }
        return ok;
    }

    bool reloadHttpsCredentials(uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        SwQuicServerCredential credential;
        SwQuicHttp3Server::CredentialMap credentialsByHost;
        if (m_http3Config.enabled &&
            !loadHttp3TlsCredentials_(credentials, credential, credentialsByHost)) {
            return false;
        }
        const bool ok = m_server.reloadHttpsCredentials(port, credentials);
        if (ok) {
            if (m_http3Config.enabled) {
                setHttp3Credentials(credential, credentialsByHost);
                m_http3AdvertisedHost = SwString();
                if (!synchronizeHttp3ListenerAfterReload_()) {
                    m_server.closeHttpsListener();
                    return false;
                }
            }
            syncMailTlsCredentials_(credentials);
        }
        return ok;
    }

    bool isHttpListening() const {
        return m_server.isHttpListening();
    }

    bool isHttpsListening() const {
        return m_server.isHttpsListening();
    }

    uint16_t httpPort() const {
        return m_server.httpPort();
    }

    uint16_t httpsPort() const {
        return m_server.httpsPort();
    }

    void setHttp3Config(const SwHttp3Config& config) {
        m_http3Config = config;
        applyHttp3Config_();
        if (!m_http3Config.enabled) {
            m_http3Server.close();
        }
    }

    const SwHttp3Config& http3Config() const {
        return m_http3Config;
    }

    void setHttp3Enabled(bool enabled) {
        SwHttp3Config config = m_http3Config;
        config.enabled = enabled;
        setHttp3Config(config);
    }

    bool isHttp3Enabled() const {
        return m_http3Config.enabled;
    }

    void setHttp3Credential(const SwQuicServerCredential& credential) {
        m_http3Credential = credential;
        m_http3Credentials.clear();
        m_hasHttp3Credential = credential.isValid();
        m_http3Server.setCredential(credential);
    }

    void setHttp3Credentials(
        const SwQuicServerCredential& defaultCredential,
        const SwQuicHttp3Server::CredentialMap& credentialsByHost) {
        m_http3Credential = defaultCredential;
        m_http3Credentials = credentialsByHost;
        m_hasHttp3Credential = defaultCredential.isValid();
        m_http3Server.setCredentials(defaultCredential, credentialsByHost);
    }

    bool listenHttp3(uint16_t port, SwString* error = nullptr) {
        return listenHttp3(SwString(), port, error);
    }

    bool listenHttp3(const SwString& bindAddress,
                     uint16_t port,
                     SwString* error = nullptr) {
        if (!m_http3Config.enabled) {
            setHttp3Error_(error, SwString("HTTP/3 is disabled in SwHttpApp"));
            return false;
        }
        if (!m_hasHttp3Credential) {
            setHttp3Error_(error, SwString("HTTP/3 has no valid TLS credential"));
            return false;
        }
        if (!SwCoreApplication::instance(false)) {
            setHttp3Error_(error,
                           SwString("HTTP/3 automatic polling requires an active SwCoreApplication"));
            return false;
        }
        applyHttp3Config_();
        m_http3Server.setCredentials(m_http3Credential, m_http3Credentials);
        m_http3Server.setRouter(nullptr);
        m_http3Server.setAsyncRequestHandler(
            [this](const SwHttpRequest& request,
                   const SwHttp3Server::ResponseCallback& complete) {
                m_server.dispatchRequest(request, complete);
            });
        m_http3Server.setAutomaticPollingEnabled(true);
        SwString listenError;
        if (!m_http3Server.listen(bindAddress, port, &listenError)) {
            m_http3Server.setAutomaticPollingEnabled(false);
            setHttp3Error_(error, listenError);
            return false;
        }
        m_lastHttp3Error = SwString();
        if (error) *error = SwString();
        return true;
    }

    bool isHttp3Listening() const {
        return m_http3Server.isListening();
    }

    uint16_t http3Port() const {
        return m_http3Server.localPort();
    }

    SwString http3Address() const {
        return m_http3Server.localAddress();
    }

    SwString lastHttp3Error() const {
        return m_lastHttp3Error;
    }

    SwQuicHttp3Server& http3Server() {
        return m_http3Server;
    }

    const SwQuicHttp3Server& http3Server() const {
        return m_http3Server;
    }

    void setDomainTlsConfig(const SwDomainTlsConfig& config) {
        m_domainTlsConfig = config;
        syncMailTlsConfig_();
        if (m_mailService) {
            m_mailService->setDomainTlsConfig(m_domainTlsConfig);
        }
    }

    const SwDomainTlsConfig& domainTlsConfig() const {
        return m_domainTlsConfig;
    }

    void setMailConfig(const SwMailConfig& config) {
        m_mailConfig = config;
        syncMailTlsConfig_();
        if (m_mailService) {
            m_mailService->setConfig(m_mailConfig);
        }
    }

    const SwMailConfig& mailConfig() const {
        return m_mailConfig;
    }

    void setAuthConfig(const SwHttpAuthConfig& config) {
        m_authConfig = config;
        m_authConfig.routePrefix = swHttpAuthDetail::normalizeRoutePrefix(m_authConfig.routePrefix);
        m_authConfig.publicBaseUrl = swHttpAuthDetail::normalizeBaseUrl(m_authConfig.publicBaseUrl);
        if (m_authService) {
            m_authService->setConfig(m_authConfig);
        }
    }

    const SwHttpAuthConfig& authConfig() const {
        return m_authConfig;
    }

    bool startMail() {
        SwMailService* service = ensureMailService_();
        service->setConfig(m_mailConfig);
        service->setDomainTlsConfig(m_domainTlsConfig);
        if (!m_domainTlsConfig.certPath.isEmpty() && !m_domainTlsConfig.keyPath.isEmpty()) {
            SwString ignored;
            (void)service->reloadTlsCredentials(m_domainTlsConfig.certPath, m_domainTlsConfig.keyPath, &ignored);
        }
        return service->start();
    }

    void stopMail() {
        if (!m_mailService) {
            return;
        }
        m_mailService->stop();
        delete m_mailService;
        m_mailService = nullptr;
        if (m_authService) {
            m_authService->setMailService(nullptr);
        }
        m_mailAdminMounted = false;
    }

    bool isMailStarted() const {
        return m_mailService && m_mailService->isStarted();
    }

    SwMailService* mailService() {
        return m_mailService;
    }

    const SwMailService* mailService() const {
        return m_mailService;
    }

    bool startAuth() {
        SwHttpAuthService* service = ensureAuthService_();
        service->setConfig(m_authConfig);
        service->setMailService(m_mailService);
        return service->start();
    }

    void stopAuth() {
        if (!m_authService) {
            return;
        }
        m_authService->stop();
        delete m_authService;
        m_authService = nullptr;
        m_authApiMounted = false;
    }

    bool isAuthStarted() const {
        return m_authService && m_authService->isStarted();
    }

    SwHttpAuthService* authService() {
        return m_authService;
    }

    const SwHttpAuthService* authService() const {
        return m_authService;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() {
        m_http3Server.setAutomaticPollingEnabled(false);
        m_http3Server.close();
        m_server.close();
    }

    /**
     * @brief Stops accepting new work and waits for active operations to finish.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return `true` on success; otherwise `false`.
     *
     * @details The method stops accepting new work first, then waits until the active operations drain or the timeout expires.
     */
    bool closeGraceful(int timeoutMs = 5000) {
        // HTTP/3 first: GOAWAY every session and refuse new connections, then
        // let both stacks drain in parallel -- the HTTP/1.x wait below runs
        // the event loop that also drives the QUIC timers.
        m_http3Server.beginGracefulShutdown();
        const bool httpDrained = m_server.closeGraceful(timeoutMs);
        bool http3Drained = m_http3Server.isDrained();
        if (!http3Drained && m_http3Server.isListening()) {
            SwEventLoop loop;
            SwTimer probe;
            SwTimer timeout;
            bool timedOut = false;
            SwObject::connect(&probe, &SwTimer::timeout, &loop, [this, &loop]() {
                SwString ignored;
                m_http3Server.poll(0, &ignored);
                if (m_http3Server.isDrained()) {
                    loop.quit();
                }
            });
            if (timeoutMs >= 0) {
                timeout.setSingleShot(true);
                SwObject::connect(&timeout, &SwTimer::timeout, &loop,
                                  [&loop, &timedOut]() {
                    timedOut = true;
                    loop.quit();
                });
                timeout.start(timeoutMs);
            }
            probe.start(10);
            loop.exec();
            (void)timedOut;
            http3Drained = m_http3Server.isDrained();
        }
        m_http3Server.setAutomaticPollingEnabled(false);
        m_http3Server.close();
        return httpDrained && http3Drained;
    }

    /**
     * @brief Sets the limits.
     * @param limits Limit configuration to apply.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLimits(const SwHttpLimits& limits) {
        m_server.setLimits(limits);
        m_http3Server.setHttpLimits(limits);
    }

    /**
     * @brief Returns the current limit configuration.
     * @return The current limits.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwHttpLimits& limits() const {
        return m_server.limits();
    }

    /**
     * @brief Sets the timeouts.
     * @param timeouts Timeout configuration to apply.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTimeouts(const SwHttpTimeouts& timeouts) {
        m_server.setTimeouts(timeouts);
    }

    /**
     * @brief Returns the current timeout configuration.
     * @return The current timeouts.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwHttpTimeouts& timeouts() const {
        return m_server.timeouts();
    }

    /**
     * @brief Sets the thread Pool.
     * @param threadPool Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setThreadPool(SwThreadPool* threadPool) {
        m_server.setThreadPool(threadPool);
    }

    /**
     * @brief Returns the current thread Pool.
     * @return The current thread Pool.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwThreadPool* threadPool() const {
        return m_server.threadPool();
    }

    /**
     * @brief Sets the dispatch Mode.
     * @param mode Mode value that controls the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDispatchMode(SwHttpServer::DispatchMode mode) {
        m_server.setDispatchMode(mode);
    }

    /**
     * @brief Returns the current dispatch Mode.
     * @return The current dispatch Mode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpServer::DispatchMode dispatchMode() const {
        return m_server.dispatchMode();
    }

    /**
     * @brief Returns the current metrics Snapshot.
     * @return The current metrics Snapshot.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpServerMetrics metricsSnapshot() const {
        return m_server.metricsSnapshot();
    }

    /**
     * @brief Resets the object to a baseline state.
     */
    void resetMetrics() {
        m_server.resetMetrics();
    }

    /**
     * @brief Sets how trailing slashes are handled during route matching.
     * @param policy Policy value applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTrailingSlashPolicy(SwHttpRouter::TrailingSlashPolicy policy) {
        m_server.setTrailingSlashPolicy(policy);
    }

    /**
     * @brief Returns the current trailing-slash policy.
     * @return The current trailing Slash Policy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpRouter::TrailingSlashPolicy trailingSlashPolicy() const {
        return m_server.trailingSlashPolicy();
    }

    /**
     * @brief Returns whether a named route is registered.
     * @param routeName Stable route name used by the operation.
     * @return `true` when the object reports route Name; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool hasRouteName(const SwString& routeName) const {
        return m_server.hasRouteName(routeName);
    }

    /**
     * @brief Returns the pattern registered for a named route.
     * @param routeName Stable route name used by the operation.
     * @return The requested route Pattern.
     */
    SwString routePattern(const SwString& routeName) const {
        return m_server.routePattern(routeName);
    }

    /**
     * @brief Builds a URL from a named route and parameter values.
     * @param routeName Stable route name used by the operation.
     * @param pathParams Path parameters associated with the operation.
     * @param outUrl Output string that receives the generated URL.
     * @param queryParams Query parameters associated with the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details Use this helper when route generation needs to stay consistent with the patterns registered in the router.
     */
    bool buildUrl(const SwString& routeName,
                  const SwMap<SwString, SwString>& pathParams,
                  SwString& outUrl,
                  const SwMap<SwString, SwString>& queryParams = SwMap<SwString, SwString>()) const {
        return m_server.buildUrl(routeName, pathParams, outUrl, queryParams);
    }

    /**
     * @brief Performs the `urlFor` operation.
     * @param routeName Stable route name used by the operation.
     * @param pathParams Path parameters associated with the operation.
     * @param queryParams Query parameters associated with the operation.
     * @return The requested url For.
     */
    SwString urlFor(const SwString& routeName,
                    const SwMap<SwString, SwString>& pathParams = SwMap<SwString, SwString>(),
                    const SwMap<SwString, SwString>& queryParams = SwMap<SwString, SwString>()) const {
        SwString out;
        if (!m_server.buildUrl(routeName, pathParams, out, queryParams)) {
            return SwString();
        }
        return out;
    }

    /**
     * @brief Installs the SEO discovery endpoints and private-route noindex middleware.
     * @details Call this before registering site routes. Middleware is snapshotted when a route is
     * registered, and the SEO-aware constructor does this automatically for derived sites.
     */
    bool mountSeo(const SwHttpSeoConfig& config, SwString* errorOut = nullptr) {
        if (m_seoMounted) {
            if (errorOut) {
                *errorOut = "SEO discovery is already mounted";
            }
            return false;
        }

        SwString error;
        if (!m_seo.configure(config, &error)) {
            if (errorOut) {
                *errorOut = error;
            }
            return false;
        }
        const SwHttpSeoConfig effective = m_seo.config();

        use([this](SwHttpContext& context, const SwHttpNext& next) {
            if (next) {
                next();
            }
            if (m_seo.shouldNoIndex(context.path())) {
                context.response().headers["x-robots-tag"] = "noindex, nofollow";
            }
        });

        mountSeoDiscoveryRoutes_(effective);
        m_seoMounted = true;
        if (errorOut) {
            errorOut->clear();
        }
        return true;
    }

    bool seoMounted() const {
        return m_seoMounted;
    }

    SwHttpSeo& seo() {
        return m_seo;
    }

    const SwHttpSeo& seo() const {
        return m_seo;
    }

    bool addSeoPage(const SwHttpSeoPage& page, SwString* errorOut = nullptr) {
        return m_seo.addPage(page, errorOut);
    }

    bool removeSeoPage(const SwString& path) {
        return m_seo.removePage(path);
    }

    SwString renderSeoPage(const SwString& pagePath,
                           const SwString& bodyHtml,
                           const SwString& extraHeadHtml = SwString(),
                           bool* okOut = nullptr) const {
        return m_seo.renderPage(pagePath, bodyHtml, extraHeadHtml, okOut);
    }

    /**
     * @brief Returns the current server.
     * @return The current server.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpServer& server() {
        return m_server;
    }

    /**
     * @brief Returns the current server.
     * @return The current server.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwHttpServer& server() const {
        return m_server;
    }

    /**
     * @brief Performs the `use` operation.
     * @param middleware Value passed to the method.
     */
    void use(const SwHttpMiddleware& middleware) {
        if (!middleware) {
            return;
        }
        m_middlewares.append(middleware);
    }

    /**
     * @brief Performs the `use` operation.
     * @param pathPrefix Value passed to the method.
     * @param middleware Value passed to the method.
     */
    void use(const SwString& pathPrefix, const SwHttpMiddleware& middleware) {
        if (!middleware) {
            return;
        }

        const SwString effectivePrefix = resolvePath_(pathPrefix);
        m_middlewares.append([effectivePrefix, middleware](SwHttpContext& context, const SwHttpNext& next) {
            const SwString requestPath = swHttpNormalizePath(context.path());
            bool match = false;
            if (effectivePrefix == "/") {
                match = true;
            } else if (requestPath.startsWith(effectivePrefix)) {
                if (requestPath.size() == effectivePrefix.size()) {
                    match = true;
                } else {
                    const char separator = requestPath[effectivePrefix.size()];
                    match = (separator == '/' || separator == '\\');
                }
            }

            if (!match) {
                if (next) {
                    next();
                }
                return;
            }

            middleware(context, next);
        });
    }

    /**
     * @brief Clears the current object state.
     */
    void clearMiddlewares() {
        m_middlewares.clear();
    }

    /**
     * @brief Performs the `group` operation.
     * @param prefix Prefix used by the operation.
     * @param setup Value passed to the method.
     */
    void group(const SwString& prefix, const std::function<void(SwHttpApp&)>& setup) {
        if (!setup) {
            return;
        }
        const SwString previousPrefix = m_groupPrefix;
        m_groupPrefix = joinGroupPrefix_(m_groupPrefix, prefix);
        setup(*this);
        m_groupPrefix = previousPrefix;
    }

    /**
     * @brief Sets the recovery Handler.
     * @param recoveryHandler Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRecoveryHandler(const SwHttpRecoveryHandler& recoveryHandler) {
        m_recoveryHandler = recoveryHandler;
    }

    /**
     * @brief Returns the current recovery Handler.
     * @return The current recovery Handler.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpRecoveryHandler recoveryHandler() const {
        return m_recoveryHandler;
    }

    /**
     * @brief Registers an HTTP route handler.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void addRoute(const SwString& method,
                  const SwString& pattern,
                  const SwHttpHandler& handler) {
        addRoute(method, pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Registers an HTTP route handler.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void addRoute(const SwString& method,
                  const SwString& pattern,
                  const SwHttpHandler& handler,
                  const SwHttpRouteOptions& options) {
        if (!handler) {
            return;
        }

        const SwString fullPattern = resolvePath_(pattern);
        const SwList<SwHttpMiddleware> middlewaresSnapshot = m_middlewares;
        const SwHttpRecoveryHandler recovery = m_recoveryHandler;
        if (!options.name.trimmed().isEmpty()) {
            m_server.addNamedRoute(options.name, method, fullPattern, [middlewaresSnapshot, handler, options, recovery](const SwHttpRequest& request) {
                return buildResponse_(request, middlewaresSnapshot, handler, options, recovery, false);
            });
            return;
        }

        m_server.addRoute(method, fullPattern, [middlewaresSnapshot, handler, options, recovery](const SwHttpRequest& request) {
            return buildResponse_(request, middlewaresSnapshot, handler, options, recovery, false);
        });
    }

    /**
     * @brief Registers a named HTTP route handler.
     * @param routeName Stable route name used by the operation.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void addNamedRoute(const SwString& routeName,
                       const SwString& method,
                       const SwString& pattern,
                       const SwHttpHandler& handler) {
        addNamedRoute(routeName, method, pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Registers a named HTTP route handler.
     * @param routeName Stable route name used by the operation.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void addNamedRoute(const SwString& routeName,
                       const SwString& method,
                       const SwString& pattern,
                       const SwHttpHandler& handler,
                       const SwHttpRouteOptions& options) {
        SwHttpRouteOptions effective = options;
        effective.name = routeName;
        addRoute(method, pattern, handler, effective);
    }

    /**
     * @brief Registers an HTTP route handler.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void addRoute(const SwString& pattern,
                  const SwHttpHandler& handler) {
        addRoute(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Registers an HTTP route handler.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void addRoute(const SwString& pattern,
                  const SwHttpHandler& handler,
                  const SwHttpRouteOptions& options) {
        addRoute("*", pattern, handler, options);
    }

    /**
     * @brief Performs the `any` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void any(const SwString& pattern,
             const SwHttpHandler& handler) {
        any(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `any` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void any(const SwString& pattern,
             const SwHttpHandler& handler,
             const SwHttpRouteOptions& options) {
        addRoute("*", pattern, handler, options);
    }

    /**
     * @brief Performs the `get` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void get(const SwString& pattern,
             const SwHttpHandler& handler) {
        get(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `get` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void get(const SwString& pattern,
             const SwHttpHandler& handler,
             const SwHttpRouteOptions& options) {
        addRoute("GET", pattern, handler, options);
    }

    /**
     * @brief Performs the `post` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void post(const SwString& pattern,
              const SwHttpHandler& handler) {
        post(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `post` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void post(const SwString& pattern,
              const SwHttpHandler& handler,
              const SwHttpRouteOptions& options) {
        addRoute("POST", pattern, handler, options);
    }

    /**
     * @brief Performs the `put` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void put(const SwString& pattern,
             const SwHttpHandler& handler) {
        put(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `put` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void put(const SwString& pattern,
             const SwHttpHandler& handler,
             const SwHttpRouteOptions& options) {
        addRoute("PUT", pattern, handler, options);
    }

    /**
     * @brief Performs the `patch` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void patch(const SwString& pattern,
               const SwHttpHandler& handler) {
        patch(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `patch` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void patch(const SwString& pattern,
               const SwHttpHandler& handler,
               const SwHttpRouteOptions& options) {
        addRoute("PATCH", pattern, handler, options);
    }

    /**
     * @brief Performs the `del` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void del(const SwString& pattern,
             const SwHttpHandler& handler) {
        del(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `del` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void del(const SwString& pattern,
             const SwHttpHandler& handler,
             const SwHttpRouteOptions& options) {
        addRoute("DELETE", pattern, handler, options);
    }

    /**
     * @brief Performs the `head` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void head(const SwString& pattern,
              const SwHttpHandler& handler) {
        head(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `head` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void head(const SwString& pattern,
              const SwHttpHandler& handler,
              const SwHttpRouteOptions& options) {
        addRoute("HEAD", pattern, handler, options);
    }

    /**
     * @brief Performs the `options` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     */
    void options(const SwString& pattern,
                 const SwHttpHandler& handler) {
        options(pattern, handler, SwHttpRouteOptions());
    }

    /**
     * @brief Performs the `options` operation.
     * @param pattern Pattern used by the operation.
     * @param handler Value passed to the method.
     * @param options Option set controlling the operation.
     */
    void options(const SwString& pattern,
                 const SwHttpHandler& handler,
                 const SwHttpRouteOptions& options) {
        addRoute("OPTIONS", pattern, handler, options);
    }

    /**
     * @brief Registers a WebSocket upgrade endpoint on the HTTP listener.
     * @param pattern Route pattern used to match the upgrade request.
     * @param handler Callback invoked with the accepted `SwWebSocket`.
     */
    void ws(const SwString& pattern,
            const SwHttpWebSocketHandler& handler) {
        ws(pattern, handler, SwHttpWebSocketRouteOptions());
    }

    /**
     * @brief Registers a WebSocket upgrade endpoint on the HTTP listener.
     * @param pattern Route pattern used to match the upgrade request.
     * @param handler Callback invoked with the accepted `SwWebSocket`.
     * @param options Route options controlling naming and handshake capabilities.
     */
    void ws(const SwString& pattern,
            const SwHttpWebSocketHandler& handler,
            const SwHttpWebSocketRouteOptions& options) {
        if (!handler) {
            return;
        }

        SwHttpRouteOptions routeOptions;
        routeOptions.name = options.name;

        addRoute("GET", pattern, [this, handler, options](SwHttpContext& context) {
            const SwHttpRequest request = context.request();

            int failureStatus = 0;
            SwString failureMessage;
            if (!validateWebSocketUpgradeRequest_(request, failureStatus, failureMessage)) {
                if (failureStatus == 405) {
                    context.setHeader("allow", "GET");
                } else if (failureStatus == 426) {
                    context.setHeader("upgrade", "websocket");
                    context.setHeader("connection", "Upgrade");
                    context.setHeader("sec-websocket-version", "13");
                }
                context.text(failureMessage.isEmpty() ? swHttpStatusReason(failureStatus) : failureMessage,
                             failureStatus > 0 ? failureStatus : 400);
                return;
            }

            const SwHttpRequest requestCopy = request;
            const SwList<SwString> supportedSubprotocols = options.supportedSubprotocols;
            const bool enablePerMessageDeflate = options.enablePerMessageDeflate;

            context.switchToRawSocket([this, requestCopy, handler, supportedSubprotocols, enablePerMessageDeflate](SwAbstractSocket* socket, SwByteArray initialData) {
                SwWebSocket* ws = new SwWebSocket(SwWebSocket::ServerRole, &m_server);
                ws->setSupportedSubprotocols(supportedSubprotocols);
                ws->setPerMessageDeflateEnabled(enablePerMessageDeflate);
                SwObject::connect(ws, &SwWebSocket::disconnected, [ws]() {
                    ws->deleteLater();
                });

                if (!ws->acceptHttpUpgrade(socket,
                                           requestCopy.path,
                                           requestCopy.headers,
                                           requestCopy.isTls,
                                           std::move(initialData))) {
                    ws->deleteLater();
                    return;
                }

                try {
                    handler(ws, requestCopy);
                } catch (...) {
                    ws->abort();
                    ws->deleteLater();
                }
            }, false);
        }, routeOptions);
    }

    /**
     * @brief Registers a static-file handler for a URL prefix.
     * @param prefix Prefix used by the operation.
     * @param rootDir Root directory used by the operation.
     * @param options Option set controlling the operation.
     *
     * @details The mounted handler is later consulted to translate matching request paths into on-disk resources.
     */
    void mountStatic(const SwString& prefix, const SwString& rootDir, const SwHttpStaticOptions& options = SwHttpStaticOptions()) {
        m_server.mountStatic(resolvePath_(prefix), rootDir, options);
    }

    /**
     * @brief Sets the handler used when no route matches.
     * @param handler Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setNotFoundHandler(const SwHttpHandler& handler) {
        if (!handler) {
            m_server.setNotFoundHandler([](const SwHttpRequest& request) {
                SwHttpResponse response = swHttpTextResponse(404, "Not Found");
                response.closeConnection = !request.keepAlive;
                return response;
            });
            return;
        }

        const SwList<SwHttpMiddleware> middlewaresSnapshot = m_middlewares;
        const SwHttpRecoveryHandler recovery = m_recoveryHandler;
        const SwHttpRouteOptions routeOptions;
        m_server.setNotFoundHandler([middlewaresSnapshot, handler, recovery, routeOptions](const SwHttpRequest& request) {
            return buildResponse_(request, middlewaresSnapshot, handler, routeOptions, recovery, true);
        });
    }

    void addPreRouteHandler(const SwHttpPreRouteHandler& handler) {
        m_server.addPreRouteHandler(handler);
    }

    void clearPreRouteHandlers() {
        m_server.clearPreRouteHandlers();
    }

    void addPreRouteHandlerAsync(const SwHttpPreRouteAsyncHandler& handler) {
        m_server.addPreRouteHandlerAsync(handler);
    }

    void clearPreRouteHandlersAsync() {
        m_server.clearPreRouteHandlersAsync();
    }

    void useAuthContext(const SwString& pathPrefix = SwString("/")) {
        SwHttpAuthService* service = ensureAuthService_();
        use(pathPrefix, [service](SwHttpContext& ctx, const SwHttpNext& next) {
            if (service->isStarted()) {
                const SwString token =
                    service->extractRequestToken(ctx.headerValue("cookie"), ctx.headerValue("authorization"));
                if (!token.isEmpty()) {
                    SwHttpAuthIdentity identity;
                    SwString error;
                    if (service->resolveIdentityFromToken(token, &identity, &error)) {
                        service->applyIdentityToContext(ctx, identity);
                    }
                }
            }
            if (next) {
                next();
            }
        });
    }

    void mountAuthApi(const SwHttpAuthApiOptions& options = SwHttpAuthApiOptions(),
                      const SwHttpAuthHooks& hooks = SwHttpAuthHooks()) {
        if (m_authApiMounted) {
            return;
        }

        SwHttpAuthService* service = ensureAuthService_();
        service->setHooks(hooks);
        if (!options.routePrefix.trimmed().isEmpty() &&
            swHttpAuthDetail::normalizeRoutePrefix(options.routePrefix) != m_authConfig.routePrefix) {
            m_authConfig.routePrefix = swHttpAuthDetail::normalizeRoutePrefix(options.routePrefix);
            service->setConfig(m_authConfig);
        }

        const SwString basePrefix = resolvePath_(service->config().routePrefix);
        auto makeMessage = [](const SwString& key, const SwString& value) {
            SwJsonObject object;
            object[key] = value;
            return SwJsonValue(object);
        };
        auto makeOk = []() {
            SwJsonObject object;
            object["ok"] = true;
            return SwJsonValue(object);
        };
        auto makeAuth = [](const SwHttpAuthAccount& account) {
            return SwJsonValue(swHttpAuthServiceDetail::accountToJson_(account));
        };
        auto makeRecoveryCodeStatus = [](const SwHttpAuthRecoveryCodeStatus& status) {
            SwJsonObject object;
            object["totalCodes"] = status.totalCodes;
            object["remainingCodes"] = status.remainingCodes;
            object["generatedAt"] = status.generatedAt;
            return object;
        };
        auto secureCookie = [service](SwHttpContext& ctx) {
            return ctx.isTls() || service->config().publicBaseUrl.toLower().startsWith("https://");
        };
        auto clearCookie = [service, secureCookie](SwHttpContext& ctx) {
            ctx.setHeader("set-cookie",
                          swHttpAuthDetail::buildSessionCookie(service->config().sessionCookieName,
                                                               SwString(),
                                                               0,
                                                               secureCookie(ctx)));
        };
        auto requireStarted = [service, makeMessage](SwHttpContext& ctx) -> bool {
            if (service->isStarted()) {
                return true;
            }
            ctx.json(makeMessage("error", "Auth service not started"), 503);
            return false;
        };
        auto requestToken = [service](SwHttpContext& ctx) {
            return service->extractRequestToken(ctx.headerValue("cookie"), ctx.headerValue("authorization"));
        };
        auto resolveIdentity = [service, makeMessage, requestToken](SwHttpContext& ctx,
                                                                    SwHttpAuthIdentity& outIdentity,
                                                                    SwString& outToken) -> bool {
            outToken = requestToken(ctx);
            if (outToken.isEmpty()) {
                ctx.json(makeMessage("error", "Authentication required"), 401);
                return false;
            }
            SwString error;
            if (!service->resolveIdentityFromToken(outToken, &outIdentity, &error)) {
                ctx.json(makeMessage("error", error.isEmpty() ? SwString("Authentication required") : error), 401);
                return false;
            }
            service->applyIdentityToContext(ctx, outIdentity);
            return true;
        };
        auto respondSession = [service, makeAuth, secureCookie](SwHttpContext& ctx,
                                                                const SwString& rawToken,
                                                                const SwHttpAuthIdentity& identity) {
            ctx.setHeader("set-cookie",
                          swHttpAuthDetail::buildSessionCookie(service->config().sessionCookieName,
                                                               rawToken,
                                                               static_cast<long long>(service->config().sessionTtlMs / 1000ull),
                                                               secureCookie(ctx)));
            service->applyIdentityToContext(ctx, identity);

            SwJsonObject object;
            object["token"] = rawToken;
            object["tokenType"] = "Bearer";
            object["expiresAtMs"] = static_cast<long long>(identity.session.expiresAtMs);
            object["auth"] = makeAuth(identity.account);
            if (!identity.subject.isNull()) {
                object["subject"] = identity.subject;
            }
            ctx.json(SwJsonValue(object));
        };

        group(basePrefix, [&](SwHttpApp& auth) {
            auth.post("/register", [service, requireStarted, makeMessage, makeAuth](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwJsonObject body = document.object();
                SwHttpAuthAccount account;
                SwJsonValue subject;
                bool pending = false;
                const SwDbStatus status = service->registerAccount(body.value("email").toString(),
                                                                   body.value("password").toString(),
                                                                   body.value("payload"),
                                                                   &account,
                                                                   &subject,
                                                                   &pending,
                                                                   &error);
                if (!status.ok()) {
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), 400);
                    return;
                }
                SwJsonObject object;
                object["auth"] = makeAuth(account);
                object["pendingEmailVerification"] = pending;
                if (!subject.isNull()) {
                    object["subject"] = subject;
                }
                ctx.json(SwJsonValue(object), 201);
            });

            auth.post("/login", [service, requireStarted, makeMessage, respondSession](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                SwString rawToken;
                SwHttpAuthIdentity identity;
                SwHttpAuthMfaLoginChallenge mfaChallenge;
                const SwDbStatus status = service->login(document.object().value("email").toString(),
                                                         document.object().value("password").toString(),
                                                         ctx.headerValue("user-agent"),
                                                         ctx.isTls(),
                                                         &rawToken,
                                                         &identity,
                                                         nullptr,
                                                         &error,
                                                         &mfaChallenge);
                if (!status.ok()) {
                    if (!mfaChallenge.challengeToken.isEmpty()) {
                        SwJsonObject object;
                        object["mfaRequired"] = true;
                        object["mfaToken"] = mfaChallenge.challengeToken;
                        object["email"] = mfaChallenge.email;
                        object["expiresAtMs"] = static_cast<long long>(mfaChallenge.expiresAtMs);
                        ctx.json(SwJsonValue(object));
                        return;
                    }
                    if (status.code() == SwDbStatus::Busy &&
                        (error == "Password reset required" || status.message() == "Password reset required")) {
                        SwJsonObject object;
                        object["error"] =
                            (error.isEmpty() ? SwString("Password reset required") : error);
                        object["passwordResetRequired"] = true;
                        ctx.json(SwJsonValue(object), 403);
                        return;
                    }
                    const int httpStatus = status.code() == SwDbStatus::Busy ? 403 : 401;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                respondSession(ctx, rawToken, identity);
            });

            auth.post("/mfa/totp/login", [service, requireStarted, makeMessage, respondSession](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }

                SwString rawToken;
                SwHttpAuthIdentity identity;
                const SwDbStatus status = service->verifyTotpLogin(document.object().value("mfaToken").toString(),
                                                                   document.object().value("code").toString(),
                                                                   ctx.headerValue("user-agent"),
                                                                   ctx.isTls(),
                                                                   &rawToken,
                                                                   &identity,
                                                                   &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::Busy ? 403 : 401;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                respondSession(ctx, rawToken, identity);
            });

            auth.get("/mfa/totp", [service, requireStarted, makeMessage, makeAuth, resolveIdentity](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthIdentity identity;
                SwString rawToken;
                if (!resolveIdentity(ctx, identity, rawToken)) {
                    return;
                }
                SwJsonObject object;
                object["enabled"] = identity.account.mfaTotpEnabled;
                object["enabledAt"] = identity.account.mfaTotpEnabledAt;
                object["auth"] = makeAuth(identity.account);
                ctx.json(SwJsonValue(object));
            });

            auth.post("/mfa/totp/setup", [service, requireStarted, makeMessage, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }

                SwHttpAuthMfaTotpSetup setup;
                const SwDbStatus status =
                    service->beginTotpSetup(requestToken(ctx),
                                            document.object().value("currentPassword").toString(),
                                            &setup,
                                            &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 :
                                           (status.code() == SwDbStatus::Busy ? 409 : 400);
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }

                SwJsonObject object;
                object["setupToken"] = setup.setupToken;
                object["secret"] = setup.secret;
                object["otpauthUri"] = setup.otpauthUri;
                object["issuer"] = setup.issuer;
                object["label"] = setup.label;
                object["digits"] = setup.digits;
                object["periodSeconds"] = setup.periodSeconds;
                object["expiresAtMs"] = static_cast<long long>(setup.expiresAtMs);
                ctx.json(SwJsonValue(object));
            });

            auth.post("/mfa/totp/confirm", [service, requireStarted, makeMessage, makeAuth, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }

                SwHttpAuthIdentity identity;
                const SwDbStatus status =
                    service->confirmTotpSetup(requestToken(ctx),
                                              document.object().value("setupToken").toString(),
                                              document.object().value("code").toString(),
                                              &identity,
                                              &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::Busy ? 403 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }

                SwJsonObject object;
                object["enabled"] = true;
                object["enabledAt"] = identity.account.mfaTotpEnabledAt;
                object["auth"] = makeAuth(identity.account);
                ctx.json(SwJsonValue(object));
            });

            auth.post("/mfa/totp/disable", [service, requireStarted, makeMessage, makeAuth, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }

                SwHttpAuthIdentity identity;
                const SwDbStatus status =
                    service->disableTotp(requestToken(ctx),
                                         document.object().value("currentPassword").toString(),
                                         &identity,
                                         &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }

                SwJsonObject object;
                object["enabled"] = false;
                object["enabledAt"] = "";
                object["auth"] = makeAuth(identity.account);
                ctx.json(SwJsonValue(object));
            });

            auth.get("/mfa/recovery-codes", [service, requireStarted, makeMessage, makeRecoveryCodeStatus, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }

                SwString error;
                SwHttpAuthRecoveryCodeStatus recoveryStatus;
                const SwDbStatus status = service->getRecoveryCodeStatus(requestToken(ctx),
                                                                         &recoveryStatus,
                                                                         &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                ctx.json(SwJsonValue(makeRecoveryCodeStatus(recoveryStatus)));
            });

            auth.post("/mfa/recovery-codes", [service, requireStarted, makeMessage, makeRecoveryCodeStatus, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }

                SwHttpAuthRecoveryCodeGeneration generation;
                const SwDbStatus status =
                    service->generateRecoveryCodes(requestToken(ctx),
                                                   document.object().value("currentPassword").toString(),
                                                   &generation,
                                                   &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }

                SwJsonArray codes;
                for (std::size_t i = 0; i < generation.codes.size(); ++i) {
                    codes.append(generation.codes[i]);
                }
                SwJsonObject object = makeRecoveryCodeStatus(generation.status);
                object["codes"] = codes;
                ctx.json(SwJsonValue(object));
            });

            auth.del("/mfa/recovery-codes", [service, requireStarted, makeMessage, makeRecoveryCodeStatus, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }

                SwHttpAuthRecoveryCodeStatus recoveryStatus;
                const SwDbStatus status =
                    service->clearRecoveryCodes(requestToken(ctx),
                                                document.object().value("currentPassword").toString(),
                                                &recoveryStatus,
                                                &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                ctx.json(SwJsonValue(makeRecoveryCodeStatus(recoveryStatus)));
            });

            auth.post("/logout", [service, requireStarted, makeOk, clearCookie, requestToken](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                const SwString token = requestToken(ctx);
                (void)service->logout(token);
                clearCookie(ctx);
                ctx.json(makeOk());
            });

            auth.get("/me", [service, requireStarted, makeMessage, makeAuth, resolveIdentity](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthIdentity identity;
                SwString rawToken;
                if (!resolveIdentity(ctx, identity, rawToken)) {
                    return;
                }
                SwJsonObject object;
                object["auth"] = makeAuth(identity.account);
                if (!identity.subject.isNull()) {
                    object["subject"] = identity.subject;
                }
                ctx.json(SwJsonValue(object));
            });

            auth.post("/email/resend-verification", [service, requireStarted, makeOk](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (ctx.parseJsonBody(document, error) && document.isObject()) {
                    (void)service->requestEmailVerification(document.object().value("email").toString(), &error);
                }
                ctx.json(makeOk());
            });

            auth.post("/email/verify", [service, requireStarted, makeMessage, makeAuth](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                SwHttpAuthAccount account;
                SwJsonValue subject;
                const SwDbStatus status = service->verifyEmail(document.object().value("code").toString(),
                                                               document.object().value("token").toString(),
                                                               &account,
                                                               &subject,
                                                               &error);
                if (!status.ok()) {
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), 400);
                    return;
                }
                SwJsonObject object;
                object["ok"] = true;
                object["auth"] = makeAuth(account);
                if (!subject.isNull()) {
                    object["subject"] = subject;
                }
                ctx.json(SwJsonValue(object));
            });

            auth.get("/email/verify", [service, requireStarted, makeMessage, makeAuth](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthAccount account;
                SwJsonValue subject;
                SwString error;
                const SwDbStatus status =
                    service->verifyEmail(SwString(), ctx.queryValue("token"), &account, &subject, &error);
                if (!status.ok()) {
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), 400);
                    return;
                }
                SwJsonObject object;
                object["ok"] = true;
                object["auth"] = makeAuth(account);
                if (!subject.isNull()) {
                    object["subject"] = subject;
                }
                ctx.json(SwJsonValue(object));
            });

            auth.post("/email/change/request", [service, requireStarted, makeMessage, makeOk, resolveIdentity](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthIdentity currentIdentity;
                SwString rawToken;
                if (!resolveIdentity(ctx, currentIdentity, rawToken)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwJsonObject object = document.object();
                const SwDbStatus status =
                    service->requestEmailChange(rawToken,
                                                object.value("currentPassword").toString(),
                                                object.value("newEmail").toString(),
                                                &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                ctx.json(makeOk());
            });

            auth.post("/email/change/confirm", [service, requireStarted, makeMessage, makeAuth, resolveIdentity](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthIdentity currentIdentity;
                SwString rawToken;
                if (!resolveIdentity(ctx, currentIdentity, rawToken)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwJsonObject object = document.object();
                SwHttpAuthIdentity updatedIdentity;
                const SwDbStatus status =
                    service->confirmEmailChange(rawToken,
                                                object.value("code").toString(),
                                                object.value("token").toString(),
                                                &updatedIdentity,
                                                &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 404 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                service->applyIdentityToContext(ctx, updatedIdentity);
                SwJsonObject response;
                response["ok"] = true;
                response["auth"] = makeAuth(updatedIdentity.account);
                if (!updatedIdentity.subject.isNull()) {
                    response["subject"] = updatedIdentity.subject;
                }
                ctx.json(SwJsonValue(response));
            });

            auth.post("/password/forgot", [service, requireStarted, makeOk](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (ctx.parseJsonBody(document, error) && document.isObject()) {
                    (void)service->requestPasswordReset(document.object().value("email").toString(), &error);
                }
                ctx.json(makeOk());
            });

            auth.get("/password/reset", [service, requireStarted, makeMessage](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                if (ctx.queryValue("token").trimmed().isEmpty()) {
                    ctx.json(makeMessage("error", "Invalid reset token"), 400);
                    return;
                }
                SwJsonObject object;
                object["ok"] = true;
                object["purpose"] = "reset_password";
                object["codeRequired"] = true;
                ctx.json(SwJsonValue(object));
            });

            auth.post("/password/change/request", [service, requireStarted, makeMessage, makeOk, resolveIdentity](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthIdentity currentIdentity;
                SwString rawToken;
                if (!resolveIdentity(ctx, currentIdentity, rawToken)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwDbStatus status =
                    service->requestPasswordChange(rawToken,
                                                   document.object().value("currentPassword").toString(),
                                                   &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                ctx.json(makeOk());
            });

            auth.post("/password/reset", [service, requireStarted, makeMessage, makeOk, clearCookie](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwJsonObject object = document.object();
                const SwString code = object.value("code").toString();
                const SwString token = object.value("token").toString();
                const SwString newPassword = object.contains("newPassword")
                                                 ? SwString(object.value("newPassword").toString())
                                                 : SwString();
                const SwDbStatus status = service->resetPassword(code,
                                                                 token,
                                                                 newPassword,
                                                                 &error);
                if (!status.ok()) {
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), 400);
                    return;
                }
                clearCookie(ctx);
                ctx.json(makeOk());
            });

            auth.post("/password/change", [service, requireStarted, makeMessage, makeAuth, resolveIdentity](SwHttpContext& ctx) {
                if (!requireStarted(ctx)) {
                    return;
                }
                SwHttpAuthIdentity currentIdentity;
                SwString rawToken;
                if (!resolveIdentity(ctx, currentIdentity, rawToken)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                SwHttpAuthIdentity updatedIdentity;
                const SwDbStatus status =
                    service->changePassword(rawToken,
                                            document.object().value("currentPassword").toString(),
                                            document.object().value("newPassword").toString(),
                                            &updatedIdentity,
                                            &error);
                if (!status.ok()) {
                    const int httpStatus = status.code() == SwDbStatus::NotFound ? 401 : 400;
                    ctx.json(makeMessage("error", error.isEmpty() ? status.message() : error), httpStatus);
                    return;
                }
                service->applyIdentityToContext(ctx, updatedIdentity);
                SwJsonObject object;
                object["ok"] = true;
                object["auth"] = makeAuth(updatedIdentity.account);
                if (!updatedIdentity.subject.isNull()) {
                    object["subject"] = updatedIdentity.subject;
                }
                ctx.json(SwJsonValue(object));
            });
        });

        m_authApiMounted = true;
    }

    void mountMailAdminApi(const SwMailAdminApiOptions& options = SwMailAdminApiOptions(),
                           const SwMailAdminGuard& guard = SwMailAdminGuard()) {
        if (m_mailAdminMounted) {
            return;
        }

        SwMailService* service = ensureMailService_();
        const SwString basePrefix =
            resolvePath_(options.routePrefix.trimmed().isEmpty() ? m_mailConfig.adminRoutePrefix : options.routePrefix);

        auto requireGuard = [guard](SwHttpContext& ctx) -> bool {
            if (!guard) {
                SwJsonObject object;
                object["error"] = "Mail admin guard missing";
                ctx.json(SwJsonValue(object), 403);
                return false;
            }
            if (!guard(ctx)) {
                if (!ctx.handled()) {
                    SwJsonObject object;
                    object["error"] = "Forbidden";
                    ctx.json(SwJsonValue(object), 403);
                }
                return false;
            }
            return true;
        };
        auto makeJsonMessage = [](const SwString& key, const SwString& value) {
            SwJsonObject object;
            object[key] = value;
            return SwJsonValue(object);
        };
        auto makeJsonBool = [](const SwString& key, bool value) {
            SwJsonObject object;
            object[key] = value;
            return SwJsonValue(object);
        };

        group(basePrefix, [&](SwHttpApp& mail) {
            mail.get("/config", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonObject object;
                object["domain"] = service->config().domain;
                object["mailHost"] = service->config().mailHost;
                object["smtpPort"] = static_cast<long long>(service->config().smtpPort);
                object["submissionPort"] = static_cast<long long>(service->config().submissionPort);
                object["imapsPort"] = static_cast<long long>(service->config().imapsPort);
                object["tlsReady"] = service->hasTlsCredentials();
                object["started"] = service->isStarted();
                object["adminRoutePrefix"] = service->config().adminRoutePrefix;
                SwJsonObject relayObject;
                relayObject["enabled"] = !service->config().outboundRelay.host.trimmed().isEmpty();
                relayObject["host"] = service->config().outboundRelay.host;
                relayObject["port"] = static_cast<long long>(service->config().outboundRelay.port);
                relayObject["implicitTls"] = service->config().outboundRelay.implicitTls;
                relayObject["startTls"] = service->config().outboundRelay.startTls;
                relayObject["authConfigured"] = !service->config().outboundRelay.username.isEmpty();
                relayObject["trustedCaConfigured"] = !service->config().outboundRelay.trustedCaFile.isEmpty();
                object["outboundRelay"] = relayObject;
                ctx.json(SwJsonValue(object));
            });

            mail.get("/accounts", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonArray array;
                const SwList<SwMailAccount> accounts = service->listAccounts();
                for (std::size_t i = 0; i < accounts.size(); ++i) {
                    SwJsonObject object;
                    object["address"] = accounts[i].address;
                    object["active"] = accounts[i].active;
                    object["suspended"] = accounts[i].suspended;
                    object["quotaBytes"] = static_cast<long long>(accounts[i].quotaBytes);
                    object["usedBytes"] = static_cast<long long>(accounts[i].usedBytes);
                    object["createdAt"] = accounts[i].createdAt;
                    object["updatedAt"] = accounts[i].updatedAt;
                    array.append(object);
                }
                ctx.json(SwJsonDocument(array));
            });

            mail.post("/accounts", [service, requireGuard, makeJsonMessage](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeJsonMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwJsonObject body = document.object();
                const SwString address = body.value("address").toString();
                const SwString password = body.value("password").toString();
                SwMailAccount created;
                const SwDbStatus status = service->createAccount(address, password, &created);
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 400);
                    return;
                }
                SwJsonObject object;
                object["address"] = created.address;
                object["createdAt"] = created.createdAt;
                ctx.json(SwJsonValue(object), 201);
            });

            mail.post("/accounts/:address/password", [service, requireGuard, makeJsonMessage, makeJsonBool](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeJsonMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const SwString password = document.object().value("password").toString();
                const SwDbStatus status = service->setAccountPassword(ctx.pathValue("address"), password);
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 400);
                    return;
                }
                ctx.json(makeJsonBool("ok", true));
            });

            mail.post("/accounts/:address/suspend", [service, requireGuard, makeJsonMessage, makeJsonBool](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeJsonMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                const bool suspended = document.object().value("suspended").toBool(true);
                const SwDbStatus status = service->setAccountSuspended(ctx.pathValue("address"), suspended);
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 400);
                    return;
                }
                ctx.json(makeJsonBool("ok", true));
            });

            mail.get("/aliases", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonArray array;
                const SwList<SwMailAlias> aliases = service->listAliases();
                for (std::size_t i = 0; i < aliases.size(); ++i) {
                    SwJsonObject object;
                    object["address"] = aliases[i].address;
                    object["targets"] = swMailDetail::toJsonArray(aliases[i].targets);
                    object["active"] = aliases[i].active;
                    array.append(object);
                }
                ctx.json(SwJsonDocument(array));
            });

            mail.post("/aliases", [service, requireGuard, makeJsonMessage, makeJsonBool](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonDocument document;
                SwString error;
                if (!ctx.parseJsonBody(document, error) || !document.isObject()) {
                    ctx.json(makeJsonMessage("error", "Invalid JSON body"), 400);
                    return;
                }
                SwMailAlias alias;
                alias.address = document.object().value("address").toString();
                alias.active = document.object().value("active").toBool(true);
                alias.targets = swMailDetail::fromJsonStringArray(document.object().value("targets"));
                SwString localPart;
                SwString domain;
                if (!swMailDetail::splitAddress(alias.address, localPart, domain)) {
                    ctx.json(makeJsonMessage("error", "Invalid alias address"), 400);
                    return;
                }
                for (std::size_t i = 0; i < alias.targets.size(); ++i) {
                    SwString targetLocal;
                    SwString targetDomain;
                    if (!swMailDetail::splitAddress(alias.targets[i], targetLocal, targetDomain)) {
                        ctx.json(makeJsonMessage("error", "Invalid alias target: " + alias.targets[i]), 400);
                        return;
                    }
                }
                alias.domain = domain;
                alias.localPart = localPart;
                const SwDbStatus status = service->upsertAlias(alias);
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 400);
                    return;
                }
                ctx.json(makeJsonBool("ok", true));
            });

            mail.del("/aliases/:address", [service, requireGuard, makeJsonMessage, makeJsonBool](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                const SwDbStatus status = service->deleteAlias(ctx.pathValue("address"));
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 404);
                    return;
                }
                ctx.json(makeJsonBool("ok", true));
            });

            mail.get("/mailboxes/:address", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonArray array;
                const SwList<SwMailMailbox> mailboxes = service->listMailboxes(ctx.pathValue("address"));
                for (std::size_t i = 0; i < mailboxes.size(); ++i) {
                    SwJsonObject object;
                    object["name"] = mailboxes[i].name;
                    object["uidNext"] = static_cast<long long>(mailboxes[i].uidNext);
                    object["totalCount"] = static_cast<long long>(mailboxes[i].totalCount);
                    object["unseenCount"] = static_cast<long long>(mailboxes[i].unseenCount);
                    array.append(object);
                }
                ctx.json(SwJsonDocument(array));
            });

            mail.get("/messages/:address/:mailbox", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonArray array;
                const SwList<SwMailMessageEntry> messages =
                    service->listMessages(ctx.pathValue("address"), ctx.pathValue("mailbox"));
                for (std::size_t i = 0; i < messages.size(); ++i) {
                    SwJsonObject object;
                    object["uid"] = static_cast<long long>(messages[i].uid);
                    object["subject"] = messages[i].subject;
                    object["from"] = messages[i].from;
                    object["to"] = swMailDetail::toJsonArray(messages[i].to);
                    object["flags"] = swMailDetail::toJsonArray(messages[i].flags);
                    object["sizeBytes"] = static_cast<long long>(messages[i].sizeBytes);
                    object["internalDate"] = messages[i].internalDate;
                    array.append(object);
                }
                ctx.json(SwJsonDocument(array));
            });

            mail.get("/messages/:address/:mailbox/:uid", [service, requireGuard, makeJsonMessage](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                const unsigned long long uid =
                    static_cast<unsigned long long>(std::strtoull(ctx.pathValue("uid").toStdString().c_str(), nullptr, 10));
                SwMailMessageEntry message;
                const SwDbStatus status = service->getMessage(ctx.pathValue("address"), ctx.pathValue("mailbox"), uid, &message);
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 404);
                    return;
                }
                SwJsonObject object;
                object["uid"] = static_cast<long long>(message.uid);
                object["subject"] = message.subject;
                object["from"] = message.from;
                object["to"] = swMailDetail::toJsonArray(message.to);
                object["flags"] = swMailDetail::toJsonArray(message.flags);
                object["raw"] = SwString(message.rawMessage.toBase64());
                ctx.json(SwJsonValue(object));
            });

            mail.get("/queue", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonArray array;
                const SwList<SwMailQueueItem> items = service->listQueueItems();
                for (std::size_t i = 0; i < items.size(); ++i) {
                    SwJsonObject object;
                    object["id"] = items[i].id;
                    object["mailFrom"] = items[i].envelope.mailFrom;
                    object["rcptTo"] = swMailDetail::toJsonArray(items[i].envelope.rcptTo);
                    object["attemptCount"] = items[i].attemptCount;
                    object["nextAttemptAtMs"] = items[i].nextAttemptAtMs;
                    object["expireAtMs"] = items[i].expireAtMs;
                    object["lastError"] = items[i].lastError;
                    array.append(object);
                }
                ctx.json(SwJsonDocument(array));
            });

            mail.post("/queue/:id/retry", [service, requireGuard, makeJsonMessage, makeJsonBool](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwMailQueueItem item;
                const SwDbStatus status = service->store().getQueueItem(ctx.pathValue("id"), &item);
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 404);
                    return;
                }
                item.nextAttemptAtMs = swMailDetail::currentEpochMs();
                item.updatedAtMs = item.nextAttemptAtMs;
                item.lastError.clear();
                const SwDbStatus writeStatus = service->store().storeQueueItem(item);
                if (!writeStatus.ok()) {
                    ctx.json(makeJsonMessage("error", writeStatus.message()), 400);
                    return;
                }
                ctx.json(makeJsonBool("ok", true));
            });

            mail.del("/queue/:id", [service, requireGuard, makeJsonMessage, makeJsonBool](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                const SwDbStatus status = service->store().removeQueueItem(ctx.pathValue("id"));
                if (!status.ok()) {
                    ctx.json(makeJsonMessage("error", status.message()), 404);
                    return;
                }
                ctx.json(makeJsonBool("ok", true));
            });

            mail.get("/dkim", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                SwJsonArray array;
                const SwList<SwMailDkimRecord> records = service->listDkimRecords();
                for (std::size_t i = 0; i < records.size(); ++i) {
                    SwJsonObject object;
                    object["domain"] = records[i].domain;
                    object["selector"] = records[i].selector;
                    object["publicKeyTxt"] = records[i].publicKeyTxt;
                    array.append(object);
                }
                ctx.json(SwJsonDocument(array));
            });

            mail.get("/metrics", [service, requireGuard](SwHttpContext& ctx) {
                if (!requireGuard(ctx)) {
                    return;
                }
                const SwMailMetrics metrics = service->metricsSnapshot();
                SwJsonObject object;
                object["smtpSessions"] = static_cast<long long>(metrics.smtpSessions);
                object["submissionSessions"] = static_cast<long long>(metrics.submissionSessions);
                object["imapSessions"] = static_cast<long long>(metrics.imapSessions);
                object["inboundAccepted"] = static_cast<long long>(metrics.inboundAccepted);
                object["localDeliveries"] = static_cast<long long>(metrics.localDeliveries);
                object["outboundQueued"] = static_cast<long long>(metrics.outboundQueued);
                object["outboundDelivered"] = static_cast<long long>(metrics.outboundDelivered);
                object["outboundDeferred"] = static_cast<long long>(metrics.outboundDeferred);
                object["outboundFailed"] = static_cast<long long>(metrics.outboundFailed);
                object["authFailures"] = static_cast<long long>(metrics.authFailures);
                object["forwardedMessages"] = static_cast<long long>(metrics.forwardedMessages);
                object["forwardLoopsDropped"] = static_cast<long long>(metrics.forwardLoopsDropped);
                object["srsBouncesRouted"] = static_cast<long long>(metrics.srsBouncesRouted);
                object["inboundSpfFail"] = static_cast<long long>(metrics.inboundSpfFail);
                object["inboundDkimPass"] = static_cast<long long>(metrics.inboundDkimPass);
                object["inboundDmarcPass"] = static_cast<long long>(metrics.inboundDmarcPass);
                object["inboundDmarcReject"] = static_cast<long long>(metrics.inboundDmarcReject);
                ctx.json(SwJsonValue(object));
            });
        });

        m_mailAdminMounted = true;
    }

private:
    SwHttpServer m_server;
    SwQuicHttp3Server m_http3Server;
    SwHttp3Config m_http3Config;
    SwQuicServerCredential m_http3Credential;
    SwQuicHttp3Server::CredentialMap m_http3Credentials;
    bool m_hasHttp3Credential = false;
    SwString m_http3AdvertisedHost;
    SwString m_lastHttp3Error;
    SwList<SwHttpMiddleware> m_middlewares;
    SwString m_groupPrefix;
    SwHttpRecoveryHandler m_recoveryHandler;
    SwHttpSeo m_seo;
    SwMailConfig m_mailConfig;
    SwDomainTlsConfig m_domainTlsConfig;
    SwHttpAuthConfig m_authConfig;
    SwMailService* m_mailService = nullptr;
    SwHttpAuthService* m_authService = nullptr;
    bool m_mailAdminMounted = false;
    bool m_authApiMounted = false;
    bool m_seoMounted = false;

    void mountSeoDiscoveryRoutes_(const SwHttpSeoConfig& config) {
        if (config.enableRobotsTxt) {
            SwHttpRouteOptions options;
            options.name = "seo.robots";
            get(config.robotsPath, [this](SwHttpContext& context) {
                applySeoDiscoveryHeaders_(context);
                context.text(m_seo.robotsText(), 200);
            }, options);
        }

        if (config.enableSitemap) {
            SwHttpRouteOptions options;
            options.name = "seo.sitemap";
            get(config.sitemapPath, [this](SwHttpContext& context) {
                applySeoDiscoveryHeaders_(context);
                context.send(m_seo.sitemapXml(), "application/xml; charset=utf-8", 200);
            }, options);
        }

        if (config.enableLlmsTxt) {
            SwHttpRouteOptions options;
            options.name = "seo.llms";
            get(config.llmsPath, [this](SwHttpContext& context) {
                applySeoDiscoveryHeaders_(context);
                context.send(m_seo.llmsText(), "text/markdown; charset=utf-8", 200);
            }, options);
        }
    }

    void applySeoDiscoveryHeaders_(SwHttpContext& context) const {
        const SwHttpSeoConfig config = m_seo.config();
        if (!config.discoveryCacheControl.isEmpty()) {
            context.response().headers["cache-control"] = config.discoveryCacheControl;
        }
        context.response().headers["x-content-type-options"] = "nosniff";
        context.response().headers["x-robots-tag"] = "noindex";
    }

    void applyHttp3Config_() {
        m_http3Server.setMaxClients(m_http3Config.maxClients);
        m_http3Server.setMaxPendingHandshakes(m_http3Config.maxPendingHandshakes);
        m_http3Server.setHandshakeTimeoutMs(m_http3Config.handshakeTimeoutMs);
        m_http3Server.setHttpLimits(m_server.limits());
    }

    void setHttp3Error_(SwString* output, const SwString& error) {
        m_lastHttp3Error = error;
        if (output) {
            *output = error;
        }
    }

    static bool selectHttp3TlsCredential_(
        const SwList<SwTlsCredentialEntry>& credentials,
        SwTlsCredentialEntry& selected) {
        bool found = false;
        for (std::size_t i = 0; i < credentials.size(); ++i) {
            const SwTlsCredentialEntry& candidate = credentials[i];
            if (candidate.certPath.trimmed().isEmpty() ||
                candidate.keyPath.trimmed().isEmpty()) {
                continue;
            }
            if (!found) {
                selected = candidate;
                found = true;
            }
            // Match SwBackendSsl exactly: the first valid credential is the
            // fallback, and every explicit default replaces the previous one.
            // Consequently the last isDefault entry wins. An empty SNI host is
            // not an implicit default once a valid credential already exists.
            if (candidate.isDefault) {
                selected = candidate;
            }
        }
        return found;
    }

    bool loadHttp3TlsCredentials_(
        const SwList<SwTlsCredentialEntry>& credentials,
        SwQuicServerCredential& defaultCredential,
        SwQuicHttp3Server::CredentialMap& credentialsByHost) {
        SwTlsCredentialEntry selected;
        if (!selectHttp3TlsCredential_(credentials, selected)) {
            m_lastHttp3Error = SwString("No valid TLS credential is available for HTTP/3");
            return false;
        }

        credentialsByHost.clear();
        bool loadedDefault = false;
        for (std::size_t i = 0; i < credentials.size(); ++i) {
            const SwTlsCredentialEntry& entry = credentials[i];
            if (entry.certPath.trimmed().isEmpty() || entry.keyPath.trimmed().isEmpty()) {
                continue;
            }
            SwQuicServerCredential loaded;
            if (!SwQuicPemCredential::load(entry.certPath, entry.keyPath,
                                            loaded, &m_lastHttp3Error)) {
                return false;
            }
            if (entry.certPath == selected.certPath && entry.keyPath == selected.keyPath &&
                entry.host == selected.host && entry.isDefault == selected.isDefault) {
                defaultCredential = loaded;
                loadedDefault = true;
            }
            const SwString host = entry.host.trimmed().toLower();
            if (!host.isEmpty()) {
                credentialsByHost[host] = loaded;
            }
        }
        return loadedDefault && defaultCredential.isValid();
    }

    bool startHttp3ForSecureOrigin_(const SwString& bindAddress,
                                    uint16_t requestedPort,
                                    const SwQuicServerCredential& credential,
                                    const SwQuicHttp3Server::CredentialMap& credentialsByHost,
                                    const SwString& advertisedHost,
                                    uint16_t& selectedPort) {
        selectedPort = requestedPort;
        if (!m_http3Config.enabled) {
            m_http3Server.setAutomaticPollingEnabled(false);
            m_http3Server.close();
            return true;
        }
        setHttp3Credentials(credential, credentialsByHost);
        m_http3AdvertisedHost = advertisedHost.trimmed().toLower();
        SwString error;
        if (!listenHttp3(bindAddress, requestedPort, &error)) {
            return false;
        }
        selectedPort = m_http3Server.localPort();
        return true;
    }

    bool listenSecureTransports_(const SwString& bindAddress,
                                 uint16_t port,
                                 const SwString& certPath,
                                 const SwString& keyPath,
                                 bool replaceListeners) {
        if (!m_http3Config.enabled) {
            return replaceListeners
                       ? m_server.listen(bindAddress, port, certPath, keyPath)
                       : m_server.listenHttps(bindAddress, port, certPath, keyPath);
        }

        SwQuicServerCredential credential;
        if (!SwQuicPemCredential::load(certPath, keyPath, credential, &m_lastHttp3Error)) {
            return false;
        }
        if (replaceListeners) {
            m_http3Server.setAutomaticPollingEnabled(false);
            m_http3Server.close();
        }
        const SwQuicHttp3Server::CredentialMap noNamedCredentials;
        const int attempts = port == 0 ? 32 : 1;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            // On Windows, immediately reopening TCP port 0 can repeatedly
            // yield the same numeric port. If UDP already owns that number,
            // retrying TCP-first therefore never makes progress. For an
            // ephemeral dual-transport origin, reserve UDP first and bind TCP
            // to its selected port; a TCP collision then produces a fresh UDP
            // choice on the next attempt.
            if (port == 0) {
                uint16_t selectedPort = 0;
                if (!startHttp3ForSecureOrigin_(bindAddress, 0, credential,
                                                noNamedCredentials, SwString(),
                                                selectedPort)) {
                    return false;
                }
                const bool tcpListening = replaceListeners
                                              ? m_server.listen(bindAddress, selectedPort,
                                                                certPath, keyPath)
                                              : m_server.listenHttps(bindAddress, selectedPort,
                                                                     certPath, keyPath);
                if (tcpListening) {
                    m_lastHttp3Error = SwString();
                    return true;
                }
                m_http3Server.setAutomaticPollingEnabled(false);
                m_http3Server.close();
                continue;
            }
            const bool tcpListening = replaceListeners
                                          ? m_server.listen(bindAddress, port,
                                                            certPath, keyPath)
                                          : m_server.listenHttps(bindAddress, port,
                                                                 certPath, keyPath);
            if (!tcpListening) {
                m_http3Server.setAutomaticPollingEnabled(false);
                m_http3Server.close();
                return false;
            }

            uint16_t selectedPort = m_server.httpsPort();
            if (startHttp3ForSecureOrigin_(bindAddress, selectedPort, credential,
                                           noNamedCredentials, SwString(), selectedPort)) {
                m_lastHttp3Error = SwString();
                return true;
            }

            // TCP and UDP have independent ephemeral/excluded-port pools on
            // Windows. With port 0, retry until the OS gives us a numeric port
            // that both transports can own.
            m_server.closeHttpsListener();
            if (port != 0) {
                break;
            }
        }
        return false;
    }

    bool listenSecureTransports_(const SwString& bindAddress,
                                 uint16_t port,
                                 const SwList<SwTlsCredentialEntry>& credentials) {
        if (!m_http3Config.enabled) {
            return m_server.listenHttps(bindAddress, port, credentials);
        }

        SwQuicServerCredential credential;
        SwQuicHttp3Server::CredentialMap credentialsByHost;
        if (!loadHttp3TlsCredentials_(credentials, credential, credentialsByHost)) {
            return false;
        }
        const int attempts = port == 0 ? 32 : 1;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            if (port == 0) {
                uint16_t selectedPort = 0;
                if (!startHttp3ForSecureOrigin_(bindAddress, 0, credential,
                                                credentialsByHost, SwString(),
                                                selectedPort)) {
                    return false;
                }
                if (m_server.listenHttps(bindAddress, selectedPort, credentials)) {
                    m_lastHttp3Error = SwString();
                    return true;
                }
                m_http3Server.setAutomaticPollingEnabled(false);
                m_http3Server.close();
                continue;
            }
            if (!m_server.listenHttps(bindAddress, port, credentials)) {
                m_http3Server.setAutomaticPollingEnabled(false);
                m_http3Server.close();
                return false;
            }
            uint16_t selectedPort = m_server.httpsPort();
            if (startHttp3ForSecureOrigin_(bindAddress, selectedPort, credential,
                                           credentialsByHost, SwString(), selectedPort)) {
                m_lastHttp3Error = SwString();
                return true;
            }
            m_server.closeHttpsListener();
            if (port != 0) {
                break;
            }
        }
        return false;
    }

    void addHttp3DiscoveryHeader_(const SwHttpRequest& request,
                                  SwHttpResponse& response) const {
        if (!m_http3Config.enabled || !m_http3Config.advertise ||
            !m_http3Server.isListening() || !request.isTls) {
            return;
        }

        if (!m_http3AdvertisedHost.isEmpty()) {
            const SwString host = request.headers.value("host").trimmed().toLower();
            if (host != m_http3AdvertisedHost &&
                !host.startsWith(m_http3AdvertisedHost + SwString(":"))) {
                return;
            }
        }

        for (SwMap<SwString, SwString>::const_iterator it = response.headers.begin();
             it != response.headers.end(); ++it) {
            if (it.key().toLower() == SwString("alt-svc")) {
                return;
            }
        }
        const int maxAge = (std::max)(0, m_http3Config.altSvcMaxAgeSeconds);
        response.headers["alt-svc"] =
            SwString("h3=\":") + SwString::number(m_http3Server.localPort()) +
            SwString("\"; ma=") + SwString::number(maxAge);
    }

    bool synchronizeHttp3ListenerAfterReload_() {
        if (!m_http3Config.enabled) return true;
        const uint16_t port = m_server.httpsPort();
        if (port == 0) {
            m_lastHttp3Error = SwString(
                "HTTPS credential reload did not leave a TCP listener active");
            return false;
        }
        if (m_http3Server.isListening() && m_http3Server.localPort() == port) {
            return true;
        }
        m_http3Server.setAutomaticPollingEnabled(false);
        m_http3Server.close();
        return listenHttp3(m_server.httpsAddress(), port, &m_lastHttp3Error);
    }

    SwMailService* ensureMailService_() {
        if (!m_mailService) {
            m_mailService = new SwMailService();
            syncMailTlsConfig_();
            m_mailService->setConfig(m_mailConfig);
            m_mailService->setDomainTlsConfig(m_domainTlsConfig);
            if (m_authService) {
                m_authService->setMailService(m_mailService);
            }
        }
        return m_mailService;
    }

    SwHttpAuthService* ensureAuthService_() {
        if (!m_authService) {
            m_authService = new SwHttpAuthService();
            m_authService->setConfig(m_authConfig);
            m_authService->setMailService(m_mailService);
        }
        return m_authService;
    }

    void syncMailTlsConfig_() {
        m_mailConfig.domain = swMailDetail::normalizeDomain(m_mailConfig.domain);
        if (m_mailConfig.mailHost.isEmpty() && !m_mailConfig.domain.isEmpty()) {
            m_mailConfig.mailHost = swMailDetail::defaultMailHost(m_mailConfig.domain);
        } else {
            m_mailConfig.mailHost = swMailDetail::normalizeMailHost(m_mailConfig.mailHost, m_mailConfig.domain);
        }

        m_domainTlsConfig.domain = swMailDetail::normalizeDomain(m_domainTlsConfig.domain);
        m_domainTlsConfig.mailHost = swMailDetail::normalizeMailHost(m_domainTlsConfig.mailHost, m_domainTlsConfig.domain);

        const SwString desiredMailHost = !m_mailConfig.mailHost.isEmpty() ? m_mailConfig.mailHost : m_domainTlsConfig.mailHost;
        if (desiredMailHost.isEmpty() || desiredMailHost == m_domainTlsConfig.domain) {
            return;
        }

        for (std::size_t i = 0; i < m_domainTlsConfig.subjectAlternativeNames.size(); ++i) {
            if (swMailDetail::normalizeDomain(m_domainTlsConfig.subjectAlternativeNames[i]) == desiredMailHost) {
                return;
            }
        }
        m_domainTlsConfig.subjectAlternativeNames.append(desiredMailHost);
    }

    void syncMailTlsCredentials_(const SwList<SwTlsCredentialEntry>& credentials) {
        if (!m_mailService) {
            return;
        }

        SwTlsCredentialEntry selected;
        bool hasSelected = false;
        for (std::size_t i = 0; i < credentials.size(); ++i) {
            if (!credentials[i].certPath.trimmed().isEmpty() && !credentials[i].keyPath.trimmed().isEmpty()) {
                selected = credentials[i];
                hasSelected = true;
                if (credentials[i].isDefault || credentials[i].host.trimmed().isEmpty()) {
                    break;
                }
            }
        }
        if (!hasSelected) {
            return;
        }

        SwString ignored;
        (void)m_mailService->reloadTlsCredentials(selected.certPath, selected.keyPath, &ignored);
    }

    static SwString normalizeRoutePattern_(const SwString& pattern) {
        SwString normalized = swHttpNormalizePath(pattern.trimmed());
        if (normalized.size() > 1 && normalized.endsWith("/")) {
            normalized.chop(1);
        }
        return normalized;
    }

    static SwString joinGroupPrefix_(const SwString& basePrefix, const SwString& nextPrefix) {
        SwString base = basePrefix.isEmpty() ? SwString("/") : normalizeRoutePattern_(basePrefix);
        SwString next = normalizeRoutePattern_(nextPrefix);

        if (base == "/") {
            return next;
        }
        if (next == "/") {
            return base;
        }
        return base + next;
    }

    SwString resolvePath_(const SwString& routePattern) const {
        const SwString pattern = normalizeRoutePattern_(routePattern);
        if (m_groupPrefix.isEmpty() || m_groupPrefix == "/") {
            return pattern;
        }
        if (pattern == "/") {
            return m_groupPrefix;
        }
        return m_groupPrefix + pattern;
    }

    static SwHttpRecoveryHandler defaultRecoveryHandler_() {
        return [](SwHttpContext& context, const SwString&) {
            if (context.handled()) {
                return;
            }
            context.text("Internal Server Error", 500);
            context.closeConnection(true);
        };
    }

    static void invokeRecovery_(const SwHttpRecoveryHandler& recoveryHandler,
                                SwHttpContext& context,
                                const SwString& error) {
        SwHttpRecoveryHandler effective = recoveryHandler ? recoveryHandler : defaultRecoveryHandler_();
        effective(context, error);
    }

    static void runPipeline_(const SwList<SwHttpMiddleware>& middlewares,
                             const SwHttpHandler& handler,
                             const SwHttpRecoveryHandler& recoveryHandler,
                             SwHttpContext& context) {
        std::function<void(std::size_t)> invoke;
        invoke = [&](std::size_t index) {
            try {
                if (index >= middlewares.size()) {
                    handler(context);
                    return;
                }

                const SwHttpMiddleware middleware = middlewares[index];
                if (!middleware) {
                    invoke(index + 1);
                    return;
                }

                bool nextCalled = false;
                SwHttpNext next = [&]() {
                    if (nextCalled) {
                        return;
                    }
                    nextCalled = true;
                    invoke(index + 1);
                };
                middleware(context, next);
            } catch (const std::exception& e) {
                invokeRecovery_(recoveryHandler, context, SwString(e.what()));
            } catch (...) {
                invokeRecovery_(recoveryHandler, context, "Unhandled non-std exception");
            }
        };

        invoke(0);
    }

    static void applyRouteTimeout_(const SwHttpRouteOptions& routeOptions,
                                   long long elapsedMs,
                                   SwHttpResponse& response,
                                   const SwHttpRequest& request) {
        if (routeOptions.softTimeoutMs <= 0 || elapsedMs <= routeOptions.softTimeoutMs) {
            return;
        }

        response.headers["x-route-timeout-ms"] = SwString::number(elapsedMs);
        if (routeOptions.timeoutOverridesResponse) {
            response = swHttpTextResponse(504, "Route timeout");
            response.headers["x-route-timeout-ms"] = SwString::number(elapsedMs);
            response.closeConnection = !request.keepAlive;
        }
    }

    static bool validateWebSocketUpgradeRequest_(const SwHttpRequest& request,
                                                 int& outStatus,
                                                 SwString& outMessage) {
        outStatus = 0;
        outMessage.clear();

        if (request.method.toUpper() != "GET") {
            outStatus = 405;
            outMessage = "Method Not Allowed";
            return false;
        }

        const SwString connection = request.headers.value("connection", SwString());
        const SwString upgrade = request.headers.value("upgrade", SwString()).trimmed();
        if (!swHttpHeaderContainsToken(connection, "upgrade") ||
            !upgrade.contains("websocket", Sw::CaseInsensitive)) {
            outStatus = 426;
            outMessage = "Upgrade Required";
            return false;
        }

        const SwString version = request.headers.value("sec-websocket-version", SwString()).trimmed();
        if (version != "13") {
            outStatus = 426;
            outMessage = "Unsupported WebSocket Version";
            return false;
        }

        const SwString clientKey = request.headers.value("sec-websocket-key", SwString()).trimmed();
        if (clientKey.isEmpty()) {
            outStatus = 400;
            outMessage = "Missing Sec-WebSocket-Key";
            return false;
        }

        return true;
    }

    static SwHttpResponse buildResponse_(const SwHttpRequest& request,
                                         const SwList<SwHttpMiddleware>& middlewares,
                                         const SwHttpHandler& handler,
                                         const SwHttpRouteOptions& routeOptions,
                                         const SwHttpRecoveryHandler& recoveryHandler,
                                         bool isNotFoundFallback) {
        SwHttpContext context(request);
        const auto startedAt = std::chrono::steady_clock::now();
        runPipeline_(middlewares, handler, recoveryHandler, context);
        const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - startedAt)
                                        .count();

        SwHttpResponse response = context.takeResponse();
        if (!context.handled()) {
            if (isNotFoundFallback) {
                response = swHttpTextResponse(404, "Not Found");
            } else {
                response = swHttpTextResponse(500, "Route handler produced no response");
            }
        }
        applyRouteTimeout_(routeOptions, elapsedMs, response, request);
        response.closeConnection = response.closeConnection || !request.keepAlive;
        return response;
    }
};
