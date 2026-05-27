#pragma once

/**
 * @file src/media/transport/SwUdpServerTransport.h
 * @brief UDP datagram transport implementation of the common media server interface.
 */

#include "core/io/SwUdpSocket.h"
#include "core/types/SwList.h"
#include "media/server/SwVideoTransportServer.h"

#include <mutex>

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

        const SwString bindAddress = config().endpoint.bindAddress.isEmpty()
                                         ? SwString("0.0.0.0")
                                         : config().endpoint.bindAddress;
        m_socket.close();
        m_socket.setBroadcastEnabled(config().endpoint.deliveryMode ==
                                     SwMediaTransportDeliveryMode::Broadcast);
        if (!m_socket.bind(bindAddress,
                           0,
                           SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint)) {
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

        const SwByteArray datagram = makeDatagramLocked_(*stream, packet);
        if (datagram.isEmpty() || !sendDatagramLocked_(datagram)) {
            ++m_metrics.framesDropped;
            ++m_metrics.transport.sendFailures;
            return false;
        }

        ++m_metrics.framesSent;
        m_metrics.videoBytesSent += packet.payload().size();
        ++m_metrics.transport.datagramsSent;
        m_metrics.transport.bytesSent += datagram.size();
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

    SwString m_protocolName{};
    SwUdpSocket m_socket{};
    SwString m_destinationHost{};
    uint16_t m_destinationPort{0};
};
