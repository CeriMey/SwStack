#pragma once

/**
 * @file src/core/runtime/SwCoreApplication.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwCoreApplication in the CoreSw runtime layer.
 *
 * This header belongs to the CoreSw runtime layer. It coordinates application lifetime, event
 * delivery, timers, threads, crash handling, and other process-level services consumed by the
 * rest of the stack.
 *
 * Within that layer, this file focuses on the core application interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are _T and SwCoreApplication.
 *
 * Application-oriented declarations here define the top-level lifecycle surface for startup,
 * shutdown, event processing, and integration with the rest of the framework.
 *
 * Runtime declarations in this area define lifecycle and threading contracts that higher-level
 * modules depend on for safe execution and orderly shutdown.
 *
 */


#ifndef SW_CORE_RUNTIME_SWCOREAPPLICATION_H
#define SW_CORE_RUNTIME_SWCOREAPPLICATION_H
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

/***************************************************************************************************
 * Acknowledgements:                                                                                *
 *                                                                                                  *
 * A heartfelt thank you to nullprogram.com, and especially to Chris Wellons, for the exceptional   *
 * quality of the technical information and insights shared on his blog:                            *
 * https://nullprogram.com/blog/2019/03/28/.                                                        *
 *                                                                                                  *
 * His guidance on Windows fibers and related concepts provided an invaluable foundation for        *
 * implementing this architecture. The clarity, detail, and thoroughness of his posts made          *
 * building a flexible, fiber-based event handling and async/await system more approachable.        *
 *                                                                                                  *
 * Contact for the blog's author:                                                                   *
 *  - Chris Wellons                                                                                 *
 *  - wellons@nullprogram.com (PGP)                                                                 *
 *  - ~skeeto/public-inbox@lists.sr.ht (mailing list)                                               *
 *                                                                                                  *
 ***************************************************************************************************/

#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <queue>
#include <memory>
#include <chrono>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <limits>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <vector>
static constexpr const char* kSwLogCategory_SwCoreApplication = "sw.core.runtime.swcoreapplication";


#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include "platform/win/SwWindows.h"
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <csignal>
#include <signal.h>
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "linux_fiber.h"
#endif

#include "SwMap.h"
#include "SwString.h"
#include "SwList.h"
#include "SwMutex.h"
#include "SwEvent.h"
#include "SwEventDispatchSupport.h"
#include "SwFiberPool.h"
#include "SwRuntimeProfiler.h"
#include <thread>



/**
 * @brief Internal class representing a high-precision timer that executes a callback at specified intervals.
 *
 * The `_T` class is used internally by `SwCoreApplication` to manage timers. It allows scheduling
 * and execution of a callback function either as a recurring task or as a single-shot event.
 * The timer operates with microsecond precision, making it suitable for applications requiring
 * accurate timing.
 *
 * ### Key Features:
 * - **Callback Execution**:
 *   - Executes the provided callback function when the timer is ready.
 * - **Precision Timing**:
 *   - Tracks elapsed time with microsecond-level precision using `std::chrono::steady_clock`.
 * - **Timer Modes**:
 *   - Supports both recurring timers and single-shot timers.
 * - **Utility Functions**:
 *   - Determines readiness of the timer with `isReady`.
 *   - Calculates the time remaining until the timer is ready with `timeUntilReady`.
 *
 * ### Usage:
 * 1. Create an instance of `_T` with a callback function, interval (in microseconds), and
 *    an optional single-shot mode.
 * 2. Use `isReady` to check if the timer is ready for execution.
 * 3. Call `execute` to run the callback and reset the timer (for recurring timers).
 * 4. Use `timeUntilReady` to query the time remaining until the timer is ready.
 *
 * ### Example:
 * ```cpp
 * _T timer([]() { swCDebug(kSwLogCategory_SwCoreApplication) << "Timer fired!"; }, 1000000, false); // 1-second recurring timer
 * if (timer.isReady()) {
 *     timer.execute();
 * }
 * ```
 *
 * @note This class is designed to be lightweight and efficient for use in high-frequency event loops.
 *
 * @warning Instances of `_T` are not thread-safe. Ensure that the timer is managed within a
 *          thread-safe context (e.g., using mutexes) if accessed concurrently.
 */
class _T {
    friend class SwCoreApplication;

public:
    /**
     * @brief Constructs a timer instance.
     * @param callback The function to execute when the timer fires.
     * @param interval The interval in microseconds between executions.
     * @param singleShot If `true`, the timer fires only once and must be manually removed after execution.
     */
    _T(std::function<void()> callback,
       int interval,
       bool singleShot = false,
       SwFiberLane lane = SwFiberLane::Normal)
        : callback(callback),
        interval(interval),
        singleShot(singleShot),
        lane(lane),
        dispatchPending(false),
        cancelled(false),
        lastExecutionTime(std::chrono::steady_clock::now())
    {}

    /**
     * @brief Checks if the timer is ready to fire.
     * @return `true` if the timer is ready, otherwise `false`.
     */
    bool isReady() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - lastExecutionTime).count() >= interval;
    }

    /**
     * @brief Executes the timer's callback and updates the last execution time.
     */
    void execute() {
        lastExecutionTime = std::chrono::steady_clock::now();
        callback();
    }

    /**
     * @brief Calculates the time remaining until the timer is ready.
     * @return The time in microseconds until the timer is ready, or `0` if the timer is already ready.
     */
    int timeUntilReady() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastExecutionTime).count();
        return (std::max)(0, interval - static_cast<int>(elapsed));
    }

private:
    std::function<void()> callback; ///< The function to execute when the timer fires.
    int interval; ///< Interval in microseconds between timer executions.
    bool singleShot; ///< Indicates if the timer is single-shot (`true`) or recurring (`false`).
    SwFiberLane lane; ///< Scheduling lane used when the timer callback is dispatched.
    bool dispatchPending; ///< Prevents a ready timer from being enqueued multiple times before execution.
    bool cancelled; ///< Marks a timer removed while a dispatch is still pending.
    std::chrono::steady_clock::time_point lastExecutionTime; ///< The last time the timer was executed.
};



// Déclaration du gestionnaire
#if defined(_WIN32)
static BOOL WINAPI ConsoleHandler(DWORD ctrlType);
#endif


/**
 * @brief Main application class for managing an event-driven system with fiber-based multitasking.
 *
 * The `SwCoreApplication` class serves as the core of the application, providing an event loop,
 * timer management, and fiber-based cooperative multitasking. It allows for precise control over
 * the execution flow of tasks through mechanisms like `yield`, `unYield`, and fiber switching.
 * Additionally, it utilizes high-precision timers on Windows to ensure accurate timing for events
 * and tasks.
 *
 * ### Key Features:
 * - **Event Loop**:
 *   - Handles an event queue, processing functions and callbacks asynchronously.
 *   - Supports efficient task scheduling and execution.
 * - **Timer Management**:
 *   - Allows adding, removing, and managing timers with microsecond-level precision.
 *   - Supports single-shot and recurring timers.
 * - **Fiber-Based Multitasking**:
 *   - Implements cooperative multitasking using Windows fibers.
 *   - Provides `yieldFiber` and `unYieldFiber` for pausing and resuming task execution.
 *   - Ensures efficient resource management for fibers through mechanisms like `release` and `deleteFiberIfNeeded`.
 * - **High-Precision Timing**:
 *   - Uses multimedia timers on Windows for enhanced precision in timer scheduling.
 *   - Ensures consistent timing behavior across tasks and events.
 *
 * ### Workflow:
 * 1. The main event loop (`exec`) continuously processes events and manages timers.
 * 2. Tasks and timers are executed within fibers, allowing for non-blocking execution.
 * 3. Fibers can yield control back to the main loop and resume later as needed.
 * 4. Events and timers are handled with thread-safe mechanisms to ensure consistency in
 *    multi-threaded environments.
 *
 * ### Typical Usage:
 * - Instantiate the `SwCoreApplication` class.
 * - Use `postEvent` to enqueue tasks or events.
 * - Add timers with `addTimer` for time-based task scheduling.
 * - Start the main event loop with `exec` to process tasks and timers.
 *
 * @note This class is designed for applications requiring precise control over asynchronous tasks,
 *       such as real-time systems or applications with complex scheduling requirements.
 *
 * @warning This class heavily relies on Windows-specific APIs (fibers and multimedia timers).
 *          It is not portable to non-Windows platforms.
 */
class SwCoreApplication {

    friend class SwTimer;
    friend class SwEventLoop;

private:
    struct PostedObjectEvent_ {
        SwObject* receiver{nullptr};
        std::unique_ptr<SwEvent> event;
        int priority{0};
    };

#if !defined(_WIN32)
    static int unixWatchdogSignalNumber_() { return SIGUSR2; }

    static int& unixTerminateEventFd_() {
        static int s_fd = -1;
        return s_fd;
    }

    static std::atomic<bool>& unixTerminateRequested_() {
        static std::atomic<bool> s_requested{false};
        return s_requested;
    }

    static void unixTerminateSignalHandler_(int /*signalNumber*/, siginfo_t* /*info*/, void* /*uctx*/) {
        unixTerminateRequested_().store(true, std::memory_order_relaxed);
        const int fd = unixTerminateEventFd_();
        if (fd >= 0) {
            const uint64_t one = 1;
            const ssize_t n = ::write(fd, &one, sizeof(one));
            (void)n;
        }
    }

