#pragma once

/**
 * @file src/core/io/SwHttpClient.h
 * @ingroup core_io
 * @brief Lightweight asynchronous HTTP/HTTPS client with generic method support.
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

#include "SwAbstractSocket.h"
#include "SwByteArray.h"
#include "SwByteRingBuffer.h"
#include "SwDebug.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwPointer.h"
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

static constexpr const char* kSwLogCategory_SwHttpClient = "sw.core.io.swhttpclient";

class SwHttpClient : public SwObject {
    SW_OBJECT(SwHttpClient, SwObject)

public:
    struct Limits {
        std::size_t maxHeaderBytes = 32 * 1024;
        std::size_t maxHeaderCount = 100;
        std::size_t maxRequestHeaderBytes = 32 * 1024;
        std::size_t maxRequestHeaderCount = 100;
        std::size_t maxBodyBytes = 64 * 1024 * 1024;
        std::size_t maxChunkBytes = 8 * 1024 * 1024;
        std::size_t maxRequestBodyBytes = 16 * 1024 * 1024;
        std::size_t maxInterimResponses = 8;
    };

    struct Timeouts {
        int connectTimeoutMs = 10 * 1000;
        int requestWriteInactivityTimeoutMs = 30 * 1000;
        int headerTimeoutMs = 10 * 1000;
        int bodyInactivityTimeoutMs = 30 * 1000;
    };

    using ResponseSink = std::function<bool(const char*, std::size_t)>;

    enum class Method {
        Get,
        Head,
        Post,
        Put,
        Patch,
        Delete,
        Options
    };

    explicit SwHttpClient(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwHttpClient() override {
        abort();
    }

    void setRawHeader(const SwString& key, const SwString& value) {
        const SwString normalizedKey = key.trimmed();
        if (!isValidHeaderName_(normalizedKey) || value.contains("\r") || value.contains("\n")) {
            return;
        }
        if (normalizedKey.toLower() == "transfer-encoding") {
            return;
        }
        m_headerMap[normalizedKey.toLower()] = value;
    }

    void clearRawHeaders() {
        m_headerMap.clear();
    }

    void setTrustedCaFile(const SwString& path) {
        m_trustedCaFile = path;
    }

    void setLimits(const Limits& limits) { m_limits = limits; }
    const Limits& limits() const { return m_limits; }

    void setTimeouts(const Timeouts& timeouts) { m_timeouts = timeouts; }
    const Timeouts& timeouts() const { return m_timeouts; }

    void setResponseSink(const ResponseSink& sink) { m_responseSink = sink; }
    void clearResponseSink() { m_responseSink = ResponseSink(); }

    int statusCode() const {
        return m_lastStatusCode;
    }

    const SwString& reasonPhrase() const {
        return m_lastReasonPhrase;
    }

    const SwByteArray& responseBody() const {
        return m_lastResponseBody;
    }

    SwString responseBodyAsString() const {
        return SwString(m_lastResponseBody);
    }

    const SwString& responseHeaders() const {
        return m_lastResponseHeaders;
    }

    const SwMap<SwString, SwString>& responseHeaderMap() const {
        return m_lastResponseHeaderMap;
    }

    SwString responseHeader(const SwString& key, const SwString& defaultValue = SwString()) const {
        return m_lastResponseHeaderMap.value(key.toLower(), defaultValue);
    }

    bool get(const SwString& url) {
        return request(Method::Get, url);
    }

    bool head(const SwString& url) {
        return request(Method::Head, url);
    }

    bool del(const SwString& url) {
        return request(Method::Delete, url);
    }

    bool options(const SwString& url) {
        return request(Method::Options, url);
    }

    bool post(const SwString& url,
              const SwByteArray& body,
              const SwString& contentType = "application/json") {
        return request(Method::Post, url, body, contentType);
    }

    bool put(const SwString& url,
             const SwByteArray& body,
             const SwString& contentType = "application/json") {
        return request(Method::Put, url, body, contentType);
    }

    bool patch(const SwString& url,
               const SwByteArray& body,
               const SwString& contentType = "application/json") {
        return request(Method::Patch, url, body, contentType);
    }

    bool request(Method method,
                 const SwString& url,
                 const SwByteArray& body = SwByteArray(),
                 const SwString& contentType = SwString()) {
        SwString scheme;
        SwString host;
        uint16_t port = 0;
        SwString path;
        if (!parseUrl_(url, scheme, host, port, path)) {
            emit errorOccurred(-1);
            return false;
        }
        if (m_limits.maxRequestBodyBytes > 0 && body.size() > m_limits.maxRequestBodyBytes) {
            emit errorOccurred(-9);
            return false;
        }

        const SwString normalizedScheme = scheme.toLower();
        const bool canReuse = !m_requestActive && m_idleReusable && m_socket &&
                              m_socket->state() == SwAbstractSocket::ConnectedState &&
                              m_scheme == normalizedScheme && m_host == host && m_port == port;
        if (m_requestActive || !canReuse) {
            cleanupSocket_(!m_requestActive);
        }

        m_method = method;
        ++m_requestGeneration;
        m_scheme = normalizedScheme;
        m_host = host;
        m_port = port;
        m_path = path.isEmpty() ? SwString("/") : path;
        m_https = (m_scheme == "https");
        m_requestBody = body;
        m_requestContentType = contentType;
        m_headRequest = (method == Method::Head);
        m_requestActive = true;
        m_idleReusable = false;

        resetResponseState_();

        if (canReuse) {
            onConnected_();
            return m_requestActive;
        }

        SwSslSocket* sslSocket = m_https ? new SwSslSocket(this) : nullptr;
        m_socket = sslSocket ? static_cast<SwAbstractSocket*>(sslSocket) : new SwTcpSocket(this);
        if (sslSocket) {
            sslSocket->setPeerHostName(m_host);
            // This client speaks HTTP/1.1 only. Advertising that fact is
            // required by shared TLS ingresses that route protocols by ALPN;
            // peers without ALPN support may still negotiate an empty value.
            if (!sslSocket->setApplicationProtocol(SwByteArray("http/1.1"))) {
                cleanupSocket_();
                emit errorOccurred(-2);
                return false;
            }
            if (!m_trustedCaFile.isEmpty()) {
                sslSocket->setTrustedCaFile(m_trustedCaFile);
            }
            SwObject::connect(sslSocket, &SwSslSocket::sslErrors, this, [this](const SwSslErrorList& errors) {
                if (!errors.isEmpty()) {
                    swCError(kSwLogCategory_SwHttpClient) << "[SwHttpClient] TLS error: " << errors.first();
                }
            });
        }

        connect(m_socket, &SwAbstractSocket::errorOccurred, this, &SwHttpClient::onError_);
        connect(m_socket, &SwAbstractSocket::disconnected, this, &SwHttpClient::onDisconnected_);
        connect(m_socket, &SwIODevice::readyRead, this, &SwHttpClient::onReadyRead_);
        connect(m_socket, &SwIODevice::readyWrite, this, &SwHttpClient::pumpRequestWrite_);
        if (sslSocket) {
            connect(sslSocket, &SwSslSocket::encrypted, this, &SwHttpClient::onConnected_);
        } else {
            connect(m_socket, &SwAbstractSocket::connected, this, &SwHttpClient::onConnected_);
        }

        m_connectStartedAt = std::chrono::steady_clock::now();
        const bool connectOk = sslSocket ? sslSocket->connectToHostEncrypted(m_host, m_port)
                                         : m_socket->connectToHost(m_host, m_port);
        if (!connectOk) {
            cleanupSocket_();
            emit errorOccurred(-2);
            return false;
        }
        scheduleTimeout_();
        return true;
    }

    void abort() {
        ++m_requestGeneration;
        stopTimeout_();
        cleanupSocket_();
        resetResponseState_();
        releaseRequestBuffers_();
        m_requestActive = false;
        m_idleReusable = false;
    }

signals:
    DECLARE_SIGNAL(finished, const SwByteArray&)
    DECLARE_SIGNAL(errorOccurred, int)

private slots:
    void onConnected_() {
        if (!m_socket || !m_requestActive) {
            return;
        }

        m_requestHeaders = buildRequestHeaders_();
        if ((m_limits.maxRequestHeaderBytes > 0 &&
             m_requestHeaders.size() > m_limits.maxRequestHeaderBytes) ||
            (m_limits.maxRequestHeaderCount > 0 &&
             static_cast<std::size_t>(m_headerMap.size()) + 5 >
                 m_limits.maxRequestHeaderCount)) {
            failRequest_(-15);
            return;
        }
        m_requestHeaderOffset = 0;
        m_requestBodyOffset = 0;
        m_requestSent = false;
        m_lastWriteProgressAt = std::chrono::steady_clock::now();
        scheduleTimeout_();
        pumpRequestWrite_();
    }

    void pumpRequestWrite_() {
        if (!m_socket || !m_requestActive || m_requestSent || m_finishedEmitted ||
            m_socket->state() != SwAbstractSocket::ConnectedState) {
            return;
        }
        if (m_requestWritePumping) {
            m_requestWriteAgain = true;
            return;
        }
        m_requestWritePumping = true;
        m_requestWriteAgain = false;

        static const std::size_t kWriteChunkBytes = 64 * 1024;
        while (m_requestHeaderOffset < m_requestHeaders.size() ||
               m_requestBodyOffset < m_requestBody.size()) {
            const char* data = nullptr;
            std::size_t remaining = 0;
            if (m_requestHeaderOffset < m_requestHeaders.size()) {
                data = m_requestHeaders.data() + m_requestHeaderOffset;
                remaining = m_requestHeaders.size() - m_requestHeaderOffset;
            } else {
                data = m_requestBody.constData() + m_requestBodyOffset;
                remaining = m_requestBody.size() - m_requestBodyOffset;
            }

            const std::size_t chunk = (std::min)(remaining, kWriteChunkBytes);
            if (!m_socket->write(data, chunk)) {
                SwTcpSocket* tcp = static_cast<SwTcpSocket*>(m_socket);
                if (tcp->lastWriteResult() == SwTcpSocket::WriteResult::WouldBlock) {
                    m_requestWritePumping = false;
                    scheduleTimeout_();
                    if (m_requestWriteAgain) {
                        m_requestWriteAgain = false;
                        pumpRequestWrite_();
                    }
                    return;
                }
                m_requestWritePumping = false;
                failRequest_(-3);
                return;
            }

            if (m_requestHeaderOffset < m_requestHeaders.size()) {
                m_requestHeaderOffset += chunk;
            } else {
                m_requestBodyOffset += chunk;
            }
            m_lastWriteProgressAt = std::chrono::steady_clock::now();
        }

        m_requestSent = true;
        m_requestHeaders.clear();
        m_requestBody = SwByteArray();
        m_headerStartedAt = std::chrono::steady_clock::now();
        m_lastBodyProgressAt = m_headerStartedAt;
        m_requestWritePumping = false;
        m_requestWriteAgain = false;
        scheduleTimeout_();
    }

    void onReadyRead_() {
        if (!m_socket) {
            return;
        }

        char readBuffer[kSwTcpDefaultReadChunkSize];
        const std::uint64_t generation = m_requestGeneration;
        SwPointer<SwHttpClient> self(this);
        std::size_t byteBudget = 256 * 1024;
        std::size_t syscalls = 0;
        while (byteBudget > 0 && syscalls < 32) {
            const std::size_t requested = (std::min)(sizeof(readBuffer), byteBudget);
            ++syscalls;
            const int64_t bytesRead = m_socket->readInto(readBuffer, requested);
            if (bytesRead <= 0) {
                break;
            }
            m_buffer.append(readBuffer, static_cast<size_t>(bytesRead));
            byteBudget -= static_cast<std::size_t>(bytesRead);
            m_lastBodyProgressAt = std::chrono::steady_clock::now();
            processBuffer_();
            if (!self || m_requestGeneration != generation || !m_requestActive) {
                return;
            }
        }
        if (self && m_requestGeneration == generation && m_requestActive) {
            scheduleTimeout_();
        }
    }

    void onError_(int err) {
        failRequest_(err);
    }

    void onDisconnected_() {
        if (m_idleReusable || !m_requestActive) {
            cleanupSocket_();
            m_idleReusable = false;
            return;
        }
        if (m_finishedEmitted) {
            return;
        }
        if (!m_headersReceived) {
            stopTimeout_();
            cleanupSocket_();
            m_requestActive = false;
            releaseRequestBuffers_();
            emit errorOccurred(-10);
            return;
        }

        if (!m_chunked && m_contentLength < 0) {
            if (!m_buffer.isEmpty()) {
                if (!consumeBodyBytes_(m_buffer.size())) {
                    return;
                }
            }
            finishRequest_();
            return;
        }
        stopTimeout_();
        cleanupSocket_();
        m_requestActive = false;
        releaseRequestBuffers_();
        emit errorOccurred(-10);
    }

private:
    void stopTimeout_() {
        if (m_timeoutTimer && m_timeoutTimer->isActive()) {
            m_timeoutTimer->stop();
        }
    }

    void scheduleTimeout_() {
        if (!m_requestActive || !m_socket) {
            stopTimeout_();
            return;
        }

        int timeoutMs = 0;
        std::chrono::steady_clock::time_point base;
        if (m_socket->state() == SwAbstractSocket::HostLookupState ||
            m_socket->state() == SwAbstractSocket::ConnectingState) {
            timeoutMs = m_timeouts.connectTimeoutMs;
            base = m_connectStartedAt;
        } else if (!m_requestSent) {
            timeoutMs = m_timeouts.requestWriteInactivityTimeoutMs;
            base = m_lastWriteProgressAt;
        } else if (!m_headersReceived) {
            timeoutMs = m_timeouts.headerTimeoutMs;
            base = m_headerStartedAt;
        } else {
            timeoutMs = m_timeouts.bodyInactivityTimeoutMs;
            base = m_lastBodyProgressAt;
        }
        if (timeoutMs <= 0) {
            stopTimeout_();
            return;
        }

        if (!m_timeoutTimer) {
            m_timeoutTimer = new SwTimer(this);
            m_timeoutTimer->setSingleShot(true);
            connect(m_timeoutTimer, &SwTimer::timeout, this, [this]() {
                failRequest_(-8);
            });
        }
        stopTimeout_();
        const auto deadline = base + std::chrono::milliseconds(timeoutMs);
        const auto now = std::chrono::steady_clock::now();
        const auto remainingUs = deadline > now
                                     ? std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count()
                                     : 0;
        long long delayMs = (remainingUs + 999) / 1000;
        if (delayMs < 1) {
            delayMs = 1;
        }
        if (delayMs > (std::numeric_limits<int>::max)()) {
            delayMs = (std::numeric_limits<int>::max)();
        }
        m_timeoutTimer->start(static_cast<int>(delayMs));
    }

    static bool isValidHeaderName_(const SwString& name) {
        if (name.isEmpty()) {
            return false;
        }
        for (std::size_t i = 0; i < name.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            const bool alphaNumeric = (c >= '0' && c <= '9') ||
                                      (c >= 'A' && c <= 'Z') ||
                                      (c >= 'a' && c <= 'z');
            if (!alphaNumeric && c != '!' && c != '#' && c != '$' && c != '%' &&
                c != '&' && c != '\'' && c != '*' && c != '+' && c != '-' &&
                c != '.' && c != '^' && c != '_' && c != '`' && c != '|' && c != '~') {
                return false;
            }
        }
        return true;
    }

    static bool isValidHeaderValue_(const SwString& value) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if ((c < 0x20u && c != '\t') || c == 0x7fu) {
                return false;
            }
        }
        return true;
    }

    static bool headerContainsToken_(const SwString& value, const SwString& token) {
        const SwList<SwString> parts = value.split(',');
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].trimmed().toLower() == token.toLower()) {
                return true;
            }
        }
        return false;
    }

    static bool parseChunkSize_(const SwByteArray& text, long long& out) {
        if (text.isEmpty()) {
            return false;
        }
        unsigned long long value = 0;
        const unsigned long long maximum =
            static_cast<unsigned long long>((std::numeric_limits<long long>::max)());
        for (std::size_t i = 0; i < text.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            unsigned int digit = 0;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = 10u + c - 'a';
            } else if (c >= 'A' && c <= 'F') {
                digit = 10u + c - 'A';
            } else {
                return false;
            }
            if (value > (maximum - digit) / 16u) {
                return false;
            }
            value = value * 16u + digit;
        }
        out = static_cast<long long>(value);
        return true;
    }

    static SwString methodToString_(Method method) {
        switch (method) {
        case Method::Get: return "GET";
        case Method::Head: return "HEAD";
        case Method::Post: return "POST";
        case Method::Put: return "PUT";
        case Method::Patch: return "PATCH";
        case Method::Delete: return "DELETE";
        case Method::Options: return "OPTIONS";
        default: return "GET";
        }
    }

    SwString buildRequestHeaders_() const {
        SwString requestText;
        bool hasConnection = false;
        bool hasAccept = false;
        bool hasUserAgent = false;
        bool hasContentType = false;
        bool hasContentLength = false;
        bool hasHost = false;
        for (auto it = m_headerMap.begin(); it != m_headerMap.end(); ++it) {
            const SwString key = it.key();
            const SwString lower = key.toLower();
            if (lower == "host") {
                hasHost = true;
            } else if (lower == "connection") {
                hasConnection = true;
            } else if (lower == "accept") {
                hasAccept = true;
            } else if (lower == "user-agent") {
                hasUserAgent = true;
            } else if (lower == "content-type") {
                hasContentType = true;
            } else if (lower == "content-length") {
                // Framing is generated from the owned request body below.
                hasContentLength = false;
            }
        }

        requestText += methodToString_(m_method);
        requestText += " ";
        requestText += (m_path.isEmpty() ? SwString("/") : m_path);
        requestText += " HTTP/1.1\r\n";
        if (!hasHost) {
            requestText += "Host: " + hostHeaderValue_() + "\r\n";
        }

        for (auto it = m_headerMap.begin(); it != m_headerMap.end(); ++it) {
            const SwString key = it.key();
            const SwString lower = key.toLower();
            if (lower == "content-length" || lower == "transfer-encoding") {
                continue;
            }
            requestText += key + ": " + it.value() + "\r\n";
        }

        if (!hasConnection) {
            requestText += "Connection: keep-alive\r\n";
        }
        if (!hasAccept) {
            requestText += "Accept: */*\r\n";
        }
        if (!hasUserAgent) {
            requestText += "User-Agent: SwHttpClient/1.0\r\n";
        }

        const bool sendsBody =
            (m_method == Method::Post || m_method == Method::Put || m_method == Method::Patch);
        if (sendsBody || !m_requestBody.isEmpty()) {
            if (!hasContentType) {
                const SwString effectiveContentType =
                    m_requestContentType.isEmpty() ? SwString("application/octet-stream") : m_requestContentType;
                requestText += "Content-Type: " + effectiveContentType + "\r\n";
            }
            if (!hasContentLength) {
                requestText += "Content-Length: " + SwString::number(static_cast<long long>(m_requestBody.size())) + "\r\n";
            }
        }

        requestText += "\r\n";
        return requestText;
    }

    SwString hostHeaderValue_() const {
        const bool defaultPort = (!m_https && m_port == 80) || (m_https && m_port == 443);
        const SwString formattedHost = m_host.contains(":") ? ("[" + m_host + "]") : m_host;
        if (defaultPort || m_port == 0) {
            return formattedHost;
        }
        return formattedHost + ":" + SwString::number(static_cast<int>(m_port));
    }

    void resetResponseState_() {
        m_buffer.clear();
        m_responseBody = SwByteArray();
        m_responseHeaders.clear();
        m_responseHeaderMap.clear();
        m_headersReceived = false;
        m_finishedEmitted = false;
        m_contentLength = -1;
        m_bytesReceived = 0;
        m_chunked = false;
        m_chunkBytesRemaining = -1;
        m_readingTrailers = false;
        m_connectionClose = false;
        m_seenContentLength = false;
        m_invalidHeaders = false;
        m_interimResponseCount = 0;
        m_statusCode = 0;
        m_reasonPhrase.clear();

        m_lastStatusCode = 0;
        m_lastReasonPhrase.clear();
        m_lastResponseBody = SwByteArray();
        m_lastResponseHeaders.clear();
        m_lastResponseHeaderMap.clear();

        m_requestHeaders.clear();
        m_requestHeaderOffset = 0;
        m_requestBodyOffset = 0;
        m_requestSent = false;
        m_requestWritePumping = false;
        m_requestWriteAgain = false;
    }

    static void disposeSocket_(SwAbstractSocket* socket, bool graceful) {
        if (!socket) {
            return;
        }
        socket->disconnectAllSlots();
        socket->setParent(nullptr);
        if (!graceful) {
            if (SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(socket)) {
                tcp->abort();
            } else {
                socket->close();
            }
            if (!socket->deleteLater()) {
                delete socket;
            }
            return;
        }

        SwPointer<SwAbstractSocket> guard(socket);
        std::shared_ptr<std::atomic<bool>> deletionScheduled(
            new std::atomic<bool>(false));
        SwObject::connect(socket, &SwAbstractSocket::disconnected, socket,
                          [guard, deletionScheduled]() mutable {
            if (!guard || deletionScheduled->exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            SwAbstractSocket* owned = guard.data();
            if (!owned->deleteLater()) {
                delete owned;
            }
        });
        SwTimer* deadline = new SwTimer(6000, socket);
        deadline->setSingleShot(true);
        SwObject::connect(deadline, &SwTimer::timeout, socket,
                          [guard, deletionScheduled]() mutable {
            if (!guard) {
                return;
            }
            SwAbstractSocket* owned = guard.data();
            if (SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(owned)) {
                tcp->abort();
            } else {
                owned->close();
            }
            if (guard && !deletionScheduled->exchange(true, std::memory_order_acq_rel)) {
                owned = guard.data();
                if (!owned->deleteLater()) {
                    delete owned;
                }
            }
        });
        socket->close();
        if (!guard) {
            return;
        }
        if (guard->state() == SwAbstractSocket::UnconnectedState) {
            if (!deletionScheduled->exchange(true, std::memory_order_acq_rel)) {
                SwAbstractSocket* owned = guard.data();
                if (!owned->deleteLater()) {
                    delete owned;
                }
            }
        } else {
            deadline->start();
        }
    }

    void cleanupSocket_(bool graceful = false) {
        SwAbstractSocket* socket = m_socket;
        m_socket = nullptr;
        disposeSocket_(socket, graceful);
    }

    void failRequest_(int error) {
        stopTimeout_();
        cleanupSocket_();
        m_requestActive = false;
        m_idleReusable = false;
        releaseRequestBuffers_();
        emit errorOccurred(error);
    }

    void releaseRequestBuffers_() {
        m_requestHeaders.clear();
        m_requestBody = SwByteArray();
        m_requestHeaderOffset = 0;
        m_requestBodyOffset = 0;
        m_requestSent = false;
        m_requestWritePumping = false;
        m_requestWriteAgain = false;
    }

    bool consumeBodyBytes_(std::size_t bytes) {
        const std::size_t received = static_cast<std::size_t>(m_bytesReceived);
        if (m_limits.maxBodyBytes > 0 &&
            (received > m_limits.maxBodyBytes ||
             bytes > m_limits.maxBodyBytes - received)) {
            failRequest_(-11);
            return false;
        }

        std::size_t remaining = bytes;
        while (remaining > 0) {
            const std::size_t contiguous = (std::min)(remaining, m_buffer.contiguousSize());
            const char* data = m_buffer.contiguousData();
            if (!data || contiguous == 0) {
                return false;
            }
            if (m_responseSink) {
                ResponseSink sink = m_responseSink;
                SwPointer<SwHttpClient> self(this);
                const std::uint64_t generation = m_requestGeneration;
                if (!sink(data, contiguous)) {
                    if (self && m_requestGeneration == generation) {
                        self->failRequest_(-12);
                    }
                    return false;
                }
                if (!self || m_requestGeneration != generation || !m_requestActive) {
                    return false;
                }
            } else {
                m_responseBody.append(data, contiguous);
            }
            m_buffer.consume(contiguous);
            m_bytesReceived += static_cast<long long>(contiguous);
            remaining -= contiguous;
        }
        return true;
    }

    void finishRequest_() {
        if (m_finishedEmitted) {
            return;
        }
        m_finishedEmitted = true;
        stopTimeout_();

        m_lastStatusCode = m_statusCode;
        m_lastReasonPhrase = std::move(m_reasonPhrase);
        m_lastResponseHeaders = std::move(m_responseHeaders);
        m_lastResponseHeaderMap = std::move(m_responseHeaderMap);
        m_lastResponseBody = std::move(m_responseBody);

        m_requestActive = false;
        m_idleReusable = !m_connectionClose && m_socket &&
                         m_socket->state() == SwAbstractSocket::ConnectedState;
        releaseRequestBuffers_();
        if (!m_idleReusable) {
            cleanupSocket_(true);
        }
        emit finished(m_lastResponseBody);
    }

    void processBuffer_() {
        while (!m_headersReceived) {
            const int boundary = m_buffer.indexOf("\r\n\r\n");
            if (boundary == SwByteArray::npos) {
                if (m_limits.maxHeaderBytes > 0 && m_buffer.size() > m_limits.maxHeaderBytes) {
                    failRequest_(-13);
                }
                return;
            }

            const int headerBytes = boundary + 4;
            if (m_limits.maxHeaderBytes > 0 &&
                static_cast<std::size_t>(headerBytes) > m_limits.maxHeaderBytes) {
                failRequest_(-13);
                return;
            }
            const SwByteArray headersPart = m_buffer.left(headerBytes);
            m_buffer.consume(static_cast<size_t>(headerBytes));
            m_responseHeaders = SwString(headersPart);
            parseHeaders_(m_responseHeaders);
            if (m_invalidHeaders || m_statusCode <= 0 ||
                (m_contentLength >= 0 && m_limits.maxBodyBytes > 0 &&
                 static_cast<unsigned long long>(m_contentLength) > m_limits.maxBodyBytes)) {
                failRequest_(-14);
                return;
            }

            if (m_statusCode >= 100 && m_statusCode < 200) {
                if (m_statusCode == 101) {
                    failRequest_(-16);
                    return;
                }
                if (m_chunked || m_contentLength > 0 ||
                    (m_limits.maxInterimResponses > 0 &&
                     ++m_interimResponseCount > m_limits.maxInterimResponses)) {
                    failRequest_(-14);
                    return;
                }
                resetInterimResponseState_();
                continue;
            }
            m_headersReceived = true;
            if (!m_requestSent) {
                // An early final response (for example 413/417) means the remaining request
                // bytes must not leak into a subsequent keep-alive exchange.
                m_connectionClose = true;
            }

            const bool responseHasNoBody = m_headRequest || m_statusCode == 204 ||
                                           m_statusCode == 304 || m_contentLength == 0;
            if (responseHasNoBody) {
                if (!m_buffer.isEmpty()) {
                    m_connectionClose = true;
                }
                finishRequest_();
                return;
            }
        }

        if (!m_headersReceived || m_finishedEmitted) {
            return;
        }

        if (m_chunked) {
            processChunked_();
            return;
        }

        if (m_contentLength >= 0) {
            while (!m_buffer.isEmpty() && m_bytesReceived < m_contentLength) {
                const long long remaining = m_contentLength - m_bytesReceived;
                const long long take =
                    (remaining < static_cast<long long>(m_buffer.size()))
                        ? remaining
                        : static_cast<long long>(m_buffer.size());
                if (take <= 0) {
                    break;
                }
                if (!consumeBodyBytes_(static_cast<size_t>(take))) {
                    return;
                }
            }
            if (m_bytesReceived >= m_contentLength) {
                finishRequest_();
            }
            return;
        }

        if (!m_buffer.isEmpty()) {
            const size_t buffered = m_buffer.size();
            consumeBodyBytes_(buffered);
        }
    }

    void resetInterimResponseState_() {
        m_responseHeaders.clear();
        m_responseHeaderMap.clear();
        m_contentLength = -1;
        m_chunked = false;
        m_chunkBytesRemaining = -1;
        m_readingTrailers = false;
        m_connectionClose = false;
        m_seenContentLength = false;
        m_invalidHeaders = false;
        m_statusCode = 0;
        m_reasonPhrase.clear();
        m_headerStartedAt = std::chrono::steady_clock::now();
    }

    void processChunked_() {
        while (true) {
            if (m_readingTrailers) {
                if (m_buffer.size() >= 2 && m_buffer[0] == '\r' && m_buffer[1] == '\n') {
                    m_buffer.consume(2);
                    finishRequest_();
                    return;
                }
                const int trailersEnd = m_buffer.indexOf("\r\n\r\n");
                if (trailersEnd == SwByteArray::npos) {
                    if (m_limits.maxHeaderBytes > 0 && m_buffer.size() > m_limits.maxHeaderBytes) {
                        failRequest_(-13);
                    }
                    return;
                }
                if (m_limits.maxHeaderBytes > 0 &&
                    static_cast<std::size_t>(trailersEnd + 4) > m_limits.maxHeaderBytes) {
                    failRequest_(-13);
                    return;
                }
                const SwString trailers(m_buffer.left(trailersEnd));
                const SwList<SwString> trailerLines = trailers.split("\r\n");
                std::size_t trailerCount = 0;
                for (std::size_t i = 0; i < trailerLines.size(); ++i) {
                    if (trailerLines[i].isEmpty()) {
                        continue;
                    }
                    const int colon = trailerLines[i].indexOf(':');
                    if (colon <= 0) {
                        failRequest_(-14);
                        return;
                    }
                    const SwString rawName = trailerLines[i].left(colon);
                    const SwString name = rawName.toLower();
                    const SwString value = trailerLines[i].mid(colon + 1).trimmed();
                    if (rawName != rawName.trimmed() || !isValidHeaderName_(name) ||
                        !isValidHeaderValue_(value) || name == "content-length" ||
                        name == "transfer-encoding" || name == "host" ||
                        (m_limits.maxHeaderCount > 0 &&
                         ++trailerCount > m_limits.maxHeaderCount)) {
                        failRequest_(-14);
                        return;
                    }
                }
                m_buffer.consume(static_cast<std::size_t>(trailersEnd + 4));
                finishRequest_();
                return;
            }

            if (m_chunkBytesRemaining < 0) {
                const int lineEnd = m_buffer.indexOf("\r\n");
                if (lineEnd == SwByteArray::npos) {
                    if (m_buffer.size() > 128) {
                        failRequest_(-4);
                    }
                    return;
                }
                if (lineEnd > 128) {
                    failRequest_(-4);
                    return;
                }

                SwByteArray line = m_buffer.left(lineEnd).trimmed();
                m_buffer.consume(static_cast<size_t>(lineEnd + 2));
                const int semi = line.indexOf(';');
                if (semi != SwByteArray::npos) {
                    line = line.left(semi).trimmed();
                }

                long long sz = 0;
                if (!parseChunkSize_(line, sz)) {
                    swCError(kSwLogCategory_SwHttpClient) << "[SwHttpClient] invalid chunk size: " << SwString(line);
                    failRequest_(-4);
                    return;
                }

                if (sz == 0) {
                    m_readingTrailers = true;
                    continue;
                }

                if ((m_limits.maxChunkBytes > 0 &&
                     static_cast<unsigned long long>(sz) > m_limits.maxChunkBytes) ||
                    (m_limits.maxBodyBytes > 0 &&
                     static_cast<unsigned long long>(m_bytesReceived) +
                         static_cast<unsigned long long>(sz) > m_limits.maxBodyBytes)) {
                    failRequest_(-11);
                    return;
                }

                m_chunkBytesRemaining = static_cast<long long>(sz);
            }

            if (m_chunkBytesRemaining < 0) {
                return;
            }
            if (static_cast<long long>(m_buffer.size()) < (m_chunkBytesRemaining + 2)) {
                return;
            }

            if (!consumeBodyBytes_(static_cast<size_t>(m_chunkBytesRemaining))) {
                return;
            }

            if (m_buffer.size() < 2 || m_buffer[0] != '\r' || m_buffer[1] != '\n') {
                swCError(kSwLogCategory_SwHttpClient) << "[SwHttpClient] missing CRLF after chunk";
                failRequest_(-5);
                return;
            }

            m_buffer.consume(2);
            m_chunkBytesRemaining = -1;
        }
    }

    void parseHeaders_(const SwString& headersText) {
        const SwList<SwString> lines = headersText.split("\r\n");
        bool firstLine = true;
        std::size_t headerCount = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const SwString line = lines[i];
            if (line.isEmpty()) {
                continue;
            }

            if (firstLine) {
                firstLine = false;
                const int sp1 = line.indexOf(' ');
                const int sp2 = (sp1 >= 0) ? line.indexOf(' ', sp1 + 1) : -1;
                if (sp1 >= 0) {
                    const SwString protocol = line.left(sp1).trimmed().toUpper();
                    if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") {
                        m_invalidHeaders = true;
                        return;
                    }
                    m_connectionClose = (protocol == "HTTP/1.0");
                    const SwString codeText =
                        (sp2 >= 0) ? line.mid(sp1 + 1, sp2 - (sp1 + 1)) : line.mid(sp1 + 1);
                    bool ok = false;
                    m_statusCode = codeText.toInt(&ok);
                    if (!ok || codeText.size() != 3 || m_statusCode < 100 || m_statusCode > 599) {
                        m_statusCode = 0;
                        m_invalidHeaders = true;
                        return;
                    }
                    if (sp2 >= 0) {
                        m_reasonPhrase = line.mid(sp2 + 1).trimmed();
                        if (!isValidHeaderValue_(m_reasonPhrase)) {
                            m_invalidHeaders = true;
                            return;
                        }
                    }
                } else {
                    m_invalidHeaders = true;
                    return;
                }
                continue;
            }

            const int colon = line.indexOf(':');
            if (colon <= 0) {
                m_invalidHeaders = true;
                return;
            }

            const SwString rawKey = line.left(colon);
            if (rawKey != rawKey.trimmed()) {
                m_invalidHeaders = true;
                return;
            }
            const SwString key = rawKey.toLower();
            const SwString value = line.mid(colon + 1).trimmed();
            if (!isValidHeaderName_(key) || !isValidHeaderValue_(value)) {
                m_invalidHeaders = true;
                return;
            }
            ++headerCount;
            if (m_limits.maxHeaderCount > 0 && headerCount > m_limits.maxHeaderCount) {
                m_invalidHeaders = true;
                return;
            }
            if (m_responseHeaderMap.contains(key)) {
                if (key == "content-length" || key == "transfer-encoding") {
                    m_invalidHeaders = true;
                    return;
                }
                m_responseHeaderMap[key] += ", " + value;
            } else {
                m_responseHeaderMap[key] = value;
            }

            if (key == "content-length") {
                if (value.isEmpty()) {
                    m_invalidHeaders = true;
                    return;
                }
                for (std::size_t j = 0; j < value.size(); ++j) {
                    if (value[j] < '0' || value[j] > '9') {
                        m_invalidHeaders = true;
                        return;
                    }
                }
                bool ok = false;
                const long long length = value.toLongLong(&ok);
                if (!ok || length < 0 || (m_seenContentLength && length != m_contentLength)) {
                    m_invalidHeaders = true;
                    return;
                }
                m_seenContentLength = true;
                m_contentLength = length;
            } else if (key == "transfer-encoding") {
                if (value.trimmed().toLower() != "chunked") {
                    m_invalidHeaders = true;
                    return;
                }
                m_chunked = true;
            } else if (key == "connection") {
                const SwString lowerValue = value.toLower();
                if (headerContainsToken_(lowerValue, "close")) {
                    m_connectionClose = true;
                } else if (headerContainsToken_(lowerValue, "keep-alive")) {
                    m_connectionClose = false;
                }
            }
        }
        if (m_chunked && m_seenContentLength) {
            m_invalidHeaders = true;
        }
    }

    static bool parseUrl_(const SwString& url,
                          SwString& scheme,
                          SwString& host,
                          uint16_t& port,
                          SwString& path) {
        const SwString lower = url.toLower();
        int offset = -1;
        if (lower.startsWith("http://")) {
            scheme = "http";
            offset = 7;
            port = 80;
        } else if (lower.startsWith("https://")) {
            scheme = "https";
            offset = 8;
            port = 443;
        } else {
            return false;
        }

        const SwString remainder = url.mid(offset);
        int authorityEnd = static_cast<int>(remainder.size());
        const int slashPos = remainder.indexOf('/');
        const int queryPos = remainder.indexOf('?');
        const int fragmentPos = remainder.indexOf('#');
        if (slashPos >= 0) authorityEnd = (std::min)(authorityEnd, slashPos);
        if (queryPos >= 0) authorityEnd = (std::min)(authorityEnd, queryPos);
        if (fragmentPos >= 0) authorityEnd = (std::min)(authorityEnd, fragmentPos);

        const SwString authority = remainder.left(authorityEnd);
        if (authority.isEmpty() || authority.contains("@")) {
            return false;
        }

        SwString portText;
        if (authority.startsWith("[")) {
            const int closingBracket = authority.indexOf(']');
            if (closingBracket <= 1) {
                return false;
            }
            host = authority.mid(1, closingBracket - 1);
            const SwString suffix = authority.mid(closingBracket + 1);
            if (!suffix.isEmpty()) {
                if (!suffix.startsWith(":") || suffix.size() <= 1) {
                    return false;
                }
                portText = suffix.mid(1);
            }
        } else {
            const int colonPos = authority.indexOf(':');
            if (colonPos >= 0) {
                if (authority.indexOf(':', colonPos + 1) >= 0) {
                    return false; // IPv6 literals must use brackets in a URI authority.
                }
                host = authority.left(colonPos);
                portText = authority.mid(colonPos + 1);
            } else {
                host = authority;
            }
        }

        if (!portText.isEmpty()) {
            for (std::size_t i = 0; i < portText.size(); ++i) {
                if (portText[i] < '0' || portText[i] > '9') {
                    return false;
                }
            }
            bool ok = false;
            const int parsedPort = portText.toInt(&ok);
            if (!ok || parsedPort <= 0 || parsedPort > 65535) {
                return false;
            }
            port = static_cast<uint16_t>(parsedPort);
        }

        path = remainder.mid(authorityEnd);
        const int pathFragment = path.indexOf('#');
        if (pathFragment >= 0) {
            path = path.left(pathFragment);
        }
        if (path.startsWith("?")) {
            path.prepend("/");
        }
        if (path.isEmpty()) {
            path = "/";
        }
        if (!path.startsWith("/")) {
            return false;
        }
        for (std::size_t i = 0; i < host.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(host[i]);
            if (c <= 0x20 || c == 0x7f || c == '\\' || c == '/' ||
                c == '?' || c == '#') {
                return false;
            }
        }
        for (std::size_t i = 0; i < path.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(path[i]);
            if (c <= 0x20 || c == 0x7f || c == '\\') {
                return false;
            }
        }
        return !host.isEmpty();
    }

    SwAbstractSocket* m_socket = nullptr;
    SwString m_trustedCaFile;
    Limits m_limits;
    Timeouts m_timeouts;
    ResponseSink m_responseSink;
    SwTimer* m_timeoutTimer = nullptr;
    std::chrono::steady_clock::time_point m_connectStartedAt{};
    std::chrono::steady_clock::time_point m_lastWriteProgressAt{};
    std::chrono::steady_clock::time_point m_headerStartedAt{};
    std::chrono::steady_clock::time_point m_lastBodyProgressAt{};

    Method m_method = Method::Get;
    SwString m_scheme;
    SwString m_host;
    SwString m_path;
    uint16_t m_port = 0;
    bool m_https = false;
    bool m_headRequest = false;
    bool m_requestActive = false;
    bool m_idleReusable = false;
    std::uint64_t m_requestGeneration = 0;

    SwMap<SwString, SwString> m_headerMap;

    SwString m_requestHeaders;
    SwByteArray m_requestBody;
    SwString m_requestContentType;
    std::size_t m_requestHeaderOffset = 0;
    std::size_t m_requestBodyOffset = 0;
    bool m_requestSent = false;
    bool m_requestWritePumping = false;
    bool m_requestWriteAgain = false;

    SwByteRingBuffer m_buffer;
    SwByteArray m_responseBody;
    SwString m_responseHeaders;
    SwMap<SwString, SwString> m_responseHeaderMap;
    bool m_headersReceived = false;
    bool m_finishedEmitted = false;

    long long m_contentLength = -1;
    long long m_bytesReceived = 0;
    bool m_chunked = false;
    long long m_chunkBytesRemaining = -1;
    bool m_readingTrailers = false;
    bool m_connectionClose = false;
    bool m_seenContentLength = false;
    bool m_invalidHeaders = false;
    std::size_t m_interimResponseCount = 0;

    int m_statusCode = 0;
    SwString m_reasonPhrase;

    int m_lastStatusCode = 0;
    SwString m_lastReasonPhrase;
    SwByteArray m_lastResponseBody;
    SwString m_lastResponseHeaders;
    SwMap<SwString, SwString> m_lastResponseHeaderMap;
};
