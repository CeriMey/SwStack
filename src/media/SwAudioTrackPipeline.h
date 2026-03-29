#pragma once

/**
 * @file src/media/SwAudioTrackPipeline.h
 * @ingroup media
 * @brief Declares the asynchronous live audio packet pipeline used by SwMediaPlayer.
 */

#include "media/SwAudioOutput.h"
#include "media/SwAudioPipeline.h"
#include "media/SwMediaPacket.h"
#include "media/SwMediaTrackQueue.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class SwAudioTrackPipeline {
public:
    SwAudioTrackPipeline()
        : m_queue([](const SwMediaPacket& packet) {
              return static_cast<std::size_t>(packet.payload().size()) + 64U;
          }) {
        m_queue.setLimits(128, 2 * 1024 * 1024);
    }

    ~SwAudioTrackPipeline() {
        stop();
    }

    void setAudioOutput(const std::shared_ptr<SwAudioOutput>& output) {
        std::lock_guard<std::mutex> lock(m_pipelineMutex);
        m_audioOutput = output;
        m_pipeline.setAudioOutput(m_audioOutput);
    }

    bool setDecoderSelection(SwAudioPacket::Codec codec, const SwString& decoderId) {
        std::lock_guard<std::mutex> lock(m_pipelineMutex);
        return m_pipeline.setDecoderSelection(codec, decoderId);
    }

    void clearDecoderSelection(SwAudioPacket::Codec codec) {
        std::lock_guard<std::mutex> lock(m_pipelineMutex);
        m_pipeline.clearDecoderSelection(codec);
    }

    SwString decoderSelection(SwAudioPacket::Codec codec) const {
        std::lock_guard<std::mutex> lock(m_pipelineMutex);
        return m_pipeline.decoderSelection(codec);
    }

    void start() {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true)) {
            return;
        }
        m_stopRequested.store(false);
        m_forceDiscontinuity.store(true);
        m_queue.restart();
        m_worker = std::thread([this]() { workerLoop_(); });
    }

    void stop() {
        if (!m_running.exchange(false)) {
            return;
        }
        m_stopRequested.store(true);
        m_queue.shutdown();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        m_queue.clear();
        m_forceDiscontinuity.store(true);
    }

    void enqueue(const SwMediaPacket& packet) {
        if (!m_running.load()) {
            return;
        }
        auto result = m_queue.push(packet);
        if (result.droppedOldest) {
            m_forceDiscontinuity.store(true);
        }
    }

private:
    void workerLoop_() {
        while (!m_stopRequested.load()) {
            SwMediaPacket packet;
            if (!m_queue.popWait(packet, 50)) {
                continue;
            }
            if (m_forceDiscontinuity.exchange(false)) {
                packet.setDiscontinuity(true);
            }
            std::lock_guard<std::mutex> lock(m_pipelineMutex);
            m_pipeline.handleMediaPacket(packet);
        }
    }

    mutable std::mutex m_pipelineMutex;
    std::shared_ptr<SwAudioOutput> m_audioOutput;
    SwAudioPipeline m_pipeline{};
    SwMediaTrackQueue<SwMediaPacket> m_queue;
    std::thread m_worker{};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_forceDiscontinuity{true};
};
