#pragma once

/**
 * @file src/media/SwMediaSource.h
 * @ingroup media
 * @brief Declares the generic media source interface used by SwMediaPlayer.
 */

#include "SwDebug.h"
#include "core/fs/SwMutex.h"
#include "core/types/SwList.h"
#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"

#include <atomic>
#include <functional>

class SwMediaSource {
public:
    enum class StreamState {
        Stopped,
        Connecting,
        Recovering,
        Streaming
    };

    struct RecoveryEvent {
        enum class Kind {
            LiveCut,
            TransportReset,
            Timeout,
            Reconnect
        };

        uint64_t epoch{0};
        Kind kind{Kind::LiveCut};
        SwString reason{};
    };

    struct StreamStatus {
        StreamState state{StreamState::Stopped};
        SwString reason{};
    };

    using MediaPacketCallback = std::function<void(const SwMediaPacket&)>;
    using StatusCallback = std::function<void(const StreamStatus&)>;
    using TracksChangedCallback = std::function<void(const SwList<SwMediaTrack>&)>;
    using RecoveryCallback = std::function<void(const RecoveryEvent&)>;

    virtual ~SwMediaSource() = default;

    virtual SwString name() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    void setMediaPacketCallback(MediaPacketCallback callback) {
        SwMutexLocker lock(m_mediaCallbackMutex);
        m_mediaPacketCallback = std::move(callback);
    }

    void setStatusCallback(StatusCallback callback) {
        StreamStatus currentStatus;
        {
            SwMutexLocker lock(m_statusMutex);
            m_statusCallback = std::move(callback);
            currentStatus = m_streamStatus;
        }
        StatusCallback cb;
        {
            SwMutexLocker lock(m_statusMutex);
            cb = m_statusCallback;
        }
        if (cb) {
            cb(currentStatus);
        }
    }

    void setTracksChangedCallback(TracksChangedCallback callback) {
        SwList<SwMediaTrack> currentTracks;
        {
            SwMutexLocker lock(m_tracksMutex);
            m_tracksChangedCallback = std::move(callback);
            currentTracks = m_tracks;
        }
        TracksChangedCallback cb;
        {
            SwMutexLocker lock(m_tracksMutex);
            cb = m_tracksChangedCallback;
        }
        if (cb) {
            cb(currentTracks);
        }
    }

    void setRecoveryCallback(RecoveryCallback callback) {
        SwMutexLocker lock(m_recoveryMutex);
        m_recoveryCallback = std::move(callback);
    }

    StreamStatus streamStatus() const {
        SwMutexLocker lock(m_statusMutex);
        return m_streamStatus;
    }

    SwList<SwMediaTrack> tracks() const {
        SwMutexLocker lock(m_tracksMutex);
        return m_tracks;
    }

protected:
    bool hasMediaPacketCallback() const {
        SwMutexLocker lock(m_mediaCallbackMutex);
        return static_cast<bool>(m_mediaPacketCallback);
    }

    void emitMediaPacket(const SwMediaPacket& packet) {
        MediaPacketCallback cb;
        {
            SwMutexLocker lock(m_mediaCallbackMutex);
            cb = m_mediaPacketCallback;
        }
        if (cb) {
            cb(packet);
        }
    }

    void emitStatus(StreamState state, const SwString& reason = SwString()) {
        StatusCallback cb;
        StreamStatus status;
        {
            SwMutexLocker lock(m_statusMutex);
            if (m_streamStatus.state == state && m_streamStatus.reason == reason) {
                return;
            }
            m_streamStatus.state = state;
            m_streamStatus.reason = reason;
            status = m_streamStatus;
            cb = m_statusCallback;
        }
        if (cb) {
            cb(status);
        }
    }

    void setTracks(const SwList<SwMediaTrack>& tracks) {
        TracksChangedCallback cb;
        SwList<SwMediaTrack> copy = tracks;
        {
            SwMutexLocker lock(m_tracksMutex);
            m_tracks = tracks;
            cb = m_tracksChangedCallback;
        }
        if (cb) {
            cb(copy);
        }
    }

    void emitRecovery(RecoveryEvent::Kind kind,
                      const SwString& reason = SwString()) {
        RecoveryCallback cb;
        RecoveryEvent event;
        {
            SwMutexLocker lock(m_recoveryMutex);
            event.epoch = m_nextRecoveryEpoch.fetch_add(1);
            event.kind = kind;
            event.reason = reason;
            cb = m_recoveryCallback;
        }
        if (cb) {
            cb(event);
        }
    }

    bool isRunning() const { return m_running.load(); }
    void setRunning(bool running) { m_running.store(running); }

private:
    mutable SwMutex m_mediaCallbackMutex;
    MediaPacketCallback m_mediaPacketCallback{};
    mutable SwMutex m_statusMutex;
    StatusCallback m_statusCallback{};
    StreamStatus m_streamStatus{};
    mutable SwMutex m_tracksMutex;
    TracksChangedCallback m_tracksChangedCallback{};
    SwList<SwMediaTrack> m_tracks{};
    mutable SwMutex m_recoveryMutex;
    RecoveryCallback m_recoveryCallback{};
    std::atomic<uint64_t> m_nextRecoveryEpoch{1};
    std::atomic<bool> m_running{false};
};
