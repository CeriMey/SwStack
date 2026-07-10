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
#include "SwHostResolver.h"
#include "SwHash.h"
#include "SwMutex.h"
#include "SwSocketTrafficTelemetry.h"

#include <atomic>
#include <cstring>
#include <utility>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <limits>
#include <vector>
static constexpr const char* kSwLogCategory_SwUdpSocket = "sw.core.io.swudpsocket";


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#if !defined(SIO_UDP_CONNRESET)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
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

    struct PendingDatagram {
        std::vector<char> bytes;
        size_t size{0};
        size_t originalSize{0};
        bool truncated{false};
        sockaddr_storage sender{};

        PendingDatagram() = default;

        void ensureBuffer(size_t capacity) {
            if (bytes.size() < capacity) {
                bytes.resize(capacity);
            }
        }

        void assign(const char* data,
                    size_t length,
                    const sockaddr_storage& source,
                    size_t sourceLength = 0,
                    bool wasTruncated = false) {
            if (bytes.size() < length) {
                bytes.resize(length);
            }
            if (length > 0) {
                std::memcpy(bytes.data(), data, length);
            }
            size = length;
            originalSize = sourceLength > 0 ? sourceLength : length;
            truncated = wasTruncated;
            sender = source;
        }

        void commit(size_t length,
                    const sockaddr_storage& source,
                    size_t sourceLength,
                    bool wasTruncated) {
            size = length;
            originalSize = sourceLength;
            truncated = wasTruncated;
            sender = source;
        }

        void clearPayload() {
            size = 0;
            originalSize = 0;
            truncated = false;
        }

        void releaseBuffer() {
            std::vector<char>().swap(bytes);
            clearPayload();
        }
    };

public:
    using NativeSocketHandle = uintptr_t;
    using NativeSocketPrepareHandler =
        std::function<bool(NativeSocketHandle socket, SwString& error)>;
    using NativeSocketClosedHandler = std::function<void(NativeSocketHandle socket)>;

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
        OperationError,
        WouldBlockError
    };

    enum class DatagramIoStatus {
        Ok = 0,
        NoDatagram,
        WouldBlock,
        Truncated,
        Error
    };

    struct DatagramView {
        const char* data{nullptr};
        size_t size{0};
        size_t originalSize{0};
        bool truncated{false};
        sockaddr_storage sender{};
    };

    struct OwnedDatagram {
        std::vector<char> bytes{};
        size_t size{0};
        size_t originalSize{0};
        bool truncated{false};
        sockaddr_storage sender{};

        const char* data() const { return size > 0U ? bytes.data() : nullptr; }
    };

    using DatagramViewHandler = std::function<void(const DatagramView&)>;

    enum BindFlag : uint32_t {
        DefaultForPlatform = 0x0,
        ShareAddress = 0x1,
        DontShareAddress = 0x2,
        ReuseAddressHint = 0x4,
        // Wildcard "::" binds default to dual-stack (IPV6_V6ONLY=0), which
        // maps IPv4 senders to ::ffff:a.b.c.d strings. Ipv6Only keeps the
        // socket pure IPv6 so it can coexist with a separate IPv4 socket on
        // the same port (two-socket magicsock pattern).
        Ipv6Only = 0x8
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
     * Installs a direct platform hook executed once after native socket creation and before
     * bind/connect. Full-tunnel runtimes use it to keep infrastructure traffic out of the TUN.
     * A rejected hook closes the just-created socket and makes the operation fail synchronously.
     */
    void setNativeSocketLifecycleHandlers(NativeSocketPrepareHandler prepare,
                                          NativeSocketClosedHandler closed = {}) {
        m_nativeSocketPrepare = std::move(prepare);
        m_nativeSocketClosed = std::move(closed);
    }

    NativeSocketHandle nativeSocketHandle() const {
        if (!isSocketValid()) return static_cast<NativeSocketHandle>(-1);
        return static_cast<NativeSocketHandle>(m_socket);
    }

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
        if ((mode & Ipv6Only) && addr.family == AF_INET6) {
            dualStack = false;
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
        refreshTrafficMonitorEndpoints_();
        return true;
    }

    /**
     * @brief Performs the `connectToHost` operation.
     * @param host Value passed to the method.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool connectToHost(const SwString& host, uint16_t port) {
        // Cache-first DNS (SwHostResolver): substitute a known/just-resolved IP literal so
        // resolveRemoteAddress_ performs NO blocking DNS for an already-seen host. A cache
        // hit (or numeric IP) is instant — important because UDP callers (e.g. the mesh
        // datalink) send the bind datagram immediately after connectToHost returns.
        SwString connectHost = host;
        if (!SwHostResolver::isNumericHost(host)) {
            const SwString resolvedIp = SwHostResolver::instance().resolveBlockingAndCache(host);
            if (!resolvedIp.isEmpty()) {
                connectHost = resolvedIp;
            }
        }
        ResolvedAddress addr{};
        if (!resolveRemoteAddress_(connectHost, port, addr)) {
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
#if defined(_WIN32)
        configureUdpConnectionReset_(true);
#endif
        if (m_state == SocketState::UnconnectedState) {
            m_state = SocketState::ConnectedState;
        }
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        return true;
    }

    /**
     * @brief Performs the `disconnectFromHost` operation.
     */
    void disconnectFromHost() {
#if defined(_WIN32)
        configureUdpConnectionReset_(false);
#endif
        m_remoteSet = false;
        m_remoteAddress.clear();
        m_remotePort = 0;
        m_remoteAddrLen = 0;
        if (m_state == SocketState::ConnectedState) {
            m_state = isSocketValid() ? SocketState::BoundState : SocketState::UnconnectedState;
        }
        refreshTrafficMonitorEndpoints_();
    }

    bool joinMulticastGroup(const SwString& groupAddress,
                            const SwString& localInterface = SwString()) {
        return updateMulticastMembership_(groupAddress, localInterface, true);
    }

    bool leaveMulticastGroup(const SwString& groupAddress,
                             const SwString& localInterface = SwString()) {
        return updateMulticastMembership_(groupAddress, localInterface, false);
    }

    bool setMulticastTimeToLive(uint8_t ttl) {
        if (!isSocketValid()) {
            return false;
        }
        if (m_socketFamily == AF_INET) {
#if defined(_WIN32)
            DWORD value = ttl;
            return ::setsockopt(m_socket,
                                IPPROTO_IP,
                                IP_MULTICAST_TTL,
                                reinterpret_cast<const char*>(&value),
                                sizeof(value)) == 0;
#else
            unsigned char value = ttl;
            return ::setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_TTL, &value, sizeof(value)) == 0;
#endif
        }
        if (m_socketFamily == AF_INET6) {
            int value = static_cast<int>(ttl);
#if defined(_WIN32)
            return ::setsockopt(m_socket,
                                IPPROTO_IPV6,
                                IPV6_MULTICAST_HOPS,
                                reinterpret_cast<const char*>(&value),
                                sizeof(value)) == 0;
#else
            return ::setsockopt(m_socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &value, sizeof(value)) == 0;
#endif
        }
        return false;
    }

    bool setMulticastLoopbackEnabled(bool enabled) {
        if (!isSocketValid()) {
            return false;
        }
        if (m_socketFamily == AF_INET) {
#if defined(_WIN32)
            DWORD value = enabled ? 1U : 0U;
            return ::setsockopt(m_socket,
                                IPPROTO_IP,
                                IP_MULTICAST_LOOP,
                                reinterpret_cast<const char*>(&value),
                                sizeof(value)) == 0;
#else
            unsigned char value = enabled ? 1U : 0U;
            return ::setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &value, sizeof(value)) == 0;
#endif
        }
        if (m_socketFamily == AF_INET6) {
            int value = enabled ? 1 : 0;
#if defined(_WIN32)
            return ::setsockopt(m_socket,
                                IPPROTO_IPV6,
                                IPV6_MULTICAST_LOOP,
                                reinterpret_cast<const char*>(&value),
                                sizeof(value)) == 0;
#else
            return ::setsockopt(m_socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &value, sizeof(value)) == 0;
#endif
        }
        return false;
    }

    bool setMulticastInterface(const SwString& localInterface) {
        if (!isSocketValid()) {
            return false;
        }
        if (m_socketFamily == AF_INET) {
#if !defined(_WIN32)
            const std::string ifName = localInterface.trimmed().toStdString();
            if (!ifName.empty()) {
                const unsigned int index = ::if_nametoindex(ifName.c_str());
                if (index != 0U) {
                    ip_mreqn request{};
                    request.imr_ifindex = static_cast<int>(index);
                    return ::setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_IF, &request, sizeof(request)) == 0;
                }
            }
#endif
            in_addr address{};
            if (!resolveIpv4Interface_(localInterface, address)) {
                return false;
            }
#if defined(_WIN32)
            return ::setsockopt(m_socket,
                                IPPROTO_IP,
                                IP_MULTICAST_IF,
                                reinterpret_cast<const char*>(&address),
                                sizeof(address)) == 0;
#else
            return ::setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_IF, &address, sizeof(address)) == 0;
