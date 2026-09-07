#include "SwJsonDocument.h"
#include "SwSpecies.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

// Independent byte-wise oracle: JSON escapes use uppercase hex, UTF-8 stays intact.
static SwString referenceEscape(const SwString& value) {
    SwString result;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 32) {
                const char hex[] = "0123456789ABCDEF";
                result += "\\u00";
                result.append(hex[c >> 4]);
                result.append(hex[c & 15]);
            } else result.append(static_cast<char>(c));
        }
    }
    return result;
}
static void check(const SwString& input) {
    const auto expected = referenceEscape(input);
    assert(SwJsonValue::escapeString(input) == expected);
    SwString appended("prefix");
    SwJsonValue::appendEscapedString(appended, input);
    assert(appended == SwString("prefix") + expected);
    SwString alias(input);
    SwJsonValue::appendEscapedString(alias, alias);
    assert(alias == input + expected);
    SwJsonArray array;
    array.append(input);
    const auto json = SwJsonDocument(array).toJson(SwJsonDocument::JsonFormat::Compact);
    assert(json == SwString("[\"") + expected + "\"]");
    SwString error;
    const auto decoded = SwJsonDocument::fromJson(json, error);
    assert(error.isEmpty());
    assert(decoded.toJson(SwJsonDocument::JsonFormat::Compact) == json);
    assert(swJsonDetail::ordinarySpan(input.constData(), input.size()) ==
           swJsonDetail::ordinarySpanScalar(input.constData(), input.size()));
}
static void guardedBounds() {
    const size_t page = SwSpecies::pageSize();
    assert(page >= 512);
#if defined(_WIN32)
    char* pages = static_cast<char*>(::VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(pages);
    DWORD oldProtection = 0;
    assert(::VirtualProtect(pages + page, page, PAGE_NOACCESS, &oldProtection));
#else
    char* pages = static_cast<char*>(::mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(pages != MAP_FAILED);
    assert(::mprotect(pages + page, page, PROT_NONE) == 0);
#endif
    // Zero length points into an inaccessible page. No load is allowed there.
    for (size_t size = 0; size <= 256; ++size) {
        char* start = pages + page - size;
        std::memset(start, 'a', size);
        assert(swJsonDetail::ordinarySpan(start, size) == size);
        for (size_t pos = 0; pos < size; ++pos) {
            const unsigned char specials[] = {0, 31, '"', '\\'};
            for (unsigned char c : specials) {
                start[pos] = static_cast<char>(c);
                assert(swJsonDetail::ordinarySpan(start, size) == pos);
            }
            start[pos] = 'a';
        }
    }
#if defined(_WIN32)
    assert(::VirtualFree(pages, 0, MEM_RELEASE));
#else
    assert(::munmap(pages, page * 2) == 0);
#endif
}
int main() {
    assert(SwSpecies::pointerBits() == sizeof(void*) * 8);
    assert(SwSpecies::availableLogicalProcessors() >= 1);
    const auto backend = SwSpecies::simdBackend();
    assert(backend != SwSpecies::SimdBackend::Neon || (SwSpecies::compiledNeon() && SwSpecies::cpuFeatures().neon));
    assert(backend != SwSpecies::SimdBackend::Sse2 || (SwSpecies::compiledSse2() && SwSpecies::cpuFeatures().sse2));
    assert(SwJsonValue(42).stringRef().isEmpty());
    std::mt19937 random(937);
    for (size_t size = 0; size < 512; ++size) {
        for (int sample = 0; sample < 16; ++sample) {
            SwString input;
            for (size_t i = 0; i < size; ++i) input.append(static_cast<char>(random() & 255));
            check(input);
        }
        check(SwString(size, 'a'));
    }
    check(SwString("caméra 🌍\\\"\n"));
    const char* invalid[] = {"[\"\\uD800\"]", "[\"\\uDC00\"]", "[\"\\uZZZZ\"]", "[\"\\q\"]", "[\"unterminated]", "[\"a\nb\"]"};
    for (auto text : invalid) {
        SwString error;
        SwJsonDocument::fromJson(text, error);
        assert(!error.isEmpty());
    }
    SwString error;
    auto unicode = SwJsonDocument::fromJson("[\"\\uD83C\\uDF0D\"]", error);
    assert(error.isEmpty());
    assert(unicode.toJson(SwJsonDocument::JsonFormat::Compact) == "[\"🌍\"]");
    guardedBounds();
    std::cout << "CPU backend=" << static_cast<int>(backend) << "; randomized JSON, Unicode and protected-page bounds passed\n";
}
