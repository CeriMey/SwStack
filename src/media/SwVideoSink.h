#pragma once

/**
 * @file src/media/SwVideoSink.h
 * @ingroup media
 * @brief Declares the non-UI video sink used by SwMediaPlayer and SwVideoWidget.
 */

#include "media/SwVideoDecoder.h"
#include "media/SwVideoFrame.h"
#include "media/SwVideoSource.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

static constexpr const char* kSwLogCategory_SwVideoSink = "sw.media.swvideosink";

class SwVideoSink {
public:
    using FrameCallback = std::function<void(const SwVideoFrame&)>;
    using StatusCallback = std::function<void(const SwVideoSource::StreamStatus&)>;

    SwVideoSink()
        : m_pipeline(std::make_shared<SwVideoPipeline>()) {
        m_pipeline->setAsyncDecode(true);
        m_pipeline->setQueueLimits(12, 2 * 1024 * 1024);
        installPipelineFrameCallback_();
        m_pipeline->useDecoderFactory(true);
    }

    ~SwVideoSink() {
        m_callbackGuard.reset();
        detachSourceStatusCallback_(m_source);
        stop();
    }

    void setVideoSource(const std::shared_ptr<SwVideoSource>& source) {
        auto previousSource = m_source;
        m_source = source;
        if (m_pipeline) {
            m_pipeline->setSource(source);
        }
        detachSourceStatusCallback_(previousSource);
        attachSourceStatusCallback_(source);
    }

    std::shared_ptr<SwVideoSource> videoSource() const { return m_source; }

    void setVideoDecoder(const std::shared_ptr<SwVideoDecoder>& decoder) {
        if (!decoder) {
            return;
        }
        m_decoder = decoder;
        if (m_pipeline) {
            m_pipeline->setDecoder(decoder);
            installPipelineFrameCallback_();
        }
    }

    std::shared_ptr<SwVideoDecoder> videoDecoder() const { return m_decoder; }

    static SwList<SwVideoDecoderDescriptor> availableVideoDecoders(SwVideoPacket::Codec codec) {
        return SwVideoDecoderFactory::instance().list(codec);
    }

    bool setPreferredVideoDecoder(SwVideoPacket::Codec codec, const SwString& decoderId) {
        if (!m_pipeline) {
            return false;
        }
        bool ok = m_pipeline->setDecoderSelection(codec, decoderId);
        if (ok) {
            m_decoder.reset();
        }
        return ok;
    }

    void clearPreferredVideoDecoder(SwVideoPacket::Codec codec) {
        if (!m_pipeline) {
            return;
        }
        m_pipeline->clearDecoderSelection(codec);
    }

    SwString preferredVideoDecoder(SwVideoPacket::Codec codec) const {
        if (!m_pipeline) {
            return SwString();
        }
        return m_pipeline->decoderSelection(codec);
    }

    void start() {
        if (!m_pipeline || !m_source) {
            return;
        }
        m_pipeline->start();
    }

    void stop() {
        if (m_pipeline) {
            m_pipeline->stop();
        }
    }

    void setFrameCallback(FrameCallback callback) {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_frameArrived = std::move(callback);
    }

