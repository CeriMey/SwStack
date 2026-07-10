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
#include "SwDequeue.h"
#include "SwList.h"
#include "SwMap.h"

#include <algorithm>
#include <cstddef>

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
        m_tcpServer = new SwTcpServer();
        m_sslServer = new SwSslServer();
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
        delete m_tcpServer;
        m_tcpServer = nullptr;
        delete m_sslServer;
        m_sslServer = nullptr;
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

        SwList<SwWebSocket*> sockets;
        for (auto it = m_liveSockets.begin(); it != m_liveSockets.end(); ++it) {
            if (it.key()) {
                sockets.append(it.key());
            }
        }
        m_liveSockets.clear();
        m_handshakePending.clear();
        m_pendingSockets.clear();
        for (std::size_t i = 0; i < sockets.size(); ++i) {
            sockets[i]->abort();
            sockets[i]->deleteLater();
        }
    }

    void setMaxConnections(std::size_t count) { m_maxConnections = count; }
    std::size_t maxConnections() const { return m_maxConnections; }

    void setMaxPendingConnections(std::size_t count) { m_maxPendingConnections = count; }
    std::size_t maxPendingConnections() const { return m_maxPendingConnections; }

    void setHandshakeTimeoutMs(int ms) { m_handshakeTimeoutMs = (std::max)(0, ms); }
    int handshakeTimeoutMs() const { return m_handshakeTimeoutMs; }

    void setMaxHandshakeBytes(std::size_t bytes) { m_maxHandshakeBytes = bytes; }
    std::size_t maxHandshakeBytes() const { return m_maxHandshakeBytes; }

    /**
     * @brief Sets the supported Subprotocols.
     * @param subprotocols Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSupportedSubprotocols(const SwList<SwString>& subprotocols) {
        m_supportedSubprotocols = subprotocols;
    }

    void setPerMessageDeflateEnabled(bool enabled) {
        m_perMessageDeflateEnabled = enabled;
    }

    void setTcpReceiveBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_tcpReceiveBufferSize = bytes;
        if (m_tcpServer) {
            m_tcpServer->setReceiveBufferSize(bytes);
        }
        if (m_sslServer) {
            m_sslServer->setReceiveBufferSize(bytes);
        }
    }

    void setTcpSendBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_tcpSendBufferSize = bytes;
        if (m_tcpServer) {
            m_tcpServer->setSendBufferSize(bytes);
        }
        if (m_sslServer) {
            m_sslServer->setSendBufferSize(bytes);
        }
    }

    int requestedTcpReceiveBufferSize() const {
        return m_tcpReceiveBufferSize;
    }

    int requestedTcpSendBufferSize() const {
        return m_tcpSendBufferSize;
    }

    bool perMessageDeflateEnabled() const {
        return m_perMessageDeflateEnabled;
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
        return m_pendingSockets.takeFirst();
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
        if (m_maxConnections > 0 && m_liveSockets.size() >= m_maxConnections) {
            socket->close();
            socket->deleteLater();
            return;
        }

        auto* ws = new SwWebSocket(SwWebSocket::ServerRole, this);
        ws->setSupportedSubprotocols(m_supportedSubprotocols);
        ws->setPerMessageDeflateEnabled(m_perMessageDeflateEnabled);
        ws->setTcpReceiveBufferSize(m_tcpReceiveBufferSize);
        ws->setTcpSendBufferSize(m_tcpSendBufferSize);
        ws->setHandshakeTimeoutMs(m_handshakeTimeoutMs);
        ws->setMaxHandshakeBytes(m_maxHandshakeBytes);
        m_liveSockets[ws] = true;
        m_handshakePending[ws] = true;

        SwObject::connect(ws, &SwWebSocket::connected, this, [this, ws]() {
            if (!m_liveSockets.contains(ws)) {
                return;
            }
            m_handshakePending.remove(ws);
            if (m_maxPendingConnections > 0 &&
                m_pendingSockets.size() >= static_cast<int>(m_maxPendingConnections)) {
                m_liveSockets.remove(ws);
                ws->close(SwWebSocket::CloseCodeGoingAway, "Server accept queue full");
                ws->deleteLater();
                return;
            }
            m_pendingSockets.append(ws);
            emit newConnection();
        });

        SwObject::connect(ws, &SwWebSocket::errorOccurred, this, [this, ws](int) {
            if (m_handshakePending.contains(ws)) {
                m_handshakePending.remove(ws);
                m_liveSockets.remove(ws);
                m_pendingSockets.removeOne(ws);
                ws->deleteLater();
            }
        });

        SwObject::connect(ws, &SwWebSocket::disconnected, this, [this, ws]() {
            m_handshakePending.remove(ws);
            m_liveSockets.remove(ws);
            m_pendingSockets.removeOne(ws);
            ws->deleteLater();
        });
        SwObject::connect(ws, &SwObject::destroyed, this, [this, ws]() {
            m_handshakePending.remove(ws);
            m_liveSockets.remove(ws);
            m_pendingSockets.removeOne(ws);
        });

        ws->accept(socket, secure);
    }

    SwTcpServer* m_tcpServer = nullptr;
    SwSslServer* m_sslServer = nullptr;
    SwList<SwString> m_supportedSubprotocols;
    bool m_perMessageDeflateEnabled = false;
    int m_tcpReceiveBufferSize = 0;
    int m_tcpSendBufferSize = 0;
    std::size_t m_maxConnections = 4096;
    std::size_t m_maxPendingConnections = 1024;
    int m_handshakeTimeoutMs = 10 * 1000;
    std::size_t m_maxHandshakeBytes = 32 * 1024;
    SwDequeue<SwWebSocket*> m_pendingSockets;
    SwMap<SwWebSocket*, bool> m_liveSockets;
    SwMap<SwWebSocket*, bool> m_handshakePending;
};
