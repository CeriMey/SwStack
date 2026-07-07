#ifndef SWQPACKENCODER_H
#define SWQPACKENCODER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "http3/SwHpackHuffman.h"
#include "http3/SwQpackStaticTable.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

//--------------------------------------------------------------------------------------------------
// SwQpackEncoder
//
// Encodes a single QPACK encoded field section (RFC 9204 section 4.5) that references only the
// static table. The dynamic table is never used, so the section prefix always carries Required
// Insert Count = 0 and Base = 0, i.e. the two leading bytes 0x00 0x00.
//
// Each header field is emitted as the most compact representation available:
//   - Indexed Field Line (static)                 when name+value are in the static table.
//   - Literal Field Line With Name Reference       when only the name is in the static table.
//   - Literal Field Line With Literal Name         otherwise.
// String literals are Huffman-encoded (RFC 7541 Appendix B) whenever that is strictly shorter.
//
// Note: QPACK integers use the HPACK N-bit prefix integer representation (RFC 7541 section 5.1),
// which is a distinct wire format from the QUIC variable-length integer, so SwQuicVarIntCodec does
// not apply here and is intentionally not used.
//--------------------------------------------------------------------------------------------------

class SwQpackEncoder {
public:
    static bool encodeFieldSection(const std::vector<std::pair<SwByteArray, SwByteArray> >& headers,
                                   SwByteArray& out,
                                   SwString* error = nullptr) {
        out.clear();

        // Encoded Field Section Prefix: Required Insert Count = 0, then Base (S=0, DeltaBase=0).
        out.append(static_cast<char>(0x00));
        out.append(static_cast<char>(0x00));

        for (std::size_t i = 0; i < headers.size(); ++i) {
            const SwByteArray& name = headers[i].first;
            const SwByteArray& value = headers[i].second;

            std::size_t index = 0;
            if (SwQpackStaticTable::findExact(name, value, index)) {
                // Indexed Field Line, static table: pattern bits '1' '1', 6-bit prefix index.
                encodeInteger_(out, index, 6, 0xC0);
                continue;
            }

            if (SwQpackStaticTable::findName(name, index)) {
                // Literal Field Line With Name Reference: bits '0' '1' N=0 T=1, 4-bit prefix index.
                encodeInteger_(out, index, 4, 0x50);
                if (!encodeString_(out, value, 7, 0x00, 0x80, error)) {
                    return false;
                }
                continue;
            }

            // Literal Field Line With Literal Name: bits '0' '0' '1' N=0 H, 3-bit prefix name len.
            if (!encodeString_(out, name, 3, 0x20, 0x08, error)) {
                return false;
            }
            if (!encodeString_(out, value, 7, 0x00, 0x80, error)) {
                return false;
            }
        }

        clearError_(error);
        return true;
    }

private:
    static void clearError_(SwString* error) {
        if (error) {
            *error = SwString();
        }
    }

    // HPACK N-bit prefix integer (RFC 7541 section 5.1). The non-prefix high bits carried in the
    // first byte are supplied via 'pattern'; its low 'prefixBits' bits must be zero.
    static void encodeInteger_(SwByteArray& out,
                               std::uint64_t value,
                               int prefixBits,
                               std::uint8_t pattern) {
        const std::uint8_t maxPrefix = static_cast<std::uint8_t>((1U << prefixBits) - 1U);
        if (value < maxPrefix) {
            out.append(static_cast<char>(pattern | static_cast<std::uint8_t>(value)));
            return;
        }

        out.append(static_cast<char>(pattern | maxPrefix));
        value -= maxPrefix;
        while (value >= 128) {
            out.append(static_cast<char>((value & 0x7fU) | 0x80U));
            value >>= 7;
        }
        out.append(static_cast<char>(value & 0x7fU));
    }

    // Encodes a string literal: a length integer whose 'hBit' (top of the prefix byte) flags
    // Huffman coding, followed by the raw or Huffman-coded octets. Huffman is chosen only when it
    // is strictly shorter.
    static bool encodeString_(SwByteArray& out,
                              const SwByteArray& str,
                              int prefixBits,
                              std::uint8_t basePattern,
                              std::uint8_t hBit,
                              SwString* error) {
        SwByteArray huffman;
        if (!SwHpackHuffman::encode(str, huffman, error)) {
            return false;
        }

        if (huffman.size() < str.size()) {
            encodeInteger_(out, huffman.size(), prefixBits,
                           static_cast<std::uint8_t>(basePattern | hBit));
            out.append(huffman);
        } else {
            encodeInteger_(out, str.size(), prefixBits, basePattern);
            out.append(str);
        }
        return true;
    }
};

#endif
