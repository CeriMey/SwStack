#pragma once

/**
 * @file src/media/SwRtspTrackGraph.h
 * @ingroup media
 * @brief Declares the internal live RTSP track graph used by SwRtspUdpSource.
 */

#include "SwDebug.h"
#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"
#include "media/SwVideoPacket.h"
#include "media/SwVideoSource.h"
#include "media/rtp/SwRtpDepacketizerH264.h"
#include "media/rtp/SwRtpDepacketizerH265.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwTsProgramDemux.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

static constexpr const char* kSwLogCategory_SwRtspTrackGraph = "sw.media.swrtsptrackgraph";

class SwRtspTrackGraph {
public:
    struct VideoConfig {
        SwVideoPacket::Codec codec{SwVideoPacket::Codec::H264};
        int payloadType{-1};
        int clockRate{90000};
        bool liveTrimEnabled{false};
        int latencyTargetMs{500};
        bool transportStream{false};
        SwString fmtp{};
    };

    using VideoPacketCallback = std::function<void(const SwVideoPacket&)>;
    using MediaPacketCallback = std::function<void(const SwMediaPacket&)>;
    using TracksChangedCallback = std::function<void(const SwList<SwMediaTrack>&)>;
    using KeyFrameRequestCallback = std::function<void(const SwString&)>;
    using RecoveryCallback =
        std::function<void(SwMediaSource::RecoveryEvent::Kind, const SwString&)>;

    SwRtspTrackGraph() {
        m_h264Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitRecoveredPacket_(packet);
        });
        m_h265Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitRecoveredPacket_(packet);
        });
        m_tsProgramDemux.setPacketCallback([this](const SwMediaPacket& packet) {
            handleProgramPacket_(packet);
        });
        m_tsProgramDemux.setTracksChangedCallback([this](const SwList<SwMediaTrack>& tracks) {
            TracksChangedCallback callback;
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                callback = m_tracksChangedCallback;
            }
            if (callback) {
                callback(tracks);
            }
        });
    }

    ~SwRtspTrackGraph() {
        stop();
    }

    void setVideoConfig(const VideoConfig& config) {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_videoConfig = config;
        if (config.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.setFmtp(config.fmtp);
        } else {
            m_h264Depacketizer.setFmtp(config.fmtp);
        }
    }

    void setVideoPacketCallback(VideoPacketCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_videoPacketCallback = std::move(callback);
    }

    void setMediaPacketCallback(MediaPacketCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_mediaPacketCallback = std::move(callback);
    }

    void setTracksChangedCallback(TracksChangedCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_tracksChangedCallback = std::move(callback);
    }

    void setKeyFrameRequestCallback(KeyFrameRequestCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_keyFrameRequestCallback = std::move(callback);
    }

    void setRecoveryCallback(RecoveryCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_recoveryCallback = std::move(callback);
    }

    void start() {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true)) {
            return;
        }
        m_stopRequested.store(false);
        m_forceVideoDiscontinuity.store(true);
        m_suppressRecoveredVideoOutput.store(false);
        m_loggedSuppressedRecoveredPacket.store(false);
        m_forceAudioDiscontinuity.store(true);
        m_forceMetadataDiscontinuity.store(true);
        resetVideoState_();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            resetQueueStateLocked_();
        }
        m_videoWorker = std::thread([this]() { videoWorkerLoop_(); });
        m_audioWorker = std::thread([this]() { mediaWorkerLoop_(m_audioQueueState,
                                                                m_audioCv,
                                                                m_forceAudioDiscontinuity); });
        m_metadataWorker = std::thread([this]() { mediaWorkerLoop_(m_metadataQueueState,
                                                                   m_metadataCv,
                                                                   m_forceMetadataDiscontinuity); });
    }

    void stop() {
        if (!m_running.exchange(false)) {
            return;
        }
        m_stopRequested.store(true);
        notifyAllQueues_();
        if (m_videoWorker.joinable()) {
            m_videoWorker.join();
        }
        if (m_audioWorker.joinable()) {
            m_audioWorker.join();
        }
        if (m_metadataWorker.joinable()) {
            m_metadataWorker.join();
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            resetQueueStateLocked_();
        }
        resetVideoState_();
        m_suppressRecoveredVideoOutput.store(false);
        m_loggedSuppressedRecoveredPacket.store(false);
    }

    void reset() {
        if (!m_running.load()) {
            resetVideoState_();
            return;
        }
        QueuedVideoEvent item;
        item.event.kind = VideoEvent::Kind::Reset;
        item.enqueueGeneration = nextEnqueueGeneration_();
        item.queuedBytes = 64U;
        item.enqueueTime = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            pushVideoLocked_(item);
        }
        m_videoCv.notify_one();
    }

    void setConsumerPressure(const SwVideoSource::ConsumerPressure& pressure) {
        SwString requestReason;
        bool requestKeyFrame = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_consumerPressure = pressure;
            const VideoConfig config = currentVideoConfig_();
            const bool pressureActivated =
                updateUnifiedPressureStateLocked_(config, 0U, 0U, 0U, false);
            if (usesUnifiedLiveTrim_(config) &&
                !m_waitingForAlignedKeyFrame &&
                (pressure.hardPressure || pressure.decoderStalled)) {
                enterConsumerRecoveryLocked_(requestReason);
                requestKeyFrame = true;
            } else if (usesUnifiedLiveTrim_(config) &&
                       pressureActivated &&
                       !m_waitingForAlignedKeyFrame) {
                requestReason = "video consumer pressure";
            }
        }
        if (!requestReason.isEmpty()) {
            requestKeyFrame_(requestReason);
        } else if (requestKeyFrame) {
            requestKeyFrame_("video consumer hard pressure");
        }
    }

    void submitVideoPacket(const SwRtpSession::Packet& packet,
                           bool detectGapFromSequence = false) {
        if (!m_running.load()) {
            return;
        }

        const VideoConfig config = currentVideoConfig_();
        QueuedVideoEvent item;
        item.event.kind = VideoEvent::Kind::Packet;
        item.event.packet = packet;
        item.event.detectGapFromSequence = detectGapFromSequence;
        item.enqueueGeneration = nextEnqueueGeneration_();
        item.queuedBytes = packetQueuedBytes_(packet);
        item.enqueueTime = std::chrono::steady_clock::now();
        item.accessUnitTimestamp = packet.timestamp;
        item.keyFrameCandidate = packetIndicatesKeyFrame_(config, packet);

        bool requestKeyFrame = false;
        SwString requestReason;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (usesUnifiedLiveTrim_(config)) {
                queueVideoUnifiedLocked_(config, item, requestKeyFrame, requestReason);
            } else {
                queueVideoBoundedLocked_(item, requestKeyFrame, requestReason);
            }
        }

        if (!requestReason.isEmpty()) {
            requestKeyFrame_(requestReason);
        } else if (requestKeyFrame) {
            requestKeyFrame_("video queue overflow");
        }
        m_videoCv.notify_one();
    }

    void submitVideoGap(uint16_t expected, uint16_t actual) {
        if (!m_running.load()) {
            return;
        }
        QueuedVideoEvent item;
        item.event.kind = VideoEvent::Kind::Gap;
        item.event.expectedSequence = expected;
        item.event.actualSequence = actual;
        item.enqueueGeneration = nextEnqueueGeneration_();
        item.queuedBytes = 32U;
        item.enqueueTime = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            pushVideoLocked_(item);
        }
        m_videoCv.notify_one();
    }

    void submitAudioPacket(const SwMediaPacket& packet) {
        if (!m_running.load()) {
            return;
        }
        QueuedMediaPacket item;
        item.packet = packet;
        item.enqueueGeneration = nextEnqueueGeneration_();
        item.queuedBytes = mediaPacketQueuedBytes_(packet);
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_waitingForAlignedKeyFrame) {
                ++m_audioDroppedForAlignment;
                m_forceAudioDiscontinuity.store(true);
                return;
            }
            pushMediaLocked_(m_audioQueueState, item);
            if (trimMediaToSoftLimitsLocked_(m_audioQueueState, m_audioQueueLimits)) {
                m_forceAudioDiscontinuity.store(true);
            }
        }
        m_audioCv.notify_one();
    }

    void submitMetadataPacket(const SwMediaPacket& packet) {
        if (!m_running.load()) {
            return;
        }
        QueuedMediaPacket item;
        item.packet = packet;
        item.enqueueGeneration = nextEnqueueGeneration_();
        item.queuedBytes = mediaPacketQueuedBytes_(packet);
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_waitingForAlignedKeyFrame) {
                ++m_metadataDroppedForAlignment;
                m_forceMetadataDiscontinuity.store(true);
                return;
            }
            pushMediaLocked_(m_metadataQueueState, item);
            if (trimMediaToSoftLimitsLocked_(m_metadataQueueState, m_metadataQueueLimits)) {
                m_forceMetadataDiscontinuity.store(true);
            }
        }
        m_metadataCv.notify_one();
    }

