#pragma once

/**
 * @file src/core/io/http/SwHttpMiddlewarePack.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpMiddlewarePack in the CoreSw HTTP server
 * layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP middleware pack interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpMiddlewareMetrics, SwHttpMetricsCollector,
 * SwHttpRateLimiter, and SwHttpMiddlewarePack.
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

#include "../SwHttpApp.h"
#include "SwMutex.h"
#include "SwDateTime.h"
#include "SwDebug.h"

#include <functional>
#include <chrono>
#include <exception>

static constexpr const char* kSwLogCategory_SwHttpMiddlewarePack = "sw.core.io.swhttpmiddlewarepack";

struct SwHttpMiddlewareMetrics {
    long long totalRequests = 0;
    long long totalLatencyMs = 0;
    long long maxLatencyMs = 0;
    long long totalRequestBodyBytes = 0;
    long long totalResponseBodyBytes = 0;
    SwMap<SwString, long long> statusCounters;
};

class SwHttpMetricsCollector {
public:
    /**
     * @brief Performs the `record` operation.
     * @param request Request instance associated with the operation.
     * @param response Response instance associated with the operation.
     * @param latencyMs Value passed to the method.
     */
    void record(const SwHttpRequest& request, const SwHttpResponse& response, long long latencyMs) {
        SwMutexLocker locker(&m_mutex);
        ++m_metrics.totalRequests;
        m_metrics.totalLatencyMs += latencyMs;
        if (latencyMs > m_metrics.maxLatencyMs) {
            m_metrics.maxLatencyMs = latencyMs;
        }
        m_metrics.totalRequestBodyBytes += static_cast<long long>(request.body.size());
        m_metrics.totalResponseBodyBytes += static_cast<long long>(responseBodyBytes_(response));
        const SwString key = SwString::number(response.status);
        m_metrics.statusCounters[key] = m_metrics.statusCounters.value(key, 0LL) + 1LL;
    }

    /**
     * @brief Returns the current snapshot.
     * @return The current snapshot.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpMiddlewareMetrics snapshot() const {
        SwMutexLocker locker(&m_mutex);
        return m_metrics;
    }

    /**
     * @brief Resets the object to a baseline state.
     */
    void reset() {
        SwMutexLocker locker(&m_mutex);
        m_metrics = SwHttpMiddlewareMetrics();
    }

private:
    static std::size_t responseBodyBytes_(const SwHttpResponse& response) {
        if (response.hasFile) {
            return response.fileLength;
        }
        if (response.useChunkedTransfer) {
            std::size_t total = 0;
            for (std::size_t i = 0; i < response.chunkedParts.size(); ++i) {
                total += response.chunkedParts[i].size();
            }
            return total;
        }
        return response.body.size();
    }

    mutable SwMutex m_mutex;
    SwHttpMiddlewareMetrics m_metrics;
};

class SwHttpRateLimiter {
public:
    struct Options {
        int maxRequests = 120;
        int windowMs = 60 * 1000;
        int staleBucketMs = 5 * 60 * 1000;
    };

    struct Decision {
        bool allowed = true;
        int limit = 0;
        int remaining = 0;
        int retryAfterSeconds = 0;
    };

