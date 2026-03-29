#pragma once

/**
 * @file src/media/SwMetadataTrackPipeline.h
 * @ingroup media
 * @brief Declares the asynchronous metadata packet pipeline used by SwMediaPlayer.
 */

#include "media/SwMediaPacket.h"
#include "media/SwMediaTrackQueue.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class SwMetadataTrackPipeline {
public:
    using PacketCallback = std::function<void(const SwMediaPacket&)>;

    SwMetadataTrackPipeline()
        : m_queue([](const SwMediaPacket& packet) {
              return static_cast<std::size_t>(packet.payload().size()) + 64U;
          }) {
        m_queue.setLimits(96, 512 * 1024);
    }

    ~SwMetadataTrackPipeline() {
        stop();
    }

    void setPacketCallback(PacketCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callback = std::move(callback);
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
            PacketCallback callback;
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                callback = m_callback;
            }
            if (callback) {
                callback(packet);
            }
        }
    }

    mutable std::mutex m_callbackMutex;
    PacketCallback m_callback{};
    SwMediaTrackQueue<SwMediaPacket> m_queue;
    std::thread m_worker{};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_forceDiscontinuity{true};
};
