#pragma once

/**
 * @file src/media/transport/SwUdpServerTransport.h
 * @brief UDP datagram transport implementation of the common media server interface.
 */

#include "core/io/SwUdpSocket.h"
#include "core/types/SwList.h"
#include "media/server/SwVideoTransportServer.h"

#include <mutex>
#include <utility>
#include <vector>

class SwUdpServerTransport : public SwVideoTransportServer {
public:
    explicit SwUdpServerTransport(const SwString& protocolName = SwString("udp"))
        : m_protocolName(protocolName) {}

    SwString protocolName() const override {
        return m_protocolName;
    }

    bool addStream(const SwVideoPublishStream& stream) override {
        if (!stream.isValid()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            if (it->id == stream.id) {
                *it = stream;
                return true;
            }
        }
        m_streams.append(stream);
        if (m_metrics.targetBitrateKbps == 0U) {
            m_metrics.targetBitrateKbps = stream.startBitrateKbps;
            m_metrics.encoderBitrateKbps = stream.startBitrateKbps;
        }
        return true;
    }

    bool start() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_streams.isEmpty() || config().endpoint.port == 0U) {
            return false;
        }

        m_destinationHost = destinationHostFromConfig_();
        m_destinationPort = config().endpoint.port;
        if (m_destinationHost.isEmpty()) {
            return false;
        }

        const SwString bindAddress = bindAddressFromConfig_();
        m_socket.close();
        m_socket.setBroadcastEnabled(config().endpoint.deliveryMode ==
                                     SwMediaTransportDeliveryMode::Broadcast);
        if (!m_socket.bind(bindAddress,
                           0,
                           SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint)) {
            return false;
        }
        if (!configureSocketOptionsLocked_()) {
            m_socket.close();
            return false;
        }
        m_running = true;
        return true;
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_socket.close();
        m_running = false;
    }

    bool isRunning() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    bool publishVideoPacket(const SwString& streamId,
                            const SwVideoPacket& packet) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        const SwVideoPublishStream* stream = findStreamLocked_(streamId);
        if (!m_running || !stream || packet.payload().isEmpty()) {
            ++m_metrics.framesDropped;
            return false;
        }

        ++m_metrics.framesAccepted;
        m_metrics.videoBytesAccepted += packet.payload().size();

        const std::vector<SwByteArray> datagrams = makeDatagramsLocked_(*stream, packet);
        if (datagrams.empty()) {
            ++m_metrics.framesDropped;
            ++m_metrics.transport.sendFailures;
            return false;
        }

        bool sentAll = true;
        size_t bytesSent = 0U;
        size_t datagramsSent = 0U;
        for (size_t i = 0; i < datagrams.size(); ++i) {
            const SwByteArray& datagram = datagrams[i];
            if (datagram.isEmpty() || !sendDatagramLocked_(datagram)) {
                sentAll = false;
                break;
            }
            bytesSent += datagram.size();
            ++datagramsSent;
        }

        if (!sentAll) {
            ++m_metrics.framesDropped;
            ++m_metrics.transport.sendFailures;
            return false;
        }

        ++m_metrics.framesSent;
        m_metrics.videoBytesSent += packet.payload().size();
        m_metrics.transport.datagramsSent += datagramsSent;
        m_metrics.transport.bytesSent += bytesSent;
        return true;
    }

    SwVideoServerMetrics metrics() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_metrics;
    }

protected:
    virtual SwByteArray makeDatagramLocked_(const SwVideoPublishStream& stream,
                                            const SwVideoPacket& packet) {
        (void)stream;
        return packet.payload();
    }

    virtual std::vector<SwByteArray> makeDatagramsLocked_(const SwVideoPublishStream& stream,
                                                          const SwVideoPacket& packet) {
        std::vector<SwByteArray> datagrams;
        SwByteArray datagram = makeDatagramLocked_(stream, packet);
        if (!datagram.isEmpty()) {
            datagrams.push_back(std::move(datagram));
        }
        return datagrams;
    }

    bool sendDatagramLocked_(const SwByteArray& datagram) {
        if (!m_socket.isOpen() || datagram.isEmpty() || !datagram.constData()) {
            return false;
        }
        const int64_t sent = m_socket.writeDatagram(datagram.constData(),
                                                    static_cast<int64_t>(datagram.size()),
                                                    m_destinationHost,
                                                    m_destinationPort);
        return sent == static_cast<int64_t>(datagram.size());
    }

    const SwVideoPublishStream* findStreamLocked_(const SwString& streamId) const {
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            if (it->id == streamId || it->trackId == streamId) {
                return &(*it);
            }
        }
        return nullptr;
    }

    mutable std::mutex m_mutex;
    SwList<SwVideoPublishStream> m_streams{};
    SwVideoServerMetrics m_metrics{};
    bool m_running{false};

private:
    SwString destinationHostFromConfig_() const {
        const SwTransportEndpoint& endpoint = config().endpoint;
        if (endpoint.deliveryMode == SwMediaTransportDeliveryMode::Broadcast) {
            return endpoint.host.isEmpty() || endpoint.host == "0.0.0.0"
                       ? SwString("255.255.255.255")
                       : endpoint.host;
        }
        return endpoint.host.isEmpty() || endpoint.host == "0.0.0.0"
                   ? SwString()
                   : endpoint.host;
    }

    SwString bindAddressFromConfig_() const {
        if (!config().endpoint.bindAddress.isEmpty() &&
            config().endpoint.bindAddress != "0.0.0.0") {
            return config().endpoint.bindAddress;
        }
        return destinationHostFromConfig_().contains(":") ? SwString("::") : SwString("0.0.0.0");
    }

    bool configureSocketOptionsLocked_() {
        const SwTransportEndpoint& endpoint = config().endpoint;
        if (endpoint.deliveryMode != SwMediaTransportDeliveryMode::Multicast) {
            return true;
        }
        if (!m_socket.setMulticastTimeToLive(endpoint.ttl)) {
            return false;
        }
        if (!m_socket.setMulticastLoopbackEnabled(endpoint.multicastLoopback)) {
            return false;
        }
        if (!endpoint.interfaceAddress.isEmpty() &&
            !m_socket.setMulticastInterface(endpoint.interfaceAddress)) {
            return false;
        }
        return true;
    }

    SwString m_protocolName{};
    SwUdpSocket m_socket{};
    SwString m_destinationHost{};
    uint16_t m_destinationPort{0};
};
