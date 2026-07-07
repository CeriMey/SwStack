#ifndef SWHPACKHUFFMAN_H
#define SWHPACKHUFFMAN_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>
#include <cstdint>
#include <map>

//--------------------------------------------------------------------------------------------------
// SwHpackHuffman
//
// Implements the canonical HPACK Huffman code defined in RFC 7541 Appendix B. The exact same
// static Huffman table is reused by QPACK (RFC 9204) for HTTP/3 header string literals. The table
// carries 257 symbols: one code per possible octet value (0..255) plus the End-Of-String (EOS,
// symbol 256) marker used for padding.
//
// encode() serialises an octet sequence into its Huffman representation, MSB first, and pads the
// final partial byte with the most-significant bits of the EOS symbol (all ones), as required by
// RFC 7541 section 5.2.
//
// decode() walks the bit stream, emitting a symbol as soon as a complete code is matched. It
// enforces the RFC 7541 section 5.2 padding rules: a fully decoded EOS symbol is illegal, any
// padding longer than 7 bits is illegal, and any padding that is not a prefix of EOS (i.e. not all
// ones) is illegal.
//--------------------------------------------------------------------------------------------------

class SwHpackHuffman {
public:
    struct Code {
        std::uint32_t bits;   // right-aligned code value
        std::uint8_t length;  // number of significant bits (1..30)
    };

    static bool encode(const SwByteArray& in, SwByteArray& out, SwString* error = nullptr) {
        out.clear();
        const Code* table = table_();

        std::uint64_t bitBuffer = 0;
        int bitCount = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const std::uint8_t symbol = static_cast<std::uint8_t>(in.constData()[i]);
            const Code& code = table[symbol];
            bitBuffer = (bitBuffer << code.length) | code.bits;
            bitCount += code.length;
            while (bitCount >= 8) {
                bitCount -= 8;
                out.append(static_cast<char>((bitBuffer >> bitCount) & 0xffU));
            }
        }

        if (bitCount > 0) {
            const int pad = 8 - bitCount;
            bitBuffer = (bitBuffer << pad) | ((static_cast<std::uint64_t>(1) << pad) - 1);
            out.append(static_cast<char>(bitBuffer & 0xffU));
        }

