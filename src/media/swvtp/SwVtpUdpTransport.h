#pragma once

/**
 * @file src/media/swvtp/SwVtpUdpTransport.h
 * @brief SwVTP UDP adapter built on the CoreSw SwUdpSocket abstraction.
 */

#include "core/io/SwUdpSocket.h"
#include "core/types/SwByteArray.h"
#include "media/swvtp/SwVtpProtocol.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

struct SwVtpUdpPacket {
    SwByteArray bytes{};
    uint32_t senderIpv4{0};
    uint16_t senderPort{0};
};

class SwVtpUdpTransport {
public:
    SwVtpUdpTransport() = default;

    ~SwVtpUdpTransport() {
        close();
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
        m_socket.setMaxDatagramSize(65536);
        m_socket.setMaxPendingDatagrams(2048);
        m_socket.setMaxReadBatchDatagrams(256);
        return m_socket.bind(bindAddress,
                             port,
                             SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint);
    }

    void close() {
        m_socket.close();
        clearTargetCache_();
    }

    bool isOpen() const {
        return m_socket.isOpen();
    }

    uint16_t localPort() const {
        return m_socket.localPort();
    }

    bool send(const SwByteArray& bytes, uint32_t ipv4, uint16_t port) {
        if (!isOpen() || bytes.isEmpty() || !bytes.constData() || port == 0U) {
            return false;
        }
        const SwUdpSocket::ResolvedAddress target = cachedIpv4Target_(ipv4, port);
        const int64_t sent = m_socket.writeDatagram(bytes.constData(),
                                                    static_cast<int64_t>(bytes.size()),
                                                    target);
        return sent == static_cast<int64_t>(bytes.size());
    }

    bool receive(int timeoutMs, SwVtpUdpPacket& outPacket) {
        if (!isOpen()) {
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        while (true) {
            m_socket.pollPendingDatagrams(0);
            if (m_socket.hasPendingDatagrams()) {
                SwString sender;
                uint16_t senderPort = 0;
                SwByteArray bytes = m_socket.receiveDatagram(&sender, &senderPort);
                if (bytes.isEmpty()) {
                    return false;
                }

                uint32_t senderIpv4 = 0;
                swVtpParseIpv4Address(sender.toStdString().c_str(), senderIpv4);
                outPacket.bytes = std::move(bytes);
                outPacket.senderIpv4 = senderIpv4;
                outPacket.senderPort = senderPort;
                return true;
            }

            if (timeoutMs <= 0) {
                return false;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMs) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    struct CachedTarget {
        uint32_t ipv4{0};
        uint16_t port{0};
        SwUdpSocket::ResolvedAddress address{};
    };

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

    SwUdpSocket::ResolvedAddress cachedIpv4Target_(uint32_t ipv4, uint16_t port) {
        std::lock_guard<std::mutex> lock(m_targetCacheMutex);
        for (const CachedTarget& target : m_targetCache) {
            if (target.ipv4 == ipv4 && target.port == port) {
                return target.address;
            }
        }

        CachedTarget target;
        target.ipv4 = ipv4;
        target.port = port;
        target.address = makeIpv4Target_(ipv4, port);
        if (m_targetCache.size() >= kMaxCachedTargets) {
            m_targetCache.erase(m_targetCache.begin());
        }
        m_targetCache.push_back(target);
        return target.address;
    }

    void clearTargetCache_() {
        std::lock_guard<std::mutex> lock(m_targetCacheMutex);
        m_targetCache.clear();
    }

    SwUdpSocket m_socket{};
    std::mutex m_targetCacheMutex{};
    std::vector<CachedTarget> m_targetCache{};
    static constexpr std::size_t kMaxCachedTargets = 1024U;
};