    static void unixWatchdogPreemptSignalHandler_(int /*signalNumber*/, siginfo_t* /*info*/, void* uctx) {
#if defined(__ANDROID__)
         (void)uctx;
         return;
#else
         SwCoreApplication* app = SwCoreApplication::instance(false);
         if (!app) return;
         LPVOID runningFiber = app->m_runningFiber.load(std::memory_order_relaxed);
         if (!runningFiber) return;

        LPVOID current = GetCurrentFiber();
        if (!current || current == app->mainFiber) return;
        if (current != runningFiber) return;

        ucontext_t* ctx = reinterpret_cast<ucontext_t*>(uctx);
        if (!ctx) return;

         swcore::linux_fiber::Fiber* currentFiber = reinterpret_cast<swcore::linux_fiber::Fiber*>(current);
         swcore::linux_fiber::Fiber* mainFiber = reinterpret_cast<swcore::linux_fiber::Fiber*>(app->mainFiber);
         if (!currentFiber || !mainFiber) return;

         app->fireWatchDog.store(true, std::memory_order_relaxed);

         // Save the interrupted execution point into the fiber so it can be resumed later.
         currentFiber->context.uc_mcontext = ctx->uc_mcontext;
         currentFiber->context.uc_sigmask = ctx->uc_sigmask;
#if defined(__x86_64__) && defined(__GLIBC__)
         if (ctx->uc_mcontext.fpregs) {
             currentFiber->context.__fpregs_mem = *ctx->uc_mcontext.fpregs;
             currentFiber->context.uc_mcontext.fpregs = &currentFiber->context.__fpregs_mem;
         } else {
             currentFiber->context.uc_mcontext.fpregs = nullptr;
         }
         for (size_t i = 0; i < 4; ++i) {
             currentFiber->context.__ssp[i] = ctx->__ssp[i];
         }
#endif
         (void)::sigdelset(&currentFiber->context.uc_sigmask, unixWatchdogSignalNumber_());

         // Force execution back to the main fiber context (best-effort).
         //
         // WARNING: setcontext() is NOT async-signal-safe per POSIX. This is
         // technically undefined behavior. It works reliably on glibc (x86_64,
         // aarch64) because glibc's setcontext is a thin syscall wrapper, but
         // may break on musl, non-glibc, or future toolchains with shadow stacks.
         // The alternative would be siglongjmp, but it cannot restore the full
         // ucontext (FP state, signal mask) needed for correct fiber resumption.
         // This tradeoff is accepted: watchdog preemption is best-effort.
         //
         // Note: On modern x86_64 toolchains with CET enabled (shadow stacks / IBT),
         // returning from the signal handler with a manually patched ucontext can
         // crash due to inconsistent shadow-stack state. Using setcontext() here
         // lets libc restore all required state for the target context.
         swcore::linux_fiber::currentFiberRef() = mainFiber;
         ucontext_t mainCtx = mainFiber->context;
         (void)::sigdelset(&mainCtx.uc_sigmask, unixWatchdogSignalNumber_());
         (void)::setcontext(&mainCtx);
#endif
     }

    static void installUnixSignalHandlersOnce_() {
        static std::once_flag s_once;
        std::call_once(s_once, []() {
            if (unixTerminateEventFd_() < 0) {
                unixTerminateEventFd_() = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            }

            // SIGINT/SIGTERM: signal-safe handler (write to eventfd + set atomic flag).
            struct sigaction term {};
            term.sa_flags = SA_SIGINFO;
            term.sa_sigaction = &SwCoreApplication::unixTerminateSignalHandler_;
            (void)::sigemptyset(&term.sa_mask);
            (void)::sigaction(SIGINT, &term, nullptr);
            (void)::sigaction(SIGTERM, &term, nullptr);

            // Watchdog preemption: best-effort "async yield" to the main fiber.
#if !defined(__ANDROID__)
            struct sigaction wd {};
            wd.sa_flags = SA_SIGINFO;
            wd.sa_sigaction = &SwCoreApplication::unixWatchdogPreemptSignalHandler_;
            (void)::sigemptyset(&wd.sa_mask);
            (void)::sigaction(unixWatchdogSignalNumber_(), &wd, nullptr);
#endif
        });
    }
#endif

public:
    /**
     * @brief Default constructor of the application.
     *
     * Initializes high-precision timers, converts the main thread to a fiber, and sets up necessary structures.
     */
    SwCoreApplication()
        : running(true), exitCode(0) {
        bindInstanceToCurrentThread(this);
        enableHighPrecisionTimers();
        initFibers();
        refreshFiberPoolConfig_();
        fiberPool_.bindMainFiber(mainFiber);
        fiberPool_.bindRuntimeState(&m_runningFiber, &fiberStartTimeNs_, &fireWatchDog);
        initWakeup_();
#if defined(_WIN32)
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);
        // Sauvegarde du handle du thread principal
        mainThreadHandle = GetCurrentThread();
        mainThreadId = GetCurrentThreadId();
#else
        installUnixSignalHandlersOnce_();
        mainThreadPthread_ = pthread_self();
#endif
    }

    /**
     * @brief Constructor with command-line arguments.
     * @param argc Number of arguments.
     * @param argv Array of argument strings.
     *
     * Initializes high-precision timers, parses command-line arguments, converts the main thread to a fiber,
     * and sets up necessary structures.
     */
    SwCoreApplication(int argc, char* argv[])
        : running(true), exitCode(0) {
        bindInstanceToCurrentThread(this);
        enableHighPrecisionTimers();
        parseArguments(argc, argv);
        initFibers();
        refreshFiberPoolConfig_();
        fiberPool_.bindMainFiber(mainFiber);
        fiberPool_.bindRuntimeState(&m_runningFiber, &fiberStartTimeNs_, &fireWatchDog);
        initWakeup_();
#if defined(_WIN32)
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);
        // Sauvegarde du handle du thread principal
        mainThreadHandle = GetCurrentThread();
        mainThreadId = GetCurrentThreadId();
#else
        installUnixSignalHandlersOnce_();
        mainThreadPthread_ = pthread_self();
#endif
    }

    /**
     * @brief Destructor.
     *
     * Disables high-precision timers and performs necessary cleanup.
     */
    virtual ~SwCoreApplication() {
        uninstallProfiler();
        desactiveWatchDog();
        fiberPool_.shutdown();
        shutdownWakeup_();
        disableHighPrecisionTimers();
        unbindInstanceFromCurrentThread(this);
    }

    /**
     * @brief Accesses the static instance of the application.
     * @param create If `true` and the instance does not exist, creates one.
     * @return Pointer to the static application instance.
     */
    static SwCoreApplication* instance(bool create = true) {
        SwCoreApplication* tlsInstance = getThreadLocalInstance_();
        if (!tlsInstance && create) {
            tlsInstance = new SwCoreApplication();
        }
        return tlsInstance;
    }

    // Best-effort helper for OS signal/console handlers.
    //
    // On Windows, the ConsoleCtrlHandler can run on a dedicated system thread, so `instance(false)`
    // (thread-local) may return nullptr. We keep a registry of known instances per thread and can
    // request a clean shutdown from any thread.
    /**
     * @brief Returns the current request Quit All Instances.
     * @return The current request Quit All Instances.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static bool requestQuitAllInstances() {
        bool requested = false;
        {
            std::lock_guard<std::mutex> lock(instanceRegistryMutex());
            for (auto& kv : instanceRegistry()) {
                if (!kv.second) continue;
                kv.second->quit();
                requested = true;
            }
        }
        return requested;
    }

    /**
     * @brief Performs the `activeWatchDog` operation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void activeWatchDog() {
        // Si le watchdog n'est pas déjà actif
        bool expected = false;
        if (watchdogRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            watchdogThread = std::thread(&SwCoreApplication::watchdogLoop, this);
        }
    }

    // Méthode pour désactiver le watchdog
    /**
     * @brief Performs the `desactiveWatchDog` operation.
     */
    void desactiveWatchDog() {
        // Si le watchdog est actif
        if (watchdogRunning.exchange(false, std::memory_order_acq_rel)) {
            if (watchdogThread.joinable()) {
                watchdogThread.join();
            }
        }
    }


    /**
     * @brief Returns the current load Percentage.
     * @return The current load Percentage.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double getLoadPercentage() const {
        std::lock_guard<std::mutex> lock(measurementsMutex_);
        if (totalTimeMicroseconds == 0) {
            return 0.0;
        }
        return 100.0 * (double)totalBusyTimeMicroseconds / (double)totalTimeMicroseconds;
    }

    /**
     * @brief Returns the current last Second Load Percentage.
     * @return The current last Second Load Percentage.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double getLastSecondLoadPercentage() {
        std::lock_guard<std::mutex> lock(measurementsMutex_);
        auto now = std::chrono::steady_clock::now();
        auto oneSecondAgo = now - std::chrono::seconds(1);

        // On supprime les mesures plus vieilles que 1 seconde
        while (!measurements.empty() && measurements.front().timestamp < oneSecondAgo) {
            measurements.pop_front();
        }

        uint64_t sumBusy = 0;
        uint64_t sumTotal = 0;
        for (auto &m : measurements) {
            sumBusy += m.busyMicroseconds;
            sumTotal += m.totalMicroseconds;
        }

        if (sumTotal == 0) return 0.0;
        return 100.0 * (double)sumBusy / (double)sumTotal;
    }

    /**
     * @brief Sets the event Fiber Stack Size.
     * @param stackSizeBytes Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEventFiberStackSize(unsigned int stackSizeBytes) {
        eventFiberStackSize_.store(stackSizeBytes, std::memory_order_relaxed);
        if (fiberPool_.isWarmInitialized()) {
            swCWarning(kSwLogCategory_SwCoreApplication)
                << "setEventFiberStackSize() called after fiber pool warm-up; "
                << "existing warm fibers keep their original stack size and only future spillover fibers may use the new value.";
        }
        refreshFiberPoolConfig_();
    }

    /**
     * @brief Returns the current event Fiber Stack Size.
     * @return The current event Fiber Stack Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    unsigned int eventFiberStackSize() const {
        return eventFiberStackSize_.load(std::memory_order_relaxed);
    }

    bool installProfiler(SwRuntimeProfileSink* sink,
                         const SwRuntimeProfileConfig& config = SwRuntimeProfileConfig()) {
        if (!sink) {
            return false;
        }

        uninstallProfiler();
        profilerSession_.reset(new SwRuntimeProfilerSession(sink, config));
        fiberPool_.bindObserver(profilerSession_.get());
        if (SwCoreApplication::instance(false) == this) {
            profilerSession_->bindToCurrentThread();
        }
        SwRuntimeProfilerService::registerSession(profilerSession_);
        return true;
    }

    void uninstallProfiler() {
        if (!profilerSession_) {
            fiberPool_.bindObserver(nullptr);
            return;
        }

        std::shared_ptr<SwRuntimeProfilerSession> oldSession = profilerSession_;
        profilerSession_.reset();
        fiberPool_.bindObserver(nullptr);
        SwRuntimeProfilerService::unregisterSession(oldSession.get());
        oldSession->clearThreadCurrent();
    }

    bool profilerEnabled() const {
        return static_cast<bool>(profilerSession_);
    }

    void setProfilerStallThresholdUs(long long thresholdUs) {
        if (!profilerSession_) {
            return;
        }
        profilerSession_->setStallThresholdUs(thresholdUs);
    }

    /**
     * @brief Posts an event (a function) to the event queue.
     * @param event Function to execute during event processing.
     */
    void postEvent(std::function<void()> event) {
        postEventOnLane(std::move(event), SwFiberLane::Normal);
    }

    void postEventOnLane(std::function<void()> event, SwFiberLane lane) {
        postEventOnLaneImpl_(std::move(event), lane, true);
    }

