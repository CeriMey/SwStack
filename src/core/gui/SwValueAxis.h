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
 * @file src/core/gui/SwValueAxis.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwValueAxis in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the value axis interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwValueAxis.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwValueAxis - minimal numeric axis.
 **************************************************************************************************/

#include "SwObject.h"
#include "SwString.h"

#include <algorithm>
#include <cmath>

class SwValueAxis : public SwObject {
    SW_OBJECT(SwValueAxis, SwObject)

public:
    /**
     * @brief Constructs a `SwValueAxis` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwValueAxis(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Sets the range.
     * @param minimum Value passed to the method.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRange(double minimum, double maximum) {
        if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
            return;
        }
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        if (!m_autoRange && minimum == m_minimum && maximum == m_maximum) {
            return;
        }
        m_autoRange = false;
        m_minimum = minimum;
        m_maximum = maximum;
        changed();
    }

    /**
     * @brief Sets the minimum.
     * @param m_maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMinimum(double minimum) { setRange(minimum, m_maximum); }
    /**
     * @brief Sets the maximum.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaximum(double maximum) { setRange(m_minimum, maximum); }

    /**
     * @brief Returns the current min.
     * @return The current min.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double min() const { return m_minimum; }
    /**
     * @brief Returns the current max.
     * @return The current max.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double max() const { return m_maximum; }

    /**
     * @brief Sets the auto Range.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAutoRange(bool on) {
        if (m_autoRange == on) {
            return;
        }
        m_autoRange = on;
        changed();
    }

    /**
     * @brief Returns whether the object reports auto Range.
     * @return `true` when the object reports auto Range; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isAutoRange() const { return m_autoRange; }

    /**
     * @brief Sets the tick Count.
     * @param count Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTickCount(int count) {
        count = std::max(2, count);
        if (m_tickCount == count) {
            return;
        }
        m_tickCount = count;
        changed();
    }

    /**
     * @brief Returns the current tick Count.
     * @return The current tick Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int tickCount() const { return m_tickCount; }

    /**
     * @brief Sets the title Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitleText(const SwString& text) {
        if (m_titleText == text) {
            return;
        }
        m_titleText = text;
        changed();
    }

    /**
     * @brief Returns the current title Text.
     * @return The current title Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString titleText() const { return m_titleText; }

    /**
     * @brief Performs the `applyNiceNumbers` operation.
     */
    void applyNiceNumbers() {
        if (!std::isfinite(m_minimum) || !std::isfinite(m_maximum)) {
            return;
        }
        if (m_tickCount < 2) {
            return;
        }
        double span = m_maximum - m_minimum;
        if (span <= 0.0) {
            return;
        }

        const double range = niceNumber_(span, false);
        const double tickSpacing = niceNumber_(range / (m_tickCount - 1), true);
        if (tickSpacing <= 0.0) {
            return;
        }

        const double niceMin = std::floor(m_minimum / tickSpacing) * tickSpacing;
        const double niceMax = std::ceil(m_maximum / tickSpacing) * tickSpacing;

        if (niceMin == m_minimum && niceMax == m_maximum) {
            return;
        }
        m_autoRange = false;
        m_minimum = niceMin;
        m_maximum = niceMax;
        changed();
    }

    DECLARE_SIGNAL_VOID(changed);

private:
    static double niceNumber_(double value, bool round) {
        if (!std::isfinite(value) || value <= 0.0) {
            return 0.0;
        }

        const double exponent = std::floor(std::log10(value));
        const double power = std::pow(10.0, exponent);
        const double fraction = value / power;

        double niceFraction = 1.0;
        if (round) {
            if (fraction < 1.5) niceFraction = 1.0;
            else if (fraction < 3.0) niceFraction = 2.0;
            else if (fraction < 7.0) niceFraction = 5.0;
            else niceFraction = 10.0;
        } else {
            if (fraction <= 1.0) niceFraction = 1.0;
            else if (fraction <= 2.0) niceFraction = 2.0;
            else if (fraction <= 5.0) niceFraction = 5.0;
            else niceFraction = 10.0;
        }

        return niceFraction * power;
    }

    double m_minimum{0.0};
    double m_maximum{1.0};
    int m_tickCount{5};
    bool m_autoRange{true};
    SwString m_titleText{};
};
