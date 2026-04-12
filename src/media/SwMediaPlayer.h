#pragma once

/**
 * @file src/media/SwMediaPlayer.h
 * @ingroup media
 * @brief Declares the Qt-like media player facade that orchestrates source, video sink, and audio output.
 */

#include "core/runtime/SwCoreApplication.h"
#include "core/runtime/SwThread.h"
#include "media/SwAudioTrackPipeline.h"
#include "media/SwAudioOutput.h"
#include "media/SwMediaSource.h"
#include "media/SwMediaTimelineSource.h"
#include "media/SwMetadataTrackPipeline.h"
#include "media/SwVideoSink.h"

#include <atomic>
#include <memory>
#include <thread>

class SwMediaPlayer : public SwObject {
public:
    using TracksChangedCallback = std::function<void(const SwList<SwMediaTrack>&)>;
    using MetadataPacketCallback = std::function<void(const SwMediaPacket&)>;
    using SubtitlePacketCallback = std::function<void(const SwMediaPacket&)>;

    struct MediaInfo {
        int trackCount{0};
        int videoTrackCount{0};
        int audioTrackCount{0};
        int metadataTrackCount{0};
        int subtitleTrackCount{0};
        bool hasVideo{false};
        bool hasAudio{false};
        bool hasMetadata{false};
        bool hasSubtitles{false};
        SwString activeVideoTrackId{};
        SwString activeAudioTrackId{};
        SwString activeMetadataTrackId{};
        SwString activeSubtitleTrackId{};
        SwString videoCodec{};
        SwString audioCodec{};
        SwString metadataCodec{};
        int videoWidth{0};
        int videoHeight{0};
        double videoMeasuredFps{0.0};
        uint64_t presentedVideoFrames{0};
        int audioSampleRate{0};
        int audioChannelCount{0};
        bool audioOutputActive{false};
        SwString audioOutputName{};
        std::int64_t audioPlayedTimestamp{-1};
        std::int64_t durationMs{-1};
        std::int64_t positionMs{-1};
        bool seekable{false};
    };

    enum class PlaybackState {
        StoppedState,
        PlayingState,
        PausedState
    };

    explicit SwMediaPlayer(SwObject* parent = nullptr)
        : SwObject(parent),
          m_videoSink(std::make_shared<SwVideoSink>()),
          m_audioOutput(std::make_shared<SwAudioOutput>()) {
        m_audioTrackPipeline.setAudioOutput(m_audioOutput);
        m_metadataTrackPipeline.setPacketCallback([this](const SwMediaPacket& packet) {
            dispatchMetadataPacket_(packet);
        });
    }

    ~SwMediaPlayer() override {
        m_sourceCallbacksActive.store(false);
        m_callbackGuard.reset();
        if (m_source) {
            clearSourceCallbacks_(m_source);
        }
        stop();
    }

    void setSource(const std::shared_ptr<SwMediaSource>& source) {
        if (m_source == source) {
            return;
        }
        stop();
        if (m_source) {
            clearSourceCallbacks_(m_source);
        }
        m_source = source;
        m_tracks = source ? source->tracks() : SwList<SwMediaTrack>();
        m_activeAudioTrackId.clear();
        m_activeVideoTrackId.clear();
        m_activeMetadataTrackId.clear();
        m_activeSubtitleTrackId.clear();
        bindSourceCallbacks_(m_source);

        auto videoSource = std::dynamic_pointer_cast<SwVideoSource>(source);
        if (m_videoSink) {
            m_videoSink->setVideoSource(videoSource);
        }
        updateDefaultTrackSelection_();
        if (m_tracksChangedCallback) {
            m_tracksChangedCallback(m_tracks);
        }
    }

    std::shared_ptr<SwMediaSource> source() const { return m_source; }

    void setVideoSink(const std::shared_ptr<SwVideoSink>& sink) {
        m_videoSink = sink ? sink : std::make_shared<SwVideoSink>();
        auto videoSource = std::dynamic_pointer_cast<SwVideoSource>(m_source);
        m_videoSink->setVideoSource(videoSource);
    }

    std::shared_ptr<SwVideoSink> videoSink() const { return m_videoSink; }

    void setAudioOutput(const std::shared_ptr<SwAudioOutput>& output) {
        m_audioOutput = output ? output : std::make_shared<SwAudioOutput>();
        m_audioTrackPipeline.setAudioOutput(m_audioOutput);
    }

    std::shared_ptr<SwAudioOutput> audioOutput() const { return m_audioOutput; }

    SwList<SwMediaTrack> tracks() const { return m_tracks; }
    int trackCount() const { return static_cast<int>(m_tracks.size()); }