private:
    void postEventOnLaneImpl_(std::function<void()> event,
                              SwFiberLane lane,
                              bool emitPostedTiming) {
        std::function<void()> dispatchedEvent = std::move(event);
        if (emitPostedTiming && profilerSession_ && profilerSession_->autoRuntimeScopesEnabled()) {
            std::shared_ptr<SwRuntimeProfilerSession> session = profilerSession_;
            std::function<void()> inner = std::move(dispatchedEvent);
            dispatchedEvent = [session, inner, lane]() mutable {
                session->bindToCurrentThread();
                SwRuntimeScopedSpan scope(session.get(),
                                          SwRuntimeTimingKind::PostedEvent,
                                          "posted_event",
                                          lane,
                                          true);
                inner();
            };
        }

        bool rejectedByBackpressure = false;
        const bool accepted = fiberPool_.enqueueTask(std::move(dispatchedEvent), lane, &rejectedByBackpressure);
        if (!accepted && rejectedByBackpressure) {
            swCWarning(kSwLogCategory_SwCoreApplication)
                << "SwFiberPool saturated; rejecting posted event on lane=" << static_cast<int>(lane);
            return;
        }
        cv.notify_one();
        signalWakeup_();
    }

public:

    /**
     * @brief Posts an event object to a specific receiver, close to `QCoreApplication::postEvent`.
     *
     * Ownership of `event` is transferred to the event queue unless delivery is rerouted to the
     * receiver thread or dropped because the receiver is invalid.
     */
    void postEvent(SwObject* receiver, SwEvent* event, int priority = 0) {
        if (!receiver || !event) {
            delete event;
            return;
        }

        if (swForwardPostedEventToReceiverThread(receiver, event, priority)) {
            return;
        }

        PostedObjectEvent_ postedEvent;
        postedEvent.receiver = receiver;
        postedEvent.event.reset(event);
        postedEvent.priority = priority;

        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            if (priority > 0) {
                priorityPostedEventQueue_.push_back(std::move(postedEvent));
            } else {
                postedEventQueue_.push_back(std::move(postedEvent));
            }
        }
        cv.notify_one();
        signalWakeup_();
    }

    /**
     * @brief Posts a high-priority event (processed before normal events).
     *
     * Useful for latency-sensitive wakeups (ex: RPC completions) when the normal event queue is busy.
     */
    void postEventPriority(std::function<void()> event) {
        postEventOnLane(std::move(event), SwFiberLane::Control);
    }

    /**
     * @brief Posts a high-priority event and cooperatively yields the current fiber (if any).
     *
     * This is a "soft preemption" helper: it doesn't interrupt a running fiber asynchronously,
     * but it will suspend the current fiber at this safe point so the scheduler can run the
     * priority event ASAP.
     */
    void postEventPriorityAndYield(std::function<void()> event) {
        postEventPriority(std::move(event));
        // Only yield if we're on the same thread/fiber scheduler instance.
        SwCoreApplication* tls = SwCoreApplication::instance(false);
        if (tls == this) {
            SwCoreApplication::release();
        }
    }

    SwFiberPool& fiberPool() {
        return fiberPool_;
    }

    const SwFiberPool& fiberPool() const {
        return fiberPool_;
    }

    SwFiberPoolStats fiberPoolStats() const {
        return fiberPool_.stats();
    }

    /**
     * @brief Delivers an event immediately to a receiver, close to `QCoreApplication::sendEvent`.
     */
    static bool sendEvent(SwObject* receiver, SwEvent* event) {
        if (!receiver || !event || !swIsObjectLive(receiver)) {
            return false;
        }

        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            return app->notify(receiver, event);
        }

        if (swDispatchInstalledEventFilters(receiver, event)) {
            return true;
        }
        return swDispatchEventToObject(receiver, event);
    }

    /**
     * @brief Processes queued posted events, optionally filtered by receiver and type.
     */
    static void sendPostedEvents(SwObject* receiver = nullptr, EventType type = EventType::None) {
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->sendPostedEventsImpl_(receiver, type);
        }
    }

    /**
     * @brief Central event delivery hook.
     */
    virtual bool notify(SwObject* receiver, SwEvent* event) {
        if (!receiver || !event || !swIsObjectLive(receiver)) {
            return false;
        }

        SwRuntimeProfilerSession* profilerSession = profilerSessionForCurrentThread_();
        SwRuntimeScopedSpan profileScope(profilerSession && profilerSession->autoRuntimeScopesEnabled()
                                             ? profilerSession
                                             : nullptr,
                                         SwRuntimeTimingKind::ObjectEvent,
                                         "object_event",
                                         SwFiberLane::Normal,
                                         true);

        const bool previousKernelDispatch = event->isKernelDispatched();
        event->setKernelDispatched(true);

        const int filterCount = static_cast<int>(applicationEventFilters_.size());
        for (int i = filterCount - 1; i >= 0; --i) {
            SwObject* filter = applicationEventFilters_[static_cast<size_t>(i)];
            if (swDispatchEventFilter(filter, receiver, event)) {
                event->accept();
                event->setKernelDispatched(previousKernelDispatch);
                return true;
            }
        }

        if (swDispatchInstalledEventFilters(receiver, event)) {
            event->accept();
            event->setKernelDispatched(previousKernelDispatch);
            return true;
        }

        const bool handled = swDispatchEventToObject(receiver, event);
        event->setKernelDispatched(previousKernelDispatch);
        return handled;
    }

    void installEventFilter(SwObject* filterObj) {
        if (!filterObj) {
            return;
        }
        applicationEventFilters_.erase(std::remove(applicationEventFilters_.begin(),
                                                   applicationEventFilters_.end(),
                                                   filterObj),
                                       applicationEventFilters_.end());
        applicationEventFilters_.push_back(filterObj);
    }

    void removeEventFilter(SwObject* filterObj) {
        if (!filterObj) {
            return;
        }
        applicationEventFilters_.erase(std::remove(applicationEventFilters_.begin(),
                                                   applicationEventFilters_.end(),
                                                   filterObj),
                                       applicationEventFilters_.end());
    }

    /**
     * @brief Adds a timer.
     * @param callback Function to call when the timer expires.
     * @param interval Interval in microseconds between two executions of the callback.
     * @return The identifier of the created timer.
     */
    int addTimer(std::function<void()> callback,
                 int interval,
                 bool singleShot = false,
                 SwFiberLane lane = SwFiberLane::Normal) {
        int timerId;
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            timerId = nextTimerId++;
            timers.insert(timerId, new _T(callback, interval, singleShot, lane));
        }
        signalWakeup_();
        return timerId;
    }

    /**
     * @brief Removes a timer.
     * @param timerId Identifier of the timer to remove.
     */
    void removeTimer(int timerId) {
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            auto it = timers.find(timerId);
            if (it != timers.end()) {
                _T* toDelete = it->second;
                timers.erase(it);
                toDelete->cancelled = true;
                if (processingTimersDepth_ > 0 || toDelete->dispatchPending) {
                    pendingTimerDeletes_.push_back(toDelete);
                } else {
                    delete toDelete;
                }
            }
            auto itOld = timers.find(-1);
            if (itOld != timers.end()) {
                _T* toDelete = itOld->second;
                timers.erase(itOld);
                toDelete->cancelled = true;
                if (processingTimersDepth_ > 0 || toDelete->dispatchPending) {
                    pendingTimerDeletes_.push_back(toDelete);
                } else {
                    delete toDelete;
                }
            }
        }
        signalWakeup_();
    }

    /**
     * @brief Executes the main event loop for a specified duration.
     *
     * This method runs the application's main event loop for a maximum duration specified in microseconds.
     * It processes events, manages timers, and resumes fibers while ensuring precise time control
     * for the duration of the loop.
     *
     * ### Workflow:
     * 1. Sets the thread priority to high using `setHighThreadPriority`.
     * 2. Records the start time of the loop for duration tracking.
     * 3. Enters the main loop:
     *    - Processes events using `processEvent`, which also handles timers and fibers.
     *    - Calculates the elapsed time since the last iteration and adjusts the sleep duration
     *      to maintain timing precision.
     *    - Checks the total elapsed time and exits the loop if it exceeds the maximum duration.
     *    - Introduces a short sleep to prevent CPU overuse and allow for other tasks to execute.
     * 4. Exits the loop and returns the application's exit code.
     *
     * @param maxDurationMicroseconds Maximum time the loop should run, in microseconds.
     * @return The application's exit code, typically set using `exit()` or `quit()`.
     *
     * @note The loop ensures the system remains responsive by balancing event processing
     *       and controlled delays.
     *
     * @warning Ensure that the `running` flag is managed correctly to avoid infinite loops.
     *          If the application is terminated before the duration expires, the loop will exit early.
     *
     * @remarks This method is useful for applications that require running the event loop
     *          for a fixed duration, such as for testing or temporary tasks.
     */
    virtual int exec(int maxDurationMicroseconds = 0) {
        setHighThreadPriority();
        (void)profilerSessionForCurrentThread_();
        auto startTime = std::chrono::steady_clock::now();
        auto lastTime = startTime;

        while (running) {
            // Avant de traiter un nouvel événement, on remet à zéro le temps occupé de l'itération
            busyElapsedIteration = 0;

            auto currentTime = std::chrono::steady_clock::now();
            int sleepDuration = processEvent();

            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastTime).count();
            lastTime = currentTime;

            {
                std::lock_guard<std::mutex> lock(measurementsMutex_);
                // On met à jour le temps total
                totalTimeMicroseconds += (uint64_t)elapsed;
                // On ajoute à totalBusyTimeMicroseconds le temps occupé de cette itération
                totalBusyTimeMicroseconds += (uint64_t)busyElapsedIteration;

                // On enregistre la mesure de cette itération
                measurements.push_back({
                    currentTime,
                    (uint64_t)busyElapsedIteration,
                    (uint64_t)elapsed
                });

                // Nettoyage des mesures plus vieilles que 1 seconde
                auto oneSecondAgo = currentTime - std::chrono::seconds(1);
                while (!measurements.empty() && measurements.front().timestamp < oneSecondAgo) {
                    measurements.pop_front();
                }
            }

            profilerUpdateIterationSnapshot_();

            auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - startTime).count();
            if (maxDurationMicroseconds != 0 && totalElapsed >= maxDurationMicroseconds) {
                break;
            }

            if (!running) break;
            if (sleepDuration != 0) {
                waitForWork_(sleepDuration);
            }
        }
        return exitCode.load(std::memory_order_acquire);
    }


    /**
     * @brief Processes a single event or manages timers, handling fibers as needed.
     *
     * This function processes events from the event queue, executes timer callbacks, and manages
     * the resumption of fibers that are ready to run. It is the core function for handling
     * event-driven tasks and timer-based scheduling within the application.
     *
     * ### Workflow:
     * 1. **Event Handling**:
     *    - If the event queue is empty and `waitForEvent` is `true`, the function waits for a new
     *      event or timer expiration using a condition variable.
     *    - If an event is available, it is extracted from the queue and executed within a fiber
     *      using `runEventInFiber`. The function returns `0` after processing the event.
     * 2. **Timer Management**:
     *    - Iterates through the list of active timers and checks if they are ready to execute.
     *    - Executes callbacks for ready timers within fibers:
     *        - If the timer is single-shot, it is removed and deleted after execution.
     *        - If the timer is recurring, it remains in the timer list.
     *    - Updates the minimum time until the next timer is ready.
     * 3. **Fiber Management**:
     *    - Calls `resumeReadyFibers` to handle fibers that were previously paused and are now
     *      ready to run.
     * 4. **Return Value**:
     *    - If an event or a timer is processed, the function returns `0`.
     *    - Otherwise, it returns the time in microseconds until the next timer is ready, or a
     *      small default value if no timers are active.
     *
     * @param waitForEvent If `true`, blocks and waits for an event to arrive if the event queue and
     *                     timer list are empty.
     * @return The time in microseconds until the next timer expires, or `0` if an event is imminent.
     *
     * @note This function ensures thread-safe access to the event queue and timer list using
     *       `std::mutex` locks.
     *
     * @warning Timer callbacks and events are executed within fibers. Care must be taken to ensure
     *          that these callbacks do not block or cause deadlocks.
     *
     * @remarks This function is designed to be called repeatedly within the main event loop of the
     *          application to drive the event and timer system.
     */
    int processEvent(bool waitForEvent = false) {
#if !defined(_WIN32)
         if (unixTerminateRequested_().load(std::memory_order_relaxed)) {
             (void)SwCoreApplication::requestQuitAllInstances();
             if (!running.load(std::memory_order_relaxed)) {
                 return 0;
             }
         }
#endif
         std::unique_lock<std::mutex> lock(eventQueueMutex);

        // Wait for an event if the queue is empty and waiting is allowed
        if (priorityPostedEventQueue_.empty() &&
            postedEventQueue_.empty() &&
            timers.empty() &&
            !fiberPool_.hasWork() &&
            waitForEvent) {
            cv.wait(lock, [this]() {
                return !priorityPostedEventQueue_.empty() ||
                       !postedEventQueue_.empty() ||
                       !timers.empty() ||
                       fiberPool_.hasWork();
            });
        }

        if (fiberPool_.hasControlWork()) {
            lock.unlock();
            if (runFiberPoolWork_()) {
                return 0;
            }
            lock.lock();
        }

        if (!priorityPostedEventQueue_.empty()) {
            PostedObjectEvent_ postedEvent = std::move(priorityPostedEventQueue_.front());
            priorityPostedEventQueue_.pop_front();
            lock.unlock();
            dispatchPostedEvent_(std::move(postedEvent));
            return 0;
        }

        lock.unlock();

        int minTimeUntilNext = processTimers();
        if (runFiberPoolWork_()) {
            return 0;
        }

        lock.lock();

        if (fiberPool_.hasNonControlWork()) {
            lock.unlock();
            if (runFiberPoolWork_()) {
                return 0;
            }
            lock.lock();
        }

        if (!postedEventQueue_.empty()) {
            PostedObjectEvent_ postedEvent = std::move(postedEventQueue_.front());
            postedEventQueue_.pop_front();
            lock.unlock();
            dispatchPostedEvent_(std::move(postedEvent));
            return 0;
        }
        lock.unlock();

        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            if (!priorityPostedEventQueue_.empty() ||
                !postedEventQueue_.empty()) {
                return 0;
            }
        }
        if (fiberPool_.hasWork()) {
            return 0;
        }
        // No pending work: wait indefinitely unless a timer is scheduled.
        return minTimeUntilNext != (std::numeric_limits<int>::max)() ? minTimeUntilNext : -1;
    }

    /**
     * @brief Checks if there are pending events or timers.
     * @return `true` if there are pending events or timers, `false` otherwise.
     */
    bool hasPendingEvents() {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        return !priorityPostedEventQueue_.empty() ||
               !postedEventQueue_.empty() ||
               !timers.empty() ||
               fiberPool_.hasWork();
    }

