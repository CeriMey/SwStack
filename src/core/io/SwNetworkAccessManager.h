#pragma once
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
#include "SwTcpSocket.h"
#include "SwString.h"
#include "SwByteArray.h"
#include "SwMap.h"
#include "SwDebug.h"
#include <iostream>
#include <fstream>
static constexpr const char* kSwLogCategory_SwNetworkAccessManager = "sw.core.io.swnetworkaccessmanager";


/**
 * @class SwNetworkAccessManager
 * @brief Lightweight asynchronous HTTP/HTTPS client built on top of SwTcpSocket.
 *
 * This class mimics the non-blocking single-threaded philosophy of Qt's QNetworkAccessManager.
 * All operations happen in the caller's thread and rely on the event loop plus SwTcpSocket's
 * signals to drive the state machine.
 */
class SwNetworkAccessManager : public SwObject {
    SW_OBJECT(SwNetworkAccessManager, SwObject)

public:
    SwNetworkAccessManager(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwNetworkAccessManager() {
        abort();
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

    /**
     * @brief Sets or updates a header that will be sent with the next request.
     *
     * Headers persist between requests, similar to Qt's API.
     */
    void setRawHeader(const SwString& key, const SwString& value) {
        m_headerMap[key] = value;
    }

    /**
     * @brief Starts an asynchronous GET request.
     *
     * The request result is delivered through the `finished` or `errorOccurred` signals.
     */
    bool get(const SwString& url) {
        SwString scheme;
        SwString host;
        uint16_t port = 0;
        SwString path;
        if (!parseUrl(url, scheme, host, port, path)) {
            emit errorOccurred(-1);
            return false;
        }

        abort();

        m_scheme = scheme.toLower();
        m_host = host;
        m_port = port;
        m_path = path.isEmpty() ? SwString("/") : path;
        m_https = (m_scheme == "https");

        resetResponseState();

        m_socket = new SwTcpSocket(this);
        if (m_https) {
            m_socket->useSsl(true, m_host);
        } else {
            m_socket->useSsl(false);
        }

        connect(m_socket, SIGNAL(connected), this, &SwNetworkAccessManager::onConnected);
        connect(m_socket, SIGNAL(errorOccurred), this, &SwNetworkAccessManager::onError);
        connect(m_socket, SIGNAL(disconnected), this, &SwNetworkAccessManager::onDisconnected);
        connect(m_socket, SIGNAL(readyRead), this, &SwNetworkAccessManager::onReadyRead);

        if (!m_socket->connectToHost(m_host, m_port)) {
            cleanupSocket();
            emit errorOccurred(-2);
            return false;
        }

        return true;
    }

    /**
     * @brief Cancels the current request, if any.
     */
    void abort() {
        cleanupSocket();
    }

signals:
    DECLARE_SIGNAL(finished, const SwByteArray&)
    DECLARE_SIGNAL(errorOccurred, int)

private slots:
    void onConnected() {
        swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] connected, sending request";
        SwString request = buildRequest();
        if (!m_socket || !m_socket->write(request)) {
            cleanupSocket();
            emit errorOccurred(-3);
        }
    }

    void onReadyRead() {
        if (!m_socket) {
            return;
        }

        swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] readyRead";
        bool received = false;
        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }

            swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] chunk size=" << chunk.size();
            m_buffer.append(chunk.data(), chunk.size());
            swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] buffer size after append=" << m_buffer.size();
            received = true;
        }

        if (received) {
            processBuffer();
        }

        if (m_socket && m_socket->isRemoteClosed()) {
            if (!m_headersReceived || m_contentLength < 0) {
                if (!m_buffer.isEmpty()) {
                    m_responseBody.append(m_buffer.constData(), m_buffer.size());
                    m_bytesReceived += static_cast<int64_t>(m_buffer.size());
                    m_buffer.clear();
                }
                finishRequest();
            } else if (m_bytesReceived >= m_contentLength) {
                finishRequest();
            } else {
                cleanupSocket();
                emit errorOccurred(-4);
            }
        }
    }

    void onDisconnected() {
        // Completion is handled in onReadyRead when the peer closes.
    }

    void onError(int err) {
        swCError(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] socket error " << err;
        cleanupSocket();
        emit errorOccurred(err);
    }

    void cleanupSocket() {
        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        resetResponseState();
        m_scheme.clear();
        m_host.clear();
        m_path.clear();
        m_port = 0;
        m_https = false;
    }

    void resetResponseState() {
        m_buffer.clear();
        m_responseBody.clear();
        m_responseHeaders.clear();
        m_headersReceived = false;
        m_contentLength = -1;
        m_bytesReceived = 0;
    }

    SwString buildRequest() const {
        SwString request = "GET " + (m_path.isEmpty() ? SwString("/") : m_path) + " HTTP/1.1\r\n";
        request += "Host: " + hostHeaderValue() + "\r\n";

        bool hasConnection = false;
        bool hasUserAgent = false;

        for (const auto& header : m_headerMap) {
            SwString lowerKey = header.first.toLower();
            if (lowerKey == "connection") {
                hasConnection = true;
            } else if (lowerKey == "user-agent") {
                hasUserAgent = true;
            }
            request += header.first + ": " + header.second + "\r\n";
        }

        if (!hasConnection) {
            request += "Connection: close\r\n";
        }
        if (!hasUserAgent) {
            request += "User-Agent: SwNetworkAccessManager/1.0\r\n";
        }
        request += "\r\n";
        return request;
    }

    SwString hostHeaderValue() const {
        bool defaultPort = (!m_https && m_port == 80) || (m_https && m_port == 443);
        if (defaultPort || m_port == 0) {
            return m_host;
        }
        return m_host + ":" + SwString::number(static_cast<int>(m_port));
    }

    void processBuffer() {
        swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] processBuffer called, buffer size=" << m_buffer.size()
                  << " headers=" << (m_headersReceived ? 1 : 0);
        if (!m_headersReceived) {
            int boundary = m_buffer.indexOf("\r\n\r\n");
            if (boundary < 0) {
                std::string preview(m_buffer.constData(),
                                    m_buffer.constData() + std::min<size_t>(m_buffer.size(), 80));
                swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] waiting headers, buffer preview=\""
                          << preview << "\" size=" << m_buffer.size();
                return;
            }

            SwByteArray headerBytes = m_buffer.left(boundary);
            m_buffer.remove(0, boundary + 4);
            m_headersReceived = true;
            parseHeaders(SwString(headerBytes));
            swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] parsed headers, remaining buffer=" << m_buffer.size()
                      << " contentLength=" << m_contentLength;
        }

        if (!m_headersReceived) {
            return;
        }

        if (m_contentLength >= 0) {
            while (!m_buffer.isEmpty() && m_bytesReceived < m_contentLength) {
                int64_t remaining = m_contentLength - m_bytesReceived;
                size_t chunk = static_cast<size_t>(std::min<int64_t>(remaining, static_cast<int64_t>(m_buffer.size())));
                if (chunk == 0) {
                    break;
                }
                m_responseBody.append(m_buffer.constData(), chunk);
                m_buffer.remove(0, static_cast<int>(chunk));
                m_bytesReceived += static_cast<int64_t>(chunk);
                swCDebug(kSwLogCategory_SwNetworkAccessManager) << "[SwNetworkAccessManager] appended chunk=" << chunk
                          << " total=" << m_bytesReceived;
            }

            if (m_bytesReceived >= m_contentLength) {
                finishRequest();
            }
        } else if (!m_buffer.isEmpty()) {
            m_responseBody.append(m_buffer.constData(), m_buffer.size());
            m_bytesReceived += static_cast<int64_t>(m_buffer.size());
            m_buffer.clear();
        }
    }

    void parseHeaders(const SwString& headersPart) {
        m_responseHeaders = headersPart;
        auto lines = headersPart.split("\r\n");
        for (const SwString& line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            int colon = line.indexOf(":");
            if (colon < 0) {
                continue;
            }

            SwString key = line.left(colon).trimmed().toLower();
            SwString value = line.mid(colon + 1).trimmed();

            if (key == "content-length") {
                bool ok = false;
                int length = value.toInt(&ok);
                m_contentLength = ok ? static_cast<int64_t>(length) : -1;
            }
        }
    }

    void finishRequest() {
        if (!m_socket) {
            return;
        }

        if (!m_buffer.isEmpty()) {
            if (m_contentLength >= 0 && m_bytesReceived >= m_contentLength) {
                m_buffer.clear(); // ignore surplus beyond declared length
            } else {
                m_responseBody.append(m_buffer.constData(), m_buffer.size());
                m_bytesReceived += static_cast<int64_t>(m_buffer.size());
                m_buffer.clear();
            }
        }

        m_lastResponseHeaders = m_responseHeaders;
        m_lastResponseBody = m_responseBody;

        cleanupSocket();

        {
            std::ofstream debugRaw("SwNetworkAccessManager_raw.bin", std::ios::binary | std::ios::trunc);
            if (debugRaw && !m_lastResponseBody.isEmpty()) {
                debugRaw.write(m_lastResponseBody.constData(),
                               static_cast<std::streamsize>(m_lastResponseBody.size()));
            }
        }

        emit finished(m_lastResponseBody);
    }

    bool parseUrl(const SwString& url, SwString& scheme, SwString& host, uint16_t& port, SwString& path) {
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
        int slashPos = remainder.indexOf("/");
        SwString hostPortPart;
        if (slashPos >= 0) {
            hostPortPart = remainder.left(slashPos);
            path = remainder.mid(slashPos);
        } else {
            hostPortPart = remainder;
            path = "/";
        }

        int colonPos = hostPortPart.indexOf(":");
        if (colonPos >= 0) {
            host = hostPortPart.left(colonPos);
            SwString portStr = hostPortPart.mid(colonPos + 1);
            bool ok = false;
            int p = portStr.toInt(&ok);
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

    SwTcpSocket* m_socket = nullptr;
    SwString m_scheme;
    SwString m_host;
    SwString m_path;
    uint16_t m_port = 0;
    bool m_https = false;

    SwMap<SwString, SwString> m_headerMap;

    SwByteArray m_buffer;
    SwByteArray m_responseBody;
    SwString m_responseHeaders;
    SwByteArray m_lastResponseBody;
    SwString m_lastResponseHeaders;
    bool m_headersReceived = false;
    int64_t m_contentLength = -1;
    int64_t m_bytesReceived = 0;
};
