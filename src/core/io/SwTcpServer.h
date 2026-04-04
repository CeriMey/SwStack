#pragma once

/**
 * @file src/core/io/SwTcpServer.h
 * @ingroup core_io
 * @brief TCP-only non-blocking listening socket with overridable accept hooks.
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

#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwList.h"
#include "SwObject.h"
#include "SwTcpSocket.h"

static constexpr const char* kSwLogCategory_SwTcpServer = "sw.core.io.swtcpserver";

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/win/SwWindows.h"

#pragma comment(lib, "ws2_32.lib")

using SwNativeListenHandle = SOCKET;
static constexpr SwNativeListenHandle kSwInvalidListenHandle = INVALID_SOCKET;

#else

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using SwNativeListenHandle = int;
static constexpr SwNativeListenHandle kSwInvalidListenHandle = -1;

#endif

class SwTcpServer : public SwObject {
    SW_OBJECT(SwTcpServer, SwObject)

public:
    explicit SwTcpServer(SwObject* parent = nullptr)
        : SwObject(parent) {
#if defined(_WIN32)
        initializeWinsock_();
#endif
    }

    ~SwTcpServer() override {
        close();
    }

    bool isListening() const {
        return m_listenSocket != kSwInvalidListenHandle;
    }

    uint16_t localPort() const {
        return m_listenPort;
    }

    SwString localAddress() const {
        return m_listenAddress;
    }

    bool listen(uint16_t port) {
        return listen(SwString(), port);
    }

    bool listen(const SwString& bindAddress, uint16_t port) {
        SwString effectiveBindAddress = bindAddress.trimmed();
        if (effectiveBindAddress.isEmpty()) {
            effectiveBindAddress = "0.0.0.0";
        }

        if (isListening() && m_listenPort == port && m_listenAddress == effectiveBindAddress) {
            return true;
        }
        close();

#if defined(_WIN32)
        m_listenSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (m_listenSocket == INVALID_SOCKET) {
            swCError(kSwLogCategory_SwTcpServer) << "WSASocket failed: " << WSAGetLastError();
            return false;
        }

        u_long mode = 1;
        ::ioctlsocket(m_listenSocket, FIONBIO, &mode);

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        if (effectiveBindAddress == "0.0.0.0") {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (::InetPtonA(AF_INET, effectiveBindAddress.toStdString().c_str(), &addr.sin_addr) != 1) {
            swCError(kSwLogCategory_SwTcpServer) << "invalid bind address: " << effectiveBindAddress;
            close();
            return false;
        }
        addr.sin_port = htons(port);

        if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "bind failed: " << WSAGetLastError();
            close();
            return false;
        }

        if (::listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "listen failed: " << WSAGetLastError();
            close();
            return false;
        }

        m_listenEvent = ::WSACreateEvent();
        if (m_listenEvent == WSA_INVALID_EVENT) {
            swCError(kSwLogCategory_SwTcpServer) << "WSACreateEvent failed: " << WSAGetLastError();
            close();
            return false;
        }

        if (::WSAEventSelect(m_listenSocket, m_listenEvent, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "WSAEventSelect failed: " << WSAGetLastError();
            close();
            return false;
        }
#else
        m_listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_listenSocket < 0) {
            swCError(kSwLogCategory_SwTcpServer) << "socket failed: " << std::strerror(errno);
            return false;
        }

        int opt = 1;
        ::setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setNonBlocking_(m_listenSocket);

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        if (effectiveBindAddress == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else if (::inet_pton(AF_INET, effectiveBindAddress.toStdString().c_str(), &addr.sin_addr) != 1) {
            swCError(kSwLogCategory_SwTcpServer) << "invalid bind address: " << effectiveBindAddress;
            close();
            return false;
        }
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
#endif

        if (!registerDispatcher_()) {
            close();
            return false;
        }

        m_listenPort = port;
        m_listenAddress = effectiveBindAddress;
        return true;
    }

    void close() {
        unregisterDispatcher_();

#if defined(_WIN32)
        if (m_listenSocket != INVALID_SOCKET) {
            ::closesocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
        }
        if (m_listenEvent) {
            ::WSACloseEvent(m_listenEvent);
            m_listenEvent = NULL;
        }
#else
        if (m_listenSocket >= 0) {
            ::close(m_listenSocket);
            m_listenSocket = -1;
        }
#endif
        m_listenPort = 0;
        m_listenAddress.clear();
    }

    virtual SwTcpSocket* nextPendingConnection() {
        if (m_pendingConnections.isEmpty()) {
            return nullptr;
        }
        SwTcpSocket* sock = m_pendingConnections.first();
        m_pendingConnections.removeFirst();
        return sock;
    }

signals:
    DECLARE_SIGNAL_VOID(newConnection)

protected:
    SwList<SwTcpSocket*> m_pendingConnections;

    virtual SwTcpSocket* createPendingSocket_() {
        return new SwTcpSocket();
    }

    virtual bool shouldEmitConnectedOnAdopt_(SwTcpSocket*) const {
        return true;
    }

    virtual bool finalizeAcceptedSocket_(SwTcpSocket* socket) {
        queuePendingConnection_(socket);
        return true;
    }

    void queuePendingConnection_(SwTcpSocket* socket) {
        if (!socket) {
            return;
        }
        m_pendingConnections.append(socket);
        emit newConnection();
    }

private:
    SwNativeListenHandle m_listenSocket = kSwInvalidListenHandle;
    size_t m_dispatchToken = 0;
    uint16_t m_listenPort = 0;
    SwString m_listenAddress = "0.0.0.0";

#if defined(_WIN32)
    WSAEVENT m_listenEvent = NULL;
#endif

    bool registerDispatcher_() {
        unregisterDispatcher_();

#if defined(_WIN32)
        if (m_listenEvent == NULL) {
            return false;
        }
#else
        if (m_listenSocket < 0) {
            return false;
        }
#endif

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return false;
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }

#if defined(_WIN32)
        m_dispatchToken = app->ioDispatcher().watchHandle(
            m_listenEvent,
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) && ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this]() {
                if (!SwObject::isLive(this)) {
                    return;
                }
                onCheckEvents_();
            });
#else
        m_dispatchToken = app->ioDispatcher().watchFd(
            m_listenSocket,
            SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup,
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) && ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this](uint32_t) {
                if (!SwObject::isLive(this)) {
                    return;
                }
                onCheckEvents_();
            });
#endif
        return m_dispatchToken != 0;
    }

    void unregisterDispatcher_() {
        if (!m_dispatchToken) {
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->ioDispatcher().remove(m_dispatchToken);
        }
        m_dispatchToken = 0;
    }

    void onCheckEvents_() {
#if defined(_WIN32)
        if (m_listenEvent == NULL || m_listenSocket == INVALID_SOCKET) {
            return;
        }

        WSANETWORKEVENTS networkEvents {};
        if (::WSAEnumNetworkEvents(m_listenSocket, m_listenEvent, &networkEvents) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "WSAEnumNetworkEvents error: " << WSAGetLastError();
            return;
        }

        if (networkEvents.lNetworkEvents & FD_ACCEPT) {
            if (networkEvents.iErrorCode[FD_ACCEPT_BIT] != 0) {
                swCError(kSwLogCategory_SwTcpServer) << "FD_ACCEPT error: " << networkEvents.iErrorCode[FD_ACCEPT_BIT];
            } else {
                while (true) {
                    SOCKET clientSocket = ::accept(m_listenSocket, NULL, NULL);
                    if (clientSocket == INVALID_SOCKET) {
                        const int acceptError = WSAGetLastError();
                        if (acceptError != WSAEWOULDBLOCK) {
                            swCError(kSwLogCategory_SwTcpServer) << "accept failed: " << acceptError;
                        }
                        break;
                    }
                    handleAcceptedSocket_(clientSocket);
                }
            }
        }

        if (networkEvents.lNetworkEvents & FD_CLOSE) {
            close();
        }
#else
        if (m_listenSocket < 0) {
            return;
        }

        while (true) {
            int clientFd = ::accept(m_listenSocket, nullptr, nullptr);
            if (clientFd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    swCError(kSwLogCategory_SwTcpServer) << "accept failed: " << std::strerror(errno);
                }
                break;
            }
            handleAcceptedSocket_(clientFd);
        }
#endif
    }

    void handleAcceptedSocket_(SwNativeSocketHandle socketHandle) {
        SwTcpSocket* client = createPendingSocket_();
        if (!client) {
#if defined(_WIN32)
            ::closesocket(socketHandle);
#else
            ::close(socketHandle);
#endif
            return;
        }

        client->adoptSocket(socketHandle, shouldEmitConnectedOnAdopt_(client));
        if (!finalizeAcceptedSocket_(client)) {
            client->close();
            client->deleteLater();
        }
    }

#if defined(_WIN32)
    static void initializeWinsock_() {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        WSADATA wsaData {};
        if (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            initialized = true;
        }
    }
#else
    void setNonBlocking_(int fd) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
#endif
};
