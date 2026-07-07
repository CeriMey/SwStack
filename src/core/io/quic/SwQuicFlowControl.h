#ifndef SWQUICFLOWCONTROL_H
#define SWQUICFLOWCONTROL_H

#include "SwString.h"

#include <cstdint>

// Flow control for QUIC transport (RFC 9000, section 4).
//
// Two independent flow-control levels are modelled:
//   * SwQuicStreamFlowControl     -- per-stream limits (MAX_STREAM_DATA)
//   * SwQuicConnectionFlowControl -- aggregate connection limit (MAX_DATA)
//
// Each level has a receive side (we advertise a limit, the peer must respect
// it) and a send side (the peer advertises a limit, we must respect it).
// Header-only, C++11, fallible operations return bool with a trailing
// SwString* error out-parameter, matching the surrounding quic/ code.

class SwQuicStreamFlowControl {
public:
    explicit SwQuicStreamFlowControl(std::uint64_t initialWindow = 262144)
        : m_initialWindow(initialWindow),
          m_maxDataLimit(initialWindow),
          m_highestReceivedOffset(0),
          m_bytesConsumed(0),
          m_peerMaxData(0) {
    }

    // ---- receive side (limits we advertise to the peer) -----------------

    std::uint64_t initialWindow() const { return m_initialWindow; }
    std::uint64_t maxDataLimit() const { return m_maxDataLimit; }
    std::uint64_t highestReceivedOffset() const { return m_highestReceivedOffset; }
    std::uint64_t bytesConsumed() const { return m_bytesConsumed; }

    // Called when a STREAM frame is received. Enforces that the peer did not
    // send data beyond the advertised limit (RFC 9000 4.1: FLOW_CONTROL_ERROR).
    bool onDataReceived(std::uint64_t offset,
                        std::uint64_t length,
                        SwString* error = nullptr) {
        const std::uint64_t end = offset + length;
        if (end < offset) {
            setError_(error, "FLOW_CONTROL_ERROR");
            return false;
        }
        if (end > m_maxDataLimit) {
            setError_(error, "FLOW_CONTROL_ERROR");
            return false;
        }
        if (end > m_highestReceivedOffset) {
            m_highestReceivedOffset = end;
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Application read data off the stream; frees up receive-window space.
    void consume(std::uint64_t bytes) {
        m_bytesConsumed += bytes;
    }

    // Autotune: signal a MAX_STREAM_DATA update once the application has
    // consumed past half of the current window.
    bool shouldSendMaxStreamData() const {
        const std::uint64_t threshold =
            (m_maxDataLimit - m_initialWindow) + (m_initialWindow / 2);
        return m_bytesConsumed > threshold;
    }

    // The new absolute limit to advertise.
    std::uint64_t nextMaxStreamData() const {
        return m_bytesConsumed + m_initialWindow;
    }

    // Adopt nextMaxStreamData() as the advertised limit and return it.
    std::uint64_t applyMaxStreamData() {
        m_maxDataLimit = nextMaxStreamData();
        return m_maxDataLimit;
    }

    // ---- send side (limits the peer advertises to us) -------------------

    void setPeerMaxData(std::uint64_t limit) {
        m_peerMaxData = limit;
    }

    std::uint64_t peerMaxData() const { return m_peerMaxData; }

    // How many more bytes we may send given what we already sent.
    std::uint64_t sendableBytes(std::uint64_t alreadySent) const {
        if (alreadySent >= m_peerMaxData) {
            return 0;
        }
        return m_peerMaxData - alreadySent;
    }

    // True when we cannot send wantToSend more bytes without exceeding the
    // peer-advertised limit (STREAM_DATA_BLOCKED condition).
    bool isBlocked(std::uint64_t wantToSend, std::uint64_t alreadySent) const {
        return alreadySent + wantToSend > m_peerMaxData;
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    std::uint64_t m_initialWindow;
    std::uint64_t m_maxDataLimit;
    std::uint64_t m_highestReceivedOffset;
    std::uint64_t m_bytesConsumed;
    std::uint64_t m_peerMaxData;
};

class SwQuicConnectionFlowControl {
public:
    explicit SwQuicConnectionFlowControl(std::uint64_t initialWindow = 262144)
        : m_initialWindow(initialWindow),
          m_maxDataLimit(initialWindow),
          m_bytesReceived(0),
          m_bytesConsumed(0),
          m_peerMaxData(0) {
    }

    // ---- receive side (aggregate limit we advertise) --------------------

    std::uint64_t initialWindow() const { return m_initialWindow; }
    std::uint64_t maxDataLimit() const { return m_maxDataLimit; }
    std::uint64_t bytesReceived() const { return m_bytesReceived; }
    std::uint64_t bytesConsumed() const { return m_bytesConsumed; }

    // Called with the number of newly received stream bytes across all
    // streams. Enforces the connection-level MAX_DATA limit.
    bool onDataReceived(std::uint64_t length, SwString* error = nullptr) {
        const std::uint64_t total = m_bytesReceived + length;
        if (total < m_bytesReceived) {
            setError_(error, "FLOW_CONTROL_ERROR");
            return false;
        }
        if (total > m_maxDataLimit) {
            setError_(error, "FLOW_CONTROL_ERROR");
            return false;
        }
        m_bytesReceived = total;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    void consume(std::uint64_t bytes) {
        m_bytesConsumed += bytes;
    }

    bool shouldSendMaxData() const {
        const std::uint64_t threshold =
            (m_maxDataLimit - m_initialWindow) + (m_initialWindow / 2);
        return m_bytesConsumed > threshold;
    }

    std::uint64_t nextMaxData() const {
        return m_bytesConsumed + m_initialWindow;
    }

    std::uint64_t applyMaxData() {
        m_maxDataLimit = nextMaxData();
        return m_maxDataLimit;
    }

    // ---- send side (aggregate limit the peer advertises) ----------------

    void setPeerMaxData(std::uint64_t limit) {
        m_peerMaxData = limit;
    }

    std::uint64_t peerMaxData() const { return m_peerMaxData; }

    std::uint64_t sendableBytes(std::uint64_t alreadySent) const {
        if (alreadySent >= m_peerMaxData) {
            return 0;
        }
        return m_peerMaxData - alreadySent;
    }

    bool isBlocked(std::uint64_t wantToSend, std::uint64_t alreadySent) const {
        return alreadySent + wantToSend > m_peerMaxData;
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    std::uint64_t m_initialWindow;
    std::uint64_t m_maxDataLimit;
    std::uint64_t m_bytesReceived;
    std::uint64_t m_bytesConsumed;
    std::uint64_t m_peerMaxData;
};

#endif
