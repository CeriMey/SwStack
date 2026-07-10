#ifndef SWQUICSESSIONTICKET_H
#define SWQUICSESSIONTICKET_H

#include "SwByteArray.h"
#include "SwMap.h"
#include "SwString.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>

// Client-side resumption state kept after a NewSessionTicket (RFC 8446 4.6.1):
// the opaque ticket to echo as the PSK identity, the derived resumption PSK,
// obfuscation/lifetime metadata, the server's max_early_data_size (0 = no
// 0-RTT), and the server transport parameters the client must honour when it
// sends 0-RTT data (RFC 9001 4.6 requires reusing the remembered values).
struct SwQuicSessionTicket {
    SwByteArray ticket;                 // PSK identity on the wire
    SwByteArray resumptionPsk;          // 32-byte derived PSK
    std::uint32_t ticketAgeAdd;         // obfuscation addend
    std::uint32_t ticketLifetimeS;      // seconds
    std::uint32_t maxEarlyDataSize;     // wire value: 0xffffffff enables 0-RTT (RFC 9001 4.6.1)
    std::uint64_t serverInitialMaxData; // remembered flow-control bound for 0-RTT
    SwByteArray serverTransportParams;  // remembered for 0-RTT
    bool valid;

    SwQuicSessionTicket()
        : ticketAgeAdd(0), ticketLifetimeS(0), maxEarlyDataSize(0),
          serverInitialMaxData(0), valid(false) {}

    // In QUIC the early_data extension enables 0-RTT only when its
    // max_early_data_size is exactly 0xffffffff (RFC 9001 4.6.1); the actual
    // 0-RTT volume is governed by initial_max_data, not this field.
    bool allowsEarlyData() const { return valid && maxEarlyDataSize == 0xffffffffu; }
};

// Server-side ticket store. A real server seals the resumption state into an
// opaque, self-authenticated ticket (STEK-encrypted); here the ticket is a
// random handle indexing an in-memory entry, which is a valid single-node
// implementation and all a loopback/self-test needs.
class SwQuicTicketStore {
public:
    typedef std::function<std::uint64_t()> MonotonicNowProvider;

    struct Entry {
        SwByteArray resumptionPsk;
        std::uint32_t maxEarlyDataSize;
        SwByteArray serverTransportParams; // params to re-apply on resumption
        SwString serverIdentity;            // normalized SNI/origin binding
        std::uint64_t issuedAtMs;
        std::uint64_t expiresAtMs;          // exclusive monotonic deadline
        bool found;

        Entry()
            : maxEarlyDataSize(0), issuedAtMs(0), expiresAtMs(0),
              found(false) {}
    };

    // Tests and deterministic embedders can inject a monotonic millisecond
    // source. Passing an empty provider restores steady_clock.
    void setMonotonicNowProvider(const MonotonicNowProvider& provider) {
        m_nowProvider = provider;
    }

    void store(const SwByteArray& ticket,
               const SwByteArray& resumptionPsk,
               std::uint32_t maxEarlyDataSize,
               const SwByteArray& serverTransportParams,
               const SwString& serverIdentity = SwString(),
               std::uint32_t ticketLifetimeS = 7U * 24U * 60U * 60U) {
        const std::uint64_t now = nowMs_();
        purgeExpired_(now);
        // RFC 8446 uses a zero lifetime to invalidate a ticket immediately.
        if (ticketLifetimeS == 0) {
            m_entries.erase(ticket);
            return;
        }
        if (m_maxEntries > 0 && m_entries.find(ticket) == m_entries.end()) {
            while (m_entries.size() >= m_maxEntries && !m_entries.empty()) {
                m_entries.erase(m_entries.begin());
            }
        }
        Entry entry;
        entry.resumptionPsk = resumptionPsk;
        entry.maxEarlyDataSize = maxEarlyDataSize;
        entry.serverTransportParams = serverTransportParams;
        entry.serverIdentity = serverIdentity.trimmed().toLower();
        entry.issuedAtMs = now;
        const std::uint64_t lifetimeMs =
            static_cast<std::uint64_t>(ticketLifetimeS) * 1000U;
        const std::uint64_t maximum =
            (std::numeric_limits<std::uint64_t>::max)();
        entry.expiresAtMs = lifetimeMs > maximum - now
            ? maximum
            : now + lifetimeMs;
        entry.found = true;
        m_entries[ticket] = entry;
    }

    Entry lookup(const SwByteArray& ticket) const {
        const std::uint64_t now = nowMs_();
        SwMap<SwByteArray, Entry>::const_iterator it = m_entries.find(ticket);
        if (it == m_entries.end()) {
            return Entry();
        }
        if (isExpired_(it->second, now)) {
            m_entries.erase(ticket);
            return Entry();
        }
        return it->second;
    }

    Entry lookup(const SwByteArray& ticket, const SwString& serverIdentity) const {
        Entry entry = lookup(ticket);
        if (!entry.found ||
            entry.serverIdentity != serverIdentity.trimmed().toLower()) {
            return Entry();
        }
        return entry;
    }

    // Single-use tickets are the safest default against 0-RTT replay
    // (RFC 8446 8.1): a resumed ticket is consumed so it cannot be replayed.
    void consume(const SwByteArray& ticket) {
        m_entries.erase(ticket);
    }

    std::size_t size() const {
        purgeExpired_(nowMs_());
        return m_entries.size();
    }
    void setMaxEntries(std::size_t maximum) {
        purgeExpired_(nowMs_());
        m_maxEntries = maximum;
        if (m_maxEntries > 0) {
            while (m_entries.size() > m_maxEntries) m_entries.erase(m_entries.begin());
        }
    }
    std::size_t maxEntries() const { return m_maxEntries; }
    void clear() { m_entries.clear(); }

private:
    static std::uint64_t defaultNowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::uint64_t nowMs_() const {
        return m_nowProvider ? m_nowProvider() : defaultNowMs_();
    }

    static bool isExpired_(const Entry& entry, std::uint64_t now) {
        return entry.expiresAtMs == 0 || now >= entry.expiresAtMs;
    }

    void purgeExpired_(std::uint64_t now) const {
        for (SwMap<SwByteArray, Entry>::iterator it = m_entries.begin();
             it != m_entries.end();) {
            if (isExpired_(it->second, now)) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    mutable SwMap<SwByteArray, Entry> m_entries;
    std::size_t m_maxEntries = 4096;
    MonotonicNowProvider m_nowProvider;
};

#endif
