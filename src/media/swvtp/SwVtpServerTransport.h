#pragma once

/**
 * @file src/media/swvtp/SwVtpServerTransport.h
 * @brief SwVTP implementation of the common video transport server interface.
 */

#include "media/server/SwVideoTransportServer.h"
#include "media/SwAv1Bitstream.h"
#include "media/swvtp/SwVtpAv1.h"
#include "media/swvtp/SwVtpFeedbackController.h"
#include "media/swvtp/SwVtpKlv.h"
#include "media/swvtp/SwVtpUdpTransport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

class SwVtpServerTransport : public SwVideoTransportServer {
public:
    SwString protocolName() const override {
        return "swvtp";
    }

    bool addStream(const SwVideoPublishStream& stream) override {
        if (!stream.isValid()) {
            return false;
        }
        if (stream.codec != SwVideoPacket::Codec::AV1 &&
            stream.codec != SwVideoPacket::Codec::H264 &&
            stream.codec != SwVideoPacket::Codec::H265) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stream = stream;
        SwVtpAdaptiveBitratePolicy policy;
        policy.startBitrateKbps = stream.startBitrateKbps;
        policy.minBitrateKbps = stream.minBitrateKbps;
        policy.maxBitrateKbps = stream.maxBitrateKbps;
        m_feedbackController.setPolicy(policy);
        m_feedbackController.reset(stream.startBitrateKbps);
        m_metrics.targetBitrateKbps = m_feedbackController.targetBitrateKbps();
        m_metrics.encoderBitrateKbps = stream.startBitrateKbps;
        m_streamConfig.streamId = 1;
        m_streamConfig.trackId = 1;
        m_streamConfig.trackType = SwVtpTrackType::Video;
        m_streamConfig.codec = swVtpCodecFromVideoCodec_(stream.codec);
        m_streamConfig.endpoint = endpointFromConfig_();
        m_av1SequenceHeader = SwByteArray();
        refreshKlvConfigLocked_();
        return m_streamConfig.isValid();
    }

    bool addMetadataTrack(const SwMediaTrack& track) override {
        if (!track.isValid() || !track.isMetadata() || !isKlvTrack_(track)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_klvTrack = track;
        refreshKlvConfigLocked_();
        return m_klvConfig.isValid();
    }

    bool start() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_stream.isValid() || !m_streamConfig.isValid()) {
            return false;
        }
        uint32_t bindIpv4 = kSwVtpIpv4Any;
        const SwString bindAddress = config().endpoint.bindAddress.isEmpty()
                                         ? SwString("0.0.0.0")
                                         : config().endpoint.bindAddress;
        if (!swVtpParseIpv4Address(bindAddress.toStdString().c_str(), bindIpv4)) {
            return false;
        }
        const uint16_t bindPort = config().endpoint.port != 0U ? config().endpoint.port : 55245;
        if (!m_udp.open(bindIpv4, bindPort)) {
            return false;
        }
        m_stopRequested.store(false);
        m_running = true;
        m_receiverThread = std::thread([this]() { receiverLoop_(); });
        return true;
    }

    void stop() override {
        m_stopRequested.store(true);
        if (m_receiverThread.joinable()) {
            m_receiverThread.join();
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_udp.close();
        m_clients.clear();
        m_recentFragments.clear();
        m_metrics.transport.activeClients = 0;
        m_running = false;
    }

    bool isRunning() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    std::size_t activeClientCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_clients.size();
    }

    bool publishVideoPacket(const SwString& streamId,
                            const SwVideoPacket& packet) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running ||
                (streamId != m_stream.id && streamId != m_stream.trackId) ||
                packet.codec() != m_stream.codec ||
                m_clients.empty()) {
                ++m_metrics.framesDropped;
                return false;
            }
            ++m_metrics.framesAccepted;
            m_metrics.videoBytesAccepted += packet.payload().size();
        }

        bool ok = false;
        if (packet.codec() != SwVideoPacket::Codec::AV1) {
            ok = publishOpaquePacket_(packet);
        } else {
            ok = publishAv1Packet_(packet);
        }
        return ok;
    }

    bool publishMediaPacket(const SwString& trackId,
                            const SwMediaPacket& packet) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running ||
                !m_klvTrack.isValid() ||
                !m_klvConfig.isValid() ||
                (trackId != m_klvTrack.id && packet.trackId() != m_klvTrack.id) ||
                !SwVtpKlvPacketizer::isKlvPacket(packet) ||
                m_clients.empty()) {
                ++m_metrics.framesDropped;
                return false;
            }
        }
        return publishKlvPacket_(packet);
    }

    SwVideoServerMetrics metrics() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_metrics;
    }

    const SwVtpStreamConfig& streamConfig() const {
        return m_streamConfig;
    }

