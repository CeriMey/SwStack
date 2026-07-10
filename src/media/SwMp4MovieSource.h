#pragma once

/**
 * @file src/media/SwMp4MovieSource.h
 * @ingroup media
 * @brief Declares the platform-neutral movie source backed by the native MP4 demuxer.
 *
 * The source demuxes progressive MP4/MOV/M4V files with `SwMp4Demuxer` and emits the video
 * track as compressed `SwVideoPacket`s (H.264/H.265 Annex-B or AV1 OBU streams) paced in
 * real time on the sample decode timestamps. Decoding is left to the regular pipeline
 * decoders, mirroring how network sources feed the player. Seek, duration and position are
 * exposed through `SwMediaTimelineSource`.
 *
 * It contains no platform-specific code: `SwPlatformMovieSource` selects it on every
 * non-Windows platform (Windows keeps the MediaFoundation source, which also covers
 * MKV/AVI/... containers).
 *
 * Lifecycle notes: `start()` reaps a worker left over from a previous run (end of file or
 * error), honours a `seek()` issued while stopped, and otherwise resumes from the last
 * position when stopped mid-file — matching the pause()/play() and seek()/play() patterns
 * of SwMediaPlayer.
 */

#include "media/SwMediaTimelineSource.h"
#include "media/SwMp4Demuxer.h"
#include "media/SwVideoSource.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

static constexpr const char* kSwLogCategory_SwMp4MovieSource = "sw.media.swmp4moviesource";

class SwMp4MovieSource : public SwVideoSource, public SwMediaTimelineSource {
public:
    explicit SwMp4MovieSource(const std::string& filePath)
        : m_path(filePath) {}

    ~SwMp4MovieSource() override {
        stop();
    }

    SwString name() const override { return "SwMp4MovieSource"; }

    bool initialize() {
        if (m_initialized) {
            return true;
        }
        if (m_path.empty()) {
            swCError(kSwLogCategory_SwMp4MovieSource) << "[SwMp4MovieSource] empty path.";
            return false;
        }
        if (!m_demuxer.openFile(m_path)) {
            swCError(kSwLogCategory_SwMp4MovieSource)
                << "[SwMp4MovieSource] failed to open " << m_path << ": "
                << m_demuxer.errorText();
            return false;
        }
        const SwMp4Demuxer::Track* video = m_demuxer.videoTrack();
        if (!video) {
            swCError(kSwLogCategory_SwMp4MovieSource)
                << "[SwMp4MovieSource] no supported video track in " << m_path;
            return false;
        }
        m_videoTrackId = video->trackId;
        m_videoCodec = video->videoCodec;
        m_keyFramePrefix = video->keyFramePrefix;
        m_sampleCount = m_demuxer.sampleCount(m_videoTrackId);
        m_durationMs = video->durationMs >= 0 ? video->durationMs : m_demuxer.durationMs();
        publishTracks_(*video);
        m_initialized = true;
        swCDebug(kSwLogCategory_SwMp4MovieSource)
            << "[SwMp4MovieSource] " << m_path << ": " << video->codecName << " "
            << video->width << "x" << video->height << ", " << m_sampleCount << " samples, "
            << m_durationMs << " ms";
        return true;
    }

    void start() override {
        if (isRunning()) {
            return;
        }
        // Reap a worker that ended on its own (end of file or read error): assigning a new
        // std::thread over a still-joinable one would call std::terminate.
        if (m_worker.joinable()) {
            m_worker.join();
        }
        emitStatus(StreamState::Connecting, "Opening media file...");
        if (!initialize()) {
            emitStatus(StreamState::Recovering, "Failed to initialize movie source");
            return;
        }
        // Honour a seek issued while stopped; otherwise resume mid-file (pause/play pattern).
        if (m_pendingSeekMs.load() < 0) {
            const std::int64_t last = m_lastPositionMs.load();
            if (last > 0 && (m_durationMs <= 0 || last < m_durationMs)) {
                m_pendingSeekMs.store(last);
            } else {
                m_lastPositionMs.store(0);
            }
        }
        setRunning(true);
        m_worker = std::thread([this]() { streamLoop_(); });
    }

