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
 * SwMenuBar - Qt-like menu bar widget (≈ QMenuBar).
 *
 * Focus:
 * - Lightweight in-client menu bar (not OS-native).
 * - Works with SwMenu popups + snapshot-based validation.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwMenu.h"
#include "SwWidgetPlatformAdapter.h"

#include "core/types/SwVector.h"

#include <algorithm>

class SwMenuBar : public SwFrame {
    SW_OBJECT(SwMenuBar, SwFrame)

public:
    explicit SwMenuBar(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
    }

    SwMenu* addMenu(const SwString& title) {
        auto* menu = new SwMenu(this);
        hookMenuLifecycle_(menu);
        MenuEntry entry;
        entry.title = title;
        entry.menu = menu;
        m_leftMenus.push_back(entry);
        updateGeometryCache();
        update();
        return menu;
    }

    SwMenu* addMenuRight(const SwString& title) {
        auto* menu = new SwMenu(this);
        hookMenuLifecycle_(menu);
        MenuEntry entry;
        entry.title = title;
        entry.menu = menu;
        m_rightMenus.push_back(entry);
        updateGeometryCache();
        update();
        return menu;
    }

    int menuCount() const { return m_leftMenus.size() + m_rightMenus.size(); }

    SwMenu* menuAt(int index) const {
        const MenuEntry* entry = entryAt_(index);
        if (!entry) {
            return nullptr;
        }
        return entry->menu;
    }

    SwRect menuRect(int index) const {
        if (index < 0 || index >= m_menuRects.size()) {
            return {};
        }
        return m_menuRects[index];
    }

    void closeActiveMenu() {
        if (m_activeIndex >= 0 && m_activeIndex < menuCount()) {
            if (SwMenu* menu = menuAt(m_activeIndex)) {
                menu->hide();
            }
        }
        m_activeIndex = -1;
        update();
    }

protected:
    CUSTOM_PROPERTY(int, HoverIndex, -1) { update(); }

    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateGeometryCache();
    }

    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();
        StyleSheet* sheet = getToolSheet();

        SwColor bg{248, 250, 252};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{226, 232, 240};
        int borderWidth = 1;
        int radius = 0;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRect(bounds, bg, bg, 0);
        }
        if (borderWidth > 0) {
            const int yLine = bounds.y + std::max(0, bounds.height - borderWidth);
            painter->fillRect(SwRect{bounds.x, yLine, bounds.width, borderWidth}, border, border, 0);
        }

        const SwColor textColor{51, 65, 85};
        const SwColor textActive{24, 28, 36};
        const SwColor hoverFill{241, 245, 249};

        const int total = std::min(menuCount(), m_menuRects.size());
        for (int i = 0; i < total; ++i) {
            const SwRect r = m_menuRects[i];
            const bool hovered = (i == getHoverIndex());
            const bool active = (i == m_activeIndex);

            if (hovered || active) {
                SwRect hi{r.x, r.y + 2, r.width, std::max(0, r.height - 4)};
                painter->fillRoundedRect(hi, 6, hoverFill, hoverFill, 0);
            }

            painter->drawText(r,
                              entryAt_(i) ? entryAt_(i)->title : SwString(),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              active ? textActive : textColor,
                              getFont());
        }

        painter->finalize();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const int idx = indexAt(event->x(), event->y());
        if (idx != getHoverIndex()) {
            setHoverIndex(idx);
        }

        // Only allow hover-to-switch when a menu popup is actually open.
        // (If a popup was closed by an outside click, action trigger, or Escape,
        // we must not stay in "menu mode".)
        SwMenu* active = (m_activeIndex >= 0 && m_activeIndex < menuCount()) ? menuAt(m_activeIndex) : nullptr;
        const bool menuMode = active && active->isVisible();
        if (menuMode && idx >= 0 && idx != m_activeIndex) {
            openMenu(idx);
        }

        event->accept();
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const int idx = indexAt(event->x(), event->y());
        if (idx < 0) {
            SwFrame::mousePressEvent(event);
            return;
        }

        if (m_activeIndex == idx) {
            closeActiveMenu();
        } else {
            openMenu(idx);
        }
        event->accept();
    }

