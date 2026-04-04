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

#pragma once

/**
 * @file src/fireBD/FireBDHttpClient.h
 * @ingroup firebd
 * @brief Declares the public interface exposed by FireBDHttpClient in the FireBD service layer.
 *
 * This header belongs to the FireBD service layer. It declares application-facing clients,
 * service types, and data models used to communicate with the FireBD backend.
 *
 * Within that layer, this file focuses on the fire bd HTTP client interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are FireBDHttpClient.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * The contracts in this area mainly describe request and response shapes, client composition, and
 * higher-level service boundaries.
 *
 */


/***************************************************************************************************
 * fireBD - Minimal async HTTP/HTTPS client (GET/PUT/PATCH/POST/DELETE).
 *
 * Rationale:
 * - SwNetworkAccessManager currently provides GET only.
 * - Firebase RTDB REST API requires PUT/PATCH/DELETE for queue-like semantics.
 **************************************************************************************************/

#include "SwByteArray.h"
#include "SwAbstractSocket.h"
#include "SwDebug.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTcpSocket.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

static constexpr const char* kSwLogCategory_FireBDHttpClient = "sw.firebd.http";

class FireBDHttpClient : public SwObject {
    SW_OBJECT(FireBDHttpClient, SwObject)

public:
    enum class Method {
        Get,
        Post,
        Put,
        Patch,
        Delete
    };

    /**
     * @brief Constructs a `FireBDHttpClient` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit FireBDHttpClient(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Destroys the `FireBDHttpClient` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~FireBDHttpClient() override { abort(); }

    /**
     * @brief Sets the raw Header.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRawHeader(const SwString& key, const SwString& value) {
        m_headerMap[key] = value;
    }

    void setTrustedCaFile(const SwString& path) {
        m_trustedCaFile = path;
    }

    /**
     * @brief Returns the current status Code.
     * @return The current status Code.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int statusCode() const { return m_lastStatusCode; }
    /**
     * @brief Returns the current reason Phrase.
     * @return The current reason Phrase.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& reasonPhrase() const { return m_lastReasonPhrase; }

    /**
     * @brief Returns the current response Body.
     * @return The current response Body.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwByteArray& responseBody() const { return m_lastResponseBody; }
    /**
     * @brief Performs the `responseBodyAsString` operation.
     * @param m_lastResponseBody Value passed to the method.
     * @return The requested response Body As String.
     */
    SwString responseBodyAsString() const { return SwString(m_lastResponseBody); }

