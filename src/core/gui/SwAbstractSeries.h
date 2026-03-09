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
 * @file src/core/gui/SwAbstractSeries.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwAbstractSeries in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the abstract series interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwAbstractSeries.
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
 * SwAbstractSeries - minimal XY series base.
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

    /**
     * @brief Constructs a `SwAbstractSeries` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwAbstractSeries(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Destroys the `SwAbstractSeries` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwAbstractSeries() = default;

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SeriesType type() const = 0;
    /**
     * @brief Returns whether the object reports xYSeries.
     * @return The current xYSeries.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual bool isXYSeries() const { return true; }

    /**
     * @brief Sets the name.
     * @param name Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setName(const SwString& name) {
        if (m_name == name) {
            return;
        }
        m_name = name;
        updated();
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const { return m_name; }

    /**
     * @brief Sets the color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColor(const SwColor& color) {
        const SwColor clamped = clampColor_(color);
        if (m_color.r == clamped.r && m_color.g == clamped.g && m_color.b == clamped.b) {
            return;
        }
        m_color = clamped;
        updated();
    }

    /**
     * @brief Returns the current color.
     * @return The current color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor color() const { return m_color; }

    /**
     * @brief Sets the visible.
     * @param on Value passed to the method.
     * @return The requested visible.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual void setVisible(bool on) {
        if (m_visible == on) {
            return;
        }
        m_visible = on;
        updated();
    }

    /**
     * @brief Returns whether the object reports visible.
     * @return `true` when the object reports visible; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Sets the max Points.
     * @param maxPoints Value passed to the method.
     * @return The requested max Points.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual void setMaxPoints(int maxPoints) {
        maxPoints = std::max(0, maxPoints);
        if (m_maxPoints == maxPoints) {
            return;
        }
        m_maxPoints = maxPoints;
        trimToMax_();
        updated();
    }

    /**
     * @brief Returns the current max Points.
     * @return The current max Points.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maxPoints() const { return m_maxPoints; }

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    virtual int count() const { return m_points.size(); }

    /**
     * @brief Returns the current points.
     * @return The current points.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual const SwVector<SwPointF>& points() const { return m_points; }

    /**
     * @brief Performs the `append` operation.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @return The requested append.
     */
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

    /**
     * @brief Returns the current clear.
     * @return The current clear.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void clear() {
        if (m_points.isEmpty()) {
            return;
        }
        m_points.clear();
        updated();
    }

    /**
     * @brief Performs the `computeBounds` operation.
     * @param minX Value passed to the method.
     * @param maxX Value passed to the method.
     * @param minY Value passed to the method.
     * @param maxY Value passed to the method.
     * @param hasPoint Value passed to the method.
     * @return The requested compute Bounds.
     */
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

    /**
     * @brief Performs the `lastX` operation.
     * @param outX Output value filled by the method.
     * @return The requested last X.
     */
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
    /**
     * @brief Performs the `clampInt_` operation.
     * @param value Value passed to the method.
     * @param minValue Value passed to the method.
     * @param maxValue Value passed to the method.
     * @return The requested clamp Int.
     */
    static int clampInt_(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    /**
     * @brief Performs the `clampColor_` operation.
     * @param c Value passed to the method.
     * @return The requested clamp Color.
     */
    static SwColor clampColor_(const SwColor& c) {
        return SwColor{clampInt_(c.r, 0, 255), clampInt_(c.g, 0, 255), clampInt_(c.b, 0, 255)};
    }

    /**
     * @brief Returns the current trim To Max.
     * @return The current trim To Max.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
