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
#include "SwByteArray.h"
#include "SwDebug.h"
#include "SwMutex.h"
#include "SwList.h"
#include "SwPair.h"
#include "SwSocketTrafficTelemetry.h"

#include <atomic>
#include <cstring>
#include <utility>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
static constexpr const char* kSwLogCategory_SwUdpSocket = "sw.core.io.swudpsocket";


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
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
    struct ResolvedAddress {
        sockaddr_storage storage{};
        socklen_t length{0};
        int family{AF_UNSPEC};
        SwString address{};
        uint16_t port{0};
    };

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
        socketTrafficState_ = swSocketTrafficRegisterSocket(this, SwSocketTrafficTransportKind::Udp);
#if defined(_WIN32)
        WORD version = MAKEWORD(2, 2);
        WSAStartup(version, &m_wsaData);
        m_socket = INVALID_SOCKET;
#else
        m_socket = -1;
#endif
        std::memset(&m_remoteAddr, 0, sizeof(m_remoteAddr));
        std::memset(&m_boundAddr, 0, sizeof(m_boundAddr));
        m_remoteAddrLen = 0;
        m_boundAddrLen = 0;
        m_readBuffer.resize(m_maxDatagramSize);
    }

    /**
     * @brief Destroys the `SwUdpSocket` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwUdpSocket() override {
        close();
        swSocketTrafficUnregisterSocket(this);
        socketTrafficState_.reset();
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
        ResolvedAddress addr{};
        bool dualStack = false;
        if (!resolveBindAddress_(localAddress, port, addr, dualStack)) {
            setSocketError(SocketError::HostNotFoundError, SwString("Invalid bind address"));
            return false;
        }
        if (!ensureSocketForFamily_(addr.family, dualStack)) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return false;
        }

        applyBindMode(mode);
        if (!bindSocket(addr)) {
            setSocketError(SocketError::BoundError, SwString("Failed to bind socket"));
            return false;
        }

        m_boundAddr = addr.storage;
        m_boundAddrLen = addr.length;
        refreshLocalEndpoint_();
        m_state = SocketState::BoundState;
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_(true);
        return true;
    }

    /**
     * @brief Performs the `connectToHost` operation.
     * @param host Value passed to the method.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool connectToHost(const SwString& host, uint16_t port) {
        ResolvedAddress addr{};
        if (!resolveRemoteAddress_(host, port, addr)) {
            setSocketError(SocketError::HostNotFoundError, SwString("Invalid host address"));
            return false;
        }
        if (!ensureSocketForAddress_(addr)) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return false;
        }
        if (!coerceAddressForSocket_(addr)) {
            setSocketError(SocketError::OperationError, SwString("Address family mismatch"));
            return false;
        }
        m_remoteAddr = addr.storage;
        m_remoteAddrLen = addr.length;
        m_remoteAddress = addr.address;
        m_remotePort = addr.port;
        m_remoteSet = true;
        if (m_state == SocketState::UnconnectedState) {
            m_state = SocketState::ConnectedState;
        }
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_(true);
        return true;
    }

    /**
     * @brief Performs the `disconnectFromHost` operation.
     */
    void disconnectFromHost() {
        m_remoteSet = false;
        m_remoteAddress.clear();
        m_remotePort = 0;
        m_remoteAddrLen = 0;
        if (m_state == SocketState::ConnectedState) {
            m_state = isSocketValid() ? SocketState::BoundState : SocketState::UnconnectedState;
        }
        refreshTrafficMonitorEndpoints_(true);
    }

    bool joinMulticastGroup(const SwString& groupAddress,
                            const SwString& localInterface = SwString()) {
        return updateMulticastMembership_(groupAddress, localInterface, true);
    }

    bool leaveMulticastGroup(const SwString& groupAddress,
                             const SwString& localInterface = SwString()) {
        return updateMulticastMembership_(groupAddress, localInterface, false);
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
        ResolvedAddress target{};
        if (!resolveRemoteAddress_(host, port, target)) {
            setSocketError(SocketError::HostNotFoundError, SwString("Invalid host address"));
            return -1;
        }
        if (!ensureSocketForAddress_(target)) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return -1;
        }
        if (!coerceAddressForSocket_(target)) {
            setSocketError(SocketError::OperationError, SwString("Address family mismatch"));
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
        ResolvedAddress target{};
        target.storage = m_remoteAddr;
        target.length = m_remoteAddrLen;
        target.family = m_socketFamily;
        target.address = m_remoteAddress;
        target.port = m_remotePort;
        return sendDatagram(data, static_cast<size_t>(size), target);
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
        m_pendingDatagramCount.store(static_cast<uint64_t>(m_pending.size()), std::memory_order_relaxed);

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
        m_pendingDatagramCount.store(static_cast<uint64_t>(m_pending.size()), std::memory_order_relaxed);

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
     * @brief Returns the current pending Datagram count.
     * @return The current pending Datagram count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t pendingDatagramCount() const {
        return static_cast<size_t>(m_pendingDatagramCount.load(std::memory_order_relaxed));
    }

    /**
     * @brief Returns the total number of datagrams received from the OS.
     * @return The total number of datagrams received from the OS.
     */
    uint64_t totalReceivedDatagrams() const {
        return m_totalReceivedDatagrams.load();
    }

    uint64_t totalReceivedBytes() const {
        return m_totalReceivedBytes.load(std::memory_order_relaxed);
    }

    uint64_t totalSentBytes() const {
        return m_totalSentBytes.load(std::memory_order_relaxed);
    }

    uint64_t totalSentDatagrams() const {
        return m_totalSentDatagrams.load(std::memory_order_relaxed);
    }

    /**
     * @brief Returns the total number of datagrams dropped locally due to queue pressure.
     * @return The total number of locally dropped datagrams.
     */
    uint64_t droppedDatagrams() const {
        return m_totalQueueDrops.load();
    }

    /**
     * @brief Returns the maximum pending datagram depth reached since open.
     * @return The maximum observed pending datagram depth.
     */
    uint64_t queueHighWatermark() const {
        return m_queueHighWatermark.load();
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

    void setMaxReadBatchDatagrams(size_t maxPackets) {
        if (maxPackets == 0) {
            return;
        }
        m_maxReadBatchDatagrams = maxPackets;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() override {
        unregisterDispatcher_();
        swSocketTrafficSetOpenState(socketTrafficState_, false);
        if (!isSocketValid()) {
            m_readyReadPosted.store(false);
            m_pendingDatagramCount.store(0, std::memory_order_relaxed);
            publishTrafficMonitorUdpStats_(0);
            m_state = SocketState::UnconnectedState;
            m_remoteSet = false;
            m_remoteAddress.clear();
            m_remotePort = 0;
            m_remoteAddrLen = 0;
            m_boundAddress.clear();
            m_boundPort = 0;
            m_boundAddrLen = 0;
            m_socketFamily = AF_UNSPEC;
            m_dualStackEnabled = false;
            return;
        }
#if defined(_WIN32)
        if (m_event != WSA_INVALID_EVENT) {
            WSACloseEvent(m_event);
            m_event = WSA_INVALID_EVENT;
        }
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
#else
        ::close(m_socket);
        m_socket = -1;
#endif
        {
            SwMutexLocker lock(m_queueMutex);
            m_pending.clear();
            m_senderQueue.clear();
        }
        m_pendingDatagramCount.store(0, std::memory_order_relaxed);
        publishTrafficMonitorUdpStats_(0);
        m_totalReceivedDatagrams.store(0);
        m_totalQueueDrops.store(0);
        m_queueHighWatermark.store(0);
        m_readyReadPosted.store(false);
        m_state = SocketState::UnconnectedState;
        m_remoteSet = false;
        m_remoteAddress.clear();
        m_remotePort = 0;
        m_remoteAddrLen = 0;
        m_boundAddress.clear();
        m_boundPort = 0;
        m_boundAddrLen = 0;
        m_socketFamily = AF_UNSPEC;
        m_dualStackEnabled = false;
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
    bool isSocketValid() const {
#if defined(_WIN32)
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }

    bool ensureSocketForAddress_(const ResolvedAddress& address) {
        if (address.family == AF_UNSPEC) {
            return ensureSocketForFamily_(AF_INET6, true);
        }
        if (isSocketValid()) {
            if (m_socketFamily == address.family) {
                return true;
            }
            if (m_socketFamily == AF_INET6 && m_dualStackEnabled && address.family == AF_INET) {
                return true;
            }
            return false;
        }
        return ensureSocketForFamily_(address.family, false);
    }

    bool ensureSocketForFamily_(int family, bool dualStack) {
        if (family == AF_UNSPEC) {
            family = AF_INET6;
            dualStack = true;
        }
        if (isSocketValid()) {
            if (m_socketFamily == family) {
                return true;
            }
            if (m_socketFamily == AF_INET6 && m_dualStackEnabled && family == AF_INET) {
                return true;
            }
            return false;
        }
#if defined(_WIN32)
        m_socket = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return false;
        }
        u_long mode = 1;
        ioctlsocket(m_socket, FIONBIO, &mode);
#else
        m_socket = ::socket(family, SOCK_DGRAM, 0);
        if (m_socket < 0) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return false;
        }
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags != -1) {
            fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        }
#endif
        m_socketFamily = family;
        m_dualStackEnabled = false;
        if (family == AF_INET6) {
            const int v6Only = dualStack ? 0 : 1;
#if defined(_WIN32)
            const int rc = ::setsockopt(m_socket,
                                        IPPROTO_IPV6,
                                        IPV6_V6ONLY,
                                        reinterpret_cast<const char*>(&v6Only),
                                        sizeof(v6Only));
#else
            const int rc = ::setsockopt(m_socket,
                                        IPPROTO_IPV6,
                                        IPV6_V6ONLY,
                                        &v6Only,
                                        sizeof(v6Only));
#endif
            m_dualStackEnabled = (rc == 0 && dualStack);
        }
        applyReceiveBufferSize();
        m_totalReceivedDatagrams.store(0);
        m_totalQueueDrops.store(0);
        m_queueHighWatermark.store(0);
        m_pendingDatagramCount.store(0, std::memory_order_relaxed);
        registerDispatcher_();
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_(true);
        return true;
    }

    bool refreshLocalEndpoint_() {
        if (!isSocketValid()) {
            return false;
        }
        sockaddr_storage address {};
#if defined(_WIN32)
        int length = sizeof(address);
        if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return false;
        }
#else
        socklen_t length = sizeof(address);
        if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return false;
        }
