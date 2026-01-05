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
#include "SwTcpServer.h"
#include "SwWebSocket.h"
#include "SwList.h"
#include "SwMap.h"

static constexpr const char* kSwLogCategory_SwWebSocketServer = "sw.core.io.swwebsocketserver";

/**
 * @brief Minimal QWebSocketServer-like wrapper built on SwTcpServer + SwWebSocket (ServerRole).
 *
 * - Accepts TCP connections, performs the WebSocket server handshake, and queues ready sockets.
 * - Use nextPendingConnection() to retrieve accepted SwWebSocket instances.
 */
class SwWebSocketServer : public SwObject {
    SW_OBJECT(SwWebSocketServer, SwObject)

public:
    explicit SwWebSocketServer(SwObject* parent = nullptr)
        : SwObject(parent) {
        m_tcpServer = new SwTcpServer(this);
        connect(m_tcpServer, SIGNAL(newConnection), this, &SwWebSocketServer::onNewTcpConnection_);
    }

    ~SwWebSocketServer() override {
        close();
    }

    bool listen(uint16_t port) {
        if (!m_tcpServer) {
            return false;
        }
        return m_tcpServer->listen(port);
    }

    void close() {
        if (m_tcpServer) {
            m_tcpServer->close();
        }
    }

    void setSupportedSubprotocols(const SwList<SwString>& subprotocols) {
        m_supportedSubprotocols = subprotocols;
    }

    const SwList<SwString>& supportedSubprotocols() const {
        return m_supportedSubprotocols;
    }

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
    void onNewTcpConnection_() {
        if (!m_tcpServer) {
            return;
        }

        while (SwTcpSocket* tcp = m_tcpServer->nextPendingConnection()) {
            auto* ws = new SwWebSocket(SwWebSocket::ServerRole, this);
            ws->setSupportedSubprotocols(m_supportedSubprotocols);
            m_handshakeComplete[ws] = false;

            connect(ws, SIGNAL(connected), this, [this, ws]() {
                m_handshakeComplete[ws] = true;
                m_pendingSockets.append(ws);
                emit newConnection();
            });

            connect(ws, SIGNAL(errorOccurred), this, [this, ws](int) {
                if (!m_handshakeComplete.value(ws, false)) {
                    m_handshakeComplete.remove(ws);
                    ws->deleteLater();
                }
            });

            connect(ws, SIGNAL(disconnected), this, [this, ws]() {
                if (!m_handshakeComplete.value(ws, false)) {
                    m_handshakeComplete.remove(ws);
                    ws->deleteLater();
                }
            });

            ws->accept(tcp);
        }
    }

private:
    SwTcpServer* m_tcpServer = nullptr;
    SwList<SwString> m_supportedSubprotocols;
    SwList<SwWebSocket*> m_pendingSockets;
    SwMap<SwWebSocket*, bool> m_handshakeComplete;
};

