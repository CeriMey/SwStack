#pragma once

/**
 * @file src/media/rtp/SwRtpStats.h
 * @ingroup media
 * @brief Declares lightweight RTP counters exposed by RTP session helpers.
 */

#include <atomic>
#include <cstdint>

struct SwRtpStatsSnapshot {
    uint64_t receivedDatagrams{0};
    uint64_t emittedPackets{0};
    uint64_t latePackets{0};
    uint64_t payloadMismatches{0};
    uint64_t gapEvents{0};
    uint64_t receiverReportsSent{0};
    uint64_t pliSent{0};
};

class SwRtpStats {
public:
    SwRtpStatsSnapshot snapshot() const {
        SwRtpStatsSnapshot out;
        out.receivedDatagrams = m_receivedDatagrams.load();
        out.emittedPackets = m_emittedPackets.load();
        out.latePackets = m_latePackets.load();
        out.payloadMismatches = m_payloadMismatches.load();
        out.gapEvents = m_gapEvents.load();
        out.receiverReportsSent = m_receiverReportsSent.load();
        out.pliSent = m_pliSent.load();
        return out;
    }

    std::atomic<uint64_t> m_receivedDatagrams{0};
    std::atomic<uint64_t> m_emittedPackets{0};
    std::atomic<uint64_t> m_latePackets{0};
    std::atomic<uint64_t> m_payloadMismatches{0};
    std::atomic<uint64_t> m_gapEvents{0};
    std::atomic<uint64_t> m_receiverReportsSent{0};
    std::atomic<uint64_t> m_pliSent{0};
};
