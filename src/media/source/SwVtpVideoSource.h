#pragma once

/**
 * @file src/media/source/SwVtpVideoSource.h
 * @brief Low-latency SwVTP video source for SwMediaPlayer and SwVideoWidget.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwVideoSource.h"
#include "media/swvtp/SwVtpAv1.h"
#include "media/swvtp/SwVtpFrameReassembler.h"
#include "media/swvtp/SwVtpKlv.h"
#include "media/swvtp/SwVtpUdpTransport.h"
#include "core/object/SwObject.h"
#include "core/types/Sw.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

static constexpr const char* kSwLogCategory_SwVtpVideoSource = "sw.media.swvtpvideosource";

struct SwVtpVideoSourceMetrics {
    uint64_t datagramsReceived{0};
    uint64_t datagramBytesReceived{0};
    uint64_t framesCompleted{0};
    uint64_t videoBytesCompleted{0};
    uint64_t klvPacketsCompleted{0};
    uint64_t klvBytesCompleted{0};
    uint64_t klvAcceptedFragments{0};
    uint64_t klvDuplicateFragments{0};
    uint64_t klvStaleFragments{0};
    uint64_t klvDroppedPackets{0};
    uint64_t duplicateFragments{0};
    uint64_t staleFragments{0};
    uint64_t droppedFrames{0};
    uint64_t acceptedFragments{0};
    uint64_t nackRequestsSent{0};
    uint64_t nackFragmentsRequested{0};
    uint64_t transferLatencySamples{0};
    uint64_t transferLatencyTotalUs{0};
    uint64_t transferLatencyMinUs{0};
    uint64_t transferLatencyMaxUs{0};
    uint64_t captureLatencySamples{0};
    uint64_t captureLatencyTotalUs{0};
    uint64_t captureLatencyMinUs{0};
    uint64_t captureLatencyMaxUs{0};
    uint64_t clockRttUs{0};
    uint64_t clockUncertaintyUs{0};
    double liveVideoKbps{0.0};
    double liveUdpKbps{0.0};
    uint32_t negotiatedTargetBitrateKbps{0};
    uint32_t negotiatedEncoderBitrateKbps{0};
    uint32_t estimatedBandwidthKbps{0};
    uint8_t bitrateReason{0};
    uint8_t bitrateFlags{0};
    uint8_t clockConfidencePercent{0};
    uint16_t localPort{0};
    SwString serverAddress{};
    uint16_t serverPort{0};
    SwString announcedAddress{};
    bool clockSynced{false};
    bool accepted{false};

    double averageTransferLatencyMs() const {
        return transferLatencySamples == 0U
                   ? 0.0
                   : static_cast<double>(transferLatencyTotalUs) /
                         static_cast<double>(transferLatencySamples) / 1000.0;
    }

    double minTransferLatencyMs() const {
        return transferLatencySamples == 0U ? 0.0
                                            : static_cast<double>(transferLatencyMinUs) / 1000.0;
    }

    double maxTransferLatencyMs() const {
        return transferLatencySamples == 0U ? 0.0
                                            : static_cast<double>(transferLatencyMaxUs) / 1000.0;
    }

    double averageCaptureLatencyMs() const {
        return captureLatencySamples == 0U
                   ? 0.0
                   : static_cast<double>(captureLatencyTotalUs) /
                         static_cast<double>(captureLatencySamples) / 1000.0;
    }

    double minCaptureLatencyMs() const {
        return captureLatencySamples == 0U ? 0.0
                                           : static_cast<double>(captureLatencyMinUs) / 1000.0;
    }

    double maxCaptureLatencyMs() const {
        return captureLatencySamples == 0U ? 0.0
                                           : static_cast<double>(captureLatencyMaxUs) / 1000.0;
    }
};

class SwVtpVideoSource : public SwVideoSource {
public:
    using MetricsCallback = std::function<void(const SwVtpVideoSourceMetrics&)>;

    explicit SwVtpVideoSource(const SwMediaOpenOptions& options, SwObject* parent = nullptr)
        : m_options(options) {
        SW_UNUSED(parent);

        SwList<SwMediaTrack> tracks;
        SwMediaTrack video;
        video.id = "video";
        video.type = SwMediaTrack::Type::Video;
        video.codec = "unknown";
        video.selected = true;
        video.availability = SwMediaTrack::Availability::Available;
        tracks.append(video);
        setTracks(tracks);
    }

    ~SwVtpVideoSource() override {
        stop();
    }

    SwString name() const override { return "SwVtpVideoSource"; }

    void setMetricsCallback(MetricsCallback callback) {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        m_metricsCallback = std::move(callback);
    }

    SwVtpVideoSourceMetrics metrics() const {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        return m_metrics;
    }

    void start() override {
        if (isRunning()) {
            return;
        }
        m_serverHost = m_options.mediaUrl.host();
        m_serverPort = static_cast<uint16_t>(m_options.mediaUrl.port() > 0
                                                 ? m_options.mediaUrl.port()
                                                 : 55245);
        m_bindAddress = m_options.bindAddress.isEmpty() ? SwString("0.0.0.0")
                                                        : m_options.bindAddress;
        if (!swVtpParseIpv4Address(m_serverHost.toStdString().c_str(), m_serverIpv4)) {
            emitStatus(StreamState::Recovering, "SwVTP server host must be an IPv4 address");
            return;
        }
        if (!swVtpParseIpv4Address(m_bindAddress.toStdString().c_str(), m_bindIpv4)) {
            emitStatus(StreamState::Recovering, "SwVTP bind address must be an IPv4 address");
            return;
        }
        if (!resolveAnnounceAddress_()) {
            emitStatus(StreamState::Recovering, "Invalid SwVTP announce address");
            return;
        }

        emitStatus(StreamState::Connecting, "Opening SwVTP UDP source...");
        m_clockEstimate = SwVtpClockEstimate();
        m_streamConfig = SwVtpStreamConfig();
        m_reassembler.reset();
        m_klvReassembler.reset();
        m_klvTrackId = 0;
        m_lastPacketTime = {};
        m_lastHelloUs = 0;
        m_lastPingUs = 0;
        m_lastStatsUs = 0;
        m_lastNackUs = 0;
        m_lastKeyFrameRequestUs = 0;
        m_lastRecoveryEmitUs = 0;
        m_lastReconnectProbeUs = 0;
        m_lastAcceptUs = 0;
        m_lastVideoReceiveUs = 0;
        m_noVideoRecoveryActive = false;
        m_lastNackFrameId = 0;
        m_lastFrameId = 0;
        m_lastVideoPtsUs = -1;
        m_videoFrameIntervalUs = kDefaultVideoFrameIntervalUs;
        m_lastTimestampNormalizeLogUs = 0;
        m_nextSyncId = 1;
        m_rateWindowStartUs = nowUs_();
        m_rateWindowDatagramBytes = 0;
        m_rateWindowVideoBytes = 0;
        m_stopRequested.store(false);
        m_listenPort = m_options.rtpPort;

        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics = SwVtpVideoSourceMetrics();
            m_metrics.serverAddress = m_serverHost;
            m_metrics.serverPort = m_serverPort;
            m_metrics.announcedAddress = m_announceAddress;
        }

        m_announcement.streamId = static_cast<uint16_t>(
            queryInt_("stream", 1));
        setRunning(true);
        m_worker = std::thread([this]() { workerMain_(); });
    }

    void stop() override {
        setRunning(false);
        m_stopRequested.store(true);
        if (m_worker.joinable()) {
            m_worker.join();
        }
        m_reassembler.reset();
        m_klvReassembler.reset();
        emitStatus(StreamState::Stopped, "SwVTP stopped");
    }

private:
    static uint64_t nowUs_() {
        const std::chrono::steady_clock::duration elapsed =
            std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    }

    SwString queryValue_(const char* key) const {
        return m_options.mediaUrl.queryValue(SwString(key));
    }

    int queryInt_(const char* key, int fallback) const {
        const SwString value = queryValue_(key);
        if (value.isEmpty()) {
            return fallback;
        }
        char* end = nullptr;
        const long parsed = std::strtol(value.toStdString().c_str(), &end, 10);
        if (!end || *end != '\0') {
            return fallback;
        }
        return static_cast<int>(parsed);
    }

    SwString explicitAnnounceAddress_() const {
        SwString value = queryValue_("announce");
        if (value.isEmpty()) {
            value = queryValue_("client");
        }
        if (value.isEmpty()) {
            value = queryValue_("client-ip");
        }
        if (value.isEmpty()) {
            value = queryValue_("announce-ip");
        }
        return value;
    }

    bool resolveAnnounceAddress_() {
        m_announceAddress = explicitAnnounceAddress_();
        if (!m_announceAddress.isEmpty()) {
            return swVtpParseIpv4Address(m_announceAddress.toStdString().c_str(),
                                         m_announcement.clientIpv4) &&
                   swVtpIsIpv4UnicastAddress(m_announcement.clientIpv4);
        }

        if (m_serverHost == "127.0.0.1" || m_serverHost == "localhost") {
            m_announceAddress = "127.0.0.1";
            return swVtpParseIpv4Address("127.0.0.1", m_announcement.clientIpv4);
        }

        uint32_t bindIpv4 = 0;
        if (swVtpParseIpv4Address(m_bindAddress.toStdString().c_str(), bindIpv4) &&
            swVtpIsIpv4UnicastAddress(bindIpv4)) {
            m_announceAddress = m_bindAddress;
            m_announcement.clientIpv4 = bindIpv4;
            return true;
        }

        m_announceAddress = "auto";
        m_announcement.clientIpv4 = kSwVtpIpv4Any;
        return true;
    }

    static uint16_t clampU16_(uint64_t value) {
        return value > 65535ULL ? 65535U : static_cast<uint16_t>(value);
    }

    static uint32_t clampKbps_(double value) {
        if (value <= 0.0) {
            return 0;
        }
        if (value >= 4294967295.0) {
            return 0xFFFFFFFFU;
        }
        return static_cast<uint32_t>(value);
    }

    static uint64_t saturatedAddUs_(uint64_t lhs, uint64_t rhs) {
        return lhs > std::numeric_limits<uint64_t>::max() - rhs
                   ? std::numeric_limits<uint64_t>::max()
                   : lhs + rhs;
    }

    SwByteArray makeControlDatagram_(SwVtpMessageType type,
                                     const SwByteArray& payload) const {
        SwVtpDatagram datagram;
        datagram.header.version = kSwVtpVersion1;
        datagram.header.messageType = type;
        datagram.header.trackType = SwVtpTrackType::Control;
        datagram.header.codec = SwVtpCodec::Unknown;
        if (m_streamConfig.isValid()) {
            datagram.header.streamId = m_streamConfig.streamId;
            datagram.header.trackId = m_streamConfig.trackId;
        } else {
            datagram.header.streamId = m_announcement.streamId;
        }
        datagram.header.sendTimeUs = nowUs_();
        datagram.payload = payload;
        return swVtpSerializeDatagram(datagram);
    }

    bool sendControl_(SwVtpUdpTransport& transport,
                      SwVtpMessageType type,
                      const SwByteArray& payload) {
        const SwByteArray bytes = makeControlDatagram_(type, payload);
        return transport.send(bytes, m_serverIpv4, m_serverPort);
    }

    void sendClockPing_(SwVtpUdpTransport& transport) {
        SwVtpClockSyncPing ping;
        ping.syncId = m_nextSyncId++;
        ping.clientSendTimeUs = nowUs_();
        m_lastPingUs = ping.clientSendTimeUs;
        sendControl_(transport, SwVtpMessageType::Ping, swVtpSerializeClockSyncPing(ping));
    }

    bool sendKeyFrameRequest_(SwVtpUdpTransport& transport, uint64_t now) {
        static constexpr uint64_t kKeyFrameRequestCooldownUs = 500000ULL;
        if (!m_streamConfig.isValid()) {
            return false;
        }
        if (m_lastKeyFrameRequestUs != 0U &&
            now < m_lastKeyFrameRequestUs + kKeyFrameRequestCooldownUs) {
            return false;
        }
        if (!sendControl_(transport, SwVtpMessageType::KeyFrameRequest, SwByteArray())) {
            return false;
        }
        m_lastKeyFrameRequestUs = now;
        return true;
    }

    void triggerVideoResync_(SwVtpUdpTransport& transport,
                             uint64_t now,
                             const SwString& reason) {
        sendKeyFrameRequest_(transport, now);
        if (m_lastRecoveryEmitUs != 0U &&
            now >= m_lastRecoveryEmitUs &&
            now - m_lastRecoveryEmitUs < kRecoveryEmitCooldownUs) {
            return;
        }
        m_lastRecoveryEmitUs = now;
        swCWarning(kSwLogCategory_SwVtpVideoSource)
            << "[SwVtpVideoSource] Video reference lost, requesting decoder resync"
            << " reason=" << reason;
        emitRecovery(SwMediaSource::RecoveryEvent::Kind::LiveCut, reason);
    }

    void sendReconnectProbe_(SwVtpUdpTransport& transport,
                             uint64_t now,
                             uint64_t silentForUs) {
        if (m_lastReconnectProbeUs != 0U &&
            now >= m_lastReconnectProbeUs &&
            now - m_lastReconnectProbeUs < kReconnectProbeIntervalUs) {
            return;
        }
        m_lastReconnectProbeUs = now;

        if (!m_noVideoRecoveryActive) {
            m_noVideoRecoveryActive = true;
            m_reassembler.reset();
            m_klvReassembler.reset();
            m_lastFrameId = 0;
            m_lastNackFrameId = 0;
            m_lastNackUs = 0;
            m_lastKeyFrameRequestUs = 0;
            m_lastVideoPtsUs = -1;
            m_videoFrameIntervalUs = kDefaultVideoFrameIntervalUs;
            emitRecovery(SwMediaSource::RecoveryEvent::Kind::Reconnect,
                         "No SwVTP video received; reconnect probe");
        }

        swCWarning(kSwLogCategory_SwVtpVideoSource)
            << "[SwVtpVideoSource] No video received, probing reconnect"
            << " silentForMs=" << (silentForUs / 1000ULL)
            << " accepted=" << (m_streamConfig.isValid() ? 1 : 0);

        sendClockPing_(transport);
        sendAnnouncement_(transport);
        sendKeyFrameRequest_(transport, now);
    }

    void sendAnnouncement_(SwVtpUdpTransport& transport) {
        if (m_announcement.receivePort == 0U) {
            m_announcement.receivePort = transport.localPort();
        }
        m_lastHelloUs = nowUs_();
        sendControl_(transport,
                     SwVtpMessageType::Hello,
                     swVtpSerializeClientAnnouncement(m_announcement));
    }

    void sendReceiverStats_(SwVtpUdpTransport& transport, uint64_t now) {
        if (!m_streamConfig.isValid()) {
            return;
        }

        SwVtpReceiverStats stats;
        stats.streamId = m_streamConfig.streamId;
        stats.trackId = m_streamConfig.trackId;
        stats.lastFrameId = m_lastFrameId;

        SwVtpVideoSourceMetrics metrics;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            metrics = m_metrics;
        }
        const ConsumerPressure pressure = consumerPressure();
        const uint64_t frameTotal = metrics.framesCompleted + metrics.droppedFrames;

        stats.estimatedBandwidthKbps = clampKbps_(metrics.liveUdpKbps);
        if (stats.estimatedBandwidthKbps == 0U) {
            stats.estimatedBandwidthKbps = metrics.negotiatedTargetBitrateKbps;
        }
        stats.rttMs = clampU16_(metrics.clockRttUs / 1000ULL);
        stats.clockUncertaintyMs = clampU16_(metrics.clockUncertaintyUs / 1000ULL);
        stats.lossPermille = frameTotal == 0U
                                 ? 0U
                                 : clampU16_((metrics.droppedFrames * 1000ULL) / frameTotal);
        {
            const uint64_t fragmentTotal =
                metrics.acceptedFragments + metrics.nackFragmentsRequested;
            stats.nackPermille =
                fragmentTotal == 0U
                    ? 0U
                    : clampU16_((metrics.nackFragmentsRequested * 1000ULL) /
                                fragmentTotal);
        }
        stats.receiveQueueMs = clampU16_(static_cast<uint64_t>(pressure.queuedPackets) * 2ULL);
        if (pressure.softPressure) {
            stats.receiveQueueMs = std::max<uint16_t>(stats.receiveQueueMs, 25U);
        }
        if (pressure.hardPressure) {
            stats.receiveQueueMs = std::max<uint16_t>(stats.receiveQueueMs, 80U);
        }
        stats.decodeQueueMs =
            pressure.decoderStalled
                ? clampU16_(std::max<uint64_t>(
                      80ULL,
                      static_cast<uint64_t>(
                          std::max<std::int64_t>(0, pressure.stalledForMs))))
                : 0U;
        stats.transferLatencyMs = clampU16_(
            static_cast<uint64_t>(metrics.averageTransferLatencyMs() + 0.5));
        stats.captureLatencyMs = clampU16_(
            static_cast<uint64_t>(metrics.averageCaptureLatencyMs() + 0.5));
        stats.droppedFrames = metrics.droppedFrames > 0xFFFFFFFFULL
                                  ? 0xFFFFFFFFU
                                  : static_cast<uint32_t>(metrics.droppedFrames);

        if (sendControl_(transport,
                         SwVtpMessageType::ReceiverStats,
                         swVtpSerializeReceiverStats(stats))) {
            m_lastStatsUs = now;
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.estimatedBandwidthKbps = stats.estimatedBandwidthKbps;
        }
        if (pressure.hardPressure || pressure.decoderStalled) {
            sendKeyFrameRequest_(transport, now);
        }
    }

    void maintainSession_(SwVtpUdpTransport& transport) {
        if (!isRunning()) {
            return;
        }
        const uint64_t now = nowUs_();
        if (!m_clockEstimate.valid && (m_lastPingUs == 0U || now - m_lastPingUs > 500000ULL)) {
            sendClockPing_(transport);
        }
        if (!m_streamConfig.isValid() &&
            (m_lastHelloUs == 0U || now - m_lastHelloUs > 500000ULL)) {
            sendAnnouncement_(transport);
        }
        if (m_streamConfig.isValid() &&
            (m_lastStatsUs == 0U || now - m_lastStatsUs > 250000ULL)) {
            sendReceiverStats_(transport, now);
        }
        if (m_streamConfig.isValid()) {
            const uint64_t lastVideoUs = m_lastVideoReceiveUs != 0U
                                             ? m_lastVideoReceiveUs
                                             : m_lastAcceptUs;
            if (lastVideoUs != 0U && now > lastVideoUs) {
                const uint64_t silentForUs = now - lastVideoUs;
                if (silentForUs > kNoVideoReconnectTimeoutUs) {
                    emitStatus(StreamState::Recovering,
                               SwString("No SwVTP video received for ") +
                                   SwString::number(static_cast<int>(silentForUs / 1000000ULL)) +
                                   SwString(" s"));
                    sendReconnectProbe_(transport, now, silentForUs);
                }
            }
        }
    }

    void workerMain_() {
        SwVtpUdpTransport transport;
        if (!transport.open(m_bindIpv4, m_listenPort)) {
            emitStatus(StreamState::Recovering, "Failed to bind SwVTP UDP socket");
            return;
        }
        m_announcement.receivePort = transport.localPort();
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.localPort = m_announcement.receivePort;
        }
        publishMetrics_();

        sendClockPing_(transport);
        sendAnnouncement_(transport);

        while (!m_stopRequested.load()) {
            maintainSession_(transport);
            SwVtpUdpPacket packet;
            if (!transport.receive(50, packet)) {
                continue;
            }

            m_lastPacketTime = std::chrono::steady_clock::now();

            SwVtpDatagram datagram;
            if (!swVtpParseDatagram(packet.bytes, datagram)) {
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::Pong) {
                handlePong_(datagram.payload);
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::Accept) {
                handleAccept_(transport, datagram.payload);
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::StreamConfig) {
                handleStreamConfig_(datagram.payload);
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::BitrateControl) {
                handleBitrateControl_(datagram.payload);
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::FrameFragment) {
                handleFrameFragment_(transport, datagram, packet.bytes.size());
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::KlvFragment) {
                handleKlvFragment_(datagram, packet.bytes.size());
                continue;
            }
        }
        transport.close();
    }

    void handlePong_(const SwByteArray& payload) {
        SwVtpClockSyncPong pong;
        if (!swVtpParseClockSyncPong(payload, pong)) {
            return;
        }
        SwVtpClockSyncSample sample;
        sample.syncId = pong.syncId;
        sample.clientSendTimeUs = pong.clientSendTimeUs;
        sample.serverReceiveTimeUs = pong.serverReceiveTimeUs;
        sample.serverSendTimeUs = pong.serverSendTimeUs;
        sample.clientReceiveTimeUs = nowUs_();
        m_clockEstimate = swVtpEstimateClock(sample);
        if (m_clockEstimate.valid) {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.clockSynced = true;
            m_metrics.clockRttUs = m_clockEstimate.rttUs;
            m_metrics.clockUncertaintyUs = m_clockEstimate.oneWayUncertaintyUs;
            m_metrics.clockConfidencePercent = m_clockEstimate.confidencePercent;
        }
        publishMetrics_();
    }

    void handleAccept_(SwVtpUdpTransport& transport, const SwByteArray& payload) {
        SwVtpStreamConfig config;
        if (!swVtpParseStreamConfigPayload(payload, config)) {
            return;
        }
        const uint64_t now = nowUs_();
        m_streamConfig = config;
        m_lastAcceptUs = now;
        m_lastReconnectProbeUs = 0;
        m_noVideoRecoveryActive = false;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.accepted = true;
        }
        updateVideoTrack_(config.codec);
        emitStatus(StreamState::Streaming, "SwVTP accepted");
        sendKeyFrameRequest_(transport, now);
        publishMetrics_();
    }

    void handleStreamConfig_(const SwByteArray& payload) {
        SwVtpStreamConfig config;
        if (!swVtpParseStreamConfigPayload(payload, config)) {
            return;
        }
        if (config.trackType == SwVtpTrackType::MetadataKlv &&
            config.codec == SwVtpCodec::Klv) {
            m_klvTrackId = config.trackId;
            m_klvReassembler.setTrackId("klv");
            addKlvTrack_();
        }
    }

    void addKlvTrack_() {
        SwList<SwMediaTrack> currentTracks = tracks();
        for (auto it = currentTracks.begin(); it != currentTracks.end(); ++it) {
            if (it->id == "klv") {
                return;
            }
        }
        SwMediaTrack klv;
        klv.id = "klv";
        klv.type = SwMediaTrack::Type::Metadata;
        klv.codec = "klv";
        klv.clockRate = 90000;
        klv.selected = true;
        klv.availability = SwMediaTrack::Availability::Available;
        currentTracks.append(klv);
        setTracks(currentTracks);
    }

    void updateVideoTrack_(SwVtpCodec codec) {
        const SwString codecName = swVtpVideoCodecName(codec);
        if (codecName == "unknown") {
            return;
        }
        SwList<SwMediaTrack> currentTracks = tracks();
        bool updated = false;
        for (auto it = currentTracks.begin(); it != currentTracks.end(); ++it) {
            if (it->type == SwMediaTrack::Type::Video || it->id == "video") {
                it->id = "video";
                it->type = SwMediaTrack::Type::Video;
                it->codec = codecName;
                it->selected = true;
                it->availability = SwMediaTrack::Availability::Available;
                updated = true;
                break;
            }
        }
        if (!updated) {
            SwMediaTrack video;
            video.id = "video";
            video.type = SwMediaTrack::Type::Video;
            video.codec = codecName;
            video.selected = true;
            video.availability = SwMediaTrack::Availability::Available;
            currentTracks.append(video);
        }
        setTracks(currentTracks);
    }

    void handleBitrateControl_(const SwByteArray& payload) {
        SwVtpBitrateControl control;
        if (!swVtpParseBitrateControlPayload(payload, control) || !control.isValid()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.negotiatedTargetBitrateKbps = control.targetBitrateKbps;
            m_metrics.negotiatedEncoderBitrateKbps = control.encoderBitrateKbps;
            m_metrics.estimatedBandwidthKbps = control.estimatedBandwidthKbps;
            m_metrics.bitrateReason = control.reason;
            m_metrics.bitrateFlags = control.flags;
        }
        publishMetrics_();
    }

    void handleKlvFragment_(const SwVtpDatagram& datagram, std::size_t datagramBytes) {
        const uint64_t receiveUs = nowUs_();
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            ++m_metrics.datagramsReceived;
            m_metrics.datagramBytesReceived += datagramBytes;
            m_rateWindowDatagramBytes += datagramBytes;
        }

        if (m_klvTrackId == 0U) {
            m_klvTrackId = datagram.header.trackId;
            m_klvReassembler.setTrackId("klv");
            addKlvTrack_();
        }

        const SwVtpDatagram reassemblyDatagram = datagramWithClientDeadline_(datagram);
        SwVtpKlvReassembler::PushResult push =
            m_klvReassembler.pushDatagram(reassemblyDatagram, receiveUs);
        const SwVtpKlvReassembler::Snapshot snapshot = m_klvReassembler.snapshot();
        if (push.completed()) {
            {
                std::lock_guard<std::mutex> lock(m_metricsMutex);
                ++m_metrics.klvPacketsCompleted;
                m_metrics.klvBytesCompleted += push.packet.payload().size();
            }
            emitMediaPacket(push.packet);
        }
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.klvAcceptedFragments = snapshot.acceptedFragments;
            m_metrics.klvDuplicateFragments = snapshot.duplicateFragments;
            m_metrics.klvStaleFragments = snapshot.staleFragments;
            m_metrics.klvDroppedPackets = snapshot.droppedSamples;
        }
        publishMetrics_();
    }

    SwVtpDatagram datagramWithClientDeadline_(const SwVtpDatagram& datagram) const {
        if (datagram.header.deadlineUs == 0U) {
            return datagram;
        }

        SwVtpDatagram normalized = datagram;
        const bool videoFragment = datagram.header.trackType == SwVtpTrackType::Video;
        const bool videoSyncFragment =
            videoFragment &&
            (datagram.header.hasFlag(SwVtpFlag_KeyFrame) ||
             datagram.header.hasFlag(SwVtpFlag_CodecConfig));
        if (videoSyncFragment) {
            normalized.header.deadlineUs = 0;
            return normalized;
        }

        uint64_t clientDeadlineUs = 0;
        if (swVtpServerTimeToClientTime(datagram.header.deadlineUs,
                                        m_clockEstimate,
                                        clientDeadlineUs)) {
            const uint64_t latencyBudgetUs =
                datagram.header.sendTimeUs == 0U
                    ? 0U
                    : swVtpPositiveDelta(datagram.header.deadlineUs,
                                         datagram.header.sendTimeUs);
            if (latencyBudgetUs != 0U &&
                m_clockEstimate.oneWayUncertaintyUs >= latencyBudgetUs) {
                normalized.header.deadlineUs = 0;
                return normalized;
            }
            if (videoFragment) {
                uint32_t targetKbps = 0;
                uint64_t averageTransferUs = 0;
                uint64_t maxTransferUs = 0;
                {
                    std::lock_guard<std::mutex> lock(m_metricsMutex);
                    targetKbps = m_metrics.negotiatedTargetBitrateKbps;
                    if (m_metrics.transferLatencySamples != 0U) {
                        averageTransferUs =
                            m_metrics.transferLatencyTotalUs /
                            m_metrics.transferLatencySamples;
                        maxTransferUs = m_metrics.transferLatencyMaxUs;
                    }
                }
                const uint64_t observedTransferUs =
                    std::max(averageTransferUs, maxTransferUs);
                if ((targetKbps != 0U && targetKbps <= kDeadlineBypassLowBitrateKbps) ||
                    (latencyBudgetUs != 0U &&
                     observedTransferUs >= latencyBudgetUs * kDeadlineBypassLatencyFactor)) {
                    normalized.header.deadlineUs = 0;
                    return normalized;
                }
            }
            uint64_t deadlineSlackUs = m_clockEstimate.oneWayUncertaintyUs;
            if (videoFragment) {
                static constexpr uint64_t kFragmentDeadlineSlackPerFragmentUs = 2000ULL;
                static constexpr uint64_t kMaxFragmentDeadlineSlackUs = 500000ULL;
                static constexpr uint64_t kMaxObservedDeadlineSlackUs = 2000000ULL;
                const uint64_t fragmentSlackUs = std::min<uint64_t>(
                    kMaxFragmentDeadlineSlackUs,
                    static_cast<uint64_t>(datagram.header.fragmentCount) *
                        kFragmentDeadlineSlackPerFragmentUs);
                deadlineSlackUs = saturatedAddUs_(deadlineSlackUs, fragmentSlackUs);

                uint64_t averageTransferUs = 0;
                uint64_t maxTransferUs = 0;
                {
                    std::lock_guard<std::mutex> lock(m_metricsMutex);
                    if (m_metrics.transferLatencySamples != 0U) {
                        averageTransferUs =
                            m_metrics.transferLatencyTotalUs /
                            m_metrics.transferLatencySamples;
                        maxTransferUs = m_metrics.transferLatencyMaxUs;
                    }
                }
                const uint64_t observedTransferUs =
                    std::max(averageTransferUs, maxTransferUs);
                if (latencyBudgetUs != 0U && observedTransferUs > latencyBudgetUs) {
                    deadlineSlackUs = saturatedAddUs_(
                        deadlineSlackUs,
                        std::min<uint64_t>(kMaxObservedDeadlineSlackUs,
                                           observedTransferUs - latencyBudgetUs));
                }
            }
            normalized.header.deadlineUs =
                clientDeadlineUs > std::numeric_limits<uint64_t>::max() - deadlineSlackUs
                    ? 0U
                    : clientDeadlineUs + deadlineSlackUs;
        } else {
            normalized.header.deadlineUs = 0;
        }
        return normalized;
    }

    void addLatencySample_(const SwVtpFrameLatencySample& latency) {
        if (!latency.valid) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        ++m_metrics.transferLatencySamples;
        m_metrics.transferLatencyTotalUs += latency.transferLatencyUs;
        m_metrics.transferLatencyMinUs =
            m_metrics.transferLatencySamples == 1U
                ? latency.transferLatencyUs
                : std::min(m_metrics.transferLatencyMinUs, latency.transferLatencyUs);
        m_metrics.transferLatencyMaxUs =
            std::max(m_metrics.transferLatencyMaxUs, latency.transferLatencyUs);
        if (latency.captureLatencyValid) {
            ++m_metrics.captureLatencySamples;
            m_metrics.captureLatencyTotalUs += latency.captureToReceiveUs;
            m_metrics.captureLatencyMinUs =
                m_metrics.captureLatencySamples == 1U
                    ? latency.captureToReceiveUs
                    : std::min(m_metrics.captureLatencyMinUs, latency.captureToReceiveUs);
            m_metrics.captureLatencyMaxUs =
                std::max(m_metrics.captureLatencyMaxUs, latency.captureToReceiveUs);
        }
    }

    SwVideoPacket normalizeCompletedVideoPacket_(const SwVideoPacket& packet,
                                                 uint64_t receiveUs) {
        SwVideoPacket normalized = packet;
        normalized.setClockRate(packet.clockRate() > 0 ? packet.clockRate() : 1000000);

        std::int64_t ptsUs = normalized.pts();
        if (ptsUs < 0 && normalized.dts() >= 0) {
            ptsUs = normalized.dts();
        }
        if (ptsUs < 0) {
            ptsUs = m_lastVideoPtsUs >= 0
                        ? m_lastVideoPtsUs + m_videoFrameIntervalUs
                        : 0;
        }

        if (m_lastVideoPtsUs >= 0) {
            const std::int64_t deltaUs = ptsUs - m_lastVideoPtsUs;
            if (deltaUs > 0) {
                if (deltaUs >= kMinTrackedFrameIntervalUs &&
                    deltaUs <= kMaxTrackedFrameIntervalUs) {
                    m_videoFrameIntervalUs = deltaUs;
                }
            } else {
                const std::int64_t inputPtsUs = ptsUs;
                ptsUs = m_lastVideoPtsUs + m_videoFrameIntervalUs;
                if (m_lastTimestampNormalizeLogUs == 0U ||
                    receiveUs < m_lastTimestampNormalizeLogUs ||
                    receiveUs - m_lastTimestampNormalizeLogUs >= kTimestampNormalizeLogCooldownUs) {
                    m_lastTimestampNormalizeLogUs = receiveUs;
                    swCWarning(kSwLogCategory_SwVtpVideoSource)
                        << "[SwVtpVideoSource] Normalizing backward video timestamp"
                        << " previousPtsUs=" << m_lastVideoPtsUs
                        << " packetPtsUs=" << inputPtsUs
                        << " syntheticPtsUs=" << ptsUs;
                }
            }
        }

        normalized.setPts(ptsUs);
        normalized.setDts(ptsUs);
        m_lastVideoPtsUs = ptsUs;
        return normalized;
    }

    void maybeSendNack_(SwVtpUdpTransport& transport,
                        uint16_t streamId,
                        uint16_t trackId,
                        uint32_t frameId,
                        uint16_t observedFragmentIndex,
                        bool requestAllMissing,
                        uint64_t nowUs) {
        if (!m_streamConfig.isValid() || frameId == 0U) {
            return;
        }
        if (m_lastNackFrameId == frameId &&
            nowUs > m_lastNackUs &&
            nowUs - m_lastNackUs < kMinNackIntervalUs) {
            return;
        }

        SwVtpNackRequest request;
        if (!m_reassembler.makeNackRequest(streamId,
                                           trackId,
                                           frameId,
                                           nowUs,
                                           kNackRetransmitBudgetUs,
                                           request)) {
            return;
        }
        if (!requestAllMissing) {
            SwVtpNackRequest limited;
            limited.streamId = request.streamId;
            limited.trackId = request.trackId;
            limited.frameId = request.frameId;
            for (auto it = request.missingFragments.begin();
                 it != request.missingFragments.end();
                 ++it) {
                if (*it <= observedFragmentIndex) {
                    limited.missingFragments.append(*it);
                }
            }
            request = limited;
        }
        if (!request.isValid()) {
            return;
        }

        if (sendControl_(transport, SwVtpMessageType::Nack, swVtpSerializeNack(request))) {
            m_lastNackUs = nowUs;
            m_lastNackFrameId = frameId;
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            ++m_metrics.nackRequestsSent;
            m_metrics.nackFragmentsRequested +=
                static_cast<uint64_t>(request.missingFragments.size());
        }
    }

    void handleFrameFragment_(SwVtpUdpTransport& transport,
                               const SwVtpDatagram& datagram,
                               std::size_t datagramBytes) {
        const uint64_t receiveUs = nowUs_();
        m_lastVideoReceiveUs = receiveUs;
        m_lastReconnectProbeUs = 0;
        m_noVideoRecoveryActive = false;
        const uint32_t previousFrameId = m_lastFrameId;
        const bool movedToNewerFrame =
            previousFrameId != 0U && datagram.header.frameId > previousFrameId;
        m_lastFrameId = datagram.header.frameId;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            ++m_metrics.datagramsReceived;
            m_metrics.datagramBytesReceived += datagramBytes;
            m_rateWindowDatagramBytes += datagramBytes;
        }

        if (m_clockEstimate.valid) {
            addLatencySample_(swVtpMeasureFrameLatency(datagram.header,
                                                       m_clockEstimate,
                                                       receiveUs));
        }

        updateVideoTrack_(datagram.header.codec);
        const SwVtpDatagram reassemblyDatagram = datagramWithClientDeadline_(datagram);
        const SwVtpFrameReassembler::Snapshot previousSnapshot = m_reassembler.snapshot();
        SwVtpFrameReassembler::PushResult push =
            m_reassembler.pushDatagram(reassemblyDatagram, receiveUs);
        const SwVtpFrameReassembler::Snapshot snapshot = m_reassembler.snapshot();
        const bool droppedVideoReference =
            snapshot.droppedFrames > previousSnapshot.droppedFrames;
        const bool staleVideoFragment =
            snapshot.staleFragments > previousSnapshot.staleFragments;
        if (droppedVideoReference || staleVideoFragment) {
            triggerVideoResync_(
                transport,
                receiveUs,
                droppedVideoReference
                    ? SwString("SwVTP video frame dropped; waiting for keyframe")
                    : SwString("SwVTP video fragment stale; waiting for keyframe"));
        }

        bool completed = false;
        std::size_t completedBytes = 0;
        if (push.completed()) {
            completed = true;
            completedBytes = push.packet.payload().size();
            SwVideoPacket outputPacket = normalizeCompletedVideoPacket_(push.packet, receiveUs);
            emitStatus(StreamState::Streaming, "SwVTP streaming");
            emitPacket(outputPacket);
        } else if (datagram.header.fragmentCount > 1U) {
            maybeSendNack_(transport,
                           datagram.header.streamId,
                           datagram.header.trackId,
                           datagram.header.frameId,
                           datagram.header.fragmentIndex,
                           false,
                           receiveUs);
        }
        if (movedToNewerFrame) {
            maybeSendNack_(transport,
                           datagram.header.streamId,
                           datagram.header.trackId,
                           previousFrameId,
                           0xFFFFU,
                           true,
                           receiveUs);
        }
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            if (completed) {
                ++m_metrics.framesCompleted;
                m_metrics.videoBytesCompleted += completedBytes;
                m_rateWindowVideoBytes += completedBytes;
            }
            m_metrics.acceptedFragments = snapshot.acceptedFragments;
            m_metrics.duplicateFragments = snapshot.duplicateFragments;
            m_metrics.staleFragments = snapshot.staleFragments;
            m_metrics.droppedFrames = snapshot.droppedFrames;
            updateLiveBitrateLocked_(receiveUs);
        }
        publishMetrics_();
    }

    void updateLiveBitrateLocked_(uint64_t nowUs) {
        if (m_rateWindowStartUs == 0U) {
            m_rateWindowStartUs = nowUs;
            return;
        }
        const uint64_t elapsedUs = nowUs > m_rateWindowStartUs
                                       ? nowUs - m_rateWindowStartUs
                                       : 0U;
        if (elapsedUs < 500000ULL) {
            return;
        }
        m_metrics.liveVideoKbps =
            static_cast<double>(m_rateWindowVideoBytes * 8ULL) /
            static_cast<double>(elapsedUs) * 1000.0;
        m_metrics.liveUdpKbps =
            static_cast<double>(m_rateWindowDatagramBytes * 8ULL) /
            static_cast<double>(elapsedUs) * 1000.0;
        m_rateWindowStartUs = nowUs;
        m_rateWindowDatagramBytes = 0;
        m_rateWindowVideoBytes = 0;
    }

    void publishMetrics_() {
        MetricsCallback cb;
        SwVtpVideoSourceMetrics metrics;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            cb = m_metricsCallback;
            metrics = m_metrics;
        }
        if (cb) {
            cb(metrics);
        }
    }

    SwMediaOpenOptions m_options{};
    SwVtpFrameReassembler m_reassembler{};
    SwVtpKlvReassembler m_klvReassembler{};
    SwVtpClockEstimate m_clockEstimate{};
    SwVtpStreamConfig m_streamConfig{};
    SwVtpClientAnnouncement m_announcement{};
    SwString m_serverHost{};
    uint32_t m_serverIpv4{0};
    uint16_t m_serverPort{0};
    SwString m_bindAddress{};
    uint32_t m_bindIpv4{0};
    uint16_t m_listenPort{0};
    SwString m_announceAddress{};
    uint16_t m_klvTrackId{0};
    uint32_t m_nextSyncId{1};
    uint64_t m_lastPingUs{0};
    uint64_t m_lastHelloUs{0};
    uint64_t m_lastStatsUs{0};
    uint64_t m_lastNackUs{0};
    uint64_t m_lastKeyFrameRequestUs{0};
    uint64_t m_lastRecoveryEmitUs{0};
    uint64_t m_lastTimestampNormalizeLogUs{0};
    uint64_t m_lastReconnectProbeUs{0};
    uint64_t m_lastAcceptUs{0};
    uint64_t m_lastVideoReceiveUs{0};
    uint32_t m_lastFrameId{0};
    uint32_t m_lastNackFrameId{0};
    std::int64_t m_lastVideoPtsUs{-1};
    std::int64_t m_videoFrameIntervalUs{33333};
    uint64_t m_rateWindowStartUs{0};
    uint64_t m_rateWindowDatagramBytes{0};
    uint64_t m_rateWindowVideoBytes{0};
    std::chrono::steady_clock::time_point m_lastPacketTime{};
    mutable std::mutex m_metricsMutex;
    SwVtpVideoSourceMetrics m_metrics{};
    MetricsCallback m_metricsCallback{};
    std::thread m_worker{};
    std::atomic<bool> m_stopRequested{false};
    bool m_noVideoRecoveryActive{false};
    static constexpr uint64_t kMinNackIntervalUs = 2000ULL;
    static constexpr uint64_t kNackRetransmitBudgetUs = 15000ULL;
    static constexpr uint32_t kDeadlineBypassLowBitrateKbps = 500U;
    static constexpr uint64_t kDeadlineBypassLatencyFactor = 4ULL;
    static constexpr uint64_t kRecoveryEmitCooldownUs = 500000ULL;
    static constexpr uint64_t kTimestampNormalizeLogCooldownUs = 1000000ULL;
    static constexpr uint64_t kNoVideoReconnectTimeoutUs = 3000000ULL;
    static constexpr uint64_t kReconnectProbeIntervalUs = 1000000ULL;
    static constexpr std::int64_t kDefaultVideoFrameIntervalUs = 33333;
    static constexpr std::int64_t kMinTrackedFrameIntervalUs = 1000;
    static constexpr std::int64_t kMaxTrackedFrameIntervalUs = 250000;
};
