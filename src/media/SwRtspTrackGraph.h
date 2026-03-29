#pragma once

/**
 * @file src/media/SwRtspTrackGraph.h
 * @ingroup media
 * @brief Declares the internal live RTSP track graph used by SwRtspUdpSource.
 */

#include "SwDebug.h"
#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"
#include "media/SwMediaTrackQueue.h"
#include "media/SwVideoPacket.h"
#include "media/rtp/SwRtpDepacketizerH264.h"
#include "media/rtp/SwRtpDepacketizerH265.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwTsProgramDemux.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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
        bool lowLatencyDrop{false};
        int latencyTargetMs{150};
        bool transportStream{false};
        SwString fmtp{};
    };

    using VideoPacketCallback = std::function<void(const SwVideoPacket&)>;
    using MediaPacketCallback = std::function<void(const SwMediaPacket&)>;
    using TracksChangedCallback = std::function<void(const SwList<SwMediaTrack>&)>;
    using KeyFrameRequestCallback = std::function<void(const SwString&)>;

    SwRtspTrackGraph()
        : m_videoQueue([](const VideoEvent& event) {
              return static_cast<std::size_t>(event.packet.payload.size()) + 96U;
          }),
          m_audioQueue([](const SwMediaPacket& packet) {
              return static_cast<std::size_t>(packet.payload().size()) + 64U;
          }),
          m_metadataQueue([](const SwMediaPacket& packet) {
              return static_cast<std::size_t>(packet.payload().size()) + 64U;
          }) {
        m_videoQueue.setLimits(256, 4 * 1024 * 1024);
        m_audioQueue.setLimits(128, 2 * 1024 * 1024);
        m_metadataQueue.setLimits(96, 512 * 1024);

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

    void start() {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true)) {
            return;
        }
        m_stopRequested.store(false);
        m_forceVideoDiscontinuity.store(true);
        m_forceAudioDiscontinuity.store(true);
        m_forceMetadataDiscontinuity.store(true);
        resetVideoState_();
        m_videoQueue.restart();
        m_audioQueue.restart();
        m_metadataQueue.restart();
        m_videoWorker = std::thread([this]() { videoWorkerLoop_(); });
        m_audioWorker = std::thread([this]() { mediaWorkerLoop_(m_audioQueue,
                                                                m_forceAudioDiscontinuity); });
        m_metadataWorker = std::thread([this]() { mediaWorkerLoop_(m_metadataQueue,
                                                                   m_forceMetadataDiscontinuity); });
    }

    void stop() {
        if (!m_running.exchange(false)) {
            return;
        }
        m_stopRequested.store(true);
        m_videoQueue.shutdown();
        m_audioQueue.shutdown();
        m_metadataQueue.shutdown();
        if (m_videoWorker.joinable()) {
            m_videoWorker.join();
        }
        if (m_audioWorker.joinable()) {
            m_audioWorker.join();
        }
        if (m_metadataWorker.joinable()) {
            m_metadataWorker.join();
        }
        m_videoQueue.clear();
        m_audioQueue.clear();
        m_metadataQueue.clear();
        resetVideoState_();
    }

    void reset() {
        if (!m_running.load()) {
            resetVideoState_();
            return;
        }
        VideoEvent event;
        event.kind = VideoEvent::Kind::Reset;
        m_videoQueue.push(event);
    }

    void notePlaybackStarted() {
        if (!m_running.load()) {
            return;
        }
        VideoEvent event;
        event.kind = VideoEvent::Kind::PlaybackStarted;
        m_videoQueue.push(event);
    }

    void submitVideoPacket(const SwRtpSession::Packet& packet,
                           bool detectGapFromSequence = false) {
        if (!m_running.load()) {
            return;
        }
        VideoEvent event;
        event.kind = VideoEvent::Kind::Packet;
        event.packet = packet;
        event.detectGapFromSequence = detectGapFromSequence;
        auto result = m_videoQueue.push(event);
        if (result.droppedOldest) {
            m_forceVideoDiscontinuity.store(true);
            requestKeyFrame_("video queue overflow");
        }
    }

    void submitVideoGap(uint16_t expected, uint16_t actual) {
        if (!m_running.load()) {
            return;
        }
        VideoEvent event;
        event.kind = VideoEvent::Kind::Gap;
        event.expectedSequence = expected;
        event.actualSequence = actual;
        m_videoQueue.push(event);
    }

    void submitAudioPacket(const SwMediaPacket& packet) {
        if (!m_running.load()) {
            return;
        }
        auto result = m_audioQueue.push(packet);
        if (result.droppedOldest) {
            m_forceAudioDiscontinuity.store(true);
        }
    }

    void submitMetadataPacket(const SwMediaPacket& packet) {
        if (!m_running.load()) {
            return;
        }
        auto result = m_metadataQueue.push(packet);
        if (result.droppedOldest) {
            m_forceMetadataDiscontinuity.store(true);
        }
    }

