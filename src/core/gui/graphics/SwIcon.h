#pragma once

/**
 * @file src/core/gui/graphics/SwIcon.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwIcon in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the icon interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwIcon.
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
 * @brief Declares SwIcon, a small multi-resolution icon container.
 *
 * SwIcon groups one or more pixmaps that represent the same semantic asset at
 * different sizes. The current implementation keeps selection logic simple by
 * choosing the closest stored pixmap for a requested target size, which is
 * sufficient for menus, buttons, and lightweight custom widgets.
 */

#include "graphics/SwPixmap.h"

#include <vector>

/**
 * @brief Stores a set of pixmaps that can be queried by requested size.
 */
class SwIcon {
public:
    /**
     * @brief Constructs a `SwIcon` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwIcon() = default;
    /**
     * @brief Constructs a `SwIcon` instance.
     * @param pix Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwIcon(const SwPixmap& pix) {
        addPixmap(pix);
    }

    /**
     * @brief Adds the specified pixmap.
     * @param pix Value passed to the method.
     */
    void addPixmap(const SwPixmap& pix) {
        if (!pix.isNull()) {
            m_pixmaps.push_back(pix);
        }
    }

    /**
     * @brief Performs the `pixmap` operation.
     * @param w Width value.
     * @param h Height value.
     * @return The requested pixmap.
     */
    SwPixmap pixmap(int w, int h) const {
        if (m_pixmaps.empty()) {
            return SwPixmap();
        }
        // Nearest-size selection (simple).
        const int target = w * w + h * h;
        size_t best = 0;
        int bestScore = 0x7fffffff;
        for (size_t i = 0; i < m_pixmaps.size(); ++i) {
            const int dw = m_pixmaps[i].width();
            const int dh = m_pixmaps[i].height();
            const int score = std::abs(dw * dw + dh * dh - target);
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        return m_pixmaps[best];
    }

private:
    std::vector<SwPixmap> m_pixmaps;
};