private:
    struct VideoEvent {
        enum class Kind {
            Packet,
            Gap,
            Reset
        };

        Kind kind{Kind::Packet};
        SwRtpSession::Packet packet{};
        uint16_t expectedSequence{0};
        uint16_t actualSequence{0};
        bool detectGapFromSequence{false};
    };

    struct QueuedVideoEvent {
        VideoEvent event{};
        uint64_t enqueueGeneration{0};
        std::size_t queuedBytes{0};
        std::chrono::steady_clock::time_point enqueueTime{};
        uint32_t accessUnitTimestamp{0};
        bool keyFrameCandidate{false};
        bool cutBeforeDispatch{false};
    };

    struct QueuedMediaPacket {
        SwMediaPacket packet{};
        uint64_t enqueueGeneration{0};
        std::size_t queuedBytes{0};
    };

    struct QueueLimits {
        QueueLimits(std::size_t inSoftItems = 0,
                    std::size_t inSoftBytes = 0,
                    std::size_t inHardItems = 0,
                    std::size_t inHardBytes = 0)
            : softItems(inSoftItems),
              softBytes(inSoftBytes),
              hardItems(inHardItems),
              hardBytes(inHardBytes) {}

        std::size_t softItems;
        std::size_t softBytes;
        std::size_t hardItems;
        std::size_t hardBytes;
    };

    struct VideoQueueState {
        std::deque<QueuedVideoEvent> items{};
        std::size_t queuedBytes{0};
        uint64_t droppedItems{0};
        uint64_t droppedBytes{0};
    };

    struct MediaQueueState {
        std::deque<QueuedMediaPacket> items{};
        std::size_t queuedBytes{0};
        uint64_t droppedItems{0};
        uint64_t droppedBytes{0};
    };

    static std::size_t packetQueuedBytes_(const SwRtpSession::Packet& packet) {
        return static_cast<std::size_t>(packet.payload.size()) + 96U;
    }

    static std::size_t mediaPacketQueuedBytes_(const SwMediaPacket& packet) {
        return static_cast<std::size_t>(packet.payload().size()) + 64U;
    }

    static bool isH264KeyNalType_(uint8_t nalType) {
        return nalType == 5;
    }

    static bool isH265KeyNalType_(uint8_t nalType) {
        return nalType >= 16U && nalType <= 21U;
    }

    static bool h264PayloadIndicatesKeyFrame_(const SwByteArray& payload) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
        const std::size_t size = static_cast<std::size_t>(payload.size());
        if (!data || size == 0) {
            return false;
        }
        const uint8_t nalType = static_cast<uint8_t>(data[0] & 0x1FU);
        if (nalType >= 1U && nalType <= 23U) {
            return isH264KeyNalType_(nalType);
        }
        if ((nalType == 24U || nalType == 25U) && size > 3U) {
            std::size_t offset = (nalType == 25U) ? 3U : 1U;
            while ((offset + 2U) <= size) {
                const std::size_t nalSize =
                    static_cast<std::size_t>((static_cast<uint16_t>(data[offset]) << 8) |
                                             static_cast<uint16_t>(data[offset + 1]));
                offset += 2U;
                if (nalSize == 0U || (offset + nalSize) > size) {
                    break;
                }
                if (isH264KeyNalType_(static_cast<uint8_t>(data[offset] & 0x1FU))) {
                    return true;
                }
                offset += nalSize;
            }
        }
        if ((nalType == 28U || nalType == 29U) && size > 1U) {
            const uint8_t fuNalType = static_cast<uint8_t>(data[1] & 0x1FU);
            return isH264KeyNalType_(fuNalType);
        }
        return false;
    }

    static bool h265PayloadIndicatesKeyFrame_(const SwByteArray& payload) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
        const std::size_t size = static_cast<std::size_t>(payload.size());
        if (!data || size < 2U) {
            return false;
        }
        const uint8_t nalType = static_cast<uint8_t>((data[0] >> 1) & 0x3FU);
        if (nalType <= 47U) {
            return isH265KeyNalType_(nalType);
        }
        if (nalType == 48U && size > 4U) {
            std::size_t offset = 2U;
            while ((offset + 2U) <= size) {
                const std::size_t nalSize =
                    static_cast<std::size_t>((static_cast<uint16_t>(data[offset]) << 8) |
                                             static_cast<uint16_t>(data[offset + 1]));
                offset += 2U;
                if (nalSize == 0U || (offset + nalSize) > size) {
                    break;
                }
                const uint8_t innerType = static_cast<uint8_t>((data[offset] >> 1) & 0x3FU);
                if (isH265KeyNalType_(innerType)) {
                    return true;
                }
                offset += nalSize;
            }
        }
        if (nalType == 49U && size > 2U) {
            const uint8_t fuNalType = static_cast<uint8_t>(data[2] & 0x3FU);
            return isH265KeyNalType_(fuNalType);
        }
        return false;
    }

    static bool packetIndicatesKeyFrame_(const VideoConfig& config, const SwRtpSession::Packet& packet) {
        if (config.transportStream) {
            return false;
        }
        if (config.codec == SwVideoPacket::Codec::H265) {
            return h265PayloadIndicatesKeyFrame_(packet.payload);
        }
        if (config.codec == SwVideoPacket::Codec::H264) {
            return h264PayloadIndicatesKeyFrame_(packet.payload);
        }
        return false;
    }

    static bool forceKeyFrameRecoveryForGap_(const VideoConfig& config,
                                             int lostPackets) {
        if (config.transportStream) {
            return true;
        }
        if (config.codec == SwVideoPacket::Codec::H264 ||
            config.codec == SwVideoPacket::Codec::H265) {
            return lostPackets > 2;
        }
        return lostPackets > 4;
    }

    static bool usesUnifiedLiveTrim_(const VideoConfig& config) {
        return config.liveTrimEnabled &&
               !config.transportStream &&
               (config.codec == SwVideoPacket::Codec::H264 ||
                config.codec == SwVideoPacket::Codec::H265);
    }

    static std::size_t queueLowWatermark_(std::size_t softLimit) {
        if (softLimit == 0U) {
            return 0U;
        }
        return std::max<std::size_t>(1U, softLimit / 2U);
    }

    static int latencyLowWatermarkMs_(const VideoConfig& config) {
        const int targetMs = std::max(0, config.latencyTargetMs);
        if (targetMs <= 0) {
            return 0;
        }
        return std::max(25, targetMs / 2);
    }

    static int latencyHardLimitMs_(const VideoConfig& config) {
        const int targetMs = std::max(0, config.latencyTargetMs);
        if (targetMs <= 0) {
            return 250;
        }
        return std::max(250, targetMs * 3);
    }

    static int pressureRearmCooldownMs_(const VideoConfig& config) {
        const int latencyMs = std::max(0, config.latencyTargetMs);
        return std::max(1000, latencyMs * 4);
    }

    bool pressureCooldownActiveLocked_() const {
        return m_pressureRearmDeadline.time_since_epoch().count() != 0 &&
               std::chrono::steady_clock::now() < m_pressureRearmDeadline;
    }

    void armPressureCooldownLocked_(const VideoConfig& config) {
        m_pressureRearmDeadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(pressureRearmCooldownMs_(config));
    }

    bool queuedVideoLatencyMsLocked_(const VideoConfig& config,
                                     uint32_t extraTimestamp,
                                     bool includeExtraPacket,
                                     int& outLatencyMs) const {
        outLatencyMs = 0;
        if (config.clockRate <= 0) {
            return false;
        }

        bool havePacket = false;
        uint32_t oldestTimestamp = 0;
        uint32_t newestTimestamp = 0;
        for (std::deque<QueuedVideoEvent>::const_iterator it = m_videoQueueState.items.begin();
             it != m_videoQueueState.items.end();
             ++it) {
            if (it->event.kind != VideoEvent::Kind::Packet) {
                continue;
            }
            if (!havePacket) {
                oldestTimestamp = it->accessUnitTimestamp;
                havePacket = true;
            }
            newestTimestamp = it->accessUnitTimestamp;
        }

        if (includeExtraPacket) {
            if (!havePacket) {
                oldestTimestamp = extraTimestamp;
                havePacket = true;
            }
            newestTimestamp = extraTimestamp;
        }

        if (!havePacket) {
            return false;
        }

        const uint32_t spanTicks = newestTimestamp - oldestTimestamp;
        outLatencyMs = static_cast<int>(
            (static_cast<uint64_t>(spanTicks) * 1000ULL) /
            static_cast<uint64_t>(config.clockRate));
        return true;
    }

    bool queuedVideoWallAgeMsLocked_(int& outWallAgeMs) const {
        outWallAgeMs = 0;

        bool havePacket = false;
        std::chrono::steady_clock::time_point oldestEnqueueTime{};
        for (std::deque<QueuedVideoEvent>::const_iterator it = m_videoQueueState.items.begin();
             it != m_videoQueueState.items.end();
             ++it) {
            if (it->event.kind != VideoEvent::Kind::Packet) {
                continue;
            }
            if (!havePacket || it->enqueueTime < oldestEnqueueTime) {
                oldestEnqueueTime = it->enqueueTime;
                havePacket = true;
            }
        }

        if (!havePacket || oldestEnqueueTime.time_since_epoch().count() == 0) {
            return false;
        }

        outWallAgeMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - oldestEnqueueTime)
                .count());
        return true;
    }

    bool unifiedIngressHighLocked_(const VideoConfig& config,
                                   std::size_t extraBytes,
                                   uint32_t extraTimestamp,
                                   bool includeExtraPacket) const {
        const bool bytesHigh =
            (m_videoQueueLimits.softBytes > 0U) &&
            ((m_videoQueueState.queuedBytes + extraBytes) > m_videoQueueLimits.softBytes);
        int latencyMs = 0;
        const bool haveLatency =
            queuedVideoLatencyMsLocked_(config, extraTimestamp, includeExtraPacket, latencyMs);
        const bool latencyHigh =
            haveLatency &&
            (config.latencyTargetMs > 0) &&
            (latencyMs > config.latencyTargetMs);
        return bytesHigh || latencyHigh;
    }

    bool unifiedIngressLowLocked_(const VideoConfig& config) const {
        const bool bytesLow =
            (m_videoQueueLimits.softBytes == 0U) ||
            (m_videoQueueState.queuedBytes <= queueLowWatermark_(m_videoQueueLimits.softBytes));
        int latencyMs = 0;
        const bool haveLatency = queuedVideoLatencyMsLocked_(config, 0U, false, latencyMs);
        const bool latencyLow =
            !haveLatency ||
            (config.latencyTargetMs <= 0) ||
            (latencyMs <= latencyLowWatermarkMs_(config));
        return bytesLow && latencyLow;
    }

    bool unifiedHardOverflowLocked_(const VideoConfig& config) const {
        const bool bytesHigh =
            (m_videoQueueLimits.hardBytes > 0U) &&
            (m_videoQueueState.queuedBytes > m_videoQueueLimits.hardBytes);
        int latencyMs = 0;
        const bool haveLatency = queuedVideoLatencyMsLocked_(config, 0U, false, latencyMs);
        const bool latencyHigh =
            haveLatency &&
            (latencyMs > latencyHardLimitMs_(config));
        return bytesHigh || latencyHigh;
    }

    void logUnifiedPressureActivatedLocked_(bool ingressHigh,
                                            bool consumerActive,
                                            int latencyMs) const {
        int wallAgeMs = 0;
        const bool haveWallAge = queuedVideoWallAgeMsLocked_(wallAgeMs);
        swCWarning(kSwLogCategory_SwRtspTrackGraph)
            << "[SwRtspTrackGraph] Unified live pressure activated"
            << " ingressHigh=" << (ingressHigh ? 1 : 0)
            << " consumerActive=" << (consumerActive ? 1 : 0)
            << " consumerSoft=" << (m_consumerPressure.softPressure ? 1 : 0)
            << " consumerHard=" << (m_consumerPressure.hardPressure ? 1 : 0)
            << " decoderStalled=" << (m_consumerPressure.decoderStalled ? 1 : 0)
            << " consumerQueuedPackets=" << m_consumerPressure.queuedPackets
            << " consumerQueuedBytes=" << m_consumerPressure.queuedBytes
            << " videoQueueItems=" << m_videoQueueState.items.size()
            << " videoQueueBytes=" << m_videoQueueState.queuedBytes
            << " videoLatencyMs=" << latencyMs
            << " videoWallAgeMs=" << (haveWallAge ? wallAgeMs : -1)
            << " videoLatencyDeltaMs=" << (haveWallAge ? (latencyMs - wallAgeMs) : 0);
    }

    void resetQueueStateLocked_() {
        m_videoQueueState = VideoQueueState();
        m_audioQueueState = MediaQueueState();
        m_metadataQueueState = MediaQueueState();
        m_consumerPressure = SwVideoSource::ConsumerPressure();
        m_unifiedPressureActive = false;
        m_waitingForAlignedKeyFrame = false;
        m_loggedWaitingForAlignedKeyFrame = false;
        m_videoDroppedBeforeKeyframe = 0;
        m_audioDroppedForAlignment = 0;
        m_metadataDroppedForAlignment = 0;
        m_pressureRearmDeadline = std::chrono::steady_clock::time_point();
        m_nextEnqueueGeneration.store(1);
        m_videoWorkerStats = VideoWorkerStats();
    }

    uint64_t nextEnqueueGeneration_() {
        return m_nextEnqueueGeneration.fetch_add(1);
    }

    void notifyAllQueues_() {
        m_videoCv.notify_all();
        m_audioCv.notify_all();
        m_metadataCv.notify_all();
    }

    void pushVideoLocked_(const QueuedVideoEvent& item) {
        m_videoQueueState.items.push_back(item);
        m_videoQueueState.queuedBytes += item.queuedBytes;
    }

    void pushMediaLocked_(MediaQueueState& state, const QueuedMediaPacket& item) {
        state.items.push_back(item);
        state.queuedBytes += item.queuedBytes;
    }

    static void popFrontVideoLocked_(VideoQueueState& state, QueuedVideoEvent& out) {
        out = std::move(state.items.front());
        state.items.pop_front();
        if (state.queuedBytes >= out.queuedBytes) {
            state.queuedBytes -= out.queuedBytes;
        } else {
            state.queuedBytes = 0;
        }
    }

    static void popFrontMediaLocked_(MediaQueueState& state, QueuedMediaPacket& out) {
        out = std::move(state.items.front());
        state.items.pop_front();
        if (state.queuedBytes >= out.queuedBytes) {
            state.queuedBytes -= out.queuedBytes;
        } else {
            state.queuedBytes = 0;
        }
    }

    void dropFrontVideoLocked_() {
        if (m_videoQueueState.items.empty()) {
            return;
        }
        const QueuedVideoEvent& front = m_videoQueueState.items.front();
        if (m_videoQueueState.queuedBytes >= front.queuedBytes) {
            m_videoQueueState.queuedBytes -= front.queuedBytes;
        } else {
            m_videoQueueState.queuedBytes = 0;
        }
        ++m_videoQueueState.droppedItems;
        m_videoQueueState.droppedBytes += front.queuedBytes;
        m_videoQueueState.items.pop_front();
    }

    static void dropFrontMediaLocked_(MediaQueueState& state) {
        if (state.items.empty()) {
            return;
        }
        const QueuedMediaPacket& front = state.items.front();
        if (state.queuedBytes >= front.queuedBytes) {
            state.queuedBytes -= front.queuedBytes;
        } else {
            state.queuedBytes = 0;
        }
        ++state.droppedItems;
        state.droppedBytes += front.queuedBytes;
        state.items.pop_front();
    }

    static bool queueExceedsLimits_(std::size_t queuedItems,
                                    std::size_t queuedBytes,
                                    const QueueLimits& limits,
                                    bool hard) {
        const std::size_t itemLimit = hard ? limits.hardItems : limits.softItems;
        const std::size_t byteLimit = hard ? limits.hardBytes : limits.softBytes;
        const bool tooManyItems = (itemLimit > 0U) && (queuedItems > itemLimit);
        const bool tooManyBytes = (byteLimit > 0U) && (queuedBytes > byteLimit);
        return tooManyItems || tooManyBytes;
    }

    bool trimMediaToSoftLimitsLocked_(MediaQueueState& state, const QueueLimits& limits) {
        bool droppedAny = false;
        while (queueExceedsLimits_(state.items.size(), state.queuedBytes, limits, false)) {
            dropFrontMediaLocked_(state);
            droppedAny = true;
        }
        return droppedAny;
    }

    bool purgeMediaBeforeGenerationLocked_(MediaQueueState& state,
                                           uint64_t generationCut,
                                           uint64_t& droppedCount) {
        bool droppedAny = false;
        while (!state.items.empty() &&
               state.items.front().enqueueGeneration < generationCut) {
            dropFrontMediaLocked_(state);
            ++droppedCount;
            droppedAny = true;
        }
        return droppedAny;
    }

    bool purgeVideoBeforeGenerationLocked_(uint64_t generationCut,
                                           uint64_t& droppedCount) {
        bool droppedAny = false;
        while (!m_videoQueueState.items.empty() &&
               m_videoQueueState.items.front().enqueueGeneration < generationCut) {
            dropFrontVideoLocked_();
            ++droppedCount;
            droppedAny = true;
        }
        return droppedAny;
    }

    bool hasQueuedVideoPacketOlderThanGenerationLocked_(uint64_t generationCut) const {
        for (std::deque<QueuedVideoEvent>::const_iterator it = m_videoQueueState.items.begin();
             it != m_videoQueueState.items.end();
             ++it) {
            if (it->event.kind != VideoEvent::Kind::Packet) {
                continue;
            }
            if (it->enqueueGeneration < generationCut) {
                return true;
            }
        }
        return false;
    }

    bool hasAnyQueuedVideoPacketLocked_() const {
        for (std::deque<QueuedVideoEvent>::const_iterator it = m_videoQueueState.items.begin();
             it != m_videoQueueState.items.end();
             ++it) {
            if (it->event.kind == VideoEvent::Kind::Packet) {
                return true;
            }
        }
        return false;
    }

    bool findQueuedVideoByTimestampLocked_(uint32_t timestamp,
                                           uint64_t& earliestGeneration,
                                           std::deque<QueuedVideoEvent>::iterator& firstMatch) {
        for (std::deque<QueuedVideoEvent>::iterator it = m_videoQueueState.items.begin();
             it != m_videoQueueState.items.end();
             ++it) {
            if (it->event.kind != VideoEvent::Kind::Packet) {
                continue;
            }
            if (it->accessUnitTimestamp == timestamp) {
                earliestGeneration = it->enqueueGeneration;
                firstMatch = it;
                return true;
            }
        }
        return false;
    }

    void logLiveJump_(uint64_t alignmentGeneration,
                      uint64_t videoDropped,
                      uint64_t audioDropped,
                      uint64_t metadataDropped,
                      uint32_t timestamp) const {
        swCWarning(kSwLogCategory_SwRtspTrackGraph)
            << "[SwRtspTrackGraph] Live GOP jump to keyframe"
            << " ts=" << timestamp
            << " alignmentGeneration=" << alignmentGeneration
            << " videoDroppedBeforeKeyframe=" << videoDropped
            << " audioDroppedForAlignment=" << audioDropped
            << " metadataDroppedForAlignment=" << metadataDropped;
    }

    void activateAlignmentCutLocked_(const VideoConfig& config,
                                     uint64_t alignmentGeneration,
                                     uint32_t timestamp,
                                     QueuedVideoEvent* pendingItem,
                                     bool markQueuedCandidate,
                                     bool emitRecoveryEvent) {
        uint64_t videoDropped = 0;
        uint64_t audioDropped = 0;
        uint64_t metadataDropped = 0;
        purgeVideoBeforeGenerationLocked_(alignmentGeneration, videoDropped);
        purgeMediaBeforeGenerationLocked_(m_audioQueueState, alignmentGeneration, audioDropped);
        purgeMediaBeforeGenerationLocked_(m_metadataQueueState, alignmentGeneration, metadataDropped);
        if (markQueuedCandidate) {
            for (std::deque<QueuedVideoEvent>::iterator it = m_videoQueueState.items.begin();
                 it != m_videoQueueState.items.end();
                 ++it) {
                if (it->event.kind != VideoEvent::Kind::Packet) {
                    continue;
                }
                if (it->enqueueGeneration == alignmentGeneration) {
                    it->cutBeforeDispatch = true;
                    break;
                }
            }
        } else if (pendingItem) {
            pendingItem->cutBeforeDispatch = true;
        }
        m_suppressRecoveredVideoOutput.store(true);
        m_loggedSuppressedRecoveredPacket.store(false);
        m_forceVideoDiscontinuity.store(true);
        m_forceAudioDiscontinuity.store(true);
        m_forceMetadataDiscontinuity.store(true);
        m_loggedWaitingForAlignedKeyFrame = false;
        m_unifiedPressureActive = false;
        armPressureCooldownLocked_(config);
        if (emitRecoveryEvent) {
            emitRecovery_(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                          "live keyframe cut");
        }
        logLiveJump_(alignmentGeneration, videoDropped, audioDropped, metadataDropped, timestamp);
    }

    bool consumerPressureActiveLocked_() const {
        return m_consumerPressure.softPressure ||
               m_consumerPressure.hardPressure ||
               m_consumerPressure.decoderStalled;
    }

    bool updateUnifiedPressureStateLocked_(const VideoConfig& config,
                                           std::size_t extraItems,
                                           std::size_t extraBytes,
                                           uint32_t extraTimestamp,
                                           bool includeExtraPacket) {
        SW_UNUSED(extraItems);
        if (!usesUnifiedLiveTrim_(config)) {
            m_unifiedPressureActive = false;
            return false;
        }
        const bool wasActive = m_unifiedPressureActive;
        int latencyMs = 0;
        queuedVideoLatencyMsLocked_(config, extraTimestamp, includeExtraPacket, latencyMs);
        const bool ingressHigh =
            unifiedIngressHighLocked_(config, extraBytes, extraTimestamp, includeExtraPacket);
        const bool consumerActive = consumerPressureActiveLocked_();
        const bool cooldownActive = pressureCooldownActiveLocked_();
        if (m_waitingForAlignedKeyFrame ||
            (!cooldownActive && (ingressHigh || consumerActive))) {
            m_unifiedPressureActive = true;
        } else if (unifiedIngressLowLocked_(config)) {
            m_unifiedPressureActive = false;
        }
        if (!wasActive && m_unifiedPressureActive) {
            logUnifiedPressureActivatedLocked_(ingressHigh, consumerActive, latencyMs);
        }
        return !wasActive && m_unifiedPressureActive;
    }

    void enterHardOverflowRecoveryLocked_(SwString& requestReason) {
        const uint64_t videoDropped =
            static_cast<uint64_t>(m_videoQueueState.items.size());
        const uint64_t audioDropped =
            static_cast<uint64_t>(m_audioQueueState.items.size());
        const uint64_t metadataDropped =
            static_cast<uint64_t>(m_metadataQueueState.items.size());
        m_videoQueueState.items.clear();
        m_videoQueueState.queuedBytes = 0;
        m_audioQueueState.items.clear();
        m_audioQueueState.queuedBytes = 0;
        m_metadataQueueState.items.clear();
        m_metadataQueueState.queuedBytes = 0;
        m_unifiedPressureActive = true;
        m_waitingForAlignedKeyFrame = true;
        m_loggedWaitingForAlignedKeyFrame = false;
        m_suppressRecoveredVideoOutput.store(true);
        m_loggedSuppressedRecoveredPacket.store(false);
        m_forceVideoDiscontinuity.store(true);
        m_forceAudioDiscontinuity.store(true);
        m_forceMetadataDiscontinuity.store(true);
        requestReason = "video hard overflow";
        emitRecovery_(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                      requestReason);
        swCWarning(kSwLogCategory_SwRtspTrackGraph)
            << "[SwRtspTrackGraph] Hard overflow waiting for next keyframe"
            << " videoDroppedBeforeKeyframe=" << videoDropped
            << " audioDroppedForAlignment=" << audioDropped
            << " metadataDroppedForAlignment=" << metadataDropped;
    }

    void enterConsumerRecoveryLocked_(SwString& requestReason) {
        const uint64_t videoDropped =
            static_cast<uint64_t>(m_videoQueueState.items.size());
        const uint64_t audioDropped =
            static_cast<uint64_t>(m_audioQueueState.items.size());
        const uint64_t metadataDropped =
            static_cast<uint64_t>(m_metadataQueueState.items.size());
        m_videoQueueState.items.clear();
        m_videoQueueState.queuedBytes = 0;
        m_audioQueueState.items.clear();
        m_audioQueueState.queuedBytes = 0;
        m_metadataQueueState.items.clear();
        m_metadataQueueState.queuedBytes = 0;
        m_unifiedPressureActive = true;
        m_waitingForAlignedKeyFrame = true;
        m_loggedWaitingForAlignedKeyFrame = false;
        m_suppressRecoveredVideoOutput.store(true);
        m_loggedSuppressedRecoveredPacket.store(false);
        m_forceVideoDiscontinuity.store(true);
        m_forceAudioDiscontinuity.store(true);
        m_forceMetadataDiscontinuity.store(true);
        requestReason = m_consumerPressure.decoderStalled
                            ? SwString("video decoder stalled")
                            : SwString("video consumer hard pressure");
        emitRecovery_(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                      requestReason);
        swCWarning(kSwLogCategory_SwRtspTrackGraph)
            << "[SwRtspTrackGraph] Consumer pressure waiting for next keyframe"
            << " videoDroppedBeforeKeyframe=" << videoDropped
            << " audioDroppedForAlignment=" << audioDropped
            << " metadataDroppedForAlignment=" << metadataDropped
            << " consumerQueuedPackets=" << m_consumerPressure.queuedPackets
            << " consumerQueuedBytes=" << m_consumerPressure.queuedBytes
            << " consumerHard=" << (m_consumerPressure.hardPressure ? 1 : 0)
            << " decoderStalled=" << (m_consumerPressure.decoderStalled ? 1 : 0);
    }

    void queueVideoUnifiedLocked_(const VideoConfig& config,
                                  QueuedVideoEvent& item,
                                  bool& requestKeyFrame,
                                  SwString& requestReason) {
        const bool pressureActivated =
            updateUnifiedPressureStateLocked_(config,
                                             1U,
                                             item.queuedBytes,
                                             item.accessUnitTimestamp,
                                             true);
        if (pressureActivated &&
            !m_waitingForAlignedKeyFrame &&
            hasAnyQueuedVideoPacketLocked_()) {
            requestReason = "video pressure";
        }

        if (m_waitingForAlignedKeyFrame && !item.keyFrameCandidate) {
            ++m_videoDroppedBeforeKeyframe;
            if (!m_loggedWaitingForAlignedKeyFrame) {
                m_loggedWaitingForAlignedKeyFrame = true;
                swCWarning(kSwLogCategory_SwRtspTrackGraph)
                    << "[SwRtspTrackGraph] Dropping video packets while waiting for a retained keyframe";
            }
            return;
        }

        bool queuedCut = false;
        if (item.keyFrameCandidate) {
            uint64_t alignmentGeneration = item.enqueueGeneration;
            std::deque<QueuedVideoEvent>::iterator existingCandidate = m_videoQueueState.items.end();
            const bool foundExisting =
                findQueuedVideoByTimestampLocked_(item.accessUnitTimestamp,
                                                 alignmentGeneration,
                                                 existingCandidate);
            const bool haveOlderBacklog =
                hasQueuedVideoPacketOlderThanGenerationLocked_(alignmentGeneration);
            if (m_waitingForAlignedKeyFrame || (haveOlderBacklog && m_unifiedPressureActive)) {
                activateAlignmentCutLocked_(config,
                                            alignmentGeneration,
                                            item.accessUnitTimestamp,
                                            foundExisting ? nullptr : &item,
                                            foundExisting,
                                            !m_waitingForAlignedKeyFrame);
                m_waitingForAlignedKeyFrame = false;
                queuedCut = true;
                requestReason.clear();
            }
        }

        pushVideoLocked_(item);
        updateUnifiedPressureStateLocked_(config, 0U, 0U, 0U, false);

        if (unifiedHardOverflowLocked_(config)) {
            enterHardOverflowRecoveryLocked_(requestReason);
            requestKeyFrame = true;
            return;
        }

        if (queuedCut) {
            m_audioCv.notify_one();
            m_metadataCv.notify_one();
        }
    }

    void queueVideoBoundedLocked_(const QueuedVideoEvent& item,
                                  bool& requestKeyFrame,
                                  SwString& requestReason) {
        pushVideoLocked_(item);
        if (queueExceedsLimits_(m_videoQueueState.items.size(),
                                m_videoQueueState.queuedBytes,
                                m_videoQueueLimits,
                                true)) {
            enterHardOverflowRecoveryLocked_(requestReason);
            requestKeyFrame = true;
        }
    }

    void resetVideoState_() {
        std::lock_guard<std::mutex> lock(m_videoStateMutex);
        m_h264Depacketizer.reset();
        m_h265Depacketizer.reset();
        m_tsProgramDemux.reset();
        m_haveSequence = false;
        m_lastSequence = 0;
        m_framesEmitted = 0;
        m_lastWaitingKeyFrameRequestTime = std::chrono::steady_clock::time_point();
        m_waitingKeyFrameRequestSuppressedUntil = std::chrono::steady_clock::time_point();
    }

    bool popVideoWait_(QueuedVideoEvent& out, int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_videoCv.wait_for(lock,
                           std::chrono::milliseconds(timeoutMs),
                           [this]() {
                               return m_stopRequested.load() ||
                                      !m_videoQueueState.items.empty();
                           });
        if (m_videoQueueState.items.empty()) {
            return false;
        }
        popFrontVideoLocked_(m_videoQueueState, out);
        return true;
    }

    bool popMediaWait_(MediaQueueState& state,
                       std::condition_variable& cv,
                       QueuedMediaPacket& out,
                       int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        cv.wait_for(lock,
                    std::chrono::milliseconds(timeoutMs),
                    [this, &state]() {
                        return m_stopRequested.load() || !state.items.empty();
                    });
        if (state.items.empty()) {
            return false;
        }
        popFrontMediaLocked_(state, out);
        return true;
    }

    void applyQueuedAlignmentCut_() {
        const VideoConfig config = currentVideoConfig_();
        std::lock_guard<std::mutex> lock(m_videoStateMutex);
        m_haveSequence = false;
        m_lastWaitingKeyFrameRequestTime = std::chrono::steady_clock::time_point();
        m_waitingKeyFrameRequestSuppressedUntil =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
        if (config.transportStream) {
            m_tsProgramDemux.reset();
        } else if (config.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.onSequenceGap(true);
        } else {
            m_h264Depacketizer.onSequenceGap(true);
        }
        m_suppressRecoveredVideoOutput.store(false);
        m_loggedSuppressedRecoveredPacket.store(false);
    }

    void videoWorkerLoop_() {
        while (!m_stopRequested.load()) {
            QueuedVideoEvent queuedEvent;
            if (!popVideoWait_(queuedEvent, 50)) {
                maybeLogVideoWorkerStats_();
                continue;
            }
            const std::chrono::steady_clock::time_point startTime =
                std::chrono::steady_clock::now();
            if (queuedEvent.cutBeforeDispatch) {
                applyQueuedAlignmentCut_();
            }
            switch (queuedEvent.event.kind) {
            case VideoEvent::Kind::Packet:
                handleVideoPacketEvent_(queuedEvent.event);
                break;
            case VideoEvent::Kind::Gap:
                handleVideoGap_(queuedEvent.event.expectedSequence,
                                queuedEvent.event.actualSequence);
                break;
            case VideoEvent::Kind::Reset:
                resetVideoState_();
                m_forceVideoDiscontinuity.store(true);
                break;
            }
            const int64_t handleUs =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startTime)
                    .count();
            recordVideoWorkerEvent_(queuedEvent.event.kind, handleUs);
            maybeLogVideoWorkerStats_();
        }
    }

    void mediaWorkerLoop_(MediaQueueState& state,
                          std::condition_variable& cv,
                          std::atomic<bool>& discontinuityFlag) {
        while (!m_stopRequested.load()) {
            QueuedMediaPacket queuedPacket;
            if (!popMediaWait_(state, cv, queuedPacket, 50)) {
                continue;
            }
            SwMediaPacket packet = queuedPacket.packet;
            if (discontinuityFlag.exchange(false)) {
                packet.setDiscontinuity(true);
            }
            MediaPacketCallback callback;
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                callback = m_mediaPacketCallback;
            }
            if (callback) {
                callback(packet);
            }
        }
    }

    void handleVideoPacketEvent_(const VideoEvent& event) {
        const VideoConfig config = currentVideoConfig_();
        if (event.packet.payloadType != 0 &&
            config.payloadType >= 0 &&
            event.packet.payloadType != static_cast<uint8_t>(config.payloadType)) {
            return;
        }
        if (event.detectGapFromSequence) {
            if (m_haveSequence) {
                const uint16_t expected =
                    static_cast<uint16_t>(m_lastSequence + 1);
                const int16_t delta =
                    static_cast<int16_t>(event.packet.sequenceNumber - expected);
                if (delta < 0) {
                    return;
                }
                if (delta != 0) {
                    handleVideoGap_(expected, event.packet.sequenceNumber);
                }
            }
            m_lastSequence = event.packet.sequenceNumber;
            m_haveSequence = true;
        }

        if (config.transportStream) {
            m_tsProgramDemux.feed(
                reinterpret_cast<const uint8_t*>(event.packet.payload.constData()),
                static_cast<size_t>(event.packet.payload.size()),
                event.packet.timestamp);
            return;
        }

        if (config.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.push(event.packet);
            maybeRequestKeyFrameWhileWaiting_(config, m_h265Depacketizer.isWaitingForKeyFrame());
            return;
        }

        m_h264Depacketizer.push(event.packet);
        maybeRequestKeyFrameWhileWaiting_(config, m_h264Depacketizer.isWaitingForKeyFrame());
    }

    void handleVideoGap_(uint16_t expected, uint16_t actual) {
        const VideoConfig config = currentVideoConfig_();
        const int lostPackets =
            static_cast<int>(static_cast<uint16_t>(actual - expected));
        const bool forceKeyFrameRecovery =
            forceKeyFrameRecoveryForGap_(config, lostPackets);
        m_forceVideoDiscontinuity.store(true);
        if (config.transportStream) {
            m_tsProgramDemux.reset();
        } else if (config.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.onSequenceGap(forceKeyFrameRecovery);
        } else {
            m_h264Depacketizer.onSequenceGap(forceKeyFrameRecovery);
        }
        if (forceKeyFrameRecovery) {
            emitRecovery_(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                          "rtp loss");
        }
        if (forceKeyFrameRecovery || lostPackets > 1 || m_framesEmitted == 0) {
            requestKeyFrame_("rtp loss");
        }
    }

    void maybeRequestKeyFrameWhileWaiting_(const VideoConfig& config,
                                           bool waitingForKeyFrame) {
        if (config.transportStream || !waitingForKeyFrame) {
            m_lastWaitingKeyFrameRequestTime = std::chrono::steady_clock::time_point();
            return;
        }
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (m_waitingKeyFrameRequestSuppressedUntil.time_since_epoch().count() != 0 &&
            now < m_waitingKeyFrameRequestSuppressedUntil) {
            return;
        }
        if (m_lastWaitingKeyFrameRequestTime.time_since_epoch().count() != 0) {
            const long long elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastWaitingKeyFrameRequestTime)
                    .count();
            if (elapsed < 1000) {
                return;
            }
        }
        m_lastWaitingKeyFrameRequestTime = now;
        requestKeyFrame_("waiting keyframe");
    }

    void handleProgramPacket_(const SwMediaPacket& packet) {
        switch (packet.type()) {
        case SwMediaPacket::Type::Video: {
            const SwVideoPacket::Codec codec =
                (packet.codec() == "h265" || packet.codec() == "hevc")
                    ? SwVideoPacket::Codec::H265
                    : SwVideoPacket::Codec::H264;
            SwVideoPacket videoPacket(codec,
                                      packet.payload(),
                                      packet.pts(),
                                      packet.dts(),
                                      packet.isKeyFrame());
            videoPacket.setDiscontinuity(packet.isDiscontinuity());
            emitRecoveredPacket_(videoPacket);
            break;
        }
        case SwMediaPacket::Type::Audio:
            submitAudioPacket(packet);
            break;
        case SwMediaPacket::Type::Metadata:
            submitMetadataPacket(packet);
            break;
        case SwMediaPacket::Type::Subtitle:
            submitMetadataPacket(packet);
            break;
        case SwMediaPacket::Type::Unknown:
        default:
            break;
        }
    }

    void emitRecoveredPacket_(const SwVideoPacket& packet) {
        if (m_suppressRecoveredVideoOutput.load()) {
            if (!m_loggedSuppressedRecoveredPacket.exchange(true)) {
                swCWarning(kSwLogCategory_SwRtspTrackGraph)
                    << "[SwRtspTrackGraph] Suppressing recovered packet while awaiting retained keyframe";
            }
            return;
        }
        SwVideoPacket outputPacket = packet;
        if (m_forceVideoDiscontinuity.exchange(false)) {
            outputPacket.setDiscontinuity(true);
        }
        ++m_framesEmitted;
        VideoPacketCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            callback = m_videoPacketCallback;
        }
        if (callback) {
            const std::chrono::steady_clock::time_point callbackStart =
                std::chrono::steady_clock::now();
            callback(outputPacket);
            const int64_t callbackUs =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - callbackStart)
                    .count();
            recordVideoWorkerCallback_(callbackUs);
        }
    }

    struct VideoWorkerStats {
        uint64_t packetEvents{0};
        uint64_t gapEvents{0};
        uint64_t resetEvents{0};
        uint64_t callbacks{0};
        uint64_t callbackFrames{0};
        int64_t totalHandleUs{0};
        int64_t maxHandleUs{0};
        int64_t totalCallbackUs{0};
        int64_t maxCallbackUs{0};
        std::chrono::steady_clock::time_point lastLogTime{};
    };

    void recordVideoWorkerEvent_(VideoEvent::Kind kind, int64_t handleUs) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        switch (kind) {
        case VideoEvent::Kind::Packet:
            ++m_videoWorkerStats.packetEvents;
            break;
        case VideoEvent::Kind::Gap:
            ++m_videoWorkerStats.gapEvents;
            break;
        case VideoEvent::Kind::Reset:
            ++m_videoWorkerStats.resetEvents;
            break;
        }
        m_videoWorkerStats.totalHandleUs += handleUs;
        if (handleUs > m_videoWorkerStats.maxHandleUs) {
            m_videoWorkerStats.maxHandleUs = handleUs;
        }
    }

    void recordVideoWorkerCallback_(int64_t callbackUs) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        ++m_videoWorkerStats.callbacks;
        ++m_videoWorkerStats.callbackFrames;
        m_videoWorkerStats.totalCallbackUs += callbackUs;
        if (callbackUs > m_videoWorkerStats.maxCallbackUs) {
            m_videoWorkerStats.maxCallbackUs = callbackUs;
        }
    }

    void maybeLogVideoWorkerStats_() {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const bool highQueue = m_videoQueueState.items.size() >= 64U;
        if (m_videoWorkerStats.lastLogTime.time_since_epoch().count() != 0) {
            const int64_t elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_videoWorkerStats.lastLogTime)
                    .count();
            const int64_t minElapsedMs = highQueue ? 1000 : 5000;
            if (elapsedMs < minElapsedMs) {
                return;
            }
        }

        const uint64_t eventCount =
            m_videoWorkerStats.packetEvents +
            m_videoWorkerStats.gapEvents +
            m_videoWorkerStats.resetEvents;
        const int64_t avgHandleUs =
            eventCount > 0 ? (m_videoWorkerStats.totalHandleUs / static_cast<int64_t>(eventCount)) : 0;
        const int64_t avgCallbackUs =
            m_videoWorkerStats.callbacks > 0
                ? (m_videoWorkerStats.totalCallbackUs / static_cast<int64_t>(m_videoWorkerStats.callbacks))
                : 0;

        swCWarning(kSwLogCategory_SwRtspTrackGraph)
            << "[SwRtspTrackGraph] Video worker stats"
            << " packets=" << m_videoWorkerStats.packetEvents
            << " gaps=" << m_videoWorkerStats.gapEvents
            << " resets=" << m_videoWorkerStats.resetEvents
            << " frames=" << m_videoWorkerStats.callbackFrames
            << " avgHandleUs=" << avgHandleUs
            << " maxHandleUs=" << m_videoWorkerStats.maxHandleUs
            << " avgCallbackUs=" << avgCallbackUs
            << " maxCallbackUs=" << m_videoWorkerStats.maxCallbackUs
            << " queueItems=" << m_videoQueueState.items.size()
            << " queueBytes=" << m_videoQueueState.queuedBytes;

        m_videoWorkerStats = VideoWorkerStats();
        m_videoWorkerStats.lastLogTime = now;
    }

    void requestKeyFrame_(const SwString& reason) {
        KeyFrameRequestCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            callback = m_keyFrameRequestCallback;
        }
        if (callback) {
            callback(reason);
        }
    }

    void emitRecovery_(SwMediaSource::RecoveryEvent::Kind kind,
                       const SwString& reason) {
        RecoveryCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            callback = m_recoveryCallback;
        }
        if (callback) {
            callback(kind, reason);
        }
    }

    VideoConfig currentVideoConfig_() const {
        std::lock_guard<std::mutex> lock(m_configMutex);
        return m_videoConfig;
    }

    mutable std::mutex m_configMutex;
    VideoConfig m_videoConfig{};

    mutable std::mutex m_callbackMutex;
    VideoPacketCallback m_videoPacketCallback{};
    MediaPacketCallback m_mediaPacketCallback{};
    TracksChangedCallback m_tracksChangedCallback{};
    KeyFrameRequestCallback m_keyFrameRequestCallback{};
    RecoveryCallback m_recoveryCallback{};

    mutable std::mutex m_videoStateMutex;
    SwRtpDepacketizerH264 m_h264Depacketizer{};
    SwRtpDepacketizerH265 m_h265Depacketizer{};
    SwTsProgramDemux m_tsProgramDemux{};
    bool m_haveSequence{false};
    uint16_t m_lastSequence{0};
    uint64_t m_framesEmitted{0};
    std::chrono::steady_clock::time_point m_lastWaitingKeyFrameRequestTime{};
    std::chrono::steady_clock::time_point m_waitingKeyFrameRequestSuppressedUntil{};

    mutable std::mutex m_queueMutex;
    std::condition_variable m_videoCv{};
    std::condition_variable m_audioCv{};
    std::condition_variable m_metadataCv{};
    VideoQueueState m_videoQueueState{};
    MediaQueueState m_audioQueueState{};
    MediaQueueState m_metadataQueueState{};
    QueueLimits m_videoQueueLimits{256U, 4U * 1024U * 1024U, 512U, 8U * 1024U * 1024U};
    QueueLimits m_audioQueueLimits{128U, 2U * 1024U * 1024U, 256U, 4U * 1024U * 1024U};
    QueueLimits m_metadataQueueLimits{96U, 512U * 1024U, 192U, 1024U * 1024U};
    std::atomic<uint64_t> m_nextEnqueueGeneration{1};
    SwVideoSource::ConsumerPressure m_consumerPressure{};
    bool m_unifiedPressureActive{false};
    std::chrono::steady_clock::time_point m_pressureRearmDeadline{};
    bool m_waitingForAlignedKeyFrame{false};
    bool m_loggedWaitingForAlignedKeyFrame{false};
    uint64_t m_videoDroppedBeforeKeyframe{0};
    uint64_t m_audioDroppedForAlignment{0};
    uint64_t m_metadataDroppedForAlignment{0};
    VideoWorkerStats m_videoWorkerStats{};

    std::thread m_videoWorker{};
    std::thread m_audioWorker{};
    std::thread m_metadataWorker{};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_forceVideoDiscontinuity{true};
    std::atomic<bool> m_suppressRecoveredVideoOutput{false};
    std::atomic<bool> m_loggedSuppressedRecoveredPacket{false};
    std::atomic<bool> m_forceAudioDiscontinuity{true};
    std::atomic<bool> m_forceMetadataDiscontinuity{true};
};
