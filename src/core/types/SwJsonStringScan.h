#pragma once

// Bounded byte scanner shared by JSON parsing and escaping. No padding contract:
// all vector loads remain within [data, data + size), including unaligned input.
#include "SwSpecies.h"
#if SW_SPECIES_COMPILED_NEON
#if defined(_MSC_VER) && !defined(__clang__) && SW_SPECIES_ARM64
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif
#if SW_SPECIES_COMPILED_SSE2
#include <emmintrin.h>
#endif

namespace swJsonDetail {
inline bool needsEscape(unsigned char c) { return c < 0x20 || c == '"' || c == '\\'; }

inline std::size_t ordinarySpanScalar(const char* data, std::size_t size) {
    std::size_t i = 0;
    while (i < size && !needsEscape(static_cast<unsigned char>(data[i]))) ++i;
    return i;
}

inline std::size_t ordinarySpan(const char* data, std::size_t size) {
    std::size_t i = 0;
#if !defined(SW_JSON_FORCE_SCALAR)
    // Avoid CPU dispatch and vector setup for short keys / frequent escapes.
    if (size >= 32) {
        static const SwSpecies::SimdBackend backend = SwSpecies::simdBackend();
#if SW_SPECIES_COMPILED_NEON
        if (backend == SwSpecies::SimdBackend::Neon) {
            for (; size - i >= 16; i += 16) {
                const uint8x16_t bytes = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
                const uint8x16_t special = vorrq_u8(vcltq_u8(bytes, vdupq_n_u8(32)),
                    vorrq_u8(vceqq_u8(bytes, vdupq_n_u8('"')), vceqq_u8(bytes, vdupq_n_u8('\\'))));
#if SW_SPECIES_ARM64
                if (vmaxvq_u8(special)) break;
#else
                // ARMv7 NEON has no vmaxvq reduction.
                uint8x8_t reduced = vmax_u8(vget_low_u8(special), vget_high_u8(special));
                reduced = vpmax_u8(reduced, reduced);
                reduced = vpmax_u8(reduced, reduced);
                reduced = vpmax_u8(reduced, reduced);
                if (vget_lane_u8(reduced, 0)) break;
#endif
            }
        }
#endif
#if SW_SPECIES_COMPILED_SSE2
        if (backend == SwSpecies::SimdBackend::Sse2) {
            for (; size - i >= 16; i += 16) {
                const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
                const __m128i control = _mm_cmpeq_epi8(_mm_subs_epu8(bytes, _mm_set1_epi8(31)), _mm_setzero_si128());
                const __m128i special = _mm_or_si128(control, _mm_or_si128(
                    _mm_cmpeq_epi8(bytes, _mm_set1_epi8('"')), _mm_cmpeq_epi8(bytes, _mm_set1_epi8('\\'))));
                if (_mm_movemask_epi8(special)) break;
            }
        }
#endif
        (void)backend;
    }
#endif
    return i + ordinarySpanScalar(data + i, size - i);
}
} // namespace swJsonDetail
