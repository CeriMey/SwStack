#include "SwPair.h"
#include "SwMap.h"
#ifndef SWQUICLOSSRECOVERY_H
#define SWQUICLOSSRECOVERY_H

#include "SwVector.h"
#include "SwString.h"

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <map>
#include <vector>
#include <utility>

// SwQuicLossRecovery implements RFC 9002 sections 5 (RTT estimation) and 6
// (loss detection + PTO) for a *single* packet-number space.
//
// Internal precision choice:
//   RFC 9002 defines the RTT estimator with the fractions 1/8, 7/8, 1/4 and
//   3/4.  Doing this with integer milliseconds would discard sub-millisecond
//   state on every ACK (e.g. the RFC example second sample yields rttvar=42.5
//   and smoothed=102.5).  To preserve that precision through the EWMA we keep
//   all RTT state as double milliseconds internally.  Public getters expose the
//   double values verbatim; computePtoMs() rounds to the nearest whole
//   millisecond only at the uint64_t API boundary.
class SwQuicLossRecovery {
public:
    struct SentPacket {
        std::uint64_t packetNumber;
        std::uint64_t sentTimeMs;
        bool ackEliciting;
        bool inFlight;
        std::size_t sentBytes;

        SentPacket()
            : packetNumber(0), sentTimeMs(0), ackEliciting(false),
              inFlight(false), sentBytes(0) {}
    };

    struct AckResult {
        SwVector<std::uint64_t> newlyAcked;
        SwVector<std::uint64_t> lost;
    };

    // RFC 9002 constants.
    static std::uint64_t kPacketThreshold() { return 3; }   // section 6.1.1
    static double kTimeThresholdFactor() { return 9.0 / 8.0; } // section 6.1.2
    static double kGranularityMs() { return 1.0; }          // timer granularity
    static double kInitialRttMs() { return 333.0; }         // section 6.2.2

    SwQuicLossRecovery()
        : m_handshakeConfirmed(false),
          m_firstSample(false),
          m_latestRttMs(kInitialRttMs()),
          m_minRttMs(0.0),
          m_smoothedRttMs(kInitialRttMs()),
          m_rttVarMs(kInitialRttMs() / 2.0),
          m_largestAckedPacket(0),
          m_haveLargestAcked(false) {}

    // Handshake-confirmed gate: ack delay is only subtracted from RTT samples
    // once the handshake is confirmed (RFC 9002 section 5.3).
    void setHandshakeConfirmed(bool confirmed) { m_handshakeConfirmed = confirmed; }
    bool handshakeConfirmed() const { return m_handshakeConfirmed; }

    // RTT accessors (double milliseconds; see precision note above).
    double latestRttMs() const { return m_latestRttMs; }
    double minRttMs() const { return m_minRttMs; }
    double smoothedRttMs() const { return m_smoothedRttMs; }
    double rttVarMs() const { return m_rttVarMs; }
    bool hasRttSample() const { return m_firstSample; }

    std::size_t inFlightCount() const { return m_sent.size(); }

    // Record a packet as sent / tracked in this space.
    void onPacketSent(const SentPacket& packet) {
        m_sent[packet.packetNumber] = packet;
    }