    /**
     * @brief Returns the current response Headers.
     * @return The current response Headers.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& responseHeaders() const { return m_lastResponseHeaders; }

    /**
     * @brief Performs the `get` operation.
     * @param url Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool get(const SwString& url) { return request(Method::Get, url); }
    /**
     * @brief Performs the `del` operation.
     * @param url Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool del(const SwString& url) { return request(Method::Delete, url); }

    /**
     * @brief Performs the `post` operation.
     * @param url Value passed to the method.
     * @param body Value passed to the method.
     * @param contentType Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool post(const SwString& url, const SwByteArray& body, const SwString& contentType = "application/json") {
        return request(Method::Post, url, body, contentType);
    }

    /**
     * @brief Performs the `put` operation.
     * @param url Value passed to the method.
     * @param body Value passed to the method.
     * @param contentType Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool put(const SwString& url, const SwByteArray& body, const SwString& contentType = "application/json") {
        return request(Method::Put, url, body, contentType);
    }

    /**
     * @brief Performs the `patch` operation.
     * @param url Value passed to the method.
     * @param body Value passed to the method.
     * @param contentType Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool patch(const SwString& url, const SwByteArray& body, const SwString& contentType = "application/json") {
        return request(Method::Patch, url, body, contentType);
    }

    /**
     * @brief Performs the `request` operation.
     * @param method HTTP method involved in the operation.
     * @param url Value passed to the method.
     * @param body Value passed to the method.
     * @param contentType Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

        abort();

        m_method = method;
        m_scheme = scheme.toLower();
        m_host = host;
        m_port = port;
        m_path = path.isEmpty() ? SwString("/") : path;
        m_https = (m_scheme == "https");
        m_requestBody = body;
        m_requestContentType = contentType;

        resetResponseState_();

        SwSslSocket* sslSocket = m_https ? new SwSslSocket(this) : nullptr;
        m_socket = sslSocket ? static_cast<SwAbstractSocket*>(sslSocket) : new SwTcpSocket(this);
        if (sslSocket) {
            sslSocket->setPeerHostName(m_host);
            if (!m_trustedCaFile.isEmpty()) {
                sslSocket->setTrustedCaFile(m_trustedCaFile);
            }
            SwObject::connect(sslSocket, &SwSslSocket::sslErrors, [this](const SwSslErrorList& errors) {
                if (!errors.isEmpty()) {
                    swCError(kSwLogCategory_FireBDHttpClient) << "[FireBDHttpClient] TLS error: " << errors.first();
                }
            });
        }

        connect(m_socket, &SwAbstractSocket::errorOccurred, this, &FireBDHttpClient::onError_);
        connect(m_socket, &SwAbstractSocket::disconnected, this, &FireBDHttpClient::onDisconnected_);
        connect(m_socket, &SwIODevice::readyRead, this, &FireBDHttpClient::onReadyRead_);
        if (sslSocket) {
            connect(sslSocket, &SwSslSocket::encrypted, this, &FireBDHttpClient::onConnected_);
        } else {
            connect(m_socket, &SwAbstractSocket::connected, this, &FireBDHttpClient::onConnected_);
        }

        const bool connectOk = sslSocket ? sslSocket->connectToHostEncrypted(m_host, m_port)
                                         : m_socket->connectToHost(m_host, m_port);
        if (!connectOk) {
            cleanupSocket_();
            emit errorOccurred(-2);
            return false;
        }
        return true;
    }

    /**
     * @brief Performs the `abort` operation.
     */
    void abort() { cleanupSocket_(); }

signals:
    DECLARE_SIGNAL(finished, const SwByteArray&)
    DECLARE_SIGNAL(errorOccurred, int)

private slots:
    /**
     * @brief Performs the `onConnected_` operation.
     */
    void onConnected_() {
        if (!m_socket) {
            return;
        }

        const SwString request = buildRequest_();
        if (!m_socket->write(request)) {
            cleanupSocket_();
            emit errorOccurred(-3);
        }
    }

    /**
     * @brief Performs the `onReadyRead_` operation.
     */
    void onReadyRead_() {
        if (!m_socket) {
            return;
        }

        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_buffer.append(chunk.data(), static_cast<size_t>(chunk.size()));
        }

        processBuffer_();
    }

    /**
     * @brief Performs the `onError_` operation.
     * @param err Value passed to the method.
     */
    void onError_(int err) {
        cleanupSocket_();
        emit errorOccurred(err);
    }

    /**
     * @brief Performs the `onDisconnected_` operation.
     */
    void onDisconnected_() {
        if (m_headersReceived && !m_finishedEmitted && !m_chunked && m_contentLength < 0) {
            m_responseBody.append(m_buffer.constData(), m_buffer.size());
            m_buffer.clear();
            finishRequest_();
        }
    }