    /**
     * @brief Constructs a `SwHttpRateLimiter` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHttpRateLimiter()
        : m_options(Options()) {
    }

    /**
     * @brief Constructs a `SwHttpRateLimiter` instance.
     * @param options Option set controlling the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwHttpRateLimiter(const Options& options)
        : m_options(options) {
    }

    /**
     * @brief Sets the options.
     * @param options Option set controlling the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOptions(const Options& options) {
        SwMutexLocker locker(&m_mutex);
        m_options = options;
        m_buckets.clear();
        m_requestCountSincePrune = 0;
    }

    /**
     * @brief Returns the current options.
     * @return The current options.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Options options() const {
        SwMutexLocker locker(&m_mutex);
        return m_options;
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        SwMutexLocker locker(&m_mutex);
        m_buckets.clear();
        m_requestCountSincePrune = 0;
    }

    /**
     * @brief Performs the `acquire` operation.
     * @param rawKey Value passed to the method.
     * @return The requested acquire.
     */
    Decision acquire(const SwString& rawKey) {
        Decision result;

        SwString key = rawKey.trimmed();
        if (key.isEmpty()) {
            key = "<global>";
        }

        const long long nowMs = nowMs_();
        SwMutexLocker locker(&m_mutex);
        ++m_requestCountSincePrune;

        if (m_options.maxRequests <= 0 || m_options.windowMs <= 0) {
            result.allowed = true;
            result.limit = 0;
            result.remaining = 0;
            result.retryAfterSeconds = 0;
            return result;
        }

        Bucket& bucket = m_buckets[key];
        if (bucket.lastSeenMs == 0) {
            bucket.tokens = static_cast<double>(m_options.maxRequests);
            bucket.lastRefillMs = nowMs;
            bucket.lastSeenMs = nowMs;
        } else {
            refillBucket_(bucket, nowMs);
            bucket.lastSeenMs = nowMs;
        }

        result.limit = m_options.maxRequests;
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            result.allowed = true;
            result.remaining = static_cast<int>(bucket.tokens);
            if (result.remaining < 0) {
                result.remaining = 0;
            }
            result.retryAfterSeconds = 0;
        } else {
            result.allowed = false;
            result.remaining = 0;
            const double refillPerMs = static_cast<double>(m_options.maxRequests) / static_cast<double>(m_options.windowMs);
            if (refillPerMs <= 0.0) {
                result.retryAfterSeconds = (m_options.windowMs + 999) / 1000;
            } else {
                const double missingTokens = 1.0 - bucket.tokens;
                long long retryMs = static_cast<long long>(missingTokens / refillPerMs);
                if (retryMs < 1) {
                    retryMs = 1;
                }
                result.retryAfterSeconds = static_cast<int>((retryMs + 999) / 1000);
            }
        }

        if (m_requestCountSincePrune >= 256) {
            pruneBucketsLocked_(nowMs);
            m_requestCountSincePrune = 0;
        }
        return result;
    }

private:
    struct Bucket {
        double tokens = 0.0;
        long long lastRefillMs = 0;
        long long lastSeenMs = 0;
    };

    static long long nowMs_() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void refillBucket_(Bucket& bucket, long long nowMs) const {
        if (nowMs <= bucket.lastRefillMs) {
            return;
        }

        const long long elapsedMs = nowMs - bucket.lastRefillMs;
        const double refillPerMs = static_cast<double>(m_options.maxRequests) / static_cast<double>(m_options.windowMs);
        const double next = bucket.tokens + refillPerMs * static_cast<double>(elapsedMs);
        const double cap = static_cast<double>(m_options.maxRequests);
        bucket.tokens = (next > cap) ? cap : next;
        bucket.lastRefillMs = nowMs;
    }

    void pruneBucketsLocked_(long long nowMs) {
        if (m_options.staleBucketMs <= 0 || m_buckets.isEmpty()) {
            return;
        }

        SwList<SwString> toDelete;
        for (auto it = m_buckets.begin(); it != m_buckets.end(); ++it) {
            const Bucket& bucket = it.value();
            if (bucket.lastSeenMs <= 0) {
                continue;
            }
            const long long idleMs = nowMs - bucket.lastSeenMs;
            if (idleMs > m_options.staleBucketMs) {
                toDelete.append(it.key());
            }
        }
        for (std::size_t i = 0; i < toDelete.size(); ++i) {
            m_buckets.remove(toDelete[i]);
        }
    }

    mutable SwMutex m_mutex;
    Options m_options;
    SwMap<SwString, Bucket> m_buckets;
    int m_requestCountSincePrune = 0;
};

class SwHttpMiddlewarePack {
public:
    using SwHttpMiddleware = SwHttpApp::SwHttpMiddleware;
    using SwHttpNext = SwHttpApp::SwHttpNext;
    using SwHttpRecoveryHandler = SwHttpApp::SwHttpRecoveryHandler;

    struct RequestIdOptions {
        SwString requestHeader = "x-request-id";
        SwString responseHeader = "x-request-id";
        bool trustIncomingHeader = true;
        SwString contextLocalKey = "request-id";
    };

    struct StructuredLogOptions {
        SwString requestIdLocalKey = "request-id";
        bool logRequestBytes = true;
        bool logResponseBytes = true;
    };

    struct CorsOptions {
        SwList<SwString> allowedOrigins = SwList<SwString>{ "*" };
        SwString allowMethods = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
        SwString allowHeaders = "content-type,authorization,x-request-id";
        SwString exposeHeaders = "x-request-id";
        bool allowCredentials = false;
        int maxAgeSeconds = 600;
        bool handlePreflight = true;
    };