private:
    struct MenuEntry {
        SwString title;
        SwMenu* menu{nullptr};
    };

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    void initDefaults() {
        resize(560, 34);
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setFrameShape(Shape::NoFrame);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setStyleSheet(R"(
            SwMenuBar {
                background-color: rgb(248, 250, 252);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 0px;
            }
        )");
    }

    int indexAt(int px, int py) const {
        for (int i = 0; i < m_menuRects.size(); ++i) {
            const SwRect r = m_menuRects[i];
            if (px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height)) {
                return i;
            }
        }
        return -1;
    }

    void updateGeometryCache() {
        m_menuRects.clear();
        const int leftCount = m_leftMenus.size();
        const int rightCount = m_rightMenus.size();
        if (leftCount + rightCount == 0) {
            return;
        }

        const auto handle = nativeWindowHandle();
        const SwFont font = getFont();

        auto measureWidth = [&](const SwString& title) -> int {
            const int tw = SwWidgetPlatformAdapter::textWidthUntil(handle, title, font, title.size(), 5000);
            return clampInt(tw + 22, 46, 220);
        };

        SwVector<int> leftWidths;
        leftWidths.reserve(m_leftMenus.count());
        for (int i = 0; i < m_leftMenus.size(); ++i) {
            leftWidths.push_back(measureWidth(m_leftMenus[i].title));
        }

        SwVector<int> rightWidths;
        rightWidths.reserve(m_rightMenus.count());
        for (int i = 0; i < m_rightMenus.size(); ++i) {
            rightWidths.push_back(measureWidth(m_rightMenus[i].title));
        }

        const SwRect bounds = getRect();
        const int left = bounds.x + m_margin;
        const int right = bounds.x + std::max(0, bounds.width - m_margin);
        const int y = bounds.y;
        const int h = bounds.height;

        int rightGroupWidth = 0;
        for (int i = 0; i < rightWidths.size(); ++i) {
            rightGroupWidth += std::max(0, rightWidths[i]);
        }
        if (rightCount > 1) {
            rightGroupWidth += m_spacing * (rightCount - 1);
        }
        const int rightGroupLeft = right - rightGroupWidth;
        const int rightLimitForLeft = (rightCount > 0) ? std::max(left, rightGroupLeft - m_spacing) : right;

        int x = left;
        for (int i = 0; i < leftCount; ++i) {
            const int w = leftWidths[i];
            SwRect r{x, y, w, h};
            if (r.x + r.width > rightLimitForLeft) {
                r.width = std::max(0, rightLimitForLeft - r.x);
            }
            m_menuRects.push_back(r);
            x += w + m_spacing;
        }

        int xr = right;
        for (int i = 0; i < rightCount; ++i) {
            const int w = rightWidths[i];
            SwRect r{xr - w, y, w, h};
            if (r.x < left) {
                r.width = std::max(0, xr - left);
                r.x = left;
            }
            m_menuRects.push_back(r);
            xr = r.x - m_spacing;
        }
    }

    void openMenu(int index) {
        if (index < 0 || index >= menuCount()) {
            return;
        }

        if (m_activeIndex >= 0 && m_activeIndex < menuCount()) {
            if (SwMenu* old = menuAt(m_activeIndex)) {
                old->hide();
            }
        }

        m_activeIndex = index;
        SwMenu* menu = menuAt(index);
        if (!menu) {
            update();
            return;
        }

        const SwRect r = menuRect(index);
        // Allow auto-close when the cursor is no longer over the menubar entry nor the popup.
        menu->popup(r.x, r.y + r.height + 4, r);
        update();
    }

    const MenuEntry* entryAt_(int index) const {
        if (index < 0) {
            return nullptr;
        }
        if (index < m_leftMenus.size()) {
            return &m_leftMenus[index];
        }
        const int r = index - m_leftMenus.size();
        if (r < 0 || r >= m_rightMenus.size()) {
            return nullptr;
        }
        return &m_rightMenus[r];
    }

    void hookMenuLifecycle_(SwMenu* menu) {
        if (!menu) {
            return;
        }

        // If a menu closes itself (outside click, action triggered, Escape), ensure we exit
        // the active-menu state; otherwise subsequent hover events can reopen menus unexpectedly.
        SwObject::connect(menu, &SwMenu::aboutToHide, this, [this, menu]() {
            if (m_activeIndex < 0 || m_activeIndex >= menuCount()) {
                return;
            }
            if (menuAt(m_activeIndex) != menu) {
                return;
            }
            m_activeIndex = -1;
            update();
        });
    }

    SwVector<MenuEntry> m_leftMenus;
    SwVector<MenuEntry> m_rightMenus;
    SwVector<SwRect> m_menuRects;
    int m_margin{8};
    int m_spacing{6};
    int m_activeIndex{-1};
};
