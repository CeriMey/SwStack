#pragma once

/**
 * @file src/core/io/SwTcpSocket.h
 * @ingroup core_io
 * @brief TCP-only non-blocking socket used as the transport base for higher layers.
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

#include "SwAbstractSocket.h"
#include "SwByteArray.h"
#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwEventLoop.h"
#include "SwSocketTrafficTelemetry.h"
#include "SwTimer.h"

#include <algorithm>
#include <atomic>
#include <cstring>

static constexpr const char* kSwLogCategory_SwTcpSocket = "sw.core.io.swtcpsocket";

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/win/SwWindows.h"

#pragma comment(lib, "ws2_32.lib")

using SwNativeSocketHandle = SOCKET;
static constexpr SwNativeSocketHandle kSwInvalidSocketHandle = INVALID_SOCKET;

#else

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using SwNativeSocketHandle = int;
static constexpr SwNativeSocketHandle kSwInvalidSocketHandle = -1;

#endif

class SwTcpSocket : public SwAbstractSocket {
    SW_OBJECT(SwTcpSocket, SwAbstractSocket)

public:
    struct ResolvedAddress {
        sockaddr_storage storage{};
        socklen_t length{0};
        int family{AF_UNSPEC};
    };

    explicit SwTcpSocket(SwObject* parent = nullptr)
        : SwAbstractSocket(parent) {
        socketTrafficState_ = swSocketTrafficRegisterSocket(this, SwSocketTrafficTransportKind::Tcp);
#if defined(_WIN32)
        initializeWinsock_();
#endif
    }

    ~SwTcpSocket() override {
        close();
        swSocketTrafficUnregisterSocket(this);
        socketTrafficState_.reset();
    }

    bool connectToHost(const SwString& host, uint16_t port) override {
        close();
        m_lastHost = host;
        m_remoteClosed = false;

        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        const int resolveRc = ::getaddrinfo(host.toStdString().c_str(),
                                            SwString::number(port).toStdString().c_str(),
                                            &hints,
                                            &result);
        if (resolveRc != 0 || !result) {
#if defined(_WIN32)
            emit errorOccurred(WSAHOST_NOT_FOUND);
#else
            emit errorOccurred(errno);
#endif
            return false;
        }

        int lastError = 0;
        bool connectedImmediately = false;

#if defined(_WIN32)
        for (auto* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            SOCKET candidateSocket =
                ::WSASocketW(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
            if (candidateSocket == INVALID_SOCKET) {
                lastError = WSAGetLastError();
                continue;
            }

            u_long mode = 1;
            if (::ioctlsocket(candidateSocket, FIONBIO, &mode) == SOCKET_ERROR) {
                lastError = WSAGetLastError();
                ::closesocket(candidateSocket);
                continue;
            }

            WSAEVENT candidateEvent = ::WSACreateEvent();
            if (candidateEvent == WSA_INVALID_EVENT) {
                lastError = WSAGetLastError();
                ::closesocket(candidateSocket);
                continue;
            }

            if (::WSAEventSelect(candidateSocket, candidateEvent, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) ==
                SOCKET_ERROR) {
                lastError = WSAGetLastError();
                ::WSACloseEvent(candidateEvent);
                ::closesocket(candidateSocket);
                continue;
            }

            const int connectResult = ::connect(candidateSocket,
                                                ptr->ai_addr,
                                                static_cast<int>(ptr->ai_addrlen));
            if (connectResult == 0) {
                m_socket = candidateSocket;
                m_event = candidateEvent;
                connectedImmediately = true;
                break;
            }

            lastError = WSAGetLastError();
            if (lastError == WSAEWOULDBLOCK || lastError == WSAEINPROGRESS) {
                m_socket = candidateSocket;
                m_event = candidateEvent;
                break;
            }

            ::WSACloseEvent(candidateEvent);
            ::closesocket(candidateSocket);
        }
#else
        int fd = -1;
        for (auto* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            fd = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (fd < 0) {
                lastError = errno;
                continue;
            }
            setNonBlocking_(fd);
            if (::connect(fd, ptr->ai_addr, ptr->ai_addrlen) == 0) {
                m_connecting = false;
                connectedImmediately = true;
                break;
            }
            if (errno == EINPROGRESS) {
                m_connecting = true;
                break;
            }
            lastError = errno;
            ::close(fd);
            fd = -1;
        }
        m_socket = fd;
#endif
        ::freeaddrinfo(result);

#if defined(_WIN32)
        if (m_socket == INVALID_SOCKET) {
            emit errorOccurred(lastError);
            return false;
        }
#else
        if (m_socket < 0) {
            emit errorOccurred(lastError);
            return false;
        }
#endif

        if (!registerDispatcher_()) {
            close();
            return false;
        }

        setState(ConnectingState);

#if !defined(_WIN32)
        updateDispatcherInterest_();
        if (m_connecting) {
            return true;
        }
        if (!connectedImmediately) {
            return true;
        }
#else
        if (!connectedImmediately) {
            return true;
        }
#endif

        if (!handleTransportConnectedEvent_()) {
            close();
            return false;
        }
        return true;
    }

    bool waitForConnected(int msecs = 30000) override {
        if (state() == ConnectedState) {
            return true;
        }

        SwEventLoop loop;
        SwTimer timeoutTimer;
        bool success = false;

        auto complete = [&]() {
            success = (state() == ConnectedState);
            loop.quit();
        };

        connect(this, &SwTcpSocket::connected, &loop, complete);
        connect(this, &SwTcpSocket::disconnected, &loop, [&]() { loop.quit(); });
        connect(this, &SwTcpSocket::errorOccurred, &loop, [&](int) { loop.quit(); });

        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }

        loop.exec();
        return success || state() == ConnectedState;
    }

    void close() override {
        const SocketState oldState = state();

        unregisterDispatcher_();
        swSocketTrafficSetOpenState(socketTrafficState_, false);

#if defined(_WIN32)
        if (m_socket != INVALID_SOCKET) {
            disableLinger_();
            ::closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        if (m_event) {
            ::WSACloseEvent(m_event);
            m_event = NULL;
        }
#else
        if (m_socket >= 0) {
            disableLinger_();
            ::close(m_socket);
            m_socket = -1;
        }
        m_connecting = false;
#endif

        m_writeBuffer.clear();
        m_remoteClosed = false;

        if (oldState == ConnectedState || oldState == ConnectingState || oldState == ClosingState) {
            emit disconnected();
        }
        setState(UnconnectedState);
    }

    SwString read(int64_t maxSize = 0) override {
        if (!isSocketValid_() || state() != ConnectedState) {
            return "";
        }

        char buffer[1024];
        const int toRead =
            (maxSize > 0 && maxSize < static_cast<int64_t>(sizeof(buffer))) ? static_cast<int>(maxSize)
                                                                            : static_cast<int>(sizeof(buffer));
#if defined(_WIN32)
        const int ret = ::recv(m_socket, buffer, toRead, 0);
        if (ret > 0) {
            incrementTotalReceivedBytes_(static_cast<size_t>(ret));
            return SwString::fromLatin1(buffer, ret);
        }
        if (ret == 0) {
            m_remoteClosed = true;
            close();
            return "";
        }

        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            emit errorOccurred(err);
        }
#else
        const ssize_t ret = ::recv(m_socket, buffer, static_cast<size_t>(toRead), 0);
        if (ret > 0) {
            incrementTotalReceivedBytes_(static_cast<size_t>(ret));
            return SwString::fromLatin1(buffer, static_cast<int>(ret));
        }
        if (ret == 0) {
            m_remoteClosed = true;
            close();
            return "";
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            emit errorOccurred(errno);
        }
#endif
        return "";
    }

    bool write(const SwString& data) override {
        if (!isSocketValid_() || state() != ConnectedState) {
            return false;
        }
        m_writeBuffer.append(data.toStdString());
        onWriteQueued_();
        return true;
    }

    bool waitForBytesWritten(int msecs = 30000) override {
        if (m_writeBuffer.isEmpty()) {
            return true;
        }

        SwEventLoop loop;
        SwTimer timeoutTimer;
        bool success = false;

        auto tryComplete = [&]() {
            if (m_writeBuffer.isEmpty()) {
                success = true;
                loop.quit();
            }
        };

        connect(this, &SwTcpSocket::writeFinished, &loop, tryComplete);
        connect(this, &SwTcpSocket::disconnected, &loop, [&]() { loop.quit(); });
        connect(this, &SwTcpSocket::errorOccurred, &loop, [&](int) { loop.quit(); });

        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }

        onWriteQueued_();
        tryComplete();
        if (!success) {
            loop.exec();
        }
        return success;
    }

    unsigned long long totalReceivedBytes() const {
        return totalReceivedBytes_.load(std::memory_order_relaxed);
    }

    unsigned long long totalSentBytes() const {
        return totalSentBytes_.load(std::memory_order_relaxed);
    }

    SwString localAddress() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(false, address)) {
            return SwString();
        }
        return socketAddressToString_(address);
    }

    uint16_t localPort() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(false, address)) {
            return 0;
        }
        return socketAddressPort_(address);
    }

    SwString peerAddress() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(true, address)) {
            return SwString();
        }
        return socketAddressToString_(address);
    }

    uint16_t peerPort() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(true, address)) {
            return 0;
        }
        return socketAddressPort_(address);
    }

    bool shutdownWrite(int lingerSeconds = 5) {
        if (!isSocketValid_() || state() != ConnectedState) {
            return false;
        }

#if defined(_WIN32)
        if (::shutdown(m_socket, SD_SEND) == SOCKET_ERROR) {
            emit errorOccurred(WSAGetLastError());
            return false;
        }
#else
        if (::shutdown(m_socket, SHUT_WR) != 0) {
            emit errorOccurred(errno);
            return false;
        }
#endif
        enableLinger_(lingerSeconds);
        return true;
    }

    void adoptSocket(SwNativeSocketHandle sock, bool emitConnectedSignal = true) {
        close();
        m_socket = sock;
        if (!isSocketValid_()) {
            return;
        }

#if defined(_WIN32)
        u_long mode = 1;
        if (::ioctlsocket(m_socket, FIONBIO, &mode) == SOCKET_ERROR) {
            emit errorOccurred(WSAGetLastError());
            close();
            return;
        }

        m_event = ::WSACreateEvent();
        if (m_event == WSA_INVALID_EVENT) {
            emit errorOccurred(WSAGetLastError());
            close();
            return;
        }

        if (::WSAEventSelect(m_socket, m_event, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
            emit errorOccurred(WSAGetLastError());
            close();
            return;
        }
#else
        setNonBlocking_(m_socket);
        m_connecting = false;
#endif

        if (!registerDispatcher_()) {
            close();
            return;
        }
        updateDispatcherInterest_();

        setState(ConnectedState);
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        if (emitConnectedSignal) {
            emit connected();
        }
    }

    bool isRemoteClosed() const override {
        return m_remoteClosed;
    }

    bool hasPendingWrites() const override {
        return !m_writeBuffer.isEmpty();
    }

protected:
    SwNativeSocketHandle m_socket = kSwInvalidSocketHandle;
    SwByteArray m_writeBuffer;
    SwString m_lastHost;
    bool m_remoteClosed = false;
    size_t m_dispatchToken = 0;
    std::atomic<unsigned long long> totalReceivedBytes_{0};
    std::atomic<unsigned long long> totalSentBytes_{0};
    SwSocketTrafficStateHandle socketTrafficState_;

#if defined(_WIN32)
    WSAEVENT m_event = NULL;
#else
    bool m_connecting = false;
#endif

    bool isSocketValid_() const {
#if defined(_WIN32)
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }

    intptr_t nativeSocket_() const {
        return static_cast<intptr_t>(m_socket);
    }

    virtual bool handleTransportConnectedEvent_() {
        setState(ConnectedState);
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        emit connected();
        return true;
    }

    virtual bool handleTransportReadableEvent_() {
        emit readyRead();
        return true;
    }

    virtual bool handleTransportWritableEvent_() {
        tryFlushWriteBuffer_();
        return true;
    }

    virtual void handleTransportCloseEvent_() {
        m_remoteClosed = true;
        emit readyRead();
    }

    virtual bool shouldCloseAfterTransportClose_() const {
        return m_remoteClosed && m_writeBuffer.isEmpty();
    }

    virtual void onWriteQueued_() {
        tryFlushWriteBuffer_();
    }

    virtual void tryFlushWriteBuffer_() {
        if (!isSocketValid_() || state() != ConnectedState || m_writeBuffer.isEmpty()) {
            return;
        }

#if defined(_WIN32)
        const int sent = ::send(m_socket, m_writeBuffer.data(), static_cast<int>(m_writeBuffer.size()), 0);
        if (sent > 0) {
            incrementTotalSentBytes_(static_cast<size_t>(sent));
            m_writeBuffer.remove(0, sent);
            if (m_writeBuffer.isEmpty()) {
                emit writeFinished();
            }
            return;
        }
        if (sent == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                emit errorOccurred(err);
            }
        }
#else
        const ssize_t sent = ::send(m_socket, m_writeBuffer.data(), m_writeBuffer.size(), 0);
        if (sent > 0) {
            incrementTotalSentBytes_(static_cast<size_t>(sent));
            m_writeBuffer.remove(0, static_cast<int>(sent));
            if (m_writeBuffer.isEmpty()) {
                emit writeFinished();
            }
            return;
        }
        if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
            emit errorOccurred(errno);
            close();
        }
#endif
    }

#if !defined(_WIN32)
    virtual SwIoDispatcher::EventMask desiredDispatcherEvents_() const {
        uint32_t events = SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup;
        if (m_connecting || !m_writeBuffer.isEmpty()) {
            events |= SwIoDispatcher::Writable;
        }
        return events;
    }
#endif

    void updateDispatcherInterest_() {
#if !defined(_WIN32)
        if (!m_dispatchToken) {
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->ioDispatcher().updateFd(m_dispatchToken, desiredDispatcherEvents_());
        }
#endif
    }

    void closeIfRemoteClosedAndIdle_() {
        if (m_remoteClosed && shouldCloseAfterTransportClose_() && state() != UnconnectedState) {
            close();
        }
    }

    void incrementTotalReceivedBytes_(size_t bytes) {
        if (bytes == 0) {
            return;
        }
        totalReceivedBytes_.fetch_add(static_cast<unsigned long long>(bytes), std::memory_order_relaxed);
        swSocketTrafficAddReceivedBytes(socketTrafficState_, static_cast<unsigned long long>(bytes));
    }

    void incrementTotalSentBytes_(size_t bytes) {
        if (bytes == 0) {
            return;
        }
        totalSentBytes_.fetch_add(static_cast<unsigned long long>(bytes), std::memory_order_relaxed);
        swSocketTrafficAddSentBytes(socketTrafficState_, static_cast<unsigned long long>(bytes));
    }

    void refreshTrafficMonitorEndpoints_() {
        swSocketTrafficUpdateEndpoints(socketTrafficState_, localAddress(), localPort(), peerAddress(), peerPort());
    }

private:
    SwString socketAddressToString_(const sockaddr_storage& address) const {
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

    uint16_t socketAddressPort_(const sockaddr_storage& address) const {
        if (address.ss_family == AF_INET) {
            return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
        }
        if (address.ss_family == AF_INET6) {
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
        }
        return 0;
    }

    bool querySocketAddress_(bool peer, sockaddr_storage& address) const {
        if (!isSocketValid_()) {
            return false;
        }
        std::memset(&address, 0, sizeof(address));
#if defined(_WIN32)
        int length = sizeof(address);
        const int rc = peer ? ::getpeername(m_socket, reinterpret_cast<sockaddr*>(&address), &length)
                            : ::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length);
#else
        socklen_t length = sizeof(address);
        const int rc = peer ? ::getpeername(m_socket, reinterpret_cast<sockaddr*>(&address), &length)
                            : ::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length);
#endif
        return rc == 0;
    }

    bool registerDispatcher_() {
        unregisterDispatcher_();

#if defined(_WIN32)
        if (!m_event) {
            return false;
        }
#else
        if (!isSocketValid_()) {
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
            m_event,
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
                checkSocketEvents_();
            });
#else
        m_dispatchToken = app->ioDispatcher().watchFd(
            m_socket,
            desiredDispatcherEvents_(),
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) && ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this](uint32_t events) {
                if (!SwObject::isLive(this)) {
                    return;
                }
                pollEvents_(events);
                updateDispatcherInterest_();
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

#if defined(_WIN32)
    bool checkSocketEvents_() {
        if (!isSocketValid_() || m_event == NULL) {
            return true;
        }

        WSANETWORKEVENTS networkEvents {};
        if (::WSAEnumNetworkEvents(m_socket, m_event, &networkEvents) == SOCKET_ERROR) {
            emit errorOccurred(WSAGetLastError());
            return false;
        }

        if (networkEvents.lNetworkEvents & FD_CONNECT) {
            if (networkEvents.iErrorCode[FD_CONNECT_BIT] != 0) {
                emit errorOccurred(networkEvents.iErrorCode[FD_CONNECT_BIT]);
                close();
                return false;
            }
            if (!handleTransportConnectedEvent_()) {
                close();
                return false;
            }
        }

        if (networkEvents.lNetworkEvents & FD_READ) {
            if (networkEvents.iErrorCode[FD_READ_BIT] != 0) {
                emit errorOccurred(networkEvents.iErrorCode[FD_READ_BIT]);
                return false;
            }
            if (!handleTransportReadableEvent_()) {
                close();
                return false;
            }
        }

        if (networkEvents.lNetworkEvents & FD_WRITE) {
            if (networkEvents.iErrorCode[FD_WRITE_BIT] != 0) {
                emit errorOccurred(networkEvents.iErrorCode[FD_WRITE_BIT]);
                return false;
            }
            if (!handleTransportWritableEvent_()) {
                close();
                return false;
            }
        }

        if (networkEvents.lNetworkEvents & FD_CLOSE) {
            handleTransportCloseEvent_();
            closeIfRemoteClosedAndIdle_();
        }

        return true;
    }

    void enableLinger_(int seconds) {
        linger lng {};
        lng.l_onoff = 1;
        lng.l_linger = static_cast<u_short>(seconds);
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lng), sizeof(lng));
    }

    void disableLinger_() {
        if (!isSocketValid_()) {
            return;
        }
        linger lng {};
        lng.l_onoff = 0;
        lng.l_linger = 0;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lng), sizeof(lng));
    }

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
    void pollEvents_(uint32_t events) {
        if (!isSocketValid_()) {
            return;
        }

        const bool readable = (events & SwIoDispatcher::Readable) != 0;
        const bool writable = (events & SwIoDispatcher::Writable) != 0;
        const bool failed = (events & (SwIoDispatcher::Error | SwIoDispatcher::Hangup)) != 0;

        if (m_connecting && (writable || failed)) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                m_connecting = false;
                if (!handleTransportConnectedEvent_()) {
                    close();
                    return;
                }
            } else {
                emit errorOccurred(err != 0 ? err : errno);
                close();
                return;
            }
        }

        if (readable) {
            if (!handleTransportReadableEvent_()) {
                close();
                return;
            }
        }

        if (writable) {
            if (!handleTransportWritableEvent_()) {
                close();
                return;
            }
        }

        if (failed && !m_connecting) {
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) {
                emit errorOccurred(err);
                close();
                return;
            }
            handleTransportCloseEvent_();
            closeIfRemoteClosedAndIdle_();
        }
    }

    void setNonBlocking_(int fd) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    void enableLinger_(int seconds) {
        linger lng {};
        lng.l_onoff = 1;
        lng.l_linger = seconds;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
    }

    void disableLinger_() {
        if (!isSocketValid_()) {
            return;
        }
        linger lng {};
        lng.l_onoff = 0;
        lng.l_linger = 0;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
    }
#endif
};
