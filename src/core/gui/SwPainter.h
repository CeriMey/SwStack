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
 * Platform-agnostic painter interface used by widgets and styles.
 **************************************************************************************************/

#include "Sw.h"
#include "SwString.h"
#include "SwFont.h"

class SwImage;

class SwPainter {
public:
    virtual ~SwPainter() = default;

    virtual void clear(const SwColor& color) = 0;
    virtual void fillRect(const SwRect& rect,
                          const SwColor& fillColor,
                          const SwColor& borderColor,
                          int borderWidth) = 0;
    virtual void fillRoundedRect(const SwRect& rect,
                                 int radius,
                                 const SwColor& fillColor,
                                 const SwColor& borderColor,
                                 int borderWidth) = 0;
    virtual void drawRect(const SwRect& rect,
                          const SwColor& borderColor,
                          int borderWidth) = 0;
    virtual void drawLine(int x1,
                          int y1,
                          int x2,
                          int y2,
                          const SwColor& color,
                          int width) = 0;

    virtual void fillPolygon(const SwPoint* points,
                             int count,
                             const SwColor& fillColor,
                             const SwColor& borderColor,
                             int borderWidth) {
        if (!points || count <= 0) {
            return;
        }
        int minX = points[0].x;
        int maxX = points[0].x;
        int minY = points[0].y;
        int maxY = points[0].y;
        for (int i = 1; i < count; ++i) {
            if (points[i].x < minX) minX = points[i].x;
            if (points[i].x > maxX) maxX = points[i].x;
            if (points[i].y < minY) minY = points[i].y;
            if (points[i].y > maxY) maxY = points[i].y;
        }
        SwRect r{minX, minY, maxX - minX, maxY - minY};
        fillRect(r, fillColor, borderColor, borderWidth);
    }

    virtual void pushClipRect(const SwRect& rect) { SW_UNUSED(rect) }
    virtual void popClipRect() {}

    virtual void fillEllipse(const SwRect& rect,
                             const SwColor& fillColor,
                             const SwColor& borderColor,
                             int borderWidth) {
        fillRect(rect, fillColor, borderColor, borderWidth);
    }

    virtual void drawEllipse(const SwRect& rect,
                             const SwColor& borderColor,
                             int borderWidth) {
        drawRect(rect, borderColor, borderWidth);
    }

    // Minimal image hook for the graphics module (ARGB32).
    virtual void drawImage(const SwRect& targetRect,
                           const SwImage& image,
                           const SwRect* sourceRect = nullptr) {
        SW_UNUSED(targetRect)
        SW_UNUSED(image)
        SW_UNUSED(sourceRect)
    }

    virtual void drawText(const SwRect& rect,
                          const SwString& text,
                          DrawTextFormats alignment,
                          const SwColor& color,
                          const SwFont& font) = 0;
    virtual void finalize() {}
    virtual void* nativeHandle() { return nullptr; }
};
