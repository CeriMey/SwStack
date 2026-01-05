#pragma once
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

#include "SwTimer.h"
#include "SwCoreApplication.h"
static constexpr const char* kSwLogCategory_SwEventLoop = "sw.core.runtime.sweventloop";

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif
#include <atomic>
#include <memory>


/**
 * @def tswhile(cond, ms)
 * @brief Cooperative loop with a sleep delay.
 *
 * This macro defines a non-blocking cooperative loop that continuously evaluates the condition `cond`.
 * Between iterations, the loop yields control to other tasks by sleeping for the specified `ms` milliseconds.
 *
 * @param cond The condition to evaluate in each iteration. The loop continues while this evaluates to `true`.
 * @param ms The delay in milliseconds between iterations.
 *
 * ### Example:
 * ```cpp
 * tswhile(taskIsRunning, 500) {
 *     swCDebug(kSwLogCategory_SwEventLoop) << "Task still running...";
 * }
 * ```
 *
 * @note This macro ensures that other tasks or fibers can execute during the delay period,
 *       enabling cooperative multitasking.
 */
#define tswhile(cond, ms) for(bool _tkeepGoing_ = (cond); _tkeepGoing_; _tkeepGoing_ = (cond), SwEventLoop::swsleep(ms))

/**
 * @def swhile(cond)
 * @brief Cooperative loop without a fixed delay.
 *
 * This macro defines a non-blocking cooperative loop that continuously evaluates the condition `cond`.
 * Between iterations, the loop yields control to other tasks by invoking `SwCoreApplication::instance()->release()`.
 *
 * @param cond The condition to evaluate in each iteration. The loop continues while this evaluates to `true`.
 *
 * ### Example:
 * ```cpp
 * swhile(!taskCompleted) {
 *     swCDebug(kSwLogCategory_SwEventLoop) << "Processing task...";
 * }
 * ```
 *
 * @note This macro allows other tasks or fibers to execute between iterations, making it ideal for non-blocking operations.
 */
#define swhile(cond) for(bool _keepGoing_ = (cond); _keepGoing_; _keepGoing_ = (cond), SwCoreApplication::instance()->release())

/**
 * @def swLocalLoop()
 * @brief Infinite cooperative loop without a fixed delay.
 *
 * This macro defines a non-blocking infinite loop that continuously yields control to other tasks
 * by invoking `SwCoreApplication::instance()->release()`.
 *
 * ### Example:
 * ```cpp
 * swLocalLoop() {
 *     swCDebug(kSwLogCategory_SwEventLoop) << "Running indefinitely...";
 * }
 * ```
 *
 * @note This macro is useful for creating runtime functions or background processes that run indefinitely
 *       while allowing other tasks to execute.
 */
#define swLocalLoop() swhile(true)

/**
 * @def swLocalSlowLoop(ms)
 * @brief Infinite cooperative loop with a fixed delay.
 *
 * This macro defines a non-blocking infinite loop that continuously yields control to other tasks
 * with a specified delay of `ms` milliseconds between iterations.
 *
 * @param ms The delay in milliseconds between iterations.
 *
 * ### Example:
 * ```cpp
 * swLocalSlowLoop(1000) {
 *     swCDebug(kSwLogCategory_SwEventLoop) << "Task running every second...";
 * }
 * ```
 *
 * @note This macro is ideal for periodic tasks or background operations that require a fixed interval between executions.
 */
#define swLocalSlowLoop(ms) tswhile(true, ms)


