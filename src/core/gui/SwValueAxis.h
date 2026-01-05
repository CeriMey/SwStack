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
 * SwValueAxis - minimal Qt-like numeric axis (QValueAxis-ish).
 **************************************************************************************************/

#include "SwObject.h"
#include "SwString.h"

#include <algorithm>
#include <cmath>

class SwValueAxis : public SwObject {
    SW_OBJECT(SwValueAxis, SwObject)

public:
    explicit SwValueAxis(SwObject* parent = nullptr)
        : SwObject(parent) {}

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

    void setMinimum(double minimum) { setRange(minimum, m_maximum); }
    void setMaximum(double maximum) { setRange(m_minimum, maximum); }

    double min() const { return m_minimum; }
    double max() const { return m_maximum; }

    void setAutoRange(bool on) {
        if (m_autoRange == on) {
            return;
        }
        m_autoRange = on;
        changed();
    }

    bool isAutoRange() const { return m_autoRange; }

    void setTickCount(int count) {
        count = std::max(2, count);
        if (m_tickCount == count) {
            return;
        }
        m_tickCount = count;
        changed();
    }

    int tickCount() const { return m_tickCount; }

    void setTitleText(const SwString& text) {
        if (m_titleText == text) {
            return;
        }
        m_titleText = text;
        changed();
    }

    SwString titleText() const { return m_titleText; }

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