#endif
        }
        if (m_socketFamily == AF_INET6) {
            const unsigned int index = resolveIpv6InterfaceIndex_(localInterface);
            if (!localInterface.isEmpty() && index == 0U) {
                return false;
            }
#if defined(_WIN32)
            return ::setsockopt(m_socket,
                                IPPROTO_IPV6,
                                IPV6_MULTICAST_IF,
                                reinterpret_cast<const char*>(&index),
                                sizeof(index)) == 0;
#else
            return ::setsockopt(m_socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, &index, sizeof(index)) == 0;
#endif
        }
        return false;
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
        if (size < 0 || (!data && size > 0)) {
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
     * @brief Pre-resolves a host:port into a ResolvedAddress the caller can cache.
     * @param host Destination host (DNS name or numeric literal).
     * @param port Destination port.
     * @param out Receives the resolved address on success.
     * @return `true` on success; otherwise `false`.
     *
     * @details Pairs with `writeDatagram(data, size, const ResolvedAddress&)` to keep
     * name resolution off the per-datagram path: resolve a destination once (ideally
     * off the event-loop thread, since DNS may block) and send to the cached result
     * many times. A numeric literal resolves without touching the network. Unlike
     * QUdpSocket — which resolves inside every writeDatagram — this makes the
     * resolve-once/send-many pattern first-class, which a non-connected socket
     * talking to a fixed peer (e.g. a mesh relay) needs to avoid per-send getaddrinfo.
     */
    bool resolveHostAddress(const SwString& host, uint16_t port, ResolvedAddress& out) const {
        return resolveRemoteAddress_(host, port, out);
    }

    int64_t writeDatagram(const char* data, int64_t size, const ResolvedAddress& target) {
        if (size < 0 || (!data && size > 0) || target.length == 0 || target.family == AF_UNSPEC) {
            return -1;
        }
        if (!ensureSocketForAddress_(target)) {
            setSocketError(SocketError::SocketAccessError, SwString("Socket creation failed"));
            return -1;
        }
        if (m_socketFamily == target.family) {
            return sendDatagram(data, static_cast<size_t>(size), target);
        }
        ResolvedAddress resolved = target;
        if (!coerceAddressForSocket_(resolved)) {
            setSocketError(SocketError::OperationError, SwString("Address family mismatch"));
            return -1;
        }
        return sendDatagram(data, static_cast<size_t>(size), resolved);
    }

    // Fast path for a stable UDP peer: resolve host:port once per socket, then reuse the native
    // sockaddr for every datagram. This is the intended path for QUIC connections; it keeps DNS/
    // inet_pton and address-family coercion out of the per-packet hot path.
    int64_t writeDatagramCached(const char* data, int64_t size,
                                const SwString& host, uint16_t port) {
        SwString key = host;
        key.push_back('\0');
        key.push_back(static_cast<char>((port >> 8) & 0xffU));
        key.push_back(static_cast<char>(port & 0xffU));

        auto it = m_resolvedAddressCache.find(key);
        if (it == m_resolvedAddressCache.end()) {
            ResolvedAddress target{};
            if (!resolveRemoteAddress_(host, port, target)) {
                setSocketError(SocketError::HostNotFoundError, SwString("Invalid host address"));
                return -1;
            }
            it = m_resolvedAddressCache.emplace(std::move(key), std::move(target)).first;
        }
        return writeDatagram(data, size, it->second);
    }

    void clearResolvedAddressCache() { m_resolvedAddressCache.clear(); }

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
        if (size < 0 || (!data && size > 0)) {
            return -1;
        }
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
        return write(SwByteArray(data.data(), data.size()));
    }

    bool write(const SwByteArray& data) override {
        return writeDatagram(data) >= 0;
    }

    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param maxSize Value passed to the method.
     * @return The resulting read.
     */
    SwByteArray read(int64_t maxSize = 0) override {
        if (!hasPendingDatagrams()) {
            return SwByteArray();
        }
        const int sizeHint = pendingDatagramSize();
        SwByteArray buffer;
        buffer.resize(static_cast<size_t>((maxSize > 0) ? maxSize : sizeHint));
        char* raw = buffer.isEmpty() ? nullptr : buffer.data();
        const int64_t bytes = readDatagram(raw, static_cast<int64_t>(buffer.size()));
        if (bytes <= 0) {
            return SwByteArray();
        }
        buffer.resize(static_cast<size_t>(bytes));
        return buffer;
    }

    /**
     * @brief Performs the `readDatagram` operation on the associated resource.
     * @param data Value passed to the method.
     * @param maxSize Value passed to the method.
     * @param sender Value passed to the method.
     * @param senderPort Value passed to the method.
     * @return The resulting datagram.
     */
    int64_t readDatagram(char* data,
                         int64_t maxSize,
                         SwString* sender = nullptr,
                         uint16_t* senderPort = nullptr,
                         bool* truncated = nullptr,
                         size_t* originalSize = nullptr,
                         sockaddr_storage* nativeSender = nullptr) {
        sockaddr_storage source{};
        size_t datagramSize = 0;
        size_t sourceSize = 0;
        bool wasTruncated = false;
        size_t bytesToCopy = 0;
        {
            SwMutexLocker lock(m_queueMutex);
            if (pendingEmptyLocked_()) {
                m_lastDatagramIoStatus.store(DatagramIoStatus::NoDatagram,
                                             std::memory_order_relaxed);
                return -1;
            }

            PendingDatagram& datagram = pendingFrontLocked_();
            datagramSize = datagram.size;
            sourceSize = datagram.originalSize;
            wasTruncated = datagram.truncated;
            source = datagram.sender;

            if (data && maxSize > 0) {
                bytesToCopy = static_cast<size_t>(
                    std::min<int64_t>(maxSize, static_cast<int64_t>(datagramSize)));
                if (bytesToCopy > 0) {
                    std::memcpy(data, datagram.bytes.data(), bytesToCopy);
                }
            }

            popPendingFrontLocked_();
        }

        if (sender) {
            *sender = socketAddressToString_(source);
        }
        if (senderPort) {
            *senderPort = socketAddressPort_(source);
        }
        if (truncated) {
            *truncated = wasTruncated;
        }
        if (originalSize) {
            *originalSize = sourceSize;
        }
        if (nativeSender) {
            *nativeSender = source;
        }
        m_lastDatagramIoStatus.store(wasTruncated ? DatagramIoStatus::Truncated
                                                  : DatagramIoStatus::Ok,
                                     std::memory_order_relaxed);

        if (!data || maxSize <= 0) {
            return static_cast<int64_t>(datagramSize);
        }
        return static_cast<int64_t>(bytesToCopy);
    }

    /**
     * @brief Performs the `receiveDatagram` operation.
     * @param sender Value passed to the method.
     * @param senderPort Value passed to the method.
     * @return The requested receive Datagram.
     */
    SwByteArray receiveDatagram(SwString* sender = nullptr,
                                uint16_t* senderPort = nullptr,
                                bool* truncated = nullptr,
                                size_t* originalSize = nullptr,
                                sockaddr_storage* nativeSender = nullptr) {
        SwByteArray result;
        sockaddr_storage source{};
        size_t sourceSize = 0;
        bool wasTruncated = false;
        {
            SwMutexLocker lock(m_queueMutex);
            if (pendingEmptyLocked_()) {
                m_lastDatagramIoStatus.store(DatagramIoStatus::NoDatagram,
                                             std::memory_order_relaxed);
                return SwByteArray();
            }

            PendingDatagram& datagram = pendingFrontLocked_();
            result = SwByteArray(datagram.bytes.data(), datagram.size);
            source = datagram.sender;
            sourceSize = datagram.originalSize;
            wasTruncated = datagram.truncated;
            popPendingFrontLocked_();
        }

        if (sender) {
            *sender = socketAddressToString_(source);
        }
        if (senderPort) {
            *senderPort = socketAddressPort_(source);
        }
        if (truncated) {
            *truncated = wasTruncated;
        }
        if (originalSize) {
            *originalSize = sourceSize;
        }
        if (nativeSender) {
            *nativeSender = source;
        }
        m_lastDatagramIoStatus.store(wasTruncated ? DatagramIoStatus::Truncated
                                                  : DatagramIoStatus::Ok,
                                     std::memory_order_relaxed);

        return result;
    }

    /**
     * Visits and consumes the oldest queued datagram without copying its payload.
     * The view is valid only for the duration of the callback. The callback must
     * not call another queue-mutating method on this socket.
     */
    bool consumePendingDatagram(const DatagramViewHandler& handler) {
        if (!handler) {
            return false;
        }
        SwMutexLocker lock(m_queueMutex);
        if (pendingEmptyLocked_()) {
            m_lastDatagramIoStatus.store(DatagramIoStatus::NoDatagram,
                                         std::memory_order_relaxed);
            return false;
        }
        PendingDatagram& datagram = pendingFrontLocked_();
        DatagramView view;
        view.data = datagram.size > 0 ? datagram.bytes.data() : nullptr;
        view.size = datagram.size;
        view.originalSize = datagram.originalSize;
        view.truncated = datagram.truncated;
        view.sender = datagram.sender;
        handler(view);
        popPendingFrontLocked_();
        m_lastDatagramIoStatus.store(view.truncated ? DatagramIoStatus::Truncated
                                                    : DatagramIoStatus::Ok,
                                     std::memory_order_relaxed);
        return true;
    }

    /**
     * Moves the oldest queued slot into caller-owned storage. This transfers
     * the native receive allocation without copying; the vacated slot is
     * allocated again only if it is reused by a future receive.
     */
    bool takePendingDatagram(OwnedDatagram& out) {
        SwMutexLocker lock(m_queueMutex);
        if (pendingEmptyLocked_()) {
            m_lastDatagramIoStatus.store(DatagramIoStatus::NoDatagram,
                                         std::memory_order_relaxed);
            return false;
        }
        PendingDatagram& datagram = pendingFrontLocked_();
        out.bytes = std::move(datagram.bytes);
        out.size = datagram.size;
        out.originalSize = datagram.originalSize;
        out.truncated = datagram.truncated;
        out.sender = datagram.sender;
        popPendingFrontLocked_();
        m_lastDatagramIoStatus.store(out.truncated ? DatagramIoStatus::Truncated
                                                   : DatagramIoStatus::Ok,
                                     std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Returns whether the object reports pending Datagrams.
     * @return `true` when the object reports pending Datagrams; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasPendingDatagrams() const {
        SwMutexLocker lock(m_queueMutex);
        return !pendingEmptyLocked_();
    }

    /**
     * @brief Returns the current pending Datagram Size.
     * @return The current pending Datagram Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int pendingDatagramSize() const {
        SwMutexLocker lock(m_queueMutex);
        return pendingEmptyLocked_() ? 0 : static_cast<int>(pendingFrontLocked_().size);
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

    uint64_t truncatedDatagrams() const {
        return m_totalTruncatedDatagrams.load(std::memory_order_relaxed);
    }

    uint64_t sendWouldBlockCount() const {
        return m_totalSendWouldBlock.load(std::memory_order_relaxed);
    }

    uint64_t suppressedConnectionResetErrors() const {
        return m_totalSuppressedConnectionResets.load(std::memory_order_relaxed);
    }

    bool udpConnectionResetSuppressionEnabled() const {
        return m_udpConnectionResetSuppressionEnabled.load(std::memory_order_relaxed);
    }

    DatagramIoStatus lastDatagramIoStatus() const {
        return m_lastDatagramIoStatus.load(std::memory_order_relaxed);
    }

    size_t allocatedReceiveBufferBytes() const {
        SwMutexLocker lock(m_queueMutex);
        size_t total = static_cast<size_t>(m_batchRecvBuf.size());
        total += static_cast<size_t>(m_readBuffer.size());
        for (size_t i = 0; i < m_pending.size(); ++i) {
            total += m_pending[i].bytes.size();
        }
        return total;
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

    void setSendBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_sendBufferSize = bytes;
        if (isSocketValid()) {
            applySendBufferSize();
        }
    }

    int requestedReceiveBufferSize() const {
        return m_receiveBufferSize;
    }

    int requestedSendBufferSize() const {
        return m_sendBufferSize;
    }

    int actualReceiveBufferSize() const {
        return socketIntOption_(SO_RCVBUF);
    }

    int actualSendBufferSize() const {
        return socketIntOption_(SO_SNDBUF);
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
        {
            SwMutexLocker lock(m_queueMutex);
            if (!m_pending.empty()) {
                configurePendingQueueLocked_(effectivePendingCapacity_());
            }
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
        SwMutexLocker lock(m_queueMutex);
        m_maxPendingDatagrams = maxPackets;
        if (!m_pending.empty()) {
            configurePendingQueueLocked_(effectivePendingCapacity_());
        }
    }

    void setMaxPendingBytes(size_t maxBytes) {
        if (maxBytes == 0) {
            return;
        }
        SwMutexLocker lock(m_queueMutex);
        m_maxPendingBytes = maxBytes;
        if (!m_pending.empty()) {
            configurePendingQueueLocked_(effectivePendingCapacity_());
        }
    }

    size_t maxPendingBytes() const {
        SwMutexLocker lock(m_queueMutex);
        return m_maxPendingBytes;
    }

    size_t effectiveMaxPendingDatagrams() const {
        SwMutexLocker lock(m_queueMutex);
        return effectivePendingCapacity_();
    }

    void setMaxReadBatchDatagrams(size_t maxPackets) {
        if (maxPackets == 0) {
            return;
        }
        m_maxReadBatchDatagrams = std::min<size_t>(maxPackets, 4096U);
    }

    // Réception par lots (recvmmsg) : draine la socket en 1 syscall pour N datagrammes au lieu d'un
    // recvfrom par paquet. OPT-IN, défaut OFF -> le comportement historique est STRICTEMENT inchangé
    // pour les appelants existants ; sans effet hors Linux (repli sur le chemin recvfrom).
    void setBatchReceive(bool enabled) { m_batchReceive = enabled; }
    bool batchReceive() const { return m_batchReceive; }

    /**
     * Enables or disables automatic dispatcher-driven reads. Synchronous users
     * that own a dedicated receive thread can disable this before bind() so a
     * single consumer owns the native socket.
     */
    void setReadNotificationsEnabled(bool enabled) {
        if (m_readNotificationsEnabled == enabled) {
            return;
        }
        m_readNotificationsEnabled = enabled;
        if (!enabled && !m_writeNotificationsEnabled) {
            unregisterDispatcher_();
        } else if (isSocketValid()) {
            registerDispatcher_();
        }
    }

    bool readNotificationsEnabled() const { return m_readNotificationsEnabled; }

    /** Arms a one-shot writable readiness notification. */
    void setWriteNotificationsEnabled(bool enabled) {
        if (m_writeNotificationsEnabled == enabled) {
            return;
        }
        m_writeNotificationsEnabled = enabled;
        if (isSocketValid()) {
            registerDispatcher_();
        }
    }

    bool writeNotificationsEnabled() const { return m_writeNotificationsEnabled; }

    void setBroadcastEnabled(bool enabled) {
        m_broadcastEnabled = enabled;
        applyBroadcastMode();
    }

    bool pollPendingDatagrams(int timeoutMs = 0) {
        if (!m_nativeIoMutex.tryLock()) {
            return hasPendingDatagrams();
        }
        bool receivedAny = false;
        bool result = false;
        {
            struct NativeIoUnlock_ {
                explicit NativeIoUnlock_(SwMutex& value) : mutex(value) {}
                SwMutex& mutex;
                ~NativeIoUnlock_() { mutex.unlock(); }
            } nativeIoUnlock{m_nativeIoMutex};
            if (hasPendingDatagrams()) {
                result = true;
            } else if (timeoutMs > 0 && !waitForSocketReadable_(timeoutMs)) {
                m_lastDatagramIoStatus.store(DatagramIoStatus::WouldBlock,
                                             std::memory_order_relaxed);
                result = hasPendingDatagrams();
            } else {
                const uint64_t receivedBefore = totalReceivedDatagrams();
                receivedAny = pollSocket_(timeoutMs);
                result = hasPendingDatagrams() ||
                         totalReceivedDatagrams() != receivedBefore;
            }
        }
        // A readyRead slot is allowed to close or destroy this socket. Emit
        // only after every native-I/O guard has released its mutex reference,
        // and do not touch any member after the notification.
        if (receivedAny) {
            scheduleReadyRead_();
        }
        return result;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() override {
        unregisterDispatcher_();
        m_writeNotificationsEnabled = false;
        SwMutexLocker nativeIoLock(m_nativeIoMutex);
        flushPendingTrafficTelemetry_();
        swSocketTrafficSetOpenState(socketTrafficState_, false);
        if (!isSocketValid()) {
            m_readyReadPosted.store(false);
            {
                SwMutexLocker lock(m_queueMutex);
                clearPendingLocked_();
                releasePendingPayloadBuffersLocked_();
            }
            m_pendingDatagramCount.store(0, std::memory_order_relaxed);
            m_totalSuppressedConnectionResets.store(0, std::memory_order_relaxed);
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
            m_udpConnectionResetSuppressionEnabled.store(false,
                                                          std::memory_order_relaxed);
            m_resolvedAddressCache.clear();
            return;
        }
        const NativeSocketHandle closingHandle = nativeSocketHandle();
        if (m_nativeSocketClosed && closingHandle != static_cast<NativeSocketHandle>(-1)) {
            m_nativeSocketClosed(closingHandle);
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
            clearPendingLocked_();
            releasePendingPayloadBuffersLocked_();
        }
        m_pendingDatagramCount.store(0, std::memory_order_relaxed);
        publishTrafficMonitorUdpStats_(0);
        m_totalReceivedDatagrams.store(0);
        m_totalQueueDrops.store(0);
        m_totalTruncatedDatagrams.store(0, std::memory_order_relaxed);
        m_totalSendWouldBlock.store(0, std::memory_order_relaxed);
        m_totalSuppressedConnectionResets.store(0, std::memory_order_relaxed);
        m_queueHighWatermark.store(0);
        m_readyReadPosted.store(false);
        m_lastDatagramIoStatus.store(DatagramIoStatus::NoDatagram,
                                     std::memory_order_relaxed);
        m_lastQueueDropLogAt = {};
        m_suppressedQueueDropCount = 0;
        m_suppressedQueueDropBytes = 0;
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
        m_udpConnectionResetSuppressionEnabled.store(false,
                                                      std::memory_order_relaxed);
        m_resolvedAddressCache.clear();
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
    struct PendingQueueResult_ {
        uint64_t queueDepth{0};
        size_t droppedCount{0};
        size_t droppedBytes{0};
    };

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
        configureUdpConnectionReset_(false);
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
        SwString platformError;
        if (m_nativeSocketPrepare &&
            !m_nativeSocketPrepare(static_cast<NativeSocketHandle>(m_socket), platformError)) {
#if defined(_WIN32)
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
#else
            ::close(m_socket);
            m_socket = -1;
#endif
            setSocketError(SocketError::SocketAccessError,
                           platformError.isEmpty() ? SwString("Native socket platform hook failed")
                                                   : platformError);
            return false;
        }
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
        applySendBufferSize();
        applyBroadcastMode();
        m_totalReceivedDatagrams.store(0);
        m_totalQueueDrops.store(0);
        m_totalSuppressedConnectionResets.store(0, std::memory_order_relaxed);
        m_queueHighWatermark.store(0);
        m_pendingDatagramCount.store(0, std::memory_order_relaxed);
        registerDispatcher_();
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
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

    void refreshTrafficMonitorEndpoints_() {
        swSocketTrafficUpdateEndpoints(socketTrafficState_,
                                       m_boundAddress,
                                       m_boundPort,
                                       m_remoteAddress,
                                       m_remotePort);
    }

    void publishTrafficMonitorUdpStats_(unsigned long long pendingDatagramCount) {
        swSocketTrafficUpdateUdpStats(socketTrafficState_,
                                      m_totalReceivedDatagrams.load(std::memory_order_relaxed),
                                      m_totalSentDatagrams.load(std::memory_order_relaxed),
                                      m_totalQueueDrops.load(std::memory_order_relaxed),
                                      m_queueHighWatermark.load(std::memory_order_relaxed),
                                      pendingDatagramCount);
    }

    void flushPendingTrafficTelemetry_() {
        const uint64_t pendingSentBytes =
            m_pendingTelemetrySentBytes.exchange(0, std::memory_order_relaxed);
        if (pendingSentBytes > 0) {
            swSocketTrafficAddSentBytes(socketTrafficState_,
                                        static_cast<unsigned long long>(pendingSentBytes));
        }
    }

    void logQueueDrops_(const PendingQueueResult_& result) {
        if (result.droppedCount == 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool shouldPublish =
            m_lastQueueDropLogAt.time_since_epoch().count() == 0 ||
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastQueueDropLogAt).count() >= 1000;
        if (!shouldPublish) {
            m_suppressedQueueDropCount += result.droppedCount;
            m_suppressedQueueDropBytes += result.droppedBytes;
            return;
        }
        const size_t droppedCount = result.droppedCount + m_suppressedQueueDropCount;
        const size_t droppedBytes = result.droppedBytes + m_suppressedQueueDropBytes;
        m_suppressedQueueDropCount = 0;
        m_suppressedQueueDropBytes = 0;
        m_lastQueueDropLogAt = now;
        size_t queueSlots = 0U;
        size_t byteBudget = 0U;
        {
            SwMutexLocker lock(m_queueMutex);
            queueSlots = m_pending.size();
            byteBudget = m_maxPendingBytes;
        }
        swCWarning(kSwLogCategory_SwUdpSocket)
            << "[SwUdpSocket] Dropping " << droppedCount
            << " oldest datagram(s) (" << droppedBytes
            << " bytes) due to queue pressure (slots=" << queueSlots
            << ", byteBudget=" << byteBudget << ")";
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
        SwMutexLocker nativeIoLock(m_nativeIoMutex);
        if (!isSocketValid() || (!data && size > 0) || size > 65535U) {
            return -1;
        }
        int sent = ::sendto(m_socket, data, static_cast<int>(size), 0,
                            reinterpret_cast<const sockaddr*>(&target.storage),
                            static_cast<int>(target.length));
        if (sent < 0) {
            const int error = lastErrorCode();
#if defined(_WIN32)
            if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) {
#else
            if (error == EWOULDBLOCK || error == EAGAIN) {
#endif
                m_lastDatagramIoStatus.store(DatagramIoStatus::WouldBlock,
                                             std::memory_order_relaxed);
                m_totalSendWouldBlock.fetch_add(1, std::memory_order_relaxed);
                return -1;
            }
            m_lastDatagramIoStatus.store(DatagramIoStatus::Error,
                                         std::memory_order_relaxed);
            setSocketError(SocketError::OperationError, SwString("sendto failed"));
            return -1;
        }
        m_lastDatagramIoStatus.store(DatagramIoStatus::Ok, std::memory_order_relaxed);
        const bool shouldRefreshLocalEndpoint =
            m_boundAddress.isEmpty() || m_boundPort == 0U;
        const bool localEndpointChanged =
            shouldRefreshLocalEndpoint ? refreshLocalEndpoint_() : false;
        if (sent > 0) {
            m_totalSentBytes.fetch_add(static_cast<uint64_t>(sent), std::memory_order_relaxed);
            m_pendingTelemetrySentBytes.fetch_add(static_cast<uint64_t>(sent),
                                                  std::memory_order_relaxed);
        }
        const uint64_t sentDatagrams =
            m_totalSentDatagrams.fetch_add(1, std::memory_order_relaxed) + 1U;
        if (sentDatagrams == 1U || (sentDatagrams % 64U) == 0U) {
            const uint64_t telemetryBytes =
                m_pendingTelemetrySentBytes.exchange(0, std::memory_order_relaxed);
            if (telemetryBytes > 0) {
                swSocketTrafficAddSentBytes(socketTrafficState_,
                                            static_cast<unsigned long long>(telemetryBytes));
            }
            publishTrafficMonitorUdpStats_(m_pendingDatagramCount.load(std::memory_order_relaxed));
        }
        if (localEndpointChanged) {
            refreshTrafficMonitorEndpoints_();
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

    enum class NativeReceiveStatus_ {
        Data,
        WouldBlock,
        Error
    };

    struct NativeReceiveResult_ {
        NativeReceiveStatus_ status{NativeReceiveStatus_::WouldBlock};
        size_t storedBytes{0};
        size_t originalBytes{0};
        bool truncated{false};
        int errorCode{0};
    };

    NativeReceiveResult_ receiveInto_(char* destination,
                                      size_t capacity,
                                      sockaddr_storage& sender) {
        NativeReceiveResult_ result;
        std::memset(&sender, 0, sizeof(sender));
#if defined(_WIN32)
        int senderLength = sizeof(sender);
        const int received = ::recvfrom(m_socket,
                                        destination,
                                        static_cast<int>(capacity),
                                        0,
                                        reinterpret_cast<sockaddr*>(&sender),
                                        &senderLength);
        if (received == SOCKET_ERROR) {
            result.errorCode = WSAGetLastError();
            if (result.errorCode == WSAEWOULDBLOCK || result.errorCode == WSAEINPROGRESS) {
                result.status = NativeReceiveStatus_::WouldBlock;
                return result;
            }
            if (result.errorCode == WSAECONNRESET &&
                m_state != SocketState::ConnectedState) {
                m_totalSuppressedConnectionResets.fetch_add(1,
                                                            std::memory_order_relaxed);
                result.status = NativeReceiveStatus_::WouldBlock;
                return result;
            }
            if (result.errorCode == WSAEMSGSIZE) {
                result.status = NativeReceiveStatus_::Data;
                result.storedBytes = capacity;
                result.originalBytes = capacity < (std::numeric_limits<size_t>::max)()
                                           ? capacity + 1U
                                           : capacity;
                result.truncated = true;
                return result;
            }
            result.status = NativeReceiveStatus_::Error;
            return result;
        }
        result.status = NativeReceiveStatus_::Data;
        result.storedBytes = static_cast<size_t>(received);
        result.originalBytes = result.storedBytes;
#else
        iovec vector{};
        vector.iov_base = destination;
        vector.iov_len = capacity;
        msghdr message{};
        message.msg_name = &sender;
        message.msg_namelen = sizeof(sender);
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        const ssize_t received = ::recvmsg(m_socket, &message, MSG_DONTWAIT | MSG_TRUNC);
        if (received < 0) {
            result.errorCode = errno;
            result.status = (errno == EWOULDBLOCK || errno == EAGAIN)
                                ? NativeReceiveStatus_::WouldBlock
                                : NativeReceiveStatus_::Error;
            return result;
        }
        result.status = NativeReceiveStatus_::Data;
        result.originalBytes = static_cast<size_t>(received);
        result.storedBytes = std::min(result.originalBytes, capacity);
        result.truncated = (message.msg_flags & MSG_TRUNC) != 0 ||
                           result.originalBytes > capacity;
#endif
        return result;
    }

    bool waitForSocketReadable_(int timeoutMs) const {
        if (!isSocketValid()) {
            return false;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_socket, &readSet);
        timeval timeout{};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
#if defined(_WIN32)
        const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
#else
        const int ready = ::select(m_socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
        return ready > 0 && FD_ISSET(m_socket, &readSet);
    }

    bool pendingEmptyLocked_() const {
        return m_pendingSize == 0;
    }

    size_t pendingTailIndexLocked_() const {
        return (m_pendingHead + m_pendingSize) % m_pending.size();
    }

    PendingDatagram& pendingFrontLocked_() {
        return m_pending[m_pendingHead];
    }

    const PendingDatagram& pendingFrontLocked_() const {
        return m_pending[m_pendingHead];
    }

    void popPendingFrontLocked_() {
        if (m_pendingSize == 0 || m_pending.empty()) {
            return;
        }
        const size_t consumedBytes = m_pending[m_pendingHead].size;
        m_pending[m_pendingHead].clearPayload();
        m_pendingBytes = consumedBytes > m_pendingBytes ? 0 : m_pendingBytes - consumedBytes;
        --m_pendingSize;
        if (m_pendingSize == 0) {
            m_pendingHead = 0;
        } else {
            m_pendingHead = (m_pendingHead + 1) % m_pending.size();
        }
        m_pendingDatagramCount.store(static_cast<uint64_t>(m_pendingSize), std::memory_order_relaxed);
    }

    void clearPendingLocked_() {
        for (size_t i = 0; i < m_pending.size(); ++i) {
            m_pending[i].clearPayload();
        }
        m_pendingHead = 0;
        m_pendingSize = 0;
        m_pendingBytes = 0;
        m_pendingDatagramCount.store(0, std::memory_order_relaxed);
    }

    void releasePendingPayloadBuffersLocked_() {
        for (size_t i = 0; i < m_pending.size(); ++i) {
            m_pending[i].releaseBuffer();
        }
        std::vector<PendingDatagram>().swap(m_pending);
        m_pendingHead = 0;
        m_pendingSize = 0;
        m_pendingBytes = 0;
        m_readBuffer.clear();
        m_readBuffer.squeeze();
        m_batchRecvBuf.clear();
        m_batchRecvBuf.squeeze();
    }

    size_t effectivePendingCapacity_() const {
        const size_t slotBytes = std::max<size_t>(1U, m_maxDatagramSize);
        const size_t byBytes = std::max<size_t>(1U, m_maxPendingBytes / slotBytes);
        return std::max<size_t>(1U, std::min(m_maxPendingDatagrams, byBytes));
    }

    void configurePendingQueueLocked_(size_t capacity) {
        if (capacity == 0) {
            return;
        }

        const size_t oldCapacity = m_pending.size();
        size_t keep = 0;
        size_t keptBytes = 0;
        const size_t candidateCount = std::min(m_pendingSize, capacity);
        while (keep < candidateCount && oldCapacity > 0U) {
            const size_t index =
                (m_pendingHead + m_pendingSize - keep - 1U) % oldCapacity;
            const size_t bytes = m_pending[index].size;
            if (keep > 0U && keptBytes + bytes > m_maxPendingBytes) {
                break;
            }
            keptBytes += bytes;
            ++keep;
        }
        const size_t dropped = m_pendingSize - keep;
        std::vector<PendingDatagram> next(capacity);

        if (oldCapacity > 0 && keep > 0) {
            const size_t firstKept = (m_pendingHead + dropped) % oldCapacity;
            for (size_t i = 0; i < keep; ++i) {
                const size_t oldIndex = (firstKept + i) % oldCapacity;
                next[i] = std::move(m_pending[oldIndex]);
            }
        }

        m_pending.swap(next);
        m_pendingHead = 0;
        m_pendingSize = keep;
        m_pendingBytes = keptBytes;
        if (dropped > 0U) {
            m_totalQueueDrops.fetch_add(static_cast<uint64_t>(dropped),
                                        std::memory_order_relaxed);
        }
        m_pendingDatagramCount.store(static_cast<uint64_t>(m_pendingSize), std::memory_order_relaxed);
    }

    PendingQueueResult_ enqueuePendingDatagramLocked_(const char* data,
                                                      size_t bytes,
                                                      const sockaddr_storage& sender,
                                                      size_t originalBytes = 0,
                                                      bool truncated = false) {
        PendingQueueResult_ result{};
        const size_t effectiveCapacity = effectivePendingCapacity_();
        if (m_pending.size() != effectiveCapacity) {
            configurePendingQueueLocked_(effectiveCapacity);
        }

        if (m_pending.empty()) {
            ++result.droppedCount;
            m_totalQueueDrops.fetch_add(1, std::memory_order_relaxed);
            result.queueDepth = static_cast<uint64_t>(m_pendingSize);
            m_pendingDatagramCount.store(result.queueDepth, std::memory_order_relaxed);
            return result;
        }

        while (m_pendingSize >= m_pending.size() ||
               (m_pendingSize > 0 && m_pendingBytes + bytes > m_maxPendingBytes)) {
            result.droppedBytes += pendingFrontLocked_().size;
            popPendingFrontLocked_();
            ++result.droppedCount;
        }

        PendingDatagram& slot = m_pending[pendingTailIndexLocked_()];
        slot.assign(data, bytes, sender, originalBytes, truncated);
        ++m_pendingSize;
        m_pendingBytes += bytes;
        result.queueDepth = static_cast<uint64_t>(m_pendingSize);
        if (result.queueDepth > m_queueHighWatermark.load(std::memory_order_relaxed)) {
            m_queueHighWatermark.store(result.queueDepth, std::memory_order_relaxed);
        }
        if (result.droppedCount > 0) {
            m_totalQueueDrops.fetch_add(static_cast<uint64_t>(result.droppedCount), std::memory_order_relaxed);
        }
        m_pendingDatagramCount.store(result.queueDepth, std::memory_order_relaxed);
        return result;
    }

#if defined(__linux__)
    // Enqueue d'un datagramme reçu (mêmes règles que la boucle recvfrom : compteurs, file bornée,
    // drop du plus ancien sous pression). Utilisé UNIQUEMENT par le chemin recvmmsg opt-in.
    // Chemin recvmmsg (opt-in) : jusqu'à kBatch datagrammes par appel syscall (amortit le coût du
    // franchissement de syscall, ~x30 mesuré). Linux uniquement ; ne touche jamais le chemin par défaut.
    bool pollSocketBatch_() {
        constexpr size_t kBatch = 64;
        const size_t slot = m_maxDatagramSize;
        {
            SwMutexLocker lock(m_queueMutex);
            if (static_cast<size_t>(m_batchRecvBuf.size()) < slot * kBatch) {
                m_batchRecvBuf.resize(slot * kBatch);
            }
        }
        struct mmsghdr   msgs[kBatch];
        struct iovec     iovs[kBatch];
        sockaddr_storage addrs[kBatch];
        bool receivedAny = false;
        size_t total = 0;
        size_t totalTruncated = 0;
        PendingQueueResult_ aggregateQueueResult{};
        while (total < m_maxReadBatchDatagrams) {
            const size_t batchLimit = std::min(kBatch, m_maxReadBatchDatagrams - total);
            for (size_t i = 0; i < batchLimit; ++i) {
                iovs[i].iov_base = m_batchRecvBuf.data() + i * slot;
                iovs[i].iov_len  = slot;
                std::memset(&msgs[i], 0, sizeof(msgs[i]));
                msgs[i].msg_hdr.msg_iov     = &iovs[i];
                msgs[i].msg_hdr.msg_iovlen  = 1;
                msgs[i].msg_hdr.msg_name    = &addrs[i];
                msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_storage);
            }
            const int r = ::recvmmsg(m_socket,
                                     msgs,
                                     static_cast<unsigned int>(batchLimit),
                                     MSG_DONTWAIT | MSG_TRUNC,
                                     nullptr);
            if (r <= 0) {
                if (r < 0) {
                    const int err = lastErrorCode();
                    if (err != EWOULDBLOCK && err != EAGAIN) {
                        swCError(kSwLogCategory_SwUdpSocket) << "[SwUdpSocket] recvmmsg error=" << err;
                    }
                }
                break;
            }
            PendingQueueResult_ queueResult{};
            size_t receivedBytes = 0;
            {
                SwMutexLocker lock(m_queueMutex);
                for (int i = 0; i < r; ++i) {
                    const size_t originalLength = static_cast<size_t>(msgs[i].msg_len);
                    const size_t storedLength = std::min(originalLength, slot);
                    const bool truncated =
                        (msgs[i].msg_hdr.msg_flags & MSG_TRUNC) != 0 || originalLength > slot;
                    const PendingQueueResult_ one = enqueuePendingDatagramLocked_(
                        m_batchRecvBuf.data() + static_cast<size_t>(i) * slot,
                        storedLength,
                        addrs[i],
                        originalLength,
                        truncated);
                    queueResult.queueDepth = one.queueDepth;
                    queueResult.droppedCount += one.droppedCount;
                    queueResult.droppedBytes += one.droppedBytes;
                    receivedBytes += originalLength;
                    totalTruncated += truncated ? 1U : 0U;
                }
            }
            m_totalReceivedDatagrams.fetch_add(static_cast<uint64_t>(r), std::memory_order_relaxed);
            m_totalReceivedBytes.fetch_add(static_cast<uint64_t>(receivedBytes), std::memory_order_relaxed);
            swSocketTrafficAddReceivedBytes(socketTrafficState_, static_cast<unsigned long long>(receivedBytes));
            aggregateQueueResult.queueDepth = queueResult.queueDepth;
            aggregateQueueResult.droppedCount += queueResult.droppedCount;
            aggregateQueueResult.droppedBytes += queueResult.droppedBytes;
            receivedAny = true;
            total += static_cast<size_t>(r);
            if (static_cast<size_t>(r) < batchLimit) {
                break; // socket drainé
            }
        }
        if (receivedAny) {
            if (totalTruncated > 0) {
                m_totalTruncatedDatagrams.fetch_add(static_cast<uint64_t>(totalTruncated),
                                                    std::memory_order_relaxed);
            }
            logQueueDrops_(aggregateQueueResult);
            const bool localEndpointChanged =
                (m_boundAddress.isEmpty() || m_boundPort == 0U) ? refreshLocalEndpoint_() : false;
            publishTrafficMonitorUdpStats_(m_pendingDatagramCount.load(std::memory_order_relaxed));
            if (localEndpointChanged) {
                refreshTrafficMonitorEndpoints_();
            }
            m_lastDatagramIoStatus.store(totalTruncated > 0 ? DatagramIoStatus::Truncated
                                                           : DatagramIoStatus::Ok,
                                         std::memory_order_relaxed);
        }
        return receivedAny;
    }
#endif // __linux__

    bool pollSocket_(int timeoutMs) {
        if (!isSocketValid()) {
            return false;
        }
        SW_UNUSED(timeoutMs)
        if (!m_nativeIoMutex.tryLock()) {
            return false;
        }
        struct NativeIoUnlock_ {
            explicit NativeIoUnlock_(SwMutex& value) : mutex(value) {}
            SwMutex& mutex;
            ~NativeIoUnlock_() { mutex.unlock(); }
        } nativeIoUnlock{m_nativeIoMutex};
#if defined(__linux__)
        if (m_batchReceive) { // opt-in : réception par lots recvmmsg (défaut OFF = chemin ci-dessous)
            return pollSocketBatch_();
        }
#endif

        size_t receivedDatagrams = 0;
        size_t receivedBytes = 0;
        size_t truncatedDatagrams = 0;
        PendingQueueResult_ aggregateQueueResult{};

        while (receivedDatagrams < m_maxReadBatchDatagrams) {
            sockaddr_storage sender{};
            NativeReceiveResult_ receiveResult;
            bool directSlotAvailable = false;
            {
                SwMutexLocker lock(m_queueMutex);
                const size_t effectiveCapacity = effectivePendingCapacity_();
                if (m_pending.size() != effectiveCapacity) {
                    configurePendingQueueLocked_(effectiveCapacity);
                }
                if (m_pendingSize < m_pending.size()) {
                    PendingDatagram& slot = m_pending[pendingTailIndexLocked_()];
                    const bool allocatedForAttempt = slot.bytes.size() < m_maxDatagramSize;
                    slot.ensureBuffer(m_maxDatagramSize);
                    directSlotAvailable = true;
                    receiveResult = receiveInto_(slot.bytes.data(), m_maxDatagramSize, sender);
                    if (receiveResult.status == NativeReceiveStatus_::Data) {
                        slot.commit(receiveResult.storedBytes,
                                    sender,
                                    receiveResult.originalBytes,
                                    receiveResult.truncated);
                        ++m_pendingSize;
                        m_pendingBytes += receiveResult.storedBytes;
                        aggregateQueueResult.queueDepth = static_cast<uint64_t>(m_pendingSize);
                        if (aggregateQueueResult.queueDepth >
                            m_queueHighWatermark.load(std::memory_order_relaxed)) {
                            m_queueHighWatermark.store(aggregateQueueResult.queueDepth,
                                                       std::memory_order_relaxed);
                        }
                        m_pendingDatagramCount.store(aggregateQueueResult.queueDepth,
                                                     std::memory_order_relaxed);
                    } else if (allocatedForAttempt) {
                        slot.releaseBuffer();
                    }
                }
            }

            if (!directSlotAvailable) {
                {
                    SwMutexLocker lock(m_queueMutex);
                    if (static_cast<size_t>(m_readBuffer.size()) < m_maxDatagramSize) {
                        m_readBuffer.resize(m_maxDatagramSize);
                    }
                }
                receiveResult = receiveInto_(m_readBuffer.data(), m_maxDatagramSize, sender);
                if (receiveResult.status == NativeReceiveStatus_::Data) {
                    PendingQueueResult_ queueResult;
                    {
                        SwMutexLocker lock(m_queueMutex);
                        queueResult = enqueuePendingDatagramLocked_(m_readBuffer.constData(),
                                                                     receiveResult.storedBytes,
                                                                     sender,
                                                                     receiveResult.originalBytes,
                                                                     receiveResult.truncated);
                    }
                    aggregateQueueResult.queueDepth = queueResult.queueDepth;
                    aggregateQueueResult.droppedCount += queueResult.droppedCount;
                    aggregateQueueResult.droppedBytes += queueResult.droppedBytes;
                }
            }

            if (receiveResult.status == NativeReceiveStatus_::WouldBlock) {
                if (receivedDatagrams == 0) {
                    m_lastDatagramIoStatus.store(DatagramIoStatus::WouldBlock,
                                                 std::memory_order_relaxed);
                }
                break;
            }
            if (receiveResult.status == NativeReceiveStatus_::Error) {
                m_lastDatagramIoStatus.store(DatagramIoStatus::Error,
                                             std::memory_order_relaxed);
                swCError(kSwLogCategory_SwUdpSocket)
                    << "[SwUdpSocket] receive error=" << receiveResult.errorCode;
                break;
            }

            ++receivedDatagrams;
            receivedBytes += receiveResult.originalBytes;
            if (receiveResult.truncated) {
                ++truncatedDatagrams;
            }
        }

        if (receivedDatagrams > 0) {
            m_totalReceivedDatagrams.fetch_add(static_cast<uint64_t>(receivedDatagrams),
                                               std::memory_order_relaxed);
            m_totalReceivedBytes.fetch_add(static_cast<uint64_t>(receivedBytes),
                                           std::memory_order_relaxed);
            if (truncatedDatagrams > 0) {
                m_totalTruncatedDatagrams.fetch_add(static_cast<uint64_t>(truncatedDatagrams),
                                                    std::memory_order_relaxed);
            }
            swSocketTrafficAddReceivedBytes(socketTrafficState_,
                                            static_cast<unsigned long long>(receivedBytes));
            logQueueDrops_(aggregateQueueResult);
            const bool localEndpointChanged =
                (m_boundAddress.isEmpty() || m_boundPort == 0U) ? refreshLocalEndpoint_() : false;
            publishTrafficMonitorUdpStats_(aggregateQueueResult.queueDepth);
            if (localEndpointChanged) {
                refreshTrafficMonitorEndpoints_();
            }
            m_lastDatagramIoStatus.store(truncatedDatagrams > 0 ? DatagramIoStatus::Truncated
                                                               : DatagramIoStatus::Ok,
                                         std::memory_order_relaxed);
        }
        return receivedDatagrams != 0;
    }

    void registerDispatcher_() {
        unregisterDispatcher_();
        if (!isSocketValid() ||
            (!m_readNotificationsEnabled && !m_writeNotificationsEnabled)) {
            return;
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        SwCoreApplication* app = affinity ? affinity->application() : nullptr;
        if (!app) {
            app = SwCoreApplication::instance(false);
        }
        if (!app) {
            return;
        }

#if defined(_WIN32)
        if (m_event == WSA_INVALID_EVENT) {
            m_event = WSACreateEvent();
            if (m_event == WSA_INVALID_EVENT) {
                setSocketError(SocketError::SocketAccessError, SwString("WSACreateEvent failed"));
                return;
            }
        }
        const long networkMask = FD_CLOSE |
                                 (m_readNotificationsEnabled ? FD_READ : 0) |
                                 (m_writeNotificationsEnabled ? FD_WRITE : 0);
        if (WSAEventSelect(m_socket, m_event, networkMask) == SOCKET_ERROR) {
            setSocketError(SocketError::SocketAccessError, SwString("WSAEventSelect failed"));
            return;
        }
        m_dispatchToken = app->ioDispatcher().watchHandleReliable(
            m_event,
            [affinity](std::function<void()> task) mutable -> bool {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    return affinity->postTaskOnLaneReliable(std::move(task),
                                                            SwFiberLane::Input);
                }
                task();
                return true;
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
                const bool receivedAny = pollSocket_(0);
                if (receivedAny) {
                    scheduleReadyRead_();
                }
                if (!SwObject::isLive(this) || !isSocketValid()) {
                    return;
                }
            }
            if (networkEvents.lNetworkEvents & FD_WRITE) {
                m_writeNotificationsEnabled = false;
                registerDispatcher_();
                readyWrite();
            }
        });
#else
        std::uint32_t eventMask = SwIoDispatcher::Error | SwIoDispatcher::Hangup;
        if (m_readNotificationsEnabled) eventMask |= SwIoDispatcher::Readable;
        if (m_writeNotificationsEnabled) eventMask |= SwIoDispatcher::Writable;
        m_dispatchToken = app->ioDispatcher().watchFdReliable(
            m_socket,
            eventMask,
            [affinity](std::function<void()> task) mutable -> bool {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    return affinity->postTaskOnLaneReliable(std::move(task),
                                                            SwFiberLane::Input);
                }
                task();
                return true;
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
                                                              const bool receivedAny = pollSocket_(0);
                                                              if (receivedAny) {
                                                                  scheduleReadyRead_();
                                                              }
                                                              if (!SwObject::isLive(this) || !isSocketValid()) {
                                                                  return;
                                                              }
                                                          }
                                                          if (events & SwIoDispatcher::Writable) {
                                                              m_writeNotificationsEnabled = false;
                                                              registerDispatcher_();
                                                              readyWrite();
                                                          }
                                                      });
#endif
    }

    void unregisterDispatcher_() {
        if (!m_dispatchToken) {
            return;
        }
        ThreadHandle* affinity = threadHandle();
        SwCoreApplication* app = affinity ? affinity->application() : nullptr;
        if (!app) {
            app = SwCoreApplication::instance(false);
        }
        if (app) {
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

    int socketIntOption_(int option) const {
        if (!isSocketValid()) {
            return 0;
        }
        int value = 0;
#if defined(_WIN32)
        int length = sizeof(value);
        if (getsockopt(m_socket,
                       SOL_SOCKET,
                       option,
                       reinterpret_cast<char*>(&value),
                       &length) != 0) {
            return 0;
        }
#else
        socklen_t length = sizeof(value);
        if (getsockopt(m_socket, SOL_SOCKET, option, &value, &length) != 0) {
            return 0;
        }
#endif
        return value;
    }

    void applySocketBufferSize_(int option, int bytes, const char* label) {
        if (bytes <= 0 || !isSocketValid()) {
            return;
        }
        int desired = bytes;
        int rc = 0;
#if defined(_WIN32)
        rc = setsockopt(m_socket,
                        SOL_SOCKET,
                        option,
                        reinterpret_cast<const char*>(&desired),
                        sizeof(desired));
#else
        rc = setsockopt(m_socket, SOL_SOCKET, option, &desired, sizeof(desired));
#endif
        if (rc != 0) {
            swCWarning(kSwLogCategory_SwUdpSocket)
                << "[SwUdpSocket] Failed to apply " << label
                << " buffer size=" << bytes
                << " error=" << lastErrorCode();
            return;
        }

        const int actual = socketIntOption_(option);
        if (actual > 0 && actual < bytes) {
            swCWarning(kSwLogCategory_SwUdpSocket)
                << "[SwUdpSocket] " << label
                << " buffer requested=" << bytes
                << " actual=" << actual;
        }
    }

    void applyReceiveBufferSize() {
        applySocketBufferSize_(SO_RCVBUF, m_receiveBufferSize, "receive");
    }

    void applySendBufferSize() {
        applySocketBufferSize_(SO_SNDBUF, m_sendBufferSize, "send");
    }

    void applyBroadcastMode() {
        if (!isSocketValid()) {
            return;
        }
        const int enabled = m_broadcastEnabled ? 1 : 0;
#if defined(_WIN32)
        setsockopt(m_socket,
                   SOL_SOCKET,
                   SO_BROADCAST,
                   reinterpret_cast<const char*>(&enabled),
                   sizeof(enabled));
#else
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
#endif
    }

#if defined(_WIN32)
    void configureUdpConnectionReset_(bool reportConnectionResets) {
        if (!isSocketValid()) {
            m_udpConnectionResetSuppressionEnabled.store(false,
                                                          std::memory_order_relaxed);
            return;
        }
        BOOL behavior = reportConnectionResets ? TRUE : FALSE;
        DWORD returned = 0;
        const int rc = WSAIoctl(m_socket,
                                SIO_UDP_CONNRESET,
                                &behavior,
                                sizeof(behavior),
                                nullptr,
                                0,
                                &returned,
                                nullptr,
                                nullptr);
        m_udpConnectionResetSuppressionEnabled.store(
            rc == 0 && !reportConnectionResets,
            std::memory_order_relaxed);
    }
#endif

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
    // FIFO O(1). Payload buffers are allocated only when a slot receives data.
    std::vector<PendingDatagram> m_pending;
    size_t m_pendingHead{0};
    size_t m_pendingSize{0};
    SwString m_boundAddress;
    uint16_t m_boundPort{0};
    SwString m_remoteAddress;
    uint16_t m_remotePort{0};
    SocketState m_state{SocketState::UnconnectedState};
    SocketError m_error{SocketError::UnknownSocketError};
    SwString m_errorString;
    int m_lastSystemError{0};
    std::atomic<uint64_t> m_totalReceivedBytes{0};
    std::atomic<uint64_t> m_totalSentBytes{0};
    std::atomic<uint64_t> m_pendingTelemetrySentBytes{0};
    std::atomic<uint64_t> m_totalSentDatagrams{0};
    std::atomic<uint64_t> m_totalReceivedDatagrams{0};
    std::atomic<uint64_t> m_totalQueueDrops{0};
    std::atomic<uint64_t> m_totalTruncatedDatagrams{0};
    std::atomic<uint64_t> m_totalSendWouldBlock{0};
    std::atomic<uint64_t> m_totalSuppressedConnectionResets{0};
    std::atomic<uint64_t> m_queueHighWatermark{0};
    std::atomic<uint64_t> m_pendingDatagramCount{0};
    std::atomic<bool> m_readyReadPosted{false};
    std::atomic<bool> m_udpConnectionResetSuppressionEnabled{false};
    mutable SwMutex m_nativeIoMutex{SwMutex::Recursive};
    std::atomic<DatagramIoStatus> m_lastDatagramIoStatus{DatagramIoStatus::NoDatagram};
    int m_receiveBufferSize{0};
    int m_sendBufferSize{0};
    size_t m_maxDatagramSize{2048};
    size_t m_maxPendingDatagrams{512};
    size_t m_maxPendingBytes{4U * 1024U * 1024U};
    size_t m_pendingBytes{0};
    size_t m_maxReadBatchDatagrams{128};
    bool m_broadcastEnabled{false};
    SwByteArray m_readBuffer;
    bool m_batchReceive{false};   // opt-in recvmmsg (Linux) ; défaut OFF = chemin recvfrom legacy inchangé
    bool m_writeNotificationsEnabled{false}; // one-shot writable readiness
    bool m_readNotificationsEnabled{true};
    SwByteArray m_batchRecvBuf;   // buffer de réception par lots (kBatch * m_maxDatagramSize)
    std::chrono::steady_clock::time_point m_lastQueueDropLogAt{};
    size_t m_suppressedQueueDropCount{0};
    size_t m_suppressedQueueDropBytes{0};
    SwHash<SwString, ResolvedAddress> m_resolvedAddressCache;
    SwSocketTrafficStateHandle socketTrafficState_;
    NativeSocketPrepareHandler m_nativeSocketPrepare;
    NativeSocketClosedHandler m_nativeSocketClosed;
};
