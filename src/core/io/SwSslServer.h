#pragma once

/**
 * @file src/core/io/SwSslServer.h
 * @ingroup core_io
 * @brief TLS server built on top of SwTcpServer and SwSslSocket.
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

#include "SwBackendSsl.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTcpServer.h"
#include "SwTimer.h"

#include <algorithm>
#include <cstddef>

struct SwTlsCredentialEntry {
    SwString host;
    SwString certPath;
    SwString keyPath;
    bool isDefault = false;
};

class SwSslServer : public SwTcpServer {
    SW_OBJECT(SwSslServer, SwTcpServer)

public:
    explicit SwSslServer(SwObject* parent = nullptr)
        : SwTcpServer(parent) {
    }

    ~SwSslServer() override {
        close();
        clearCredentials_();
    }

    /**
     * Configure the server ALPN preference list before loading credentials.
     * A configured list is mandatory at handshake time: peers offering no
     * exact match are rejected with no_application_protocol semantics.
     */
    bool setApplicationProtocols(const SwList<SwByteArray>& protocols) {
        if (m_sslCtx || isListening()) {
            return false;
        }
        for (std::size_t i = 0; i < protocols.size(); ++i) {
            if (protocols[i].isEmpty() || protocols[i].size() > 255) {
                return false;
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (protocols[j] == protocols[i]) {
                    return false;
                }
            }
        }
        m_applicationProtocols = protocols;
        return true;
    }

    bool setApplicationProtocol(const SwByteArray& protocol) {
        SwList<SwByteArray> protocols;
        protocols.append(protocol);
        return setApplicationProtocols(protocols);
    }

    bool setTls13Only(bool enabled) {
        if (m_sslCtx || isListening()) {
            return false;
        }
        m_tls13Only = enabled;
        return true;
    }

    bool setLocalCredentials(const SwString& certPath, const SwString& keyPath) {
        return reloadLocalCredentials(certPath, keyPath);
    }

    bool setLocalCredentials(const SwList<SwTlsCredentialEntry>& credentials) {
        return reloadLocalCredentials(credentials);
    }

    bool reloadLocalCredentials(const SwString& certPath, const SwString& keyPath) {
        return reloadLocalCredentials(singleCredentialList_(certPath, keyPath));
    }

    bool reloadLocalCredentials(const SwList<SwTlsCredentialEntry>& credentials) {
        std::vector<SwBackendSsl::ServerCertificateConfig> configs;
        configs.reserve(credentials.size());
        for (std::size_t i = 0; i < credentials.size(); ++i) {
            const SwTlsCredentialEntry& entry = credentials[i];
            if (entry.certPath.trimmed().isEmpty() || entry.keyPath.trimmed().isEmpty()) {
                continue;
            }
            SwBackendSsl::ServerCertificateConfig config;
            config.host = entry.host.trimmed().toStdString();
            config.certPath = entry.certPath.trimmed().toStdString();
            config.keyPath = entry.keyPath.trimmed().toStdString();
            config.isDefault = entry.isDefault;
            configs.push_back(config);
        }
        if (configs.empty()) {
            swCError(kSwLogCategory_SwTcpServer) << "TLS init failed: no certificate configured";
            return false;
        }

        std::string error;
        std::vector<std::string> applicationProtocols;
        applicationProtocols.reserve(m_applicationProtocols.size());
        for (std::size_t i = 0; i < m_applicationProtocols.size(); ++i) {
            applicationProtocols.emplace_back(m_applicationProtocols[i].constData(),
                                              m_applicationProtocols[i].size());
        }
        void* nextCtx = SwBackendSsl::createServerContextSet(
            configs, error, applicationProtocols, m_tls13Only);
        if (!nextCtx) {
            swCError(kSwLogCategory_SwTcpServer) << "TLS init failed: " << error;
            return false;
        }
        void* previousCtx = m_sslCtx;
        m_sslCtx = nextCtx;
        if (previousCtx) {
            SwBackendSsl::freeServerContext(previousCtx);
        }
        return true;
    }

    bool listen(uint16_t port) {
        return listen(SwString(), port);
    }

    bool listen(const SwString& bindAddress, uint16_t port) {
        if (!m_sslCtx) {
            swCError(kSwLogCategory_SwTcpServer) << "TLS credentials not configured";
            return false;
        }
        return SwTcpServer::listen(bindAddress, port);
    }

    void close() {
        SwList<SwSslSocket*> handshakes;
        for (auto it = m_handshakes.begin(); it != m_handshakes.end(); ++it) {
            if (it.key()) {
                handshakes.append(it.key());
            }
            if (it.value()) {
                it.value()->stop();
                it.value()->deleteLater();
            }
        }
        m_handshakes.clear();
        for (std::size_t i = 0; i < handshakes.size(); ++i) {
            SwObject::disconnect(handshakes[i], this);
            handshakes[i]->close();
            handshakes[i]->deleteLater();
        }
        SwTcpServer::close();
    }

    void setMaxConcurrentHandshakes(std::size_t count) { m_maxConcurrentHandshakes = count; }
    std::size_t maxConcurrentHandshakes() const { return m_maxConcurrentHandshakes; }

    void setHandshakeTimeoutMs(int timeoutMs) { m_handshakeTimeoutMs = (std::max)(0, timeoutMs); }
    int handshakeTimeoutMs() const { return m_handshakeTimeoutMs; }

    SwSslSocket* nextPendingConnection() override {
        return static_cast<SwSslSocket*>(SwTcpServer::nextPendingConnection());
    }

