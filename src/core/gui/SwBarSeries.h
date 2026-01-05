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
    explicit SwBarSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    SeriesType type() const override { return SeriesType::Bar; }

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
    double barWidth() const { return m_barWidth; }

    void setBaseline(double baseline) {
        if (!std::isfinite(baseline) || baseline == m_baseline) {
            return;
        }
        m_baseline = baseline;
        updated();
    }

    double baseline() const { return m_baseline; }

private:
    double m_barWidth{0.0};
    double m_baseline{0.0};
};