protected:
    void refreshFiberPoolConfig_() {
        SwFiberPoolConfig config;
        const unsigned int configuredStackSize = eventFiberStackSize_.load(std::memory_order_relaxed);
        if (configuredStackSize != 0) {
            config.stackSizeBytes = configuredStackSize;
        }
        fiberPool_.setConfig(config);
    }

    SwRuntimeProfilerSession* profilerSessionForCurrentThread_() {
        if (!profilerSession_) {
            return nullptr;
        }
        profilerSession_->bindToCurrentThread();
        return profilerSession_.get();
    }

    double profilerLastSecondLoadPercentage_() const {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point oneSecondAgo = now - std::chrono::seconds(1);

        uint64_t sumBusy = 0;
        uint64_t sumTotal = 0;
        for (std::deque<IterationMeasurement>::const_iterator it = measurements.begin();
             it != measurements.end();
             ++it) {
            if (it->timestamp < oneSecondAgo) {
                continue;
            }
            sumBusy += it->busyMicroseconds;
            sumTotal += it->totalMicroseconds;
        }
        if (sumTotal == 0) {
            return 0.0;
        }
        return 100.0 * static_cast<double>(sumBusy) / static_cast<double>(sumTotal);
    }

    void profilerUpdateIterationSnapshot_() {
        if (!profilerSession_) {
            return;
        }

        SwRuntimeCountersSnapshot snapshot;
        snapshot.busyMicroseconds = busyElapsedIteration;
        if (!measurements.empty()) {
            snapshot.totalMicroseconds = measurements.back().totalMicroseconds;
        }
        if (totalTimeMicroseconds != 0) {
            snapshot.loadPercentage =
                100.0 * static_cast<double>(totalBusyTimeMicroseconds) / static_cast<double>(totalTimeMicroseconds);
        }
        snapshot.lastSecondLoadPercentage = profilerLastSecondLoadPercentage_();
        snapshot.fiberPoolStats = fiberPool_.stats();
        snapshot.droppedRecords = 0;

        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            snapshot.postedEventCount = static_cast<int>(postedEventQueue_.size());
            snapshot.priorityPostedEventCount = static_cast<int>(priorityPostedEventQueue_.size());
            snapshot.timerCount = static_cast<int>(timers.size());
        }

        profilerSession_->updateCounters(snapshot);
    }

