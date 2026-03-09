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

/**
 * @file src/core/gui/SwMenuBar.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwMenuBar in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the menu bar interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwMenuBar.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwMenuBar - menu bar widget.
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
    /**
     * @brief Constructs a `SwMenuBar` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwMenuBar(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
    }

    /**
     * @brief Adds the specified menu.
     * @param title Title text applied by the operation.
     * @return The requested menu.
     */
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

    /**
     * @brief Adds the specified menu Right.
     * @param title Title text applied by the operation.
     * @return The requested menu Right.
     */
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

    /**
     * @brief Performs the `menuCount` operation.
     * @return The requested menu Count.
     */
    int menuCount() const { return m_leftMenus.size() + m_rightMenus.size(); }

    /**
     * @brief Performs the `menuAt` operation.
     * @param index Value passed to the method.
     * @return The requested menu At.
     */
    SwMenu* menuAt(int index) const {
        const MenuEntry* entry = entryAt_(index);
        if (!entry) {
            return nullptr;
        }
        return entry->menu;
    }

    /**
     * @brief Performs the `menuRect` operation.
     * @param index Value passed to the method.
     * @return The requested menu Rect.
     */
    SwRect menuRect(int index) const {
        if (index < 0 || index >= m_menuRects.size()) {
            return {};
        }
        return m_menuRects[index];
    }

    /**
     * @brief Closes the active Menu handled by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
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

    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateGeometryCache();
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
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

        const SwColor textColor{30, 30, 30};
        const SwColor textActive{0, 84, 166};
        const SwColor hoverFill{224, 238, 255};
        const SwColor activeFill{198, 222, 252};

        const int total = std::min(menuCount(), m_menuRects.size());
        for (int i = 0; i < total; ++i) {
            const SwRect r = m_menuRects[i];
            const bool hovered = (i == getHoverIndex());
            const bool active = (i == m_activeIndex);

            if (active) {
                SwRect hi{r.x + 2, r.y + 2, r.width - 4, std::max(0, r.height - 4)};
                painter->fillRoundedRect(hi, 4, activeFill, activeFill, 0);
            } else if (hovered) {
                SwRect hi{r.x + 2, r.y + 2, r.width - 4, std::max(0, r.height - 4)};
                painter->fillRoundedRect(hi, 4, hoverFill, hoverFill, 0);
            }

            painter->drawText(r,
                              entryAt_(i) ? entryAt_(i)->title : SwString(),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              active ? textActive : textColor,
                              getFont());
        }

        painter->finalize();
    }

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    void initDefaults() {
        resize(560, 28);
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setFrameShape(Shape::NoFrame);
        setFont(SwFont(L"Segoe UI", 9, Normal));
        setStyleSheet(R"(
            SwMenuBar {
                background-color: rgb(255, 255, 255);
                border-color: rgb(218, 218, 218);
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

        const SwRect bounds = rect();
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
        menu->popup(r.x, r.y + r.height, r);
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