        clearError_(error);
        return true;
    }

    static bool decode(const SwByteArray& in, SwByteArray& out, SwString* error = nullptr) {
        out.clear();
        const std::map<std::uint64_t, int>& lookup = decodeLookup_();

        std::uint32_t code = 0;
        int length = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const std::uint8_t byte = static_cast<std::uint8_t>(in.constData()[i]);
            for (int bit = 7; bit >= 0; --bit) {
                code = (code << 1) | static_cast<std::uint32_t>((byte >> bit) & 1U);
                ++length;
                if (length > 30) {
                    setError_(error, "HPACK Huffman code is invalid (no symbol within 30 bits)");
                    return false;
                }
                std::map<std::uint64_t, int>::const_iterator it = lookup.find(key_(length, code));
                if (it != lookup.end()) {
                    if (it->second == 256) {
                        setError_(error, "HPACK Huffman stream contains an explicit EOS symbol");
                        return false;
                    }
                    out.append(static_cast<char>(static_cast<std::uint8_t>(it->second)));
                    code = 0;
                    length = 0;
                }
            }
        }

        if (length > 0) {
            if (length > 7) {
                setError_(error, "HPACK Huffman padding is longer than 7 bits");
                return false;
            }
            const std::uint32_t allOnes = (static_cast<std::uint32_t>(1) << length) - 1;
            if (code != allOnes) {
                setError_(error, "HPACK Huffman padding is not a prefix of EOS");
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

    static std::uint64_t key_(int length, std::uint32_t code) {
        return (static_cast<std::uint64_t>(length) << 32) | static_cast<std::uint64_t>(code);
    }

    static const std::map<std::uint64_t, int>& decodeLookup_() {
        static const std::map<std::uint64_t, int> lookup = buildLookup_();
        return lookup;
    }

    static std::map<std::uint64_t, int> buildLookup_() {
        std::map<std::uint64_t, int> lookup;
        const Code* table = table_();
        for (int symbol = 0; symbol < 257; ++symbol) {
            lookup[key_(table[symbol].length, table[symbol].bits)] = symbol;
        }
        return lookup;
    }

    static const Code* table_() {
        static const Code table[257] = {
            {0x1ff8U, 13},     {0x7fffd8U, 23},   {0xfffffe2U, 28},  {0xfffffe3U, 28},
            {0xfffffe4U, 28},  {0xfffffe5U, 28},  {0xfffffe6U, 28},  {0xfffffe7U, 28},
            {0xfffffe8U, 28},  {0xffffeaU, 24},   {0x3ffffffcU, 30}, {0xfffffe9U, 28},
            {0xfffffeaU, 28},  {0x3ffffffdU, 30}, {0xfffffebU, 28},  {0xfffffecU, 28},
            {0xfffffedU, 28},  {0xfffffeeU, 28},  {0xfffffefU, 28},  {0xffffff0U, 28},
            {0xffffff1U, 28},  {0xffffff2U, 28},  {0x3ffffffeU, 30}, {0xffffff3U, 28},
            {0xffffff4U, 28},  {0xffffff5U, 28},  {0xffffff6U, 28},  {0xffffff7U, 28},
            {0xffffff8U, 28},  {0xffffff9U, 28},  {0xffffffaU, 28},  {0xffffffbU, 28},
            {0x14U, 6},        {0x3f8U, 10},      {0x3f9U, 10},      {0xffaU, 12},
            {0x1ff9U, 13},     {0x15U, 6},        {0xf8U, 8},        {0x7faU, 11},
            {0x3faU, 10},      {0x3fbU, 10},      {0xf9U, 8},        {0x7fbU, 11},
            {0xfaU, 8},        {0x16U, 6},        {0x17U, 6},        {0x18U, 6},
            {0x0U, 5},         {0x1U, 5},         {0x2U, 5},         {0x19U, 6},
            {0x1aU, 6},        {0x1bU, 6},        {0x1cU, 6},        {0x1dU, 6},
            {0x1eU, 6},        {0x1fU, 6},        {0x5cU, 7},        {0xfbU, 8},
            {0x7ffcU, 15},     {0x20U, 6},        {0xffbU, 12},      {0x3fcU, 10},
            {0x1ffaU, 13},     {0x21U, 6},        {0x5dU, 7},        {0x5eU, 7},
            {0x5fU, 7},        {0x60U, 7},        {0x61U, 7},        {0x62U, 7},
            {0x63U, 7},        {0x64U, 7},        {0x65U, 7},        {0x66U, 7},
            {0x67U, 7},        {0x68U, 7},        {0x69U, 7},        {0x6aU, 7},
            {0x6bU, 7},        {0x6cU, 7},        {0x6dU, 7},        {0x6eU, 7},
            {0x6fU, 7},        {0x70U, 7},        {0x71U, 7},        {0x72U, 7},
            {0xfcU, 8},        {0x73U, 7},        {0xfdU, 8},        {0x1ffbU, 13},
            {0x7fff0U, 19},    {0x1ffcU, 13},     {0x3ffcU, 14},     {0x22U, 6},
            {0x7ffdU, 15},     {0x3U, 5},         {0x23U, 6},        {0x4U, 5},
            {0x24U, 6},        {0x5U, 5},         {0x25U, 6},        {0x26U, 6},
            {0x27U, 6},        {0x6U, 5},         {0x74U, 7},        {0x75U, 7},
            {0x28U, 6},        {0x29U, 6},        {0x2aU, 6},        {0x7U, 5},
            {0x2bU, 6},        {0x76U, 7},        {0x2cU, 6},        {0x8U, 5},
            {0x9U, 5},         {0x2dU, 6},        {0x77U, 7},        {0x78U, 7},
            {0x79U, 7},        {0x7aU, 7},        {0x7bU, 7},        {0x7ffeU, 15},
            {0x7fcU, 11},      {0x3ffdU, 14},     {0x1ffdU, 13},     {0xffffffcU, 28},
            {0xfffe6U, 20},    {0x3fffd2U, 22},   {0xfffe7U, 20},    {0xfffe8U, 20},
            {0x3fffd3U, 22},   {0x3fffd4U, 22},   {0x3fffd5U, 22},   {0x7fffd9U, 23},
            {0x3fffd6U, 22},   {0x7fffdaU, 23},   {0x7fffdbU, 23},   {0x7fffdcU, 23},
            {0x7fffddU, 23},   {0x7fffdeU, 23},   {0xffffebU, 24},   {0x7fffdfU, 23},
            {0xffffecU, 24},   {0xffffedU, 24},   {0x3fffd7U, 22},   {0x7fffe0U, 23},
            {0xffffeeU, 24},   {0x7fffe1U, 23},   {0x7fffe2U, 23},   {0x7fffe3U, 23},
            {0x7fffe4U, 23},   {0x1fffdcU, 21},   {0x3fffd8U, 22},   {0x7fffe5U, 23},
            {0x3fffd9U, 22},   {0x7fffe6U, 23},   {0x7fffe7U, 23},   {0xffffefU, 24},
            {0x3fffdaU, 22},   {0x1fffddU, 21},   {0xfffe9U, 20},    {0x3fffdbU, 22},
            {0x3fffdcU, 22},   {0x7fffe8U, 23},   {0x7fffe9U, 23},   {0x1fffdeU, 21},
            {0x7fffeaU, 23},   {0x3fffddU, 22},   {0x3fffdeU, 22},   {0xfffff0U, 24},
            {0x1fffdfU, 21},   {0x3fffdfU, 22},   {0x7fffebU, 23},   {0x7fffecU, 23},
            {0x1fffe0U, 21},   {0x1fffe1U, 21},   {0x3fffe0U, 22},   {0x1fffe2U, 21},
            {0x7fffedU, 23},   {0x3fffe1U, 22},   {0x7fffeeU, 23},   {0x7fffefU, 23},
            {0xfffeaU, 20},    {0x3fffe2U, 22},   {0x3fffe3U, 22},   {0x3fffe4U, 22},
            {0x7ffff0U, 23},   {0x3fffe5U, 22},   {0x3fffe6U, 22},   {0x7ffff1U, 23},
            {0x3ffffe0U, 26},  {0x3ffffe1U, 26},  {0xfffebU, 20},    {0x7fff1U, 19},
            {0x3fffe7U, 22},   {0x7ffff2U, 23},   {0x3fffe8U, 22},   {0x1ffffecU, 25},
            {0x3ffffe2U, 26},  {0x3ffffe3U, 26},  {0x3ffffe4U, 26},  {0x7ffffdeU, 27},
            {0x7ffffdfU, 27},  {0x3ffffe5U, 26},  {0xfffff1U, 24},   {0x1ffffedU, 25},
            {0x7fff2U, 19},    {0x1fffe3U, 21},   {0x3ffffe6U, 26},  {0x7ffffe0U, 27},
            {0x7ffffe1U, 27},  {0x3ffffe7U, 26},  {0x7ffffe2U, 27},  {0xfffff2U, 24},
            {0x1fffe4U, 21},   {0x1fffe5U, 21},   {0x3ffffe8U, 26},  {0x3ffffe9U, 26},
            {0xffffffdU, 28},  {0x7ffffe3U, 27},  {0x7ffffe4U, 27},  {0x7ffffe5U, 27},
            {0xfffecU, 20},    {0xfffff3U, 24},   {0xfffedU, 20},    {0x1fffe6U, 21},
            {0x3fffe9U, 22},   {0x1fffe7U, 21},   {0x1fffe8U, 21},   {0x7ffff3U, 23},
            {0x3fffeaU, 22},   {0x3fffebU, 22},   {0x1ffffeeU, 25},  {0x1ffffefU, 25},
            {0xfffff4U, 24},   {0xfffff5U, 24},   {0x3ffffeaU, 26},  {0x7ffff4U, 23},
            {0x3ffffebU, 26},  {0x7ffffe6U, 27},  {0x3ffffecU, 26},  {0x3ffffedU, 26},
            {0x7ffffe7U, 27},  {0x7ffffe8U, 27},  {0x7ffffe9U, 27},  {0x7ffffeaU, 27},
            {0x7ffffebU, 27},  {0xffffffeU, 28},  {0x7ffffecU, 27},  {0x7ffffedU, 27},
            {0x7ffffeeU, 27},  {0x7ffffefU, 27},  {0x7fffff0U, 27},  {0x3ffffeeU, 26},
            {0x3fffffffU, 30}
        };
        return table;
    }
};

#endif