protected:
    SwTcpSocket* createPendingSocket_() override {
        return new SwSslSocket();
    }

    bool shouldEmitConnectedOnAdopt_(SwTcpSocket*) const override {
        return false;
    }

    bool finalizeAcceptedSocket_(SwTcpSocket* socket) override {
        SwSslSocket* sslSocket = static_cast<SwSslSocket*>(socket);
        if (m_maxConcurrentHandshakes > 0 &&
            m_handshakes.size() >= m_maxConcurrentHandshakes) {
            swCWarning(kSwLogCategory_SwTcpServer)
                << "[SwSslServer] rejecting connection: TLS handshake cap reached";
            return false;
        }
        swCDebug(kSwLogCategory_SwTcpServer) << "[SwSslServer] accepted TCP socket, starting TLS handshake";

        SwTimer* timeout = nullptr;
        if (m_handshakeTimeoutMs > 0) {
            timeout = new SwTimer(this);
            timeout->setSingleShot(true);
            SwObject::connect(timeout, &SwTimer::timeout, this, [this, sslSocket]() {
                finishHandshake_(sslSocket, false, "TLS handshake timeout");
            });
        }
        m_handshakes[sslSocket] = timeout;

        SwObject::connect(sslSocket, &SwSslSocket::encrypted, this, [this, sslSocket]() {
            finishHandshake_(sslSocket, true, SwString());
        });
        SwObject::connect(sslSocket, &SwSslSocket::errorOccurred, this, [this, sslSocket](int) {
            finishHandshake_(sslSocket, false, "TLS handshake error");
        });
        SwObject::connect(sslSocket, &SwSslSocket::disconnected, this, [this, sslSocket]() {
            finishHandshake_(sslSocket, false, "TLS peer disconnected during handshake");
        });

        if (!sslSocket->startServerEncryption_(m_sslCtx)) {
            // A synchronous error signal may already have completed and disposed
            // this socket.  In that case tell the base accept loop not to do it a
            // second time.
            auto it = m_handshakes.find(sslSocket);
            if (it == m_handshakes.end()) {
                return true;
            }
            SwTimer* pendingTimeout = it.value();
            m_handshakes.erase(it);
            if (pendingTimeout) {
                pendingTimeout->deleteLater();
            }
            SwObject::disconnect(sslSocket, this);
            swCError(kSwLogCategory_SwTcpServer)
                << "[SwSslServer] unable to start TLS server handshake";
            return false;
        }
        if (timeout) {
            timeout->start(m_handshakeTimeoutMs);
        }
        return true;
    }

private:
    void* m_sslCtx = nullptr;
    std::size_t m_maxConcurrentHandshakes = 1024;
    int m_handshakeTimeoutMs = 10 * 1000;
    SwMap<SwSslSocket*, SwTimer*> m_handshakes;
    SwList<SwByteArray> m_applicationProtocols;
    bool m_tls13Only = false;

    void finishHandshake_(SwSslSocket* socket, bool success, const SwString& error) {
        auto it = m_handshakes.find(socket);
        if (it == m_handshakes.end()) {
            return;
        }
        SwTimer* timeout = it.value();
        m_handshakes.erase(it);
        if (timeout) {
            timeout->stop();
            timeout->deleteLater();
        }
        SwObject::disconnect(socket, this);

        if (success) {
            swCDebug(kSwLogCategory_SwTcpServer)
                << "[SwSslServer] TLS handshake completed, queueing connection";
            queuePendingConnection_(socket);
            return;
        }

        swCError(kSwLogCategory_SwTcpServer)
            << "[SwSslServer] " << (error.isEmpty() ? SwString("TLS handshake failed") : error);
        socket->close();
        socket->deleteLater();
    }

    static SwList<SwTlsCredentialEntry> singleCredentialList_(const SwString& certPath, const SwString& keyPath) {
        SwList<SwTlsCredentialEntry> credentials;
        if (!certPath.trimmed().isEmpty() && !keyPath.trimmed().isEmpty()) {
            SwTlsCredentialEntry entry;
            entry.certPath = certPath.trimmed();
            entry.keyPath = keyPath.trimmed();
            entry.isDefault = true;
            credentials.append(entry);
        }
        return credentials;
    }

    void clearCredentials_() {
        if (!m_sslCtx) {
            return;
        }
        SwBackendSsl::freeServerContext(m_sslCtx);
        m_sslCtx = nullptr;
    }
};
