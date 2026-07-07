#ifndef SWQUICACKTRACKER_H
#define SWQUICACKTRACKER_H

#include "SwString.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstdint>
#include <set>
#include <vector>

class SwQuicAckTracker {
public:
    // Upper bound on tracked packet numbers: the lowest entries are pruned
    // beyond this so a long-lived connection cannot grow the set without
    // bound. 1024 packets is far more history than any ACK frame needs.
    static std::size_t kMaxTrackedPackets() { return 1024; }

    SwQuicAckTracker()
        : m_ackElicitingPending(false) {
    }

    void recordReceivedPacket(std::uint64_t packetNumber, bool ackEliciting = true) {
        m_receivedPackets.insert(packetNumber);
        if (ackEliciting) {
            m_ackElicitingPending = true;
        }
        while (m_receivedPackets.size() > kMaxTrackedPackets()) {
            m_receivedPackets.erase(m_receivedPackets.begin());
        }
    }

    // True when at least one ack-eliciting packet was received since the last
    // ACK frame we sent: only then does an ACK of ours need to go out
    // (RFC 9000 section 13.2.1).
    bool ackElicitingPending() const { return m_ackElicitingPending; }

    // Called when an ACK frame built from this tracker was handed to the send
    // path, so pure-ACK traffic does not trigger endless ACK exchanges.
    void onAckSent() { m_ackElicitingPending = false; }

    // Drop state for packets at or below `packetNumber` (e.g. once the peer
    // confirmed reception of an ACK covering them).
    void pruneUpTo(std::uint64_t packetNumber) {
        m_receivedPackets.erase(m_receivedPackets.begin(),
                                m_receivedPackets.upper_bound(packetNumber));
    }

    void clear() {
        m_receivedPackets.clear();
        m_ackElicitingPending = false;
    }

    bool isEmpty() const {
        return m_receivedPackets.empty();
    }

    std::uint64_t largestReceivedPacket() const {
        if (m_receivedPackets.empty()) {
            return 0;
        }
        return *m_receivedPackets.rbegin();
    }

    bool buildAckFrame(SwQuicFrame& outFrame,
                       std::uint64_t ackDelay = 0,
                       SwString* error = nullptr) const {
        if (m_receivedPackets.empty()) {
            setError_(error, "Cannot build QUIC ACK frame without received packets");
            return false;
        }

        std::vector<LocalAckRange_> ranges;
        std::set<std::uint64_t>::const_reverse_iterator it = m_receivedPackets.rbegin();
        LocalAckRange_ current = {*it, *it};
        ++it;

        for (; it != m_receivedPackets.rend(); ++it) {
            const std::uint64_t packetNumber = *it;
            if (packetNumber + 1 == current.low) {
                current.low = packetNumber;
            } else {
                ranges.push_back(current);
                current.high = packetNumber;
                current.low = packetNumber;
            }
        }
        ranges.push_back(current);

        const std::uint64_t largestAcknowledged = ranges[0].high;
        const std::uint64_t firstAckRange = ranges[0].high - ranges[0].low;
        if (!ackValueFits_(largestAcknowledged, error) ||
            !ackValueFits_(ackDelay, error) ||
            !ackValueFits_(firstAckRange, error)) {
            return false;
        }

        std::vector<SwQuicFrame::AckRange> frameRanges;
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            const std::uint64_t previousLow = ranges[i - 1].low;
            const std::uint64_t currentHigh = ranges[i].high;
            const std::uint64_t currentLow = ranges[i].low;

            if (previousLow <= currentHigh + 1) {
                setError_(error, "Invalid QUIC ACK range ordering");
                return false;
            }

            SwQuicFrame::AckRange range = {
                previousLow - currentHigh - 2,
                currentHigh - currentLow
            };
            if (!ackValueFits_(range.gap, error) ||
                !ackValueFits_(range.rangeLength, error)) {
                return false;
            }
            frameRanges.push_back(range);
        }

        outFrame = SwQuicFrame::ack(largestAcknowledged, ackDelay, firstAckRange, frameRanges);
        if (error) {
            *error = SwString();
        }
        return true;
    }

private:
    struct LocalAckRange_ {
        std::uint64_t high;
        std::uint64_t low;
    };

    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static bool ackValueFits_(std::uint64_t value, SwString* error) {
        if (value > SwQuicVarIntCodec::maxValue()) {
            setError_(error, "QUIC ACK value does not fit variable integer encoding");
            return false;
        }
        return true;
    }

    std::set<std::uint64_t> m_receivedPackets;
    bool m_ackElicitingPending;
};

#endif
