#pragma once

/**
 * @file src/media/SwMediaTrackQueue.h
 * @ingroup media
 * @brief Declares a bounded queue used by live media track pipelines.
 */

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

template <typename T>
class SwMediaTrackQueue {
public:
    struct PushResult {
        bool accepted{false};
        bool droppedOldest{false};
        std::size_t droppedItems{0};
        std::size_t droppedBytes{0};
    };

    struct Snapshot {
        std::size_t queuedItems{0};
        std::size_t queuedBytes{0};
        std::uint64_t pushedItems{0};
        std::uint64_t droppedItems{0};
        std::uint64_t droppedBytes{0};
    };

    using SizeFunction = std::function<std::size_t(const T&)>;

    explicit SwMediaTrackQueue(SizeFunction sizeFunction = SizeFunction())
        : m_sizeFunction(std::move(sizeFunction)) {}

    void setLimits(std::size_t maxItems, std::size_t maxBytes) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_maxItems = maxItems;
        m_maxBytes = maxBytes;
        trimLocked_(0, nullptr);
    }

    void restart() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = false;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_cv.notify_all();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_queuedBytes = 0;
    }

    PushResult push(T item) {
        PushResult result;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) {
            return result;
        }

        const std::size_t itemBytes = itemSizeLocked_(item);
        trimLocked_(itemBytes, &result);
        m_queue.push_back(std::move(item));
        m_queuedBytes += itemBytes;
        ++m_pushedItems;
        result.accepted = true;
        m_cv.notify_one();
        return result;
    }

    bool popWait(T& out, int timeoutMs = 50) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock,
                      std::chrono::milliseconds(timeoutMs),
                      [this]() { return m_stopped || !m_queue.empty(); });
        if (m_queue.empty()) {
            return false;
        }

        const std::size_t itemBytes = itemSizeLocked_(m_queue.front());
        out = std::move(m_queue.front());
        m_queue.pop_front();
        if (m_queuedBytes >= itemBytes) {
            m_queuedBytes -= itemBytes;
        } else {
            m_queuedBytes = 0;
        }
        return true;
    }

    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        Snapshot snapshot;
        snapshot.queuedItems = m_queue.size();
        snapshot.queuedBytes = m_queuedBytes;
        snapshot.pushedItems = m_pushedItems;
        snapshot.droppedItems = m_droppedItems;
        snapshot.droppedBytes = m_droppedBytes;
        return snapshot;
    }

private:
    std::size_t itemSizeLocked_(const T& item) const {
        return m_sizeFunction ? m_sizeFunction(item) : 0;
    }

    void trimLocked_(std::size_t incomingBytes, PushResult* result) {
        while (!m_queue.empty()) {
            const bool tooManyItems =
                (m_maxItems > 0) && (m_queue.size() >= m_maxItems);
            const bool tooManyBytes =
                (m_maxBytes > 0) && ((m_queuedBytes + incomingBytes) > m_maxBytes);
            if (!tooManyItems && !tooManyBytes) {
                break;
            }

            const std::size_t droppedBytes = itemSizeLocked_(m_queue.front());
            if (m_queuedBytes >= droppedBytes) {
                m_queuedBytes -= droppedBytes;
            } else {
                m_queuedBytes = 0;
            }
            m_queue.pop_front();
            ++m_droppedItems;
            m_droppedBytes += droppedBytes;
            if (result) {
                result->droppedOldest = true;
                ++result->droppedItems;
                result->droppedBytes += droppedBytes;
            }
        }
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<T> m_queue{};
    SizeFunction m_sizeFunction{};
    std::size_t m_maxItems{0};
    std::size_t m_maxBytes{0};
    std::size_t m_queuedBytes{0};
    std::uint64_t m_pushedItems{0};
    std::uint64_t m_droppedItems{0};
    std::uint64_t m_droppedBytes{0};
    bool m_stopped{false};
};