    void setStatusCallback(StatusCallback callback) {
        SwVideoSource::StreamStatus status;
        {
            std::lock_guard<std::mutex> lock(m_streamStatusMutex);
            m_statusCallback = std::move(callback);
            status = m_streamStatus;
        }
        StatusCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_streamStatusMutex);
            cb = m_statusCallback;
        }
        if (cb) {
            cb(status);
        }
    }

    bool hasFrame() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame.isValid();
    }

    SwVideoFrame currentFrame() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame;
    }

    std::chrono::steady_clock::time_point lastFrameTime() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_lastFrameTime;
    }

    uint64_t presentedFrameCount() const { return m_presentedFrameCount.load(); }

    int frameWidth() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame.isValid() ? m_currentFrame.width() : 0;
    }

    int frameHeight() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame.isValid() ? m_currentFrame.height() : 0;
    }

    SwVideoPixelFormat framePixelFormat() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame.isValid() ? m_currentFrame.pixelFormat() : SwVideoPixelFormat::Unknown;
    }

    std::int64_t currentFrameTimestamp() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame.isValid() ? m_currentFrame.timestamp() : -1;
    }

    double measuredFps() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        const uint64_t count = m_presentedFrameCount.load();
        if (count < 2 ||
            m_firstFrameTime.time_since_epoch().count() == 0 ||
            m_lastFrameTime.time_since_epoch().count() == 0 ||
            m_lastFrameTime <= m_firstFrameTime) {
            return 0.0;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(m_lastFrameTime - m_firstFrameTime);
        if (elapsed.count() <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(count - 1) / elapsed.count();
    }

    SwVideoSource::StreamStatus streamStatus() const {
        std::lock_guard<std::mutex> lock(m_streamStatusMutex);
        return m_streamStatus;
    }

private:
    void installPipelineFrameCallback_() {
        if (!m_pipeline) {
            return;
        }
        std::weak_ptr<int> weakGuard = m_callbackGuard;
        m_pipeline->setFrameCallback([this, weakGuard](const SwVideoFrame& frame) {
            if (weakGuard.expired()) {
                return;
            }
            handleIncomingFrame_(frame);
        });
    }

    void attachSourceStatusCallback_(const std::shared_ptr<SwVideoSource>& source) {
        if (!source) {
            setStreamStatus_({});
            return;
        }
        std::weak_ptr<int> weakGuard = m_callbackGuard;
        source->setStatusCallback([this, weakGuard](const SwVideoSource::StreamStatus& status) {
            if (weakGuard.expired()) {
                return;
            }
            setStreamStatus_(status);
        });
        setTracksFromSource_(source);
    }

    void detachSourceStatusCallback_(const std::shared_ptr<SwVideoSource>& source) {
        if (!source) {
            return;
        }
        source->setStatusCallback(SwVideoSource::StatusCallback());
    }

    void setTracksFromSource_(const std::shared_ptr<SwVideoSource>& source) {
        if (!source) {
            return;
        }
        const auto mediaSource = std::dynamic_pointer_cast<SwMediaSource>(source);
        if (!mediaSource) {
            return;
        }
        mediaSource->setTracksChangedCallback([this](const SwList<SwMediaTrack>&) {});
    }

    void setStreamStatus_(const SwVideoSource::StreamStatus& status) {
        StatusCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_streamStatusMutex);
            m_streamStatus = status;
            cb = m_statusCallback;
        }
        if (cb) {
            cb(status);
        }
    }

    void handleIncomingFrame_(const SwVideoFrame& frame) {
        auto presented = ++m_presentedFrameCount;
        FrameCallback frameCallback;
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            if (m_firstFrameTime.time_since_epoch().count() == 0) {
                m_firstFrameTime = std::chrono::steady_clock::now();
            }
            m_currentFrame = frame;
            m_lastFrameTime = std::chrono::steady_clock::now();
            frameCallback = m_frameArrived;
        }
        if (!m_loggedFirstPresentedFrame.exchange(true)) {
            swCWarning(kSwLogCategory_SwVideoSink) << "[SwVideoSink] First frame received "
                        << frame.width() << "x" << frame.height()
                        << " ts=" << frame.timestamp()
                        << " fmt=" << static_cast<int>(frame.pixelFormat());
        } else if ((presented % 25) == 0) {
            swCWarning(kSwLogCategory_SwVideoSink) << "[SwVideoSink] Frame count="
                        << presented
                        << " ts=" << frame.timestamp()
                        << " fmt=" << static_cast<int>(frame.pixelFormat());
        }
        if (frameCallback) {
            frameCallback(frame);
        }
    }

    std::shared_ptr<SwVideoPipeline> m_pipeline;
    std::shared_ptr<SwVideoSource> m_source;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    mutable std::mutex m_frameMutex;
    SwVideoFrame m_currentFrame{};
    std::chrono::steady_clock::time_point m_firstFrameTime{};
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    FrameCallback m_frameArrived{};
    mutable std::mutex m_streamStatusMutex;
    SwVideoSource::StreamStatus m_streamStatus{};
    StatusCallback m_statusCallback{};
    std::shared_ptr<int> m_callbackGuard{std::make_shared<int>(0)};
    std::atomic<bool> m_loggedFirstPresentedFrame{false};
    std::atomic<uint64_t> m_presentedFrameCount{0};
};