private:
    static SwString methodToString_(Method m) {
        switch (m) {
        case Method::Get:
            return "GET";
        case Method::Post:
            return "POST";
        case Method::Put:
            return "PUT";
        case Method::Patch:
            return "PATCH";
        case Method::Delete:
            return "DELETE";
        default:
            return "GET";
        }
    }

    SwString buildRequest_() const {
        SwString request;
        request += methodToString_(m_method);
        request += " ";
        request += (m_path.isEmpty() ? SwString("/") : m_path);
        request += " HTTP/1.1\r\n";

        request += "Host: " + m_host + "\r\n";
        request += "Connection: close\r\n";
        request += "Accept: application/json\r\n";

        for (auto it = m_headerMap.begin(); it != m_headerMap.end(); ++it) {
            const SwString key = it->first;
            const SwString val = it->second;
            if (key.isEmpty()) {
                continue;
            }
            request += key + ": " + val + "\r\n";
        }

        if (!m_requestBody.isEmpty() || m_method == Method::Post || m_method == Method::Put || m_method == Method::Patch) {
            const SwString ct = m_requestContentType.isEmpty() ? SwString("application/json") : m_requestContentType;
            request += "Content-Type: " + ct + "\r\n";
            request += "Content-Length: " + SwString(std::to_string(m_requestBody.size())) + "\r\n";
        }

        request += "\r\n";

        if (!m_requestBody.isEmpty()) {
            request += SwString(m_requestBody);
        }

        return request;
    }

    void resetResponseState_() {
        m_buffer.clear();
        m_responseBody.clear();
        m_responseHeaders.clear();
        m_headersReceived = false;
        m_finishedEmitted = false;
        m_contentLength = -1;
        m_bytesReceived = 0;
        m_chunked = false;
        m_chunkBytesRemaining = -1;

        m_statusCode = 0;
        m_reasonPhrase = SwString();

        m_lastStatusCode = 0;
        m_lastReasonPhrase = SwString();
        m_lastResponseBody.clear();
        m_lastResponseHeaders = SwString();
    }

    void cleanupSocket_() {
        if (m_socket) {
            m_socket->deleteLater();
            m_socket = nullptr;
        }
    }

    void finishRequest_() {
        if (m_finishedEmitted) {
            return;
        }
        m_finishedEmitted = true;

        m_lastStatusCode = m_statusCode;
        m_lastReasonPhrase = m_reasonPhrase;
        m_lastResponseHeaders = m_responseHeaders;
        m_lastResponseBody = m_responseBody;

        cleanupSocket_();
        emit finished(m_lastResponseBody);
    }

    void processBuffer_() {
        if (!m_headersReceived) {
            const int sep = m_buffer.indexOf("\r\n\r\n");
            if (sep == SwByteArray::npos) {
                return;
            }
            const int headerBytes = sep + 4;
            const SwByteArray headersPart = m_buffer.left(headerBytes);
            m_buffer.remove(0, headerBytes);
            m_responseHeaders = SwString(headersPart);
            parseHeaders_(m_responseHeaders);
            m_headersReceived = true;
        }

        if (!m_headersReceived) {
            return;
        }

        if (m_chunked) {
            processChunked_();
            return;
        }

        if (m_contentLength >= 0) {
            while (!m_buffer.isEmpty() && m_bytesReceived < m_contentLength) {
                const std::int64_t remaining = m_contentLength - m_bytesReceived;
                const std::int64_t take = std::min<std::int64_t>(remaining, static_cast<std::int64_t>(m_buffer.size()));
                if (take <= 0) {
                    break;
                }
                m_responseBody.append(m_buffer.constData(), static_cast<size_t>(take));
                m_buffer.remove(0, static_cast<int>(take));
                m_bytesReceived += take;
            }
            if (m_bytesReceived >= m_contentLength) {
                finishRequest_();
            }
            return;
        }

        if (!m_buffer.isEmpty()) {
            m_responseBody.append(m_buffer.constData(), m_buffer.size());
            m_bytesReceived += static_cast<std::int64_t>(m_buffer.size());
            m_buffer.clear();
        }
    }

    void processChunked_() {
        while (true) {
            if (m_chunkBytesRemaining < 0) {
                const int lineEnd = m_buffer.indexOf("\r\n");
                if (lineEnd == SwByteArray::npos) {
                    return;
                }

                SwByteArray line = m_buffer.left(lineEnd).trimmed();
                m_buffer.remove(0, lineEnd + 2);

                const int semi = line.indexOf(';');
                if (semi != SwByteArray::npos) {
                    line = line.left(semi).trimmed();
                }

                bool ok = false;
                const long long sz = line.toLongLong(&ok, 16);
                if (!ok || sz < 0) {
                    swCError(kSwLogCategory_FireBDHttpClient) << "Invalid chunk size line: " << SwString(line);
                    cleanupSocket_();
                    emit errorOccurred(-4);
                    return;
                }

                if (sz == 0) {
                    finishRequest_();
                    return;
                }

                m_chunkBytesRemaining = static_cast<std::int64_t>(sz);
            }

            if (m_chunkBytesRemaining < 0) {
                return;
            }

            if (static_cast<std::int64_t>(m_buffer.size()) < (m_chunkBytesRemaining + 2)) {
                return;
            }

            m_responseBody.append(m_buffer.constData(), static_cast<size_t>(m_chunkBytesRemaining));
            m_buffer.remove(0, static_cast<int>(m_chunkBytesRemaining));
            m_bytesReceived += m_chunkBytesRemaining;

            if (m_buffer.size() >= 2 && m_buffer[0] == '\r' && m_buffer[1] == '\n') {
                m_buffer.remove(0, 2);
            } else {
                swCError(kSwLogCategory_FireBDHttpClient) << "Missing CRLF after chunk.";
                cleanupSocket_();
                emit errorOccurred(-5);
                return;
            }

            m_chunkBytesRemaining = -1;
        }
    }

    void parseHeaders_(const SwString& headersPart) {
        m_responseHeaders = headersPart;
        const auto lines = headersPart.split("\r\n");
        bool firstLine = true;
        for (const SwString& line : lines) {
            if (line.isEmpty()) {
                continue;
            }

            if (firstLine) {
                firstLine = false;
                const int sp1 = line.indexOf(' ');
                const int sp2 = (sp1 >= 0) ? line.indexOf(' ', sp1 + 1) : -1;
                if (sp1 >= 0) {
                    const SwString codeStr = (sp2 >= 0) ? line.mid(sp1 + 1, sp2 - (sp1 + 1)) : line.mid(sp1 + 1);
                    bool ok = false;
                    m_statusCode = codeStr.toInt(&ok);
                    if (!ok) {
                        m_statusCode = 0;
                    }
                    if (sp2 >= 0) {
                        m_reasonPhrase = line.mid(sp2 + 1).trimmed();
                    }
                }
                continue;
            }

            const int colon = line.indexOf(":");
            if (colon < 0) {
                continue;
            }

            const SwString key = line.left(colon).trimmed().toLower();
            const SwString value = line.mid(colon + 1).trimmed();

            if (key == "content-length") {
                bool ok = false;
                const int length = value.toInt(&ok);
                m_contentLength = ok ? static_cast<std::int64_t>(length) : -1;
            } else if (key == "transfer-encoding") {
                const SwString lower = value.toLower();
                if (lower.contains("chunked")) {
                    m_chunked = true;
                }
            }
        }
    }

    static bool parseUrl_(const SwString& url, SwString& scheme, SwString& host, uint16_t& port, SwString& path) {
        SwString lower = url.toLower();
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

        SwString remainder = url.mid(offset);
        const int slashPos = remainder.indexOf("/");
        SwString hostPortPart;
        if (slashPos >= 0) {
            hostPortPart = remainder.left(slashPos);
            path = remainder.mid(slashPos);
        } else {
            hostPortPart = remainder;
            path = "/";
        }

        const int colonPos = hostPortPart.indexOf(":");
        if (colonPos >= 0) {
            host = hostPortPart.left(colonPos);
            SwString portStr = hostPortPart.mid(colonPos + 1);
            bool ok = false;
            const int p = portStr.toInt(&ok);
            if (!ok || p <= 0 || p > 65535) {
                return false;
            }
            port = static_cast<uint16_t>(p);
        } else {
            host = hostPortPart;
        }

        if (path.isEmpty()) {
            path = "/";
        }
        return !host.isEmpty();
    }

    SwAbstractSocket* m_socket{nullptr};
    SwString m_trustedCaFile;

    Method m_method{Method::Get};
    SwString m_scheme;
    SwString m_host;
    SwString m_path;
    uint16_t m_port{0};
    bool m_https{false};

    SwMap<SwString, SwString> m_headerMap;

    SwByteArray m_requestBody;
    SwString m_requestContentType;

    SwByteArray m_buffer;
    SwByteArray m_responseBody;
    SwString m_responseHeaders;
    bool m_headersReceived{false};
    bool m_finishedEmitted{false};

    std::int64_t m_contentLength{-1};
    std::int64_t m_bytesReceived{0};
    bool m_chunked{false};
    std::int64_t m_chunkBytesRemaining{-1};

    int m_statusCode{0};
    SwString m_reasonPhrase;

    int m_lastStatusCode{0};
    SwString m_lastReasonPhrase;
    SwByteArray m_lastResponseBody;
    SwString m_lastResponseHeaders;
};
