# CPU capabilities and bounded JSON SIMD

`#include <core/types/SwSpecies.h>` supplies system information without creating
an application, starting a thread, allocating strings, or depending on SwString.
The header and the scanner compile as C++11. The integration tests use C++17,
which is needed by other runtime headers.

```cpp
const auto& cpu = SwSpecies::cpuFeatures();
const char* architecture = SwSpecies::architectureName();
const char* backend = SwSpecies::simdBackendName();
unsigned available = SwSpecies::availableLogicalProcessors();
std::size_t width = SwSpecies::simdBytes();
```

| API | Contract |
| --- | --- |
| `architecture()` / `architectureName()` | Build target: ARM32, ARM64, x86, x86-64 or unknown. Not the host's physical architecture under emulation. |
| `operatingSystem()` | Windows, Linux or unknown. |
| `cpuFeatures()` | Cached capabilities available to the process: NEON, SSE2, AVX2. Windows x86 AVX2 checks CPUID plus OSXSAVE and enabled XMM/YMM state. GCC/Clang use their CPU runtime detection. Linux ARM uses the kernel's HWCAP. |
| `compiledNeon()` / `compiledSse2()` | Intrinsics enabled for this build. A hardware capability alone does not make a kernel callable. |
| `simdBackend()` / `simdBackendName()` | Intersection of compiled implementations and available CPU capabilities: NEON, SSE2, otherwise scalar. |
| `simdBytes()` | Width of the selected implemented backend: 16 bytes, or 1 for scalar. It does not advertise AVX2 kernels; none are supplied yet. |
| `availableLogicalProcessors()` | Current affinity count if available, otherwise hardware concurrency, minimum 1. Windows reports the process mask in its applicable processor group. Does not measure free cores or cgroup CPU-time quotas. Not a worker pool size recommendation. |
| `pointerBits()` / `isLittleEndian()` | Pointer width and byte order of this process. |
| `pageSize()` | Native allocation page size; 0 when unknown. Not a SIMD alignment requirement. |

## Using SIMD safely

Use `SW_SPECIES_COMPILED_NEON` or `SW_SPECIES_COMPILED_SSE2` around intrinsic code,
and check the selected backend before executing it. Hardware detection does not
make a binary compiled globally with `-march=native` or `/arch:AVX2` portable to
older processors. Build a baseline target when portability is required.

The JSON scanner uses the native ARM NEON header, MSVC's `arm64_neon.h` on ARM64,
and SSE2 intrinsics on x86. ARM32 uses a portable NEON reduction instead of the
AArch64-only `vmaxvq_u8`. Scalar code remains available on every target.

## JSON behavior

Applications keep using `SwJsonDocument`, `SwJsonObject`, `SwJsonArray` and
`SwJsonValue`. Both parsing ordinary string bytes and escaping them share
`SwJsonStringScan.h`, an internal bounded scanner. It skips blocks of 16 bytes
until a quote, backslash or byte below 0x20 is encountered. Short inputs and tails
use scalar code. Every load stays inside the supplied size, with no alignment,
NUL-termination or extra readable padding requirement.

Serialization appends spans directly to the destination SwString; parsing
appends spans to the owned decoded string. This avoids intermediate strings and
per-byte appends; it does not make JSON parsing zero-copy. Escape handling,
Unicode surrogate checks, wire formatting and encrypted scalar tags retain
their previous behavior. This does not introduce additional UTF-8 validation.

```cpp
SwString destination("prefix:");
SwJsonValue::appendEscapedString(destination, source);
// Appends escaped contents without surrounding quotes. Self-append is supported.

const SwString& text = value.stringRef();
// Borrows the stored string, or returns an empty string for another value type.
// Valid until value is modified/destroyed. Use toString() for conversions.
```

`SW_JSON_FORCE_SCALAR=1` disables explicit SIMD scanning for differential tests.
Apply it consistently to all translation units of a binary. It does not disable
compiler auto-vectorization or change the hardware information from SwSpecies.

## Linux dispatcher

`SwIoDispatcher.h` selects the Windows or Linux backend without changing the
public watch/poster API. Linux uses EPOLLONESHOT: an fd stays disabled while its
callback is queued or executing. Only the reactor thread rearms it, after
consuming the entire epoll batch. An interest update during a callback changes
the desired mask; completion applies that mask. Callbacks must consume available
IO or change/remove their watch (especially always-writable sockets).

If a reliable poster returns false, it must not have executed or retained the
task. The dispatcher retries the retained notification after 2, 4, 8, 16, then
32 ms, without polling in normal idle operation. A throwing poster follows the
same retry path and likewise must not retain the task. A callback exception is
logged and disables that watch until removal. `updateFd()` acknowledges a valid
interest change; a later native rearm failure is logged and disables the watch.

Always remove a watch before closing/reusing its fd. Removal cancels queued
callbacks; a callback that has already begun may finish. Queued callbacks use a
weak backend reference and never dereference the destroyed dispatcher. Native fds
stay owned by the reactor until it exits. Repeated sequential shutdown is safe;
concurrent destruction/shutdown of the dispatcher itself requires caller
synchronization. The Windows native wait implementation is unchanged.

## Reproducible tests

From the SwStack root:

```sh
cmake -S tests/core_optimizations -B build/core-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-tests --config Release --parallel 8
ctest --test-dir build/core-tests -C Release --output-on-failure
```

Linux requires the OpenSSL development package. Windows uses the native crypto
libraries. On Windows choose the matching CMake architecture (`-A x64` or
`-A ARM64`). Tests keep assertions enabled even in Release.

The CI workflow `.github/workflows/core-optimizations.yml` targets native
Linux/Windows on ARM64/x86-64. It covers SIMD and forced scalar JSON, binary
escape parity, Unicode, protected-page boundaries, dispatcher lifecycle and
backpressure, and the existing JSON/TCP/UDP/local socket regressions. ARM32 and
x86-32 code paths are supplied but are not part of that four-target CI matrix.

References: [Linux EPOLLONESHOT](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html),
[Arm NEON intrinsics](https://arm-software.github.io/acle/neon_intrinsics/),
[MSVC ARM64 intrinsics](https://learn.microsoft.com/en-us/cpp/intrinsics/arm64-intrinsics?view=msvc-170),
[GCC CPU detection](https://gcc.gnu.org/onlinedocs/gcc/x86-Built-in-Functions.html).

For JSON timing, configure with `-DSW_BUILD_OPTIMIZATION_BENCHMARK=ON` and build
`SwJsonBenchmark`. It times escaping and whole-document serialization/parsing on
fixed metadata and long-string corpora. Copy the same benchmark source to the
baseline checkout, use identical Release flags, stop competing builds, and
compare medians and checksums. The benchmark is deliberately not a CTest pass/fail
threshold and is not run by CI.

GCC 11's TSan on the development Linux host does not intercept
`pthread_cond_clockwait`. The dispatcher test uses a system-clock timed condition
wait, which is intercepted, rather than suppressing mutex/race diagnostics. A
minimal standard-library-only reproducer confirmed the tool limitation.
