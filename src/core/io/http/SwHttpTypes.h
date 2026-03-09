#pragma once

/**
 * @file src/core/io/http/SwHttpTypes.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpTypes in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP types interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwHttpLimits, SwHttpTimeouts, SwHttpStaticOptions,
 * SwHttpRequest, and SwHttpResponse.
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

#include "SwString.h"
#include "SwMap.h"
#include "SwList.h"
#include "SwByteArray.h"

#include <cstddef>
#include <cstdint>
#include <functional>

class SwTcpSocket;

struct SwHttpLimits {
    std::size_t maxRequestLineBytes = 8 * 1024;
    std::size_t maxHeaderBytes = 32 * 1024;
    std::size_t maxHeaderCount = 100;
    std::size_t maxBodyBytes = 8 * 1024 * 1024;
    std::size_t maxChunkSize = 2 * 1024 * 1024;
    std::size_t maxPipelinedRequests = 16;
    std::size_t maxConnections = 0;
    std::size_t maxInFlightRequests = 0;
    std::size_t maxThreadPoolQueuedDispatches = 0;
    std::size_t maxMultipartParts = 256;
    std::size_t maxMultipartPartHeadersBytes = 16 * 1024;
    std::size_t maxMultipartFieldBytes = 2 * 1024 * 1024;
    bool enableMultipartFileStreaming = false;
    SwString multipartTempDirectory = "http_multipart_tmp";
};

struct SwHttpTimeouts {
    int headerReadTimeoutMs = 10 * 1000;
    int bodyReadTimeoutMs = 30 * 1000;
    int keepAliveIdleTimeoutMs = 15 * 1000;
    int writeTimeoutMs = 30 * 1000;
};

struct SwHttpStaticOptions {
    /**
     * @brief Performs the `SwString` operation.
     * @return The requested sw String.
     */
    SwList<SwString> indexFiles = SwList<SwString>{ SwString("index.html") };
    std::size_t ioChunkBytes = 64 * 1024;
    bool enableRange = true;
    SwString cacheControl;
};

struct SwHttpRequest {
    SwString method;
    SwString target;
    SwString path;
    SwString queryString;
    SwString protocol;
    SwMap<SwString, SwString> headers;
    SwMap<SwString, SwString> queryParams;
    SwMap<SwString, SwString> pathParams;
    SwByteArray body;
    bool keepAlive = true;
    bool isChunkedBody = false;
    bool isMultipartFormData = false;

    struct MultipartPart {
        SwMap<SwString, SwString> headers;
        SwString name;
        SwString fileName;
        SwString contentType;
        SwByteArray data;
        SwString tempFilePath;
        std::size_t sizeBytes = 0;
        bool storedOnDisk = false;
        bool isFile = false;
    };

    SwList<MultipartPart> multipartParts;
    SwMap<SwString, SwString> formFields;
};

struct SwHttpResponse {
    int status = 200;
    /**
     * @brief Performs the `SwString` operation.
     * @return The requested sw String.
     */
    SwString reason = SwString("OK");
    SwMap<SwString, SwString> headers;
    SwByteArray body;

    // Streamed/chunked response support.
    bool useChunkedTransfer = false;
    SwList<SwByteArray> chunkedParts;

    // File response support.
    bool hasFile = false;
    SwString filePath;
    std::size_t fileOffset = 0;
    std::size_t fileLength = 0;
    std::size_t fileTotalSize = 0;
    std::size_t streamChunkBytes = 0;

    // Request method/head behavior.
    bool headOnly = false;

    // Connection policy.
    bool closeConnection = false;

    // Optional raw socket handover (protocol upgrade/tunneling).
    // When set, the HTTP session sends the response headers then transfers
    // socket ownership to the callback instead of closing/keeping HTTP parsing.
    bool switchToRawSocket = false;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(SwTcpSocket*)> onSwitchToRawSocket;
};

inline SwString swHttpStatusReason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 411: return "Length Required";
    case 429: return "Too Many Requests";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 416: return "Range Not Satisfiable";
    case 431: return "Request Header Fields Too Large";
    case 308: return "Permanent Redirect";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "HTTP Response";
    }
}

