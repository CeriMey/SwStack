#pragma once

/**
 * @file src/media/SwVtpVideoSource.h
 * @brief Low-latency SwVTP video source for SwMediaPlayer and SwVideoWidget.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwVideoSource.h"
#include "media/swvtp/SwVtpAv1.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SwVtpVideoSourceSocketHandle;
static const SwVtpVideoSourceSocketHandle kSwVtpVideoSourceInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SwVtpVideoSourceSocketHandle;
static const SwVtpVideoSourceSocketHandle kSwVtpVideoSourceInvalidSocket = -1;
#endif

static constexpr const char* kSwLogCategory_SwVtpVideoSource = "sw.media.swvtpvideosource";

struct SwVtpVideoSourceMetrics {
    uint64_t datagramsReceived{0};
    uint64_t datagramBytesReceived{0};
    uint64_t framesCompleted{0};
    uint64_t videoBytesCompleted{0};
    uint64_t duplicateFragments{0};
    uint64_t staleFragments{0};
    uint64_t droppedFrames{0};
    uint64_t acceptedFragments{0};
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
        video.codec = "av1";
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
        m_announceAddress = queryValue_("announce");
        if (m_announceAddress.isEmpty()) {
            m_announceAddress = queryValue_("client");
        }
        if (m_announceAddress.isEmpty()) {
            m_announceAddress = queryValue_("client-ip");
        }
        if (m_announceAddress.isEmpty()) {
            m_announceAddress = queryValue_("announce-ip");
        }
        if (m_announceAddress.isEmpty()) {
            m_announceAddress = (m_serverHost == "127.0.0.1" || m_serverHost == "localhost")
                                    ? SwString("127.0.0.1")
                                    : m_bindAddress;
        }

        if (!swVtpParseIpv4Address(m_announceAddress.toStdString().c_str(),
                                   m_announcement.clientIpv4) ||
            !swVtpIsIpv4UnicastAddress(m_announcement.clientIpv4)) {
            emitStatus(StreamState::Recovering, "Invalid SwVTP announce address");
            return;
        }

        if (!swVtpParseIpv4Address(m_serverHost.toStdString().c_str(), m_serverIpv4)) {
            emitStatus(StreamState::Recovering, "SwVTP server host must be an IPv4 address");
            return;
        }
        if (!swVtpParseIpv4Address(m_bindAddress.toStdString().c_str(), m_bindIpv4)) {
            emitStatus(StreamState::Recovering, "SwVTP bind address must be an IPv4 address");
            return;
        }

        emitStatus(StreamState::Connecting, "Opening SwVTP UDP source...");
        m_clockEstimate = SwVtpClockEstimate();
        m_streamConfig = SwVtpStreamConfig();
        m_reassembler.reset();
        m_lastPacketTime = {};
        m_lastHelloUs = 0;
        m_lastPingUs = 0;
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

    SwByteArray makeControlDatagram_(SwVtpMessageType type,
                                     const SwByteArray& payload) const {
        SwVtpDatagram datagram;
        datagram.header.version = kSwVtpVersion1;
        datagram.header.messageType = type;
        datagram.header.trackType = SwVtpTrackType::Control;
        datagram.header.codec = SwVtpCodec::Unknown;
        datagram.header.sendTimeUs = nowUs_();
        datagram.payload = payload;
        return swVtpSerializeDatagram(datagram);
    }

    static void closeNativeSocket_(SwVtpVideoSourceSocketHandle socketHandle) {
        if (socketHandle == kSwVtpVideoSourceInvalidSocket) {
            return;
        }
#if defined(_WIN32)
        ::closesocket(socketHandle);
#else
        ::close(socketHandle);
#endif
    }

    static sockaddr_in makeSockaddr_(uint32_t ipv4, uint16_t port) {
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(ipv4);
        return addr;
    }

    static uint16_t sockaddrPort_(const sockaddr_in& addr) {
        return ntohs(addr.sin_port);
    }

    static bool bindNativeSocket_(SwVtpVideoSourceSocketHandle socketHandle,
                                  uint32_t ipv4,
                                  uint16_t port) {
        const int yes = 1;
        ::setsockopt(socketHandle,
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     reinterpret_cast<const char*>(&yes),
                     sizeof(yes));
        sockaddr_in addr = makeSockaddr_(ipv4, port);
        return ::bind(socketHandle,
                      reinterpret_cast<const sockaddr*>(&addr),
                      sizeof(addr)) == 0;
    }

    static bool localNativePort_(SwVtpVideoSourceSocketHandle socketHandle,
                                 uint16_t& outPort) {
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
#if defined(_WIN32)
        int length = sizeof(addr);
#else
        socklen_t length = sizeof(addr);
#endif
        if (::getsockname(socketHandle, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
            return false;
        }
        outPort = sockaddrPort_(addr);
        return true;
    }

    bool sendNative_(SwVtpVideoSourceSocketHandle socketHandle,
                     const SwByteArray& bytes,
                     uint32_t ipv4,
                     uint16_t port) {
        if (bytes.isEmpty() || !bytes.constData()) {
            return false;
        }
        sockaddr_in dest = makeSockaddr_(ipv4, port);
#if defined(_WIN32)
        const int sent = ::sendto(socketHandle,
                                  bytes.constData(),
                                  static_cast<int>(bytes.size()),
                                  0,
                                  reinterpret_cast<const sockaddr*>(&dest),
                                  sizeof(dest));
        return sent == static_cast<int>(bytes.size());
#else
        const ssize_t sent = ::sendto(socketHandle,
                                      bytes.constData(),
                                      bytes.size(),
                                      0,
                                      reinterpret_cast<const sockaddr*>(&dest),
                                      sizeof(dest));
        return sent == static_cast<ssize_t>(bytes.size());
#endif
    }

    bool sendControl_(SwVtpVideoSourceSocketHandle socketHandle,
                      SwVtpMessageType type,
                      const SwByteArray& payload) {
        const SwByteArray bytes = makeControlDatagram_(type, payload);
        return sendNative_(socketHandle, bytes, m_serverIpv4, m_serverPort);
    }

    void sendClockPing_(SwVtpVideoSourceSocketHandle socketHandle) {
        SwVtpClockSyncPing ping;
        ping.syncId = m_nextSyncId++;
        ping.clientSendTimeUs = nowUs_();
        m_lastPingUs = ping.clientSendTimeUs;
        sendControl_(socketHandle, SwVtpMessageType::Ping, swVtpSerializeClockSyncPing(ping));
    }

    void sendAnnouncement_(SwVtpVideoSourceSocketHandle socketHandle) {
        if (m_announcement.receivePort == 0U) {
            localNativePort_(socketHandle, m_announcement.receivePort);
        }
        m_lastHelloUs = nowUs_();
        sendControl_(socketHandle,
                     SwVtpMessageType::Hello,
                     swVtpSerializeClientAnnouncement(m_announcement));
    }

    void maintainSession_(SwVtpVideoSourceSocketHandle socketHandle) {
        if (!isRunning()) {
            return;
        }
        const uint64_t now = nowUs_();
        if (!m_clockEstimate.valid && (m_lastPingUs == 0U || now - m_lastPingUs > 500000ULL)) {
            sendClockPing_(socketHandle);
        }
        if (!m_streamConfig.isValid() &&
            (m_lastHelloUs == 0U || now - m_lastHelloUs > 500000ULL)) {
            sendAnnouncement_(socketHandle);
        }
        if (m_lastPacketTime.time_since_epoch().count() != 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - m_lastPacketTime).count();
            if (elapsed > 3) {
                emitStatus(StreamState::Recovering,
                           SwString("No SwVTP data received for ") +
                               SwString::number(static_cast<int>(elapsed)) + SwString(" s"));
            }
        }
    }

    void workerMain_() {
#if defined(_WIN32)
        WSADATA wsaData;
        const bool wsaOk = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        if (!wsaOk) {
            emitStatus(StreamState::Recovering, "WSAStartup failed");
            return;
        }
#endif
        SwVtpVideoSourceSocketHandle socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == kSwVtpVideoSourceInvalidSocket ||
            !bindNativeSocket_(socketHandle, m_bindIpv4, m_listenPort)) {
            emitStatus(StreamState::Recovering, "Failed to bind native SwVTP UDP socket");
            closeNativeSocket_(socketHandle);
#if defined(_WIN32)
            ::WSACleanup();
#endif
            return;
        }
        localNativePort_(socketHandle, m_announcement.receivePort);
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.localPort = m_announcement.receivePort;
        }
        publishMetrics_();

        sendClockPing_(socketHandle);
        sendAnnouncement_(socketHandle);

        while (!m_stopRequested.load()) {
            maintainSession_(socketHandle);
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socketHandle, &readSet);
            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;
            const int ready = ::select(static_cast<int>(socketHandle + 1),
                                       &readSet,
                                       nullptr,
                                       nullptr,
                                       &timeout);
            if (ready <= 0 || !FD_ISSET(socketHandle, &readSet)) {
                continue;
            }

            std::vector<char> buffer(65536);
            sockaddr_in sender;
            std::memset(&sender, 0, sizeof(sender));
#if defined(_WIN32)
            int senderLength = sizeof(sender);
            const int received = ::recvfrom(socketHandle,
                                            buffer.data(),
                                            static_cast<int>(buffer.size()),
                                            0,
                                            reinterpret_cast<sockaddr*>(&sender),
                                            &senderLength);
#else
            socklen_t senderLength = sizeof(sender);
            const ssize_t received = ::recvfrom(socketHandle,
                                                buffer.data(),
                                                buffer.size(),
                                                0,
                                                reinterpret_cast<sockaddr*>(&sender),
                                                &senderLength);
#endif
            if (received <= 0) {
                continue;
            }
            SwByteArray bytes;
            bytes.append(buffer.data(), static_cast<std::size_t>(received));
            m_lastPacketTime = std::chrono::steady_clock::now();

            SwVtpDatagram datagram;
            if (!swVtpParseDatagram(bytes, datagram)) {
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::Pong) {
                handlePong_(datagram.payload);
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::Accept) {
                handleAccept_(datagram.payload);
                continue;
            }
            if (datagram.header.messageType == SwVtpMessageType::FrameFragment) {
                handleFrameFragment_(datagram, bytes.size());
                continue;
            }
        }
        closeNativeSocket_(socketHandle);
#if defined(_WIN32)
        ::WSACleanup();
#endif
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

    void handleAccept_(const SwByteArray& payload) {
        SwVtpStreamConfig config;
        if (!swVtpParseStreamConfigPayload(payload, config)) {
            return;
        }
        m_streamConfig = config;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_metrics.accepted = true;
        }
        emitStatus(StreamState::Streaming, "SwVTP accepted");
        publishMetrics_();
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

    void handleFrameFragment_(const SwVtpDatagram& datagram, std::size_t datagramBytes) {
        const uint64_t receiveUs = nowUs_();
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

        SwVtpAv1Reassembler::PushResult push =
            m_reassembler.pushDatagram(datagram, receiveUs);
        const SwVtpAv1Reassembler::Snapshot snapshot = m_reassembler.snapshot();
        bool completed = false;
        std::size_t completedBytes = 0;
        if (push.completed()) {
            completed = true;
            completedBytes = push.packet.payload().size();
            emitStatus(StreamState::Streaming, "SwVTP streaming");
            emitPacket(push.packet);
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
    SwVtpAv1Reassembler m_reassembler{};
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
    uint32_t m_nextSyncId{1};
    uint64_t m_lastPingUs{0};
    uint64_t m_lastHelloUs{0};
    uint64_t m_rateWindowStartUs{0};
    uint64_t m_rateWindowDatagramBytes{0};
    uint64_t m_rateWindowVideoBytes{0};
    std::chrono::steady_clock::time_point m_lastPacketTime{};
    mutable std::mutex m_metricsMutex;
    SwVtpVideoSourceMetrics m_metrics{};
    MetricsCallback m_metricsCallback{};
    std::thread m_worker{};
    std::atomic<bool> m_stopRequested{false};
};
