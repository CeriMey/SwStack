#pragma once

/**
 * @file src/media/rtp/SwRtpJitterBuffer.h
 * @ingroup media
 * @brief Declares a small bounded RTP reordering buffer for low-latency video sessions.
 */

#include "core/types/SwByteArray.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <map>

class SwRtpJitterBuffer {
public:
    enum class InsertResult {
        Accepted,
        Duplicate,
        Late
    };

    struct PopResult {
        bool ready{false};
        bool gapAdvanced{false};
        uint16_t expectedSequence{0};
        uint16_t actualSequence{0};
        SwByteArray datagram{};
    };

    void reset() {
        m_buffer.clear();
        m_expectedSequence = 0;
        m_expectedValid = false;
    }

    void setLimits(std::size_t maxPackets, int maxDelayMs) {
        m_maxPackets = maxPackets == 0 ? 1 : maxPackets;
        m_maxDelayMs = maxDelayMs <= 0 ? 1 : maxDelayMs;
    }

    InsertResult enqueue(uint16_t sequenceNumber,
                         const SwByteArray& datagram,
                         const std::chrono::steady_clock::time_point& arrivalTime) {
        if (!m_expectedValid) {
            m_expectedSequence = sequenceNumber;
            m_expectedValid = true;
        }
        if (compareSequence_(sequenceNumber, m_expectedSequence) < 0) {
            return InsertResult::Late;
        }
        auto inserted = m_buffer.emplace(sequenceNumber, Entry{datagram, arrivalTime});
        if (!inserted.second) {
            inserted.first->second.arrivalTime = arrivalTime;
            inserted.first->second.datagram = datagram;
            return InsertResult::Duplicate;
        }
        return InsertResult::Accepted;
    }

    PopResult popReady(bool allowGapAdvance) {
        PopResult result;
        if (m_buffer.empty()) {
            return result;
        }

        if (!m_expectedValid) {
            m_expectedSequence = m_buffer.begin()->first;
            m_expectedValid = true;
        }

        const auto now = std::chrono::steady_clock::now();
        while (!m_buffer.empty()) {
            auto exactIt = m_buffer.find(m_expectedSequence);
            if (exactIt != m_buffer.end()) {
                result.ready = true;
                result.actualSequence = exactIt->first;
                result.datagram = exactIt->second.datagram;
                m_buffer.erase(exactIt);
                m_expectedSequence = static_cast<uint16_t>(m_expectedSequence + 1);
                return result;
            }

            auto nextIt = findNext_(m_expectedSequence);
            if (nextIt == m_buffer.end()) {
                trimLatePackets_();
                return result;
            }

            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   now - nextIt->second.arrivalTime)
                                   .count();
            if (!allowGapAdvance &&
                m_buffer.size() < m_maxPackets &&
                ageMs < m_maxDelayMs) {
                return result;
            }

            result.ready = true;
            result.gapAdvanced = true;
            result.expectedSequence = m_expectedSequence;
            result.actualSequence = nextIt->first;
            result.datagram = nextIt->second.datagram;
            m_buffer.erase(nextIt);
            m_expectedSequence = static_cast<uint16_t>(result.actualSequence + 1);
            return result;
        }

        return result;
    }

private:
    struct Entry {
        SwByteArray datagram{};
        std::chrono::steady_clock::time_point arrivalTime{};
    };

    static int16_t compareSequence_(uint16_t lhs, uint16_t rhs) {
        return static_cast<int16_t>(lhs - rhs);
    }

    void trimLatePackets_() {
        auto it = m_buffer.begin();
        while (it != m_buffer.end()) {
            if (compareSequence_(it->first, m_expectedSequence) >= 0) {
                break;
            }
            it = m_buffer.erase(it);
        }
    }

    std::map<uint16_t, Entry>::iterator findNext_(uint16_t sequenceNumber) {
        auto bestIt = m_buffer.end();
        int bestDelta = std::numeric_limits<int>::max();
        for (auto it = m_buffer.begin(); it != m_buffer.end(); ++it) {
            const int delta = static_cast<int>(compareSequence_(it->first, sequenceNumber));
            if (delta < 0) {
                continue;
            }
            if (delta < bestDelta) {
                bestDelta = delta;
                bestIt = it;
            }
        }
        return bestIt;
    }

    std::map<uint16_t, Entry> m_buffer{};
    uint16_t m_expectedSequence{0};
    bool m_expectedValid{false};
    std::size_t m_maxPackets{32};
    int m_maxDelayMs{40};
};