/**
 * @class SwEventLoop
 * @brief A local event loop mechanism built on top of the SwCoreApplication fiber system.
 *
 * This class provides a local event loop similar to QEventLoop. It allows you to start a nested
 * event loop and block execution until `quit()` or `exit()` is called, at which point it returns
 * control back to the caller of `exec()`.
 *
 * @details
 * The SwEventLoop relies on the underlying fiber-based architecture of SwCoreApplication.
 * When `exec()` is called, it suspends the current fiber using `yieldFiber()` and waits until
 * `unYieldFiber()` is called elsewhere in the code (typically via `quit()` or `exit()`), causing
 * the event loop to resume and exit. This makes it possible to implement nested event loops,
 * modal dialogs, or short-lived waiting loops without blocking the entire application.
 *
 * ### Example Usage
 *
 * ```cpp
 * SwEventLoop loop;
 *
 * // Some code that posts an event to quit the loop later...
 * // For instance:
 * // SwCoreApplication::instance()->postEvent([&]() {
 * //     loop.quit();
 * // });
 *
 * // This call will block here until quit() or exit() is called
 * int returnCode = loop.exec();
 *
 * // At this point, the loop has stopped running and we have `returnCode`.
 * ```
 *
 * ### Key Methods
 * - `exec()`: Starts the event loop. Execution will be suspended until `quit()` or `exit()` is called.
 * - `quit()`: Stops the event loop and returns control to the caller of `exec()`.
 * - `exit(int code)`: Like `quit()`, but also sets an exit code that can be retrieved from `exec()`.
 * - `isRuning()`: Checks if the event loop is currently running.
 *
 * ### Internal Details
 * - `exec()` obtains a unique identifier for the event loop and yields the current fiber, effectively
 *   pausing execution. The fiber will only resume once `unYieldFiber()` is called with the corresponding
 *   identifier, which happens in `quit()` or `exit()`.
 * - When `quit()` is invoked, the event loop sets `running_` to `false` and triggers `unYieldFiber(id_)`,
 *   causing `exec()` to return.
 * - `exit(int code)` is similar to `quit()` but also sets an exit code, which `exec()` returns.
 *
 * @note Ensure that SwCoreApplication and its fiber system are properly initialized before using SwEventLoop.
 *
 * @author
 * Eymeric O'Neill
 * Contact: eymeric.oneill@gmail.com, +33 6 52 83 83 31
 *
 * @since 1.0
 */
class SwEventLoop : public SwObject {
public:
    /**
     * @brief Constructs a new SwEventLoop.
     *
     * The event loop is not running initially. You can start it by calling `exec()`.
     */
    SwEventLoop()
        : running_(false),
        exitCode(0)
    {
        // Assumes that mainFiber is accessible and initialized in SwCoreApplication.
    }

    /**
     * @brief Destroys the SwEventLoop.
     *
     * If the event loop is running, it will be stopped by calling `quit()` before destruction.
     */
    ~SwEventLoop() {
        quit();
    }

    /**
     * @brief Starts the event loop.
     *
     * This method suspends the current fiber and waits until `quit()` or `exit()` is called.
     * Once resumed, `exec()` returns the exit code set by `exit()` or 0 if `quit()` was used.
     *
     * @return The exit code set by `exit()` or 0 if `quit()` was called.
     */
    int exec(int delay = 0) {
        if(running_) return -1; // return if already runing
        if(delay) SwTimer::singleShot(delay, this, &SwEventLoop::quit); // auto wake up if delay
        exitCode = 0;
        running_ = true;
        id_ = SwCoreApplication::generateYieldId();
        SwCoreApplication::instance()->yieldFiber(id_);
        // Control will return here after `unYieldFiber()` is called for `id_`.
        return exitCode;
    }

