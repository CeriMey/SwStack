#ifndef SWQUICCONGESTIONCONTROL_H
#define SWQUICCONGESTIONCONTROL_H

#include <cstdint>
#include <cstddef>

// RFC 9002 (QUIC Loss Detection and Congestion Control) section 7 NewReno.
//
// This class implements the NewReno congestion controller described in
// RFC 9002. It tracks the congestion window, the slow start threshold and the
// number of bytes currently in flight, and reacts to acknowledgements, loss
// detection and congestion (ECN or loss) events.
//
// Header-only, C++11, no external dependencies.
class SwQuicCongestionControl {
public:
    SwQuicCongestionControl()
        : m_maxDatagramSize(1200),
          m_congestionWindow(0),
          m_bytesInFlight(0),
          m_ssthresh(UINT64_MAX),
          m_recoveryStartTimeMs(0),
          m_started(false) {
        m_congestionWindow = initialWindow_(m_maxDatagramSize);
    }

    // Number of bytes carried by the largest datagram the path allows.
    std::uint64_t maxDatagramSize() const { return m_maxDatagramSize; }

    // Current congestion window in bytes (RFC 9002 congestion_window).
    std::uint64_t congestionWindow() const { return m_congestionWindow; }

    // Bytes sent but not yet acknowledged or declared lost.
    std::uint64_t bytesInFlight() const { return m_bytesInFlight; }

    // Slow start threshold (RFC 9002 ssthresh).
    std::uint64_t ssthresh() const { return m_ssthresh; }

    // Start time of the current recovery period (RFC 9002 congestion_recovery_start_time).
    std::uint64_t recoveryStartTimeMs() const { return m_recoveryStartTimeMs; }

    // True once a congestion recovery period has been entered at least once.
    bool started() const { return m_started; }

    // RFC 9002 7.5 On Sending a Packet: add the sent bytes to bytes_in_flight.
    void onPacketSent(std::size_t sentBytes) {
        m_bytesInFlight += static_cast<std::uint64_t>(sentBytes);
    }

    // RFC 9002 7.3.1/7.3.2 On Receiving an Acknowledgment for an ack-eliciting
    // packet: remove the acked bytes from flight and grow the window.
    //
    // In slow start (cwnd < ssthresh) the window grows by the number of acked
    // bytes. In congestion avoidance the window grows by
    // maxDatagramSize * ackedBytes / cwnd. Acks for packets sent before the
    // current recovery period do not change the window.
    void onPacketAcked(std::size_t ackedBytes, std::uint64_t sentTimeMs) {
        const std::uint64_t acked = static_cast<std::uint64_t>(ackedBytes);

        if (m_bytesInFlight >= acked) {
            m_bytesInFlight -= acked;
        } else {
            m_bytesInFlight = 0;
        }

        // Do not increase the congestion window during recovery: a packet sent
        // before the recovery period started must not inflate the window.
        if (m_started && !isTimeAfterRecovery_(sentTimeMs)) {
            return;
        }

        if (m_congestionWindow < m_ssthresh) {
            // Slow start.
            m_congestionWindow += acked;
        } else {
            // Congestion avoidance (additive increase).
            if (m_congestionWindow > 0) {
                m_congestionWindow += (m_maxDatagramSize * acked) / m_congestionWindow;
            }
        }
    }

    // RFC 9002 7.6 On Congestion Event: halve the window, but only if the event
    // was triggered by a packet sent after the current recovery period began.
    void onCongestionEvent(std::uint64_t sentTimeMs, std::uint64_t nowMs) {
        // No reaction if already in a recovery period started at or after the
        // time this packet was sent.
        if (m_started && !isTimeAfterRecovery_(sentTimeMs)) {
            return;
        }

        m_started = true;
        m_recoveryStartTimeMs = nowMs;

        std::uint64_t halved = m_congestionWindow / 2;
        const std::uint64_t floor = 2 * m_maxDatagramSize;
        if (halved < floor) {
            halved = floor;
        }
        m_ssthresh = halved;
        m_congestionWindow = halved;
    }

    // RFC 9002 7.6 On packets declared lost: remove the lost bytes from flight
    // and enter a congestion recovery period.
    void onPacketsLost(std::size_t lostBytes,
                       std::uint64_t largestLostSentTimeMs,
                       std::uint64_t nowMs) {
        const std::uint64_t lost = static_cast<std::uint64_t>(lostBytes);
        if (m_bytesInFlight >= lost) {
            m_bytesInFlight -= lost;
        } else {
            m_bytesInFlight = 0;
        }

        onCongestionEvent(largestLostSentTimeMs, nowMs);
    }

    // RFC 9002 7.6.2 Persistent Congestion: collapse the window to the minimum.
    void onPersistentCongestion() {
        m_congestionWindow = 2 * m_maxDatagramSize;
    }

    // Whether the controller currently permits sending `bytes` more bytes.
    bool canSend(std::size_t bytes) const {
        return m_bytesInFlight + static_cast<std::uint64_t>(bytes) <= m_congestionWindow;
    }

private:
    static std::uint64_t initialWindow_(std::uint64_t maxDatagramSize) {
        // RFC 9002 7.2 Initial and Minimum Congestion Window:
        // min(10 * max_datagram_size, max(2 * max_datagram_size, 14720)).
        const std::uint64_t tenPackets = 10 * maxDatagramSize;
        const std::uint64_t twoPackets = 2 * maxDatagramSize;
        std::uint64_t lowerBound = twoPackets;
        if (lowerBound < 14720) {
            lowerBound = 14720;
        }
        return tenPackets < lowerBound ? tenPackets : lowerBound;
    }

    // A packet is "after" the current recovery period when it was sent strictly
    // after the recovery period started.
    bool isTimeAfterRecovery_(std::uint64_t sentTimeMs) const {
        return sentTimeMs > m_recoveryStartTimeMs;
    }

    std::uint64_t m_maxDatagramSize;
    std::uint64_t m_congestionWindow;
    std::uint64_t m_bytesInFlight;
    std::uint64_t m_ssthresh;
    std::uint64_t m_recoveryStartTimeMs;
    bool m_started;
};

#endif