private:
    bool runFiberPoolWork_() {
        (void)profilerSessionForCurrentThread_();
        auto startBusy = std::chrono::steady_clock::now();
        const bool ranWork = fiberPool_.runNextWorkItem();
        if (ranWork) {
            auto endBusy = std::chrono::steady_clock::now();
            busyElapsedIteration += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(endBusy - startBusy).count());
        }
        return ranWork;
    }

    static bool routeYieldWakeAcrossInstances_(int id, SwFiberLane lane) {
        SwCoreApplication* tlsInstance = SwCoreApplication::instance(false);
        if (tlsInstance && tlsInstance->fiberPool_.unYield(id, lane)) {
            tlsInstance->signalWakeup_();
            return true;
        }

        std::lock_guard<std::mutex> registryLock(instanceRegistryMutex());
        for (auto& kv : instanceRegistry()) {
            SwCoreApplication* app = kv.second;
            if (!app || app == tlsInstance) {
                continue;
            }
            if (app->fiberPool_.unYield(id, lane)) {
                app->signalWakeup_();
                return true;
            }
        }
        return false;
    }

    void dispatchPostedEvent_(PostedObjectEvent_ postedEvent) {
        if (!postedEvent.event || !postedEvent.receiver || !swIsObjectLive(postedEvent.receiver)) {
            return;
        }
        notify(postedEvent.receiver, postedEvent.event.get());
    }

    void sendPostedEventsImpl_(SwObject* receiver, EventType type) {
        while (true) {
            PostedObjectEvent_ postedEvent;
            bool found = false;

            {
                std::lock_guard<std::mutex> lock(eventQueueMutex);
                auto matchEvent = [receiver, type](const PostedObjectEvent_& candidate) {
                    if (receiver && candidate.receiver != receiver) {
                        return false;
                    }
                    if (type != EventType::None && candidate.event && candidate.event->type() != type) {
                        return false;
                    }
                    return true;
                };

                for (auto it = priorityPostedEventQueue_.begin(); it != priorityPostedEventQueue_.end(); ++it) {
                    if (!matchEvent(*it)) {
                        continue;
                    }
                    postedEvent = std::move(*it);
                    priorityPostedEventQueue_.erase(it);
                    found = true;
                    break;
                }

                if (!found) {
                    for (auto it = postedEventQueue_.begin(); it != postedEventQueue_.end(); ++it) {
                        if (!matchEvent(*it)) {
                            continue;
                        }
                        postedEvent = std::move(*it);
                        postedEventQueue_.erase(it);
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                return;
            }

            dispatchPostedEvent_(std::move(postedEvent));
        }
    }

public:

    /**
     * @brief Exits the application with a specified exit code.
     * @param code Exit code.
     */
    void exit(int code = 0) {
        exitCode.store(code, std::memory_order_release);
        quit();
    }

    /**
     * @brief Exits the application.
     */
    void quit() {
        running = false;
        cv.notify_all();
        signalWakeup_();
    }

protected:
    // Derived event loops (e.g. SwGuiApplication) sometimes need to integrate OS waitables (HANDLE/fd)
    // with their own platform message pumps. This helper provides access to the core waitables wait.
    /**
     * @brief Performs the `waitForWork` operation.
     * @param timeoutUs Value passed to the method.
     */
    void waitForWork(int timeoutUs) { waitForWork_(timeoutUs); }

#if defined(_WIN32)
    // Windows GUI-friendly wait: wakes on either waitables OR pending Win32 messages.
    /**
     * @brief Performs the `waitForWorkGui` operation.
     * @param timeoutUs Value passed to the method.
     */
    void waitForWorkGui(int timeoutUs) {
        if (!running) return;

        std::vector<HANDLE> handles;
        std::vector<std::function<void()>> callbacks;
        handles.reserve(1);
        callbacks.reserve(1);

        handles.push_back(wakeEvent_);
        callbacks.push_back(std::function<void()>{});

        {
            SwMutexLocker lk(waitablesMutex_);
            for (auto& kv : waitHandles_) {
                handles.push_back(kv.second.handle);
                callbacks.push_back(kv.second.cb);
            }
        }

        HANDLE timeoutTimer = NULL;
        DWORD timeoutMs = INFINITE;
        if (timeoutUs >= 0) {
            timeoutMs = static_cast<DWORD>((timeoutUs + 999) / 1000);

            timeoutTimer = ::CreateWaitableTimerExW(NULL,
                                                    NULL,
                                                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                    TIMER_ALL_ACCESS);
            if (!timeoutTimer) {
                timeoutTimer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
            }

            if (timeoutTimer) {
                LARGE_INTEGER dueTime;
                const LONGLONG clampedTimeoutUs = timeoutUs > 0 ? static_cast<LONGLONG>(timeoutUs) : 1LL;
                dueTime.QuadPart = -(clampedTimeoutUs * 10LL);
                if (::SetWaitableTimer(timeoutTimer, &dueTime, 0, NULL, NULL, FALSE)) {
                    handles.push_back(timeoutTimer);
                    callbacks.push_back(std::function<void()>{});
                    timeoutMs = INFINITE;
                } else {
                    ::CloseHandle(timeoutTimer);
                    timeoutTimer = NULL;
                }
            }
        }

        if (handles.empty()) return;
        if (handles.size() > MAXIMUM_WAIT_OBJECTS) {
            if (timeoutUs > 0) std::this_thread::sleep_for(std::chrono::microseconds(timeoutUs));
            return;
        }

        const DWORD r = ::MsgWaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                    handles.data(),
                                                    FALSE,
                                                    timeoutMs,
                                                    QS_ALLINPUT);

        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + handles.size()) {
            const size_t idx = static_cast<size_t>(r - WAIT_OBJECT_0);
            if (idx > 0 && idx < callbacks.size() && callbacks[idx]) {
                postEventOnLane(callbacks[idx], SwFiberLane::Control);
                (void)runFiberPoolWork_();
            }
            return;
        }
        // WAIT_OBJECT_0 + handles.size() means: messages are pending; let the GUI pump them.
        return;
    }
#endif

    // ---------------------------------------------------------------------
    // OS waitables (event-driven wakeup: Windows HANDLE / Linux fd)
    // ---------------------------------------------------------------------
public:
#if defined(_WIN32)
    /**
     * @brief Adds the specified wait Handle.
     * @param h Height value.
     * @param onSignaled Value passed to the method.
     * @return The requested wait Handle.
     */
    size_t addWaitHandle(HANDLE h, std::function<void()> onSignaled) {
        if (!h) return 0;
        SwMutexLocker lk(waitablesMutex_);
        const size_t id = nextWaitableId_++;
        waitHandles_[id] = WaitHandleEntry{h, std::move(onSignaled)};
        signalWakeup_();
        return id;
    }
#else
    /**
     * @brief Adds the specified wait Fd.
     * @param fd Value passed to the method.
     * @param onReadable Value passed to the method.
     * @return The requested wait Fd.
     */
    size_t addWaitFd(int fd, std::function<void()> onReadable) {
        if (fd < 0) return 0;
        SwMutexLocker lk(waitablesMutex_);
        const size_t id = nextWaitableId_++;
        waitFds_[id] = WaitFdEntry{fd, std::move(onReadable)};
        signalWakeup_();
        return id;
    }
#endif

    /**
     * @brief Removes the specified waitable.
     * @param id Value passed to the method.
     */
    void removeWaitable(size_t id) {
        if (!id) return;
        SwMutexLocker lk(waitablesMutex_);
#if defined(_WIN32)
        waitHandles_.remove(id);
#else
        waitFds_.remove(id);
#endif
        signalWakeup_();
    }

    /**
     * @brief Releases the current fiber and queues it for re-execution in the event loop.
     *
     * This function is used to pause the execution of the current fiber and move it to the
     * `s_readyFibers` queue, enabling it to be resumed during the next iteration of the event loop.
     * Once the fiber is released, execution switches back to the main fiber.
     *
     * ### Workflow:
     * 1. Retrieve the current fiber using `GetCurrentFiber`.
     * 2. Check if the current fiber is the main fiber:
     *    - If true, the function ignores the release operation and returns immediately, as the main
     *      fiber cannot be paused or queued.
     * 3. Lock the `s_readyMutex` to ensure thread-safe access to the `s_readyFibers` queue.
     * 4. Add the current fiber to the `s_readyFibers` queue.
     * 5. Switch execution back to the main fiber using `SwitchToFiber`.
     *
     * @note This function differs from `yieldFiber` in that the fiber is immediately queued for
     *       re-execution without requiring an explicit call to resume it (e.g., via `unYieldFiber`).
     *
     * @remarks This method is particularly useful when a fiber has completed some work and needs
     *          to temporarily pause to allow other fibers or tasks to execute before resuming.
     *
     * @warning If called from the main fiber, the function exits silently, as the main fiber cannot
     *          participate in cooperative multitasking.
     */
    static void release() {
        SwCoreApplication* app = instance(false);
        if (!app) {
            return;
        }
        app->fiberPool_.releaseCurrent();
        // Retour à la fibre principale
    }

    /**
     * @brief Returns the current generate Yield Id.
     * @return The current generate Yield Id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static int generateYieldId() {
        static std::atomic<int> s_nextYieldCounter{0};
        int value = s_nextYieldCounter.fetch_add(1, std::memory_order_relaxed);
        return -(value + 1);
    }

    /**
     * @brief Pauses the execution of the current fiber and associates it with the given ID.
     *
     * This function is used to yield (pause) the execution of the current fiber by storing it in
     * the `s_yieldedFibers` map, using the provided `id` as the key. Once yielded, the function
     * switches execution back to the main fiber, allowing other fibers or tasks to run.
     *
     * ### Workflow:
     * 1. Retrieve the current fiber using `GetCurrentFiber`.
     * 2. If the current fiber is the main fiber, yielding is ignored since it is not part of the
     *    cooperative multitasking system.
     * 3. If the current fiber is not the main fiber:
     *    - Lock the `s_yieldMutex` to ensure thread-safe access to the `s_yieldedFibers` map.
     *    - Store the current fiber in the map with the specified `id` as the key.
     * 4. Switch execution back to the main fiber using `SwitchToFiber`.
     *
     * @param id The unique identifier to associate with the yielded fiber.
     *
     * @note Yielding allows a fiber to pause its execution until explicitly resumed by calling
     *       `unYieldFiber` with the same `id`.
     *
     * @warning If the main fiber attempts to yield itself, the function will exit without performing
     *          any operation, as yielding is only valid for non-main fibers.
     *
     * @remarks This function is critical for implementing cooperative multitasking, where fibers
     *          voluntarily yield control to enable other fibers or tasks to execute.
     */
    static void yieldFiber(int id) {
        SwCoreApplication* app = instance(false);
        if (!app) {
            return;
        }
        app->fiberPool_.yieldCurrent(id);
    }

    /**
     * @brief Restores a previously yielded fiber to the ready queue for execution.
     *
     * This function handles the transition of a fiber from the "yielded" state back to the "ready"
     * state. It locates the fiber associated with the specified identifier (`id`) in the
     * `s_yieldedFibers` map. If the fiber is found, it is removed from the yielded fibers map and
     * added to the `s_readyFibers` queue. Fibers in the ready queue will be resumed by the main
     * fiber during the event loop execution.
     *
     * ### Workflow:
     * 1. Lock the `s_yieldMutex` to safely access the `s_yieldedFibers` map.
     * 2. Search for the fiber using the provided `id`.
     * 3. If the fiber is found:
     *    - Extract and remove it from the `s_yieldedFibers` map.
     *    - Store the fiber pointer in a local variable.
     * 4. Lock the `s_readyMutex` to safely push the fiber into the `s_readyFibers` queue.
     * 5. If the fiber is not found in `s_yieldedFibers`, no operation is performed.
     *
     * @param id The unique identifier of the fiber to un-yield.
     *
     * @note This function ensures thread safety when modifying shared data structures by using
     *       `std::lock_guard` for both `s_yieldMutex` and `s_readyMutex`.
     *
     * @warning If the `id` provided does not correspond to any fiber in `s_yieldedFibers`, the
     *          function will exit silently without performing any operation.
     *
     * @remarks This function is designed to facilitate cooperative multitasking by enabling
     *          previously paused fibers to rejoin the execution flow. It is typically called
     *          when an external event signals that the fiber should continue its execution.
     */
    static void unYieldFiber(int id) {
        routeYieldWakeAcrossInstances_(id, SwFiberLane::Normal);
    }

    /**
     * @brief Like unYieldFiber(), but enqueues the fiber in a high-priority ready queue.
     *
     * This reduces wakeup latency for fibers waiting on critical events (ex: RPC replies).
     */
    static void unYieldFiberHighPriority(int id) {
        routeYieldWakeAcrossInstances_(id, SwFiberLane::Control);
    }

    /**
     * @brief Retrieves the value of a command-line argument.
     * @param key The key (name) of the argument.
     * @param defaultValue The default value if the argument is not found.
     * @return The value of the argument or `defaultValue` if not defined.
     */
    SwString getArgument(const SwString& key, const SwString& defaultValue = "") const {
        if (parsedArguments.contains(key)) {
            return parsedArguments[key];
        }
        return defaultValue;
    }

    /**
     * @brief Checks for the presence of a command-line argument.
     * @param key The key (name) of the argument.
     * @return `true` if the argument exists, `false` otherwise.
     */
    bool hasArgument(const SwString& key) const {
        return parsedArguments.contains(key);
    }

    /**
     * @brief Retrieves positional command-line arguments (those without a hyphen).
     * @return A list of strings containing the positional arguments.
     */
    SwList<SwString> getPositionalArguments() const {
        SwList<SwString> positionalArgs;
        for (auto it = parsedArguments.begin(); it != parsedArguments.end(); ++it) {
            const SwString& k = it->first;
            if (!k.startsWith("-")) {
                positionalArgs.append(k);
            }
        }
        return positionalArgs;
    }

