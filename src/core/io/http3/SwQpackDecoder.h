#ifndef SWQPACKDECODER_H
#define SWQPACKDECODER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "http3/SwHpackHuffman.h"
#include "http3/SwQpackStaticTable.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

//--------------------------------------------------------------------------------------------------
// SwQpackDecoder
//
// Decodes a single QPACK encoded field section (RFC 9204 section 4.5) produced against the static
// table only. The section prefix must announce Required Insert Count = 0 and Base = 0; any other
// value refers to the dynamic table, which this decoder does not implement and rejects.
//
// It understands the three static field-line representations emitted by SwQpackEncoder and always
// supports Huffman-coded string literals (RFC 7541 Appendix B).
//
// Note: QPACK integers use the HPACK N-bit prefix integer representation (RFC 7541 section 5.1),
// not the QUIC variable-length integer, so SwQuicVarIntCodec is intentionally not used here.
//--------------------------------------------------------------------------------------------------

class SwQpackDecoder {
public:
    static bool decodeFieldSection(const SwByteArray& in,
                                   std::vector<std::pair<SwByteArray, SwByteArray> >& out,
                                   SwString* error = nullptr) {
        return decodeFieldSection(in, out, 0, 0, error);
    }

    static bool decodeFieldSection(
        const SwByteArray& in,
        std::vector<std::pair<SwByteArray, SwByteArray> >& out,
        std::size_t maxFieldCount,
        std::size_t maxDecodedBytes,
        SwString* error = nullptr) {
        out.clear();
        std::size_t offset = 0;
        std::size_t decodedBytes = 0;
        const std::size_t decodedLimit = maxDecodedBytes == 0
            ? (std::numeric_limits<std::size_t>::max)()
            : maxDecodedBytes;

        // Encoded Field Section Prefix.
        std::uint64_t requiredInsertCount = 0;
        if (!decodeInteger_(in, offset, 8, requiredInsertCount, error)) {
            return false;
        }
        if (requiredInsertCount != 0) {
            setError_(error, "QPACK dynamic table is not supported (Required Insert Count != 0)");
            return false;
        }
        std::uint64_t deltaBase = 0;
        if (!decodeInteger_(in, offset, 7, deltaBase, error)) {
            return false;
        }
        if (deltaBase != 0) {
            setError_(error, "QPACK dynamic table is not supported (Base != 0)");
            return false;
        }

        while (offset < in.size()) {
            if (maxFieldCount > 0 && out.size() >= maxFieldCount) {
                setError_(error, "QPACK field count exceeds the configured limit");
                return false;
            }
            const std::uint8_t first = static_cast<std::uint8_t>(in.constData()[offset]);

            if (first & 0x80U) {
                // Indexed Field Line.
                if (!(first & 0x40U)) {
                    setError_(error, "QPACK Indexed Field Line references the dynamic table");
                    return false;
                }
                std::uint64_t index = 0;
                if (!decodeInteger_(in, offset, 6, index, error)) {
                    return false;
                }
                SwByteArray name;
                SwByteArray value;
                if (!SwQpackStaticTable::nameValueAt(static_cast<std::size_t>(index), name, value)) {
                    setError_(error, "QPACK Indexed Field Line has an out-of-range static index");
                    return false;
                }
                if (!appendField_(out, std::move(name), std::move(value),
                                  decodedLimit, decodedBytes, error)) {
                    return false;
                }
            } else if (first & 0x40U) {
                // Literal Field Line With Name Reference.
                if (!(first & 0x10U)) {
                    setError_(error, "QPACK Literal Field Line name reference is dynamic");
                    return false;
                }
                std::uint64_t index = 0;
                if (!decodeInteger_(in, offset, 4, index, error)) {
                    return false;
                }
                SwByteArray name;
                SwByteArray staticValue;
                if (!SwQpackStaticTable::nameValueAt(static_cast<std::size_t>(index), name,
                                                     staticValue)) {
                    setError_(error, "QPACK name reference has an out-of-range static index");
                    return false;
                }
                SwByteArray value;
                const std::size_t remaining = remainingBytes_(decodedLimit, decodedBytes,
                                                               name.size());
                if (!decodeString_(in, offset, 7, 0x80, remaining, value, error)) {
                    return false;
                }
                if (!appendField_(out, std::move(name), std::move(value),
                                  decodedLimit, decodedBytes, error)) {
                    return false;
                }
            } else if (first & 0x20U) {
                // Literal Field Line With Literal Name.
                SwByteArray name;
                const std::size_t nameLimit = remainingBytes_(decodedLimit, decodedBytes, 0);
                if (!decodeString_(in, offset, 3, 0x08, nameLimit, name, error)) {
                    return false;
                }
                SwByteArray value;
                const std::size_t valueLimit = remainingBytes_(decodedLimit, decodedBytes,
                                                                name.size());
                if (!decodeString_(in, offset, 7, 0x80, valueLimit, value, error)) {
                    return false;
                }
                if (!appendField_(out, std::move(name), std::move(value),
                                  decodedLimit, decodedBytes, error)) {
                    return false;
                }
            } else {
                setError_(error, "QPACK post-base field line representations are not supported");
                return false;
            }
        }

        clearError_(error);
        return true;
    }

private:
    static std::size_t remainingBytes_(std::size_t limit,
                                       std::size_t used,
                                       std::size_t reserved) {
        if (used > limit || reserved > limit - used) {
            return 0;
        }
        return limit - used - reserved;
    }