    /**
     * @brief Blocks execution for a specified duration in milliseconds.
     *
     * This method provides a mechanism to pause execution for a given duration, while maintaining the
     * fiber-based architecture of the event loop. Depending on the context in which it is called,
     * it behaves as follows:
     *
     * - **If called from the main fiber** (before the event loop starts):
     *   - A blocking sleep is used via `std::this_thread::sleep_for`. This is a simple, synchronous
     *     blocking mechanism.
     * - **If called from a secondary fiber** (after the event loop has started):
     *   - A non-blocking mechanism is used:
     *     1. A one-shot timer (`SwTimer::singleShot`) is created, which schedules a callback to wake
     *        the current fiber after the specified duration.
     *     2. The current fiber is "yielded" (paused) using `SwCoreApplication::yieldFiber`.
     *     3. When the timer's callback triggers, the fiber is "un-yielded" using `SwCoreApplication::unYieldFiber`,
     *        allowing the fiber to resume execution.
     *
     * This approach ensures that other fibers and tasks can continue to execute during the pause,
     * maintaining responsiveness of the application.
     *
     * @param milliseconds Duration of the sleep in milliseconds.
     *
     * ### Workflow:
     * 1. Determine whether the current context is the main fiber or a secondary fiber.
     * 2. If it is the main fiber:
     *    - Use a blocking sleep (`std::this_thread::sleep_for`).
     * 3. If it is a secondary fiber:
     *    - Assign a unique identifier (`myId`) to the fiber.
     *    - Set up a one-shot timer that schedules a callback to un-yield the fiber after the specified duration.
     *    - Yield the current fiber, pausing its execution.
     *    - The fiber resumes when the timer's callback calls `SwCoreApplication::unYieldFiber`.
     *
     * ### Example:
     * ```cpp
     * SwEventLoop::swsleep(500); // Pauses execution for 500 milliseconds
     * ```
     *
     * @note This method integrates seamlessly with the fiber-based event loop system,
     *       enabling non-blocking delays.
     */
    static void swsleep(int milliseconds) {
        SwCoreApplication* app = SwCoreApplication::instance(false);
        LPVOID current = GetCurrentFiber();

        if (current == app->mainFiber) {
            // If the event loop hasn't started yet (in the main fiber), use a blocking sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        } else {
            // If inside a secondary fiber, use non-blocking mechanism
            int myId = SwCoreApplication::generateYieldId();

            // Schedule a one-shot timer (microseconds) to wake up the fiber after the specified duration
            int intervalUs = milliseconds <= 0 ? 0 : (milliseconds * 1000);
            app->addTimer([myId]() {
                SwCoreApplication::unYieldFiber(myId);
            }, intervalUs, /*singleShot=*/true);

            // Yield the current fiber, pausing its execution
            SwCoreApplication::yieldFiber(myId);
        }
    }

    /**
     * @brief Stops the event loop without an exit code.
     *
     * Calling `quit()` will cause `exec()` to return 0. If the event loop is not currently running,
     * this function has no effect.
     */
    void quit() {
        if (running_) {
            running_ = false;
            // Prioritize resuming the waiting fiber to reduce wakeup latency.
            SwCoreApplication::unYieldFiberHighPriority(id_);
        }
    }

    /**
     * @brief Installs a runtime function that runs continuously in a local event loop.
     *
     * This method schedules a provided function (`fn`) to run repeatedly within a local fiber-based
     * event loop (`swLocalLoop`). The runtime function is executed continuously without a fixed delay,
     * allowing tasks to be processed as quickly as possible while still yielding control to the main
     * application loop for cooperative multitasking.
     *
     * ### Workflow:
     * 1. A one-shot timer (`SwTimer::singleShot`) is triggered with a delay of `0`. This ensures that:
     *    - The runtime function starts immediately.
     *    - A new dedicated fiber is created for the runtime function, preventing the current fiber
     *      (calling this method) from being blocked.
     * 2. Inside the timer's callback:
     *    - The `swLocalLoop` macro is used to continuously call the provided function (`fn`) in an
     *      infinite loop.
     *    - Execution yields periodically to avoid blocking the application and to allow other tasks
     *      or events to execute.
     * 3. The runtime function continues until explicitly stopped or interrupted by other control
     *    mechanisms in the application.
     *
     * ### Example Usage:
     * ```cpp
     * SwEventLoop::installRuntime([]() {
     *     swCDebug(kSwLogCategory_SwEventLoop) << "Executing continuous runtime task...";
     * });
     * ```
     *
     * @param fn The function to execute continuously within the local event loop.
     *
     * @note By triggering a one-shot timer, this method ensures that a dedicated fiber is created
     *       for the runtime function. This design prevents the current fiber from being blocked and
     *       allows the runtime task to operate independently.
     *
     * @warning The infinite nature of `swLocalLoop` means the function will run indefinitely unless
     *          explicitly stopped or interrupted. Ensure appropriate safeguards or stopping
     *          mechanisms are in place if needed.
     */
    using RuntimeHandle = std::shared_ptr<std::atomic<bool>>;