    struct RateLimitOptions {
        SwString clientKeyHeader = "x-forwarded-for";
        bool keyByPath = false;
        int maxRequests = 120;
        int windowMs = 60 * 1000;
        int staleBucketMs = 5 * 60 * 1000;
        SwString rejectBody = "Too Many Requests";
    };

    struct AuthOptions {
        /**
         * @brief Performs the `function<bool` operation.
         * @return The requested function<bool.
         */
        std::function<bool(SwHttpContext&)> authorize;
        /**
         * @brief Performs the `function<void` operation.
         * @return The requested function<void.
         */
        std::function<void(SwHttpContext&)> onReject;
        bool bypassOptionsPreflight = true;
        int rejectStatus = 401;
        SwString rejectBody = "Unauthorized";
    };

    struct RecoveryOptions {
        int statusCode = 500;
        SwString message = "Internal Server Error";
        bool includeErrorDetails = false;
        bool closeConnection = true;
        bool logException = true;
    };

    struct InstallOptions {
        bool enableRequestId = true;
        bool enableStructuredLogs = true;
        bool enableCors = true;
        bool enableRateLimit = true;
        bool enableAuth = false;
        bool enableMetrics = true;
        bool enableRecoveryHandler = true;
        RequestIdOptions requestId;
        StructuredLogOptions structuredLogs;
        CorsOptions cors;
        RateLimitOptions rateLimit;
        AuthOptions auth;
        RecoveryOptions recovery;
    };

    /**
     * @brief Returns the current request Id.
     * @return The current request Id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwHttpMiddleware requestId() {
        return requestId(RequestIdOptions());
    }

    /**
     * @brief Performs the `requestId` operation.
     * @param options Option set controlling the operation.
     * @return The requested request Id.
     */
    static SwHttpMiddleware requestId(const RequestIdOptions& options) {
        const SwString requestKey = options.requestHeader.toLower();
        const SwString responseKey = options.responseHeader.toLower();
        return [options, requestKey, responseKey](SwHttpContext& context, const SwHttpNext& next) {
            SwString requestId;
            if (options.trustIncomingHeader) {
                requestId = context.headerValue(requestKey, SwString()).trimmed();
            }
            if (requestId.isEmpty()) {
                requestId = generateRequestId_();
            }

            context.setLocal(options.contextLocalKey, requestId);
            context.response().headers[responseKey] = requestId;
            if (next) {
                next();
            }
            context.response().headers[responseKey] = requestId;
        };
    }