    static bool appendField_(
        std::vector<std::pair<SwByteArray, SwByteArray> >& out,
        SwByteArray name,
        SwByteArray value,
        std::size_t decodedLimit,
        std::size_t& decodedBytes,
        SwString* error) {
        const std::size_t nameBytes = name.size();
        const std::size_t valueBytes = value.size();
        // SETTINGS_MAX_FIELD_SECTION_SIZE uses the HTTP field-section metric:
        // name + value + 32 bytes per field line (RFC 9114 / QPACK).
        static const std::size_t kFieldLineOverhead = 32;
        if (decodedBytes > decodedLimit ||
            kFieldLineOverhead > decodedLimit - decodedBytes) {
            setError_(error, "QPACK decoded fields exceed the configured byte limit");
            return false;
        }
        const std::size_t afterOverhead = decodedBytes + kFieldLineOverhead;
        if (nameBytes > decodedLimit - afterOverhead) {
            setError_(error, "QPACK decoded fields exceed the configured byte limit");
            return false;
        }
        const std::size_t afterName = afterOverhead + nameBytes;
        if (valueBytes > decodedLimit - afterName) {
            setError_(error, "QPACK decoded fields exceed the configured byte limit");
            return false;
        }
        decodedBytes = afterName + valueBytes;
        out.push_back(std::make_pair(std::move(name), std::move(value)));
        return true;
    }

    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void clearError_(SwString* error) {
        if (error) {
            *error = SwString();
        }
    }

    // HPACK N-bit prefix integer (RFC 7541 section 5.1).
    static bool decodeInteger_(const SwByteArray& in,
                               std::size_t& offset,
                               int prefixBits,
                               std::uint64_t& value,
                               SwString* error) {
        if (offset >= in.size()) {
            setError_(error, "QPACK integer is truncated");
            return false;
        }

        const std::uint8_t maxPrefix = static_cast<std::uint8_t>((1U << prefixBits) - 1U);
        const std::uint8_t firstByte = static_cast<std::uint8_t>(in.constData()[offset]);
        ++offset;

        value = static_cast<std::uint64_t>(firstByte & maxPrefix);
        if (value < maxPrefix) {
            return true;
        }

        int shift = 0;
        while (true) {
            if (offset >= in.size()) {
                setError_(error, "QPACK integer continuation is truncated");
                return false;
            }
            const std::uint8_t byte = static_cast<std::uint8_t>(in.constData()[offset]);
            ++offset;
            value += static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            shift += 7;
            if (!(byte & 0x80U)) {
                break;
            }
            if (shift > 63) {
                setError_(error, "QPACK integer is too large");
                return false;
            }
        }
        return true;
    }

    // Decodes a string literal whose length integer uses 'prefixBits' and whose 'hBit' (in the
    // length byte) flags Huffman coding.
    static bool decodeString_(const SwByteArray& in,
                              std::size_t& offset,
                              int prefixBits,
                              std::uint8_t hBit,
                              std::size_t maxOutputBytes,
                              SwByteArray& out,
                              SwString* error) {
        if (offset >= in.size()) {
            setError_(error, "QPACK string literal is truncated");
            return false;
        }

        const bool huffman = (static_cast<std::uint8_t>(in.constData()[offset]) & hBit) != 0;
        std::uint64_t length = 0;
        if (!decodeInteger_(in, offset, prefixBits, length, error)) {
            return false;
        }

        if (length > static_cast<std::uint64_t>(in.size() - offset)) {
            setError_(error, "QPACK string literal length exceeds available data");
            return false;
        }
        if ((!huffman && length > maxOutputBytes) ||
            (huffman && maxOutputBytes <=
                            (std::numeric_limits<std::size_t>::max)() / 4 &&
             length > static_cast<std::uint64_t>(maxOutputBytes * 4))) {
            setError_(error, "QPACK string literal exceeds the configured decoded limit");
            return false;
        }
        if (length > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
            setError_(error, "QPACK string literal is too large");
            return false;
        }

        SwByteArray raw = in.mid(static_cast<int>(offset), static_cast<int>(length));
        offset += static_cast<std::size_t>(length);

        if (huffman) {
            return SwHpackHuffman::decode(raw, out, maxOutputBytes, error);
        }
        out = raw;
        return true;
    }
};

#endif