    int videoTrackCount() const { return trackCountByType_(SwMediaTrack::Type::Video); }
    int audioTrackCount() const { return trackCountByType_(SwMediaTrack::Type::Audio); }
    int metadataTrackCount() const { return trackCountByType_(SwMediaTrack::Type::Metadata); }
    int subtitleTrackCount() const { return trackCountByType_(SwMediaTrack::Type::Subtitle); }

    bool hasVideo() const { return videoTrackCount() > 0; }
    bool hasAudio() const { return audioTrackCount() > 0; }
    bool hasMetadata() const { return metadataTrackCount() > 0; }
    bool hasSubtitles() const { return subtitleTrackCount() > 0; }

    SwList<SwMediaTrack> tracksByType(SwMediaTrack::Type type) const {
        SwList<SwMediaTrack> filteredTracks;
        for (const auto& track : m_tracks) {
            if (track.type == type) {
                filteredTracks.append(track);
            }
        }
        return filteredTracks;
    }

    SwMediaTrack trackById(const SwString& trackId) const {
        for (const auto& track : m_tracks) {
            if (track.id == trackId) {
                return track;
            }
        }
        return SwMediaTrack();
    }

    SwMediaTrack activeVideoTrackInfo() const { return trackById(m_activeVideoTrackId); }
    SwMediaTrack activeAudioTrackInfo() const { return trackById(m_activeAudioTrackId); }
    SwMediaTrack activeMetadataTrackInfo() const { return trackById(m_activeMetadataTrackId); }
    SwMediaTrack activeSubtitleTrackInfo() const { return trackById(m_activeSubtitleTrackId); }

    SwString videoCodec() const { return activeVideoTrackInfo().codec; }
    SwString audioCodec() const { return activeAudioTrackInfo().codec; }
    SwString metadataCodec() const { return activeMetadataTrackInfo().codec; }
    SwString subtitleCodec() const { return activeSubtitleTrackInfo().codec; }

    int videoWidth() const { return m_videoSink ? m_videoSink->frameWidth() : 0; }
    int videoHeight() const { return m_videoSink ? m_videoSink->frameHeight() : 0; }
    double videoMeasuredFps() const { return m_videoSink ? m_videoSink->measuredFps() : 0.0; }
    uint64_t presentedVideoFrameCount() const { return m_videoSink ? m_videoSink->presentedFrameCount() : 0; }
    std::int64_t currentVideoTimestamp() const { return m_videoSink ? m_videoSink->currentFrameTimestamp() : -1; }

    int audioSampleRate() const {
        const SwMediaTrack track = activeAudioTrackInfo();
        if (track.sampleRate > 0) {
            return track.sampleRate;
        }
        return m_audioOutput ? m_audioOutput->sampleRate() : 0;
    }

    int audioChannelCount() const {
        const SwMediaTrack track = activeAudioTrackInfo();
        if (track.channelCount > 0) {
            return track.channelCount;
        }
        return m_audioOutput ? m_audioOutput->channelCount() : 0;
    }

    bool isAudioOutputActive() const { return m_audioOutput ? m_audioOutput->isActive() : false; }
    SwString audioOutputName() const { return m_audioOutput ? m_audioOutput->sinkName() : SwString(); }
    std::int64_t audioPlayedTimestamp() const { return m_audioOutput ? m_audioOutput->playedTimestamp() : -1; }

    MediaInfo mediaInfo() const {
        MediaInfo info;
        info.trackCount = trackCount();
        info.videoTrackCount = videoTrackCount();
        info.audioTrackCount = audioTrackCount();
        info.metadataTrackCount = metadataTrackCount();
        info.subtitleTrackCount = subtitleTrackCount();
        info.hasVideo = info.videoTrackCount > 0;
        info.hasAudio = info.audioTrackCount > 0;
        info.hasMetadata = info.metadataTrackCount > 0;
        info.hasSubtitles = info.subtitleTrackCount > 0;
        info.activeVideoTrackId = m_activeVideoTrackId;
        info.activeAudioTrackId = m_activeAudioTrackId;
        info.activeMetadataTrackId = m_activeMetadataTrackId;
        info.activeSubtitleTrackId = m_activeSubtitleTrackId;
        info.videoCodec = videoCodec();
        info.audioCodec = audioCodec();
        info.metadataCodec = metadataCodec();
        info.videoWidth = videoWidth();
        info.videoHeight = videoHeight();
        info.videoMeasuredFps = videoMeasuredFps();
        info.presentedVideoFrames = presentedVideoFrameCount();
        info.audioSampleRate = audioSampleRate();
        info.audioChannelCount = audioChannelCount();
        info.audioOutputActive = isAudioOutputActive();
        info.audioOutputName = audioOutputName();
        info.audioPlayedTimestamp = audioPlayedTimestamp();
        info.durationMs = durationMs();
        info.positionMs = positionMs();
        info.seekable = isSeekable();
        return info;
    }

