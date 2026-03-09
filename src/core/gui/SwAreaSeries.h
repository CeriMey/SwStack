
/**
 * @file
 * @ingroup core_gui
 * @brief Declares the filled area series type used by the chart subsystem.
 *
 * `SwAreaSeries` extends the generic series base with properties specific to area plots,
 * such as the baseline, fill color, and border width. Chart views read those settings to
 * render a line series with a filled region down to a configurable reference value.
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

class SwAreaSeries : public SwAbstractSeries {
    SW_OBJECT(SwAreaSeries, SwAbstractSeries)

public:
    /**
     * @brief Constructs a `SwAreaSeries` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwAreaSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SeriesType type() const override { return SeriesType::Area; }

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

    /**
     * @brief Sets the fill Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFillColor(const SwColor& color) {
        const SwColor clamped = clampColor_(color);
        if (m_fillColor.r == clamped.r && m_fillColor.g == clamped.g && m_fillColor.b == clamped.b) {
            return;
        }
        m_fillColor = clamped;
        m_hasFillColor = true;
        updated();
    }

    /**
     * @brief Returns whether the object reports fill Color.
     * @return `true` when the object reports fill Color; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasFillColor() const { return m_hasFillColor; }
    /**
     * @brief Returns the current fill Color.
     * @return The current fill Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor fillColor() const { return m_fillColor; }

    /**
     * @brief Sets the border Width.
     * @param width Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBorderWidth(int width) {
        width = std::max(0, std::min(12, width));
        if (m_borderWidth == width) {
            return;
        }
        m_borderWidth = width;
        updated();
    }

    /**
     * @brief Returns the current border Width.
     * @return The current border Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int borderWidth() const { return m_borderWidth; }

private:
    double m_baseline{0.0};
    bool m_hasFillColor{false};
    SwColor m_fillColor{191, 219, 254};
    int m_borderWidth{2};
};
