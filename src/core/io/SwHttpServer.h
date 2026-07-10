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
#include "SwTimer.h"

#include "http/SwHttpTypes.h"
#include "http/SwHttpRouter.h"
#include "http/SwHttpSession.h"
#include "http/SwHttpStaticFileHandler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

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

private:
    struct LifetimeState_ {
#if defined(_WIN32)
        LifetimeState_() {
            InitializeCriticalSection(&mutex);
            InitializeConditionVariable(&drained);
        }
        ~LifetimeState_() { DeleteCriticalSection(&mutex); }
        void lock() { EnterCriticalSection(&mutex); }
        void unlock() { LeaveCriticalSection(&mutex); }
        void notifyDrained_() { WakeAllConditionVariable(&drained); }
        void stopAndDrain_() {
            lock();
            stopping = true;
            while (activeUsers != 0) {
                SleepConditionVariableCS(&drained, &mutex, INFINITE);
            }
            owner = nullptr;
            unlock();
        }
        CRITICAL_SECTION mutex;
        CONDITION_VARIABLE drained;
#else
        void lock() { mutex.lock(); }
        void unlock() { mutex.unlock(); }
        void notifyDrained_() { drained.notify_all(); }
        void stopAndDrain_() {
            std::unique_lock<std::mutex> lock(mutex);
            stopping = true;
            drained.wait(lock, [this]() { return activeUsers == 0; });
            owner = nullptr;
        }
        std::mutex mutex;
        std::condition_variable drained;
#endif
        SwHttpServer* owner = nullptr;
        std::size_t activeUsers = 0;
        bool stopping = false;
    };

    class LifetimeAccess_ {
    public:
        explicit LifetimeAccess_(const std::shared_ptr<LifetimeState_>& state)
            : state_(state) {
            if (!state_) {
                return;
            }
            std::lock_guard<LifetimeState_> lock(*state_);
            if (state_->stopping || !state_->owner) {
                return;
            }
            owner_ = state_->owner;
            ++state_->activeUsers;
        }

        ~LifetimeAccess_() {
            if (!owner_ || !state_) {
                return;
            }
            std::lock_guard<LifetimeState_> lock(*state_);
            if (state_->activeUsers > 0) {
                --state_->activeUsers;
            }
            if (state_->stopping && state_->activeUsers == 0) {
                state_->notifyDrained_();
            }
        }

        SwHttpServer* get() const { return owner_; }
        explicit operator bool() const { return owner_ != nullptr; }

        LifetimeAccess_(const LifetimeAccess_&) = delete;
        LifetimeAccess_& operator=(const LifetimeAccess_&) = delete;

    private:
        std::shared_ptr<LifetimeState_> state_;
        SwHttpServer* owner_ = nullptr;
    };

    struct DispatchGate_ {
        std::atomic<bool> completed{false};
        SwPointer<SwTimer> timeout;
        std::function<void(SwHttpResponse)> finishCallback;

        void finish(SwHttpResponse response) {
            if (completed.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            if (timeout) {
                timeout->stop();
                timeout->deleteLater();
                timeout = nullptr;
            }
            std::function<void(SwHttpResponse)> callback = std::move(finishCallback);
            if (callback) {
                callback(std::move(response));
            }
        }
    };

public:
    using SwHttpResponseCallback = SwHttpSession::SwHttpResponseCallback;

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
        : SwObject(parent),
          m_lifetime(std::make_shared<LifetimeState_>()) {
        m_lifetime->owner = this;
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
        shutdownLifetime_();
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
        resumeDispatching_();
        return m_tcpServer.listen(bindAddress, port);
    }

    bool listenHttps(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        return listenHttps(SwString(), port, certPath, keyPath);
    }

    bool listenHttps(const SwString& bindAddress, uint16_t port, const SwString& certPath, const SwString& keyPath) {
        if (!m_sslServer.setLocalCredentials(certPath, keyPath)) {
            return false;
        }
        resumeDispatching_();
        return m_sslServer.listen(bindAddress, port);
    }

    bool listenHttps(uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        return listenHttps(SwString(), port, credentials);
    }

    bool listenHttps(const SwString& bindAddress, uint16_t port, const SwList<SwTlsCredentialEntry>& credentials) {
        if (!m_sslServer.setLocalCredentials(credentials)) {
            return false;
        }
        resumeDispatching_();
        return m_sslServer.listen(bindAddress, port);
    }

    // Listener-scoped shutdown is used by SwHttpApp when one half of a
    // multi-transport origin fails to bind. It intentionally preserves the
    // other listener and already-dispatched sessions.
    void closeHttpListener() {
        m_tcpServer.close();
    }

    void closeHttpsListener() {
        m_sslServer.close();
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
        while (true) {
            SwHttpSession* session = nullptr;
            {
                SwMutexLocker locker(&m_sessionsMutex);
                if (m_sessions.isEmpty()) {
                    break;
                }
                session = m_sessions.first();
                m_sessions.removeAt(0);
            }
            if (session) {
                session->closeSession();
            }
        }
        cancelDispatches_();
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
        if (isDrained_()) {
            return true;
        }

        SwEventLoop loop;
        SwTimer timeout;
        bool timedOut = false;
        connect(this, &SwHttpServer::sessionsDrained, &loop, [this, &loop]() {
            if (isDrained_()) {
                loop.quit();
            }
        });
        if (timeoutMs >= 0) {
            timeout.setSingleShot(true);
            connect(&timeout, &SwTimer::timeout, &loop, [&loop, &timedOut]() {
                timedOut = true;
                loop.quit();
            });
            timeout.start(timeoutMs);
        }
        loop.exec();

        if (timedOut && !isDrained_()) {
            close();
            return false;
        }
        return isDrained_();
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

    // Transport adapters use this shared reservation gate so
    // maxPendingRequestBytesGlobal remains global across TCP/TLS and QUIC.
    bool tryReservePendingRequestBytes(std::size_t bytes) {
        return reservePendingRequestBytes_(bytes);
    }

    void releasePendingRequestBytes(std::size_t bytes) {
        releasePendingRequestBytes_(bytes);
    }

    std::size_t pendingRequestBytes() const {
        SwMutexLocker locker(&m_metricsMutex);
        return m_pendingRequestBytesGlobal;
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
     * @brief Returns the transport-neutral router used by this server.
     *
     * The same router can be attached to another HTTP transport (notably
     * SwQuicHttp3Server), allowing one route table to serve HTTP/1.x and HTTP/3.
     */
    SwHttpRouter& router() {
        return m_router;
    }

    const SwHttpRouter& router() const {
        return m_router;
    }

    /**
     * @brief Dispatches a request from a non-TCP transport on the current thread.
     *
     * This entry point shares synchronous pre-route handlers, route filters,
     * in-flight limits and metrics with native HTTP sessions. Transports that
     * can suspend a response must use dispatchRequest() so asynchronous
     * authentication and route callbacks cannot be bypassed.
     */
    SwHttpResponse dispatchRequestInline(const SwHttpRequest& request) {
        const auto startAt = std::chrono::steady_clock::now();
        if (!tryAcquireInFlight_(request)) {
            SwHttpResponse response = swHttpTextResponse(503, "Server busy");
            response.closeConnection = !request.keepAlive;
            filterResponseSafely_(request, response);
            recordResponseMetrics_(response, elapsedMs_(startAt));
            return response;
        }

        bool stopping = false;
        {
            SwMutexLocker locker(&m_dispatchMutex);
            stopping = m_dispatchStopping;
        }
        SwHttpResponse response;
        if (stopping) {
            response = swHttpTextResponse(503, "Server shutting down");
            response.closeConnection = true;
            filterResponseSafely_(request, response);
        } else if (!m_preRouteHandlersAsync.isEmpty()) {
            // Never bypass an asynchronous authentication/authorization gate
            // merely because a transport selected the inline dispatcher.
            response = swHttpTextResponse(503, "Asynchronous request pipeline required");
            response.closeConnection = !request.keepAlive;
            filterResponseSafely_(request, response);
        } else {
            response = routeRequestInlineSafely_(request);
        }
        releaseInFlight_();
        recordResponseMetrics_(response, elapsedMs_(startAt));
        notifyDrainState_();
        return response;
    }

    /**
     * @brief Dispatches a request from another transport through the complete
     *        synchronous/asynchronous HTTP application pipeline.
     *
     * Unlike dispatchRequestInline(), this entry point preserves asynchronous
     * pre-route handlers and asynchronous route callbacks. The completion may
     * therefore run after this method returns.
     */
    void dispatchRequest(const SwHttpRequest& request,
                         const SwHttpResponseCallback& complete) {
        dispatchRequest_(request, complete);
    }

    /**
     * @brief Permanently rejects new dispatches and drains callback access.
     *
     * This is an irreversible destruction barrier for owners whose response
     * filters or routes capture owner state. Normal close()/listen() cycles do
     * not use it; aggregate owners call it before destroying captured members.
     */
    void shutdownDispatching() {
        close();
        shutdownLifetime_();
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
    }

signals:
    DECLARE_SIGNAL_VOID(sessionsDrained)

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
    bool runOnAffinityReliable_(std::function<void()> task) {
        if (!task) {
            return false;
        }
        ThreadHandle* affinity = threadHandle();
        if (!affinity || ThreadHandle::currentThread() == affinity) {
            task();
            return true;
        }
        return affinity->postTaskOnLaneReliable(std::move(task), SwFiberLane::Control);
    }

    bool isDrained_() const {
        {
            SwMutexLocker locker(&m_sessionsMutex);
            if (!m_sessions.isEmpty()) {
                return false;
            }
        }
        SwMutexLocker locker(&m_dispatchMutex);
        return m_dispatchGates.isEmpty();
    }

    void notifyDrainState_() {
        ThreadHandle* affinity = threadHandle();
        if (affinity && ThreadHandle::currentThread() != affinity) {
            std::shared_ptr<LifetimeState_> lifetime = m_lifetime;
            (void)affinity->postTaskOnLaneReliable([lifetime]() {
                LifetimeAccess_ access(lifetime);
                if (access) {
                    access.get()->notifyDrainState_();
                }
            }, SwFiberLane::Control);
            return;
        }
        if (isDrained_()) {
            emit sessionsDrained();
        }
    }

    void shutdownLifetime_() {
        if (!m_lifetime) {
            return;
        }
        m_lifetime->stopAndDrain_();
    }

    void cancelDispatches_() {
        SwList<std::shared_ptr<DispatchGate_>> gates;
        {
            SwMutexLocker locker(&m_dispatchMutex);
            m_dispatchStopping = true;
            for (auto it = m_dispatchGates.begin(); it != m_dispatchGates.end(); ++it) {
                if (it.value()) {
                    gates.append(it.value());
                }
            }
            m_dispatchGates.clear();
        }
        for (std::size_t i = 0; i < gates.size(); ++i) {
            SwHttpResponse cancelled = swHttpTextResponse(503, "Server shutting down");
            cancelled.closeConnection = true;
            gates[i]->finish(std::move(cancelled));
        }
    }

    void resumeDispatching_() {
        SwMutexLocker locker(&m_dispatchMutex);
        m_dispatchStopping = false;
    }

    void closeListeners_() {
        m_tcpServer.close();
        m_sslServer.close();
    }

    void attachAcceptedSocket_(SwAbstractSocket* socket, bool isTls, uint16_t localPort) {
        if (!socket) {
            return;
        }
        bool connectionLimitReached = false;
        {
            SwMutexLocker locker(&m_sessionsMutex);
            connectionLimitReached = m_limits.maxConnections > 0 &&
                                     m_sessions.size() >= m_limits.maxConnections;
        }
        if (connectionLimitReached) {
            {
                SwMutexLocker locker(&m_metricsMutex);
                ++m_metrics.rejectedConnections;
            }
            if (SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(socket)) {
                tcp->abort();
            } else {
                socket->close();
            }
            socket->deleteLater();
            return;
        }

        SwHttpSession* session = new SwHttpSession(socket, &m_router, m_limits, m_timeouts, isTls, localPort, this);
        SwPointer<SwHttpSession> sessionGuard(session);
        std::shared_ptr<LifetimeState_> lifetime = m_lifetime;
        session->setRequestHandler([lifetime, sessionGuard](const SwHttpRequest& request,
                                                            const SwHttpSession::SwHttpResponseCallback& complete) {
            LifetimeAccess_ access(lifetime);
            if (!access) {
                return;
            }
            SwHttpSession::SwHttpResponseCallback guardedComplete =
                [sessionGuard, complete](SwHttpResponse response) mutable {
                    if (!sessionGuard) {
                        return;
                    }
                    if (complete) {
                        complete(std::move(response));
                    }
                };
            access.get()->dispatchRequest_(request, guardedComplete);
        });
        session->setPendingBytesBudgetCallbacks(
            [lifetime](std::size_t bytes) {
                LifetimeAccess_ access(lifetime);
                return access && access.get()->reservePendingRequestBytes_(bytes);
            },
            [lifetime](std::size_t bytes) {
                LifetimeAccess_ access(lifetime);
                if (access) {
                    access.get()->releasePendingRequestBytes_(bytes);
                }
            });
        {
            SwMutexLocker locker(&m_sessionsMutex);
            m_sessions.append(session);
        }
        session->setFinishedCallback([lifetime](SwHttpSession* doneSession) {
            LifetimeAccess_ access(lifetime);
            if (access) {
                {
                    SwMutexLocker locker(&access.get()->m_sessionsMutex);
                    access.get()->m_sessions.removeOne(doneSession);
                }
                access.get()->notifyDrainState_();
            }
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

    static SwHttpResponse internalErrorResponse_(const SwHttpRequest& request) {
        SwHttpResponse response = swHttpTextResponse(500, "Internal Server Error");
        response.closeConnection = !request.keepAlive;
        return response;
    }

    void filterResponseSafely_(const SwHttpRequest& request,
                               SwHttpResponse& response) const {
        try {
            m_router.filterResponse(request, response);
        } catch (...) {
            response = internalErrorResponse_(request);
            response.closeConnection = true;
        }
    }

    SwHttpResponse routeRequestInlineSafely_(const SwHttpRequest& request,
                                             bool applyResponseFilters = true) {
        try {
            return routeRequestInline_(request, applyResponseFilters);
        } catch (...) {
            // A throwing route/filter must not leak in-flight accounting or
            // escape an event-loop / thread-pool dispatch boundary.
            SwHttpResponse response = internalErrorResponse_(request);
            response.closeConnection = true;
            return response;
        }
    }

    SwHttpResponse routeRequestInline_(const SwHttpRequest& request,
                                       bool applyResponseFilters = true) {
        SwHttpResponse response;
        bool handled = tryPreRoute_(request, response);
        if (handled && applyResponseFilters) {
            m_router.filterResponse(request, response);
        }
        if (!handled) {
            handled = m_router.route(request, response, applyResponseFilters);
        }
        if (!handled) {
            response = swHttpTextResponse(404, "Not Found");
            response.closeConnection = !request.keepAlive;
            if (applyResponseFilters) {
                m_router.filterResponse(request, response);
            }
        }
        return response;
    }

    void routeRequestAsync_(const std::shared_ptr<const SwHttpRequest>& request,
                            const SwHttpSession::SwHttpResponseCallback& complete) {
        if (!request) {
            return;
        }
        SwHttpResponse preRouteResponse;
        try {
            if (tryPreRoute_(*request, preRouteResponse)) {
                if (complete) {
                    complete(std::move(preRouteResponse));
                }
                return;
            }
        } catch (...) {
            if (complete) {
                complete(internalErrorResponse_(*request));
            }
            return;
        }
        std::shared_ptr<LifetimeState_> lifetime = m_lifetime;
        try {
            tryPreRouteAsync_(request, 0,
                [lifetime, request, complete](bool handled,
                                              const SwHttpResponse& preRouteAsyncResponse) {
                    if (handled) {
                        if (complete) {
                            complete(preRouteAsyncResponse);
                        }
                        return;
                    }

                    LifetimeAccess_ access(lifetime);
                    if (!access) {
                        return;
                    }
                    try {
                        const bool routed = access.get()->m_router.routeAsync(
                            *request,
                            [request, complete](const SwHttpResponse& response) {
                                if (complete) {
                                    complete(response);
                                }
                            },
                            false);
                        if (routed) {
                            return;
                        }

                        SwHttpResponse response = swHttpTextResponse(404, "Not Found");
                        response.closeConnection = !request->keepAlive;
                        if (complete) {
                            complete(std::move(response));
                        }
                    } catch (...) {
                        if (complete) {
                            complete(internalErrorResponse_(*request));
                        }
                    }
                });
        } catch (...) {
            if (complete) {
                complete(internalErrorResponse_(*request));
            }
        }
    }

    static void finishAsyncResponse_(
        const std::shared_ptr<LifetimeState_>& lifetime,
        const std::shared_ptr<const SwHttpRequest>& request,
        const std::shared_ptr<DispatchGate_>& gate,
        SwHttpResponse response) {
        if (!request || !gate || gate->completed.load(std::memory_order_acquire)) {
            return;
        }

        std::shared_ptr<SwHttpResponse> responseState;
        try {
            responseState = std::make_shared<SwHttpResponse>(std::move(response));
        } catch (...) {
            gate->finish(internalErrorResponse_(*request));
            return;
        }

        bool scheduled = false;
        {
            LifetimeAccess_ access(lifetime);
            if (!access) {
                return;
            }
            try {
                scheduled = access.get()->runOnAffinityReliable_(
                    [lifetime, request, gate, responseState]() {
                        if (gate->completed.load(std::memory_order_acquire)) {
                            return;
                        }
                        LifetimeAccess_ callbackAccess(lifetime);
                        if (!callbackAccess) {
                            return;
                        }
                        callbackAccess.get()->filterResponseSafely_(
                            *request, *responseState);
                        gate->finish(std::move(*responseState));
                    });
            } catch (...) {
                scheduled = false;
            }
        }

        if (!scheduled && !gate->completed.load(std::memory_order_acquire)) {
            SwHttpResponse unavailable = swHttpTextResponse(
                503, "Response finalization unavailable");
            unavailable.closeConnection = true;
            gate->finish(std::move(unavailable));
        }
    }

    void dispatchRequest_(const SwHttpRequest& request,
                          const SwHttpSession::SwHttpResponseCallback& complete) {
        const auto startAt = std::chrono::steady_clock::now();
        if (!tryAcquireInFlight_(request)) {
            SwHttpResponse response = swHttpTextResponse(503, "Server busy");
            response.closeConnection = !request.keepAlive;
            filterResponseSafely_(request, response);
            recordResponseMetrics_(response, elapsedMs_(startAt));
            if (complete) {
                complete(std::move(response));
            }
            return;
        }
        bool dispatchStopping = false;
        {
            SwMutexLocker locker(&m_dispatchMutex);
            dispatchStopping = m_dispatchStopping;
        }
        if (dispatchStopping) {
            releaseInFlight_();
            SwHttpResponse response = swHttpTextResponse(503, "Server shutting down");
            response.closeConnection = true;
            filterResponseSafely_(request, response);
            recordResponseMetrics_(response, elapsedMs_(startAt));
            if (complete) {
                complete(std::move(response));
            }
            return;
        }

        // The common inline route is fully synchronous: avoid a request copy, shared gate,
        // timer allocation and dispatch-map mutex on every keep-alive request.
        if (m_dispatchMode == DispatchMode::Inline &&
            m_preRouteHandlersAsync.isEmpty() &&
            !m_router.willRouteAsync(request)) {
            SwHttpResponse response = routeRequestInlineSafely_(request);
            releaseInFlight_();
            recordResponseMetrics_(response, elapsedMs_(startAt));
            SwPointer<SwHttpServer> self(this);
            if (complete) {
                complete(std::move(response));
            }
            if (self) {
                self->notifyDrainState_();
            }
            return;
        }

        // Async dispatch owns a deep request snapshot. Charge that second body
        // allocation independently so repeated HTTP/3 disconnects cannot drop
        // the transport reservation while a slow application still retains
        // the copied request.
        const std::size_t dispatchRequestBytes = retainedRequestPayloadBytes_(request);
        if (dispatchRequestBytes > 0 &&
            !reservePendingRequestBytes_(dispatchRequestBytes)) {
            releaseInFlight_();
            SwHttpResponse response = swHttpTextResponse(503, "Request dispatch memory limit");
            response.closeConnection = !request.keepAlive;
            filterResponseSafely_(request, response);
            recordResponseMetrics_(response, elapsedMs_(startAt));
            if (complete) {
                complete(std::move(response));
            }
            notifyDrainState_();
            return;
        }

        std::shared_ptr<const SwHttpRequest> requestState;
        std::shared_ptr<DispatchGate_> gate;
        try {
            requestState = std::make_shared<SwHttpRequest>(request);
            gate = std::make_shared<DispatchGate_>();
        } catch (...) {
            if (dispatchRequestBytes > 0) {
                releasePendingRequestBytes_(dispatchRequestBytes);
            }
            releaseInFlight_();
            SwHttpResponse response = internalErrorResponse_(request);
            response.closeConnection = true;
            recordResponseMetrics_(response, elapsedMs_(startAt));
            if (complete) {
                complete(std::move(response));
            }
            notifyDrainState_();
            return;
        }
        std::shared_ptr<LifetimeState_> lifetime = m_lifetime;
        DispatchGate_* const gateKey = gate.get();
        gate->finishCallback = [lifetime, complete, startAt, gateKey,
                                dispatchRequestBytes](SwHttpResponse response) mutable {
            {
                LifetimeAccess_ access(lifetime);
                if (!access) {
                    return;
                }
                SwHttpServer* owner = access.get();
                {
                    SwMutexLocker locker(&owner->m_dispatchMutex);
                    owner->m_dispatchGates.remove(gateKey);
                }
                if (dispatchRequestBytes > 0) {
                    owner->releasePendingRequestBytes_(dispatchRequestBytes);
                }
                owner->releaseInFlight_();
                owner->recordResponseMetrics_(response, elapsedMs_(startAt));
                owner->notifyDrainState_();
            }
            if (complete) {
                try {
                    complete(std::move(response));
                } catch (...) {
                    // Accounting and gate removal are already complete. A
                    // transport callback must not unwind through the server.
                }
            }
        };

        if (m_timeouts.routeTimeoutMs > 0) {
            SwTimer* timeout = new SwTimer(this);
            timeout->setSingleShot(true);
            gate->timeout = timeout;
            const bool keepAlive = request.keepAlive;
            SwObject::connect(timeout, &SwTimer::timeout, this,
                              [this, gate, keepAlive, requestState]() {
                SwHttpResponse timedOut = swHttpTextResponse(504, "Route timeout");
                timedOut.closeConnection = !keepAlive;
                filterResponseSafely_(*requestState, timedOut);
                gate->finish(std::move(timedOut));
            });
            timeout->start(m_timeouts.routeTimeoutMs);
        }

        // Publish only after callback and deadline are fully initialized. Timer callbacks execute
        // on this affinity after the current dispatch turn, so the gate cannot finish before it
        // becomes visible; a concurrent close() can safely finish everything it can observe.
        bool published = false;
        {
            SwMutexLocker locker(&m_dispatchMutex);
            if (!m_dispatchStopping) {
                m_dispatchGates[gateKey] = gate;
                published = true;
            }
        }
        if (!published) {
            SwHttpResponse stopping = swHttpTextResponse(503, "Server shutting down");
            stopping.closeConnection = true;
            filterResponseSafely_(request, stopping);
            gate->finish(std::move(stopping));
            return;
        }

#if SW_HTTPSERVER_HAS_THREADPOOL
        if (m_dispatchMode == DispatchMode::ThreadPool &&
            m_threadPool &&
            !m_router.willRouteAsync(request) &&
            m_preRouteHandlersAsync.isEmpty()) {
            if (!tryReserveThreadPoolDispatch_()) {
                SwHttpResponse response = swHttpTextResponse(503, "ThreadPool saturated");
                response.closeConnection = !request.keepAlive;
                filterResponseSafely_(request, response);
                gate->finish(std::move(response));
                return;
            }

            bool rejectedByBackpressure = false;
            bool started = m_threadPool->tryStartQueued([lifetime, requestState, gate]() {
                LifetimeAccess_ access(lifetime);
                if (!access) {
                    return;
                }
                SwHttpServer* owner = access.get();
                SwHttpResponse computed = owner->routeRequestInlineSafely_(
                    *requestState, false);
                owner->releaseThreadPoolDispatch_();
                finishAsyncResponse_(lifetime, requestState, gate,
                                     std::move(computed));
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
            filterResponseSafely_(request, saturated);
            gate->finish(std::move(saturated));
            return;
        }
#endif

        routeRequestAsync_(requestState, [lifetime, requestState, gate](SwHttpResponse response) {
            finishAsyncResponse_(lifetime, requestState, gate, std::move(response));
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

    bool reservePendingRequestBytes_(std::size_t bytes) {
        SwMutexLocker locker(&m_metricsMutex);
        if (m_limits.maxPendingRequestBytesGlobal > 0 &&
            (bytes > m_limits.maxPendingRequestBytesGlobal ||
             m_pendingRequestBytesGlobal >
                 m_limits.maxPendingRequestBytesGlobal - bytes)) {
            return false;
        }
        m_pendingRequestBytesGlobal += bytes;
        return true;
    }

    static void saturatingAdd_(std::size_t value, std::size_t& total) {
        const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        total = value > maximum - total ? maximum : total + value;
    }

    static std::size_t retainedRequestPayloadBytes_(const SwHttpRequest& request) {
        std::size_t total = static_cast<std::size_t>(request.body.size());
        for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
            saturatingAdd_(static_cast<std::size_t>(part.data.size()), total);
            saturatingAdd_(part.name.size(), total);
            saturatingAdd_(part.fileName.size(), total);
            saturatingAdd_(part.contentType.size(), total);
            saturatingAdd_(part.tempFilePath.size(), total);
            for (SwMap<SwString, SwString>::const_iterator it = part.headers.begin();
                 it != part.headers.end(); ++it) {
                saturatingAdd_(it.key().size(), total);
                saturatingAdd_(it.value().size(), total);
            }
        }
        for (SwMap<SwString, SwString>::const_iterator it = request.formFields.begin();
             it != request.formFields.end(); ++it) {
            saturatingAdd_(it.key().size(), total);
            saturatingAdd_(it.value().size(), total);
        }
        return total;
    }

    void releasePendingRequestBytes_(std::size_t bytes) {
        SwMutexLocker locker(&m_metricsMutex);
        m_pendingRequestBytesGlobal = bytes > m_pendingRequestBytesGlobal
                                          ? 0
                                          : m_pendingRequestBytesGlobal - bytes;
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
            if (response.chunkedParts.isEmpty()) {
                return response.body.size();
            }
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
    mutable SwMutex m_sessionsMutex;
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
    std::size_t m_pendingRequestBytesGlobal = 0;
    mutable SwMutex m_dispatchMutex;
    SwMap<DispatchGate_*, std::shared_ptr<DispatchGate_>> m_dispatchGates;
    bool m_dispatchStopping = false;
    std::shared_ptr<LifetimeState_> m_lifetime;

    void tryPreRouteAsync_(const std::shared_ptr<const SwHttpRequest>& request,
                           std::size_t index,
                           const std::function<void(bool, const SwHttpResponse&)>& complete) const {
        if (!request) {
            return;
        }
        if (index >= m_preRouteHandlersAsync.size()) {
            if (complete) {
                complete(false, SwHttpResponse());
            }
            return;
        }

        const SwHttpPreRouteAsyncHandler handler = m_preRouteHandlersAsync[index];
        if (!handler) {
            tryPreRouteAsync_(request, index + 1, complete);
            return;
        }

        std::shared_ptr<LifetimeState_> lifetime = m_lifetime;
        try {
            handler(*request,
                    [lifetime, request, index, complete](
                        bool handled, const SwHttpResponse& response) {
                if (handled) {
                    if (complete) {
                        complete(true, response);
                    }
                    return;
                }
                LifetimeAccess_ access(lifetime);
                if (!access) {
                    return;
                }
                SwHttpServer* owner = access.get();
                const bool scheduled = owner->runOnAffinityReliable_(
                    [lifetime, request, index, complete]() {
                        LifetimeAccess_ continuationAccess(lifetime);
                        if (continuationAccess) {
                            continuationAccess.get()->tryPreRouteAsync_(
                                request, index + 1, complete);
                        }
                    });
                if (!scheduled && complete) {
                    SwHttpResponse unavailable = swHttpTextResponse(
                        503, "Async pre-route continuation unavailable");
                    unavailable.closeConnection = true;
                    complete(true, unavailable);
                }
            });
        } catch (...) {
            if (complete) {
                SwHttpResponse failed = internalErrorResponse_(*request);
                failed.closeConnection = true;
                complete(true, failed);
            }
        }
    }
};

#undef SW_HTTPSERVER_HAS_THREADPOOL
