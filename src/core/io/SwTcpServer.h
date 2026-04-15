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
#include <netdb.h>
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
    struct ResolvedAddress {
        sockaddr_storage storage{};
        socklen_t length{0};
        int family{AF_UNSPEC};
        SwString address{};
        uint16_t port{0};
    };

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
        const SwString requestedBindAddress = normalizeAddressText_(bindAddress);
        if (isListening() && m_listenPort == port && m_requestedListenAddress == requestedBindAddress) {
            return true;
        }
        close();

        ResolvedAddress resolved{};
        bool dualStack = false;
        if (!resolveBindAddress_(requestedBindAddress, port, resolved, dualStack)) {
            swCError(kSwLogCategory_SwTcpServer) << "invalid bind address: " << requestedBindAddress;
            return false;
        }

        if (!createListenSocket_(resolved.family, dualStack)) {
            close();
            return false;
        }

        if (!bindResolvedAddress_(resolved)) {
            close();
            return false;
        }

        if (::listen(m_listenSocket, SOMAXCONN)
#if defined(_WIN32)
            == SOCKET_ERROR
#else
            < 0
#endif
        ) {
#if defined(_WIN32)
            swCError(kSwLogCategory_SwTcpServer) << "listen failed: " << WSAGetLastError();
#else
            swCError(kSwLogCategory_SwTcpServer) << "listen failed: " << std::strerror(errno);
#endif
            close();
            return false;
        }

        if (!registerDispatcher_()) {
            close();
            return false;
        }

        m_requestedListenAddress = requestedBindAddress;
        refreshLocalEndpoint_();
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
        m_requestedListenAddress.clear();
        m_listenFamily = AF_UNSPEC;
        m_dualStackEnabled = false;
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
    int m_listenFamily = AF_UNSPEC;
    bool m_dualStackEnabled = false;
    uint16_t m_listenPort = 0;
    SwString m_listenAddress;
    SwString m_requestedListenAddress;

#if defined(_WIN32)
    WSAEVENT m_listenEvent = NULL;
#endif

    static SwString normalizeAddressText_(const SwString& text) {
        SwString normalized = text.trimmed();
        if (normalized.startsWith("[") && normalized.endsWith("]") && normalized.size() > 2) {
            normalized = normalized.mid(1, static_cast<int>(normalized.size()) - 2);
        }
        return normalized;
    }

    static SwString socketAddressToString_(const sockaddr_storage& address) {
        char buffer[INET6_ADDRSTRLEN] = {0};
        if (address.ss_family == AF_INET) {
            const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
#if defined(_WIN32)
            InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), buffer, sizeof(buffer));
#else
            inet_ntop(AF_INET, const_cast<in_addr*>(&ipv4->sin_addr), buffer, sizeof(buffer));
#endif
            return SwString(buffer);
        }
        if (address.ss_family == AF_INET6) {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
            if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
                in_addr mapped{};
                std::memcpy(&mapped, ipv6->sin6_addr.s6_addr + 12, sizeof(mapped));
#if defined(_WIN32)
                InetNtopA(AF_INET, &mapped, buffer, sizeof(buffer));
#else
                inet_ntop(AF_INET, &mapped, buffer, sizeof(buffer));
#endif
                return SwString(buffer);
            }
#if defined(_WIN32)
            InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(&ipv6->sin6_addr), buffer, sizeof(buffer));
#else
            inet_ntop(AF_INET6, const_cast<in6_addr*>(&ipv6->sin6_addr), buffer, sizeof(buffer));
