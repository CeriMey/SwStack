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

#include "SwFont.h"
#include "SwString.h"
#include "Sw.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include <algorithm>

class SwFontMetrics {
public:
    explicit SwFontMetrics(const SwFont& font)
        : m_font(font) {}

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

    SwRect boundingRect(const SwString& text) const {
        const int w = std::max(0, horizontalAdvance(text));
        const int h = std::max(0, height());
        return SwRect{0, 0, w, h};
    }

private:
    int fallbackHeight_() const {
        const int pt = std::max(1, m_font.getPointSize());
        return std::max(1, static_cast<int>(pt * 1.4));
    }

    int fallbackWidth_(const SwString& text) const {
        const int pt = std::max(1, m_font.getPointSize());
        const int avg = std::max(4, static_cast<int>(pt * 0.6));
        return static_cast<int>(text.length()) * avg;
    }

    SwFont m_font;
};

