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

    explicit SwPieSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    SeriesType type() const override { return SeriesType::Pie; }
    bool isXYSeries() const override { return false; }

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

    void append(const SwString& label, double value) {
        append(label, value, nextPaletteColor_());
    }

    int count() const override { return m_slices.size(); }

    void clear() override {
        if (m_slices.isEmpty()) {
            return;
        }
        m_slices.clear();
        updated();
    }

    const SwVector<Slice>& slices() const { return m_slices; }

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

    double holeSize() const { return m_holeSize; }

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

    double startAngleDegrees() const { return m_startAngleDegrees; }

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

