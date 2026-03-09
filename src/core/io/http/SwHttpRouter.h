#pragma once

/**
 * @file src/core/io/http/SwHttpRouter.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpRouter in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP router interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpRouter.
 *
 * The routing contract in this header is meant to stay predictable under complex URL patterns,
 * path parameters, and dispatch ordering, without performing socket IO itself.
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

#include "http/SwHttpTypes.h"

#include <functional>

using SwHttpRouteCallback = std::function<SwHttpResponse(const SwHttpRequest&)>;
using SwHttpRouteResponder = std::function<void(const SwHttpResponse&)>;
using SwHttpRouteAsyncCallback = std::function<void(const SwHttpRequest&, const SwHttpRouteResponder&)>;

class SwHttpRouter {
public:
    enum class TrailingSlashPolicy {
        Ignore,
        RedirectToNoSlash,
        RedirectToSlash
    };

    /**
     * @brief Registers an HTTP route handler.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addRoute(const SwString& method, const SwString& pattern, const SwHttpRouteCallback& callback) {
        addNamedRoute(SwString(), method, pattern, callback);
    }

    /**
     * @brief Registers an asynchronous HTTP route handler.
     * @param method HTTP method involved in the operation.
     * @param pattern Pattern used by the operation.
     * @param callback Callback invoked by the operation.
     */
    void addRouteAsync(const SwString& method, const SwString& pattern, const SwHttpRouteAsyncCallback& callback) {
        addNamedRouteAsync(SwString(), method, pattern, callback);
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
        if (!callback) {
            return;
        }

        SwHttpRouteEntry entry;
        entry.method = method.trimmed().toUpper();
        if (entry.method.isEmpty()) {
            entry.method = "*";
        }
        entry.pattern = normalizePattern_(pattern);
        if (!compilePattern_(entry.pattern, entry.segments, entry.specificityScore)) {
            return;
        }
        entry.callback = callback;
        entry.asyncCallback = nullptr;
        entry.isAsync = false;
        entry.name = routeName.trimmed();

        int insertPos = static_cast<int>(m_routes.size());
        for (int i = 0; i < static_cast<int>(m_routes.size()); ++i) {
            if (entry.specificityScore > m_routes[i].specificityScore) {
                insertPos = i;
                break;
            }
        }
        m_routes.insert(static_cast<std::size_t>(insertPos), entry);

        if (!entry.name.isEmpty()) {
            m_namedPatterns[entry.name] = entry.pattern;
        }
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
        if (!callback) {
            return;
        }

        SwHttpRouteEntry entry;
        entry.method = method.trimmed().toUpper();
        if (entry.method.isEmpty()) {
            entry.method = "*";
        }
        entry.pattern = normalizePattern_(pattern);
        if (!compilePattern_(entry.pattern, entry.segments, entry.specificityScore)) {
            return;
        }
        entry.callback = nullptr;
        entry.asyncCallback = callback;
        entry.isAsync = true;
        entry.name = routeName.trimmed();

        int insertPos = static_cast<int>(m_routes.size());
        for (int i = 0; i < static_cast<int>(m_routes.size()); ++i) {
            if (entry.specificityScore > m_routes[i].specificityScore) {
                insertPos = i;
                break;
            }
        }
        m_routes.insert(static_cast<std::size_t>(insertPos), entry);

        if (!entry.name.isEmpty()) {
            m_namedPatterns[entry.name] = entry.pattern;
        }
    }

    /**
     * @brief Returns whether a named route is currently registered.
     * @param routeName Stable route name to look up.
     * @return `true` when the router contains the requested route name; otherwise `false`.
     */
    bool hasRouteName(const SwString& routeName) const {
        return m_namedPatterns.contains(routeName);
    }

    /**
     * @brief Returns the normalized pattern registered for a named route.
     * @param routeName Stable route name to resolve.
     * @return The pattern associated with the route, or an empty string when the name is unknown.
     */
    SwString routePattern(const SwString& routeName) const {
        return m_namedPatterns.value(routeName, SwString());
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
     * Route generation reuses the same normalized pattern data that request matching uses, which
     * keeps reverse routing consistent with the registered route table.
     */
    bool buildUrl(const SwString& routeName,
                  const SwMap<SwString, SwString>& pathParams,
                  SwString& outUrl,
                  const SwMap<SwString, SwString>& queryParams = SwMap<SwString, SwString>()) const {
        outUrl.clear();
        if (!m_namedPatterns.contains(routeName)) {
            return false;
        }

        const SwString pattern = m_namedPatterns[routeName];
        SwList<SwString> parts = splitPath_(pattern);
        SwString path = "/";

        for (std::size_t i = 0; i < parts.size(); ++i) {
            const SwString token = parts[i];
            SwString segment;
            if (token.startsWith(":")) {
                SwString nameToken = token.mid(1);
                SwString key;
                ParamConstraint constraint = ParamConstraint::None;
                if (!parseConstraint_(nameToken, key, constraint)) {
                    return false;
                }
                if (!pathParams.contains(key)) {
                    return false;
                }
                segment = pathParams[key];
                if (!checkConstraint_(constraint, segment)) {
                    return false;
                }
            } else if (token.startsWith("*")) {
                SwString key = token.mid(1);
                if (key.isEmpty()) {
                    key = "wildcard";
                }
                if (!pathParams.contains(key)) {
                    return false;
                }
                segment = pathParams[key];
            } else {
                segment = token;
            }

            if (i > 0) {
                path += "/";
            }
            path += segment;
        }

        if (path.isEmpty()) {
            path = "/";
        }
        outUrl = swHttpNormalizePath(path);
        appendQueryString_(outUrl, queryParams);
        return true;
    }

    /**
     * @brief Sets the fallback handler used when no registered route matches a request.
     * @param callback Callback invoked to build the fallback response.
     */
    void setNotFoundHandler(const SwHttpRouteCallback& callback) {
        m_notFoundHandler = callback;
    }

    /**
     * @brief Configures how paths that differ only by a trailing slash are handled.
     * @param policy Trailing-slash policy applied during route resolution.
     */
    void setTrailingSlashPolicy(TrailingSlashPolicy policy) {
        m_trailingSlashPolicy = policy;
    }

    /**
     * @brief Returns the trailing-slash policy currently used by the router.
     * @return The active trailing-slash handling policy.
     */
    TrailingSlashPolicy trailingSlashPolicy() const {
        return m_trailingSlashPolicy;
    }

    /**
     * @brief Matches a request against the registered routes and produces a synchronous response.
     * @param request Request to resolve.
     * @param response Output response filled when a route, redirect, or fallback handler applies.
     * @return `true` when the request was handled; otherwise `false`.
     *
     * @details
     * This method also handles trailing-slash redirects, `405 Method Not Allowed` responses, and
     * fallback dispatch through the configured not-found handler.
     */
    bool route(const SwHttpRequest& request, SwHttpResponse& response) const {
        if (shouldRedirectTrailingSlash_(request, response)) {
            return true;
        }

        SwList<SwString> pathSegments = splitPath_(request.path);
        SwString method = request.method.toUpper();

        SwList<SwString> allowedMethods;
        bool pathMatchedOtherMethod = false;

        for (std::size_t i = 0; i < m_routes.size(); ++i) {
            const SwHttpRouteEntry& entry = m_routes[i];

            SwMap<SwString, SwString> params;
            if (!matchSegments_(entry.segments, pathSegments, params)) {
                continue;
            }

            const bool methodMatch = (entry.method == "*" || entry.method == method ||
                                      (method == "HEAD" && entry.method == "GET"));
            if (!methodMatch) {
                pathMatchedOtherMethod = true;
                if (entry.method != "*") {
                    appendUniqueMethod_(allowedMethods, entry.method);
                }
                continue;
            }

            SwHttpRequest routedRequest = request;
            routedRequest.pathParams = params;
            if (entry.callback) {
                response = entry.callback(routedRequest);
            } else {
                response = swHttpTextResponse(500, "Async route requires async dispatch");
                response.closeConnection = !request.keepAlive;
            }
            return true;
        }

        if (pathMatchedOtherMethod) {
            response = swHttpTextResponse(405, "Method Not Allowed");
            response.closeConnection = !request.keepAlive;
            if (!allowedMethods.isEmpty()) {
                SwString allowValue;
                for (std::size_t i = 0; i < allowedMethods.size(); ++i) {
                    if (!allowValue.isEmpty()) {
                        allowValue += ", ";
                    }
                    allowValue += allowedMethods[i];
                }
                response.headers["allow"] = allowValue;
            }
            return true;
        }

        if (m_notFoundHandler) {
            response = m_notFoundHandler(request);
            return true;
        }

        return false;
    }

    /**
     * @brief Matches a request and dispatches its response through the supplied responder callback.
     * @param request Request to resolve.
     * @param responder Callback used to deliver the final response.
     * @return `true` when the request was accepted for synchronous or asynchronous handling;
     *         otherwise `false`.
     *
     * @details
     * The router transparently handles both synchronous and asynchronous route entries. Redirects,
     * `405` responses, and fallback responses are also emitted through `responder`.
     */
    bool routeAsync(const SwHttpRequest& request, const SwHttpRouteResponder& responder) const {
        if (!responder) {
            return false;
        }

        SwHttpResponse redirectResponse;
        if (shouldRedirectTrailingSlash_(request, redirectResponse)) {
            responder(redirectResponse);
            return true;
        }

        SwList<SwString> pathSegments = splitPath_(request.path);
        SwString method = request.method.toUpper();

        SwList<SwString> allowedMethods;
        bool pathMatchedOtherMethod = false;

        for (std::size_t i = 0; i < m_routes.size(); ++i) {
            const SwHttpRouteEntry& entry = m_routes[i];

            SwMap<SwString, SwString> params;
            if (!matchSegments_(entry.segments, pathSegments, params)) {
                continue;
            }

            const bool methodMatch = (entry.method == "*" || entry.method == method ||
                                      (method == "HEAD" && entry.method == "GET"));
            if (!methodMatch) {
                pathMatchedOtherMethod = true;
                if (entry.method != "*") {
                    appendUniqueMethod_(allowedMethods, entry.method);
                }
                continue;
            }

            SwHttpRequest routedRequest = request;
            routedRequest.pathParams = params;
            if (entry.asyncCallback) {
                entry.asyncCallback(routedRequest, responder);
                return true;
            }
            if (entry.callback) {
                responder(entry.callback(routedRequest));
                return true;
            }

            SwHttpResponse response = swHttpTextResponse(500, "Route callback missing");
            response.closeConnection = !request.keepAlive;
            responder(response);
            return true;
        }

        if (pathMatchedOtherMethod) {
            SwHttpResponse response = swHttpTextResponse(405, "Method Not Allowed");
            response.closeConnection = !request.keepAlive;
            if (!allowedMethods.isEmpty()) {
                SwString allowValue;
                for (std::size_t i = 0; i < allowedMethods.size(); ++i) {
                    if (!allowValue.isEmpty()) {
                        allowValue += ", ";
                    }
                    allowValue += allowedMethods[i];
                }
                response.headers["allow"] = allowValue;
            }
            responder(response);
            return true;
        }

        if (m_notFoundHandler) {
            responder(m_notFoundHandler(request));
            return true;
        }

        return false;
    }

    /**
     * @brief Returns whether the matching route, if any, is asynchronous.
     * @param request Request to probe against the registered route table.
     * @return `true` when the first matching route is asynchronous; otherwise `false`.
     */
    bool willRouteAsync(const SwHttpRequest& request) const {
        SwHttpResponse redirectResponse;
        if (shouldRedirectTrailingSlash_(request, redirectResponse)) {
            return false;
        }

        const SwList<SwString> pathSegments = splitPath_(request.path);
        const SwString method = request.method.toUpper();

        for (std::size_t i = 0; i < m_routes.size(); ++i) {
            const SwHttpRouteEntry& entry = m_routes[i];
            SwMap<SwString, SwString> params;
            if (!matchSegments_(entry.segments, pathSegments, params)) {
                continue;
            }

            const bool methodMatch = (entry.method == "*" || entry.method == method ||
                                      (method == "HEAD" && entry.method == "GET"));
            if (!methodMatch) {
                continue;
            }
            return entry.isAsync;
        }
        return false;
    }

private:
    enum class SegmentType {
        Literal,
        Parameter,
        Wildcard
    };

    enum class ParamConstraint {
        None,
        Int,
        Alpha,
        Alnum,
        Hex,
        Slug,
        Uuid
    };

    struct RouteSegment {
        SegmentType type = SegmentType::Literal;
        SwString literal;
        SwString name;
        ParamConstraint constraint = ParamConstraint::None;
    };

    struct SwHttpRouteEntry {
        SwString method;
        SwString pattern;
        SwString name;
        SwList<RouteSegment> segments;
        int specificityScore = 0;
        bool isAsync = false;
        SwHttpRouteCallback callback;
        SwHttpRouteAsyncCallback asyncCallback;
    };

    SwList<SwHttpRouteEntry> m_routes;
    SwHttpRouteCallback m_notFoundHandler;
    SwMap<SwString, SwString> m_namedPatterns;
    TrailingSlashPolicy m_trailingSlashPolicy = TrailingSlashPolicy::Ignore;

    static SwString normalizePattern_(const SwString& pattern) {
        SwString normalized = pattern.trimmed();
        if (normalized.isEmpty()) {
            normalized = "/";
        }
        if (!normalized.startsWith("/")) {
            normalized.prepend("/");
        }
        while (normalized.contains("//")) {
            normalized.replace("//", "/");
        }
        if (normalized.size() > 1 && normalized.endsWith("/")) {
            normalized.chop(1);
        }
        return normalized;
    }

    static SwList<SwString> splitPath_(const SwString& path) {
        SwList<SwString> result;
        SwString normalized = swHttpNormalizePath(path);
        if (normalized == "/") {
            return result;
        }
        SwList<SwString> parts = normalized.split('/');
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (!parts[i].isEmpty()) {
                result.append(parts[i]);
            }
        }
        return result;
    }

    static bool parseConstraint_(const SwString& token, SwString& nameOut, ParamConstraint& constraintOut) {
        constraintOut = ParamConstraint::None;
        SwString name = token;
        int paren = token.indexOf("(");
        if (paren < 0) {
            nameOut = name;
            return !nameOut.isEmpty();
        }
        if (!token.endsWith(")")) {
            return false;
        }
        nameOut = token.left(paren).trimmed();
        SwString spec = token.mid(paren + 1, static_cast<int>(token.size()) - paren - 2).trimmed().toLower();
        if (nameOut.isEmpty()) {
            return false;
        }
        if (spec == "int") {
            constraintOut = ParamConstraint::Int;
            return true;
        }
        if (spec == "alpha") {
            constraintOut = ParamConstraint::Alpha;
            return true;
        }
        if (spec == "alnum") {
            constraintOut = ParamConstraint::Alnum;
            return true;
        }
        if (spec == "hex") {
            constraintOut = ParamConstraint::Hex;
            return true;
        }
        if (spec == "slug") {
            constraintOut = ParamConstraint::Slug;
            return true;
        }
        if (spec == "uuid") {
            constraintOut = ParamConstraint::Uuid;
            return true;
        }
        return false;
    }

    static bool compilePattern_(const SwString& pattern, SwList<RouteSegment>& segmentsOut, int& scoreOut) {
        segmentsOut.clear();
        scoreOut = 0;
        SwList<SwString> parts = splitPath_(pattern);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            SwString token = parts[i];
            RouteSegment segment;
            if (token.startsWith(":")) {
                segment.type = SegmentType::Parameter;
                SwString nameToken = token.mid(1);
                if (!parseConstraint_(nameToken, segment.name, segment.constraint)) {
                    return false;
                }
                scoreOut += (segment.constraint == ParamConstraint::None) ? 30 : 50;
            } else if (token.startsWith("*")) {
                segment.type = SegmentType::Wildcard;
                segment.name = token.mid(1);
                if (segment.name.isEmpty()) {
                    segment.name = "wildcard";
                }
                scoreOut += 1;
                segmentsOut.append(segment);
                break;
            } else {
                segment.type = SegmentType::Literal;
                segment.literal = token;
                scoreOut += 100;
            }
            segmentsOut.append(segment);
        }
        return true;
    }

    static bool checkConstraint_(ParamConstraint constraint, const SwString& value) {
        if (constraint == ParamConstraint::None) {
            return true;
        }
        if (value.isEmpty()) {
            return false;
        }

        if (constraint == ParamConstraint::Uuid) {
            if (value.size() != 36) {
                return false;
            }
            for (std::size_t i = 0; i < value.size(); ++i) {
                const char c = value[i];
                if (i == 8 || i == 13 || i == 18 || i == 23) {
                    if (c != '-') {
                        return false;
                    }
                    continue;
                }
                if (!isHex_(c)) {
                    return false;
                }
            }
            return true;
        }

        for (std::size_t i = 0; i < value.size(); ++i) {
            char c = value[i];
            if (constraint == ParamConstraint::Int) {
                if (c < '0' || c > '9') {
                    return false;
                }
                continue;
            }
            if (constraint == ParamConstraint::Alpha) {
                const bool lower = (c >= 'a' && c <= 'z');
                const bool upper = (c >= 'A' && c <= 'Z');
                if (!lower && !upper) {
                    return false;
                }
                continue;
            }
            if (constraint == ParamConstraint::Alnum) {
                const bool lower = (c >= 'a' && c <= 'z');
                const bool upper = (c >= 'A' && c <= 'Z');
                const bool digit = (c >= '0' && c <= '9');
                if (!lower && !upper && !digit) {
                    return false;
                }
                continue;
            }
            if (constraint == ParamConstraint::Hex) {
                if (!isHex_(c)) {
                    return false;
                }
                continue;
            }
            if (constraint == ParamConstraint::Slug) {
                const bool lower = (c >= 'a' && c <= 'z');
                const bool upper = (c >= 'A' && c <= 'Z');
                const bool digit = (c >= '0' && c <= '9');
                const bool punct = (c == '-' || c == '_' || c == '.');
                if (!lower && !upper && !digit && !punct) {
                    return false;
                }
                continue;
            }
        }
        return true;
    }

    static bool isHex_(char c) {
        const bool digit = (c >= '0' && c <= '9');
        const bool lower = (c >= 'a' && c <= 'f');
        const bool upper = (c >= 'A' && c <= 'F');
        return digit || lower || upper;
    }

    static bool matchSegments_(const SwList<RouteSegment>& routeSegments,
                               const SwList<SwString>& pathSegments,
                               SwMap<SwString, SwString>& paramsOut) {
        paramsOut.clear();

        std::size_t pi = 0;
        std::size_t ri = 0;
        while (ri < routeSegments.size()) {
            const RouteSegment& segment = routeSegments[ri];
            if (segment.type == SegmentType::Wildcard) {
                SwString rest;
                for (std::size_t j = pi; j < pathSegments.size(); ++j) {
                    if (!rest.isEmpty()) {
                        rest.append("/");
                    }
                    rest.append(pathSegments[j]);
                }
                paramsOut[segment.name] = rest;
                return true;
            }

            if (pi >= pathSegments.size()) {
                return false;
            }

            const SwString& current = pathSegments[pi];
            if (segment.type == SegmentType::Literal) {
                if (current != segment.literal) {
                    return false;
                }
            } else if (segment.type == SegmentType::Parameter) {
                if (!checkConstraint_(segment.constraint, current)) {
                    return false;
                }
                paramsOut[segment.name] = current;
            }

            ++pi;
            ++ri;
        }

        return pi == pathSegments.size();
    }

    bool shouldRedirectTrailingSlash_(const SwHttpRequest& request, SwHttpResponse& response) const {
        if (m_trailingSlashPolicy == TrailingSlashPolicy::Ignore) {
            return false;
        }

        SwString path = swHttpNormalizePath(request.path);
        if (path == "/") {
            return false;
        }

        const bool hasTrailingSlash = path.endsWith("/");
        bool needRedirect = false;
        if (m_trailingSlashPolicy == TrailingSlashPolicy::RedirectToNoSlash && hasTrailingSlash) {
            path.chop(1);
            needRedirect = true;
        } else if (m_trailingSlashPolicy == TrailingSlashPolicy::RedirectToSlash && !hasTrailingSlash) {
            path += "/";
            needRedirect = true;
        }

        if (!needRedirect) {
            return false;
        }

        SwString location = path;
        if (!request.queryString.isEmpty()) {
            location += "?";
            location += request.queryString;
        }

        response.status = 308;
        response.reason = swHttpStatusReason(308);
        response.headers["location"] = location;
        response.headers["content-length"] = "0";
        response.body.clear();
        response.closeConnection = !request.keepAlive;
        return true;
    }

    static void appendUniqueMethod_(SwList<SwString>& methods, const SwString& method) {
        if (methods.contains(method)) {
            return;
        }

        int insertPos = static_cast<int>(methods.size());
        for (int i = 0; i < static_cast<int>(methods.size()); ++i) {
            if (method < methods[i]) {
                insertPos = i;
                break;
            }
        }
        methods.insert(static_cast<std::size_t>(insertPos), method);
    }

    static void appendQueryString_(SwString& url, const SwMap<SwString, SwString>& queryParams) {
        if (queryParams.isEmpty()) {
            return;
        }

        bool first = true;
        for (auto it = queryParams.begin(); it != queryParams.end(); ++it) {
            const SwString key = percentEncode_(it.key());
            const SwString val = percentEncode_(it.value());
            if (first) {
                url += "?";
                first = false;
            } else {
                url += "&";
            }
            url += key;
            url += "=";
            url += val;
        }
    }

    static SwString percentEncode_(const SwString& text) {
        SwString out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            const bool lower = (c >= 'a' && c <= 'z');
            const bool upper = (c >= 'A' && c <= 'Z');
            const bool digit = (c >= '0' && c <= '9');
            const bool safe = (c == '-' || c == '_' || c == '.' || c == '~');
            if (lower || upper || digit || safe) {
                out.append(c);
                continue;
            }
            out.append('%');
            out.append(hexDigit_((c >> 4) & 0xF));
            out.append(hexDigit_(c & 0xF));
        }
        return out;
    }

    static char hexDigit_(int value) {
        if (value < 10) {
            return static_cast<char>('0' + value);
        }
        return static_cast<char>('A' + (value - 10));
    }
};