private:
    struct VideoEvent {
        enum class Kind {
            Packet,
            Gap,
            Reset,
            PlaybackStarted
        };

        Kind kind{Kind::Packet};
        SwRtpSession::Packet packet{};
        uint16_t expectedSequence{0};
        uint16_t actualSequence{0};
        bool detectGapFromSequence{false};
    };

    static uint64_t rtpDelta_(uint32_t newer, uint32_t older) {
        if (newer >= older) {
            return static_cast<uint64_t>(newer - older);
        }
        return static_cast<uint64_t>(newer) +
               (0x100000000ULL - static_cast<uint64_t>(older));
    }

    void resetVideoState_() {
        std::lock_guard<std::mutex> lock(m_videoStateMutex);
        m_h264Depacketizer.reset();
        m_h265Depacketizer.reset();
        m_tsProgramDemux.reset();
        m_haveFirstTimestamp = false;
        m_firstTimestamp = 0;
        m_haveSequence = false;
        m_lastSequence = 0;
        m_waitingForKeyFrame = true;
        m_framesEmitted = 0;
        m_framesDropped = 0;
        m_playStart = {};
        m_lastWaitingKeyFrameRequestTime = {};
        m_currentCodec = SwVideoPacket::Codec::H264;
    }

    void videoWorkerLoop_() {
        while (!m_stopRequested.load()) {
            VideoEvent event;
            if (!m_videoQueue.popWait(event, 50)) {
                continue;
            }
            switch (event.kind) {
            case VideoEvent::Kind::Packet:
                handleVideoPacketEvent_(event);
                break;
            case VideoEvent::Kind::Gap:
                handleVideoGap_(event.expectedSequence, event.actualSequence);
                break;
            case VideoEvent::Kind::Reset:
                resetVideoState_();
                m_forceVideoDiscontinuity.store(true);
                break;
            case VideoEvent::Kind::PlaybackStarted:
                m_playStart = std::chrono::steady_clock::now();
                break;
            }
        }
    }

    void mediaWorkerLoop_(SwMediaTrackQueue<SwMediaPacket>& queue,
                          std::atomic<bool>& discontinuityFlag) {
        while (!m_stopRequested.load()) {
            SwMediaPacket packet;
            if (!queue.popWait(packet, 50)) {
                continue;
            }
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
            config.transportStream ||
            (config.codec == SwVideoPacket::Codec::H265 ? (lostPackets > 2)
                                                        : (lostPackets > 4));
        m_forceVideoDiscontinuity.store(true);
        if (config.transportStream) {
            m_tsProgramDemux.reset();
        } else if (config.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.onSequenceGap(forceKeyFrameRecovery);
        } else {
            m_h264Depacketizer.onSequenceGap(forceKeyFrameRecovery);
        }
        if (forceKeyFrameRecovery || lostPackets > 1 || m_framesEmitted == 0) {
            requestKeyFrame_("rtp loss");
        }
    }

    void maybeRequestKeyFrameWhileWaiting_(const VideoConfig& config,
                                           bool waitingForKeyFrame) {
        if (config.transportStream || !waitingForKeyFrame) {
            m_lastWaitingKeyFrameRequestTime = {};
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastWaitingKeyFrameRequestTime.time_since_epoch().count() != 0) {
            const auto elapsed =
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
        case SwMediaPacket::Type::Unknown:
        default:
            break;
        }
    }

    void emitRecoveredPacket_(const SwVideoPacket& packet) {
        SwVideoPacket outputPacket = packet;
        const VideoConfig config = currentVideoConfig_();
        m_currentCodec = outputPacket.codec();
        const uint32_t timestamp =
            static_cast<uint32_t>(outputPacket.pts() >= 0 ? outputPacket.pts() : 0);
        if (!m_haveFirstTimestamp) {
            m_firstTimestamp = timestamp;
            m_haveFirstTimestamp = true;
        }
        if (shouldDropFrame_(config, timestamp, outputPacket.isKeyFrame())) {
            ++m_framesDropped;
            return;
        }
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
            callback(outputPacket);
        }
    }

    bool shouldDropFrame_(const VideoConfig& config,
                          uint32_t timestamp,
                          bool keyFrame) const {
        if (!config.lowLatencyDrop || keyFrame || !m_haveFirstTimestamp ||
            config.clockRate <= 0) {
            return false;
        }
        if (m_playStart.time_since_epoch().count() == 0) {
            return false;
        }
        auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - m_playStart)
                          .count();
        if (wallMs < 0) {
            wallMs = 0;
        }
        const uint64_t wallRtp =
            (static_cast<uint64_t>(wallMs) *
             static_cast<uint64_t>(config.clockRate)) /
            1000ULL;
        const uint64_t mediaRtp = rtpDelta_(timestamp, m_firstTimestamp);
        uint64_t allowedLag =
            (static_cast<uint64_t>(config.latencyTargetMs) *
             static_cast<uint64_t>(config.clockRate)) /
            1000ULL;
        if (allowedLag == 0) {
            allowedLag = static_cast<uint64_t>(config.clockRate) / 100ULL;
        }
        return mediaRtp > (wallRtp + allowedLag);
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

    std::mutex m_videoStateMutex;
    SwRtpDepacketizerH264 m_h264Depacketizer{};
    SwRtpDepacketizerH265 m_h265Depacketizer{};
    SwTsProgramDemux m_tsProgramDemux{};
    bool m_haveFirstTimestamp{false};
    uint32_t m_firstTimestamp{0};
    bool m_haveSequence{false};
    uint16_t m_lastSequence{0};
    bool m_waitingForKeyFrame{true};
    std::uint64_t m_framesEmitted{0};
    std::uint64_t m_framesDropped{0};
    SwVideoPacket::Codec m_currentCodec{SwVideoPacket::Codec::H264};
    std::chrono::steady_clock::time_point m_playStart{};
    std::chrono::steady_clock::time_point m_lastWaitingKeyFrameRequestTime{};

    SwMediaTrackQueue<VideoEvent> m_videoQueue;
    SwMediaTrackQueue<SwMediaPacket> m_audioQueue;
    SwMediaTrackQueue<SwMediaPacket> m_metadataQueue;

    std::thread m_videoWorker{};
    std::thread m_audioWorker{};
    std::thread m_metadataWorker{};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_forceVideoDiscontinuity{true};
    std::atomic<bool> m_forceAudioDiscontinuity{true};
    std::atomic<bool> m_forceMetadataDiscontinuity{true};
};
