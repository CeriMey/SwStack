#pragma once

/**
 * @file src/core/gui/graphics/SwFontMetrics.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwFontMetrics in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the font metrics interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwFontMetrics.
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
 * @brief Declares text measurement helpers for SwFont.
 *
 * SwFontMetrics centralizes the small amount of font measurement logic needed by
 * widgets, text items, and layout code. It uses native Win32 metrics when
 * available and falls back to deterministic approximations on other platforms,
 * which keeps the API usable even when no platform text backend is wired yet.
 */

#include "SwFont.h"
#include "SwString.h"
#include "Sw.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include <algorithm>

/**
 * @brief Computes bounding information for text rendered with a given font.
 *
 * The class intentionally exposes only a narrow API: line height, horizontal
 * advance, and a simple bounding rectangle. Those queries cover the current
 * needs of the stack while keeping the implementation easy to port.
 */
class SwFontMetrics {
public:
    /**
     * @brief Constructs a `SwFontMetrics` instance.
     * @param font Font value used by the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwFontMetrics(const SwFont& font)
        : m_font(font) {}

    /**
     * @brief Returns the current height.
     * @return The current height.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int height() const {
#if defined(_WIN32)
        HDC dc = GetDC(nullptr);
        if (!dc) {
            return fallbackHeight_();
        }
        HFONT hFont = const_cast<SwFont&>(m_font).handle(dc);
        HFONT old = nullptr;
        if (hFont) {
            old = (HFONT)SelectObject(dc, hFont);
        }
        TEXTMETRICW tm{};
        int result = fallbackHeight_();
        if (GetTextMetricsW(dc, &tm)) {
            result = static_cast<int>(tm.tmHeight);
        }
        if (old) {
            SelectObject(dc, old);
        }
        ReleaseDC(nullptr, dc);
        return result;
#else
        return fallbackHeight_();
#endif
    }

    /**
     * @brief Performs the `horizontalAdvance` operation.
     * @param text Value passed to the method.
     * @return The requested horizontal Advance.
     */
    int horizontalAdvance(const SwString& text) const {
#if defined(_WIN32)
        std::wstring wide = text.toStdWString();
        HDC dc = GetDC(nullptr);
        if (!dc) {
            return fallbackWidth_(text);
        }
        HFONT hFont = const_cast<SwFont&>(m_font).handle(dc);
        HFONT old = nullptr;
        if (hFont) {
            old = (HFONT)SelectObject(dc, hFont);
        }
        SIZE sz{};
        int result = fallbackWidth_(text);
        if (!wide.empty()) {
            if (GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &sz)) {
                result = static_cast<int>(sz.cx);
            }
        } else {
            result = 0;
        }
        if (old) {
            SelectObject(dc, old);
        }
        ReleaseDC(nullptr, dc);
        return result;
#else
        return fallbackWidth_(text);
#endif
    }

    /**
     * @brief Performs the `boundingRect` operation.
     * @param text Value passed to the method.
     * @return The requested bounding Rect.
     */
    SwRect boundingRect(const SwString& text) const {
        const int w = std::max(0, horizontalAdvance(text));
        const int h = std::max(0, height());
        return SwRect{0, 0, w, h};
    }

private:
    int fallbackHeight_() const {
        const int px = m_font.getPixelSize() > 0
                           ? m_font.getPixelSize()
                           : static_cast<int>(std::max(1, m_font.getPointSize()) * 96.0 / 72.0 + 0.5);
        return std::max(1, static_cast<int>(px * 1.4));
    }

    int fallbackWidth_(const SwString& text) const {
        const int px = m_font.getPixelSize() > 0
                           ? m_font.getPixelSize()
                           : static_cast<int>(std::max(1, m_font.getPointSize()) * 96.0 / 72.0 + 0.5);
        const int avg = std::max(4, static_cast<int>(px * 0.6));
        return static_cast<int>(text.length()) * avg;
    }

    SwFont m_font;
};
