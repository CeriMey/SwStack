#ifndef SWQUICLIMITS_H
#define SWQUICLIMITS_H

#include <cstddef>
#include <cstdint>

// Shared QUIC wire limits. The RFC 9000 minimum Initial/path-probe datagram remains 1200 bytes;
// it is deliberately distinct from the path's maximum UDP payload. RFC 9221's transport
// parameter counts the complete DATAGRAM frame (type + length + application bytes).
struct SwQuicLimits {
    static constexpr std::size_t minimumInitialUdpPayloadBytes() { return 1200; }
    static constexpr std::size_t maximumUdpPayloadBytes() { return 1450; }
    static constexpr std::uint64_t maximumApplicationDatagramBytes() { return 1350; }
    // Includes protocol envelopes above an application's 1350-byte payload (for example an
    // eight-byte flow varint) plus RFC 9221's type/length fields.
    static constexpr std::uint64_t maximumDatagramFrameBytes() { return 1400; }
};

#endif // SWQUICLIMITS_H
