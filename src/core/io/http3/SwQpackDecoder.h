#ifndef SWQPACKDECODER_H
#define SWQPACKDECODER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "http3/SwHpackHuffman.h"
#include "http3/SwQpackStaticTable.h"

#include <cstddef>
#include <cstdint>
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
        out.clear();
        std::size_t offset = 0;

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
                out.push_back(std::make_pair(name, value));
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
                if (!decodeString_(in, offset, 7, 0x80, value, error)) {
                    return false;
                }
                out.push_back(std::make_pair(name, value));
            } else if (first & 0x20U) {
                // Literal Field Line With Literal Name.
                SwByteArray name;
                if (!decodeString_(in, offset, 3, 0x08, name, error)) {
                    return false;
                }
                SwByteArray value;
                if (!decodeString_(in, offset, 7, 0x80, value, error)) {
                    return false;
                }
                out.push_back(std::make_pair(name, value));
            } else {
                setError_(error, "QPACK post-base field line representations are not supported");
                return false;
            }
        }

        clearError_(error);
        return true;
    }

private:
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

        SwByteArray raw = in.mid(static_cast<int>(offset), static_cast<int>(length));
        offset += static_cast<std::size_t>(length);

        if (huffman) {
            return SwHpackHuffman::decode(raw, out, error);
        }
        out = raw;
        return true;
    }
};

#endif
