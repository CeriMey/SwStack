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

#include "SwIODevice.h"
#include "SwString.h"
#include "SwTimer.h"
#include "SwByteArray.h"
#include "SwDebug.h"

#include <atomic>
#include <deque>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
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

    ~SwUdpSocket() override {
        close();
        if (m_pollTimer) {
            m_pollTimer->stop();
        }
#if defined(_WIN32)
        WSACleanup();
#endif
    }

    SocketState state() const { return m_state; }
    SocketError error() const { return m_error; }
    SwString errorString() const { return m_errorString; }
    int systemError() const { return m_lastSystemError; }

    bool bind(uint16_t port, BindMode mode = DefaultForPlatform) {
        return bind(SwString(), port, mode);
    }

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

    void disconnectFromHost() {
        m_remoteSet = false;
        m_remoteAddress.clear();
        m_remotePort = 0;
        if (m_state == SocketState::ConnectedState) {
            m_state = isSocketValid() ? SocketState::BoundState : SocketState::UnconnectedState;
        }
    }

    void abort() { close(); }

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

    int64_t writeDatagram(const SwByteArray& payload) {
        return writeDatagram(payload.constData(), static_cast<int64_t>(payload.size()));
    }

    int64_t writeDatagram(const SwString& payload) {
        return writeDatagram(payload.data(), static_cast<int64_t>(payload.size()));
    }

    int64_t writeDatagram(const char* data, int64_t size) {
        if (!m_remoteSet) {
            setSocketError(SocketError::OperationError, SwString("No remote host set"));
            return -1;
        }
        return sendDatagram(data, static_cast<size_t>(size), m_remoteAddr);
    }

    bool write(const SwString& data) override {
        return writeDatagram(data) >= 0;
    }

    SwString read(int64_t maxSize = 0) override {
        if (!hasPendingDatagrams()) {
            return SwString();
        }
        const int sizeHint = pendingDatagramSize();
        std::string buffer(static_cast<size_t>((maxSize > 0) ? maxSize : sizeHint), '\0');
        char* raw = buffer.empty() ? nullptr : &buffer[0];
        const int64_t bytes = readDatagram(raw, static_cast<int64_t>(buffer.size()));
        if (bytes <= 0) {
            return SwString();
        }
        buffer.resize(static_cast<size_t>(bytes));
        return SwString(buffer);
    }

    int64_t readDatagram(char* data, int64_t maxSize, SwString* sender = nullptr, uint16_t* senderPort = nullptr) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_pending.empty()) {
            return -1;
        }

        auto packet = std::move(m_pending.front());
        m_pending.pop_front();

        SwString sourceAddress;
        uint16_t sourcePort = 0;
        if (!m_senderQueue.empty()) {
            sourceAddress = m_senderQueue.front().first;
            sourcePort = m_senderQueue.front().second;
            m_senderQueue.pop_front();
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

    SwByteArray receiveDatagram(SwString* sender = nullptr, uint16_t* senderPort = nullptr) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_pending.empty()) {
            return SwByteArray();
        }

        auto packet = std::move(m_pending.front());
        m_pending.pop_front();

        if (sender && !m_senderQueue.empty()) {
            *sender = m_senderQueue.front().first;
        }
        if (senderPort && !m_senderQueue.empty()) {
            *senderPort = m_senderQueue.front().second;
        }
        if (!m_senderQueue.empty()) {
            m_senderQueue.pop_front();
        }

        return SwByteArray(packet.data(), static_cast<int>(packet.size()));
    }

    bool hasPendingDatagrams() const {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        return !m_pending.empty();
    }

    int pendingDatagramSize() const {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        return m_pending.empty() ? 0 : static_cast<int>(m_pending.front().size());
    }

    SwString localAddress() const { return m_boundAddress; }
    uint16_t localPort() const { return m_boundPort; }
    SwString peerAddress() const { return m_remoteAddress; }
    uint16_t peerPort() const { return m_remotePort; }

    void setReceiveBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_receiveBufferSize = bytes;
        if (isSocketValid()) {
            applyReceiveBufferSize();
        }
    }

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

    void setMaxPendingDatagrams(size_t maxPackets) {
        if (maxPackets == 0) {
            return;
        }
        m_maxPendingDatagrams = maxPackets;
    }

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
                          << (ready < 0 ? " err=" + std::to_string(lastErrorCode()) : "");
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
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_pending.emplace_back(m_readBuffer.begin(), m_readBuffer.begin() + bytes);
                m_senderQueue.emplace_back(senderToString(sender), ntohs(sender.sin_port));
                if (m_pending.size() > m_maxPendingDatagrams) {
                    size_t droppedBytes = m_pending.front().size();
                    m_pending.pop_front();
                    if (!m_senderQueue.empty()) {
                        m_senderQueue.pop_front();
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
    mutable std::mutex m_queueMutex;
    std::deque<std::vector<char>> m_pending;
    std::deque<std::pair<SwString, uint16_t>> m_senderQueue;
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
    std::vector<char> m_readBuffer;
};
