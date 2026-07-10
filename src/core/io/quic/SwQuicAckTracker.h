#ifndef SWQUICACKTRACKER_H
#define SWQUICACKTRACKER_H

#include "SwVector.h"
#include "SwString.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstdint>
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
        if (ackEliciting) {
            m_ackElicitingPending = true;
        }

        if (m_ranges.empty()) {
            m_ranges.push_back(LocalAckRange_{packetNumber, packetNumber});
            return;
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(m_ranges.size()); ++i) {
            LocalAckRange_& current = m_ranges[i];
            if (packetNumber >= current.low && packetNumber <= current.high) {
                return; // doublon
            }

            if (packetNumber > current.high) {
                if (packetNumber == current.high + 1) {
                    current.high = packetNumber;
                    if (i > 0 && m_ranges[i - 1].low == current.high + 1) {
                        m_ranges[i - 1].low = current.low;
                        m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                } else {
                    m_ranges.insert(m_ranges.begin() + static_cast<std::ptrdiff_t>(i),
                                    LocalAckRange_{packetNumber, packetNumber});
                }
                trimWindow_();
                return;
            }

            if (packetNumber + 1 == current.low) {
                current.low = packetNumber;
                if (i + 1 < static_cast<std::size_t>(m_ranges.size()) &&
                    current.low == m_ranges[i + 1].high + 1) {
                    current.low = m_ranges[i + 1].low;
                    m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(i + 1));
                }
                trimWindow_();
                return;
            }
        }

        m_ranges.push_back(LocalAckRange_{packetNumber, packetNumber});
        trimWindow_();
    }

    // Consultation sans mutation pour jeter un replay AVANT de retraiter ses
    // frames applicatives. recordReceivedPacket() déduplique les plages d'ACK,
    // mais ne peut pas, à lui seul, empêcher la redélivraison d'un DATAGRAM.
    bool containsReceivedPacket(std::uint64_t packetNumber) const {
        for (std::size_t i = 0; i < static_cast<std::size_t>(m_ranges.size()); ++i) {
            const LocalAckRange_& current = m_ranges[i];
            // Les plages sont triées de la plus récente à la plus ancienne.
            // Le paquet nominal suivant est donc rejeté du lookup dès la
            // première plage, au lieu de parcourir les trous historiques.
            if (packetNumber > current.high) {
                return false;
            }
            if (packetNumber >= current.low) {
                return true;
            }
        }
        return false;
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
        for (std::size_t i = 0; i < static_cast<std::size_t>(m_ranges.size()); ++i) {
            if (m_ranges[i].high <= packetNumber) {
                m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(i), m_ranges.end());
                return;
            }
            if (m_ranges[i].low <= packetNumber) {
                m_ranges[i].low = packetNumber + 1;
                m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(i + 1), m_ranges.end());
                return;
            }
        }
    }

    void clear() {
        m_ranges.clear();
        m_ackElicitingPending = false;
    }

    bool isEmpty() const {
        return m_ranges.empty();
    }

    std::uint64_t largestReceivedPacket() const {
        if (m_ranges.empty()) {
            return 0;
        }
        return m_ranges[0].high;
    }

    bool buildAckFrame(SwQuicFrame& outFrame,
                       std::uint64_t ackDelay = 0,
                       SwString* error = nullptr) const {
        if (m_ranges.empty()) {
            setError_(error, "Cannot build QUIC ACK frame without received packets");
            return false;
        }

        const std::uint64_t largestAcknowledged = m_ranges[0].high;
        const std::uint64_t firstAckRange = m_ranges[0].high - m_ranges[0].low;
        if (!ackValueFits_(largestAcknowledged, error) ||
            !ackValueFits_(ackDelay, error) ||
            !ackValueFits_(firstAckRange, error)) {
            return false;
        }

        SwVector<SwQuicFrame::AckRange> frameRanges;
        for (std::size_t i = 1; i < static_cast<std::size_t>(m_ranges.size()); ++i) {
            const std::uint64_t previousLow = m_ranges[i - 1].low;
            const std::uint64_t currentHigh = m_ranges[i].high;
            const std::uint64_t currentLow = m_ranges[i].low;

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

    void trimWindow_() {
        if (m_ranges.empty()) return;
        const std::uint64_t largest = m_ranges[0].high;
        const std::uint64_t floor = largest >= kMaxTrackedPackets()
            ? largest - static_cast<std::uint64_t>(kMaxTrackedPackets() - 1)
            : 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(m_ranges.size()); ++i) {
            if (m_ranges[i].high < floor) {
                m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(i), m_ranges.end());
                return;
            }
            if (m_ranges[i].low < floor) {
                m_ranges[i].low = floor;
                m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(i + 1), m_ranges.end());
                return;
            }
        }
    }

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

    // Plages inclusives triées de la plus grande vers la plus petite. Le cas
    // normal (numéros croissants) ne touche que m_ranges[0].
    SwVector<LocalAckRange_> m_ranges;
    bool m_ackElicitingPending;
};

#endif
