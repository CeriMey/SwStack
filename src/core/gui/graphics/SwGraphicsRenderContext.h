#pragma once
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

#include "Sw.h"
#include "graphics/SwGraphicsTypes.h"

#include <cmath>

struct SwGraphicsRenderContext {
    // Global widget rect of the view (same coordinate system as SwPainter / SwWidget).
    SwRect viewRect{0, 0, 0, 0};

    // Scene origin/scroll in scene coordinates.
    SwPointF scroll{0.0, 0.0};

    // Scene->view scale.
    double scale{1.0};

    SwPointF mapFromScene(const SwPointF& scenePoint) const {
        const double x = static_cast<double>(viewRect.x) + (scenePoint.x - scroll.x) * scale;
        const double y = static_cast<double>(viewRect.y) + (scenePoint.y - scroll.y) * scale;
        return SwPointF(x, y);
    }

    SwRect mapFromScene(const SwRectF& sceneRect) const {
        const SwPointF tl = mapFromScene(SwPointF(sceneRect.x, sceneRect.y));
        const SwPointF br = mapFromScene(SwPointF(sceneRect.x + sceneRect.width, sceneRect.y + sceneRect.height));
        const int x = static_cast<int>(std::lround(std::min(tl.x, br.x)));
        const int y = static_cast<int>(std::lround(std::min(tl.y, br.y)));
        const int w = static_cast<int>(std::lround(std::abs(br.x - tl.x)));
        const int h = static_cast<int>(std::lround(std::abs(br.y - tl.y)));
        return SwRect{x, y, w, h};
    }

    SwPointF mapToScene(const SwPointF& viewPoint) const {
        const double sx = ((viewPoint.x - static_cast<double>(viewRect.x)) / scale) + scroll.x;
        const double sy = ((viewPoint.y - static_cast<double>(viewRect.y)) / scale) + scroll.y;
        return SwPointF(sx, sy);
    }
};

