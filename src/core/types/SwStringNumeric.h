#pragma once
#include <cstddef>
#include <limits>
#include <type_traits>

namespace sw { namespace detail {
inline bool numericSpace(char value) {
    return value == ' ' || (value >= '\t' && value <= '\r');
}

// Parse the complete byte range without allocating, throwing or logging.
// ASCII whitespace at the ends and a leading '+' are accepted. Negative
// values, embedded NULs, invalid digits and overflow fail with ok=false.
template<class UInt>
UInt stringToUnsigned(const char* data, std::size_t size, bool* ok, int base) {
    static_assert(std::is_unsigned<UInt>::value, "Unsigned integer required");
    if (ok) *ok = false;
    if (base != 0 && (base < 2 || base > 36)) return 0;
    std::size_t first = 0, last = size;
    while (first < last && numericSpace(data[first])) ++first;
    while (first < last && numericSpace(data[last - 1])) --last;
    if (first == last) return 0;
    if (data[first] == '+') ++first;
    if (first == last || data[first] == '-') return 0;
    if (last - first >= 2 && data[first] == '0') {
        const char next = data[first + 1];
        if ((base == 0 || base == 16) && (next == 'x' || next == 'X')) {
            base = 16; first += 2;
        } else if ((base == 0 || base == 2) && (next == 'b' || next == 'B')) {
            base = 2; first += 2;
        } else if (base == 0) base = 8;
    }
    if (base == 0) base = 10;
    if (first == last) return 0;
    UInt value = 0;
    for (; first < last; ++first) {
        const char character = data[first];
        const unsigned digit = character >= '0' && character <= '9' ? character - '0'
            : character >= 'a' && character <= 'z' ? character - 'a' + 10
            : character >= 'A' && character <= 'Z' ? character - 'A' + 10 : 36;
        if (digit >= static_cast<unsigned>(base) ||
            value > (std::numeric_limits<UInt>::max() - digit) / static_cast<unsigned>(base)) return 0;
        value = value * static_cast<unsigned>(base) + digit;
    }
    if (ok) *ok = true;
    return value;
}
}}
