#ifndef SWQUICVARINTCODEC_H
#define SWQUICVARINTCODEC_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstdint>
#include <cstddef>

class SwQuicVarIntCodec {
public:
    static std::uint64_t maxValue() {
        return 0x3fffffffffffffffULL;
    }

    static bool encode(std::uint64_t value,
                       SwByteArray& outBytes,
                       SwString* error = nullptr) {
        if (value > maxValue()) {
            setError_(error, "QUIC variable integer is too large");
            return false;
        }

        if (value <= 0x3fULL) {
            appendByte_(outBytes, static_cast<std::uint8_t>(value));
        } else if (value <= 0x3fffULL) {
            appendBigEndian_(outBytes, value | 0x4000ULL, 2);
        } else if (value <= 0x3fffffffULL) {
            appendBigEndian_(outBytes, value | 0x80000000ULL, 4);
        } else {
            appendBigEndian_(outBytes, value | 0xc000000000000000ULL, 8);
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool decode(const SwByteArray& bytes,
                       std::size_t& offset,
                       std::uint64_t& outValue,
                       SwString* error = nullptr) {
        if (offset >= bytes.size()) {
            setError_(error, "QUIC variable integer is missing");
            return false;
        }

        const char* data = bytes.constData();
        const std::uint8_t first = static_cast<std::uint8_t>(data[offset]);
        const std::uint8_t prefix = static_cast<std::uint8_t>(first >> 6);
        const std::size_t byteCount = static_cast<std::size_t>(1) << prefix;

        if (bytes.size() - offset < byteCount) {
            setError_(error, "QUIC variable integer is truncated");
            return false;
        }

        std::uint64_t encoded = 0;
        for (std::size_t i = 0; i < byteCount; ++i) {
            encoded = (encoded << 8) | static_cast<std::uint8_t>(data[offset + i]);
        }

        if (byteCount == 1) {
            outValue = encoded & 0x3fULL;
        } else if (byteCount == 2) {
            outValue = encoded & 0x3fffULL;
        } else if (byteCount == 4) {
            outValue = encoded & 0x3fffffffULL;
        } else {
            outValue = encoded & maxValue();
        }

        offset += byteCount;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    static std::size_t encodedSize(std::uint64_t value, bool* ok = nullptr) {
        if (ok) {
            *ok = value <= maxValue();
        }

        if (value <= 0x3fULL) {
            return 1;
        }
        if (value <= 0x3fffULL) {
            return 2;
        }
        if (value <= 0x3fffffffULL) {
            return 4;
        }
        if (value <= maxValue()) {
            return 8;
        }
        return 0;
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void appendByte_(SwByteArray& outBytes, std::uint8_t value) {
        outBytes.append(static_cast<char>(value));
    }

    static void appendBigEndian_(SwByteArray& outBytes,
                                 std::uint64_t value,
                                 std::size_t byteCount) {
        for (std::size_t i = 0; i < byteCount; ++i) {
            const std::size_t shift = (byteCount - 1 - i) * 8;
            appendByte_(outBytes, static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }
};

#endif
