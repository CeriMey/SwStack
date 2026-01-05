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

#include "graphics/SwPixmap.h"

#include <vector>

class SwIcon {
public:
    SwIcon() = default;
    explicit SwIcon(const SwPixmap& pix) {
        addPixmap(pix);
    }

    void addPixmap(const SwPixmap& pix) {
        if (!pix.isNull()) {
            m_pixmaps.push_back(pix);
        }
    }

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

