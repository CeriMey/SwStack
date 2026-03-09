#pragma once

/**
 * @file src/core/gui/graphics/SwBrush.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwBrush in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the brush interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwGradient and SwBrush.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Graphics-facing declarations here define the data flow from high-level UI state to lower-level
 * rendering backends.
 *
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

/**
 * @file
 * @brief Declares fill descriptions used by the graphics stack.
 *
 * This header provides both the gradient payload and the brush abstraction used
 * to fill vector geometry. The current implementation focuses on the small set
 * of fill modes required by the stack today: no fill, solid color, and linear
 * gradient. Keeping the API compact makes it straightforward to serialize style
 * state and forward it to painting backends.
 */

#include "Sw.h"
#include "SwGraphicsTypes.h"

/**
 * @brief Stores the endpoints and colors of a linear gradient fill.
 */
class SwGradient {
public:
    /**
     * @brief Constructs a `SwGradient` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGradient() = default;
    /**
     * @brief Constructs a `SwGradient` instance.
     * @param start Value passed to the method.
     * @param end Value passed to the method.
     * @param a Value passed to the method.
     * @param b Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGradient(const SwPointF& start, const SwPointF& end, const SwColor& a, const SwColor& b)
        : m_start(start), m_end(end), m_color0(a), m_color1(b) {}

    /**
     * @brief Returns the current start.
     * @return The current start.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF start() const { return m_start; }
    /**
     * @brief Returns the current final Stop.
     * @return The current final Stop.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF finalStop() const { return m_end; }
    /**
     * @brief Returns the current color0.
     * @return The current color0.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor color0() const { return m_color0; }
    /**
     * @brief Returns the current color1.
     * @return The current color1.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor color1() const { return m_color1; }

private:
    SwPointF m_start{};
    SwPointF m_end{};
    SwColor m_color0{0, 0, 0};
    SwColor m_color1{255, 255, 255};
};

/**
 * @brief Describes how closed geometry should be filled.
 *
 * A brush can either be empty, use a flat color, or reference a linear
 * gradient. Concrete graphics items and painter helpers read this object when
 * producing fills for shapes and text backgrounds.
 */
class SwBrush {
public:
    enum Style {
        NoBrush,
        SolidPattern,
        LinearGradientPattern
    };

    /**
     * @brief Constructs a `SwBrush` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwBrush() = default;
    /**
     * @brief Constructs a `SwBrush` instance.
     * @param c Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwBrush(const SwColor& c) : m_style(SolidPattern), m_color(c) {}
    /**
     * @brief Constructs a `SwBrush` instance.
     * @param g Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwBrush(const SwGradient& g) : m_style(LinearGradientPattern), m_gradient(g) {}

    /**
     * @brief Returns the current style.
     * @return The current style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Style style() const { return m_style; }

    /**
     * @brief Sets the color.
     * @param c Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColor(const SwColor& c) {
        m_style = SolidPattern;
        m_color = c;
    }
    /**
     * @brief Returns the current color.
     * @return The current color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor color() const { return m_color; }

    /**
     * @brief Sets the gradient.
     * @param g Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setGradient(const SwGradient& g) {
        m_style = LinearGradientPattern;
        m_gradient = g;
    }
    /**
     * @brief Returns the current gradient.
     * @return The current gradient.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGradient gradient() const { return m_gradient; }

private:
    Style m_style{NoBrush};
    SwColor m_color{0, 0, 0};
    SwGradient m_gradient{};
};