private:
    static SwCoreApplication* getThreadLocalInstance_() {
#if defined(_WIN32)
        const DWORD idx = sharedTlsIndex_();
        if (idx == TLS_OUT_OF_INDEXES) return nullptr;
        return reinterpret_cast<SwCoreApplication*>(TlsGetValue(idx));
#else
        return threadLocalInstance_();
#endif
    }

    static void setThreadLocalInstance_(SwCoreApplication* app) {
#if defined(_WIN32)
        const DWORD idx = sharedTlsIndex_();
        if (idx == TLS_OUT_OF_INDEXES) return;
        (void)TlsSetValue(idx, app);
#else
        threadLocalInstance_() = app;
#endif
    }

#if defined(_WIN32)
    static DWORD sharedTlsIndex_() {
        static DWORD s_tlsIndex = TLS_OUT_OF_INDEXES;
        static HANDLE s_mapping = nullptr;
        static volatile LONG* s_slot = nullptr;
        if (s_tlsIndex != TLS_OUT_OF_INDEXES) {
            return s_tlsIndex;
        }

        char mapName[128]{};
        (void)std::snprintf(mapName, sizeof(mapName), "Local\\SwCoreApplicationTlsIndexV1_%lu",
                            static_cast<unsigned long>(GetCurrentProcessId()));

        if (!s_mapping) {
            s_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG), mapName);
        }
        if (!s_mapping) {
            s_tlsIndex = TlsAlloc();
            return s_tlsIndex;
        }

        if (!s_slot) {
            s_slot = reinterpret_cast<volatile LONG*>(MapViewOfFile(s_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG)));
        }
        if (!s_slot) {
            s_tlsIndex = TlsAlloc();
            return s_tlsIndex;
        }

        LONG stored = *s_slot;
        if (stored == 0) {
            const DWORD allocated = TlsAlloc();
            if (allocated != TLS_OUT_OF_INDEXES) {
                const LONG desired = static_cast<LONG>(allocated + 1);
                const LONG prev = InterlockedCompareExchange(s_slot, desired, 0);
                if (prev == 0) {
                    stored = desired;
                } else {
                    (void)TlsFree(allocated);
                    stored = prev;
                }
            }
        }

        if (stored != 0) {
            s_tlsIndex = static_cast<DWORD>(stored - 1);
        } else {
            s_tlsIndex = TlsAlloc();
        }

        return s_tlsIndex;
    }
#else
    static SwCoreApplication*& threadLocalInstance_() {
        static thread_local SwCoreApplication* s_threadInstance = nullptr;
        return s_threadInstance;
    }
#endif

    static std::mutex& instanceRegistryMutex() {
        static std::mutex registryMutex;
        return registryMutex;
    }

    static std::map<std::thread::id, SwCoreApplication*>& instanceRegistry() {
        static std::map<std::thread::id, SwCoreApplication*> registry;
        return registry;
    }

    static void bindInstanceToCurrentThread(SwCoreApplication* app) {
        setThreadLocalInstance_(app);
        std::lock_guard<std::mutex> lock(instanceRegistryMutex());
        instanceRegistry()[std::this_thread::get_id()] = app;
    }

    static void unbindInstanceFromCurrentThread(SwCoreApplication* app) {
        if (getThreadLocalInstance_() == app) {
            setThreadLocalInstance_(nullptr);
        }

        std::lock_guard<std::mutex> lock(instanceRegistryMutex());
        auto it = instanceRegistry().find(std::this_thread::get_id());
        if (it != instanceRegistry().end() && it->second == app) {
            instanceRegistry().erase(it);
        }
    }

protected:

    // Fiber scheduling is delegated to SwFiberPool, while watchdog preemption still needs
    // a trampoline on Windows to force execution back to the main fiber context.

    #if defined(_WIN32)
    /**
     * @brief Returns the current trampoline Function.
     * @return The current trampoline Function.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static void __stdcall trampolineFunction() {
        SwCoreApplication* app = instance(false);
        if (!app) {
            return;
        }
        app->m_runningFiber = nullptr;
        SwitchToFiber(app->mainFiber);
        // Ne jamais revenir ici
    }



    /**
     * @brief Performs the `forceBackToMainFiber` operation.
     */
    void forceBackToMainFiber() {
        HANDLE hMainThread = OpenThread(THREAD_ALL_ACCESS, FALSE, mainThreadId);
        if (!hMainThread) {
            swCError(kSwLogCategory_SwCoreApplication) << "OpenThread failed";
            return;
        }

        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_FULL;
        if (SuspendThread(hMainThread) == (DWORD)-1) {
            swCError(kSwLogCategory_SwCoreApplication) << "SuspendThread failed: " << GetLastError();
            CloseHandle(hMainThread);
            return;
        }

        if (!GetThreadContext(hMainThread, &ctx)) {
            swCError(kSwLogCategory_SwCoreApplication) << "GetThreadContext failed: " << GetLastError();
            ResumeThread(hMainThread);
            CloseHandle(hMainThread);
            return;
        }

        // Emulate a normal function call frame before redirecting execution to the
        // trampoline. A plain RIP/EIP patch is not enough: the redirected function
        // expects a valid return slot on the stack and the Windows ABI alignment rules.
        #if defined(_M_X64) || defined(_WIN64)
            ctx.Rsp -= static_cast<DWORD64>(sizeof(std::uintptr_t));
            *reinterpret_cast<std::uintptr_t*>(static_cast<std::uintptr_t>(ctx.Rsp)) = 0;
            ctx.Rip = (DWORD64)&SwCoreApplication::trampolineFunction;
        #elif defined(_M_IX86)
            ctx.Esp -= static_cast<DWORD>(sizeof(std::uintptr_t));
            *reinterpret_cast<std::uintptr_t*>(static_cast<std::uintptr_t>(ctx.Esp)) = 0;
            ctx.Eip = (DWORD)&SwCoreApplication::trampolineFunction;
        #else
            // Ajouter la gestion d'une autre architecture si besoin 
            // ou au moins un commentaire pour savoir qu'on ne supporte pas
            // cette architecture par exemple:
            #error "Architecture non supportée pour la modification du contexte"
        #endif

        if (!SetThreadContext(hMainThread, &ctx)) {
            swCError(kSwLogCategory_SwCoreApplication) << "SetThreadContext failed: " << GetLastError();
        }

        if (ResumeThread(hMainThread) == (DWORD)-1) {
            swCError(kSwLogCategory_SwCoreApplication) << "ResumeThread failed: " << GetLastError();
        }

        CloseHandle(hMainThread);
    }
#else
    /**
     * @brief Performs the `forceBackToMainFiber` operation.
     */
    void forceBackToMainFiber() {
        // Best-effort async yield using a per-thread signal (see unixWatchdogPreemptSignalHandler_).
        (void)pthread_kill(mainThreadPthread_, unixWatchdogSignalNumber_());
    }
