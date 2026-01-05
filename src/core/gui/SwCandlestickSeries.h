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
#include <limits>

class SwCandlestickSeries : public SwAbstractSeries {
    SW_OBJECT(SwCandlestickSeries, SwAbstractSeries)

public:
    struct Candle {
        double x{0.0};
        double open{0.0};
        double high{0.0};
        double low{0.0};
        double close{0.0};
    };

    explicit SwCandlestickSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    SeriesType type() const override { return SeriesType::Candlestick; }

    void append(double x, double open, double high, double low, double close) {
        if (!std::isfinite(x) || !std::isfinite(open) || !std::isfinite(high) || !std::isfinite(low) ||
            !std::isfinite(close)) {
            return;
        }

        Candle c;
        c.x = x;
        c.open = open;
        c.high = high;
        c.low = low;
        c.close = close;
        if (c.low > c.high) {
            std::swap(c.low, c.high);
        }

        if (maxPoints() > 0) {
            while (m_candles.size() >= maxPoints()) {
                m_candles.removeAt(0);
            }
        }
        m_candles.push_back(c);
        pointAdded(m_candles.size() - 1);
        updated();
    }

    int count() const override { return m_candles.size(); }

    void clear() override {
        if (m_candles.isEmpty()) {
            return;
        }
        m_candles.clear();
        updated();
    }

    void computeBounds(double& minX,
                       double& maxX,
                       double& minY,
                       double& maxY,
                       bool& hasPoint) const override {
        for (int i = 0; i < m_candles.size(); ++i) {
            const Candle& c = m_candles[i];
            if (!std::isfinite(c.x) || !std::isfinite(c.low) || !std::isfinite(c.high)) {
                continue;
            }
            if (!hasPoint) {
                minX = maxX = c.x;
                minY = c.low;
                maxY = c.high;
                hasPoint = true;
                continue;
            }
            minX = std::min(minX, c.x);
            maxX = std::max(maxX, c.x);
            minY = std::min(minY, c.low);
            maxY = std::max(maxY, c.high);
        }
    }

    bool lastX(double& outX) const override {
        if (m_candles.isEmpty()) {
            return false;
        }
        const Candle& c = m_candles.back();
        if (!std::isfinite(c.x)) {
            return false;
        }
        outX = c.x;
        return true;
    }

    const SwVector<Candle>& candles() const { return m_candles; }

    void setCandleWidth(double width) {
        if (!std::isfinite(width)) {
            return;
        }
        if (width < 0.0) {
            width = 0.0;
        }
        if (width == m_candleWidth) {
            return;
        }
        m_candleWidth = width;
        updated();
    }

    // Width in X axis units. 0 == auto.
    double candleWidth() const { return m_candleWidth; }

    void setIncreasingColor(const SwColor& color) {
        m_increasing = clampColor_(color);
        updated();
    }

    SwColor increasingColor() const { return m_increasing; }

    void setDecreasingColor(const SwColor& color) {
        m_decreasing = clampColor_(color);
        updated();
    }

    SwColor decreasingColor() const { return m_decreasing; }

private:
    SwVector<Candle> m_candles{};
    double m_candleWidth{0.0};
    SwColor m_increasing{16, 185, 129};
    SwColor m_decreasing{239, 68, 68};
};

