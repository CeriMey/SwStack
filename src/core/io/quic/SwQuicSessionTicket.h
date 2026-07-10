#ifndef SWQUICSESSIONTICKET_H
#define SWQUICSESSIONTICKET_H

#include "SwByteArray.h"
#include "SwMap.h"

#include <cstdint>

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
    struct Entry {
        SwByteArray resumptionPsk;
        std::uint32_t maxEarlyDataSize;
        SwByteArray serverTransportParams; // params to re-apply on resumption
        bool found;

        Entry() : maxEarlyDataSize(0), found(false) {}
    };

    void store(const SwByteArray& ticket,
               const SwByteArray& resumptionPsk,
               std::uint32_t maxEarlyDataSize,
               const SwByteArray& serverTransportParams) {
        Entry entry;
        entry.resumptionPsk = resumptionPsk;
        entry.maxEarlyDataSize = maxEarlyDataSize;
        entry.serverTransportParams = serverTransportParams;
        entry.found = true;
        m_entries[ticket] = entry;
    }

    Entry lookup(const SwByteArray& ticket) const {
        SwMap<SwByteArray, Entry>::const_iterator it = m_entries.find(ticket);
        if (it == m_entries.end()) {
            return Entry();
        }
        return it->second;
    }

    // Single-use tickets are the safest default against 0-RTT replay
    // (RFC 8446 8.1): a resumed ticket is consumed so it cannot be replayed.
    void consume(const SwByteArray& ticket) {
        m_entries.erase(ticket);
    }

    std::size_t size() const { return m_entries.size(); }
    void clear() { m_entries.clear(); }

private:
    SwMap<SwByteArray, Entry> m_entries;
};

#endif
