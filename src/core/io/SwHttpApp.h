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
#include "http/SwHttpContext.h"

#include <functional>
#include <chrono>
#include <exception>

class SwHttpApp {
public:
    using SwHttpHandler = std::function<void(SwHttpContext&)>;
    using SwHttpNext = std::function<void()>;
    using SwHttpMiddleware = std::function<void(SwHttpContext&, const SwHttpNext&)>;
    using SwHttpRecoveryHandler = std::function<void(SwHttpContext&, const SwString&)>;

    struct SwHttpRouteOptions {
        SwString name;
        int softTimeoutMs = 0;
        bool timeoutOverridesResponse = false;
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
    }

    /**
     * @brief Starts listening for incoming traffic.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool listen(uint16_t port) {
        return m_server.listen(port);
    }

    /**
     * @brief Starts listening for incoming HTTPS traffic.
     * @param port Local port used by the operation (typically 443).
     * @param certPath Path to the PEM certificate file.
     * @param keyPath Path to the PEM private key file.
     * @return `true` on success; otherwise `false`.
     */
    bool listen(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        return m_server.listen(port, certPath, keyPath);
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() {
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
        return m_server.closeGraceful(timeoutMs);
    }

    /**
     * @brief Sets the limits.
     * @param limits Limit configuration to apply.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLimits(const SwHttpLimits& limits) {
        m_server.setLimits(limits);
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

private:
    SwHttpServer m_server;
    SwList<SwHttpMiddleware> m_middlewares;
    SwString m_groupPrefix;
    SwHttpRecoveryHandler m_recoveryHandler;

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