    void stop() override {
        setRunning(false);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    void setLoop(bool loop) {
        m_loop.store(loop);
    }

    bool isSeekable() const override { return m_initialized; }

    std::int64_t durationMs() const override { return m_durationMs; }

    std::int64_t positionMs() const override { return m_lastPositionMs.load(); }

    bool seek(std::int64_t positionMs) override {
        if (!m_initialized) {
            return false;
        }
        if (positionMs < 0) {
            positionMs = 0;
        }
        m_pendingSeekMs.store(positionMs);
        m_lastPositionMs.store(positionMs);
        return true;
    }

private:
    void publishTracks_(const SwMp4Demuxer::Track& video) {
        SwMediaTrack track;
        track.id = "file-video-0";
        track.type = SwMediaTrack::Type::Video;
        track.codec = SwString(video.codecName.c_str());
        track.clockRate = 1000;
        track.selected = true;
        track.availability = SwMediaTrack::Availability::Available;
        SwList<SwMediaTrack> tracks;
        tracks.append(track);
        setTracks(tracks);
    }

    void streamLoop_() {
        bool reachedEndOfFile = false;
        std::uint64_t index = 0;
        bool havePlaybackClock = false;
        std::chrono::steady_clock::time_point playbackStart;
        std::int64_t clockBaseMs = 0;
        bool discontinuity = false;

        while (isRunning()) {
            const std::int64_t requestedSeekMs = m_pendingSeekMs.exchange(-1);
            if (requestedSeekMs >= 0) {
                index = m_demuxer.findSyncSampleAtOrBefore(m_videoTrackId, requestedSeekMs);
                havePlaybackClock = false;
                discontinuity = true;
                emitStatus(StreamState::Streaming, "Seeking");
            }

            if (index >= m_sampleCount) {
                if (m_loop.load()) {
                    index = 0;
                    havePlaybackClock = false;
                    discontinuity = true;
                    continue;
                }
                reachedEndOfFile = true;
                break;
            }

            SwMp4Demuxer::Sample sample;
            if (!m_demuxer.readSample(m_videoTrackId, index, sample)) {
                swCError(kSwLogCategory_SwMp4MovieSource)
                    << "[SwMp4MovieSource] failed to read sample " << index << " from "
                    << m_path;
                emitStatus(StreamState::Recovering, "Movie read failed");
                break;
            }
            ++index;

            if (!havePlaybackClock) {
                playbackStart = std::chrono::steady_clock::now();
                clockBaseMs = sample.dtsMs >= 0 ? sample.dtsMs : 0;
                havePlaybackClock = true;
            }
            const std::int64_t relativeMs =
                (sample.dtsMs >= clockBaseMs) ? (sample.dtsMs - clockBaseMs) : 0;
            const std::chrono::steady_clock::time_point dueTime =
                playbackStart + std::chrono::milliseconds(relativeMs);
            while (isRunning()) {
                const std::chrono::steady_clock::time_point now =
                    std::chrono::steady_clock::now();
                if (now >= dueTime) {
                    break;
                }
                const std::int64_t waitMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(dueTime - now)
                        .count();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(waitMs > 20 ? 20 : (waitMs > 0 ? waitMs : 1)));
                if (m_pendingSeekMs.load() >= 0) {
                    break;
                }
            }
            if (!isRunning()) {
                break;
            }
            if (m_pendingSeekMs.load() >= 0) {
                continue; // drop this sample, the seek handler picks the new position
            }

            SwByteArray payload = std::move(sample.payload);
            if (sample.keyFrame && !m_keyFramePrefix.isEmpty()) {
                payload.prepend(m_keyFramePrefix);
            }

            SwVideoPacket packet(m_videoCodec,
                                 std::move(payload),
                                 sample.ptsMs,
                                 sample.dtsMs,
                                 sample.keyFrame);
            packet.setClockRate(1000);
            if (discontinuity) {
                packet.setDiscontinuity(true);
                discontinuity = false;
            }
            m_lastPositionMs.store(sample.ptsMs >= 0 ? sample.ptsMs : sample.dtsMs);
            emitStatus(StreamState::Streaming, "Streaming");
            emitPacket(packet);
        }
        setRunning(false);
        emitStatus(StreamState::Stopped,
                   reachedEndOfFile ? SwString("End of file") : SwString("Stream stopped"));
    }

    std::string m_path;
    SwMp4Demuxer m_demuxer;
    bool m_initialized{false};
    uint32_t m_videoTrackId{0};
    SwVideoPacket::Codec m_videoCodec{SwVideoPacket::Codec::Unknown};
    SwByteArray m_keyFramePrefix{};
    std::uint64_t m_sampleCount{0};
    std::int64_t m_durationMs{-1};
    std::atomic<std::int64_t> m_lastPositionMs{-1};
    std::atomic<std::int64_t> m_pendingSeekMs{-1};
    std::atomic<bool> m_loop{false};
    std::thread m_worker;
};