    void setTracksChangedCallback(TracksChangedCallback callback) {
        m_tracksChangedCallback = std::move(callback);
        if (m_tracksChangedCallback) {
            m_tracksChangedCallback(m_tracks);
        }
    }

    void setActiveAudioTrack(const SwString& trackId) { m_activeAudioTrackId = trackId; }
    SwString activeAudioTrack() const { return m_activeAudioTrackId; }

    void setActiveVideoTrack(const SwString& trackId) { m_activeVideoTrackId = trackId; }
    SwString activeVideoTrack() const { return m_activeVideoTrackId; }

    void setActiveMetadataTrack(const SwString& trackId) { m_activeMetadataTrackId = trackId; }
    SwString activeMetadataTrack() const { return m_activeMetadataTrackId; }

    void setActiveSubtitleTrack(const SwString& trackId) { m_activeSubtitleTrackId = trackId; }
    SwString activeSubtitleTrack() const { return m_activeSubtitleTrackId; }

    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }
    bool isAudioEnabled() const { return m_audioEnabled; }

    void setMetadataEnabled(bool enabled) { m_metadataEnabled = enabled; }
    bool isMetadataEnabled() const { return m_metadataEnabled; }

    void setSubtitleEnabled(bool enabled) { m_subtitleEnabled = enabled; }
    bool isSubtitleEnabled() const { return m_subtitleEnabled; }

    void setMetadataPacketCallback(MetadataPacketCallback callback) {
        m_metadataPacketCallback = std::move(callback);
    }

    void setSubtitlePacketCallback(SubtitlePacketCallback callback) {
        m_subtitlePacketCallback = std::move(callback);
    }

    void setMuted(bool muted) {
        if (m_audioOutput) {
            m_audioOutput->setMuted(muted);
        }
    }

    bool isMuted() const {
        return m_audioOutput ? m_audioOutput->isMuted() : false;
    }

    void setVolume(float volume) {
        if (m_audioOutput) {
            m_audioOutput->setVolume(volume);
        }
    }

    float volume() const {
        return m_audioOutput ? m_audioOutput->volume() : 0.0f;
    }

    void setPreferredAudioDecoder(SwAudioPacket::Codec codec, const SwString& decoderId) {
        m_audioTrackPipeline.setDecoderSelection(codec, decoderId);
    }

    SwString preferredAudioDecoder(SwAudioPacket::Codec codec) const {
        return m_audioTrackPipeline.decoderSelection(codec);
    }

    static SwList<SwAudioDecoderDescriptor> availableAudioDecoders(SwAudioPacket::Codec codec) {
        return SwAudioDecoderFactory::instance().list(codec);
    }

    bool isSeekable() const {
        const std::shared_ptr<SwMediaTimelineSource> timelineSource =
            std::dynamic_pointer_cast<SwMediaTimelineSource>(m_source);
        return timelineSource ? timelineSource->isSeekable() : false;
    }

    std::int64_t durationMs() const {
        const std::shared_ptr<SwMediaTimelineSource> timelineSource =
            std::dynamic_pointer_cast<SwMediaTimelineSource>(m_source);
        return timelineSource ? timelineSource->durationMs() : -1;
    }

    std::int64_t positionMs() const {
        const std::shared_ptr<SwMediaTimelineSource> timelineSource =
            std::dynamic_pointer_cast<SwMediaTimelineSource>(m_source);
        return timelineSource ? timelineSource->positionMs() : -1;
    }

    bool seek(std::int64_t positionMs) {
        const std::shared_ptr<SwMediaTimelineSource> timelineSource =
            std::dynamic_pointer_cast<SwMediaTimelineSource>(m_source);
        return timelineSource ? timelineSource->seek(positionMs) : false;
    }

    void play() {
        if (!m_source) {
            return;
        }
        m_sourceCallbacksActive.store(true);
        bindSourceCallbacks_(m_source);
        m_audioTrackPipeline.start();
        m_metadataTrackPipeline.start();
        auto videoSource = std::dynamic_pointer_cast<SwVideoSource>(m_source);
        if (videoSource && m_videoSink) {
            m_videoSink->start();
        } else {
            m_source->start();
        }
        m_playbackState = PlaybackState::PlayingState;
    }

    void pause() {
        stop();
        m_playbackState = PlaybackState::PausedState;
    }

    void stop() {
        m_sourceCallbacksActive.store(false);
        clearSourceCallbacks_(m_source);
        m_playbackState = PlaybackState::StoppedState;
        if (m_videoSink) {
            m_videoSink->stop();
        } else if (m_source) {
            m_source->stop();
        }
        m_audioTrackPipeline.stop();
        m_metadataTrackPipeline.stop();
        if (m_audioOutput) {
            m_audioOutput->stop();
        }
    }

    PlaybackState playbackState() const { return m_playbackState; }