    // Process an incoming ACK frame.  ackedRangesInclusive is a list of
    // [low, high] inclusive packet-number ranges that the peer acknowledged;
    // largestAcked is provided separately and is always treated as acked.
    bool onAckReceived(std::uint64_t largestAcked,
                       std::uint64_t ackDelayMs,
                       const SwVector<SwPair<std::uint64_t, std::uint64_t> >& ackedRangesInclusive,
                       std::uint64_t nowMs,
                       AckResult& out,
                       SwString* error = nullptr) {
        out.newlyAcked.clear();
        out.lost.clear();

        // 1) Determine whether the largest ACK is new and whether this ACK covers at least one
        // ack-eliciting tracked packet. Ordered range seeks avoid scanning packets newer than the
        // ACK, which is critical when the event loop receives a delayed ACK behind a large send queue.
        bool largestNewlyAcked = false;
        bool anyAckElicitingAcked = false;
        std::uint64_t largestSentTime = 0;
        SwMap<std::uint64_t, SentPacket>::iterator largestIt = m_sent.find(largestAcked);
        if (largestIt != m_sent.end()) {
            largestNewlyAcked = true;
            largestSentTime = largestIt->second.sentTimeMs;
            anyAckElicitingAcked = largestIt->second.ackEliciting;
        }
        if (largestNewlyAcked && !anyAckElicitingAcked) {
            for (std::size_t rangeIndex = 0;
                 rangeIndex < ackedRangesInclusive.size() && !anyAckElicitingAcked;
                 ++rangeIndex) {
                std::uint64_t low = ackedRangesInclusive[rangeIndex].first;
                std::uint64_t high = ackedRangesInclusive[rangeIndex].second;
                if (low > high) {
                    const std::uint64_t tmp = low;
                    low = high;
                    high = tmp;
                }
                for (SwMap<std::uint64_t, SentPacket>::iterator it = m_sent.lowerBound(low);
                     it != m_sent.end() && it->first <= high; ++it) {
                    if (it->second.ackEliciting) {
                        anyAckElicitingAcked = true;
                        break;
                    }
                }
            }
        }

        // 2) Generate an RTT sample if the largest acked packet is newly acked
        //    and at least one newly acked packet was ack-eliciting
        //    (RFC 9002 section 5.1).
        if (largestNewlyAcked && anyAckElicitingAcked) {
            if (nowMs < largestSentTime) {
                setError_(error, "QUIC ACK time precedes packet send time");
                return false;
            }
            const double latestRtt = static_cast<double>(nowMs - largestSentTime);
            updateRtt_(latestRtt, static_cast<double>(ackDelayMs));
        }

        // 3) Commit newly acked packets directly from each ordered range. ACK ranges arrive from
        // the wire highest-first, so walking them backwards preserves ascending packet order.
        for (std::size_t rangeIndex = ackedRangesInclusive.size(); rangeIndex-- > 0;) {
            std::uint64_t low = ackedRangesInclusive[rangeIndex].first;
            std::uint64_t high = ackedRangesInclusive[rangeIndex].second;
            if (low > high) {
                const std::uint64_t tmp = low;
                low = high;
                high = tmp;
            }
            SwMap<std::uint64_t, SentPacket>::iterator it = m_sent.lowerBound(low);
            while (it != m_sent.end() && it->first <= high) {
                out.newlyAcked.push_back(it->first);
                it = m_sent.erase(it);
            }
        }
        // The API contract treats largestAcked as acknowledged even if callers omit it from ranges.
        largestIt = m_sent.find(largestAcked);
        if (largestIt != m_sent.end()) {
            out.newlyAcked.push_back(largestAcked);
            m_sent.erase(largestIt);
        }

        // 4) Track the highest largest-acked value seen so far.
        if (!m_haveLargestAcked || largestAcked > m_largestAckedPacket) {
            m_largestAckedPacket = largestAcked;
            m_haveLargestAcked = true;
        }

        // 5) Loss detection (RFC 9002 section 6.1) over remaining packets that
        //    were sent before the largest acknowledged packet.
        const double timeThreshold = lossTimeThresholdMs_();
        SwMap<std::uint64_t, SentPacket>::iterator it = m_sent.begin();
        while (it != m_sent.end() && it->first < largestAcked) {
            const std::uint64_t pn = it->first;
            const bool packetOrderLost = (largestAcked - pn) >= kPacketThreshold();
            const double elapsed = static_cast<double>(nowMs) -
                                   static_cast<double>(it->second.sentTimeMs);
            const bool timeLost = elapsed >= timeThreshold;

            if (packetOrderLost || timeLost) {
                out.lost.push_back(pn);
                it = m_sent.erase(it);
            } else {
                ++it;
            }
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Probe Timeout duration (RFC 9002 section 6.2.1).
    //   PTO = smoothed_rtt + max(4 * rttvar, kGranularity) + max_ack_delay
    // Before the first RTT sample the initial values (smoothed = kInitialRtt,
    // rttvar = kInitialRtt/2) are used.  The double result is rounded to the
    // nearest whole millisecond for this uint64_t API.
    std::uint64_t computePtoMs(std::uint64_t maxAckDelayMs) const {
        const double pto = computePtoMsExact(maxAckDelayMs);
        return static_cast<std::uint64_t>(std::floor(pto + 0.5));
    }

    // Same computation without rounding, for callers/tests that need the exact
    // fractional-millisecond value.
    double computePtoMsExact(std::uint64_t maxAckDelayMs) const {
        double variance = 4.0 * m_rttVarMs;
        if (variance < kGranularityMs()) {
            variance = kGranularityMs();
        }
        return m_smoothedRttMs + variance + static_cast<double>(maxAckDelayMs);
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static bool isAcked_(std::uint64_t pn,
                         std::uint64_t largestAcked,
                         const SwVector<SwPair<std::uint64_t, std::uint64_t> >& ranges) {
        if (pn == largestAcked) {
            return true;
        }
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            std::uint64_t low = ranges[i].first;
            std::uint64_t high = ranges[i].second;
            if (low > high) {
                const std::uint64_t tmp = low;
                low = high;
                high = tmp;
            }
            if (pn >= low && pn <= high) {
                return true;
            }
        }
        return false;
    }

    // RFC 9002 section 5.3 RTT update.
    void updateRtt_(double latestRtt, double ackDelay) {
        m_latestRttMs = latestRtt;

        if (!m_firstSample) {
            m_firstSample = true;
            m_minRttMs = latestRtt;
            m_smoothedRttMs = latestRtt;
            m_rttVarMs = latestRtt / 2.0;
            return;
        }

        if (latestRtt < m_minRttMs) {
            m_minRttMs = latestRtt;
        }

        double adjustedRtt = latestRtt;
        if (m_handshakeConfirmed && (latestRtt - m_minRttMs) >= ackDelay) {
            adjustedRtt = latestRtt - ackDelay;
        }

        const double diff = std::fabs(m_smoothedRttMs - adjustedRtt);
        m_rttVarMs = (3.0 / 4.0) * m_rttVarMs + (1.0 / 4.0) * diff;
        m_smoothedRttMs = (7.0 / 8.0) * m_smoothedRttMs + (1.0 / 8.0) * adjustedRtt;
    }

    // RFC 9002 section 6.1.2 time threshold.
    double lossTimeThresholdMs_() const {
        double base = m_smoothedRttMs;
        if (m_latestRttMs > base) {
            base = m_latestRttMs;
        }
        double threshold = kTimeThresholdFactor() * base;
        if (threshold < kGranularityMs()) {
            threshold = kGranularityMs();
        }
        return threshold;
    }

    bool m_handshakeConfirmed;
    bool m_firstSample;
    double m_latestRttMs;
    double m_minRttMs;
    double m_smoothedRttMs;
    double m_rttVarMs;

    std::uint64_t m_largestAckedPacket;
    bool m_haveLargestAcked;

    SwMap<std::uint64_t, SentPacket> m_sent;
};

#endif
