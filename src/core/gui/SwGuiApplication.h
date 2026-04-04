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

#pragma once

/**
 * @file src/core/gui/SwGuiApplication.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwGuiApplication in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the GUI application interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwGuiApplication.
 *
 * Application-oriented declarations here define the top-level lifecycle surface for startup,
 * shutdown, event processing, and integration with the rest of the framework.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwGuiApplication now acts as a thin wrapper around the platform abstraction layer.
 ***************************************************************************************************/

/**
 * @file
 * @brief Declares the GUI application entry point for the SwStack widget runtime.
 *
 * SwGuiApplication extends SwCoreApplication with the pieces needed by visual
 * applications: it creates the active platform integration, lets that backend
 * pump native window-system events, and interleaves those events with the core
 * event loop maintained by SwCoreApplication.
 */

#include "SwCoreApplication.h"
#include "SwWidgetPlatformAdapter.h"
#include "platform/SwPlatformFactory.h"
#include "platform/SwPlatformIntegration.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

/**
 * @brief Coordinates the core event loop with the selected GUI platform backend.
 *
 * A SwGuiApplication owns the process-wide platform integration object and keeps
 * native input, paint, and windowing events flowing alongside the framework's
 * own timers, posted events, and waitable objects.
 */
class SwGuiApplication : public SwCoreApplication {
public:
    /**
     * @brief Constructs a `SwGuiApplication` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGuiApplication() {
        instance(false) = this;
        setRuntimeApplicationKind("gui", true);
        m_platformIntegration = SwCreateDefaultPlatformIntegration();
        if (!m_platformIntegration) {
            throw std::runtime_error("No platform integration available");
        }
        m_platformIntegration->initialize(this);
    }

    /**
     * @brief Destroys the `SwGuiApplication` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwGuiApplication() override {
        if (m_platformIntegration) {
            m_platformIntegration->shutdown();
        }
    }

    /**
     * @brief Performs the `instance` operation.
     * @param create Value passed to the method.
     * @return The requested instance.
     */
    static SwGuiApplication*& instance(bool create = true) {
        static SwGuiApplication* s_instance = nullptr;
        if (!s_instance && create) {
            s_instance = new SwGuiApplication();
        }
        return s_instance;
    }

    /**
     * @brief Performs the `exec` operation.
     * @param maxDurationMicroseconds Value passed to the method.
     * @return The requested exec.
     */
    int exec(int maxDurationMicroseconds = 0) override {
        setHighThreadPriority();
        (void)profilerSessionsForCurrentThread_();
        const auto startTime = std::chrono::steady_clock::now();
        auto iterationBoundary = startTime;

        while (running) {
            busyElapsedIteration = 0;

            const auto busyStart = std::chrono::steady_clock::now();

            if (m_platformIntegration) {
                const SwList<std::shared_ptr<SwRuntimeProfilerSession>> profilerSessions =
                    autoRuntimeProfilerSessionsForCurrentThread_();
                MultiRuntimeScopedSpan_ platformScope(profilerSessions,
                                                      SwRuntimeTimingKind::PlatformPump,
                                                      "platform_pump",
                                                      SwFiberLane::Control,
                                                      true);
                m_platformIntegration->processPlatformEvents();
            }

            int sleepDuration = processEvent();

            // Batch-drain: process all pending events before re-entering the
            // platform pump and measurement overhead.  This avoids paying the
            // full iteration cost (platform pump + timing + sleep check) for
            // every single queued event when multiple keystrokes/mouse events
            // arrive in a burst.
            while (sleepDuration == 0 && running) {
                sleepDuration = processEvent();
            }

            // Flush accumulated damage regions into a single InvalidateRect per
            // HWND before pumping messages, so the upcoming WM_PAINT covers
            // everything that was dirtied by the events we just processed.
            SwWidgetPlatformAdapter::flushDamage();

            // Process freshly posted paint/input messages that may have been queued by processEvent().
            if (m_platformIntegration) {
                const SwList<std::shared_ptr<SwRuntimeProfilerSession>> profilerSessions =
                    autoRuntimeProfilerSessionsForCurrentThread_();
                MultiRuntimeScopedSpan_ platformScope(profilerSessions,
                                                      SwRuntimeTimingKind::PlatformPump,
                                                      "platform_pump",
                                                      SwFiberLane::Control,
                                                      true);
                m_platformIntegration->processPlatformEvents();
            }
            const auto busyEnd = std::chrono::steady_clock::now();
            busyElapsedIteration = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(busyEnd - busyStart).count());

            if (!running) {
                sleepDuration = 0;
            }

            if (sleepDuration != 0) {
#if defined(_WIN32)
                // Wait on core waitables (IPC signals, wake events, ...) *and* Win32 messages.
                waitForWorkGui(sleepDuration);
#elif SW_PLATFORM_ANDROID
                // Phase 1 Android backend: the native app glue looper is not yet
                // merged into SwCoreApplication waitables, so never sleep forever.
                // Waking up regularly keeps APP_CMD_* delivery, resize and paint
                // requests flowing without changing desktop behavior.
                const int boundedSleepDuration =
                    (sleepDuration < 0) ? 16000 : std::min(sleepDuration, 16000);
                waitForWork(boundedSleepDuration);
#else
                waitForWork(sleepDuration);
#endif
            }

            const auto iterationEnd = std::chrono::steady_clock::now();
            const uint64_t totalElapsedIteration = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(iterationEnd - iterationBoundary).count());
            iterationBoundary = iterationEnd;

            this->recordRuntimeIterationMeasurement_(iterationEnd, busyElapsedIteration, totalElapsedIteration);
            this->publishRuntimeIterationSnapshot_();

            const auto totalElapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(iterationEnd - startTime).count();
            if (maxDurationMicroseconds != 0 && totalElapsed >= maxDurationMicroseconds) {
                break;
            }
        }
        return exitCode.load(std::memory_order_acquire);
    }

    /**
     * @brief Returns the current platform Integration.
     * @return The current platform Integration.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPlatformIntegration* platformIntegration() const {
        return m_platformIntegration.get();
    }

private:
    std::unique_ptr<SwPlatformIntegration> m_platformIntegration;
};
