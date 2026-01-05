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

/***************************************************************************************************
 * SwGuiApplication now acts as a thin wrapper around the platform abstraction layer.
 ***************************************************************************************************/

#include "SwCoreApplication.h"
#include "platform/SwPlatformFactory.h"
#include "platform/SwPlatformIntegration.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

class SwGuiApplication : public SwCoreApplication {
public:
    SwGuiApplication() {
        instance(false) = this;
        m_platformIntegration = SwCreateDefaultPlatformIntegration();
        if (!m_platformIntegration) {
            throw std::runtime_error("No platform integration available");
        }
        m_platformIntegration->initialize(this);
    }

    ~SwGuiApplication() override {
        if (m_platformIntegration) {
            m_platformIntegration->shutdown();
        }
    }

    static SwGuiApplication*& instance(bool create = true) {
        static SwGuiApplication* s_instance = nullptr;
        if (!s_instance && create) {
            s_instance = new SwGuiApplication();
        }
        return s_instance;
    }

    int exec(int maxDurationMicroseconds = 0) override {
        setHighThreadPriority();
        auto startTime = std::chrono::steady_clock::now();
        auto lastTime = startTime;

        while (running) {
            busyElapsedIteration = 0;

            if (m_platformIntegration) {
                m_platformIntegration->processPlatformEvents();
            }

            int sleepDuration = processEvent();

            // Process freshly posted paint/input messages that may have been queued by processEvent().
            if (m_platformIntegration) {
                m_platformIntegration->processPlatformEvents();
            }

            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastTime).count();
            lastTime = currentTime;

            totalTimeMicroseconds += static_cast<uint64_t>(elapsed);
            totalBusyTimeMicroseconds += static_cast<uint64_t>(busyElapsedIteration);

            measurements.push_back({
                currentTime,
                static_cast<uint64_t>(busyElapsedIteration),
                static_cast<uint64_t>(elapsed)
            });

            auto oneSecondAgo = currentTime - std::chrono::seconds(1);
            while (!measurements.empty() && measurements.front().timestamp < oneSecondAgo) {
                measurements.pop_front();
            }

            auto totalElapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(currentTime - startTime).count();
            if (maxDurationMicroseconds != 0 && totalElapsed >= maxDurationMicroseconds) {
                break;
            }

            if (!running) break;
            if (sleepDuration != 0) {
#if defined(_WIN32)
                // Wait on core waitables (IPC signals, wake events, ...) *and* Win32 messages.
                waitForWorkGui(sleepDuration);
#else
                waitForWork(sleepDuration);
#endif
            }
        }
        return exitCode;
    }

    SwPlatformIntegration* platformIntegration() const {
        return m_platformIntegration.get();
    }

private:
    std::unique_ptr<SwPlatformIntegration> m_platformIntegration;
};