#endif
            return SwString(buffer);
        }
        return SwString();
    }

    static uint16_t socketAddressPort_(const sockaddr_storage& address) {
        if (address.ss_family == AF_INET) {
            return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
        }
        if (address.ss_family == AF_INET6) {
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
        }
        return 0;
    }

    bool resolveBindAddress_(const SwString& bindAddress,
                             uint16_t port,
                             ResolvedAddress& out,
                             bool& dualStack) const {
        dualStack = false;
        const SwString normalized = normalizeAddressText_(bindAddress);
        const bool wildcardBind = normalized.isEmpty();
        int preferredFamily = AF_UNSPEC;
        if (normalized.contains(":")) {
            preferredFamily = AF_INET6;
        } else if (normalized.contains(".")) {
            preferredFamily = AF_INET;
        }

        addrinfo hints{};
        hints.ai_family = preferredFamily;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        addrinfo* result = nullptr;
        const int rc = ::getaddrinfo(wildcardBind ? nullptr : normalized.toStdString().c_str(),
                                     SwString::number(port).toStdString().c_str(),
                                     &hints,
                                     &result);
        if (rc != 0 || !result) {
            return false;
        }

        const addrinfo* best = nullptr;
        for (const addrinfo* it = result; it != nullptr; it = it->ai_next) {
            if (it->ai_family != AF_INET && it->ai_family != AF_INET6) {
                continue;
            }
            if (!best) {
                best = it;
            }
            if (preferredFamily != AF_UNSPEC && it->ai_family == preferredFamily) {
                best = it;
                break;
            }
            if ((wildcardBind || normalized == "::") && it->ai_family == AF_INET6) {
                best = it;
                break;
            }
        }
        if (!best) {
            ::freeaddrinfo(result);
            return false;
        }

        // ResolvedAddress owns a SwString, so raw memset would corrupt its internals.
        out = ResolvedAddress{};
        std::memcpy(&out.storage, best->ai_addr, static_cast<size_t>(best->ai_addrlen));
        out.length = static_cast<socklen_t>(best->ai_addrlen);
        out.family = best->ai_family;
        out.address = socketAddressToString_(out.storage);
        out.port = socketAddressPort_(out.storage);
        dualStack = (best->ai_family == AF_INET6) && (wildcardBind || normalized == "::");
        ::freeaddrinfo(result);
        return true;
    }

    bool createListenSocket_(int family, bool dualStack) {
#if defined(_WIN32)
        m_listenSocket = ::WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (m_listenSocket == INVALID_SOCKET) {
            swCError(kSwLogCategory_SwTcpServer) << "WSASocket failed: " << WSAGetLastError();
            return false;
        }
        u_long mode = 1;
        ::ioctlsocket(m_listenSocket, FIONBIO, &mode);
#else
        m_listenSocket = ::socket(family, SOCK_STREAM, 0);
        if (m_listenSocket < 0) {
            swCError(kSwLogCategory_SwTcpServer) << "socket failed: " << std::strerror(errno);
            return false;
        }
        int reuse = 1;
        ::setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        setNonBlocking_(m_listenSocket);
#endif

        m_listenFamily = family;
        m_dualStackEnabled = false;
        if (family == AF_INET6) {
            const int v6Only = dualStack ? 0 : 1;
#if defined(_WIN32)
            const int rc = ::setsockopt(m_listenSocket,
                                        IPPROTO_IPV6,
                                        IPV6_V6ONLY,
                                        reinterpret_cast<const char*>(&v6Only),
                                        sizeof(v6Only));
#else
            const int rc = ::setsockopt(m_listenSocket, IPPROTO_IPV6, IPV6_V6ONLY, &v6Only, sizeof(v6Only));
#endif
            m_dualStackEnabled = (rc == 0 && dualStack);
        }

#if defined(_WIN32)
        m_listenEvent = ::WSACreateEvent();
        if (m_listenEvent == WSA_INVALID_EVENT) {
            swCError(kSwLogCategory_SwTcpServer) << "WSACreateEvent failed: " << WSAGetLastError();
            return false;
        }
        if (::WSAEventSelect(m_listenSocket, m_listenEvent, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR) {
            swCError(kSwLogCategory_SwTcpServer) << "WSAEventSelect failed: " << WSAGetLastError();
            return false;
        }
#endif
        return true;
    }

    bool bindResolvedAddress_(const ResolvedAddress& resolved) {
        if (::bind(m_listenSocket,
                   reinterpret_cast<const sockaddr*>(&resolved.storage),
                   static_cast<int>(resolved.length))
#if defined(_WIN32)
            == SOCKET_ERROR
#else
            < 0
#endif
        ) {
#if defined(_WIN32)
            swCError(kSwLogCategory_SwTcpServer) << "bind failed: " << WSAGetLastError();
#else
            swCError(kSwLogCategory_SwTcpServer) << "bind failed: " << std::strerror(errno);
#endif
            return false;
        }
        return true;
    }

    void refreshLocalEndpoint_() {
        if (m_listenSocket == kSwInvalidListenHandle) {
            m_listenAddress.clear();
            m_listenPort = 0;
            return;
        }
        sockaddr_storage address{};
#if defined(_WIN32)
        int length = sizeof(address);
        if (::getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return;
        }
#else
        socklen_t length = sizeof(address);
        if (::getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return;
        }
#endif
        m_listenAddress = socketAddressToString_(address);
        m_listenPort = socketAddressPort_(address);
    }

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