    /**
     * @brief Returns the current structured Logs.
     * @return The current structured Logs.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwHttpMiddleware structuredLogs() {
        return structuredLogs(StructuredLogOptions());
    }

    /**
     * @brief Performs the `structuredLogs` operation.
     * @param options Option set controlling the operation.
     * @return The requested structured Logs.
     */
    static SwHttpMiddleware structuredLogs(const StructuredLogOptions& options) {
        return [options](SwHttpContext& context, const SwHttpNext& next) {
            const auto startedAt = std::chrono::steady_clock::now();
            if (next) {
                next();
            }

            const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - startedAt)
                                            .count();

            const SwHttpRequest& request = context.request();
            const SwHttpResponse& response = context.response();
            const SwString requestId = context.localValue(options.requestIdLocalKey, context.headerValue("x-request-id"));

            SwString json = "{";
            json += "\"event\":\"http_request\"";
            json += ",\"time\":\"" + jsonEscape_(SwDateTime().toString()) + "\"";
            json += ",\"request_id\":\"" + jsonEscape_(requestId) + "\"";
            json += ",\"method\":\"" + jsonEscape_(request.method) + "\"";
            json += ",\"path\":\"" + jsonEscape_(request.path) + "\"";
            json += ",\"status\":" + SwString::number(response.status);
            json += ",\"latency_ms\":" + SwString::number(elapsedMs);
            if (options.logRequestBytes) {
                json += ",\"request_bytes\":" + SwString::number(static_cast<long long>(request.body.size()));
            }
            if (options.logResponseBytes) {
                json += ",\"response_bytes\":" + SwString::number(static_cast<long long>(responseBodyBytes_(response)));
            }
            json += "}";

            swCDebug(kSwLogCategory_SwHttpMiddlewarePack) << json;
        };
    }

    /**
     * @brief Returns the current cors.
     * @return The current cors.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwHttpMiddleware cors() {
        return cors(CorsOptions());
    }

    /**
     * @brief Performs the `cors` operation.
     * @param options Option set controlling the operation.
     * @return The requested cors.
     */
    static SwHttpMiddleware cors(const CorsOptions& options) {
        return [options](SwHttpContext& context, const SwHttpNext& next) {
            const SwString origin = context.headerValue("origin", SwString());
            const SwString allowedOrigin = resolveAllowedOrigin_(origin, options.allowedOrigins);
            if (!allowedOrigin.isEmpty()) {
                context.response().headers["access-control-allow-origin"] = allowedOrigin;
                if (allowedOrigin != "*") {
                    context.response().headers["vary"] = "origin";
                }
            }
            if (!options.allowMethods.isEmpty()) {
                context.response().headers["access-control-allow-methods"] = options.allowMethods;
            }
            if (!options.allowHeaders.isEmpty()) {
                context.response().headers["access-control-allow-headers"] = options.allowHeaders;
            }
            if (!options.exposeHeaders.isEmpty()) {
                context.response().headers["access-control-expose-headers"] = options.exposeHeaders;
            }
            if (options.allowCredentials) {
                context.response().headers["access-control-allow-credentials"] = "true";
            }
            if (options.maxAgeSeconds > 0) {
                context.response().headers["access-control-max-age"] = SwString::number(options.maxAgeSeconds);
            }

            const bool isPreflight =
                (context.method().toUpper() == "OPTIONS" &&
                 !context.headerValue("access-control-request-method", SwString()).isEmpty());
            if (options.handlePreflight && isPreflight) {
                context.noContent(204);
                return;
            }
            if (next) {
                next();
            }
        };
    }

    /**
     * @brief Performs the `rateLimit` operation.
     * @param limiter Value passed to the method.
     * @return The requested rate Limit.
     */
    static SwHttpMiddleware rateLimit(SwHttpRateLimiter* limiter) {
        return rateLimit(limiter, RateLimitOptions());
    }

    /**
     * @brief Performs the `rateLimit` operation.
     * @param limiter Value passed to the method.
     * @param options Option set controlling the operation.
     * @return The requested rate Limit.
     */
    static SwHttpMiddleware rateLimit(SwHttpRateLimiter* limiter,
                                      const RateLimitOptions& options) {
        if (!limiter) {
            return [](SwHttpContext&, const SwHttpNext& next) {
                if (next) {
                    next();
                }
            };
        }

        SwHttpRateLimiter::Options limiterOptions;
        limiterOptions.maxRequests = options.maxRequests;
        limiterOptions.windowMs = options.windowMs;
        limiterOptions.staleBucketMs = options.staleBucketMs;
        limiter->setOptions(limiterOptions);

        const SwString keyHeader = options.clientKeyHeader.toLower();
        return [limiter, options, keyHeader](SwHttpContext& context, const SwHttpNext& next) {
            SwString key = context.headerValue(keyHeader, SwString());
            if (key.isEmpty()) {
                key = "<global>";
            } else {
                const int comma = key.indexOf(",");
                if (comma >= 0) {
                    key = key.left(comma).trimmed();
                } else {
                    key = key.trimmed();
                }
            }
            if (options.keyByPath) {
                key += "|" + swHttpNormalizePath(context.path());
            }

            const SwHttpRateLimiter::Decision decision = limiter->acquire(key);
            context.response().headers["x-ratelimit-limit"] = SwString::number(decision.limit);
            context.response().headers["x-ratelimit-remaining"] = SwString::number(decision.remaining);
            if (!decision.allowed) {
                if (decision.retryAfterSeconds > 0) {
                    context.response().headers["retry-after"] = SwString::number(decision.retryAfterSeconds);
                }
                context.text(options.rejectBody, 429);
                return;
            }

            if (next) {
                next();
            }
        };
    }

    /**
     * @brief Performs the `authHook` operation.
     * @param options Option set controlling the operation.
     * @return The requested auth Hook.
     */
    static SwHttpMiddleware authHook(const AuthOptions& options) {
        return [options](SwHttpContext& context, const SwHttpNext& next) {
            if (options.bypassOptionsPreflight && context.method().toUpper() == "OPTIONS") {
                if (next) {
                    next();
                }
                return;
            }

            bool allowed = true;
            if (options.authorize) {
                allowed = options.authorize(context);
            }
            if (!allowed) {
                if (options.onReject) {
                    options.onReject(context);
                } else {
                    context.text(options.rejectBody, options.rejectStatus);
                }
                return;
            }

            if (next) {
                next();
            }
        };
    }

    /**
     * @brief Performs the `metrics` operation.
     * @param collector Value passed to the method.
     * @return The requested metrics.
     */
    static SwHttpMiddleware metrics(SwHttpMetricsCollector* collector) {
        if (!collector) {
            return [](SwHttpContext&, const SwHttpNext& next) {
                if (next) {
                    next();
                }
            };
        }

        return [collector](SwHttpContext& context, const SwHttpNext& next) {
            const auto startedAt = std::chrono::steady_clock::now();
            if (next) {
                next();
            }
            const long long latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - startedAt)
                                            .count();
            collector->record(context.request(), context.response(), latencyMs);
        };
    }

    /**
     * @brief Returns the current recovery Handler.
     * @return The current recovery Handler.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwHttpRecoveryHandler recoveryHandler() {
        return recoveryHandler(RecoveryOptions());
    }

    /**
     * @brief Performs the `recoveryHandler` operation.
     * @param options Option set controlling the operation.
     * @return The requested recovery Handler.
     */
    static SwHttpRecoveryHandler recoveryHandler(const RecoveryOptions& options) {
        return [options](SwHttpContext& context, const SwString& error) {
            if (options.logException) {
                swCWarning(kSwLogCategory_SwHttpMiddlewarePack) << "[SwHttpMiddlewarePack] recovered route exception: " << error;
            }
            if (context.handled()) {
                return;
            }

            SwString body = options.message;
            if (options.includeErrorDetails && !error.isEmpty()) {
                body += ": ";
                body += error;
            }
            context.text(body, options.statusCode);
            context.closeConnection(options.closeConnection);
        };
    }

    /**
     * @brief Performs the `install` operation.
     * @param app Value passed to the method.
     * @param options Option set controlling the operation.
     * @param rateLimiter Value passed to the method.
     * @param metricsCollector Value passed to the method.
     * @return The requested install.
     */
    static void install(SwHttpApp& app,
                        const InstallOptions& options,
                        SwHttpRateLimiter* rateLimiter = nullptr,
                        SwHttpMetricsCollector* metricsCollector = nullptr) {
        if (options.enableRecoveryHandler) {
            app.setRecoveryHandler(recoveryHandler(options.recovery));
        }
        if (options.enableRequestId) {
            app.use(requestId(options.requestId));
        }
        if (options.enableStructuredLogs) {
            app.use(structuredLogs(options.structuredLogs));
        }
        if (options.enableCors) {
            app.use(cors(options.cors));
        }
        if (options.enableRateLimit) {
            app.use(rateLimit(rateLimiter, options.rateLimit));
        }
        if (options.enableAuth) {
            app.use(authHook(options.auth));
        }
        if (options.enableMetrics) {
            app.use(metrics(metricsCollector));
        }
    }

