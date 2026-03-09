#pragma once

/**
 * @file src/core/gui/graphics/SwPen.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwPen in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the pen interface. The declarations exposed here define
 * the stable surface that adjacent code can rely on while the implementation remains free to
 * evolve behind the header.
 *
 * The main declarations in this header are SwPen.
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
 * @brief Provides the stroke description used by the painting and scene APIs.
 *
 * SwPen captures the minimal state needed to outline primitives in the current
 * graphics layer: color, width, and cosmetic behavior. It is intentionally
 * compact so pens can be passed by value through painter calls and graphics item
 * setters without introducing hidden allocation or backend coupling.
 */

#include "Sw.h"

/**
 * @brief Describes how outlines are rendered for vector primitives.
 *
 * A pen is typically consumed by SwPainter and by concrete SwGraphicsItem
 * subclasses when they need to stroke rectangles, ellipses, paths, or lines.
 */
class SwPen {
public:
    /**
     * @brief Constructs a `SwPen` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPen() = default;
    /**
     * @brief Constructs a `SwPen` instance.
     * @param c Value passed to the method.
     * @param w Width value.
     * @param w Width value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwPen(const SwColor& c, int w = 1) : m_color(c), m_width(w) {}

    /**
     * @brief Sets the color.
     * @param c Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColor(const SwColor& c) { m_color = c; }
    /**
     * @brief Returns the current color.
     * @return The current color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor color() const { return m_color; }

    /**
     * @brief Sets the width.
     * @param w Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWidth(int w) { m_width = w; }
    /**
     * @brief Returns the current width.
     * @return The current width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int width() const { return m_width; }

    /**
     * @brief Returns whether the object reports cosmetic.
     * @return `true` when the object reports cosmetic; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isCosmetic() const { return m_cosmetic; }
    /**
     * @brief Sets the cosmetic.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCosmetic(bool on) { m_cosmetic = on; }

private:
    SwColor m_color{0, 0, 0};
    int m_width{1};
    bool m_cosmetic{false};
};
