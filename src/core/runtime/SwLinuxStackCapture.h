#pragma once

// Linux signal-side collection only. Symbol lookup belongs to the monitor.
// A collector is owned by its thread and retained by every attached session.
#if defined(__linux__) && !defined(__ANDROID__)
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <thread>
#include <ucontext.h>

namespace swRuntimeStackCapture {
static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2,
              "Signal stack collection requires lock-free integer/pointer atomics");

struct StackBounds {
    constexpr StackBounds(std::uintptr_t first = 0, std::uintptr_t last = 0) noexcept
        : low(first), high(last) {}
    std::uintptr_t low{0};
    std::uintptr_t high{0};
    __attribute__((always_inline)) bool contains(std::uintptr_t address, std::size_t bytes) const noexcept {
        return address >= low && address < high && bytes <= high - address;
    }
};

// Published records are immutable for the entire lifetime of their stack.
// A single pointer store avoids mixing bounds if interrupted during a switch.
__attribute__((always_inline)) inline const StackBounds*& fiberBoundsTls() noexcept {
    // Avoid __tls_get_addr and its possible allocation even for an unsolicited
    // signal on an unbound thread. These two trivial slots have process lifetime.
    static thread_local const StackBounds* value
        __attribute__((tls_model("initial-exec"))) = nullptr;
    return value;
}
inline void setCurrentFiberStack(const StackBounds* bounds) noexcept {
    __atomic_store_n(&fiberBoundsTls(), bounds, __ATOMIC_RELEASE);
}

class Collector;
__attribute__((always_inline)) inline Collector*& collectorTls() noexcept {
    static thread_local Collector* value
        __attribute__((tls_model("initial-exec"))) = nullptr;
    return value;
}

class Collector {
public:
    static constexpr std::size_t capacity = 128;
    Collector() : thread_(pthread_self()) {
        pthread_attr_t attributes;
        if (pthread_getattr_np(thread_, &attributes) == 0) {
            void* address = nullptr;
            std::size_t bytes = 0;
            if (pthread_attr_getstack(&attributes, &address, &bytes) == 0) {
                const auto low = reinterpret_cast<std::uintptr_t>(address);
                if (bytes <= UINTPTR_MAX - low) mainStack_ = {low, low + bytes};
            }
            pthread_attr_destroy(&attributes);
        }
    }
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    // Called only outside the signal. Contention drops a sample; it never spins.
    // A timed-out handler owns its mailbox until Ready, so a later request cannot
    // overwrite its bounds/limit or read a half-written sample.
    std::size_t capture(int signal, std::uintptr_t* output, std::size_t limit,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(20)) {
        if (!output || !limit) return 0;
        std::unique_lock<std::mutex> consumer(consumer_, std::try_to_lock);
        if (!consumer.owns_lock() || retired_) return 0;
        unsigned state = __atomic_load_n(&state_, __ATOMIC_ACQUIRE);
        if (state == Ready) { __atomic_store_n(&state_, Idle, __ATOMIC_RELEASE); state = Idle; }
        if (state != Idle) return 0;
        limit_ = limit < capacity ? limit : capacity;
        __atomic_store_n(&state_, Requested, __ATOMIC_RELEASE);
        if (pthread_kill(thread_, signal) == 0) {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            do {
                if (__atomic_load_n(&state_, __ATOMIC_ACQUIRE) == Ready) {
                    const auto count = count_;
                    for (std::size_t i = 0; i < count; ++i) output[i] = frames_[i];
                    __atomic_store_n(&state_, Idle, __ATOMIC_RELEASE);
                    return count;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } while (std::chrono::steady_clock::now() < deadline);
        }
        unsigned expected = Requested;
        __atomic_compare_exchange_n(&state_, &expected, Idle, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        return 0;
    }

    // Outside the handler, at thread exit. This prevents pthread_kill from
    // receiving a reclaimed/reused pthread_t while sessions still retain us.
    void retire() {
        std::lock_guard<std::mutex> consumer(consumer_);
        retired_ = true;
    }

    // No heap, locks, TLS containers, dynamic loading or libc stack unwinder.
    // Only the signal handler on thread_ can enter this method.
    __attribute__((always_inline)) void collect(const ucontext_t* context) noexcept {
        unsigned expected = Requested;
        if (!__atomic_compare_exchange_n(&state_, &expected, Capturing, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
        count_ = 0;
        if (context) {
            std::uintptr_t pc = 0, sp = 0, fp = 0;
#if defined(__aarch64__)
            pc = context->uc_mcontext.pc;
            sp = context->uc_mcontext.sp;
            fp = context->uc_mcontext.regs[29];
            constexpr std::uintptr_t alignment = 16;
#elif defined(__x86_64__)
            pc = context->uc_mcontext.gregs[REG_RIP];
            sp = context->uc_mcontext.gregs[REG_RSP];
            fp = context->uc_mcontext.gregs[REG_RBP];
            constexpr std::uintptr_t alignment = sizeof(std::uintptr_t);
#else
            constexpr std::uintptr_t alignment = sizeof(std::uintptr_t);
#endif
            if (pc) frames_[count_++] = pc;
            const StackBounds* bounds = __atomic_load_n(&fiberBoundsTls(), __ATOMIC_ACQUIRE);
            // During swapcontext the published fiber may already be the next
            // one. Never trust its record unless the interrupted SP belongs to it.
            if (!bounds || !bounds->contains(sp, 1)) bounds = &mainStack_;
            if (bounds->contains(sp, 1)) {
                while (count_ < limit_ && fp >= sp && (fp & (alignment - 1)) == 0 &&
                       bounds->contains(fp, 2 * sizeof(std::uintptr_t))) {
                    const auto* record = reinterpret_cast<const std::uintptr_t*>(fp);
                    const auto previous = record[0];
                    const auto address = record[1];
                    if (!address) break;
#if defined(__aarch64__)
                    if ((address & 3) != 0) break;
#endif
                    frames_[count_++] = address;
                    if (previous <= fp) break;
                    fp = previous;
                }
            }
        }
        __atomic_store_n(&state_, Ready, __ATOMIC_RELEASE);
    }

private:
    enum : unsigned { Idle, Requested, Capturing, Ready };
    pthread_t thread_;
    StackBounds mainStack_;
    // Compiler atomics also avoid out-of-line std::atomic wrappers/PLT binding
    // in Debug builds. The signal path must remain independent of the loader.
    unsigned state_{Idle};
    std::mutex consumer_; // Monitor/thread teardown only; never accessed in collect().
    bool retired_{false};
    std::size_t limit_{capacity};
    std::size_t count_{0};
    std::uintptr_t frames_[capacity]{};
};

inline std::shared_ptr<Collector> bindCurrentThread() {
    struct Owner {
        std::shared_ptr<Collector> collector{std::make_shared<Collector>()};
        Owner() {
            // Force all TLS used in the handler to exist before publishing it.
            (void)__atomic_load_n(&fiberBoundsTls(), __ATOMIC_RELAXED);
            __atomic_store_n(&collectorTls(), collector.get(), __ATOMIC_RELEASE);
        }
        ~Owner() {
            __atomic_store_n(&collectorTls(), static_cast<Collector*>(nullptr), __ATOMIC_RELEASE);
            collector->retire();
        }
    };
    static thread_local Owner owner;
    return owner.collector;
}

__attribute__((always_inline)) inline void handleSignal(void* context) noexcept {
    if (auto* collector = __atomic_load_n(&collectorTls(), __ATOMIC_ACQUIRE))
        collector->collect(static_cast<const ucontext_t*>(context));
}
} // namespace swRuntimeStackCapture
#endif