private:
    static SwString resolveAllowedOrigin_(const SwString& origin, const SwList<SwString>& allowedOrigins) {
        if (allowedOrigins.isEmpty()) {
            return SwString();
        }

        for (std::size_t i = 0; i < allowedOrigins.size(); ++i) {
            const SwString item = allowedOrigins[i].trimmed();
            if (item == "*") {
                return "*";
            }
            if (!origin.isEmpty() && item == origin) {
                return origin;
            }
        }
        return SwString();
    }

    static std::size_t responseBodyBytes_(const SwHttpResponse& response) {
        if (response.hasFile) {
            return response.fileLength;
        }
        if (response.useChunkedTransfer) {
            std::size_t total = 0;
            for (std::size_t i = 0; i < response.chunkedParts.size(); ++i) {
                total += response.chunkedParts[i].size();
            }
            return total;
        }
        return response.body.size();
    }

    static SwString jsonEscape_(const SwString& text) {
        SwString out;
        out.reserve(text.size() + 16);
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '"') {
                out += "\\\"";
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out.append(c);
            }
        }
        return out;
    }

    static SwString generateRequestId_() {
        static SwMutex mutex;
        static long long counter = 0;

        long long localCounter = 0;
        {
            SwMutexLocker locker(&mutex);
            ++counter;
            localCounter = counter;
        }

        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
        return "req-" + SwString::number(nowMs) + "-" + SwString::number(localCounter);
    }
};