private:
    void updateDefaultTrackSelection_() {
        if (!m_activeVideoTrackId.isEmpty() && !trackExists_(m_activeVideoTrackId, SwMediaTrack::Type::Video)) {
            m_activeVideoTrackId.clear();
        }
        if (!m_activeAudioTrackId.isEmpty() && !trackExists_(m_activeAudioTrackId, SwMediaTrack::Type::Audio)) {
            m_activeAudioTrackId.clear();
        }
        if (!m_activeMetadataTrackId.isEmpty() && !trackExists_(m_activeMetadataTrackId, SwMediaTrack::Type::Metadata)) {
            m_activeMetadataTrackId.clear();
        }
        if (!m_activeSubtitleTrackId.isEmpty() && !trackExists_(m_activeSubtitleTrackId, SwMediaTrack::Type::Subtitle)) {
            m_activeSubtitleTrackId.clear();
        }
        if (m_activeVideoTrackId.isEmpty()) {
            for (const auto& track : m_tracks) {
                if (track.isVideo() && track.selected) {
                    m_activeVideoTrackId = track.id;
                    break;
                }
            }
            if (m_activeVideoTrackId.isEmpty()) {
                for (const auto& track : m_tracks) {
                    if (track.isVideo()) {
                        m_activeVideoTrackId = track.id;
                        break;
                    }
                }
            }
        }
        if (m_activeAudioTrackId.isEmpty()) {
            for (const auto& track : m_tracks) {
                if (track.isAudio() && track.selected) {
                    m_activeAudioTrackId = track.id;
                    break;
                }
            }
            if (m_activeAudioTrackId.isEmpty()) {
                for (const auto& track : m_tracks) {
                    if (track.isAudio()) {
                        m_activeAudioTrackId = track.id;
                        break;
                    }
                }
            }
        }
        if (m_activeMetadataTrackId.isEmpty()) {
            for (const auto& track : m_tracks) {
                if (track.isMetadata() && track.selected) {
                    m_activeMetadataTrackId = track.id;
                    break;
                }
            }
            if (m_activeMetadataTrackId.isEmpty()) {
                for (const auto& track : m_tracks) {
                    if (track.isMetadata()) {
                        m_activeMetadataTrackId = track.id;
                        break;
                    }
                }
            }
        }
        if (m_activeSubtitleTrackId.isEmpty()) {
            for (const auto& track : m_tracks) {
                if (track.isSubtitle() && track.selected) {
                    m_activeSubtitleTrackId = track.id;
                    break;
                }
            }
            if (m_activeSubtitleTrackId.isEmpty()) {
                for (const auto& track : m_tracks) {
                    if (track.isSubtitle()) {
                        m_activeSubtitleTrackId = track.id;
                        break;
                    }
                }
            }
        }
    }

    void handleMediaPacket_(const SwMediaPacket& packet) {
        if (packet.type() == SwMediaPacket::Type::Audio) {
            if (!m_audioEnabled) {
                return;
            }
            if (!m_activeAudioTrackId.isEmpty() && packet.trackId() != m_activeAudioTrackId) {
                return;
            }
            m_audioTrackPipeline.enqueue(packet);
            return;
        }
        if (packet.type() == SwMediaPacket::Type::Metadata) {
            if (!m_metadataEnabled) {
                return;
            }
            if (!m_activeMetadataTrackId.isEmpty() && packet.trackId() != m_activeMetadataTrackId) {
                return;
            }
            m_metadataTrackPipeline.enqueue(packet);
            return;
        }
        if (packet.type() == SwMediaPacket::Type::Subtitle) {
            if (!m_subtitleEnabled) {
                return;
            }
            if (!m_activeSubtitleTrackId.isEmpty() && packet.trackId() != m_activeSubtitleTrackId) {
                return;
            }
            dispatchSubtitlePacket_(packet);
        }
    }

    bool trackExists_(const SwString& trackId, SwMediaTrack::Type type) const {
        for (const auto& track : m_tracks) {
            if (track.id == trackId && track.type == type) {
                return true;
            }
        }
        return false;
    }

    void dispatchMetadataPacket_(const SwMediaPacket& packet) {
        dispatchToAffinity_([this, packet]() {
            if (m_metadataPacketCallback) {
                m_metadataPacketCallback(packet);
            }
        });
    }

    void dispatchSubtitlePacket_(const SwMediaPacket& packet) {
        dispatchToAffinity_([this, packet]() {
            if (m_subtitlePacketCallback) {
                m_subtitlePacketCallback(packet);
            }
        });
    }

    void clearSourceCallbacks_(const std::shared_ptr<SwMediaSource>& source) {
        if (!source) {
            return;
        }
        source->setMediaPacketCallback(SwMediaSource::MediaPacketCallback());
        source->setTracksChangedCallback(SwMediaSource::TracksChangedCallback());
        source->setRecoveryCallback(SwMediaSource::RecoveryCallback());
    }

    void bindSourceCallbacks_(const std::shared_ptr<SwMediaSource>& source) {
        if (!source) {
            return;
        }
        std::weak_ptr<int> weakGuard = m_callbackGuard;
        SwMediaSource* sourcePtr = source.get();
        source->setMediaPacketCallback([this, weakGuard, sourcePtr](const SwMediaPacket& packet) {
            if (weakGuard.expired() ||
                !m_sourceCallbacksActive.load() ||
                !m_source ||
                m_source.get() != sourcePtr) {
                return;
            }
            handleMediaPacket_(packet);
        });
        source->setTracksChangedCallback([this, weakGuard, sourcePtr](const SwList<SwMediaTrack>& tracks) {
            dispatchToAffinity_([this, weakGuard, sourcePtr, tracks]() {
                if (weakGuard.expired() ||
                    !m_sourceCallbacksActive.load() ||
                    !m_source ||
                    m_source.get() != sourcePtr) {
                    return;
                }
                m_tracks = tracks;
                updateDefaultTrackSelection_();
                if (m_tracksChangedCallback) {
                    m_tracksChangedCallback(m_tracks);
                }
            });
        });
        source->setRecoveryCallback([this, weakGuard, sourcePtr](const SwMediaSource::RecoveryEvent& event) {
            dispatchToAffinity_([this, weakGuard, sourcePtr, event]() {
                if (weakGuard.expired() ||
                    !m_sourceCallbacksActive.load() ||
                    !m_source ||
                    m_source.get() != sourcePtr ||
                    m_playbackState != PlaybackState::PlayingState) {
                    return;
                }
                handleRecovery_(event);
            });
        });
    }

    void handleRecovery_(const SwMediaSource::RecoveryEvent& event) {
        if (m_videoSink) {
            m_videoSink->recoverLiveEdge(event);
        }
        m_audioTrackPipeline.recoverLiveEdge(event);
        m_metadataTrackPipeline.recoverLiveEdge(event);
    }

    void dispatchToAffinity_(std::function<void()> task) {
        if (!task) {
            return;
        }
        ThreadHandle* affinity = threadHandle();
        if (affinity) {
            const std::thread::id affinityId = affinity->threadId();
            if (affinityId != std::thread::id{} &&
                affinityId == std::this_thread::get_id()) {
                task();
                return;
            }
            affinity->postTask(std::move(task));
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->postEvent(std::move(task));
            return;
        }
        task();
    }

    int trackCountByType_(SwMediaTrack::Type type) const {
        int count = 0;
        for (const auto& track : m_tracks) {
            if (track.type == type) {
                ++count;
            }
        }
        return count;
    }

    std::shared_ptr<SwMediaSource> m_source;
    std::shared_ptr<SwVideoSink> m_videoSink;
    std::shared_ptr<SwAudioOutput> m_audioOutput;
    SwAudioTrackPipeline m_audioTrackPipeline{};
    SwMetadataTrackPipeline m_metadataTrackPipeline{};
    SwList<SwMediaTrack> m_tracks{};
    TracksChangedCallback m_tracksChangedCallback{};
    SwString m_activeAudioTrackId{};
    SwString m_activeVideoTrackId{};
    SwString m_activeMetadataTrackId{};
    SwString m_activeSubtitleTrackId{};
    bool m_audioEnabled{true};
    bool m_metadataEnabled{false};
    bool m_subtitleEnabled{true};
    PlaybackState m_playbackState{PlaybackState::StoppedState};
    MetadataPacketCallback m_metadataPacketCallback{};
    SubtitlePacketCallback m_subtitlePacketCallback{};
    std::shared_ptr<int> m_callbackGuard{std::make_shared<int>(0)};
    std::atomic<bool> m_sourceCallbacksActive{false};
};
