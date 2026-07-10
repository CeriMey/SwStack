#pragma once

/**
 * @file src/media/swvtp/SwVtpUdpTransport.h
 * @brief SwVTP UDP adapter built on the CoreSw SwUdpSocket abstraction.
 */

#include "core/io/SwUdpSocket.h"
#include "core/types/SwByteArray.h"
#include "media/swvtp/SwVtpProtocol.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

struct SwVtpUdpPacket {
    SwByteArray bytes{};
    uint32_t senderIpv4{0};
    uint16_t senderPort{0};
};

class SwVtpUdpTransport {
public:
    using PacketHandler = std::function<void(SwVtpUdpPacket&&)>;

    SwVtpUdpTransport() {
        m_callbackContext = new SwObject();
        SwObject::connect(&m_socket,
                          &SwUdpSocket::readyRead,
                          m_callbackContext,
                          [this]() { drainReadyDatagrams_(); });
    }

    ~SwVtpUdpTransport() {
        setPacketHandler(PacketHandler());
        close();
        delete m_callbackContext;
        m_callbackContext = nullptr;
    }

    SwVtpUdpTransport(const SwVtpUdpTransport&) = delete;
    SwVtpUdpTransport& operator=(const SwVtpUdpTransport&) = delete;

    bool open(uint32_t bindIpv4, uint16_t port) {
        return open(ipv4ToString_(bindIpv4), port);
    }

    bool open(const SwString& bindAddress, uint16_t port) {
        close();
        clearTargetCache_();
        m_socket.setReceiveBufferSize(4 * 1024 * 1024);
        m_socket.setMaxDatagramSize(m_maxDatagramBytes);
        m_socket.setMaxPendingDatagrams(m_maxPendingDatagrams);
        m_socket.setMaxPendingBytes(m_maxPendingBytes);
        m_socket.setMaxReadBatchDatagrams(256);
        m_socket.setBatchReceive(m_maxDatagramBytes <= 4096U);
        const bool eventDriven = canUseReadyRead_();
        m_socket.setReadNotificationsEnabled(eventDriven);
        const bool opened = m_socket.bind(bindAddress,
                                          port,
                                          SwUdpSocket::ShareAddress |
                                              SwUdpSocket::ReuseAddressHint);
        m_eventDriven.store(opened && eventDriven, std::memory_order_release);
        return opened;
    }

    void close() {
        m_eventDriven.store(false, std::memory_order_release);
        m_socket.close();
        clearTargetCache_();
    }

    bool isOpen() const {
        return m_socket.isOpen();
    }

    uint16_t localPort() const {
        return m_socket.localPort();
    }

    void setReceiveLimits(std::size_t maxDatagramBytes,
                          std::size_t maxPendingDatagrams,
                          std::size_t maxPendingBytes) {
        if (maxDatagramBytes > 0U) {
            m_maxDatagramBytes = std::min<std::size_t>(maxDatagramBytes, 65536U);
        }
        if (maxPendingDatagrams > 0U) {
            m_maxPendingDatagrams = maxPendingDatagrams;
        }
        if (maxPendingBytes > 0U) {
            m_maxPendingBytes = maxPendingBytes;
        }
    }

    void setPacketHandler(PacketHandler handler) {
        {
            std::lock_guard<std::mutex> lock(m_packetHandlerMutex);
            m_packetHandler = std::move(handler);
        }
        if (isOpen()) {
            const bool eventDriven = canUseReadyRead_();
            m_socket.setReadNotificationsEnabled(eventDriven);
            m_eventDriven.store(eventDriven, std::memory_order_release);
        }
    }

    bool eventDrivenReceiveActive() const {
        return m_eventDriven.load(std::memory_order_acquire);
    }

    void moveToThread(ThreadHandle* targetThread) {
        m_socket.moveToThread(targetThread);
        if (m_callbackContext) {
            m_callbackContext->moveToThread(targetThread);
        }
    }