#endif
        const SwString nextBoundAddress = socketAddressToString_(address);
        const uint16_t nextBoundPort = socketAddressPort_(address);
        const bool changed = (m_boundAddress != nextBoundAddress) || (m_boundPort != nextBoundPort);
        m_boundAddr = address;
        m_boundAddrLen = static_cast<socklen_t>(length);
        m_boundAddress = nextBoundAddress;
        m_boundPort = nextBoundPort;
        return changed;
    }

    void refreshTrafficMonitorEndpoints_(bool force = false) {
        if (!force &&
            m_publishedBoundAddress == m_boundAddress &&
            m_publishedBoundPort == m_boundPort &&
            m_publishedRemoteAddress == m_remoteAddress &&
            m_publishedRemotePort == m_remotePort) {
            return;
        }
        swSocketTrafficUpdateEndpoints(socketTrafficState_,
                                       m_boundAddress,
                                       m_boundPort,
                                       m_remoteAddress,
                                       m_remotePort);
        m_publishedBoundAddress = m_boundAddress;
        m_publishedBoundPort = m_boundPort;
        m_publishedRemoteAddress = m_remoteAddress;
        m_publishedRemotePort = m_remotePort;
    }

    void publishTrafficMonitorUdpStats_(unsigned long long pendingDatagramCount) {
        swSocketTrafficUpdateUdpStats(socketTrafficState_,
                                      m_totalReceivedDatagrams.load(std::memory_order_relaxed),
                                      m_totalSentDatagrams.load(std::memory_order_relaxed),
                                      m_totalQueueDrops.load(std::memory_order_relaxed),
                                      m_queueHighWatermark.load(std::memory_order_relaxed),
                                      pendingDatagramCount);
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

    bool bindSocket(const ResolvedAddress& addr) {
        if (!isSocketValid()) {
            return false;
        }
        const bool ok = (::bind(m_socket,
                                reinterpret_cast<const sockaddr*>(&addr.storage),
                                static_cast<int>(addr.length)) == 0);
        if (!ok) {
            setSocketError(SocketError::BoundError, SwString("Bind operation failed"));
        }
        return ok;
    }

    int64_t sendDatagram(const char* data, size_t size, const ResolvedAddress& target) {
        if (!isSocketValid() || !data || size == 0) {
            return -1;
        }
        int sent = ::sendto(m_socket, data, static_cast<int>(size), 0,
                            reinterpret_cast<const sockaddr*>(&target.storage),
                            static_cast<int>(target.length));
        if (sent < 0) {
            setSocketError(SocketError::OperationError, SwString("sendto failed"));
            return -1;
        }
        if (sent > 0) {
            const bool localEndpointChanged = refreshLocalEndpoint_();
            m_totalSentBytes.fetch_add(static_cast<uint64_t>(sent), std::memory_order_relaxed);
            m_totalSentDatagrams.fetch_add(1, std::memory_order_relaxed);
            swSocketTrafficAddSentBytes(socketTrafficState_, static_cast<unsigned long long>(sent));
            publishTrafficMonitorUdpStats_(m_pendingDatagramCount.load(std::memory_order_relaxed));
            if (localEndpointChanged) {
                refreshTrafficMonitorEndpoints_();
            }
        }
        return static_cast<int64_t>(sent);
    }

    SwString socketAddressToString_(const sockaddr_storage& addr) const {
        char buffer[INET6_ADDRSTRLEN] = {0};
        if (addr.ss_family == AF_INET) {
            const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&addr);
#if defined(_WIN32)
            InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), buffer, sizeof(buffer));
