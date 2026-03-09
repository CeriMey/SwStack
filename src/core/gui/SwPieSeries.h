
/**
 * @file
 * @ingroup core_gui
 * @brief Declares the pie and donut series type used by the chart subsystem.
 *
 * `SwPieSeries` stores labeled slices, their numeric weights, and per-slice display state
 * such as colors or exploded offsets. Unlike XY series, it models part-of-whole data and
 * exposes aggregate properties like total value, hole size, and start angle.
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

class SwPieSeries : public SwAbstractSeries {
    SW_OBJECT(SwPieSeries, SwAbstractSeries)

public:
    struct Slice {
        SwString label{};
        double value{0.0};
        SwColor color{0, 120, 215};
        bool exploded{false};
        double explodeFactor{0.07}; // relative to radius
    };

    /**
     * @brief Constructs a `SwPieSeries` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwPieSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SeriesType type() const override { return SeriesType::Pie; }
    /**
     * @brief Returns whether the object reports xYSeries.
     * @return `true` when the object reports xYSeries; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isXYSeries() const override { return false; }

    /**
     * @brief Performs the `append` operation.
     * @param label Value passed to the method.
     * @param value Value passed to the method.
     * @param color Value passed to the method.
     */
    void append(const SwString& label, double value, const SwColor& color) {
        if (!std::isfinite(value) || value < 0.0) {
            return;
        }
        Slice s;
        s.label = label;
        s.value = value;
        s.color = clampColor_(color);
        m_slices.push_back(s);
        pointAdded(m_slices.size() - 1);
        updated();
    }

    /**
     * @brief Performs the `append` operation.
     * @param label Value passed to the method.
     * @param value Value passed to the method.
     */
    void append(const SwString& label, double value) {
        append(label, value, nextPaletteColor_());
    }

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const override { return m_slices.size(); }

    /**
     * @brief Clears the current object state.
     */
    void clear() override {
        if (m_slices.isEmpty()) {
            return;
        }
        m_slices.clear();
        updated();
    }

    /**
     * @brief Returns the current slices.
     * @return The current slices.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwVector<Slice>& slices() const { return m_slices; }

    /**
     * @brief Sets the hole Size.
     * @param ratio Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHoleSize(double ratio) {
        if (!std::isfinite(ratio)) {
            return;
        }
        ratio = std::max(0.0, std::min(0.9, ratio));
        if (ratio == m_holeSize) {
            return;
        }
        m_holeSize = ratio;
        updated();
    }

    /**
     * @brief Returns the current hole Size.
     * @return The current hole Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double holeSize() const { return m_holeSize; }

    /**
     * @brief Sets the start Angle Degrees.
     * @param degrees Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStartAngleDegrees(double degrees) {
        if (!std::isfinite(degrees)) {
            return;
        }
        if (degrees == m_startAngleDegrees) {
            return;
        }
        m_startAngleDegrees = degrees;
        updated();
    }

    /**
     * @brief Returns the current angle Degrees.
     * @return The current angle Degrees.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double startAngleDegrees() const { return m_startAngleDegrees; }

    /**
     * @brief Sets the exploded.
     * @param index Value passed to the method.
     * @param on Value passed to the method.
     * @param explodeFactor Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setExploded(int index, bool on, double explodeFactor = 0.07) {
        if (index < 0 || index >= m_slices.size()) {
            return;
        }
        explodeFactor = std::max(0.0, std::min(0.3, explodeFactor));
        Slice& s = m_slices[index];
        if (s.exploded == on && s.explodeFactor == explodeFactor) {
            return;
        }
        s.exploded = on;
        s.explodeFactor = explodeFactor;
        updated();
    }

    /**
     * @brief Returns the current total Value.
     * @return The current total Value.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double totalValue() const {
        double total = 0.0;
        for (int i = 0; i < m_slices.size(); ++i) {
            const Slice& s = m_slices[i];
            if (std::isfinite(s.value) && s.value > 0.0) {
                total += s.value;
            }
        }
        return total;
    }

private:
    SwColor nextPaletteColor_() {
        static const SwColor palette[] = {
            SwColor{99, 102, 241},  // indigo
            SwColor{16, 185, 129},  // emerald
            SwColor{245, 158, 11},  // amber
            SwColor{239, 68, 68},   // red
            SwColor{14, 165, 233},  // sky
            SwColor{168, 85, 247},  // purple
            SwColor{236, 72, 153},  // pink
            SwColor{34, 197, 94},   // green
            SwColor{250, 204, 21}   // yellow
        };
        const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        const SwColor c = palette[m_paletteIndex % n];
        ++m_paletteIndex;
        return c;
    }

    SwVector<Slice> m_slices{};
    double m_holeSize{0.0};
    double m_startAngleDegrees{-90.0}; // nicer default (start at top)
    int m_paletteIndex{0};
};
