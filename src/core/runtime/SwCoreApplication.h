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
#include <chrono>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <limits>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <vector>
static constexpr const char* kSwLogCategory_SwCoreApplication = "sw.core.runtime.swcoreapplication";


#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include "platform/win/SwWindows.h"
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
    _T(std::function<void()> callback, int interval, bool singleShot = false)
        : callback(callback),
        interval(interval),
        singleShot(singleShot),
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
         // Note: On modern x86_64 toolchains with CET enabled (shadow stacks / IBT),
         // returning from the signal handler with a manually patched ucontext can
         // crash due to inconsistent shadow-stack state. Using setcontext() here
         // lets libc restore all required state for the target context.
         swcore::linux_fiber::currentFiberRef() = mainFiber;
         ucontext_t mainCtx = mainFiber->context;
         (void)::sigdelset(&mainCtx.uc_sigmask, unixWatchdogSignalNumber_());
         (void)::setcontext(&mainCtx);
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
            struct sigaction wd {};
            wd.sa_flags = SA_SIGINFO;
            wd.sa_sigaction = &SwCoreApplication::unixWatchdogPreemptSignalHandler_;
            (void)::sigemptyset(&wd.sa_mask);
            (void)::sigaction(unixWatchdogSignalNumber_(), &wd, nullptr);
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
        desactiveWatchDog();
        cleanupAllFibers_();
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

    /**
     * @brief Posts an event (a function) to the event queue.
     * @param event Function to execute during event processing.
     */
    void postEvent(std::function<void()> event) {
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            eventQueue.push(event);
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
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            priorityEventQueue.push(event);
        }
        cv.notify_one();
        signalWakeup_();
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

    /**
     * @brief Adds a timer.
     * @param callback Function to call when the timer expires.
     * @param interval Interval in microseconds between two executions of the callback.
     * @return The identifier of the created timer.
     */
    int addTimer(std::function<void()> callback, int interval, bool singleShot = false) {
        int timerId = nextTimerId++;
        timers.insert(timerId, new _T(callback, interval, singleShot));
        signalWakeup_();
        return timerId;
    }

    /**
     * @brief Removes a timer.
     * @param timerId Identifier of the timer to remove.
     */
    void removeTimer(int timerId) {
        auto it = timers.find(timerId);
        if (it != timers.end()) {
            _T* toDelete = it->second;
            timers.erase(it);
            if (processingTimersDepth_ > 0) {
                pendingTimerDeletes_.push_back(toDelete);
            } else {
                delete toDelete;
            }
        }
        auto itOld = timers.find(-1);
        if (itOld != timers.end()) {
            _T* toDelete = itOld->second;
            timers.erase(itOld);
            if (processingTimersDepth_ > 0) {
                pendingTimerDeletes_.push_back(toDelete);
            } else {
                delete toDelete;
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
        auto startTime = std::chrono::steady_clock::now();
        auto lastTime = startTime;

        while (running) {
            // Avant de traiter un nouvel événement, on remet à zéro le temps occupé de l'itération
            busyElapsedIteration = 0;

            auto currentTime = std::chrono::steady_clock::now();
            int sleepDuration = processEvent();

            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastTime).count();
            lastTime = currentTime;

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

            auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - startTime).count();
            if (maxDurationMicroseconds != 0 && totalElapsed >= maxDurationMicroseconds) {
                break;
            }

            if (!running) break;
            if (sleepDuration != 0) {
                waitForWork_(sleepDuration);
            }
        }
        return exitCode;
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
        if (priorityEventQueue.empty() && eventQueue.empty() && timers.empty() && waitForEvent) {
            cv.wait(lock);
        }

        // Process the next event if available
        if (!priorityEventQueue.empty() || !eventQueue.empty()) {
            std::function<void()> event;
            if (!priorityEventQueue.empty()) {
                event = priorityEventQueue.front();
                priorityEventQueue.pop();
            } else {
                event = eventQueue.front();
                eventQueue.pop();
            }
            lock.unlock(); // Unlock before running the event in a fiber
             runEventInFiber(event);
             // Run any ready fibers immediately (avoids extra loop latency after wakeups).
             resumeReadyFibers();
             return 0; // An event was processed, so no delay is required
         }
        lock.unlock();

        // Process timer and get ne next Rendez-vous
        int minTimeUntilNext = processTimers();

         // Resume fibers that are ready to run
         resumeReadyFibers();

        // If fibers are still queued, don't sleep.
        {
            SwMutexLocker lk(getReadyMutex());
            if (!getReadyFibersHi().empty() || !getReadyFibers().empty()) {
                return 0;
            }
        }

        if (!priorityEventQueue.empty() || !eventQueue.empty()) {
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
        return !priorityEventQueue.empty() || !eventQueue.empty() || !timers.empty();
    }

    /**
     * @brief Exits the application with a specified exit code.
     * @param code Exit code.
     */
    void exit(int code = 0) {
        exitCode = code;
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

        DWORD timeoutMs = INFINITE;
        if (timeoutUs >= 0) {
            timeoutMs = static_cast<DWORD>((timeoutUs + 999) / 1000);
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
                runEventInFiber(callbacks[idx]);
                resumeReadyFibers();
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
        LPVOID current = GetCurrentFiber();
        if (current == app->mainFiber) {
            // Not in the event loop; ignore yielding
            return;
        }
        {
            SwMutexLocker lock(getReadyMutex());
            getReadyFibers().push(current);
        }

        // Retour à la fibre principale
        // The fiber is no longer executing once we switch back to the main fiber.
        app->m_runningFiber = nullptr;
        SwitchToFiber(app->mainFiber);
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
        LPVOID current = GetCurrentFiber();
        if (current == app->mainFiber) {
            // Not in the event loop; ignore yielding
            return;
        }
        // Store the current fiber in the yielded fibers map
        {
            SwMutexLocker lock(getYieldMutex());
            getYieldedFibers()[id] = current;
        }

        // Switch execution back to the main fiber
        // The fiber is no longer executing once we switch back to the main fiber.
        app->m_runningFiber = nullptr;
        SwitchToFiber(app->mainFiber);
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
        LPVOID fiber = nullptr;
        {
            SwMutexLocker lock(getYieldMutex());
            auto it = getYieldedFibers().find(id);
            if (it != getYieldedFibers().end()) {
                fiber = it->second;
                getYieldedFibers().erase(it);
            }
        }

        if (fiber) {
            SwMutexLocker lock(getReadyMutex());
            getReadyFibers().push(fiber);
        }
    }

    /**
     * @brief Like unYieldFiber(), but enqueues the fiber in a high-priority ready queue.
     *
     * This reduces wakeup latency for fibers waiting on critical events (ex: RPC replies).
     */
    static void unYieldFiberHighPriority(int id) {
        LPVOID fiber = nullptr;
        {
            SwMutexLocker lock(getYieldMutex());
            auto it = getYieldedFibers().find(id);
            if (it != getYieldedFibers().end()) {
                fiber = it->second;
                getYieldedFibers().erase(it);
            }
        }

        if (fiber) {
            SwMutexLocker lock(getReadyMutex());
            getReadyFibersHi().push(fiber);
        }
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

    // Fibers are thread-affine on Windows: a fiber created on one OS thread must only be resumed on that same thread.
    // These queues/maps are therefore per-thread (thread_local). This also avoids cross-thread contention.
    /**
     * @brief Returns the current yield Mutex.
     * @return The current yield Mutex.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwMutex& getYieldMutex() {
         static thread_local SwMutex s_yieldMutex;
         return s_yieldMutex;
     }

     /**
      * @brief Returns the current yielded Fibers.
      * @return The current yielded Fibers.
      *
      * @details The returned value reflects the state currently stored by the instance.
      */
     static std::map<int, LPVOID>& getYieldedFibers() {
         static thread_local std::map<int, LPVOID> s_yieldedFibers;
         return s_yieldedFibers;
     }

     /**
      * @brief Returns the current ready Mutex.
      * @return The current ready Mutex.
      *
      * @details The returned value reflects the state currently stored by the instance.
      */
     static SwMutex& getReadyMutex() {
         static thread_local SwMutex s_readyMutex;
         return s_readyMutex;
     }

     /**
      * @brief Returns the current ready Fibers.
      * @return The current ready Fibers.
      *
      * @details The returned value reflects the state currently stored by the instance.
      */
     static std::queue<LPVOID>& getReadyFibers() {
         static thread_local std::queue<LPVOID> s_readyFibers;
         return s_readyFibers;
     }

     /**
      * @brief Returns the current ready Fibers Hi.
      * @return The current ready Fibers Hi.
      *
      * @details The returned value reflects the state currently stored by the instance.
      */
     static std::queue<LPVOID>& getReadyFibersHi() {
         static thread_local std::queue<LPVOID> s_readyFibersHi;
         return s_readyFibersHi;
     }

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
        HMODULE hWinMM = LoadLibrary(TEXT("winmm.dll"));
        if (hWinMM) {
            auto timeBeginPeriodFunc = (MMRESULT(WINAPI*)(UINT))GetProcAddress(hWinMM, "timeBeginPeriod");
            if (timeBeginPeriodFunc) {
                MMRESULT result = timeBeginPeriodFunc(1);
                if (result != TIMERR_NOERROR) {
                    swCError(kSwLogCategory_SwCoreApplication) << "Failed to enable high precision timers. Code: " << result;
                }
            }
            FreeLibrary(hWinMM);
        }
#endif
    }

    /**
     * @brief Disables high-precision timers using the Windows multimedia timer.
     */
    void disableHighPrecisionTimers() {
#if defined(_WIN32)
        HMODULE hWinMM = LoadLibrary(TEXT("winmm.dll"));
        if (hWinMM) {
            auto timeEndPeriodFunc = (MMRESULT(WINAPI*)(UINT))GetProcAddress(hWinMM, "timeEndPeriod");
            if (timeEndPeriodFunc) {
                timeEndPeriodFunc(1);
            }
            FreeLibrary(hWinMM);
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
    static VOID WINAPI FiberProc(LPVOID lpParameter) {
        std::function<void()>* callback = reinterpret_cast<std::function<void()>*>(lpParameter);

        // Execute the callback function. Once we return from this function call,
        // we know that the callback has fully completed its execution, including
        // any yields that might have occurred during its lifetime.
        (*callback)();

        // At this point, the callback is guaranteed to have finished, so we can
        // safely delete the allocated std::function. By managing the lifetime
        // here, we ensure that the callback's memory is only freed after it
        // truly completes, preventing the risk of double deletes or accessing
        // deallocated memory if it had simply yielded instead of fully returning.
        delete callback;
        instance()->m_runningFiber = nullptr;
        // Finally, return control to the main fiber.
        SwitchToFiber(instance()->mainFiber);
    }


    /**
     * @brief Executes a given event (function) within a fiber and manages its lifecycle.
     *
     * This function creates a new fiber to execute the provided event (function), switches
     * to the fiber to run it, and ensures proper cleanup of the fiber after execution.
     *
     * Steps:
     * 1. Allocates a new fiber with `CreateFiber` and passes the event function as a parameter.
     * 2. Switches to the newly created fiber using `SwitchToFiber` to execute the event.
     * 3. After execution or yielding, the function ensures the fiber is properly deleted
     *    using `deleteFiberIfNeeded`.
     *
     * @param event The function to execute within the fiber.
     *
     * @note If fiber creation fails, the event is executed synchronously in the current thread,
     *       and the function logs an error message.
     *
     * @warning The caller must ensure that the event does not hold any references to objects
     *          that may become invalid during fiber execution.
     *
     * @exception None. Errors during fiber creation are logged but do not throw exceptions.
     *
     * @remarks Fibers are a cooperative multitasking construct, so the event is expected to
     *          yield or complete its execution without blocking other operations indefinitely.
     */
    void runEventInFiber(const std::function<void()>& event) {
        auto startBusy = std::chrono::steady_clock::now();
        std::function<void()>* cbPtr = new std::function<void()>(event);
        const SIZE_T configuredStackSize = static_cast<SIZE_T>(eventFiberStackSize_.load(std::memory_order_relaxed));
        LPVOID newFiber = CreateFiber(configuredStackSize, FiberProc, cbPtr);
        if (!newFiber) {
#if defined(_WIN32)
            swCError(kSwLogCategory_SwCoreApplication) << "Failed to create fiber. Error: " << GetLastError();
#else
            swCError(kSwLogCategory_SwCoreApplication) << "Failed to create fiber.";
#endif
            (*cbPtr)();
            delete cbPtr;
            return;
        }
        
        safeRunningFiber(newFiber);

        // Calcul du temps occupé dans cette opération
        auto endBusy = std::chrono::steady_clock::now();
        auto busyElapsed = std::chrono::duration_cast<std::chrono::microseconds>(endBusy - startBusy).count();
        busyElapsedIteration += (uint64_t)busyElapsed;
    }

    /**
     * @brief Resumes fibers that are ready to run.
     *
     * This function processes the queue of ready fibers (`s_readyFibers`) and resumes their execution
     * one by one using `SwitchToFiber`. Each fiber is executed until it either completes or yields
     * again. The function ensures that no fiber is resumed more than once during the same cycle.
     *
     * The function follows these steps:
     * 1. Retrieves a fiber from the `s_readyFibers` queue.
     * 2. Checks if the fiber has already been resumed during the current cycle using `resumedThisCycle`.
     *    - If it has, the fiber is requeued, and the function exits to avoid infinite loops.
     * 3. If the fiber has not been resumed, it is marked as resumed and executed using `SwitchToFiber`.
     * 4. After execution, checks if the fiber needs to be deleted using `deleteFiberIfNeeded`.
     *
     * @note This function uses thread safety mechanisms (mutex locks) to ensure consistent access
     *       to the `s_readyFibers` queue.
     *
     * @note A fiber that has already been resumed in the current cycle is requeued for execution
     *       in subsequent cycles of the event loop.
     */
     void resumeReadyFibers() {
         if (!running.load(std::memory_order_relaxed)) {
             return;
         }
         std::unordered_set<LPVOID> resumedThisCycle;
         auto startBusy = std::chrono::steady_clock::now();
         while (true) {
             if (!running.load(std::memory_order_relaxed)) {
                 break;
             }
             LPVOID fiber = nullptr;
             bool fromHi = false;
            {
                SwMutexLocker lock(getReadyMutex());
                if (!getReadyFibersHi().empty()) {
                    fiber = getReadyFibersHi().front();
                    getReadyFibersHi().pop();
                    fromHi = true;
                } else if (!getReadyFibers().empty()) {
                    fiber = getReadyFibers().front();
                    getReadyFibers().pop();
                } else {
                    break; // No more fibers to resume
                }
            }

            // Check if the fiber has already been resumed during this cycle
            if (resumedThisCycle.find(fiber) != resumedThisCycle.end()) {
                // Fiber has already been executed this cycle, requeue it for later
                {
                    SwMutexLocker lock(getReadyMutex());
                    if (fromHi) getReadyFibersHi().push(fiber);
                    else getReadyFibers().push(fiber);
                }
                // Exit this cycle; the fiber will be retried in the next processEvent() call.
                break;
            }
            resumedThisCycle.insert(fiber);
            safeRunningFiber(fiber);
        }
        // Calcul du temps occupé dans cette opération
        auto endBusy = std::chrono::steady_clock::now();
        auto busyElapsed = std::chrono::duration_cast<std::chrono::microseconds>(endBusy - startBusy).count();
        busyElapsedIteration += (uint64_t)busyElapsed;
    }

    /**
     * @brief Performs the `safeRunningFiber` operation.
     * @param _fiber Value passed to the method.
     */
    void safeRunningFiber(LPVOID _fiber)
    {
        if (!_fiber) {
            return;
        }

        m_runningFiber.store(_fiber, std::memory_order_release);
        fiberStartTimeNs_.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count(),
            std::memory_order_release);
        SwitchToFiber(_fiber);
        // Back in the main fiber after the target fiber finishes, yields, or gets preempted by the watchdog.
        m_runningFiber.store(nullptr, std::memory_order_release);
        fiberStartTimeNs_.store(0, std::memory_order_release);
        if (fireWatchDog.exchange(false, std::memory_order_acq_rel)) {
#if defined(_WIN32)
            // On Windows the watchdog gets back to the main fiber by hijacking the
            // interrupted thread context to run trampolineFunction(). That path does
            // not preserve a resumable instruction pointer for the interrupted fiber,
            // so re-queueing it would resume into the trampoline frame and hang or
            // jump to garbage. Drop the fiber instead and let the event loop keep running.
            swCWarning(kSwLogCategory_SwCoreApplication)
                << "Watchdog preempted a blocking fiber on Windows; dropping the fiber.";
#else
            SwMutexLocker lock(getReadyMutex());
            instance()->getReadyFibers().push(_fiber);
#endif
        }
        // Back here after the fiber finishes or yields again
        // Check if the fiber needs to be deleted
        deleteFiberIfNeeded(_fiber);
    }

    /**
     * @brief Deletes a fiber if it is no longer in use.
     *
     * This function checks whether a given fiber is still being used, either as a yielded fiber
     * (waiting to be resumed) or as a ready fiber (waiting to be executed). If the fiber is neither
     * yielded nor ready, it is safely deleted.
     *
     * The function performs the following steps:
     * 1. Checks if the fiber is present in the `s_yieldedFibers` map (yielded fibers).
     * 2. If not yielded, checks if the fiber is present in the `s_readyFibers` queue (ready fibers).
     * 3. If the fiber is neither yielded nor ready, calls `DeleteFiber` to release its resources.
     *
     * @param fiber Pointer to the fiber to check and potentially delete.
     *
     * @note The function ensures thread safety by using mutex locks when accessing the shared
     *       `s_yieldedFibers` and `s_readyFibers` data structures.
     * @note If the fiber is still in use, it is not deleted.
     */    
    void deleteFiberIfNeeded(void* fiber) {
        if(fiber == instance()->mainFiber){
            return;
        }

        bool fiberYielded = isFiberYielded(fiber);
        bool fiberReady = !fiberYielded && isFiberReady(fiber);

        if (!fiberYielded && !fiberReady) {
            DeleteFiber(fiber);
        }
    }


    /**
     * @brief Returns whether the object reports fiber Yielded.
     * @param fiber Value passed to the method.
     * @return `true` when the object reports fiber Yielded; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isFiberYielded(LPVOID fiber) {
        SwMutexLocker lock(getYieldMutex());
        for (auto &kv : getYieldedFibers()) {
            if (kv.second == fiber) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Returns whether the object reports fiber Ready.
     * @param fiber Value passed to the method.
     * @return `true` when the object reports fiber Ready; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isFiberReady(LPVOID fiber) {
        SwMutexLocker lock(getReadyMutex());
        std::queue<LPVOID> tempHi = getReadyFibersHi();
        while (!tempHi.empty()) {
            LPVOID f = tempHi.front();
            tempHi.pop();
            if (f == fiber) {
                return true;
            }
        }
        std::queue<LPVOID> temp = getReadyFibers();
        while (!temp.empty()) {
            LPVOID f = temp.front();
            temp.pop();
            if (f == fiber) {
                return true;
            }
        }
        return false;
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
        // Iterate over a stable snapshot of timer IDs to avoid iterator/pointer invalidation.
        std::vector<int> timerIds;
        timerIds.reserve(timers.size());
        for (const auto& kv : timers) {
            timerIds.push_back(kv.first);
        }

        for (int timerId : timerIds) {
            auto it = timers.find(timerId);
            if (it == timers.end()) {
                continue;
            }

            _T* currentTimer = it->second;
            if (!currentTimer || !currentTimer->isReady()) {
                continue;
            }

            if (currentTimer->singleShot) {
                _T* toDelete = currentTimer;
                timers.erase(it);

                std::function<void()> timerEvent = [toDelete]() {
                    toDelete->execute();
                    delete toDelete;
                };
                runEventInFiber(timerEvent);
            } else {
                std::function<void()> timerEvent = [currentTimer]() {
                    currentTimer->execute();
                };
                runEventInFiber(timerEvent);
            }
        }

        int minTimeUntilNext = (std::numeric_limits<int>::max)();
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
            for (size_t i = 0; i < pendingTimerDeletes_.size(); ++i) {
                delete pendingTimerDeletes_[i];
            }
            pendingTimerDeletes_.clear();
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
    int exitCode; ///< Exit code of the application.
#if defined(_WIN32)
    HANDLE mainThreadHandle;
    DWORD mainThreadId;
#else
    pthread_t mainThreadPthread_{};
#endif

    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning{ false };
    std::atomic<bool> fireWatchDog{ false };
    std::atomic<int64_t> fiberStartTimeNs_{0};

    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::queue<std::function<void()>> eventQueue; ///< Queue of events to process.
    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::queue<std::function<void()>> priorityEventQueue; ///< High-priority event queue.
    std::mutex eventQueueMutex; ///< Mutex protecting access to the event queue.
    std::condition_variable cv; ///< Condition variable for event waiting.

    struct IterationMeasurement {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t busyMicroseconds;
        uint64_t totalMicroseconds;
    };
    // Queue des mesures sur la dernière seconde environ
    std::deque<IterationMeasurement> measurements;

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

    std::atomic<LPVOID> m_runningFiber{nullptr}; ///< Pointer to the currently running fiber.
    LPVOID mainFiber = nullptr; ///< Pointer to the main fiber.

private:
    void cleanupAllFibers_() {
#if defined(_WIN32)
        // Best-effort cleanup for any remaining fibers (yielded or ready) before shutdown.
        // Without this, long-lived runtimes can leave fibers allocated until process exit.
        std::vector<LPVOID> toDelete;
        {
            SwMutexLocker lk(getYieldMutex());
            for (auto& kv : getYieldedFibers()) {
                if (kv.second && kv.second != mainFiber) toDelete.push_back(kv.second);
            }
            getYieldedFibers().clear();
        }
        {
            SwMutexLocker lk(getReadyMutex());
            while (!getReadyFibersHi().empty()) {
                LPVOID f = getReadyFibersHi().front();
                getReadyFibersHi().pop();
                if (f && f != mainFiber) toDelete.push_back(f);
            }
            while (!getReadyFibers().empty()) {
                LPVOID f = getReadyFibers().front();
                getReadyFibers().pop();
                if (f && f != mainFiber) toDelete.push_back(f);
            }
        }
        for (size_t i = 0; i < toDelete.size(); ++i) {
            // DeleteFiber is safe only when the fiber is not executing.
            DeleteFiber(toDelete[i]);
        }
#endif
    }

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

        DWORD timeoutMs = INFINITE;
        if (timeoutUs >= 0) {
            timeoutMs = static_cast<DWORD>((timeoutUs + 999) / 1000);
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
        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + handles.size()) {
            const size_t idx = static_cast<size_t>(r - WAIT_OBJECT_0);
            if (idx > 0 && idx < callbacks.size() && callbacks[idx]) {
                runEventInFiber(callbacks[idx]);
                // If the waitable callback un-yielded fibers, run them now (reduces latency).
                resumeReadyFibers();
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
                    runEventInFiber(callbacks[i]);
                    resumeReadyFibers();
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

#endif // SW_CORE_RUNTIME_SWCOREAPPLICATION_H