    bool send(const SwByteArray& bytes, uint32_t ipv4, uint16_t port) {
        if (!isOpen() || bytes.isEmpty() || !bytes.constData() || port == 0U) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_targetCacheMutex);
        const uint64_t key = (static_cast<uint64_t>(ipv4) << 16U) |
                             static_cast<uint64_t>(port);
        auto target = m_targetCache.find(key);
        if (target == m_targetCache.end()) {
            if (m_targetCache.size() >= kMaxCachedTargets) {
                m_targetCache.erase(m_targetCache.begin());
            }
            target = m_targetCache.emplace(key, makeIpv4Target_(ipv4, port)).first;
        }
        const int64_t sent = m_socket.writeDatagram(bytes.constData(),
                                                    static_cast<int64_t>(bytes.size()),
                                                    target->second);
        return sent == static_cast<int64_t>(bytes.size());
    }

    bool receive(int timeoutMs, SwVtpUdpPacket& outPacket) {
        if (!isOpen() || eventDrivenReceiveActive()) {
            return false;
        }
        if (!m_socket.hasPendingDatagrams() &&
            !m_socket.pollPendingDatagrams(std::max(0, timeoutMs))) {
            return false;
        }
        return takeQueuedPacket_(outPacket);
    }

private:
    static SwString ipv4ToString_(uint32_t ipv4) {
        return SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 0))) + "." +
               SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 1))) + "." +
               SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 2))) + "." +
               SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 3)));
    }

    static SwUdpSocket::ResolvedAddress makeIpv4Target_(uint32_t ipv4, uint16_t port) {
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        target.sin_addr.s_addr = htonl(ipv4);

        SwUdpSocket::ResolvedAddress address;
        std::memcpy(&address.storage, &target, sizeof(target));
        address.length = sizeof(target);
        address.family = AF_INET;
        address.address = ipv4ToString_(ipv4);
        address.port = port;
        return address;
    }

    static uint32_t nativeIpv4_(const sockaddr_storage& sender) {
        if (sender.ss_family == AF_INET) {
            const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&sender);
            return ntohl(ipv4->sin_addr.s_addr);
        }
        if (sender.ss_family == AF_INET6) {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&sender);
            if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
                uint32_t address = 0;
                std::memcpy(&address, ipv6->sin6_addr.s6_addr + 12, sizeof(address));
                return ntohl(address);
            }
        }
        return 0U;
    }

    bool hasPacketHandler_() const {
        std::lock_guard<std::mutex> lock(m_packetHandlerMutex);
        return static_cast<bool>(m_packetHandler);
    }

    bool canUseReadyRead_() const {
        if (!hasPacketHandler_()) {
            return false;
        }
        ThreadHandle* affinity = m_socket.threadHandle();
        return (affinity && affinity->application()) ||
               SwCoreApplication::instance(false) != nullptr;
    }

    bool takeQueuedPacket_(SwVtpUdpPacket& outPacket) {
        while (m_socket.hasPendingDatagrams()) {
            uint16_t senderPort = 0;
            bool truncated = false;
            sockaddr_storage sender{};
            SwByteArray bytes = m_socket.receiveDatagram(nullptr,
                                                         &senderPort,
                                                         &truncated,
                                                         nullptr,
                                                         &sender);
            if (truncated || bytes.isEmpty()) {
                continue;
            }
            outPacket.bytes = std::move(bytes);
            outPacket.senderIpv4 = nativeIpv4_(sender);
            outPacket.senderPort = senderPort;
            return outPacket.senderIpv4 != 0U && outPacket.senderPort != 0U;
        }
        return false;
    }

    void drainReadyDatagrams_() {
        while (isOpen() && eventDrivenReceiveActive()) {
            SwVtpUdpPacket packet;
            if (!takeQueuedPacket_(packet)) {
                break;
            }
            PacketHandler handler;
            {
                std::lock_guard<std::mutex> lock(m_packetHandlerMutex);
                handler = m_packetHandler;
            }
            if (!handler) {
                break;
            }
            handler(std::move(packet));
        }
    }

    void clearTargetCache_() {
        std::lock_guard<std::mutex> lock(m_targetCacheMutex);
        m_targetCache.clear();
    }

    SwUdpSocket m_socket{};
    SwObject* m_callbackContext{nullptr};
    mutable std::mutex m_packetHandlerMutex{};
    PacketHandler m_packetHandler{};
    std::atomic<bool> m_eventDriven{false};
    std::size_t m_maxDatagramBytes{65536U};
    std::size_t m_maxPendingDatagrams{512U};
    std::size_t m_maxPendingBytes{4U * 1024U * 1024U};
    std::mutex m_targetCacheMutex{};
    std::unordered_map<uint64_t, SwUdpSocket::ResolvedAddress> m_targetCache{};
    static constexpr std::size_t kMaxCachedTargets = 1024U;
};
