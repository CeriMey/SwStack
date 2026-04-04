#pragma once

/**
 * @file src/core/io/SwWebSocketServer.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwWebSocketServer in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the web socket server interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwWebSocketServer.
 *
 * Server-oriented declarations here usually coordinate listener setup, connection or session
 * lifetime, dispatch boundaries, and integration points for higher-level request or protocol
 * logic.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
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

#include "SwObject.h"
#include "SwAbstractSocket.h"
#include "SwSslServer.h"
#include "SwTcpServer.h"
#include "SwWebSocket.h"
#include "SwList.h"
#include "SwMap.h"

static constexpr const char* kSwLogCategory_SwWebSocketServer = "sw.core.io.swwebsocketserver";

/**
 * @brief Minimal WebSocket server wrapper built on SwTcpServer + SwWebSocket (ServerRole).
 *
 * - Accepts TCP connections, performs the WebSocket server handshake, and queues ready sockets.
 * - Use nextPendingConnection() to retrieve accepted SwWebSocket instances.
 */
class SwWebSocketServer : public SwObject {
    SW_OBJECT(SwWebSocketServer, SwObject)

public:
    /**
     * @brief Constructs a `SwWebSocketServer` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwWebSocketServer(SwObject* parent = nullptr)
        : SwObject(parent) {
        m_tcpServer = new SwTcpServer(this);
        m_sslServer = new SwSslServer(this);
        connect(m_tcpServer, &SwTcpServer::newConnection, this, &SwWebSocketServer::onNewTcpConnection_);
        connect(m_sslServer, &SwTcpServer::newConnection, this, &SwWebSocketServer::onNewTcpConnection_);
    }

    /**
     * @brief Destroys the `SwWebSocketServer` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwWebSocketServer() override {
        close();
    }

    /**
     * @brief Starts listening for incoming traffic.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool listen(uint16_t port) {
        if (!m_tcpServer) {
            return false;
        }
        if (m_sslServer) {
            m_sslServer->close();
        }
        return m_tcpServer->listen(port);
    }

    bool listen(uint16_t port, const SwString& certPath, const SwString& keyPath) {
        if (!m_sslServer) {
            return false;
        }
        if (m_tcpServer) {
            m_tcpServer->close();
        }
        if (!m_sslServer->setLocalCredentials(certPath, keyPath)) {
            return false;
        }
        return m_sslServer->listen(port);
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() {
        if (m_tcpServer) {
            m_tcpServer->close();
        }
        if (m_sslServer) {
            m_sslServer->close();
        }
    }

    /**
     * @brief Sets the supported Subprotocols.
     * @param subprotocols Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSupportedSubprotocols(const SwList<SwString>& subprotocols) {
        m_supportedSubprotocols = subprotocols;
    }

    /**
     * @brief Returns the current supported Subprotocols.
     * @return The current supported Subprotocols.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwList<SwString>& supportedSubprotocols() const {
        return m_supportedSubprotocols;
    }

    /**
     * @brief Returns the current next Pending Connection.
     * @return The current next Pending Connection.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWebSocket* nextPendingConnection() {
        if (m_pendingSockets.isEmpty()) {
            return nullptr;
        }
        SwWebSocket* ws = m_pendingSockets.first();
        m_pendingSockets.removeFirst();
        return ws;
    }

signals:
    DECLARE_SIGNAL_VOID(newConnection)

private slots:
    /**
     * @brief Performs the `onNewTcpConnection_` operation.
     */
    void onNewTcpConnection_() {
        if (!m_tcpServer) {
            return;
        }

        while (SwAbstractSocket* tcp = m_tcpServer->nextPendingConnection()) {
            acceptSocket_(tcp, false);
        }
        if (!m_sslServer) {
            return;
        }
        while (SwAbstractSocket* tcp = m_sslServer->nextPendingConnection()) {
            acceptSocket_(tcp, true);
        }
    }

private:
    void acceptSocket_(SwAbstractSocket* socket, bool secure) {
        if (!socket) {
            return;
        }

        auto* ws = new SwWebSocket(SwWebSocket::ServerRole, this);
        ws->setSupportedSubprotocols(m_supportedSubprotocols);
        m_handshakeComplete[ws] = false;

        SwObject::connect(ws, &SwWebSocket::connected, [this, ws]() {
            m_handshakeComplete[ws] = true;
            m_pendingSockets.append(ws);
            emit newConnection();
        });

        SwObject::connect(ws, &SwWebSocket::errorOccurred, [this, ws](int) {
            if (!m_handshakeComplete.value(ws, false)) {
                m_handshakeComplete.remove(ws);
                ws->deleteLater();
            }
        });

        SwObject::connect(ws, &SwWebSocket::disconnected, [this, ws]() {
            if (!m_handshakeComplete.value(ws, false)) {
                m_handshakeComplete.remove(ws);
                ws->deleteLater();
            }
        });

        ws->accept(socket, secure);
    }

    SwTcpServer* m_tcpServer = nullptr;
    SwSslServer* m_sslServer = nullptr;
    SwList<SwString> m_supportedSubprotocols;
    SwList<SwWebSocket*> m_pendingSockets;
    SwMap<SwWebSocket*, bool> m_handshakeComplete;
};