protected:
    virtual bool writeDatagram_(const SwByteArray& datagram) {
        struct ClientEndpoint {
            uint32_t ipv4{0};
            uint16_t port{0};
        };

        ClientEndpoint singleClient;
        bool hasSingleClient = false;
        std::vector<ClientEndpoint> clients;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_clients.empty()) {
                return false;
            }
            if (m_clients.size() == 1U) {
                singleClient.ipv4 = m_clients.front().ipv4;
                singleClient.port = m_clients.front().port;
                hasSingleClient = true;
            } else {
                clients.reserve(m_clients.size());
                for (std::size_t i = 0; i < m_clients.size(); ++i) {
                    ClientEndpoint endpoint;
                    endpoint.ipv4 = m_clients[i].ipv4;
                    endpoint.port = m_clients[i].port;
                    clients.push_back(endpoint);
                }
            }
        }
        if (hasSingleClient) {
            return sendDatagramToClient_(datagram, singleClient.ipv4, singleClient.port);
        }
        bool allSent = true;
        for (std::size_t i = 0; i < clients.size(); ++i) {
            if (!sendDatagramToClient_(datagram, clients[i].ipv4, clients[i].port)) {
                allSent = false;
            }
        }
        return allSent;
    }

private:
    struct Client {
        uint32_t ipv4{0};
        uint16_t port{0};
        uint16_t streamId{0};
        uint64_t acceptedAtUs{0};
        uint64_t lastSeenUs{0};
        SwString id{};
    };

    struct CachedFragment {
        uint16_t streamId{0};
        uint16_t trackId{0};
        uint32_t frameId{0};
        uint16_t fragmentIndex{0};
        uint16_t fragmentCount{0};
        uint64_t storedAtUs{0};
        SwByteArray bytes{};
    };

    static uint64_t nowUs_() {
        const std::chrono::steady_clock::duration elapsed =
            std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    }

    static uint64_t nowMs_() {
        return nowUs_() / 1000ULL;
    }

    static uint64_t packetPtsUs_(const SwVideoPacket& packet, uint64_t fallbackUs) {
        return packet.pts() >= 0 ? static_cast<uint64_t>(packet.pts()) : fallbackUs;
    }

    static uint64_t packetCaptureTimeUs_(const SwVideoPacket& packet, uint64_t nowUs) {
        if (packet.pts() < 0) {
            return nowUs;
        }
        const uint64_t ptsUs = static_cast<uint64_t>(packet.pts());
        if (ptsUs < 1000000ULL) {
            return nowUs;
        }
        const uint64_t pastToleranceUs = 10ULL * 60ULL * 1000ULL * 1000ULL;
        const uint64_t futureToleranceUs = 10ULL * 1000ULL * 1000ULL;
        const bool plausibleMonotonicCapture =
            ptsUs + pastToleranceUs >= nowUs &&
            ptsUs <= nowUs + futureToleranceUs;
        return plausibleMonotonicCapture ? ptsUs : nowUs;
    }

    static SwString ipv4ToString_(uint32_t ipv4) {
        return SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 0))) + "." +
               SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 1))) + "." +
               SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 2))) + "." +
               SwString::number(static_cast<int>(swVtpIpv4Octet(ipv4, 3)));
    }

    static SwVtpCodec swVtpCodecFromVideoCodec_(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return SwVtpCodec::H264;
        case SwVideoPacket::Codec::H265:
            return SwVtpCodec::H265;
        case SwVideoPacket::Codec::AV1:
            return SwVtpCodec::AV1;
        default:
            break;
        }
        return SwVtpCodec::Unknown;
    }

    static bool isKlvTrack_(const SwMediaTrack& track) {
        const SwString codec = track.codec.toLower();
        return track.isMetadata() &&
               (codec.isEmpty() || codec == "klv" || codec == "smpte336m");
    }

    void refreshKlvConfigLocked_() {
        if (!m_klvTrack.isValid()) {
            m_klvConfig = SwVtpStreamConfig();
            return;
        }
        m_klvConfig.streamId = m_streamConfig.streamId != 0U ? m_streamConfig.streamId : 1U;
        m_klvConfig.trackId = 2;
        m_klvConfig.trackType = SwVtpTrackType::MetadataKlv;
        m_klvConfig.codec = SwVtpCodec::Klv;
        m_klvConfig.endpoint = endpointFromConfig_();
    }

    SwByteArray makeControlDatagram_(SwVtpMessageType type,
                                     const SwByteArray& payload) const {
        SwVtpDatagram datagram;
        datagram.header.version = kSwVtpVersion1;
        datagram.header.messageType = type;
        datagram.header.trackType = SwVtpTrackType::Control;
        datagram.header.codec = SwVtpCodec::Unknown;
        datagram.header.streamId = m_streamConfig.streamId;
        datagram.header.trackId = m_streamConfig.trackId;
        datagram.header.sendTimeUs = nowUs_();
        datagram.payload = payload;
        return swVtpSerializeDatagram(datagram);
    }

    void receiverLoop_() {
        while (!m_stopRequested.load()) {
            SwVtpUdpPacket packet;
            if (!m_udp.receive(50, packet)) {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_metrics.transport.datagramsReceived;
                m_metrics.transport.bytesReceived += packet.bytes.size();
            }
            SwVtpDatagram datagram;
            if (!swVtpParseDatagram(packet.bytes, datagram)) {
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::Ping) {
                handlePing_(datagram.payload, packet.senderIpv4, packet.senderPort);
            } else if (datagram.header.messageType == SwVtpMessageType::Hello) {
                handleHello_(datagram.payload, packet.senderIpv4, packet.senderPort);
            } else if (datagram.header.messageType == SwVtpMessageType::ReceiverStats) {
                handleReceiverStats_(datagram.payload, packet.senderIpv4, packet.senderPort);
            } else if (datagram.header.messageType == SwVtpMessageType::Nack) {
                handleNack_(datagram, packet.senderIpv4, packet.senderPort);
            } else if (datagram.header.messageType == SwVtpMessageType::KeyFrameRequest) {
                handleKeyFrameRequest_(datagram, packet.senderIpv4, packet.senderPort);
            }
        }
    }

    void handlePing_(const SwByteArray& payload,
                     uint32_t senderIpv4,
                     uint16_t senderPort) {
        SwVtpClockSyncPing ping;
        if (!swVtpParseClockSyncPing(payload, ping)) {
            return;
        }
        SwVtpClockSyncPong pong;
        pong.syncId = ping.syncId;
        pong.clientSendTimeUs = ping.clientSendTimeUs;
        pong.serverReceiveTimeUs = nowUs_();
        pong.serverSendTimeUs = nowUs_();
        sendDatagramToClient_(makeControlDatagram_(SwVtpMessageType::Pong,
                                                   swVtpSerializeClockSyncPong(pong)),
                              senderIpv4,
                              senderPort);
    }

    void handleHello_(const SwByteArray& payload,
                      uint32_t senderIpv4,
                      uint16_t senderPort) {
        SwVtpClientAnnouncement announcement;
        if (!swVtpParseClientAnnouncementPayload(payload, announcement)) {
            return;
        }
        if (announcement.clientIpv4 == kSwVtpIpv4Any) {
            announcement.clientIpv4 = senderIpv4;
        }
        if (announcement.receivePort == 0U) {
            announcement.receivePort = senderPort;
        }
        if (!swVtpStreamConfigAcceptsClient(m_streamConfig, announcement)) {
            return;
        }
        Client client;
        client.ipv4 = announcement.clientIpv4;
        client.port = announcement.receivePort;
        client.streamId = announcement.streamId;
        client.acceptedAtUs = nowUs_();
        client.lastSeenUs = client.acceptedAtUs;
        client.id = ipv4ToString_(client.ipv4) + ":" + SwString::number(client.port);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            upsertClientLocked_(client);
            m_metrics.transport.activeClients = static_cast<uint32_t>(m_clients.size());
        }
        sendDatagramToClient_(makeControlDatagram_(SwVtpMessageType::Accept,
                                                   swVtpSerializeStreamConfig(m_streamConfig)),
                              senderIpv4,
                              senderPort);
        sendKlvStreamConfig_(client);
        sendBitrateControl_(client);
    }

    void handleReceiverStats_(const SwByteArray& payload,
                              uint32_t senderIpv4,
                              uint16_t senderPort) {
        SwVtpReceiverStats stats;
        if (!swVtpParseReceiverStatsPayload(payload, stats)) {
            return;
        }

        SwVtpAdaptiveBitrateDecision decision =
            m_feedbackController.update(stats, nowMs_());
        SwVtpBitrateControl control;
        control.streamId = stats.streamId;
        control.trackId = stats.trackId;
        control.targetBitrateKbps = decision.targetBitrateKbps;
        control.encoderBitrateKbps = decision.targetBitrateKbps;
        control.estimatedBandwidthKbps = stats.estimatedBandwidthKbps;
        control.reason = static_cast<uint8_t>(decision.reason);
        control.flags = (decision.requestKeyFrame ? 0x01U : 0x00U) |
                        (decision.preferBaseTemporalLayer ? 0x02U : 0x00U);

        SwString clientId = ipv4ToString_(senderIpv4) + ":" + SwString::number(senderPort);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_metrics.targetBitrateKbps = decision.targetBitrateKbps;
            m_metrics.encoderBitrateKbps = decision.targetBitrateKbps;
            markClientSeenLocked_(senderIpv4, senderPort);
        }
        emitClientFeedback(swVtpReceiverStatsToClientFeedback(stats,
                                                              clientId,
                                                              decision.targetBitrateKbps,
                                                              decision.targetBitrateKbps,
                                                              decision.requestKeyFrame));
        sendDatagramToClient_(makeControlDatagram_(SwVtpMessageType::BitrateControl,
                                                   swVtpSerializeBitrateControl(control)),
                              senderIpv4,
                              senderPort);
    }

    void handleKeyFrameRequest_(const SwVtpDatagram& datagram,
                                uint32_t senderIpv4,
                                uint16_t senderPort) {
        const SwString clientId = ipv4ToString_(senderIpv4) + ":" + SwString::number(senderPort);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            markClientSeenLocked_(senderIpv4, senderPort);
        }

        SwVideoServerClientFeedback feedback;
        feedback.clientId = clientId;
        feedback.streamId = SwString::number(static_cast<int>(
            datagram.header.streamId != 0U ? datagram.header.streamId : m_streamConfig.streamId));
        feedback.targetBitrateKbps = m_feedbackController.targetBitrateKbps();
        feedback.encoderBitrateKbps = feedback.targetBitrateKbps;
        feedback.requestKeyFrame = true;
        emitClientFeedback(feedback);
    }

    void handleNack_(const SwVtpDatagram& datagram,
                     uint32_t senderIpv4,
                     uint16_t senderPort) {
        SwVtpNackRequest request;
        if (!swVtpParseNackPayload(datagram.header.streamId,
                                   datagram.header.trackId,
                                   datagram.payload,
                                   request)) {
            return;
        }

        std::vector<SwByteArray> retransmit;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            markClientSeenLocked_(senderIpv4, senderPort);
            trimFragmentCacheLocked_(nowUs_());
            for (auto missing = request.missingFragments.begin();
                 missing != request.missingFragments.end();
                 ++missing) {
                for (auto cached = m_recentFragments.rbegin();
                     cached != m_recentFragments.rend();
                     ++cached) {
                    if (cached->streamId == request.streamId &&
                        cached->trackId == request.trackId &&
                        cached->frameId == request.frameId &&
                        cached->fragmentIndex == *missing) {
                        retransmit.push_back(cached->bytes);
                        break;
                    }
                }
            }
        }

        uint64_t retransmitDatagrams = 0;
        uint64_t retransmitBytes = 0;
        for (std::size_t i = 0; i < retransmit.size(); ++i) {
            if (sendDatagramToClient_(retransmit[i], senderIpv4, senderPort)) {
                ++retransmitDatagrams;
                retransmitBytes += static_cast<uint64_t>(retransmit[i].size());
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_metrics.transport.sendFailures;
            }
        }
        addSentDatagramMetrics_(retransmitDatagrams, retransmitBytes);
    }

    void sendBitrateControl_(const Client& client) {
        SwVtpBitrateControl control;
        control.streamId = m_streamConfig.streamId;
        control.trackId = m_streamConfig.trackId;
        control.targetBitrateKbps = m_feedbackController.targetBitrateKbps();
        control.encoderBitrateKbps = control.targetBitrateKbps;
        control.reason = static_cast<uint8_t>(SwVtpAdaptiveBitrateDecision::Reason::Startup);
        sendDatagramToClient_(makeControlDatagram_(SwVtpMessageType::BitrateControl,
                                                   swVtpSerializeBitrateControl(control)),
                              client.ipv4,
                              client.port);
    }

    void sendKlvStreamConfig_(const Client& client) {
        if (!m_klvConfig.isValid()) {
            return;
        }
        sendDatagramToClient_(makeControlDatagram_(SwVtpMessageType::StreamConfig,
                                                   swVtpSerializeStreamConfig(m_klvConfig)),
                              client.ipv4,
                              client.port);
    }

    void upsertClientLocked_(const Client& client) {
        for (std::size_t i = 0; i < m_clients.size(); ++i) {
            if (m_clients[i].ipv4 == client.ipv4 && m_clients[i].port == client.port) {
                m_clients[i] = client;
                return;
            }
        }
        if (m_clients.size() < config().maxClients || config().maxClients == 0U) {
            m_clients.push_back(client);
            return;
        }

        auto oldest = std::min_element(
            m_clients.begin(),
            m_clients.end(),
            [](const Client& lhs, const Client& rhs) {
                return lhs.lastSeenUs < rhs.lastSeenUs;
            });
        if (oldest != m_clients.end()) {
            *oldest = client;
        }
    }

    void markClientSeenLocked_(uint32_t ipv4, uint16_t port) {
        const uint64_t now = nowUs_();
        for (std::size_t i = 0; i < m_clients.size(); ++i) {
            if (m_clients[i].ipv4 == ipv4 && m_clients[i].port == port) {
                m_clients[i].lastSeenUs = now;
                return;
            }
        }
    }

    void trimFragmentCacheLocked_(uint64_t nowUs) {
        while (!m_recentFragments.empty()) {
            const CachedFragment& fragment = m_recentFragments.front();
            const bool tooOld =
                fragment.storedAtUs != 0U &&
                nowUs > fragment.storedAtUs &&
                nowUs - fragment.storedAtUs > kFragmentCacheMaxAgeUs;
            if (m_recentFragments.size() <= kFragmentCacheMaxDatagrams && !tooOld) {
                break;
            }
            m_recentFragments.pop_front();
        }
    }

    void cacheFrameFragment_(const SwVtpDatagram& datagram,
                             const SwByteArray& bytes,
                             uint64_t nowUs) {
        CachedFragment fragment;
        if (!makeCachedFrameFragment_(datagram, bytes, nowUs, fragment)) {
            return;
        }
        std::vector<CachedFragment> fragments;
        fragments.push_back(fragment);
        cacheFrameFragments_(fragments, nowUs);
    }

    bool makeCachedFrameFragment_(const SwVtpDatagram& datagram,
                                  const SwByteArray& bytes,
                                  uint64_t nowUs,
                                  CachedFragment& fragment) const {
        if (datagram.header.messageType != SwVtpMessageType::FrameFragment ||
            datagram.header.trackType != SwVtpTrackType::Video ||
            datagram.header.fragmentCount == 0U) {
            return false;
        }
        fragment = CachedFragment();
        fragment.streamId = datagram.header.streamId;
        fragment.trackId = datagram.header.trackId;
        fragment.frameId = datagram.header.frameId;
        fragment.fragmentIndex = datagram.header.fragmentIndex;
        fragment.fragmentCount = datagram.header.fragmentCount;
        fragment.storedAtUs = nowUs;
        fragment.bytes = bytes;
        return true;
    }

    void cacheFrameFragments_(const std::vector<CachedFragment>& fragments,
                              uint64_t nowUs) {
        if (fragments.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        trimFragmentCacheLocked_(nowUs);
        for (std::size_t i = 0; i < fragments.size(); ++i) {
            m_recentFragments.push_back(fragments[i]);
        }
        trimFragmentCacheLocked_(nowUs);
    }

    void addSentDatagramMetrics_(uint64_t datagrams, uint64_t bytes) {
        if (datagrams == 0U && bytes == 0U) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.transport.datagramsSent += datagrams;
        m_metrics.transport.bytesSent += bytes;
    }

    void addPublishedFrameMetrics_(uint64_t datagrams,
                                   uint64_t transportBytes,
                                   uint64_t videoBytes) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.transport.datagramsSent += datagrams;
        m_metrics.transport.bytesSent += transportBytes;
        ++m_metrics.framesSent;
        m_metrics.videoBytesSent += videoBytes;
    }

    void addSendFailureDroppedFrame_() {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_metrics.transport.sendFailures;
        ++m_metrics.framesDropped;
    }

    static uint64_t saturatedAddUs_(uint64_t lhs, uint64_t rhs) {
        return lhs > std::numeric_limits<uint64_t>::max() - rhs
                   ? std::numeric_limits<uint64_t>::max()
                   : lhs + rhs;
    }

    uint64_t estimatedFrameDeadlineSlackUs_(std::size_t payloadBytes,
                                             std::size_t maxPayloadBytes) const {
        (void)payloadBytes;
        (void)maxPayloadBytes;
        return 0U;
    }

    bool sendDatagramToClient_(const SwByteArray& datagram,
                               uint32_t ipv4,
                               uint16_t port) {
        std::lock_guard<std::mutex> sendLock(m_sendMutex);
        return m_udp.send(datagram, ipv4, port);
    }

    SwVtpUdpEndpoint endpointFromConfig_() const {
        const SwTransportEndpoint& endpoint = config().endpoint;
        const uint16_t port = endpoint.port != 0U ? endpoint.port : 55245;
        if (endpoint.deliveryMode == SwMediaTransportDeliveryMode::Broadcast) {
            return swVtpMakeBroadcastEndpoint(port);
        }

        uint32_t ipv4 = kSwVtpIpv4Any;
        swVtpParseIpv4Address(endpoint.host.toStdString().c_str(), ipv4);
        if (endpoint.deliveryMode == SwMediaTransportDeliveryMode::Multicast) {
            return swVtpMakeMulticast239Endpoint(ipv4,
                                                 port,
                                                 endpoint.ttl,
                                                 endpoint.multicastLoopback);
        }
        return swVtpMakeUnicastEndpoint(ipv4, port);
    }

    bool publishOpaquePacket_(const SwVideoPacket& packet) {
        const SwByteArray& payload = packet.payload();
        const std::size_t payloadBytes = static_cast<std::size_t>(payload.size());
        const std::size_t maxDatagramBytes =
            std::max<std::size_t>(config().mtuBytes, kSwVtpHeaderBytes + 1U);
        const std::size_t maxPayloadBytes =
            std::min<std::size_t>(maxDatagramBytes - kSwVtpHeaderBytes, 0xFFFFU);
        const std::size_t fragmentCountSize =
            (payloadBytes + maxPayloadBytes - 1U) / maxPayloadBytes;
        if (fragmentCountSize == 0U || fragmentCountSize > 0xFFFFU) {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_metrics.framesDropped;
            return false;
        }

        const uint16_t fragmentCount = static_cast<uint16_t>(fragmentCountSize);
        const uint32_t frameId = m_nextFrameId++;
        const uint64_t nowUs = nowUs_();
        const uint64_t ptsUs = packetPtsUs_(packet, nowUs);
        const uint64_t captureTimeUs = packetCaptureTimeUs_(packet, nowUs);
        const uint64_t deadlineSlackUs =
            estimatedFrameDeadlineSlackUs_(payloadBytes, maxPayloadBytes);
        const uint64_t deadlineUs =
            saturatedAddUs_(saturatedAddUs_(nowUs,
                                            static_cast<uint64_t>(m_stream.latencyBudgetMs) *
                                                1000ULL),
                            deadlineSlackUs);

        std::vector<CachedFragment> cachedFragments;
        cachedFragments.reserve(fragmentCount);
        uint64_t sentDatagrams = 0;
        uint64_t sentBytes = 0;

        for (uint16_t fragmentIndex = 0; fragmentIndex < fragmentCount; ++fragmentIndex) {
            const std::size_t begin = static_cast<std::size_t>(fragmentIndex) * maxPayloadBytes;
            const std::size_t remaining = payloadBytes - begin;
            const std::size_t count = std::min(maxPayloadBytes, remaining);

            SwVtpDatagram datagram;
            datagram.header.version = kSwVtpVersion1;
            datagram.header.messageType = SwVtpMessageType::FrameFragment;
            datagram.header.streamId = m_streamConfig.streamId;
            datagram.header.trackId = m_streamConfig.trackId;
            datagram.header.trackType = SwVtpTrackType::Video;
            datagram.header.codec = m_streamConfig.codec;
            datagram.header.frameId = frameId;
            datagram.header.fragmentIndex = fragmentIndex;
            datagram.header.fragmentCount = fragmentCount;
            datagram.header.payloadBytes = static_cast<uint16_t>(count);
            datagram.header.ptsUs = ptsUs;
            datagram.header.captureTimeUs = captureTimeUs;
            datagram.header.sendTimeUs = nowUs_();
            datagram.header.deadlineUs = deadlineUs;
            if (packet.isKeyFrame()) {
                datagram.header.flags |= SwVtpFlag_KeyFrame | SwVtpFlag_Important;
            }
            if (packet.isDiscontinuity()) {
                datagram.header.flags |= SwVtpFlag_Discontinuity;
            }
            if (fragmentIndex == 0U) {
                datagram.header.flags |= SwVtpFlag_FirstFragment | SwVtpFlag_Important;
            }
            if (fragmentIndex + 1U == fragmentCount) {
                datagram.header.flags |= SwVtpFlag_LastFragment;
            }
            datagram.payload = payload.mid(static_cast<int>(begin), static_cast<int>(count));

            const SwByteArray bytes = swVtpSerializeDatagram(datagram);
            CachedFragment cachedFragment;
            if (makeCachedFrameFragment_(datagram, bytes, datagram.header.sendTimeUs, cachedFragment)) {
                cachedFragments.push_back(cachedFragment);
            }
            if (!writeDatagram_(bytes)) {
                cacheFrameFragments_(cachedFragments, nowUs);
                addSendFailureDroppedFrame_();
                return false;
            }
            ++sentDatagrams;
            sentBytes += static_cast<uint64_t>(bytes.size());
        }
        cacheFrameFragments_(cachedFragments, nowUs);
        addPublishedFrameMetrics_(sentDatagrams,
                                  sentBytes,
                                  static_cast<uint64_t>(payload.size()));
        return true;
    }

    bool publishAv1Packet_(const SwVideoPacket& packet) {
        SwVtpAv1PacketizerOptions options;
        options.streamId = m_streamConfig.streamId;
        options.trackId = m_streamConfig.trackId;
        options.frameId = m_nextFrameId++;
        options.nowUs = nowUs_();
        options.captureTimeUs = packetCaptureTimeUs_(packet, options.nowUs);
        options.latencyBudgetUs = static_cast<uint64_t>(m_stream.latencyBudgetMs) * 1000ULL;
        options.maxDatagramBytes = std::max<std::size_t>(config().mtuBytes,
                                                         kSwVtpHeaderBytes + 1U);
        options.deadlineSlackUs = estimatedFrameDeadlineSlackUs_(
            static_cast<std::size_t>(packet.payload().size()),
            options.maxDatagramBytes - kSwVtpHeaderBytes);

        SwVideoPacket packetWithCachedConfig;
        const SwVideoPacket* packetToPublish = &packet;
        SwVtpAv1PacketizerResult packetized =
            SwVtpAv1Packetizer::packetize(*packetToPublish, options);
        if (!packetized.ok) {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_metrics.framesDropped;
            return false;
        }

        if (!packetized.sequenceHeader.isEmpty()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_av1SequenceHeader = packetized.sequenceHeader;
        } else if (packet.isKeyFrame()) {
            SwByteArray cachedSequenceHeader;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cachedSequenceHeader = m_av1SequenceHeader;
            }
            if (!cachedSequenceHeader.isEmpty()) {
                packetWithCachedConfig = packet;
                packetWithCachedConfig.setPayload(
                    SwAv1Bitstream::insertSequenceHeader(packet.payload(),
                                                         cachedSequenceHeader));
                packetToPublish = &packetWithCachedConfig;
                options.deadlineSlackUs = estimatedFrameDeadlineSlackUs_(
                    static_cast<std::size_t>(packetToPublish->payload().size()),
                    options.maxDatagramBytes - kSwVtpHeaderBytes);
                packetized = SwVtpAv1Packetizer::packetize(*packetToPublish, options);
                if (!packetized.ok) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_metrics.framesDropped;
                    return false;
                }
            }
        }

        std::vector<CachedFragment> cachedFragments;
        cachedFragments.reserve(packetized.serializedDatagrams.size());
        for (std::size_t i = 0; i < packetized.serializedDatagrams.size(); ++i) {
            CachedFragment cachedFragment;
            if (makeCachedFrameFragment_(packetized.datagrams[i],
                                         packetized.serializedDatagrams[i],
                                         options.nowUs,
                                         cachedFragment)) {
                cachedFragments.push_back(cachedFragment);
            }
        }
        cacheFrameFragments_(cachedFragments, options.nowUs);

        uint64_t sentDatagrams = 0;
        uint64_t sentBytes = 0;
        for (auto it = packetized.serializedDatagrams.begin();
             it != packetized.serializedDatagrams.end();
             ++it) {
            if (!writeDatagram_(*it)) {
                addSendFailureDroppedFrame_();
                return false;
            }
            ++sentDatagrams;
            sentBytes += static_cast<uint64_t>(it->size());
        }
        addPublishedFrameMetrics_(sentDatagrams,
                                  sentBytes,
                                  static_cast<uint64_t>(packetToPublish->payload().size()));
        return true;
    }

    bool publishKlvPacket_(const SwMediaPacket& packet) {
        SwVtpKlvPacketizerOptions options;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            options.streamId = m_klvConfig.streamId;
            options.trackId = m_klvConfig.trackId;
            options.sampleId = m_nextKlvId++;
        }
        options.nowUs = nowUs_();
        options.captureTimeUs = packet.pts() >= 0
                                    ? static_cast<uint64_t>(packet.pts())
                                    : options.nowUs;
        options.latencyBudgetUs = static_cast<uint64_t>(m_stream.latencyBudgetMs) * 1000ULL;
        options.maxDatagramBytes = std::max<std::size_t>(config().mtuBytes,
                                                         kSwVtpHeaderBytes + 1U);

        const SwVtpKlvPacketizerResult packetized =
            SwVtpKlvPacketizer::packetize(packet, options);
        if (!packetized.ok) {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_metrics.framesDropped;
            return false;
        }

        uint64_t sentDatagrams = 0;
        uint64_t sentBytes = 0;
        for (auto it = packetized.serializedDatagrams.begin();
             it != packetized.serializedDatagrams.end();
             ++it) {
            if (!writeDatagram_(*it)) {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_metrics.transport.sendFailures;
                return false;
            }
            ++sentDatagrams;
            sentBytes += static_cast<uint64_t>(it->size());
        }
        addSentDatagramMetrics_(sentDatagrams, sentBytes);
        return true;
    }

    SwVideoPublishStream m_stream{};
    SwVtpStreamConfig m_streamConfig{};
    SwMediaTrack m_klvTrack{};
    SwVtpStreamConfig m_klvConfig{};
    SwByteArray m_av1SequenceHeader{};
    SwVideoServerMetrics m_metrics{};
    SwVtpFeedbackController m_feedbackController{};
    SwVtpUdpTransport m_udp{};
    std::vector<Client> m_clients{};
    std::deque<CachedFragment> m_recentFragments{};
    std::thread m_receiverThread{};
    std::atomic<bool> m_stopRequested{false};
    mutable std::mutex m_mutex;
    mutable std::mutex m_sendMutex;
    uint32_t m_nextFrameId{1};
    uint32_t m_nextKlvId{1};
    bool m_running{false};
    static constexpr std::size_t kFragmentCacheMaxDatagrams = 16384U;
    static constexpr uint64_t kFragmentCacheMaxAgeUs = 3000000ULL;
};