#else
            inet_ntop(AF_INET, const_cast<in_addr*>(&ipv4->sin_addr), buffer, sizeof(buffer));
#endif
            return SwString(buffer);
        }
        if (addr.ss_family == AF_INET6) {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&addr);
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

    uint16_t socketAddressPort_(const sockaddr_storage& addr) const {
        if (addr.ss_family == AF_INET) {
            return ntohs(reinterpret_cast<const sockaddr_in*>(&addr)->sin_port);
        }
        if (addr.ss_family == AF_INET6) {
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&addr)->sin6_port);
        }
        return 0;
    }

    void pollSocket_(int timeoutMs) {
        if (!isSocketValid()) {
            return;
        }
        SW_UNUSED(timeoutMs)

        bool receivedAny = false;
        size_t batchCount = 0;
        while (true) {
            sockaddr_storage sender{};
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
            receivedAny = true;
            ++m_totalReceivedDatagrams;
            m_totalReceivedBytes.fetch_add(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
            swSocketTrafficAddReceivedBytes(socketTrafficState_, static_cast<unsigned long long>(bytes));
            uint64_t pendingDatagramCount = 0;
            const SwString senderAddress = socketAddressToString_(sender);
            const uint16_t senderPort = socketAddressPort_(sender);
            {
                SwMutexLocker lock(m_queueMutex);
                m_pending.append(SwByteArray(m_readBuffer.data(), static_cast<size_t>(bytes)));
                m_senderQueue.append(SwPair<SwString, uint16_t>(senderAddress, senderPort));
                uint64_t queueDepth = static_cast<uint64_t>(m_pending.size());
                if (queueDepth > m_queueHighWatermark.load()) {
                    m_queueHighWatermark.store(queueDepth);
                }
                if (m_pending.size() > m_maxPendingDatagrams) {
                    size_t droppedBytes = m_pending.firstRef().size();
                    m_pending.removeAt(0);
                    if (!m_senderQueue.isEmpty()) {
                        m_senderQueue.removeAt(0);
                    }
                    ++m_totalQueueDrops;
                    swCWarning(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] Dropping oldest datagram (" << droppedBytes
                                << " bytes) due to queue pressure (limit=" << m_maxPendingDatagrams << ")";
                    queueDepth = static_cast<uint64_t>(m_pending.size());
                }
                pendingDatagramCount = queueDepth;
                m_pendingDatagramCount.store(queueDepth, std::memory_order_relaxed);
            }
            auto rx = ++m_debugRxCount;
            if (rx <= 5 || (rx % 100) == 0) {
                swCDebug(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] rx bytes=" << bytes
                          << " from=" << senderAddress.toStdString()
                          << ":" << senderPort;
            }
            const bool localEndpointChanged = refreshLocalEndpoint_();
            publishTrafficMonitorUdpStats_(pendingDatagramCount);
            if (localEndpointChanged) {
                refreshTrafficMonitorEndpoints_();
            }

            ++batchCount;
            if (batchCount >= m_maxReadBatchDatagrams) {
                break;
            }
        }
        if (receivedAny) {
            scheduleReadyRead_();
        }
    }

    void registerDispatcher_() {
        unregisterDispatcher_();
        if (!isSocketValid()) {
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

#if defined(_WIN32)
        if (m_event == WSA_INVALID_EVENT) {
            m_event = WSACreateEvent();
            if (m_event == WSA_INVALID_EVENT) {
                setSocketError(SocketError::SocketAccessError, SwString("WSACreateEvent failed"));
                return;
            }
        }
        if (WSAEventSelect(m_socket, m_event, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
            setSocketError(SocketError::SocketAccessError, SwString("WSAEventSelect failed"));
            return;
        }
        m_dispatchToken = app->ioDispatcher().watchHandle(m_event,
                                                          [affinity](std::function<void()> task) mutable {
                                                              if (affinity && ThreadHandle::isLive(affinity) &&
                                                                  ThreadHandle::currentThread() != affinity) {
                                                                  affinity->postTask(std::move(task));
                                                                  return;
                                                              }
                                                              task();
                                                          },
                                                          [this]() {
            if (!SwObject::isLive(this) || !isSocketValid()) {
                return;
            }
            WSANETWORKEVENTS networkEvents{};
            if (WSAEnumNetworkEvents(m_socket, m_event, &networkEvents) == SOCKET_ERROR) {
                setSocketError(SocketError::OperationError, SwString("WSAEnumNetworkEvents failed"));
                return;
            }
            if (networkEvents.lNetworkEvents & FD_CLOSE) {
                close();
                return;
            }
            if (networkEvents.lNetworkEvents & FD_READ) {
                pollSocket_(0);
            }
        });
#else
        m_dispatchToken = app->ioDispatcher().watchFd(m_socket,
                                                      SwIoDispatcher::Readable,
                                                      [affinity](std::function<void()> task) mutable {
                                                          if (affinity && ThreadHandle::isLive(affinity) &&
                                                              ThreadHandle::currentThread() != affinity) {
                                                              affinity->postTask(std::move(task));
                                                              return;
                                                          }
                                                          task();
                                                      },
                                                      [this](uint32_t events) {
                                                          if (!SwObject::isLive(this) || !isSocketValid()) {
                                                              return;
                                                          }
                                                          if (events & (SwIoDispatcher::Error | SwIoDispatcher::Hangup)) {
                                                              close();
                                                              return;
                                                          }
                                                          if (events & SwIoDispatcher::Readable) {
                                                              pollSocket_(0);
                                                          }
                                                      });
#endif
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

    void scheduleReadyRead_() {
        if (m_readyReadPosted.exchange(true)) {
            return;
        }
        auto notify = [this]() {
            if (!SwObject::isLive(this)) {
                return;
            }
            m_readyReadPosted.store(false);
            if (!hasPendingDatagrams()) {
                return;
            }
            readyRead();
        };

        ThreadHandle* affinity = threadHandle();
        if (affinity && ThreadHandle::currentThread() != affinity) {
            affinity->postTask(std::move(notify));
            return;
        }
        notify();
    }

    SwString normalizeAddressText_(const SwString& text) const {
        SwString normalized = text.trimmed();
        if (normalized.startsWith("[") && normalized.endsWith("]") && normalized.size() > 2) {
            normalized = normalized.mid(1, static_cast<int>(normalized.size()) - 2);
        }
        return normalized;
    }

    bool resolveAddress_(const SwString& host,
                         uint16_t port,
                         bool passive,
                         int preferredFamily,
                         ResolvedAddress& out,
                         bool* dualStack = nullptr) const {
        if (dualStack) {
            *dualStack = false;
        }
        const SwString normalizedHost = normalizeAddressText_(host);
        const bool wildcardBind = passive && normalizedHost.isEmpty();
        std::string hostStd = normalizedHost.toStdString();
        std::string portStd = SwString::number(port).toStdString();
        addrinfo hints{};
        hints.ai_family = preferredFamily;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        if (wildcardBind) {
            hints.ai_flags |= AI_PASSIVE;
        }
        addrinfo* result = nullptr;
        const int rc = ::getaddrinfo(wildcardBind ? nullptr : hostStd.c_str(),
                                     portStd.c_str(),
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
            if (wildcardBind && it->ai_family == AF_INET6) {
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
        if (dualStack) {
            *dualStack = wildcardBind && out.family == AF_INET6;
        }
        ::freeaddrinfo(result);
        return true;
    }

    bool resolveBindAddress_(const SwString& localAddress,
                             uint16_t port,
                             ResolvedAddress& out,
                             bool& dualStack) const {
        const SwString normalized = normalizeAddressText_(localAddress);
        int preferredFamily = AF_UNSPEC;
        if (normalized.contains(":")) {
            preferredFamily = AF_INET6;
        } else if (normalized.contains(".")) {
            preferredFamily = AF_INET;
        }
        if (!resolveAddress_(normalized, port, true, preferredFamily, out, &dualStack)) {
            return false;
        }
        dualStack = dualStack || (normalized == "::" && out.family == AF_INET6);
        return true;
    }

    bool resolveRemoteAddress_(const SwString& host, uint16_t port, ResolvedAddress& out) const {
        return resolveAddress_(host, port, false, AF_UNSPEC, out, nullptr);
    }

    bool coerceAddressForSocket_(ResolvedAddress& address) const {
        if (!isSocketValid()) {
            return false;
        }
        if (m_socketFamily == address.family) {
            return true;
        }
        if (m_socketFamily != AF_INET6 || !m_dualStackEnabled || address.family != AF_INET) {
            return false;
        }
        const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address.storage);
        sockaddr_in6 mapped{};
        mapped.sin6_family = AF_INET6;
        mapped.sin6_port = ipv4->sin_port;
        mapped.sin6_addr.s6_addr[10] = 0xFF;
        mapped.sin6_addr.s6_addr[11] = 0xFF;
        std::memcpy(mapped.sin6_addr.s6_addr + 12, &ipv4->sin_addr, sizeof(ipv4->sin_addr));
        std::memset(&address.storage, 0, sizeof(address.storage));
        std::memcpy(&address.storage, &mapped, sizeof(mapped));
        address.length = sizeof(mapped);
        address.family = AF_INET6;
        return true;
    }

    bool resolveIpv4Interface_(const SwString& localInterface, in_addr& address) const {
        if (localInterface.isEmpty()) {
            address.s_addr = htonl(INADDR_ANY);
            return true;
        }
        ResolvedAddress resolved{};
        if (!resolveAddress_(localInterface, 0, false, AF_INET, resolved, nullptr) ||
            resolved.family != AF_INET) {
            return false;
        }
        address = reinterpret_cast<const sockaddr_in*>(&resolved.storage)->sin_addr;
        return true;
    }

    unsigned int resolveIpv6InterfaceIndex_(const SwString& localInterface) const {
        if (localInterface.isEmpty()) {
            return 0;
        }
        bool ok = false;
        const int numericIndex = localInterface.trimmed().toInt(&ok);
        if (ok && numericIndex >= 0) {
            return static_cast<unsigned int>(numericIndex);
        }
#if !defined(_WIN32)
        const std::string name = localInterface.trimmed().toStdString();
        return ::if_nametoindex(name.c_str());
#else
        return 0;
#endif
    }

    bool updateMulticastMembership_(const SwString& groupAddress,
                                    const SwString& localInterface,
                                    bool join) {
        ResolvedAddress group{};
        if (!resolveRemoteAddress_(groupAddress, 0, group)) {
            return false;
        }
        if (!isSocketValid() || m_socketFamily != group.family) {
            return false;
        }

        if (group.family == AF_INET) {
            ip_mreq request{};
            request.imr_multiaddr = reinterpret_cast<const sockaddr_in*>(&group.storage)->sin_addr;
            if (!resolveIpv4Interface_(localInterface, request.imr_interface)) {
                return false;
            }
            const int option = join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
#if defined(_WIN32)
            return ::setsockopt(m_socket,
                                IPPROTO_IP,
                                option,
                                reinterpret_cast<const char*>(&request),
                                sizeof(request)) == 0;
#else
            return ::setsockopt(m_socket, IPPROTO_IP, option, &request, sizeof(request)) == 0;
#endif
        }

        if (group.family == AF_INET6) {
            ipv6_mreq request{};
            request.ipv6mr_multiaddr = reinterpret_cast<const sockaddr_in6*>(&group.storage)->sin6_addr;
            request.ipv6mr_interface = resolveIpv6InterfaceIndex_(localInterface);
            const int option = join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP;
#if defined(_WIN32)
            return ::setsockopt(m_socket,
                                IPPROTO_IPV6,
                                option,
                                reinterpret_cast<const char*>(&request),
                                sizeof(request)) == 0;
#else
            return ::setsockopt(m_socket, IPPROTO_IPV6, option, &request, sizeof(request)) == 0;
#endif
        }
        return false;
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
    WSAEVENT m_event{WSA_INVALID_EVENT};
#else
    int m_socket;
#endif
    size_t m_dispatchToken{0};
    int m_socketFamily{AF_UNSPEC};
    bool m_dualStackEnabled{false};
    sockaddr_storage m_remoteAddr{};
    socklen_t m_remoteAddrLen{0};
    sockaddr_storage m_boundAddr{};
    socklen_t m_boundAddrLen{0};
    bool m_remoteSet{false};
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
    std::atomic<uint64_t> m_totalReceivedBytes{0};
    std::atomic<uint64_t> m_totalSentBytes{0};
    std::atomic<uint64_t> m_totalSentDatagrams{0};
    std::atomic<uint64_t> m_totalReceivedDatagrams{0};
    std::atomic<uint64_t> m_totalQueueDrops{0};
    std::atomic<uint64_t> m_queueHighWatermark{0};
    std::atomic<uint64_t> m_pendingDatagramCount{0};
    std::atomic<bool> m_readyReadPosted{false};
    int m_receiveBufferSize{0};
    size_t m_maxDatagramSize{2048};
    size_t m_maxPendingDatagrams{512};
    size_t m_maxReadBatchDatagrams{128};
    SwByteArray m_readBuffer;
    SwSocketTrafficStateHandle socketTrafficState_;
    SwString m_publishedBoundAddress;
    uint16_t m_publishedBoundPort{0};
    SwString m_publishedRemoteAddress;
    uint16_t m_publishedRemotePort{0};
};
