#pragma once

/**
 * @file src/core/io/SwUdpSocket.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwUdpSocket in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the UDP socket interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwUdpSocket.
 *
 * Socket-oriented declarations here abstract OS-level descriptors and expose the read, write,
 * connection, and readiness semantics that higher layers build upon.
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

#include "SwIODevice.h"
#include "SwString.h"
#include "SwTimer.h"
#include "SwByteArray.h"
#include "SwDebug.h"
#include "SwMutex.h"
#include "SwList.h"
#include "SwPair.h"

#include <atomic>
#include <cstring>
#include <utility>
#include <cstdint>
#include <algorithm>
#include <iostream>
static constexpr const char* kSwLogCategory_SwUdpSocket = "sw.core.io.swudpsocket";


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

class SwUdpSocket : public SwIODevice {
    SW_OBJECT(SwUdpSocket, SwIODevice)

public:
    enum class SocketState {
        UnconnectedState,
        BoundState,
        ConnectedState,
        ClosingState
    };

    enum class SocketError {
        UnknownSocketError = 0,
        SocketAccessError,
        BoundError,
        HostNotFoundError,
        OperationError
    };

    enum BindFlag : uint32_t {
        DefaultForPlatform = 0x0,
        ShareAddress = 0x1,
        DontShareAddress = 0x2,
        ReuseAddressHint = 0x4
    };
    using BindMode = uint32_t;

    /**
     * @brief Constructs a `SwUdpSocket` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwUdpSocket(SwObject* parent = nullptr)
        : SwIODevice(parent)
    {
#if defined(_WIN32)
        WORD version = MAKEWORD(2, 2);
        WSAStartup(version, &m_wsaData);
        m_socket = INVALID_SOCKET;
#else
        m_socket = -1;
#endif
        std::memset(&m_remoteAddr, 0, sizeof(m_remoteAddr));
        std::memset(&m_boundAddr, 0, sizeof(m_boundAddr));
        m_readBuffer.resize(m_maxDatagramSize);

        m_pollTimer = new SwTimer(5, this);
        SwObject::connect(m_pollTimer, &SwTimer::timeout, [this]() { pollSocket(); });
        m_pollTimer->start(5);
    }

    /**
     * @brief Destroys the `SwUdpSocket` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwUdpSocket() override {
        close();
        if (m_pollTimer) {
            m_pollTimer->stop();
        }
#if defined(_WIN32)
        WSACleanup();
#endif
    }

    /**
     * @brief Returns the current state.
     * @return The current state.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SocketState state() const { return m_state; }
    /**
     * @brief Returns the current error.
     * @return The current error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SocketError error() const { return m_error; }
    /**
     * @brief Returns the current error String.
     * @return The current error String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString errorString() const { return m_errorString; }
    /**
     * @brief Returns the current system Error.
     * @return The current system Error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int systemError() const { return m_lastSystemError; }

    /**
     * @brief Performs the `bind` operation.
     * @param port Local port used by the operation.
     * @param mode Mode value that controls the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool bind(uint16_t port, BindMode mode = DefaultForPlatform) {
        return bind(SwString(), port, mode);
    }

    /**
     * @brief Performs the `bind` operation.
     * @param localAddress Value passed to the method.
     * @param port Local port used by the operation.
     * @param mode Mode value that controls the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool bind(const SwString& localAddress, uint16_t port, BindMode mode = DefaultForPlatform) {
        ensureSocket();
        if (!isSocketValid()) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return false;
        }

        applyBindMode(mode);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = localAddress.isEmpty()
                                   ? htonl(INADDR_ANY)
                                   : inetAddr(localAddress);

        if (!bindSocket(addr)) {
            setSocketError(SocketError::BoundError, SwString("Failed to bind socket"));
            return false;
        }

        m_boundAddr = addr;
        m_boundAddress = localAddress.isEmpty() ? SwString("0.0.0.0") : localAddress;
        m_boundPort = port;
        m_state = SocketState::BoundState;
        return true;
    }

    /**
     * @brief Performs the `connectToHost` operation.
     * @param host Value passed to the method.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool connectToHost(const SwString& host, uint16_t port) {
        ensureSocket();
        if (!isSocketValid()) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inetAddr(host);
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            setSocketError(SocketError::HostNotFoundError, SwString("Invalid host address"));
            return false;
        }
        m_remoteAddr = addr;
        m_remoteAddress = host;
        m_remotePort = port;
        m_remoteSet = true;
        if (m_state == SocketState::UnconnectedState) {
            m_state = SocketState::ConnectedState;
        }
        return true;
    }

    /**
     * @brief Performs the `disconnectFromHost` operation.
     */
    void disconnectFromHost() {
        m_remoteSet = false;
        m_remoteAddress.clear();
        m_remotePort = 0;
        if (m_state == SocketState::ConnectedState) {
            m_state = isSocketValid() ? SocketState::BoundState : SocketState::UnconnectedState;
        }
    }

    /**
     * @brief Performs the `abort` operation.
     */
    void abort() { close(); }

    /**
     * @brief Performs the `writeDatagram` operation on the associated resource.
     * @param data Value passed to the method.
     * @param size Size value used by the operation.
     * @param host Value passed to the method.
     * @param port Local port used by the operation.
     * @return The requested datagram.
     */
    int64_t writeDatagram(const char* data, int64_t size, const SwString& host, uint16_t port) {
        if (!data || size <= 0) {
            return -1;
        }
        ensureSocket();
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        target.sin_addr.s_addr = inetAddr(host);
        if (target.sin_addr.s_addr == INADDR_NONE) {
            setSocketError(SocketError::HostNotFoundError, SwString("Invalid host address"));
            return -1;
        }
        return sendDatagram(data, static_cast<size_t>(size), target);
    }

    /**
     * @brief Performs the `writeDatagram` operation on the associated resource.
     * @param payload Value passed to the method.
     * @return The requested datagram.
     */
    int64_t writeDatagram(const SwByteArray& payload) {
        return writeDatagram(payload.constData(), static_cast<int64_t>(payload.size()));
    }

    /**
     * @brief Performs the `writeDatagram` operation on the associated resource.
     * @param payload Value passed to the method.
     * @return The requested datagram.
     */
    int64_t writeDatagram(const SwString& payload) {
        return writeDatagram(payload.data(), static_cast<int64_t>(payload.size()));
    }

    /**
     * @brief Performs the `writeDatagram` operation on the associated resource.
     * @param data Value passed to the method.
     * @param size Size value used by the operation.
     * @return The requested datagram.
     */
    int64_t writeDatagram(const char* data, int64_t size) {
        if (!m_remoteSet) {
            setSocketError(SocketError::OperationError, SwString("No remote host set"));
            return -1;
        }
        return sendDatagram(data, static_cast<size_t>(size), m_remoteAddr);
    }

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool write(const SwString& data) override {
        return writeDatagram(data) >= 0;
    }

    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param maxSize Value passed to the method.
     * @return The resulting read.
     */
    SwString read(int64_t maxSize = 0) override {
        if (!hasPendingDatagrams()) {
            return SwString();
        }
        const int sizeHint = pendingDatagramSize();
        SwByteArray buffer;
        buffer.resize(static_cast<size_t>((maxSize > 0) ? maxSize : sizeHint));
        char* raw = buffer.isEmpty() ? nullptr : buffer.data();
        const int64_t bytes = readDatagram(raw, static_cast<int64_t>(buffer.size()));
        if (bytes <= 0) {
            return SwString();
        }
        buffer.resize(static_cast<size_t>(bytes));
        return SwString(buffer.constData());
    }

    /**
     * @brief Performs the `readDatagram` operation on the associated resource.
     * @param data Value passed to the method.
     * @param maxSize Value passed to the method.
     * @param sender Value passed to the method.
     * @param senderPort Value passed to the method.
     * @return The resulting datagram.
     */
    int64_t readDatagram(char* data, int64_t maxSize, SwString* sender = nullptr, uint16_t* senderPort = nullptr) {
        SwMutexLocker lock(m_queueMutex);
        if (m_pending.isEmpty()) {
            return -1;
        }

        auto packet = std::move(m_pending.firstRef());
        m_pending.removeAt(0);

        SwString sourceAddress;
        uint16_t sourcePort = 0;
        if (!m_senderQueue.isEmpty()) {
            sourceAddress = m_senderQueue.firstRef().first;
            sourcePort = m_senderQueue.firstRef().second;
            m_senderQueue.removeAt(0);
        }
        if (sender) {
            *sender = sourceAddress;
        }
        if (senderPort) {
            *senderPort = sourcePort;
        }

        if (!data || maxSize <= 0) {
            return static_cast<int64_t>(packet.size());
        }

        const auto bytesToCopy = static_cast<size_t>(std::min<int64_t>(maxSize, static_cast<int64_t>(packet.size())));
        std::memcpy(data, packet.data(), bytesToCopy);
        return static_cast<int64_t>(bytesToCopy);
    }

    /**
     * @brief Performs the `receiveDatagram` operation.
     * @param sender Value passed to the method.
     * @param senderPort Value passed to the method.
     * @return The requested receive Datagram.
     */
    SwByteArray receiveDatagram(SwString* sender = nullptr, uint16_t* senderPort = nullptr) {
        SwMutexLocker lock(m_queueMutex);
        if (m_pending.isEmpty()) {
            return SwByteArray();
        }

        auto packet = std::move(m_pending.firstRef());
        m_pending.removeAt(0);

        if (sender && !m_senderQueue.isEmpty()) {
            *sender = m_senderQueue.firstRef().first;
        }
        if (senderPort && !m_senderQueue.isEmpty()) {
            *senderPort = m_senderQueue.firstRef().second;
        }
        if (!m_senderQueue.isEmpty()) {
            m_senderQueue.removeAt(0);
        }

        return packet;
    }

    /**
     * @brief Returns whether the object reports pending Datagrams.
     * @return `true` when the object reports pending Datagrams; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasPendingDatagrams() const {
        SwMutexLocker lock(m_queueMutex);
        return !m_pending.isEmpty();
    }

    /**
     * @brief Returns the current pending Datagram Size.
     * @return The current pending Datagram Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int pendingDatagramSize() const {
        SwMutexLocker lock(m_queueMutex);
        return m_pending.isEmpty() ? 0 : static_cast<int>(m_pending.firstRef().size());
    }

    /**
     * @brief Returns the current local Address.
     * @return The current local Address.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString localAddress() const { return m_boundAddress; }
    /**
     * @brief Returns the current local Port.
     * @return The current local Port.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    uint16_t localPort() const { return m_boundPort; }
    /**
     * @brief Returns the current peer Address.
     * @return The current peer Address.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString peerAddress() const { return m_remoteAddress; }
    /**
     * @brief Returns the current peer Port.
     * @return The current peer Port.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    uint16_t peerPort() const { return m_remotePort; }

    /**
     * @brief Sets the receive Buffer Size.
     * @param bytes Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setReceiveBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_receiveBufferSize = bytes;
        if (isSocketValid()) {
            applyReceiveBufferSize();
        }
    }

    /**
     * @brief Sets the max Datagram Size.
     * @param bytes Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaxDatagramSize(size_t bytes) {
        if (bytes == 0) {
            return;
        }
        if (bytes > 65536) {
            bytes = 65536;
        }
        m_maxDatagramSize = bytes;
        if (m_readBuffer.size() < m_maxDatagramSize) {
            m_readBuffer.resize(m_maxDatagramSize);
        }
    }

    /**
     * @brief Sets the max Pending Datagrams.
     * @param maxPackets Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaxPendingDatagrams(size_t maxPackets) {
        if (maxPackets == 0) {
            return;
        }
        m_maxPendingDatagrams = maxPackets;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() override {
        if (!isSocketValid()) {
            return;
        }
#if defined(_WIN32)
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
#else
        ::close(m_socket);
        m_socket = -1;
#endif
        m_pending.clear();
        m_senderQueue.clear();
        m_state = SocketState::UnconnectedState;
        m_remoteSet = false;
        m_remoteAddress.clear();
        m_remotePort = 0;
    }

    /**
     * @brief Returns whether the object reports open.
     * @return `true` when the object reports open; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isOpen() const override {
        return isSocketValid();
    }

signals:
    DECLARE_SIGNAL(errorOccurred, int);

private:
    void ensureSocket() {
        if (isSocketValid()) {
            return;
        }
#if defined(_WIN32)
        m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return;
        }
        u_long mode = 1;
        ioctlsocket(m_socket, FIONBIO, &mode);
#else
        m_socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket < 0) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return;
        }
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags != -1) {
            fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        }
#endif
        applyReceiveBufferSize();
    }

    bool isSocketValid() const {
#if defined(_WIN32)
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }

    void applyBindMode(BindMode mode) {
#if defined(_WIN32)
        if (mode & DontShareAddress) {
            const BOOL exclusive = TRUE;
            setsockopt(m_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        }
        if (mode & (ShareAddress | ReuseAddressHint)) {
            const BOOL reuse = TRUE;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        }
#else
        if (mode & (ShareAddress | ReuseAddressHint)) {
            const int enable = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#if defined(SO_REUSEPORT)
            if (mode & ReuseAddressHint) {
                setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
            }
#endif
        }
#endif
    }

    bool bindSocket(const sockaddr_in& addr) {
        if (!isSocketValid()) {
            return false;
        }
        const bool ok = (::bind(m_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);
        if (!ok) {
            setSocketError(SocketError::BoundError, SwString("Bind operation failed"));
        }
        return ok;
    }

    int64_t sendDatagram(const char* data, size_t size, const sockaddr_in& target) {
        if (!isSocketValid() || !data || size == 0) {
            return -1;
        }
        int sent = ::sendto(m_socket, data, static_cast<int>(size), 0,
                            reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        if (sent < 0) {
            setSocketError(SocketError::OperationError, SwString("sendto failed"));
            return -1;
        }
        return static_cast<int64_t>(sent);
    }

    SwString senderToString(const sockaddr_in& addr) const {
        char buffer[INET_ADDRSTRLEN] = {0};
#if defined(_WIN32)
        InetNtopA(AF_INET, const_cast<IN_ADDR*>(&addr.sin_addr), buffer, sizeof(buffer));
#else
        inet_ntop(AF_INET, const_cast<in_addr*>(&addr.sin_addr), buffer, sizeof(buffer));
#endif
        return SwString(buffer);
    }

    void pollSocket() {
        if (!isSocketValid()) {
            return;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_socket, &readSet);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 0;
#if defined(_WIN32)
        int ready = select(0, &readSet, nullptr, nullptr, &tv);
#else
        int ready = select(m_socket + 1, &readSet, nullptr, nullptr, &tv);
#endif
        auto selTick = ++m_debugSelectCount;
        if (ready <= 0) {
            if (selTick <= 20 || (selTick % 200) == 0) {
                swCDebug(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] select ready=" << ready
                          << " bound=" << m_boundAddress.toStdString() << ":" << m_boundPort
                          << (ready < 0 ? (" err=" + SwString::number(lastErrorCode())).toStdString() : std::string());
            }
            return;
        }
        if (selTick <= 20 || (selTick % 200) == 0) {
            swCDebug(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] select ready=" << ready
                      << " bound=" << m_boundAddress.toStdString() << ":" << m_boundPort;
        }

        while (true) {
            sockaddr_in sender{};
#if defined(_WIN32)
            int senderLen = sizeof(sender);
            int bytes = recvfrom(m_socket, m_readBuffer.data(), static_cast<int>(m_maxDatagramSize), 0,
                                 reinterpret_cast<sockaddr*>(&sender), &senderLen);
#else
            socklen_t senderLen = sizeof(sender);
            int bytes = recvfrom(m_socket, m_readBuffer.data(), static_cast<int>(m_maxDatagramSize), 0,
                                 reinterpret_cast<sockaddr*>(&sender), &senderLen);
#endif
            if (bytes <= 0) {
                int err = lastErrorCode();
#if defined(_WIN32)
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
#else
                if (err != EWOULDBLOCK && err != EAGAIN) {
#endif
                    swCError(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] recvfrom error=" << err;
                }
                break;
            }
            {
                SwMutexLocker lock(m_queueMutex);
                m_pending.append(SwByteArray(m_readBuffer.data(), static_cast<size_t>(bytes)));
                m_senderQueue.append(SwPair<SwString, uint16_t>(senderToString(sender), ntohs(sender.sin_port)));
                if (m_pending.size() > m_maxPendingDatagrams) {
                    size_t droppedBytes = m_pending.firstRef().size();
                    m_pending.removeAt(0);
                    if (!m_senderQueue.isEmpty()) {
                        m_senderQueue.removeAt(0);
                    }
                    swCWarning(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] Dropping oldest datagram (" << droppedBytes
                                << " bytes) due to queue pressure (limit=" << m_maxPendingDatagrams << ")";
                }
            }
            auto rx = ++m_debugRxCount;
            if (rx <= 5 || (rx % 100) == 0) {
                swCDebug(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] rx bytes=" << bytes
                          << " from=" << senderToString(sender).toStdString()
                          << ":" << ntohs(sender.sin_port);
            }
            readyRead();
        }
    }

    uint32_t inetAddr(const SwString& text) const {
        if (text.isEmpty()) {
            return INADDR_ANY;
        }
#if defined(_WIN32)
        IN_ADDR addr{};
        if (InetPtonA(AF_INET, text.toStdString().c_str(), &addr) == 1) {
            return addr.S_un.S_addr;
        }
        return INADDR_NONE;
#else
        in_addr addr{};
        if (inet_pton(AF_INET, text.toStdString().c_str(), &addr) == 1) {
            return addr.s_addr;
        }
        return INADDR_NONE;
#endif
    }

    void setSocketError(SocketError error, const SwString& description) {
        m_error = error;
        m_errorString = description;
        m_lastSystemError = lastErrorCode();
        emit errorOccurred(static_cast<int>(error));
    }

    int lastErrorCode() const {
#if defined(_WIN32)
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    void applyReceiveBufferSize() {
        if (m_receiveBufferSize <= 0 || !isSocketValid()) {
            return;
        }
        int desired = m_receiveBufferSize;
#if defined(_WIN32)
        setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&desired),
                   sizeof(desired));
#else
        setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF,
                   &desired,
                   sizeof(desired));
#endif
    }

#if defined(_WIN32)
    SOCKET m_socket;
    WSADATA m_wsaData{};
#else
    int m_socket;
#endif
    sockaddr_in m_remoteAddr{};
    sockaddr_in m_boundAddr{};
    bool m_remoteSet{false};
    SwTimer* m_pollTimer{nullptr};
    mutable SwMutex m_queueMutex;
    SwList<SwByteArray> m_pending;
    SwList<SwPair<SwString, uint16_t>> m_senderQueue;
    SwString m_boundAddress;
    uint16_t m_boundPort{0};
    SwString m_remoteAddress;
    uint16_t m_remotePort{0};
    SocketState m_state{SocketState::UnconnectedState};
    SocketError m_error{SocketError::UnknownSocketError};
    SwString m_errorString;
    int m_lastSystemError{0};
    std::atomic<uint64_t> m_debugRxCount{0};
    std::atomic<uint64_t> m_debugSelectCount{0};
    int m_receiveBufferSize{0};
    size_t m_maxDatagramSize{2048};
    size_t m_maxPendingDatagrams{512};
    SwByteArray m_readBuffer;
};
