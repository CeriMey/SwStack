#pragma once

/**
 * @file src/core/gui/graphics/SwGraphicsRenderContext.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwGraphicsRenderContext in the CoreSw graphics
 * layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the graphics render context interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwGraphicsRenderContext.
 *
 * Context-oriented declarations here aggregate the state and helper access that a focused
 * operation needs while avoiding wide parameter lists at call sites.
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

/***************************************************************************************************
 * SwGraphicsRenderContext — Scene-to-view coordinate mapping context.
 *
 * Passed to SwGraphicsItem::paint() so that items can convert scene coordinates
 * to view (pixel) coordinates.
 **************************************************************************************************/

/**
 * @file
 * @brief Declares the mapping context shared with scene items during painting.
 *
 * The render context bundles the current viewport rectangle, scroll offset,
 * scale factor, and optional view transform. Graphics items receive it so they
 * can convert their geometry between scene and device coordinates without
 * duplicating view-specific math in every paint implementation.
 */

#include "Sw.h"
#include "graphics/SwGraphicsTypes.h"

#include <cmath>

/**
 * @brief Carries the effective scene-to-device mapping for one paint pass.
 */
struct SwGraphicsRenderContext {
    // Global widget rect of the view (same coordinate system as SwPainter / SwWidget).
    SwRect viewRect{0, 0, 0, 0};

    // Scene origin/scroll in scene coordinates.
    SwPointF scroll{0.0, 0.0};

    // Scene->view scale.
    double scale{1.0};

    // Optional view-level transform (rotation, shear, etc.).
    SwTransform viewTransform{};

    // ---------------------------------------------------------------
    // Scene -> view mapping
    // ---------------------------------------------------------------
    /**
     * @brief Performs the `mapFromScene` operation.
     * @param scenePoint Value passed to the method.
     * @return The requested map From Scene.
     */
    SwPointF mapFromScene(const SwPointF& scenePoint) const {
        // Apply view transform first (if any), then scroll + scale
        SwPointF p = viewTransform.isIdentity() ? scenePoint : viewTransform.map(scenePoint);
        double x = static_cast<double>(viewRect.x) + (p.x - scroll.x) * scale;
        double y = static_cast<double>(viewRect.y) + (p.y - scroll.y) * scale;
        return SwPointF(x, y);
    }

    /**
     * @brief Performs the `mapFromScene` operation.
     * @param sceneRect Value passed to the method.
     * @return The requested map From Scene.
     */
    SwRect mapFromScene(const SwRectF& sceneRect) const {
        const SwPointF p1 = mapFromScene(SwPointF(sceneRect.x, sceneRect.y));
        const SwPointF p2 = mapFromScene(SwPointF(sceneRect.x + sceneRect.width, sceneRect.y));
        const SwPointF p3 = mapFromScene(SwPointF(sceneRect.x + sceneRect.width,
                                                  sceneRect.y + sceneRect.height));
        const SwPointF p4 = mapFromScene(SwPointF(sceneRect.x, sceneRect.y + sceneRect.height));
        const double minX = std::min(std::min(p1.x, p2.x), std::min(p3.x, p4.x));
        const double minY = std::min(std::min(p1.y, p2.y), std::min(p3.y, p4.y));
        const double maxX = std::max(std::max(p1.x, p2.x), std::max(p3.x, p4.x));
        const double maxY = std::max(std::max(p1.y, p2.y), std::max(p3.y, p4.y));
        int x = static_cast<int>(std::lround(minX));
        int y = static_cast<int>(std::lround(minY));
        int w = static_cast<int>(std::lround(maxX - minX));
        int h = static_cast<int>(std::lround(maxY - minY));
        return SwRect{x, y, w, h};
    }

    // ---------------------------------------------------------------
    // View -> scene mapping
    // ---------------------------------------------------------------
    /**
     * @brief Performs the `mapToScene` operation.
     * @param viewPoint Value passed to the method.
     * @return The requested map To Scene.
     */
    SwPointF mapToScene(const SwPointF& viewPoint) const {
        double sx = ((viewPoint.x - static_cast<double>(viewRect.x)) / scale) + scroll.x;
        double sy = ((viewPoint.y - static_cast<double>(viewRect.y)) / scale) + scroll.y;
        SwPointF p(sx, sy);
        if (!viewTransform.isIdentity()) p = viewTransform.inverted().map(p);
        return p;
    }

    // ---------------------------------------------------------------
    // Effective device transform (combine item + view)
    // ---------------------------------------------------------------
    /**
     * @brief Returns the current device Transform.
     * @return The current device Transform.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwTransform deviceTransform() const {
        const SwPointF origin = mapFromScene(SwPointF(0.0, 0.0));
        const SwPointF ex = mapFromScene(SwPointF(1.0, 0.0));
        const SwPointF ey = mapFromScene(SwPointF(0.0, 1.0));
        return SwTransform(ex.x - origin.x, ex.y - origin.y, 0.0,
                           ey.x - origin.x, ey.y - origin.y, 0.0,
                           origin.x, origin.y, 1.0);
    }
};
