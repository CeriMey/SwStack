#pragma once

/**
 * @file src/core/io/SwTcpServer.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwTcpServer in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the TCP server interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwTcpServer.
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
#include "SwString.h"
#include "SwTcpSocket.h"
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
    /**
     * @brief Constructs a `SwTcpServer` instance.
     * @param parent Optional parent object that owns this instance.
     * @param NULL Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwTcpServer(SwObject* parent = nullptr)
        : SwObject(parent), m_listenSocket(INVALID_SOCKET), m_listenEvent(NULL)
    {
        initializeWinsock();
    }

    /**
     * @brief Destroys the `SwTcpServer` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwTcpServer() {
        close();
        disableTls();
    }

    /**
     * @brief Enables server-side TLS for all subsequently accepted connections.
     * @param certPath Path to the PEM certificate file.
     * @param keyPath Path to the PEM private key file.
     * @return true on success.
     */
    bool enableTls(const SwString& certPath, const SwString& keyPath) {
        disableTls();
        std::string err;
        m_sslCtx = SwBackendSsl::createServerContext(certPath.toStdString(), keyPath.toStdString(), err);
        if (!m_sslCtx) {
            swCError(kSwLogCategory_SwTcpServer) << "TLS init failed: " << err;
            return false;
        }
        m_tlsEnabled = true;
        return true;
    }

    /**
     * @brief Disables server-side TLS.
     */
    void disableTls() {
        if (m_sslCtx) {
            SwBackendSsl::freeServerContext(m_sslCtx);
            m_sslCtx = nullptr;
        }
        m_tlsEnabled = false;
    }

    /**
     * @brief Returns whether TLS is enabled for incoming connections.
     */
    bool isTlsEnabled() const {
        return m_tlsEnabled;
    }

    /**
     * @brief Starts listening for incoming traffic.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool listen(uint16_t port) {
        close();

        m_listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
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
        registerDispatcher_();

        return true;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() {
        unregisterDispatcher_();
        if (m_listenSocket != INVALID_SOCKET) {
            closesocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
        }
        if (m_listenEvent) {
            WSACloseEvent(m_listenEvent);
            m_listenEvent = NULL;
        }
    }

    /**
     * @brief Returns the current next Pending Connection.
     * @return The current next Pending Connection.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    /**
     * @brief Performs the `onCheckEvents` operation.
     */
    void onCheckEvents() {
        if (m_listenEvent == NULL || m_listenSocket == INVALID_SOCKET) {
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
                    handleAcceptedSocket_(clientSocket);
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
    SwList<SwTcpSocket*> m_pendingConnections;

    bool m_tlsEnabled = false;
    void* m_sslCtx = nullptr;
    size_t m_dispatchToken = 0;

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

    void registerDispatcher_() {
        unregisterDispatcher_();
        if (m_listenEvent == NULL) {
            return;
        }
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return;
        }
        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        m_dispatchToken = app->ioDispatcher().watchHandle(m_listenEvent,
                                                          [affinity](std::function<void()> task) mutable {
                                                              if (affinity && ThreadHandle::isLive(affinity) &&
                                                                  ThreadHandle::currentThread() != affinity) {
                                                                  affinity->postTask(std::move(task));
                                                                  return;
                                                              }
                                                              task();
                                                          },
                                                          [this]() {
            if (!SwObject::isLive(this)) {
                return;
            }
            onCheckEvents();
        });
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

    void handleAcceptedSocket_(SOCKET sock) {
        SwTcpSocket* client = new SwTcpSocket();
        client->adoptSocket(sock, !m_tlsEnabled || !m_sslCtx);

        if (!m_tlsEnabled || !m_sslCtx) {
            m_pendingConnections.append(client);
            emit newConnection();
            return;
        }

        std::shared_ptr<bool> completed(new bool(false));
        connect(client, &SwTcpSocket::connected, this, [this, client, completed]() {
            if (*completed) {
                return;
            }
            *completed = true;
            m_pendingConnections.append(client);
            emit newConnection();
        });
        auto failHandshake = [client, completed]() {
            if (*completed) {
                return;
            }
            *completed = true;
            client->close();
            client->deleteLater();
        };
        connect(client, &SwTcpSocket::errorOccurred, this, [failHandshake](int) { failHandshake(); });
        connect(client, &SwTcpSocket::disconnected, this, failHandshake);

        if (!client->startServerTls(m_sslCtx)) {
            failHandshake();
        }
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
    /**
     * @brief Constructs a `SwTcpServer` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwTcpServer(SwObject* parent = nullptr)
        : SwObject(parent), m_listenSocket(-1)
    {
    }

    /**
     * @brief Destroys the `SwTcpServer` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwTcpServer() {
        close();
        disableTls();
    }

    /**
     * @brief Enables server-side TLS for all subsequently accepted connections.
     * @param certPath Path to the PEM certificate file.
     * @param keyPath Path to the PEM private key file.
     * @return true on success.
     */
    bool enableTls(const SwString& certPath, const SwString& keyPath) {
        disableTls();
        std::string err;
        m_sslCtx = SwBackendSsl::createServerContext(certPath.toStdString(), keyPath.toStdString(), err);
        if (!m_sslCtx) {
            swCError(kSwLogCategory_SwTcpServer) << "TLS init failed: " << err;
            return false;
        }
        m_tlsEnabled = true;
        return true;
    }

    /**
     * @brief Disables server-side TLS.
     */
    void disableTls() {
        if (m_sslCtx) {
            SwBackendSsl::freeServerContext(m_sslCtx);
            m_sslCtx = nullptr;
        }
        m_tlsEnabled = false;
    }

    /**
     * @brief Returns whether TLS is enabled for incoming connections.
     */
    bool isTlsEnabled() const {
        return m_tlsEnabled;
    }

    /**
     * @brief Starts listening for incoming traffic.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
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

        registerDispatcher_();

        return true;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() {
        unregisterDispatcher_();
        if (m_listenSocket >= 0) {
            ::close(m_listenSocket);
            m_listenSocket = -1;
        }
    }

    /**
     * @brief Returns the current next Pending Connection.
     * @return The current next Pending Connection.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    /**
     * @brief Performs the `onCheckEvents` operation.
     */
    void onCheckEvents() {
        if (m_listenSocket < 0) {
            return;
        }

        while (true) {
            int clientFd = ::accept(m_listenSocket, nullptr, nullptr);
            if (clientFd < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    swCError(kSwLogCategory_SwTcpServer) << "accept failed: " << std::strerror(errno);
                }
                break;
            }

            handleAcceptedSocket_(clientFd);
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

    int m_listenSocket;
    SwList<SwTcpSocket*> m_pendingConnections;

    bool m_tlsEnabled = false;
    void* m_sslCtx = nullptr;
    size_t m_dispatchToken = 0;

    void registerDispatcher_() {
        unregisterDispatcher_();
        if (m_listenSocket < 0) {
            return;
        }
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return;
        }
        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        m_dispatchToken = app->ioDispatcher().watchFd(m_listenSocket,
                                                      SwIoDispatcher::Readable,
                                                      [affinity](std::function<void()> task) mutable {
                                                          if (affinity && ThreadHandle::isLive(affinity) &&
                                                              ThreadHandle::currentThread() != affinity) {
                                                              affinity->postTask(std::move(task));
                                                              return;
                                                          }
                                                          task();
                                                      },
                                                      [this](uint32_t) {
                                                          if (!SwObject::isLive(this)) {
                                                              return;
                                                          }
                                                          onCheckEvents();
                                                      });
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

    void handleAcceptedSocket_(int fd) {
        SwTcpSocket* client = new SwTcpSocket();
        client->adoptSocket(fd, !m_tlsEnabled || !m_sslCtx);

        if (!m_tlsEnabled || !m_sslCtx) {
            m_pendingConnections.append(client);
            emit newConnection();
            return;
        }

        std::shared_ptr<bool> completed(new bool(false));
        connect(client, &SwTcpSocket::connected, this, [this, client, completed]() {
            if (*completed) {
                return;
            }
            *completed = true;
            m_pendingConnections.append(client);
            emit newConnection();
        });
        auto failHandshake = [client, completed]() {
            if (*completed) {
                return;
            }
            *completed = true;
            client->close();
            client->deleteLater();
        };
        connect(client, &SwTcpSocket::errorOccurred, this, [failHandshake](int) { failHandshake(); });
        connect(client, &SwTcpSocket::disconnected, this, failHandshake);

        if (!client->startServerTls(m_sslCtx)) {
            failHandshake();
        }
    }
};

#endif // _WIN32
