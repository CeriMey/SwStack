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
    explicit SwAreaSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    SeriesType type() const override { return SeriesType::Area; }

    void setBaseline(double baseline) {
        if (!std::isfinite(baseline) || baseline == m_baseline) {
            return;
        }
        m_baseline = baseline;
        updated();
    }

    double baseline() const { return m_baseline; }

    void setFillColor(const SwColor& color) {
        const SwColor clamped = clampColor_(color);
        if (m_fillColor.r == clamped.r && m_fillColor.g == clamped.g && m_fillColor.b == clamped.b) {
            return;
        }
        m_fillColor = clamped;
        m_hasFillColor = true;
        updated();
    }

    bool hasFillColor() const { return m_hasFillColor; }
    SwColor fillColor() const { return m_fillColor; }

    void setBorderWidth(int width) {
        width = std::max(0, std::min(12, width));
        if (m_borderWidth == width) {
            return;
        }
        m_borderWidth = width;
        updated();
    }

    int borderWidth() const { return m_borderWidth; }

private:
    double m_baseline{0.0};
    bool m_hasFillColor{false};
    SwColor m_fillColor{191, 219, 254};
    int m_borderWidth{2};
};

