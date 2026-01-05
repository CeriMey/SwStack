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

#include "SwObject.h"
#include "SwCoreApplication.h"
#include <chrono>

/**
 * @class SwTimer
 * @brief Provides a timer implementation for periodic or single-shot execution of tasks, similar to QTimer.
 */
class SwTimer : public SwObject
{
public:

    /**
     * @enum TimerType
     * @brief Mimics Qt::TimerType to define how the timer measures time.
     */
    enum class TimerType {
        PreciseTimer,
        CoarseTimer,
        VeryCoarseTimer
    };

    /**
     * @brief Constructs a SwTimer SwObject.
     *
     * @param ms The interval in milliseconds for the timer (default is 1000 ms).
     * @param parent The parent SwObject for the timer.
     */
    SwTimer(int ms, SwObject *parent = nullptr)
        : SwObject(parent)
        , m_interval(ms*1000) // interval stocké en microsecondes
        , m_running(false)
        , m_timerId(-1)
        , m_singleShot(false)
        , m_timerType(TimerType::PreciseTimer)
    {
    }

    /**
     * @brief Constructs a SwTimer SwObject.
     *
     * @param parent The parent SwObject for the timer.
     */
    SwTimer(SwObject *parent = nullptr)
        : SwObject(parent)
        , m_interval(1000000) // interval stocké en microsecondes
        , m_running(false)
        , m_timerId(-1)
        , m_singleShot(false)
        , m_timerType(TimerType::PreciseTimer)
    {
    }

    /**
     * @brief Destructor to clean up the SwTimer resources.
     */
    virtual ~SwTimer() {
        stop();
        if (m_timerId != -1) {
            SwCoreApplication::instance()->removeTimer(m_timerId);
        }
    }

    /**
     * @brief Sets the interval for the timer.
     *
     * @param ms The interval in milliseconds.
     */
    void setInterval(int ms) {
        if (!m_running) {
            m_interval = ms * 1000;
        }
    }

    /**
     * @brief Returns the current interval in milliseconds.
     */
    int interval() const {
        return static_cast<int>(m_interval / 1000);
    }

    /**
     * @brief Sets whether the timer should be single-shot.
     *
     * @param singleShot True for single-shot timer, false for a repeating timer.
     */
    void setSingleShot(bool singleShot) {
        m_singleShot = singleShot;
    }

    /**
     * @brief Returns true if the timer is single-shot.
     */
    bool isSingleShot() const {
        return m_singleShot;
    }

    /**
     * @brief Starts the timer with the previously set interval.
     */
    void start() {
        // ✅ si on n'est pas dans le thread d'affinité du timer, on forward
        if (threadHandle() && ThreadHandle::currentThread() != threadHandle()) {
            auto self = this;
            threadHandle()->postTask([self]() { self->start(); });
            return;
        }

        if (!m_running) {
            m_running = true;
            m_startTime = std::chrono::steady_clock::now();

             m_timerId = SwCoreApplication::instance()->addTimer([this]() {
                 // Update internal state before emitting: slots may delete this timer.
                 m_startTime = std::chrono::steady_clock::now();
                 emit timeout();
             }, static_cast<int>(m_interval), m_singleShot);
         }
     }

    /**
     * @brief Starts the timer with a given interval in milliseconds.
     */
    void start(int ms) {
        if (threadHandle() && ThreadHandle::currentThread() != threadHandle()) {
            auto self = this;
            threadHandle()->postTask([self, ms]() { self->start(ms); });
            return;
        }
        setInterval(ms);
        start();
    }

    /**
     * @brief Stops the timer.
     */
    void stop() {
        // ✅ stop doit aussi s'exécuter dans le thread du timer
        if (threadHandle() && ThreadHandle::currentThread() != threadHandle()) {
            auto self = this;
            threadHandle()->postTask([self]() { self->stop(); });
            return;
        }

        if (m_running) {
            m_running = false;
            if (m_timerId != -1) {
                const int id = m_timerId;
                // ici on est déjà dans le bon thread => remove direct ou postEvent local
                SwCoreApplication::instance()->removeTimer(id);
                m_timerId = -1;
            }
        }
    }

    /**
     * @brief Check if the timer is currently active.
     */
    bool isActive() const {
        return m_running;
    }

    /**
     * @brief Returns the remaining time in milliseconds until the next timeout.
     *
     * If the timer is not active, returns -1.
     */
    int remainingTime() const {
        if (!m_running) {
            return -1;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - m_startTime).count();
        auto remaining_us = m_interval - elapsed_us;
        return remaining_us > 0 ? static_cast<int>(remaining_us / 1000) : 0;
    }

    /**
     * @brief Sets the timer type.
     *
     * @param type The timer type (Precise, Coarse, etc.)
     */
    void setTimerType(TimerType type) {
        if (!m_running) {
            m_timerType = type;
        }
    }

    /**
     * @brief Returns the current timer type.
     */
    TimerType timerType() const {
        return m_timerType;
    }

    /**
     * @brief Creates a single-shot timer that executes a callback after a specified delay.
     *
     * @param ms The delay in milliseconds.
     * @param callback The callback function to execute.
     */
    static void singleShot(int ms, std::function<void()> callback) {
        SwTimer* tempTimer = new SwTimer(ms);

        tempTimer->setSingleShot(true);
        tempTimer->connect(tempTimer, SIGNAL(timeout), std::function<void(void)>([callback, tempTimer]() {
            callback();
            tempTimer->stop();
            tempTimer->deleteLater();
        }));

        tempTimer->start();
    }

    template <typename T>
    static void singleShot(int ms, T* obj, void (T::*func)()) {
        SwTimer* tempTimer = new SwTimer(ms);

        tempTimer->setSingleShot(true);
        tempTimer->connect(tempTimer, SIGNAL(timeout), [obj, func, tempTimer]() {
            (obj->*func)();
            tempTimer->stop();
            tempTimer->deleteLater();
        });

        tempTimer->start();
    }
signals:
    /**
     * @brief Signal emitted when the timer interval elapses.
     */
    DECLARE_SIGNAL_VOID(timeout)

private:
    long long m_interval;  ///< The interval in microseconds for the timer.
    bool m_running;        ///< Indicates if the timer is currently running.
    int m_timerId;         ///< The unique identifier for the timer in the SwCoreApplication instance.
    bool m_singleShot;     ///< Indicates if the timer is single-shot.
    TimerType m_timerType; ///< The type of the timer.
    std::chrono::steady_clock::time_point m_startTime; ///< Keeps track of when the timer started.
};
