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
#include "SwDebug.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTcpSocket.h"

#include <algorithm>
#include <cstdint>

static constexpr const char* kSwLogCategory_SwHttpClient = "sw.core.io.swhttpclient";

class SwHttpClient : public SwObject {
    SW_OBJECT(SwHttpClient, SwObject)

public:
    enum class Method {
        Get,
        Head,
        Post,
        Put,
        Patch,
        Delete
    };

    explicit SwHttpClient(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwHttpClient() override {
        abort();
    }

    void setRawHeader(const SwString& key, const SwString& value) {
        if (key.trimmed().isEmpty()) {
            return;
        }
        m_headerMap[key] = value;
    }

    void clearRawHeaders() {
        m_headerMap.clear();
    }

    void setTrustedCaFile(const SwString& path) {
        m_trustedCaFile = path;
    }

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

        abort();

        m_method = method;
        m_scheme = scheme.toLower();
        m_host = host;
        m_port = port;
        m_path = path.isEmpty() ? SwString("/") : path;
        m_https = (m_scheme == "https");
        m_requestBody = body;
        m_requestContentType = contentType;
        m_headRequest = (method == Method::Head);

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
                    swCError(kSwLogCategory_SwHttpClient) << "[SwHttpClient] TLS error: " << errors.first();
                }
            });
        }

        connect(m_socket, &SwAbstractSocket::errorOccurred, this, &SwHttpClient::onError_);
        connect(m_socket, &SwAbstractSocket::disconnected, this, &SwHttpClient::onDisconnected_);
        connect(m_socket, &SwIODevice::readyRead, this, &SwHttpClient::onReadyRead_);
        if (sslSocket) {
            connect(sslSocket, &SwSslSocket::encrypted, this, &SwHttpClient::onConnected_);
        } else {
            connect(m_socket, &SwAbstractSocket::connected, this, &SwHttpClient::onConnected_);
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

    void abort() {
        cleanupSocket_();
        resetResponseState_();
    }

signals:
    DECLARE_SIGNAL(finished, const SwByteArray&)
    DECLARE_SIGNAL(errorOccurred, int)

private slots:
    void onConnected_() {
        if (!m_socket) {
            return;
        }

        const SwString requestText = buildRequest_();
        if (!m_socket->write(requestText)) {
            cleanupSocket_();
            emit errorOccurred(-3);
        }
    }

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

    void onError_(int err) {
        cleanupSocket_();
        emit errorOccurred(err);
    }

    void onDisconnected_() {
        if (!m_headersReceived || m_finishedEmitted) {
            return;
        }

        if (!m_chunked && m_contentLength < 0) {
            if (!m_buffer.isEmpty()) {
                m_responseBody.append(m_buffer.constData(), m_buffer.size());
                m_buffer.clear();
            }
            finishRequest_();
        }
    }

private:
    static SwString methodToString_(Method method) {
        switch (method) {
        case Method::Get: return "GET";
        case Method::Head: return "HEAD";
        case Method::Post: return "POST";
        case Method::Put: return "PUT";
        case Method::Patch: return "PATCH";
        case Method::Delete: return "DELETE";
        default: return "GET";
        }
    }

    SwString buildRequest_() const {
        SwString requestText;
        requestText += methodToString_(m_method);
        requestText += " ";
        requestText += (m_path.isEmpty() ? SwString("/") : m_path);
        requestText += " HTTP/1.1\r\n";
        requestText += "Host: " + hostHeaderValue_() + "\r\n";

        bool hasConnection = false;
        bool hasAccept = false;
        bool hasUserAgent = false;
        bool hasContentType = false;
        bool hasContentLength = false;
        for (auto it = m_headerMap.begin(); it != m_headerMap.end(); ++it) {
            const SwString key = it.key();
            const SwString lower = key.toLower();
            if (lower == "connection") {
                hasConnection = true;
            } else if (lower == "accept") {
                hasAccept = true;
            } else if (lower == "user-agent") {
                hasUserAgent = true;
            } else if (lower == "content-type") {
                hasContentType = true;
            } else if (lower == "content-length") {
                hasContentLength = true;
            }
            requestText += key + ": " + it.value() + "\r\n";
        }

        if (!hasConnection) {
            requestText += "Connection: close\r\n";
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
        if (!m_requestBody.isEmpty()) {
            requestText += SwString(m_requestBody);
        }
        return requestText;
    }

    SwString hostHeaderValue_() const {
        const bool defaultPort = (!m_https && m_port == 80) || (m_https && m_port == 443);
        if (defaultPort || m_port == 0) {
            return m_host;
        }
        return m_host + ":" + SwString::number(static_cast<int>(m_port));
    }

    void resetResponseState_() {
        m_buffer.clear();
        m_responseBody.clear();
        m_responseHeaders.clear();
        m_responseHeaderMap.clear();
        m_headersReceived = false;
        m_finishedEmitted = false;
        m_contentLength = -1;
        m_bytesReceived = 0;
        m_chunked = false;
        m_chunkBytesRemaining = -1;
        m_statusCode = 0;
        m_reasonPhrase.clear();

        m_lastStatusCode = 0;
        m_lastReasonPhrase.clear();
        m_lastResponseBody.clear();
        m_lastResponseHeaders.clear();
        m_lastResponseHeaderMap.clear();
    }

    void cleanupSocket_() {
        if (!m_socket) {
            return;
        }
        m_socket->disconnectAllSlots();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    void finishRequest_() {
        if (m_finishedEmitted) {
            return;
        }
        m_finishedEmitted = true;

        m_lastStatusCode = m_statusCode;
        m_lastReasonPhrase = m_reasonPhrase;
        m_lastResponseHeaders = m_responseHeaders;
        m_lastResponseHeaderMap = m_responseHeaderMap;
        m_lastResponseBody = m_responseBody;

        cleanupSocket_();
        emit finished(m_lastResponseBody);
    }

    void processBuffer_() {
        if (!m_headersReceived) {
            const int boundary = m_buffer.indexOf("\r\n\r\n");
            if (boundary == SwByteArray::npos) {
                return;
            }

            const int headerBytes = boundary + 4;
            const SwByteArray headersPart = m_buffer.left(headerBytes);
            m_buffer.remove(0, headerBytes);
            m_responseHeaders = SwString(headersPart);
            parseHeaders_(m_responseHeaders);
            m_headersReceived = true;

            if (m_headRequest || m_contentLength == 0) {
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
            m_bytesReceived += static_cast<long long>(m_buffer.size());
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
                    swCError(kSwLogCategory_SwHttpClient) << "[SwHttpClient] invalid chunk size: " << SwString(line);
                    cleanupSocket_();
                    emit errorOccurred(-4);
                    return;
                }

                if (sz == 0) {
                    finishRequest_();
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

            m_responseBody.append(m_buffer.constData(), static_cast<size_t>(m_chunkBytesRemaining));
            m_buffer.remove(0, static_cast<int>(m_chunkBytesRemaining));
            m_bytesReceived += m_chunkBytesRemaining;

            if (m_buffer.size() < 2 || m_buffer[0] != '\r' || m_buffer[1] != '\n') {
                swCError(kSwLogCategory_SwHttpClient) << "[SwHttpClient] missing CRLF after chunk";
                cleanupSocket_();
                emit errorOccurred(-5);
                return;
            }

            m_buffer.remove(0, 2);
            m_chunkBytesRemaining = -1;
        }
    }

    void parseHeaders_(const SwString& headersText) {
        const SwList<SwString> lines = headersText.split("\r\n");
        bool firstLine = true;
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
                    const SwString codeText =
                        (sp2 >= 0) ? line.mid(sp1 + 1, sp2 - (sp1 + 1)) : line.mid(sp1 + 1);
                    bool ok = false;
                    m_statusCode = codeText.toInt(&ok);
                    if (!ok) {
                        m_statusCode = 0;
                    }
                    if (sp2 >= 0) {
                        m_reasonPhrase = line.mid(sp2 + 1).trimmed();
                    }
                }
                continue;
            }

            const int colon = line.indexOf(':');
            if (colon < 0) {
                continue;
            }

            const SwString key = line.left(colon).trimmed().toLower();
            const SwString value = line.mid(colon + 1).trimmed();
            m_responseHeaderMap[key] = value;

            if (key == "content-length") {
                bool ok = false;
                const long long length = value.toLongLong(&ok);
                m_contentLength = ok ? static_cast<long long>(length) : -1;
            } else if (key == "transfer-encoding" && value.toLower().contains("chunked")) {
                m_chunked = true;
            }
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
            const SwString portText = hostPortPart.mid(colonPos + 1);
            bool ok = false;
            const int parsedPort = portText.toInt(&ok);
            if (!ok || parsedPort <= 0 || parsedPort > 65535) {
                return false;
            }
            port = static_cast<uint16_t>(parsedPort);
        } else {
            host = hostPortPart;
        }

        if (path.isEmpty()) {
            path = "/";
        }
        return !host.isEmpty();
    }

    SwAbstractSocket* m_socket = nullptr;
    SwString m_trustedCaFile;

    Method m_method = Method::Get;
    SwString m_scheme;
    SwString m_host;
    SwString m_path;
    uint16_t m_port = 0;
    bool m_https = false;
    bool m_headRequest = false;

    SwMap<SwString, SwString> m_headerMap;

    SwByteArray m_requestBody;
    SwString m_requestContentType;

    SwByteArray m_buffer;
    SwByteArray m_responseBody;
    SwString m_responseHeaders;
    SwMap<SwString, SwString> m_responseHeaderMap;
    bool m_headersReceived = false;
    bool m_finishedEmitted = false;

    long long m_contentLength = -1;
    long long m_bytesReceived = 0;
    bool m_chunked = false;
    long long m_chunkBytesRemaining = -1;

    int m_statusCode = 0;
    SwString m_reasonPhrase;

    int m_lastStatusCode = 0;
    SwString m_lastReasonPhrase;
    SwByteArray m_lastResponseBody;
    SwString m_lastResponseHeaders;
    SwMap<SwString, SwString> m_lastResponseHeaderMap;
};
