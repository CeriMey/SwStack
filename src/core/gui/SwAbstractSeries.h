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
 * SwAbstractSeries - minimal QtCharts-like XY series base.
 **************************************************************************************************/

#include "SwObject.h"
#include "SwString.h"
#include "SwVector.h"

#include "Sw.h"
#include "graphics/SwGraphicsTypes.h"

#include <algorithm>
#include <cmath>
#include <iterator>

class SwAbstractSeries : public SwObject {
    SW_OBJECT(SwAbstractSeries, SwObject)

public:
    enum class SeriesType {
        Line,
        Scatter,
        StepLine,
        Spline,
        Area,
        Bar,
        Candlestick,
        Pie
    };

    explicit SwAbstractSeries(SwObject* parent = nullptr)
        : SwObject(parent) {}

    virtual ~SwAbstractSeries() = default;

    virtual SeriesType type() const = 0;
    virtual bool isXYSeries() const { return true; }

    void setName(const SwString& name) {
        if (m_name == name) {
            return;
        }
        m_name = name;
        updated();
    }

    SwString name() const { return m_name; }

    void setColor(const SwColor& color) {
        const SwColor clamped = clampColor_(color);
        if (m_color.r == clamped.r && m_color.g == clamped.g && m_color.b == clamped.b) {
            return;
        }
        m_color = clamped;
        updated();
    }

    SwColor color() const { return m_color; }

    virtual void setVisible(bool on) {
        if (m_visible == on) {
            return;
        }
        m_visible = on;
        updated();
    }

    bool isVisible() const { return m_visible; }

    virtual void setMaxPoints(int maxPoints) {
        maxPoints = std::max(0, maxPoints);
        if (m_maxPoints == maxPoints) {
            return;
        }
        m_maxPoints = maxPoints;
        trimToMax_();
        updated();
    }

    int maxPoints() const { return m_maxPoints; }

    virtual int count() const { return m_points.size(); }

    virtual const SwVector<SwPointF>& points() const { return m_points; }

    virtual void append(double x, double y) {
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return;
        }

        if (m_maxPoints > 0) {
            const int overflow = (m_points.size() + 1) - m_maxPoints;
            if (overflow > 0 && m_points.size() > 0) {
                auto beginIt = m_points.begin();
                auto endIt = beginIt;
                std::advance(endIt, std::min(overflow, m_points.size()));
                m_points.erase(beginIt, endIt);
            }
        }

        m_points.push_back(SwPointF(x, y));
        pointAdded(m_points.size() - 1);
        updated();
    }

    virtual void clear() {
        if (m_points.isEmpty()) {
            return;
        }
        m_points.clear();
        updated();
    }

    virtual void computeBounds(double& minX,
                               double& maxX,
                               double& minY,
                               double& maxY,
                               bool& hasPoint) const {
        const SwVector<SwPointF>& pts = points();
        for (int i = 0; i < pts.size(); ++i) {
            const SwPointF p = pts[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            if (!hasPoint) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                hasPoint = true;
                continue;
            }
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }

    virtual bool lastX(double& outX) const {
        const SwVector<SwPointF>& pts = points();
        if (pts.isEmpty()) {
            return false;
        }
        const SwPointF p = pts.back();
        if (!std::isfinite(p.x)) {
            return false;
        }
        outX = p.x;
        return true;
    }

    DECLARE_SIGNAL(pointAdded, int);
    DECLARE_SIGNAL_VOID(updated);

protected:
    static int clampInt_(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor_(const SwColor& c) {
        return SwColor{clampInt_(c.r, 0, 255), clampInt_(c.g, 0, 255), clampInt_(c.b, 0, 255)};
    }

    virtual void trimToMax_() {
        if (m_maxPoints <= 0) {
            return;
        }
        if (m_points.size() <= m_maxPoints) {
            return;
        }
        const int overflow = m_points.size() - m_maxPoints;
        if (overflow <= 0) {
            return;
        }
        auto beginIt = m_points.begin();
        auto endIt = beginIt;
        std::advance(endIt, std::min(overflow, m_points.size()));
        m_points.erase(beginIt, endIt);
    }

    SwVector<SwPointF> m_points{};
    int m_maxPoints{0};
    bool m_visible{true};
    SwColor m_color{0, 120, 215};
    SwString m_name{};
};
