
/**
 * @file
 * @ingroup core_gui
 * @brief Declares the bar-series data container used by chart widgets.
 *
 * This series stores categorical or numeric bar values on top of `SwAbstractSeries` and
 * exposes the state needed by the chart renderer to draw grouped bars with consistent
 * bounds computation, point limits, and palette-driven styling.
 */

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



#include "SwAbstractSeries.h"

#include <algorithm>
#include <cmath>

class SwBarSeries : public SwAbstractSeries {
    SW_OBJECT(SwBarSeries, SwAbstractSeries)

public:
    /**
     * @brief Constructs a `SwBarSeries` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwBarSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SeriesType type() const override { return SeriesType::Bar; }

    /**
     * @brief Sets the bar Width.
     * @param width Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBarWidth(double width) {
        if (!std::isfinite(width)) {
            return;
        }
        if (width < 0.0) {
            width = 0.0;
        }
        if (width == m_barWidth) {
            return;
        }
        m_barWidth = width;
        updated();
    }

    // Width in X axis units. 0 == auto.
    /**
     * @brief Returns the current bar Width.
     * @return The current bar Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double barWidth() const { return m_barWidth; }

    /**
     * @brief Sets the baseline.
     * @param baseline Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBaseline(double baseline) {
        if (!std::isfinite(baseline) || baseline == m_baseline) {
            return;
        }
        m_baseline = baseline;
        updated();
    }

    /**
     * @brief Returns the current baseline.
     * @return The current baseline.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double baseline() const { return m_baseline; }

private:
    double m_barWidth{0.0};
    double m_baseline{0.0};
};
