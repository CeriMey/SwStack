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
        AcceptedInOrder,
        AcceptedOutOfOrder,
        Duplicate,
        Late
    };

    enum class AdvanceReason {
        None,
        AgeLimit,
        BufferLimit
    };

    struct PopResult {
        bool ready{false};
        bool gapAdvanced{false};
        uint16_t expectedSequence{0};
        uint16_t actualSequence{0};
        AdvanceReason advanceReason{AdvanceReason::None};
        std::size_t queuedPackets{0};
        int waitAgeMs{0};
        int gapDistance{0};
        SwByteArray datagram{};
    };

    struct Snapshot {
        std::size_t queuedPackets{0};
        std::size_t queueHighWatermark{0};
        bool expectedValid{false};
        uint16_t expectedSequence{0};
        bool blocked{false};
        uint16_t blockedExpectedSequence{0};
        uint16_t blockedNextSequence{0};
        int blockedGap{0};
        int blockedAgeMs{0};
        int maxBlockedGap{0};
        int maxBlockedAgeMs{0};
        uint64_t outOfOrderPackets{0};
        uint64_t duplicatePackets{0};
        uint64_t latePackets{0};
        uint64_t exactPops{0};
        uint64_t gapAdvanceByAge{0};
        uint64_t gapAdvanceBySize{0};
        uint64_t trimmedLatePackets{0};
    };

    void reset() {
        m_buffer.clear();
        m_expectedSequence = 0;
        m_expectedValid = false;
        m_waitingForExpected = false;
        m_waitingExpectedSequence = 0;
        m_waitingNextSequence = 0;
        m_waitingSince = {};
        m_queueHighWatermark = 0;
        m_outOfOrderPackets = 0;
        m_duplicatePackets = 0;
        m_latePackets = 0;
        m_exactPops = 0;
        m_gapAdvanceByAge = 0;
        m_gapAdvanceBySize = 0;
        m_trimmedLatePackets = 0;
        m_maxBlockedGap = 0;
        m_maxBlockedAgeMs = 0;
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
        const int sequenceDelta = static_cast<int>(compareSequence_(sequenceNumber, m_expectedSequence));
        if (sequenceDelta < 0) {
            ++m_latePackets;
            return InsertResult::Late;
        }
        auto inserted = m_buffer.emplace(sequenceNumber, Entry{datagram, arrivalTime});
        if (!inserted.second) {
            inserted.first->second.arrivalTime = arrivalTime;
            inserted.first->second.datagram = datagram;
            ++m_duplicatePackets;
            return InsertResult::Duplicate;
        }
        if (m_buffer.size() > m_queueHighWatermark) {
            m_queueHighWatermark = m_buffer.size();
        }
        if (sequenceDelta == 0) {
            return InsertResult::AcceptedInOrder;
        }
        ++m_outOfOrderPackets;
        if (sequenceDelta > m_maxBlockedGap) {
            m_maxBlockedGap = sequenceDelta;
        }
        return InsertResult::AcceptedOutOfOrder;
    }

    PopResult popReady(bool /*allowGapAdvance*/) {
        PopResult result;
        if (m_buffer.empty()) {
            clearBlockedState_();
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
                result.queuedPackets = m_buffer.size();
                m_buffer.erase(exactIt);
                ++m_exactPops;
                clearBlockedState_();
                m_expectedSequence = static_cast<uint16_t>(m_expectedSequence + 1);
                return result;
            }

            auto nextIt = findNext_(m_expectedSequence);
            if (nextIt == m_buffer.end()) {
                trimLatePackets_();
                clearBlockedState_();
                return result;
            }

            const auto ageMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   now - nextIt->second.arrivalTime)
                                   .count());
            const int gapDistance =
                static_cast<int>(compareSequence_(nextIt->first, m_expectedSequence));
            if (ageMs > m_maxBlockedAgeMs) {
                m_maxBlockedAgeMs = ageMs;
            }
            if (gapDistance > m_maxBlockedGap) {
                m_maxBlockedGap = gapDistance;
            }
            const bool sizeExceeded = m_buffer.size() >= m_maxPackets;
            const bool ageExceeded = ageMs >= m_maxDelayMs;
            if (!sizeExceeded && !ageExceeded) {
                updateBlockedState_(m_expectedSequence, nextIt->first, now);
                return result;
            }

            result.ready = true;
            result.gapAdvanced = true;
            result.expectedSequence = m_expectedSequence;
            result.actualSequence = nextIt->first;
            result.advanceReason = sizeExceeded ? AdvanceReason::BufferLimit : AdvanceReason::AgeLimit;
            result.queuedPackets = m_buffer.size();
            result.waitAgeMs = ageMs;
            result.gapDistance = gapDistance;
            result.datagram = nextIt->second.datagram;
            m_buffer.erase(nextIt);
            if (result.advanceReason == AdvanceReason::BufferLimit) {
                ++m_gapAdvanceBySize;
            } else {
                ++m_gapAdvanceByAge;
            }
            clearBlockedState_();
            m_expectedSequence = static_cast<uint16_t>(result.actualSequence + 1);
            return result;
        }

        return result;
    }

    Snapshot snapshot() const {
        Snapshot out;
        out.queuedPackets = m_buffer.size();
        out.queueHighWatermark = m_queueHighWatermark;
        out.expectedValid = m_expectedValid;
        out.expectedSequence = m_expectedSequence;
        out.outOfOrderPackets = m_outOfOrderPackets;
        out.duplicatePackets = m_duplicatePackets;
        out.latePackets = m_latePackets;
        out.exactPops = m_exactPops;
        out.gapAdvanceByAge = m_gapAdvanceByAge;
        out.gapAdvanceBySize = m_gapAdvanceBySize;
        out.trimmedLatePackets = m_trimmedLatePackets;
        out.maxBlockedGap = m_maxBlockedGap;
        out.maxBlockedAgeMs = m_maxBlockedAgeMs;
        out.blocked = m_waitingForExpected;
        out.blockedExpectedSequence = m_waitingExpectedSequence;
        out.blockedNextSequence = m_waitingNextSequence;
        if (m_waitingForExpected) {
            out.blockedGap = static_cast<int>(compareSequence_(m_waitingNextSequence,
                                                               m_waitingExpectedSequence));
            if (m_waitingSince.time_since_epoch().count() != 0) {
                out.blockedAgeMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - m_waitingSince)
                        .count());
            }
        }
        return out;
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
            ++m_trimmedLatePackets;
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

    void updateBlockedState_(uint16_t expectedSequence,
                             uint16_t nextSequence,
                             const std::chrono::steady_clock::time_point& now) {
        if (!m_waitingForExpected || m_waitingExpectedSequence != expectedSequence) {
            m_waitingForExpected = true;
            m_waitingExpectedSequence = expectedSequence;
            m_waitingSince = now;
        }
        m_waitingNextSequence = nextSequence;
    }

    void clearBlockedState_() {
        m_waitingForExpected = false;
        m_waitingExpectedSequence = 0;
        m_waitingNextSequence = 0;
        m_waitingSince = {};
    }

    std::map<uint16_t, Entry> m_buffer{};
    uint16_t m_expectedSequence{0};
    bool m_expectedValid{false};
    std::size_t m_maxPackets{32};
    int m_maxDelayMs{40};
    bool m_waitingForExpected{false};
    uint16_t m_waitingExpectedSequence{0};
    uint16_t m_waitingNextSequence{0};
    std::chrono::steady_clock::time_point m_waitingSince{};
    std::size_t m_queueHighWatermark{0};
    uint64_t m_outOfOrderPackets{0};
    uint64_t m_duplicatePackets{0};
    uint64_t m_latePackets{0};
    uint64_t m_exactPops{0};
    uint64_t m_gapAdvanceByAge{0};
    uint64_t m_gapAdvanceBySize{0};
    uint64_t m_trimmedLatePackets{0};
    int m_maxBlockedGap{0};
    int m_maxBlockedAgeMs{0};
};
