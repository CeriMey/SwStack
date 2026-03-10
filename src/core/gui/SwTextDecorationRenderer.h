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

#include "SwPainter.h"
#include "SwTextFormat.h"

#include <algorithm>

inline SwColor swResolvedUnderlineColor(const SwTextCharFormat& format, const SwColor& fallbackColor) {
    if (format.hasUnderlineColor()) {
        return format.underlineColor();
    }
    if (format.hasForeground()) {
        return format.foreground();
    }
    return fallbackColor;
}

inline void swDrawCustomUnderline(SwPainter* painter,
                                  const SwRect& rect,
                                  int textWidth,
                                  const SwTextCharFormat& format,
                                  const SwColor& fallbackColor) {
    if (!painter || textWidth <= 0) {
        return;
    }

    if (!format.hasUnderlineStyle()) {
        return;
    }

    const SwTextCharFormat::UnderlineStyle style = format.underlineStyle();
    if (style == SwTextCharFormat::NoUnderline || style == SwTextCharFormat::SingleUnderline) {
        return;
    }

    const SwColor underlineColor = swResolvedUnderlineColor(format, fallbackColor);
    const int left = rect.x;
    const int right = rect.x + textWidth;
    const int baselineY = rect.y + std::max(2, rect.height - 4);
    const int crestY = std::max(rect.y, baselineY - 2);
    const int cycle = 4;

    int x = left;
    while (x < right) {
        const int mid = std::min(x + cycle / 2, right);
        const int next = std::min(x + cycle, right);
        if (mid > x) {
            painter->drawLine(x, baselineY, mid, crestY, underlineColor, 1);
        }
        if (next > mid) {
            painter->drawLine(mid, crestY, next, baselineY, underlineColor, 1);
        }
        x = next;
    }
}
