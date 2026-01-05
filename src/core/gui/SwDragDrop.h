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
 * SwDragDrop - small internal drag visual helper (Qt-like).
 *
 * Goal:
 * - Provide a lightweight "drag pixmap / + badge" feedback for in-app drags (palette, views, etc.).
 * - No OS drag&drop; purely visual and driven by mouse events.
 **************************************************************************************************/

#include "SwGuiApplication.h"
#include "SwPainter.h"
#include "SwString.h"
#include "SwWidgetPlatformAdapter.h"

#include <algorithm>

class SwDragDrop {
public:
    static SwDragDrop& instance() {
        static SwDragDrop s;
        return s;
    }

    bool isActive() const { return m_active; }
    bool dropAllowed() const { return m_dropAllowed; }

    void begin(const SwWidgetPlatformHandle& windowHandle,
               const SwString& text,
               const SwFont& font,
               int globalX,
               int globalY,
               bool showPlus = true,
               bool dropAllowed = true) {
        if (!windowHandle) {
            return;
        }

        if (m_active) {
            end();
        }

        m_handle = windowHandle;
        m_text = text;
        m_font = font;
        m_x = globalX;
        m_y = globalY;
        m_showPlus = showPlus;
        m_dropAllowed = dropAllowed;
        m_active = true;

        invalidate_(overlayRect_(m_x, m_y));
    }

    void setDropAllowed(bool allowed) {
        if (!m_active) {
            return;
        }
        if (m_dropAllowed == allowed) {
            return;
        }
        m_dropAllowed = allowed;
        invalidate_(overlayRect_(m_x, m_y));
    }

    void updatePosition(int globalX, int globalY) {
        if (!m_active) {
            return;
        }
        const SwRect oldR = overlayRect_(m_x, m_y);
        m_x = globalX;
        m_y = globalY;
        const SwRect newR = overlayRect_(m_x, m_y);
        invalidate_(unionRect_(oldR, newR));
    }

    void end() {
        if (!m_active) {
            return;
        }
        invalidate_(overlayRect_(m_x, m_y));
        m_active = false;
        m_text = SwString();
        m_dropAllowed = true;
        m_handle = SwWidgetPlatformHandle{};
    }

    void paintOverlay(SwPainter* painter) const {
        if (!m_active || !painter) {
            return;
        }

        const SwRect bubble = overlayRect_(m_x, m_y);
        if (bubble.width <= 0 || bubble.height <= 0) {
            return;
        }

        const SwColor bg{255, 255, 255};
        const SwColor accent = m_dropAllowed ? SwColor{59, 130, 246} : SwColor{239, 68, 68};
        const SwColor border = accent;
        const SwColor textColor{15, 23, 42};

        painter->fillRoundedRect(bubble, 10, bg, border, 1);

        const int padX = 10;
        const int iconSize = 16;
        const int iconGap = (m_showPlus ? 8 : 0);

        SwRect textRect = bubble;
        textRect.x += padX;
        textRect.width = std::max(0, bubble.width - padX * 2 - (m_showPlus ? (iconSize + iconGap) : 0));

        if (!m_text.isEmpty() && textRect.width > 0) {
            painter->drawText(textRect,
                              m_text,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              m_font);
        }

        if (m_showPlus) {
            SwRect iconRect{
                bubble.x + bubble.width - padX - iconSize,
                bubble.y + (bubble.height - iconSize) / 2,
                iconSize,
                iconSize};

            const SwColor badgeBg = accent;
            const SwColor badgeFg{255, 255, 255};
            painter->fillEllipse(iconRect, badgeBg, badgeBg, 0);

            const int cx = iconRect.x + iconRect.width / 2;
            const int cy = iconRect.y + iconRect.height / 2;
            if (m_dropAllowed) {
                const int arm = 4;
                painter->drawLine(cx - arm, cy, cx + arm, cy, badgeFg, 2);
                painter->drawLine(cx, cy - arm, cx, cy + arm, badgeFg, 2);
            } else {
                const int pad = 4;
                painter->drawLine(iconRect.x + pad,
                                  iconRect.y + iconRect.height - pad,
                                  iconRect.x + iconRect.width - pad,
                                  iconRect.y + pad,
                                  badgeFg,
                                  2);
            }
        }
    }

private:
    SwDragDrop() = default;

    static SwRect unionRect_(const SwRect& a, const SwRect& b) {
        const int x1 = std::min(a.x, b.x);
        const int y1 = std::min(a.y, b.y);
        const int x2 = std::max(a.x + a.width, b.x + b.width);
        const int y2 = std::max(a.y + a.height, b.y + b.height);
        return SwRect{x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
    }

    int textWidthPx_(const SwString& text) const {
        if (!m_handle || text.isEmpty()) {
            return 0;
        }
        const int fallback = static_cast<int>(text.size()) * 7;
        int w = SwWidgetPlatformAdapter::textWidthUntil(m_handle, text, m_font, text.size(), fallback);
        if (w < 0) w = 0;
        return w;
    }

    SwRect overlayRect_(int globalX, int globalY) const {
        const int dx = 18;
        const int dy = 18;
        const int h = 26;

        const int padX = 10;
        const int iconSize = m_showPlus ? 16 : 0;
        const int iconGap = m_showPlus ? 8 : 0;

        const int textW = textWidthPx_(m_text);
        int w = std::max(30, padX + textW + (m_showPlus ? (iconGap + iconSize) : 0) + padX);

        const SwRect client = SwWidgetPlatformAdapter::clientRect(m_handle);
        int maxW = 360;
        if (client.width > 0) {
            const int margin = 8;
            maxW = std::min(maxW, std::max(1, client.width - margin * 2));
        }
        w = std::max(1, std::min(w, maxW));

        SwRect r{globalX + dx, globalY + dy, w, h};
        return clampToClient_(r);
    }

    SwRect clampToClient_(const SwRect& rect) const {
        SwRect r = rect;
        if (!m_handle) {
            return r;
        }
        const SwRect client = SwWidgetPlatformAdapter::clientRect(m_handle);
        if (client.width <= 0 || client.height <= 0) {
            return r;
        }

        r.width = std::max(1, std::min(r.width, client.width));
        r.height = std::max(1, std::min(r.height, client.height));

        const int maxX = client.x + client.width - r.width;
        const int maxY = client.y + client.height - r.height;

        if (r.x < client.x) r.x = client.x;
        if (r.y < client.y) r.y = client.y;
        if (r.x > maxX) r.x = maxX;
        if (r.y > maxY) r.y = maxY;

        return r;
    }

    void invalidate_(const SwRect& rect) const {
        if (!m_handle) {
            return;
        }
        SwWidgetPlatformAdapter::invalidateRect(m_handle, rect);
        if (auto* guiApp = SwGuiApplication::instance(false)) {
            if (auto* integration = guiApp->platformIntegration()) {
                integration->wakeUpGuiThread();
            }
        }
    }

    SwWidgetPlatformHandle m_handle{};
    SwString m_text;
    SwFont m_font;
    int m_x{0};
    int m_y{0};
    bool m_showPlus{true};
    bool m_dropAllowed{true};
    bool m_active{false};
};