#endif


    /**
     * @brief Performs the `watchdogLoop` operation.
     */
    void watchdogLoop() {
        using namespace std::chrono;
        while (watchdogRunning.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(milliseconds(5));
            if (!watchdogRunning.load(std::memory_order_relaxed)) {
                break;
            }
            LPVOID runningFiber = m_runningFiber.load(std::memory_order_relaxed);
            if (runningFiber) {
                const int64_t startNs = fiberStartTimeNs_.load(std::memory_order_relaxed);
                if (startNs == 0) continue;
                const int64_t nowNs = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
                const int64_t elapsedMs = (nowNs - startNs) / 1000000;
                if (elapsedMs > 10) {
                    // Fibre bloquante détectée !
                    bool expected = false;
                    if (!fireWatchDog.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                        continue;
                    }
                    forceBackToMainFiber();
                }
            }
        }
    }
    /**
     * @brief Enables high-precision timers using the Windows multimedia timer.
     */
    void enableHighPrecisionTimers() {
#if defined(_WIN32)
        // Load winmm.dll and keep it loaded for the lifetime of the process.
        // Calling FreeLibrary after timeBeginPeriod can undo the resolution
        // change on some Windows 11 builds, reverting to the 15.6ms default.
        if (!hWinMM_) {
            hWinMM_ = LoadLibrary(TEXT("winmm.dll"));
        }
        if (hWinMM_) {
            auto timeBeginPeriodFunc = (MMRESULT(WINAPI*)(UINT))GetProcAddress(hWinMM_, "timeBeginPeriod");
            if (timeBeginPeriodFunc) {
                MMRESULT result = timeBeginPeriodFunc(1);
                if (result != TIMERR_NOERROR) {
                    swCError(kSwLogCategory_SwCoreApplication) << "Failed to enable high precision timers. Code: " << result;
                }
            }
        }
#endif
    }

    /**
     * @brief Disables high-precision timers using the Windows multimedia timer.
     */
    void disableHighPrecisionTimers() {
#if defined(_WIN32)
        if (hWinMM_) {
            auto timeEndPeriodFunc = (MMRESULT(WINAPI*)(UINT))GetProcAddress(hWinMM_, "timeEndPeriod");
            if (timeEndPeriodFunc) {
                timeEndPeriodFunc(1);
            }
            FreeLibrary(hWinMM_);
            hWinMM_ = nullptr;
        }
#endif
    }

    /**
     * @brief Sets a high thread priority for the current thread.
     */
    void setHighThreadPriority() {
#if defined(_WIN32)
        HANDLE thread = GetCurrentThread();
        SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
#endif
    }

    /**
     * @brief Parses command-line arguments and stores them.
     * @param argc Number of arguments.
     * @param argv Array of argument strings.
     */
    void parseArguments(int argc, char* argv[]) {
        SwString lastKey = "";
        for (int i = 1; i < argc; ++i) {
            SwString arg = argv[i];
            if (arg.startsWith("--")) {
                auto eqPos = arg.indexOf('=');
                if (eqPos != -1) {
                    SwString key = arg.mid(2, eqPos - 2);
                    SwString value = arg.mid(eqPos + 1);
                    parsedArguments[key] = value;
                    lastKey = key;
                } else {
                    SwString key = arg.mid(2);
                    SwString value = "";
                    if (i + 1 < argc && !SwString(argv[i + 1]).startsWith("-")) {
                        value = argv[++i];
                    }
                    parsedArguments[key] = value;
                    lastKey = key;
                }
            } else if (arg.startsWith("-")) {
                SwString key = arg.mid(1);
                SwString value = "";
                if (i + 1 < argc && !SwString(argv[i + 1]).startsWith("-")) {
                    value = argv[++i];
                }
                parsedArguments[key] = value;
                lastKey = key;
            } else {
                if (!lastKey.isEmpty()) {
                    parsedArguments[lastKey] += SwString(" ") + arg;
                }
            }
        }
    }

    /**
     * @brief Converts the main thread to a fiber.
     *
     * If the conversion fails, an error message is printed.
     */
    void initFibers() {
#if defined(_WIN32)
        if (IsThreadAFiber()) {
            mainFiber = GetCurrentFiber();
            return;
        }
#endif
        mainFiber = ConvertThreadToFiber(nullptr);
        if (!mainFiber) {
#if defined(_WIN32)
            const DWORD err = GetLastError();
            // If the thread is already a fiber, reuse the current fiber handle.
            // This can happen if another subsystem converted the thread earlier.
            if (err == ERROR_ALREADY_FIBER) {
                mainFiber = GetCurrentFiber();
                return;
            }
#endif
#if defined(_WIN32)
            swCError(kSwLogCategory_SwCoreApplication) << "Failed to convert main thread to fiber. Error: " << err;
#else
            swCError(kSwLogCategory_SwCoreApplication) << "Failed to convert main thread to fiber.";
#endif
        }
    }

    /**
     * @brief Entry point for a newly created fiber.
     *
     * Executes the function passed as a parameter to the fiber and
     * switches back to the main fiber upon completion.
     *
     * @param lpParameter Pointer to the function to execute within the fiber.
     */
    static void executeCallbackSafely_(const std::function<void()>& callback) {
        try {
            callback();
        } catch (const std::exception& e) {
            swCError(kSwLogCategory_SwCoreApplication)
                << "Unhandled exception in SwCoreApplication callback: " << e.what();
        } catch (...) {
            swCError(kSwLogCategory_SwCoreApplication)
                << "Unhandled unknown exception in SwCoreApplication callback";
        }
    }

    /**
     * @brief Processes timers, executing callbacks for ready timers, and calculates the time
     *        until the next timer is ready.
     *
     * This function iterates through a list of timers, executes the callbacks for those that are ready,
     * and determines the minimum time remaining until the next timer becomes ready.
     *
     * @param timers Map of timers where the key is the timer ID and the value is a pointer to the timer object.
     * @return The time in microseconds until the next timer is ready, or the maximum possible integer
     *         if no timers are active.
     */
    int processTimers() {
        ++processingTimersDepth_;

        // Timer callbacks can start/stop other timers, which mutates `timers`.
        // Build a snapshot of ready timers under a single lock, then execute
        // callbacks outside the lock to avoid holding it during user code.
        struct ReadyTimer { _T* timer; bool singleShot; };
        std::vector<ReadyTimer> readyTimers;
        int minTimeUntilNext = (std::numeric_limits<int>::max)();

        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            for (auto it = timers.begin(); it != timers.end(); ) {
                _T* currentTimer = it->second;
                if (!currentTimer) {
                    ++it;
                    continue;
                }
                if (currentTimer->isReady()) {
                    if (currentTimer->dispatchPending) {
                        ++it;
                        continue;
                    }
                    currentTimer->dispatchPending = true;
                    bool isSingleShot = currentTimer->singleShot;
                    readyTimers.push_back({currentTimer, isSingleShot});
                    if (isSingleShot) {
                        it = timers.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }

        // Execute callbacks outside the lock.
        for (auto& rt : readyTimers) {
            const SwFiberLane timerLane = rt.timer->lane;
            std::shared_ptr<SwRuntimeProfilerSession> timerProfilerSession = profilerSession_;
            if (rt.singleShot) {
                _T* toDelete = rt.timer;
                std::function<void()> timerEvent = [toDelete, timerProfilerSession, timerLane]() {
                    if (timerProfilerSession) {
                        timerProfilerSession->bindToCurrentThread();
                    }
                    SwRuntimeScopedSpan timerScope(timerProfilerSession &&
                                                       timerProfilerSession->autoRuntimeScopesEnabled()
                                                       ? timerProfilerSession.get()
                                                       : nullptr,
                                                   SwRuntimeTimingKind::Timer,
                                                   "timer",
                                                   timerLane,
                                                   true);
                    toDelete->dispatchPending = false;
                    if (!toDelete->cancelled) {
                        toDelete->execute();
                    }
                    delete toDelete;
                };
                postEventOnLaneImpl_(std::move(timerEvent), timerLane, false);
            } else {
                _T* t = rt.timer;
                std::function<void()> timerEvent = [t, timerProfilerSession, timerLane]() {
                    if (timerProfilerSession) {
                        timerProfilerSession->bindToCurrentThread();
                    }
                    SwRuntimeScopedSpan timerScope(timerProfilerSession &&
                                                       timerProfilerSession->autoRuntimeScopesEnabled()
                                                       ? timerProfilerSession.get()
                                                       : nullptr,
                                                   SwRuntimeTimingKind::Timer,
                                                   "timer",
                                                   timerLane,
                                                   true);
                    t->dispatchPending = false;
                    if (!t->cancelled) {
                        t->execute();
                    }
                };
                postEventOnLaneImpl_(std::move(timerEvent), timerLane, false);
            }
        }

        // Compute next wake-up under a single lock.
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            for (const auto& kv : timers) {
                _T* currentTimer = kv.second;
                if (!currentTimer) {
                    continue;
                }
                const int timeUntilNext = currentTimer->timeUntilReady();
                if (timeUntilNext < minTimeUntilNext) {
                    minTimeUntilNext = timeUntilNext;
                }
            }

            if (--processingTimersDepth_ == 0 && !pendingTimerDeletes_.isEmpty()) {
                SwList<_T*> stillPendingDeletes;
                for (size_t i = 0; i < pendingTimerDeletes_.size(); ++i) {
                    _T* pendingDelete = pendingTimerDeletes_[i];
                    if (!pendingDelete) {
                        continue;
                    }
                    if (pendingDelete->dispatchPending) {
                        stillPendingDeletes.push_back(pendingDelete);
                    } else {
                        delete pendingDelete;
                    }
                }
                pendingTimerDeletes_ = stillPendingDeletes;
            }
        }

        return minTimeUntilNext;
    }


    /**
     * @brief Retrieves the currently running fiber.
     * @return Pointer to the currently running fiber.
     */
    LPVOID getRunningFiber() {
        return m_runningFiber.load(std::memory_order_relaxed);
    }

protected:
    std::atomic<bool> running; ///< Indicates if the event loop is running.
    std::atomic<int> exitCode; ///< Exit code of the application.
#if defined(_WIN32)
    HANDLE mainThreadHandle;
    DWORD mainThreadId;
#else
    pthread_t mainThreadPthread_{};
#endif

#if defined(_WIN32)
    HMODULE hWinMM_ = nullptr; ///< winmm.dll handle kept alive for timeBeginPeriod.
#endif
    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning{ false };
    std::atomic<bool> fireWatchDog{ false };
    std::atomic<int64_t> fiberStartTimeNs_{0};
    SwFiberPool fiberPool_;

    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::queue<std::function<void()>> eventQueue; ///< Queue of events to process.
    std::deque<PostedObjectEvent_> postedEventQueue_; ///< Object-event queue.
    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::queue<std::function<void()>> priorityEventQueue; ///< High-priority event queue.
    std::deque<PostedObjectEvent_> priorityPostedEventQueue_; ///< High-priority object-event queue.
    std::vector<SwObject*> applicationEventFilters_; ///< Application-wide event filters.
    std::mutex eventQueueMutex; ///< Mutex protecting access to the event queue.
    std::condition_variable cv; ///< Condition variable for event waiting.

    struct IterationMeasurement {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t busyMicroseconds;
        uint64_t totalMicroseconds;
    };
    // Queue des mesures sur la dernière seconde environ
    std::deque<IterationMeasurement> measurements;
    mutable std::mutex measurementsMutex_; ///< Protects measurements, totalBusyTimeMicroseconds, totalTimeMicroseconds.

    uint64_t totalBusyTimeMicroseconds = 0;
    uint64_t totalTimeMicroseconds = 0;

    // Cette variable sera mise à jour dans runEventInFiber et resumeReadyFibers
    // pour accumuler le temps occupé dans la fibre sur l'itération en cours.
    uint64_t busyElapsedIteration = 0;
    std::atomic<unsigned int> eventFiberStackSize_{0};

    int nextTimerId = 0; ///< Identifier for the next timer to be created.
    SwMap<int, _T*> timers; ///< Map associating timer IDs with their respective _T objects.

    int processingTimersDepth_ = 0;
    SwList<_T*> pendingTimerDeletes_;
    SwMap<SwString, SwString> parsedArguments; ///< Parsed command-line arguments.
    std::shared_ptr<SwRuntimeProfilerSession> profilerSession_;

    std::atomic<LPVOID> m_runningFiber{nullptr}; ///< Pointer to the currently running fiber.
    LPVOID mainFiber = nullptr; ///< Pointer to the main fiber.

private:
    void initWakeup_() {
#if defined(_WIN32)
        wakeEvent_ = ::CreateEventA(NULL, FALSE, FALSE, NULL);
#else
        wakeEventFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#endif
    }

    void shutdownWakeup_() {
#if defined(_WIN32)
        if (wakeEvent_) {
            ::CloseHandle(wakeEvent_);
            wakeEvent_ = NULL;
        }
#else
        if (wakeEventFd_ >= 0) {
            ::close(wakeEventFd_);
            wakeEventFd_ = -1;
        }
#endif
    }

    void signalWakeup_() {
#if defined(_WIN32)
        if (wakeEvent_) ::SetEvent(wakeEvent_);
#else
        if (wakeEventFd_ >= 0) {
            const uint64_t one = 1;
            const ssize_t n = ::write(wakeEventFd_, &one, sizeof(one));
            (void)n;
        }
#endif
    }

    void waitForWork_(int timeoutUs) {
        if (!running) return;

#if defined(_WIN32)
        std::vector<HANDLE> handles;
        std::vector<std::function<void()>> callbacks;
        handles.reserve(1);
        callbacks.reserve(1);

        handles.push_back(wakeEvent_);
        callbacks.push_back(std::function<void()>{});

        {
            SwMutexLocker lk(waitablesMutex_);
            for (auto& kv : waitHandles_) {
                handles.push_back(kv.second.handle);
                callbacks.push_back(kv.second.cb);
            }
        }

        HANDLE timeoutTimer = NULL;
        DWORD timeoutMs = INFINITE;
        if (timeoutUs >= 0) {
            timeoutMs = static_cast<DWORD>((timeoutUs + 999) / 1000);

            timeoutTimer = ::CreateWaitableTimerExW(NULL,
                                                    NULL,
                                                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                    TIMER_ALL_ACCESS);
            if (!timeoutTimer) {
                timeoutTimer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
            }

            if (timeoutTimer) {
                LARGE_INTEGER dueTime;
                const LONGLONG clampedTimeoutUs = timeoutUs > 0 ? static_cast<LONGLONG>(timeoutUs) : 1LL;
                dueTime.QuadPart = -(clampedTimeoutUs * 10LL);
                if (::SetWaitableTimer(timeoutTimer, &dueTime, 0, NULL, NULL, FALSE)) {
                    handles.push_back(timeoutTimer);
                    callbacks.push_back(std::function<void()>{});
                    timeoutMs = INFINITE;
                } else {
                    ::CloseHandle(timeoutTimer);
                    timeoutTimer = NULL;
                }
            }
        }

        // Limit: WaitForMultipleObjects supports up to MAXIMUM_WAIT_OBJECTS (typically 64).
        if (handles.empty()) return;
        if (handles.size() > MAXIMUM_WAIT_OBJECTS) {
            // Best-effort: fall back to sleeping if too many waitables.
            if (timeoutUs > 0) std::this_thread::sleep_for(std::chrono::microseconds(timeoutUs));
            return;
        }

        const DWORD r = ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                 handles.data(),
                                                 FALSE,
                                                 timeoutMs);
        if (timeoutTimer) {
            ::CancelWaitableTimer(timeoutTimer);
            ::CloseHandle(timeoutTimer);
        }
        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + handles.size()) {
            const size_t idx = static_cast<size_t>(r - WAIT_OBJECT_0);
            if (idx > 0 && idx < callbacks.size() && callbacks[idx]) {
                postEventOnLane(callbacks[idx], SwFiberLane::Control);
                (void)runFiberPoolWork_();
            }
            return;
        }
        return;
