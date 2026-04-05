#pragma once

/**
 * @file src/core/io/SwHttpServer.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpServer in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP server interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpServerMetrics and SwHttpServer.
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

#include "SwObject.h"
#include "SwAbstractSocket.h"
#include "SwPointer.h"
#include "SwSslServer.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwList.h"
#include "SwMutex.h"
#include "SwDebug.h"
#include "SwEventLoop.h"

#include "http/SwHttpTypes.h"
#include "http/SwHttpRouter.h"
#include "http/SwHttpSession.h"
#include "http/SwHttpStaticFileHandler.h"

#include <functional>
#include <chrono>

static constexpr const char* kSwLogCategory_SwHttpServer = "sw.core.io.swhttpserver";

#if defined(__has_include)
#if __has_include("src/core/runtime/SwThreadPool.h")
#include "src/core/runtime/SwThreadPool.h"
#define SW_HTTPSERVER_HAS_THREADPOOL 1
#elif __has_include("SwThreadPool.h")
#include "SwThreadPool.h"
#define SW_HTTPSERVER_HAS_THREADPOOL 1
#else
class SwThreadPool;
#define SW_HTTPSERVER_HAS_THREADPOOL 0
#endif
#else
#include "SwThreadPool.h"
#define SW_HTTPSERVER_HAS_THREADPOOL 1
#endif

struct SwHttpServerMetrics {
    long long totalRequests = 0;
    long long totalResponses = 0;
    long long inFlightRequests = 0;
    long long totalRequestBodyBytes = 0;
    long long totalResponseBodyBytes = 0;
    long long totalLatencyMs = 0;
    long long maxLatencyMs = 0;
    long long rejectedConnections = 0;
    long long rejectedInFlight = 0;
    long long rejectedThreadPoolSaturation = 0;
    SwMap<SwString, long long> statusCounters;
};

using SwHttpPreRouteHandler = std::function<bool(const SwHttpRequest&, SwHttpResponse&)>;
using SwHttpPreRouteAsyncResponder = std::function<void(bool, const SwHttpResponse&)>;
using SwHttpPreRouteAsyncHandler = std::function<void(const SwHttpRequest&, const SwHttpPreRouteAsyncResponder&)>;

class SwHttpServer : public SwObject {
    SW_OBJECT(SwHttpServer, SwObject)

public:
    enum class DispatchMode {
        Inline,
        ThreadPool
    };

    /**
     * @brief Constructs a `SwHttpServer` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwHttpServer(SwObject* parent = nullptr)
        : SwObject(parent) {
        connect(&m_tcpServer, &SwTcpServer::newConnection, this, &SwHttpServer::onNewTcpConnection_);
        connect(&m_sslServer, &SwTcpServer::newConnection, this, &SwHttpServer::onNewTcpConnection_);

        m_router.setNotFoundHandler([](const SwHttpRequest& request) {
            SwHttpResponse response = swHttpTextResponse(404, "Not Found");
            response.closeConnection = !request.keepAlive;
            return response;
        });
    }

    /**
     * @brief Destroys the `SwHttpServer` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwHttpServer() override {
        close();
        while (!m_staticHandlers.isEmpty()) {
            SwHttpStaticFileHandler* handler = m_staticHandlers.first();
            m_staticHandlers.removeFirst();
            delete handler;
        }
    }

    bool isHttpListening() const {
        return m_tcpServer.isListening();
    }

    bool isHttpsListening() const {
        return m_sslServer.isListening();
    }

    uint16_t httpPort() const {
        return m_tcpServer.localPort();
    }

    SwString httpAddress() const {
        return m_tcpServer.localAddress();
    }

    uint16_t httpsPort() const {
        return m_sslServer.localPort();
    }

    SwString httpsAddress() const {
        return m_sslServer.localAddress();
    }

    /**
     * @brief Starts listening for incoming traffic.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool listen(uint16_t port) {
        return listen(SwString(), port);
    }

    bool listen(const SwString& bindAddress, uint16_t port) {
        closeListeners_();
        return listenHttp(bindAddress, port);
    }

    /**
     * @brief Starts listening for incoming HTTPS traffic.
     * @param port Local port used by the operation (typically 443).
     * @param certPath Path to the PEM certificate file.
     * @param keyPath Path to the PEM private key file.
     * @return `true` on success; otherwise `false`.
     */
    bool listen(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        return listen(SwString(), port, certPath, keyPath);
    }

    bool listen(const SwString& bindAddress,
                uint16_t port,
                const SwString& certPath,
                const SwString& keyPath) {
        closeListeners_();
        return listenHttps(bindAddress, port, certPath, keyPath);
    }

    bool listenHttp(uint16_t port) {
        return listenHttp(SwString(), port);
    }

    bool listenHttp(const SwString& bindAddress, uint16_t port) {
        return m_tcpServer.listen(bindAddress, port);
    }

    bool listenHttps(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        return listenHttps(SwString(), port, certPath, keyPath);
    }

    bool listenHttps(const SwString& bindAddress, uint16_t port, const SwString& certPath, const SwString& keyPath) {
        if (!m_sslServer.setLocalCredentials(certPath, keyPath)) {
            return false;
        }
        return m_sslServer.listen(bindAddress, port);
    }

    bool listenHttps(uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        return listenHttps(SwString(), port, credentials);
    }

    bool listenHttps(const SwString& bindAddress, uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        if (!m_sslServer.setLocalCredentials(credentials)) {
            return false;
        }
        return m_sslServer.listen(bindAddress, port);
    }

    bool reloadHttpsCredentials(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        if (!m_sslServer.reloadLocalCredentials(certPath, keyPath)) {
            return false;
        }

        if (m_sslServer.isListening() && m_sslServer.localPort() == port) {
            return true;
        }
        return m_sslServer.listen(port);
    }

    bool reloadHttpsCredentials(uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        if (!m_sslServer.reloadLocalCredentials(credentials)) {
            return false;
        }

        if (m_sslServer.isListening() && m_sslServer.localPort() == port) {
            return true;
        }
        return m_sslServer.listen(port);
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() {
        closeListeners_();
        while (!m_sessions.isEmpty()) {
            SwHttpSession* session = m_sessions.first();
            m_sessions.removeFirst();
            if (session) {
                session->closeSession();
            }
        }
    }

    /**
     * @brief Stops accepting new work and waits for active operations to finish.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return `true` on success; otherwise `false`.
     *
     * @details The method stops accepting new work first, then waits until the active operations drain or the timeout expires.
     */
    bool closeGraceful(int timeoutMs = 5000) {
        closeListeners_();
        int waitedMs = 0;
        while (!m_sessions.isEmpty()) {
            if (timeoutMs >= 0 && waitedMs >= timeoutMs) {
                close();
                return false;
            }
            SwEventLoop::swsleep(10);
            waitedMs += 10;
        }
        return true;
    }

    /**
     * @brief Applies the HTTP limits used for subsequently created sessions.
     * @param limits Limit configuration to copy into the server.
     *
     * @details
     * The limits structure controls parser bounds, body sizes, connection caps, and other
     * safeguards enforced when new requests are accepted.
     */
    void setLimits(const SwHttpLimits& limits) {
        m_limits = limits;
    }

    /**
     * @brief Returns the HTTP limit configuration currently held by the server.
     * @return The limit set applied to new HTTP sessions.
     */
    const SwHttpLimits& limits() const {
        return m_limits;
    }

    /**
     * @brief Applies the timeout policy used for subsequently created sessions.
     * @param timeouts Timeout configuration to copy into the server.
     *
     * @details
     * These timeouts are forwarded to each session so idle, read, and write deadlines stay
     * consistent across all newly accepted connections.
     */
    void setTimeouts(const SwHttpTimeouts& timeouts) {
        m_timeouts = timeouts;
    }

    /**
     * @brief Returns the timeout policy currently held by the server.
     * @return The timeout set applied to new HTTP sessions.
     */
    const SwHttpTimeouts& timeouts() const {
        return m_timeouts;
    }

    /**
     * @brief Registers an HTTP route handler.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addRoute(const SwString& method, const SwString& pattern, const SwHttpRouteCallback& callback) {
        m_router.addRoute(method, pattern, callback);
    }

    /**
     * @brief Registers a named HTTP route handler.
     * @param routeName Stable route name used by the operation.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addNamedRoute(const SwString& routeName,
                       const SwString& method,
                       const SwString& pattern,
                       const SwHttpRouteCallback& callback) {
        m_router.addNamedRoute(routeName, method, pattern, callback);
    }

    /**
     * @brief Registers an asynchronous HTTP route handler.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addRouteAsync(const SwString& method,
                       const SwString& pattern,
                       const SwHttpRouteAsyncCallback& callback) {
        m_router.addRouteAsync(method, pattern, callback);
    }

    /**
     * @brief Registers a named asynchronous HTTP route handler.
     * @param routeName Stable route name used by the operation.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addNamedRouteAsync(const SwString& routeName,
                            const SwString& method,
                            const SwString& pattern,
                            const SwHttpRouteAsyncCallback& callback) {
        m_router.addNamedRouteAsync(routeName, method, pattern, callback);
    }

    /**
     * @brief Registers an HTTP route handler.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addRoute(const SwString& pattern, const SwHttpRouteCallback& callback) {
        m_router.addRoute("*", pattern, callback);
    }

    /**
     * @brief Sets the fallback handler used when no registered route matches a request.
     * @param callback Callback invoked to build the fallback response.
     */
    void setNotFoundHandler(const SwHttpRouteCallback& callback) {
        m_router.setNotFoundHandler(callback);
    }

    void addPreRouteHandler(const SwHttpPreRouteHandler& handler) {
        if (!handler) {
            return;
        }
        m_preRouteHandlers.append(handler);
    }

    void clearPreRouteHandlers() {
        m_preRouteHandlers.clear();
    }

    void addPreRouteHandlerAsync(const SwHttpPreRouteAsyncHandler& handler) {
        if (!handler) {
            return;
        }
        m_preRouteHandlersAsync.append(handler);
    }

    void clearPreRouteHandlersAsync() {
        m_preRouteHandlersAsync.clear();
    }

    /**
     * @brief Configures how the router handles paths that differ only by a trailing slash.
     * @param policy Trailing-slash policy forwarded to the router.
     */
    void setTrailingSlashPolicy(SwHttpRouter::TrailingSlashPolicy policy) {
        m_router.setTrailingSlashPolicy(policy);
    }

    /**
     * @brief Returns the trailing-slash policy currently used by the router.
     * @return The active trailing-slash handling policy.
     */
    SwHttpRouter::TrailingSlashPolicy trailingSlashPolicy() const {
        return m_router.trailingSlashPolicy();
    }

    /**
     * @brief Returns whether a named route is currently registered.
     * @param routeName Stable route name to look up.
     * @return `true` when the router contains the requested route name; otherwise `false`.
     */
    bool hasRouteName(const SwString& routeName) const {
        return m_router.hasRouteName(routeName);
    }

    /**
     * @brief Returns the normalized pattern registered for a named route.
     * @param routeName Stable route name to resolve.
     * @return The pattern associated with the route, or an empty string when the name is unknown.
     */
    SwString routePattern(const SwString& routeName) const {
        return m_router.routePattern(routeName);
    }

    /**
     * @brief Builds a URL from a named route and parameter values.
     * @param routeName Stable route name to resolve.
     * @param pathParams Path parameters used to expand the route template.
     * @param outUrl Output string that receives the generated URL.
     * @param queryParams Query parameters appended after path generation.
     * @return `true` on success; otherwise `false`.
     *
     * @details
     * Use this helper when link generation needs to stay aligned with the patterns already
     * registered in the router instead of rebuilding URLs manually.
     */
    bool buildUrl(const SwString& routeName,
                  const SwMap<SwString, SwString>& pathParams,
                  SwString& outUrl,
                  const SwMap<SwString, SwString>& queryParams = SwMap<SwString, SwString>()) const {
        return m_router.buildUrl(routeName, pathParams, outUrl, queryParams);
    }

    /**
     * @brief Mounts a static-file handler under a URL prefix.
     * @param prefix URL prefix exposed by the handler.
     * @param rootDir Root directory from which files are served.
     * @param options Static-file options controlling resolution and response behavior.
     *
     * @details
     * The server automatically registers `GET` and `HEAD` routes that delegate matching requests to
     * a dedicated `SwHttpStaticFileHandler`.
     */
    void mountStatic(const SwString& prefix, const SwString& rootDir, const SwHttpStaticOptions& options = SwHttpStaticOptions()) {
        SwHttpStaticFileHandler* handler = new SwHttpStaticFileHandler(prefix, rootDir, options);
        m_staticHandlers.append(handler);

        SwString routePattern = swHttpNormalizePath(prefix);
        if (routePattern == "/") {
            routePattern = "/*";
        } else {
            routePattern += "/*";
        }

        m_router.addRoute("GET", routePattern, [handler](const SwHttpRequest& request) {
            SwHttpResponse response;
            if (!handler->tryHandle(request, response)) {
                response = swHttpTextResponse(404, "Not Found");
                response.closeConnection = !request.keepAlive;
            }
            return response;
        });

        m_router.addRoute("HEAD", routePattern, [handler](const SwHttpRequest& request) {
            SwHttpResponse response;
            if (!handler->tryHandle(request, response)) {
                response = swHttpTextResponse(404, "Not Found");
                response.closeConnection = !request.keepAlive;
                response.headOnly = true;
            }
            return response;
        });
    }

    /**
     * @brief Sets the external thread pool used for offloaded request dispatch.
     * @param threadPool Thread-pool instance used when dispatch mode is `ThreadPool`.
     */
    void setThreadPool(SwThreadPool* threadPool) {
        m_threadPool = threadPool;
    }

    /**
     * @brief Returns the thread pool currently attached to the server.
     * @return The configured thread pool, or `nullptr` when inline dispatch is used.
     */
    SwThreadPool* threadPool() const {
        return m_threadPool;
    }

    /**
     * @brief Selects how requests are dispatched after parsing.
     * @param mode Dispatch strategy used for route execution.
     */
    void setDispatchMode(DispatchMode mode) {
        m_dispatchMode = mode;
    }

    /**
     * @brief Returns the current request-dispatch strategy.
     * @return The dispatch mode used by the server.
     */
    DispatchMode dispatchMode() const {
        return m_dispatchMode;
    }

    /**
     * @brief Returns a snapshot of the accumulated server metrics.
     * @return A copy of the current counters and latency aggregates.
     */
    SwHttpServerMetrics metricsSnapshot() const {
        SwMutexLocker locker(&m_metricsMutex);
        return m_metrics;
    }

    /**
     * @brief Clears the accumulated metrics while preserving the in-flight request count.
     */
    void resetMetrics() {
        SwMutexLocker locker(&m_metricsMutex);
        const long long currentInFlight = m_metrics.inFlightRequests;
        m_metrics = SwHttpServerMetrics();
        m_metrics.inFlightRequests = currentInFlight;
        m_threadPoolQueuedDispatches = 0;
    }

private slots:
    /**
     * @brief Accepts pending TCP connections and creates HTTP sessions for them.
     *
     * @details
     * The method enforces the current connection limit, instantiates `SwHttpSession`, wires the
     * request completion callbacks, and removes finished sessions from the live session list.
     */
    void onNewTcpConnection_() {
        while (SwAbstractSocket* socket = m_tcpServer.nextPendingConnection()) {
            attachAcceptedSocket_(socket, false, m_tcpServer.localPort());
        }
        while (SwAbstractSocket* socket = m_sslServer.nextPendingConnection()) {
            attachAcceptedSocket_(socket, true, m_sslServer.localPort());
        }
    }

private:
    void closeListeners_() {
        m_tcpServer.close();
        m_sslServer.close();
    }

    void attachAcceptedSocket_(SwAbstractSocket* socket, bool isTls, uint16_t localPort) {
        if (!socket) {
            return;
        }
        if (m_limits.maxConnections > 0 && m_sessions.size() >= m_limits.maxConnections) {
            {
                SwMutexLocker locker(&m_metricsMutex);
                ++m_metrics.rejectedConnections;
            }
            socket->close();
            socket->deleteLater();
            return;
        }

        SwHttpSession* session = new SwHttpSession(socket, &m_router, m_limits, m_timeouts, isTls, localPort, this);
        SwPointer<SwHttpSession> sessionGuard(session);
        session->setRequestHandler([this, sessionGuard](const SwHttpRequest& request,
                                                        const SwHttpSession::SwHttpResponseCallback& complete) {
            SwHttpSession::SwHttpResponseCallback guardedComplete =
                [sessionGuard, complete](const SwHttpResponse& response) {
                    if (!sessionGuard) {
                        return;
                    }
                    if (complete) {
                        complete(response);
                    }
                };
            dispatchRequest_(request, guardedComplete);
        });
        m_sessions.append(session);
        session->setFinishedCallback([this](SwHttpSession* doneSession) {
            m_sessions.removeOne(doneSession);
            if (doneSession) {
                doneSession->deleteLater();
            }
        });
        session->startBufferedReadProcessing();
    }

    bool tryPreRoute_(const SwHttpRequest& request, SwHttpResponse& response) const {
        for (size_t i = 0; i < m_preRouteHandlers.size(); ++i) {
            const SwHttpPreRouteHandler& handler = m_preRouteHandlers[i];
            if (!handler) {
                continue;
            }
            if (handler(request, response)) {
                return true;
            }
        }
        return false;
    }

    SwHttpResponse routeRequestInline_(const SwHttpRequest& request) {
        SwHttpResponse response;
        bool handled = tryPreRoute_(request, response);
        if (!handled) {
            handled = m_router.route(request, response);
        }
        if (!handled) {
            response = swHttpTextResponse(404, "Not Found");
            response.closeConnection = !request.keepAlive;
        }
        return response;
    }

    void routeRequestAsync_(const SwHttpRequest& request,
                            const SwHttpSession::SwHttpResponseCallback& complete) {
        SwHttpResponse preRouteResponse;
        if (tryPreRoute_(request, preRouteResponse)) {
            if (complete) {
                complete(preRouteResponse);
            }
            return;
        }
        tryPreRouteAsync_(request, 0, [this, request, complete](bool handled, const SwHttpResponse& preRouteAsyncResponse) {
            if (handled) {
                if (complete) {
                    complete(preRouteAsyncResponse);
                }
                return;
            }

            const bool routed = m_router.routeAsync(request, [complete](const SwHttpResponse& response) {
                if (complete) {
                    complete(response);
                }
            });
            if (routed) {
                return;
            }

            SwHttpResponse response = swHttpTextResponse(404, "Not Found");
            response.closeConnection = !request.keepAlive;
            if (complete) {
                complete(response);
            }
        });
    }

    void completeDispatch_(const SwHttpSession::SwHttpResponseCallback& complete,
                           const SwHttpResponse& response,
                           const std::chrono::steady_clock::time_point& startAt) {
        releaseInFlight_();
        recordResponseMetrics_(response, elapsedMs_(startAt));
        if (complete) {
            complete(response);
        }
    }

    void dispatchRequest_(const SwHttpRequest& request,
                          const SwHttpSession::SwHttpResponseCallback& complete) {
        const auto startAt = std::chrono::steady_clock::now();
        if (!tryAcquireInFlight_(request)) {
            SwHttpResponse response = swHttpTextResponse(503, "Server busy");
            response.closeConnection = !request.keepAlive;
            recordResponseMetrics_(response, elapsedMs_(startAt));
            if (complete) {
                complete(response);
            }
            return;
        }

#if SW_HTTPSERVER_HAS_THREADPOOL
        if (m_dispatchMode == DispatchMode::ThreadPool &&
            m_threadPool &&
            !m_router.willRouteAsync(request) &&
            m_preRouteHandlersAsync.isEmpty()) {
            if (!tryReserveThreadPoolDispatch_()) {
                releaseInFlight_();
                SwHttpResponse response = swHttpTextResponse(503, "ThreadPool saturated");
                response.closeConnection = !request.keepAlive;
                recordResponseMetrics_(response, elapsedMs_(startAt));
                if (complete) {
                    complete(response);
                }
                return;
            }

            ThreadHandle* affinity = threadHandle();
            bool rejectedByBackpressure = false;
            bool started = m_threadPool->tryStartQueued([this, request, complete, startAt, affinity]() {
                SwHttpResponse computed = routeRequestInline_(request);

                releaseThreadPoolDispatch_();
                auto completeOnAffinity = [this, complete, computed, startAt]() {
                    completeDispatch_(complete, computed, startAt);
                };
                if (affinity && ThreadHandle::currentThread() != affinity) {
                    affinity->postTaskOnLane(completeOnAffinity, SwFiberLane::Control);
                } else {
                    completeOnAffinity();
                }
            }, 0, &rejectedByBackpressure);

            if (started) {
                return;
            }

            releaseThreadPoolDispatch_();
            SwHttpResponse saturated = swHttpTextResponse(
                503,
                rejectedByBackpressure ? SwString("ThreadPool saturated")
                                       : SwString("ThreadPool unavailable"));
            saturated.closeConnection = !request.keepAlive;
            completeDispatch_(complete, saturated, startAt);
            return;
        }
#endif

        routeRequestAsync_(request, [this, complete, startAt](const SwHttpResponse& response) {
            completeDispatch_(complete, response, startAt);
        });
    }

    bool tryAcquireInFlight_(const SwHttpRequest& request) {
        SwMutexLocker locker(&m_metricsMutex);
        if (m_limits.maxInFlightRequests > 0 &&
            static_cast<size_t>(m_metrics.inFlightRequests) >= m_limits.maxInFlightRequests) {
            ++m_metrics.rejectedInFlight;
            return false;
        }
        ++m_metrics.inFlightRequests;
        ++m_metrics.totalRequests;
        m_metrics.totalRequestBodyBytes += static_cast<long long>(request.body.size());
        return true;
    }

    void releaseInFlight_() {
        SwMutexLocker locker(&m_metricsMutex);
        if (m_metrics.inFlightRequests > 0) {
            --m_metrics.inFlightRequests;
        }
    }

    bool tryReserveThreadPoolDispatch_() {
        SwMutexLocker locker(&m_metricsMutex);
        if (m_limits.maxThreadPoolQueuedDispatches > 0 &&
            static_cast<size_t>(m_threadPoolQueuedDispatches) >= m_limits.maxThreadPoolQueuedDispatches) {
            ++m_metrics.rejectedThreadPoolSaturation;
            return false;
        }
        ++m_threadPoolQueuedDispatches;
        return true;
    }

    void releaseThreadPoolDispatch_() {
        SwMutexLocker locker(&m_metricsMutex);
        if (m_threadPoolQueuedDispatches > 0) {
            --m_threadPoolQueuedDispatches;
        }
    }

    static long long elapsedMs_(const std::chrono::steady_clock::time_point& startAt) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - startAt)
            .count();
    }

    static size_t responseBodySize_(const SwHttpResponse& response) {
        if (response.hasFile) {
            return response.fileLength;
        }
        if (response.useChunkedTransfer) {
            size_t total = 0;
            for (size_t i = 0; i < response.chunkedParts.size(); ++i) {
                total += response.chunkedParts[i].size();
            }
            return total;
        }
        return response.body.size();
    }

    void recordResponseMetrics_(const SwHttpResponse& response, long long latencyMs) {
        SwMutexLocker locker(&m_metricsMutex);
        ++m_metrics.totalResponses;
        m_metrics.totalLatencyMs += latencyMs;
        if (latencyMs > m_metrics.maxLatencyMs) {
            m_metrics.maxLatencyMs = latencyMs;
        }
        m_metrics.totalResponseBodyBytes += static_cast<long long>(responseBodySize_(response));
        const SwString key = SwString::number(response.status);
        const long long prev = m_metrics.statusCounters.value(key, 0LL);
        m_metrics.statusCounters[key] = prev + 1;
    }

    SwTcpServer m_tcpServer;
    SwSslServer m_sslServer;
    SwHttpRouter m_router;
    SwList<SwHttpSession*> m_sessions;
    SwList<SwHttpPreRouteHandler> m_preRouteHandlers;
    SwList<SwHttpPreRouteAsyncHandler> m_preRouteHandlersAsync;
    SwList<SwHttpStaticFileHandler*> m_staticHandlers;
    SwHttpLimits m_limits;
    SwHttpTimeouts m_timeouts;
    SwThreadPool* m_threadPool = nullptr;
    DispatchMode m_dispatchMode = DispatchMode::Inline;
    mutable SwMutex m_metricsMutex;
    SwHttpServerMetrics m_metrics;
    long long m_threadPoolQueuedDispatches = 0;

    void tryPreRouteAsync_(const SwHttpRequest& request,
                           std::size_t index,
                           const std::function<void(bool, const SwHttpResponse&)>& complete) const {
        if (index >= m_preRouteHandlersAsync.size()) {
            if (complete) {
                complete(false, SwHttpResponse());
            }
            return;
        }

        const SwHttpPreRouteAsyncHandler& handler = m_preRouteHandlersAsync[index];
        if (!handler) {
            tryPreRouteAsync_(request, index + 1, complete);
            return;
        }

        handler(request, [this, request, index, complete](bool handled, const SwHttpResponse& response) {
            if (handled) {
                if (complete) {
                    complete(true, response);
                }
                return;
            }
            tryPreRouteAsync_(request, index + 1, complete);
        });
    }
};

#undef SW_HTTPSERVER_HAS_THREADPOOL