inline SwHttpResponse swHttpTextResponse(int status, const SwString& bodyText, const SwString& contentType = SwString("text/plain; charset=utf-8")) {
    SwHttpResponse response;
    response.status = status;
    response.reason = swHttpStatusReason(status);
    response.headers["content-type"] = contentType;
    response.body = SwByteArray(bodyText.toStdString());
    return response;
}

inline SwString swHttpToLower(const SwString& value) {
    return value.toLower();
}

inline bool swHttpHeaderContainsToken(const SwString& headerValue, const SwString& tokenLower) {
    SwList<SwString> parts = headerValue.toLower().split(',');
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].trimmed() == tokenLower) {
            return true;
        }
    }
    return false;
}

inline int swHttpHexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

inline bool swHttpParseHexSize(const SwString& text, std::size_t& outValue) {
    outValue = 0;
    SwString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    std::size_t i = 0;
    for (; i < trimmed.size(); ++i) {
        char c = trimmed[i];
        if (c == ';') {
            break;
        }
        int hex = swHttpHexValue(c);
        if (hex < 0) {
            return false;
        }
        std::size_t next = outValue * 16u + static_cast<std::size_t>(hex);
        if (next < outValue) {
            return false;
        }
        outValue = next;
    }
    return true;
}

inline bool swHttpPercentDecode(const SwString& input, SwString& output, bool plusAsSpace) {
    output.clear();
    output.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (plusAsSpace && c == '+') {
            output.append(' ');
            continue;
        }
        if (c == '%') {
            if (i + 2 >= input.size()) {
                return false;
            }
            int hi = swHttpHexValue(input[i + 1]);
            int lo = swHttpHexValue(input[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            output.append(static_cast<char>((hi << 4) | lo));
            i += 2;
            continue;
        }
        output.append(c);
    }
    return true;
}

inline SwString swHttpNormalizePath(const SwString& rawPath) {
    SwString path = rawPath;
    if (path.isEmpty()) {
        return "/";
    }
    if (!path.startsWith("/")) {
        path.prepend("/");
    }
    while (path.contains("//")) {
        path.replace("//", "/");
    }
    if (path.isEmpty()) {
        return "/";
    }
    return path;
}

inline void swHttpParseQueryString(const SwString& query, SwMap<SwString, SwString>& queryParams) {
    queryParams.clear();
    if (query.isEmpty()) {
        return;
    }
    SwList<SwString> pairs = query.split('&');
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        SwString pair = pairs[i];
        if (pair.isEmpty()) {
            continue;
        }
        int eqPos = pair.indexOf("=");
        SwString keyRaw = (eqPos >= 0) ? pair.left(eqPos) : pair;
        SwString valRaw = (eqPos >= 0) ? pair.mid(eqPos + 1) : SwString();

        SwString key;
        SwString value;
        if (!swHttpPercentDecode(keyRaw, key, true)) {
            continue;
        }
        if (!swHttpPercentDecode(valRaw, value, true)) {
            continue;
        }
        queryParams[key] = value;
    }
}

inline SwString swHttpGuessMimeType(const SwString& path) {
    SwString lower = path.toLower();
    if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html; charset=utf-8";
    if (lower.endsWith(".css")) return "text/css; charset=utf-8";
    if (lower.endsWith(".js")) return "application/javascript; charset=utf-8";
    if (lower.endsWith(".json")) return "application/json; charset=utf-8";
    if (lower.endsWith(".txt")) return "text/plain; charset=utf-8";
    if (lower.endsWith(".xml")) return "application/xml; charset=utf-8";
    if (lower.endsWith(".svg")) return "image/svg+xml";
    if (lower.endsWith(".png")) return "image/png";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    if (lower.endsWith(".gif")) return "image/gif";
    if (lower.endsWith(".webp")) return "image/webp";
    if (lower.endsWith(".ico")) return "image/x-icon";
    if (lower.endsWith(".pdf")) return "application/pdf";
    if (lower.endsWith(".zip")) return "application/zip";
    return "application/octet-stream";
}