#else
        std::vector<pollfd> fds;
        std::vector<std::function<void()>> callbacks;
        fds.reserve(2);
        callbacks.reserve(2);

        pollfd wakePfd;
        wakePfd.fd = wakeEventFd_;
        wakePfd.events = POLLIN;
        wakePfd.revents = 0;
        fds.push_back(wakePfd);
        callbacks.push_back(std::function<void()>{});

        const int termFd = unixTerminateEventFd_();
        const bool hasTermFd = (termFd >= 0);
        if (hasTermFd) {
            pollfd termPfd;
            termPfd.fd = termFd;
            termPfd.events = POLLIN;
            termPfd.revents = 0;
            fds.push_back(termPfd);
            callbacks.push_back(std::function<void()>{});
        }

        {
            SwMutexLocker lk(waitablesMutex_);
            for (auto& kv : waitFds_) {
                pollfd p;
                p.fd = kv.second.fd;
                p.events = POLLIN;
                p.revents = 0;
                fds.push_back(p);
                callbacks.push_back(kv.second.cb);
            }
        }

        // Use timerfd for high-resolution sleep (hrtimer, ~1ms granularity)
        // instead of poll() timeout which depends on CONFIG_HZ (often 10ms).
        int timerFd = -1;
        if (timeoutUs >= 0) {
            timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
            if (timerFd >= 0) {
                struct itimerspec its{};
                its.it_value.tv_sec  = timeoutUs / 1000000;
                its.it_value.tv_nsec = (timeoutUs % 1000000) * 1000L;
                if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0)
                    its.it_value.tv_nsec = 1; // 0,0 would disarm the timer
                ::timerfd_settime(timerFd, 0, &its, nullptr);
                pollfd tpfd;
                tpfd.fd = timerFd;
                tpfd.events = POLLIN;
                tpfd.revents = 0;
                fds.push_back(tpfd);
                callbacks.push_back(std::function<void()>{});
            }
        }

        // If timerfd was created, poll indefinitely (timerfd handles the timeout).
        // Otherwise fall back to poll() timeout (lower resolution but functional).
        const int pollTimeout = (timerFd >= 0) ? -1
                              : (timeoutUs < 0) ? -1
                              : static_cast<int>((timeoutUs + 999) / 1000);
        const int r = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), pollTimeout);

        // Close the one-shot timerfd now that poll() has returned.
        if (timerFd >= 0) ::close(timerFd);

        if (r <= 0) return;

        // Drain wake eventfd if needed.
        if (fds[0].revents & POLLIN) {
            uint64_t v = 0;
            while (true) {
                const ssize_t n = ::read(wakeEventFd_, &v, sizeof(v));
                if (n == sizeof(v)) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                break;
            }
        }

        size_t base = 1;
        if (hasTermFd) {
            if (fds[1].revents & POLLIN) {
                uint64_t v = 0;
                while (true) {
                    const ssize_t n = ::read(termFd, &v, sizeof(v));
                    if (n == sizeof(v)) continue;
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                    break;
                }
                (void)SwCoreApplication::requestQuitAllInstances();
                return;
            }
            base = 2;
        }

        for (size_t i = base; i < fds.size(); ++i) {
            if (fds[i].revents & POLLIN) {
                if (callbacks[i]) {
                    postEventOnLane(callbacks[i], SwFiberLane::Control);
                    (void)runFiberPoolWork_();
                }
            }
        }
#endif
    }

#if defined(_WIN32)
    struct WaitHandleEntry {
        HANDLE handle{NULL};
        /**
         * @brief Returns the current function<void.
         * @return The current function<void.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        std::function<void()> cb;

        /**
         * @brief Constructs a `WaitHandleEntry` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        WaitHandleEntry() = default;
        /**
         * @brief Constructs a `WaitHandleEntry` instance.
         * @param h Height value.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        WaitHandleEntry(HANDLE h, std::function<void()> cb_) : handle(h), cb(std::move(cb_)) {}
    };
#endif
    struct WaitFdEntry {
        int fd{-1};
        /**
         * @brief Returns the current function<void.
         * @return The current function<void.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        std::function<void()> cb;

        /**
         * @brief Constructs a `WaitFdEntry` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        WaitFdEntry() = default;
        /**
         * @brief Constructs a `WaitFdEntry` instance.
         * @param fd_ Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        WaitFdEntry(int fd_, std::function<void()> cb_) : fd(fd_), cb(std::move(cb_)) {}
    };

    SwMutex waitablesMutex_;
    size_t nextWaitableId_{1};

#if defined(_WIN32)
    HANDLE wakeEvent_{NULL};
    SwMap<size_t, WaitHandleEntry> waitHandles_;
#else
    int wakeEventFd_{-1};
    SwMap<size_t, WaitFdEntry> waitFds_;
#endif
};

/**
 * @brief Handles console control events.
 *
 * This function is called when a control signal is sent to the console.
 * It gracefully handles various events such as closure, shutdown,
 * or an interruption (Ctrl+C).
 *
 * @param ctrlType The type of control event sent to the console. Possible values are:
 *                 - `CTRL_CLOSE_EVENT`: Console close event.
 *                 - `CTRL_C_EVENT`: Ctrl+C signal.
 *                 - `CTRL_BREAK_EVENT`: Ctrl+Break signal.
 *                 - `CTRL_LOGOFF_EVENT`: User logoff.
 *                 - `CTRL_SHUTDOWN_EVENT`: System shutdown.
 * @return BOOL Returns `TRUE` if the event was successfully handled, otherwise `FALSE`.
 */
#if defined(_WIN32)
static BOOL WINAPI ConsoleHandler(DWORD ctrlType) {
    switch (ctrlType) {
    case CTRL_CLOSE_EVENT:
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    {
        // Arrêt propre de l'application
        const bool requested = SwCoreApplication::requestQuitAllInstances();
            // Appeler quit pour stopper la boucle d'événements proprement
        // On retourne TRUE pour indiquer qu'on a géré l'événement
        // If no SwCoreApplication is registered, keep the default OS behavior (terminate).
        return requested ? TRUE : FALSE;
    }
    default:
        return FALSE;
    }
}
#else
// Unix signal handlers are installed by SwCoreApplication::installUnixSignalHandlersOnce_().
#endif

#include "SwObject.h"

#endif // SW_CORE_RUNTIME_SWCOREAPPLICATION_H
