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
#include "SwSslSocket.h"
#include "SwString.h"
#include "SwTcpServer.h"

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
        void* nextCtx = SwBackendSsl::createServerContextSet(configs, error);
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
        SwTcpServer::close();
    }

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
        std::shared_ptr<bool> completed(new bool(false));
        swCDebug(kSwLogCategory_SwTcpServer) << "[SwSslServer] accepted TCP socket, starting TLS handshake";

        SwObject::connect(sslSocket, &SwSslSocket::encrypted, [this, sslSocket, completed]() {
            if (*completed) {
                return;
            }
            *completed = true;
            swCDebug(kSwLogCategory_SwTcpServer) << "[SwSslServer] TLS handshake completed, queueing connection";
            queuePendingConnection_(sslSocket);
        });

        auto fail = [completed, sslSocket]() {
            if (*completed) {
                return;
            }
            *completed = true;
            swCError(kSwLogCategory_SwTcpServer) << "[SwSslServer] TLS handshake failed or socket closed before encryption";
            sslSocket->close();
            sslSocket->deleteLater();
        };

        SwObject::connect(sslSocket, &SwSslSocket::errorOccurred, [fail](int) { fail(); });
        SwObject::connect(sslSocket, &SwSslSocket::disconnected, fail);

        if (!sslSocket->startServerEncryption_(m_sslCtx)) {
            fail();
            return false;
        }
        return true;
    }

private:
    void* m_sslCtx = nullptr;

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