    static RuntimeHandle installRuntime(std::function<void()> fn) {
        auto handle = std::make_shared<std::atomic<bool>>(true);
        SwCoreApplication* app = SwCoreApplication::instance(false);
        auto starter = [fn, handle]() {
            swLocalLoop() { // Continuous execution without a fixed delay
                if (!handle->load(std::memory_order_acquire)) {
                    return;
                }
                fn();
            }
        };

        // Use postEvent to start the runtime before any later user postEvent calls.
        // This avoids missing events in systems that subscribe then immediately publish.
        if (app) app->postEvent(starter);
        else SwTimer::singleShot(0, starter);
        return handle;
    }

    /**
     * @brief Installs a runtime function that executes periodically with a custom delay.
     *
     * This method schedules a provided function (`fn`) to run repeatedly within a local fiber-based
     * event loop (`swLocalSlowLoop`) with a specified interval (`msWait`) between executions. The
     * runtime function operates in its own dedicated fiber, ensuring the calling fiber remains unblocked.
     *
     * ### Workflow:
     * 1. A one-shot timer (`SwTimer::singleShot`) is triggered with a delay of `0`. This:
     *    - Starts the runtime function immediately.
     *    - Creates a new dedicated fiber for the runtime task, ensuring that the calling fiber is
     *      not blocked.
     * 2. Inside the timer's callback:
     *    - The `swLocalSlowLoop` macro is used to execute the provided function (`fn`) repeatedly
     *      with a delay of `msWait` milliseconds between iterations.
     *    - Execution yields control back to the main event loop during each delay, allowing other
     *      tasks or fibers to execute.
     * 3. The runtime function continues until explicitly stopped or interrupted by other mechanisms.
     *
     * ### Example Usage:
     * ```cpp
     * SwEventLoop::installSlowRuntime(500, []() {
     *     swCDebug(kSwLogCategory_SwEventLoop) << "Executing periodic runtime task every 500ms...";
     * });
     * ```
     *
     * @param msWait The interval in milliseconds between executions of the runtime function.
     * @param fn The function to execute periodically within the local slow loop.
     *
     * @note By triggering a one-shot timer, this method ensures that the runtime function operates
     *       in its own fiber, preventing the current fiber from being blocked. The use of `swLocalSlowLoop`
     *       ensures that control is yielded to the main event loop during the delay periods.
     *
     * @warning The runtime function (`fn`) will continue running indefinitely unless explicitly
     *          stopped or interrupted. Ensure appropriate stopping mechanisms or safeguards are in place.
     */
    static RuntimeHandle installSlowRuntime(int msWait, std::function<void()> fn) {
        auto handle = std::make_shared<std::atomic<bool>>(true);
        SwCoreApplication* app = SwCoreApplication::instance(false);
        auto starter = [fn, msWait, handle]() {
            swLocalSlowLoop(msWait) { // Execute repeatedly with msWait milliseconds delay
                if (!handle->load(std::memory_order_acquire)) {
                    return;
                }
                fn();
            }
        };

        // Use postEvent to start the runtime before any later user postEvent calls.
        // This avoids missing events in systems that subscribe then immediately publish.
        if (app) app->postEvent(starter);
        else SwTimer::singleShot(0, starter);
        return handle;
    }

    static void uninstallRuntime(const RuntimeHandle& handle) {
        if (handle) {
            handle->store(false, std::memory_order_release);
        }
    }

    /**
     * @brief Stops the event loop and sets an exit code.
     *
     * Similar to `quit()` but allows setting a custom exit code. `exec()` will return this code
     * when the event loop resumes.
     *
     * @param code The exit code to return from `exec()`.
     */
    void exit(int code = 0) {
        exitCode = code;
        quit();
    }

    /**
     * @brief Checks if the event loop is currently running.
     *
     * @return `true` if running, `false` otherwise.
     */
    bool isRuning(){
        return running_;
    }

private:
    bool running_;  ///< Indicates whether the event loop is currently running.
    int id_;        ///< Unique identifier for this event loop instance.
    int exitCode;   ///< Exit code returned by `exec()`.
};
