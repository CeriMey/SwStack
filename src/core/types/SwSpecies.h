#pragma once

/** Reusable, allocation-free CPU/OS capabilities. Independent of SwString/runtime. */
#include <cstddef>
#include <cstdint>
#include <thread>

#if defined(_M_ARM64) || defined(__aarch64__)
#define SW_SPECIES_ARM64 1
#else
#define SW_SPECIES_ARM64 0
#endif
#if defined(_M_ARM) || defined(__arm__)
#define SW_SPECIES_ARM32 1
#else
#define SW_SPECIES_ARM32 0
#endif
#if defined(_M_X64) || defined(__x86_64__)
#define SW_SPECIES_X64 1
#else
#define SW_SPECIES_X64 0
#endif
#if defined(_M_IX86) || defined(__i386__)
#define SW_SPECIES_X86 1
#else
#define SW_SPECIES_X86 0
#endif

// These describe what this translation unit can compile, not hardware support.
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM)
#define SW_SPECIES_COMPILED_NEON 1
#else
#define SW_SPECIES_COMPILED_NEON 0
#endif
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define SW_SPECIES_COMPILED_SSE2 1
#else
#define SW_SPECIES_COMPILED_SSE2 0
#endif

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#if SW_SPECIES_X64 || SW_SPECIES_X86
#include <intrin.h>
#endif
#elif defined(__linux__)
#include <unistd.h>
#include <sched.h>
#if SW_SPECIES_ARM64 || SW_SPECIES_ARM32
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#endif

class SwSpecies {
public:
    enum class Architecture { Unknown, Arm32, Arm64, X86, X64 };
    enum class OperatingSystem { Unknown, Windows, Linux };
    enum class SimdBackend { Scalar, Neon, Sse2 };
    struct CpuFeatures {
        bool neon{false};
        bool sse2{false};
        // AVX2 also requires OS support for saving XMM/YMM registers.
        bool avx2{false};
    };

    static constexpr Architecture architecture() {
        return SW_SPECIES_ARM64 ? Architecture::Arm64 : SW_SPECIES_ARM32 ? Architecture::Arm32 :
               SW_SPECIES_X64 ? Architecture::X64 : SW_SPECIES_X86 ? Architecture::X86 : Architecture::Unknown;
    }
    static constexpr OperatingSystem operatingSystem() {
#if defined(_WIN32)
        return OperatingSystem::Windows;
#elif defined(__linux__)
        return OperatingSystem::Linux;
#else
        return OperatingSystem::Unknown;
#endif
    }
    static constexpr unsigned pointerBits() { return sizeof(void*) * 8; }
    static constexpr const char* architectureName() {
        return SW_SPECIES_ARM64 ? "arm64" : SW_SPECIES_ARM32 ? "arm" :
               SW_SPECIES_X64 ? "x86_64" : SW_SPECIES_X86 ? "x86" : "unknown";
    }
    static bool isLittleEndian() {
        const std::uint16_t one = 1;
        return *reinterpret_cast<const unsigned char*>(&one) == 1;
    }
    static const CpuFeatures& cpuFeatures() {
        static const CpuFeatures features = detectCpu_();
        return features;
    }
    static constexpr bool compiledNeon() { return SW_SPECIES_COMPILED_NEON != 0; }
    static constexpr bool compiledSse2() { return SW_SPECIES_COMPILED_SSE2 != 0; }
    static SimdBackend simdBackend() {
        const auto& features = cpuFeatures();
        if (compiledNeon() && features.neon) return SimdBackend::Neon;
        if (compiledSse2() && features.sse2) return SimdBackend::Sse2;
        return SimdBackend::Scalar;
    }
    static std::size_t simdBytes() { return simdBackend() == SimdBackend::Scalar ? 1 : 16; }
    static const char* simdBackendName() {
        const auto backend = simdBackend();
        return backend == SimdBackend::Neon ? "neon" : backend == SimdBackend::Sse2 ? "sse2" : "scalar";
    }

    /** Logical CPUs allowed by the calling thread/process affinity; at least one.
     * Not a worker-pool recommendation, nor a cgroup CPU-time quota. Not cached:
     * affinity can change during the lifetime of the process.
     */
    static unsigned availableLogicalProcessors() {
#if defined(__linux__)
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        if (::sched_getaffinity(0, sizeof(cpus), &cpus) == 0) {
            const int count = CPU_COUNT(&cpus);
            if (count > 0) return static_cast<unsigned>(count);
        }
#elif defined(_WIN32)
        DWORD_PTR processMask = 0, systemMask = 0;
        if (::GetProcessAffinityMask(::GetCurrentProcess(), &processMask, &systemMask) && processMask) {
            unsigned count = 0;
            for (; processMask; processMask &= processMask - 1) ++count;
            return count;
        }
#endif
        const unsigned count = std::thread::hardware_concurrency();
        return count ? count : 1;
    }
    static std::size_t pageSize() {
#if defined(_WIN32)
        SYSTEM_INFO info{};
        ::GetSystemInfo(&info);
        return info.dwPageSize;
#elif defined(__linux__)
        const long size = ::sysconf(_SC_PAGESIZE);
        return size > 0 ? static_cast<std::size_t>(size) : 0;
#else
        return 0; // Unknown, never guess an allocation/alignment contract.
#endif
    }

private:
    static CpuFeatures detectCpu_() {
        CpuFeatures result;
#if defined(__linux__) && SW_SPECIES_ARM64
        result.neon = (::getauxval(AT_HWCAP) & HWCAP_ASIMD) != 0;
#elif defined(__linux__) && SW_SPECIES_ARM32
        result.neon = (::getauxval(AT_HWCAP) & HWCAP_NEON) != 0;
#elif defined(_WIN32) && (SW_SPECIES_ARM64 || SW_SPECIES_ARM32)
        result.neon = ::IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) != 0;
#elif (SW_SPECIES_X64 || SW_SPECIES_X86) && defined(_MSC_VER)
        int regs[4] = {};
        __cpuid(regs, 0);
        const int maxLeaf = regs[0];
        if (maxLeaf >= 1) {
            __cpuidex(regs, 1, 0);
            result.sse2 = (regs[3] & (1 << 26)) != 0;
            const bool osAvx = (regs[2] & (1 << 27)) && (regs[2] & (1 << 28));
            if (osAvx && (_xgetbv(0) & 6) == 6 && maxLeaf >= 7) {
                __cpuidex(regs, 7, 0);
                result.avx2 = (regs[1] & (1 << 5)) != 0;
            }
        }
#elif (SW_SPECIES_X64 || SW_SPECIES_X86) && (defined(__GNUC__) || defined(__clang__))
        __builtin_cpu_init();
        result.sse2 = __builtin_cpu_supports("sse2");
        result.avx2 = __builtin_cpu_supports("avx2");
#endif
        return result;
    }
};
