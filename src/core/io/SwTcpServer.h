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
#include "SwString.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "SwDebug.h"
static constexpr const char* kSwLogCategory_SwTcpServer = "sw.core.io.swtcpserver";

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/win/SwWindows.h"
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

class SwTcpServer : public SwObject {
    SW_OBJECT(SwTcpServer, SwObject)
public:
    SwTcpServer(SwObject* parent = nullptr)
        : SwObject(parent), m_listenSocket(INVALID_SOCKET), m_listenEvent(NULL)
    {
        initializeWinsock();
        m_timer = new SwTimer(50, this);
        connect(m_timer, SIGNAL(timeout), this, &SwTcpServer::onCheckEvents);
        m_timer->start();
    }

    virtual ~SwTcpServer() {
        close();
    }

    bool listen(uint16_t port) {
        close();

        m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (m_listenSocket == INVALID_SOCKET) {
            swCError(kSwLogCategory_SwTcpServer) << "WSASocket failed: " << WSAGetLastError();
            return false;
        }

        u_long mode = 1;
        ioctlsocket(m_listenSocket, FIONBIO, &mode);

        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (::bind(m_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "bind failed: " << WSAGetLastError();
            close();
            return false;
        }

        if (::listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "listen failed: " << WSAGetLastError();
            close();
            return false;
        }

        m_listenEvent = WSACreateEvent();
        if (m_listenEvent == WSA_INVALID_EVENT) {
            swCError(kSwLogCategory_SwTcpServer) << "WSACreateEvent failed: " << WSAGetLastError();
            close();
            return false;
        }

        if (WSAEventSelect(m_listenSocket, m_listenEvent, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "WSAEventSelect failed: " << WSAGetLastError();
            close();
            return false;
        }

        return true;
    }

    void close() {
        if (m_listenSocket != INVALID_SOCKET) {
            closesocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
        }
        if (m_listenEvent) {
            WSACloseEvent(m_listenEvent);
            m_listenEvent = NULL;
        }
    }

    SwTcpSocket* nextPendingConnection() {
        if (m_pendingConnections.isEmpty()) {
            return nullptr;
        }
        SwTcpSocket* sock = m_pendingConnections.first();
        m_pendingConnections.removeFirst();
        return sock;
    }

signals:
    DECLARE_SIGNAL_VOID(newConnection)

private slots:
    void onCheckEvents() {
        if (m_listenEvent == NULL || m_listenSocket == INVALID_SOCKET) {
            return;
        }

        DWORD res = WSAWaitForMultipleEvents(1, &m_listenEvent, FALSE, 0, FALSE);
        if (res == WSA_WAIT_TIMEOUT) {
            return;
        }

        WSAResetEvent(m_listenEvent);

        WSANETWORKEVENTS networkEvents;
        if (WSAEnumNetworkEvents(m_listenSocket, m_listenEvent, &networkEvents) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "WSAEnumNetworkEvents error: " << WSAGetLastError();
            return;
        }

        if (networkEvents.lNetworkEvents & FD_ACCEPT) {
            if (networkEvents.iErrorCode[FD_ACCEPT_BIT] == 0) {
                SOCKET clientSocket = accept(m_listenSocket, NULL, NULL);
                if (clientSocket == INVALID_SOCKET) {
                    swCError(kSwLogCategory_SwTcpServer) << "accept failed: " << WSAGetLastError();
                } else {
                    SwTcpSocket* client = createSocketFromHandle(clientSocket);
                    m_pendingConnections.append(client);
                    emit newConnection();
                }
            } else {
                swCError(kSwLogCategory_SwTcpServer) << "FD_ACCEPT error: " << networkEvents.iErrorCode[FD_ACCEPT_BIT];
            }
        }

        if (networkEvents.lNetworkEvents & FD_CLOSE) {
            swCWarning(kSwLogCategory_SwTcpServer) << "Listen socket closed by external event.";
            close();
        }
    }

private:
    SOCKET m_listenSocket;
    WSAEVENT m_listenEvent;
    SwTimer* m_timer;
    SwList<SwTcpSocket*> m_pendingConnections;

    static void initializeWinsock() {
        static bool initialized = false;
        if (!initialized) {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2,2), &wsaData);
            if (result != 0) {
                swCError(kSwLogCategory_SwTcpServer) << "WSAStartup failed: " << result;
            } else {
                initialized = true;
            }
        }
    }

    SwTcpSocket* createSocketFromHandle(SOCKET sock) {
        SwTcpSocket* client = new SwTcpSocket();
        client->adoptSocket(sock);
        return client;
    }
};

#else // !_WIN32

#include <arpa/inet.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

class SwTcpServer : public SwObject {
    SW_OBJECT(SwTcpServer, SwObject)
public:
    SwTcpServer(SwObject* parent = nullptr)
        : SwObject(parent), m_listenSocket(-1)
    {
        m_timer = new SwTimer(50, this);
        connect(m_timer, SIGNAL(timeout), this, &SwTcpServer::onCheckEvents);
        m_timer->start();
    }

    virtual ~SwTcpServer() {
        close();
    }

    bool listen(uint16_t port) {
        close();

        m_listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_listenSocket < 0) {
            swCError(kSwLogCategory_SwTcpServer) << "socket failed: " << std::strerror(errno);
            return false;
    }
        int opt = 1;
        setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (!setNonBlocking(m_listenSocket)) {
            swCError(kSwLogCategory_SwTcpServer) << "failed to set non-blocking mode";
            close();
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            swCError(kSwLogCategory_SwTcpServer) << "bind failed: " << std::strerror(errno);
            close();
            return false;
        }

        if (::listen(m_listenSocket, SOMAXCONN) < 0) {
            swCError(kSwLogCategory_SwTcpServer) << "listen failed: " << std::strerror(errno);
            close();
            return false;
        }

        return true;
    }

    void close() {
        if (m_listenSocket >= 0) {
            ::close(m_listenSocket);
            m_listenSocket = -1;
        }
    }

    SwTcpSocket* nextPendingConnection() {
        if (m_pendingConnections.isEmpty()) {
            return nullptr;
        }
        SwTcpSocket* sock = m_pendingConnections.first();
        m_pendingConnections.removeFirst();
        return sock;
    }

signals:
    DECLARE_SIGNAL_VOID(newConnection)

private slots:
    void onCheckEvents() {
        if (m_listenSocket < 0) {
            return;
        }

        struct pollfd pfd{m_listenSocket, POLLIN, 0};
        int ret = ::poll(&pfd, 1, 0);
        if (ret <= 0) {
            return;
        }

        if (pfd.revents & POLLIN) {
            while (true) {
                int client = ::accept(m_listenSocket, nullptr, nullptr);
                if (client < 0) {
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        swCError(kSwLogCategory_SwTcpServer) << "accept failed: " << std::strerror(errno);
                    }
                    break;
                }

                SwTcpSocket* socket = createSocketFromHandle(client);
                if (socket) {
                    m_pendingConnections.append(socket);
                    emit newConnection();
                }
            }
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close();
        }
    }

private:
    bool setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            return false;
        }
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    SwTcpSocket* createSocketFromHandle(int fd) {
        SwTcpSocket* client = new SwTcpSocket();
        client->adoptSocket(fd);
        return client;
    }

    int m_listenSocket;
    SwTimer* m_timer;
    SwList<SwTcpSocket*> m_pendingConnections;
};

#endif // _WIN32
