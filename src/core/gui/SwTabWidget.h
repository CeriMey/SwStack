#pragma once

/**
 * @file src/core/gui/SwTabWidget.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwTabWidget in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the tab widget interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwTabWidget.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
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

#include "SwWidget.h"
#include "SwVector.h"

class SwTabWidget : public SwWidget {
    SW_OBJECT(SwTabWidget, SwWidget)

public:
    DECLARE_SIGNAL(currentChanged, int);
    DECLARE_SIGNAL(tabCloseRequested, int);
    DECLARE_SIGNAL(tabMoved, int, int);

    enum class TabPosition {
        Top,
        Bottom,
        Left,
        Right
    };

    enum class TabStyle {
        Segmented,
        Underline,
        Pills
    };

    /**
     * @brief Constructs a `SwTabWidget` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwTabWidget(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        resize(560, 360);
        setStyleSheet("SwTabWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFont(SwFont(L"Segoe UI", 10, Medium));
    }

    CUSTOM_PROPERTY(bool, TabsClosable, false) {
        SW_UNUSED(value);
        update();
    }

public:

    /**
     * @brief Performs the `tabsClosable` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool tabsClosable() const { return getTabsClosable(); }

    CUSTOM_PROPERTY(bool, Movable, false) {
        SW_UNUSED(value);
        update();
    }

public:

    /**
     * @brief Performs the `movable` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool movable() const { return getMovable(); }
    /**
     * @brief Returns whether the object reports movable.
     * @return `true` when the object reports movable; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isMovable() const { return getMovable(); }

    /**
     * @brief Adds the specified tab.
     * @param page Value passed to the method.
     * @param label Value passed to the method.
     * @return The requested tab.
     */
    int addTab(SwWidget* page, const SwString& label) {
        if (!page) {
            return -1;
        }

        if (page->parent() != this) {
            page->setParent(this);
        }

        Tab tab;
        tab.text = label;
        tab.page = page;
        tab.rect = SwRect{0, 0, 0, 0};
        m_tabs.push_back(tab);

        if (m_currentIndex < 0) {
            m_currentIndex = 0;
            page->setVisible(true);
        } else {
            page->setVisible(false);
        }

        updateLayout();
        update();
        return m_tabs.size() - 1;
    }

    /**
     * @brief Performs the `widget` operation.
     * @param index Value passed to the method.
     * @return The requested widget.
     */
    SwWidget* widget(int index) const {
        if (index < 0 || index >= m_tabs.size()) {
            return nullptr;
        }
        return m_tabs[index].page;
    }

    /**
     * @brief Performs the `tabText` operation.
     * @param index Value passed to the method.
     * @return The requested tab Text.
     */
    SwString tabText(int index) const {
        if (index < 0 || index >= m_tabs.size()) {
            return {};
        }
        return m_tabs[index].text;
    }

    /**
     * @brief Sets the tab Text.
     * @param index Value passed to the method.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTabText(int index, const SwString& text) {
        if (index < 0 || index >= m_tabs.size()) {
            return;
        }
        m_tabs[index].text = text;
        updateLayout();
        update();
    }

    /**
     * @brief Removes the specified tab.
     * @param index Value passed to the method.
     */
    void removeTab(int index) {
        if (index < 0 || index >= m_tabs.size()) {
            return;
        }

        if (m_tabs[index].page) {
            delete m_tabs[index].page;
            m_tabs[index].page = nullptr;
        }

        m_tabs.erase(m_tabs.begin() + index);

        if (m_currentIndex == index) {
            const int newSize = m_tabs.size();
            m_currentIndex = (newSize > 0) ? ((index < newSize) ? index : (newSize - 1)) : -1;
            for (int i = 0; i < m_tabs.size(); ++i) {
                if (m_tabs[i].page) {
                    m_tabs[i].page->setVisible(i == m_currentIndex);
                }
            }
            currentChanged(m_currentIndex);
        } else if (m_currentIndex > index) {
            --m_currentIndex;
        }

        if (m_hoverIndex == index) m_hoverIndex = -1;
        if (m_pressedIndex == index) m_pressedIndex = -1;
        if (m_closeHoverIndex == index) m_closeHoverIndex = -1;
        if (m_closePressedIndex == index) m_closePressedIndex = -1;

        updateLayout();
        update();
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        // The tab widget owns its pages (parented to this), so clearing deletes them.
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].page) {
                delete m_tabs[i].page;
                m_tabs[i].page = nullptr;
            }
        }
        m_tabs.clear();
        m_currentIndex = -1;
        m_hoverIndex = -1;
        m_pressedIndex = -1;
        m_closeHoverIndex = -1;
        m_closePressedIndex = -1;
        m_dragIndex = -1;
        m_dragging = false;
        m_tabScrollOffset = 0;
        m_tabScrollMax = 0;
        m_scrollButtonsVisible = false;
        m_tabViewportRect = SwRect{0, 0, 0, 0};
        m_scrollPrevRect = SwRect{0, 0, 0, 0};
        m_scrollNextRect = SwRect{0, 0, 0, 0};
        m_scrollPrevHovered = false;
        m_scrollNextHovered = false;
        m_scrollPrevPressed = false;
        m_scrollNextPressed = false;
        update();
    }

    /**
     * @brief Sets the tab Position.
     * @param position Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTabPosition(TabPosition position) {
        if (m_tabPosition == position) {
            return;
        }
        m_tabPosition = position;
        updateLayout();
        update();
    }

    /**
     * @brief Returns the current tab Position.
     * @return The current tab Position.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    TabPosition tabPosition() const {
        return m_tabPosition;
    }

    /**
     * @brief Sets the tab Style.
     * @param style Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTabStyle(TabStyle style) {
        if (m_tabStyle == style) {
            return;
        }
        m_tabStyle = style;
        if (m_tabStyle == TabStyle::Pills) {
            m_tabsFillSpace = false;
        }
        // Default layout: underline is full-bleed; other styles keep an inset bar unless overridden via stylesheet.
        if (m_tabStyle == TabStyle::Underline) {
            m_tabBarFullBleed = true;
        } else {
            m_tabBarFullBleed = false;
        }
        updateLayout();
        update();
    }

    /**
     * @brief Returns the current tab Style.
     * @return The current tab Style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    TabStyle tabStyle() const {
        return m_tabStyle;
    }

    /**
     * @brief Sets the accent Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAccentColor(const SwColor& color) {
        m_accentColor = color;
        update();
    }

    /**
     * @brief Returns the current accent Color.
     * @return The current accent Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor accentColor() const {
        return m_accentColor;
    }

    /**
     * @brief Sets the tabs Fill Space.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTabsFillSpace(bool on) {
        if (m_tabsFillSpace == on) {
            return;
        }
        m_tabsFillSpace = on;
        updateLayout();
        update();
    }

    /**
     * @brief Returns the current tabs Fill Space.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool tabsFillSpace() const {
        return m_tabsFillSpace;
    }

    /**
     * @brief Sets the uses Scroll Buttons.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUsesScrollButtons(bool on) {
        if (m_usesScrollButtons == on) {
            return;
        }
        m_usesScrollButtons = on;
        if (!m_usesScrollButtons) {
            m_tabScrollOffset = 0;
        }
        updateLayout();
        update();
    }

    /**
     * @brief Returns the current uses Scroll Buttons.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool usesScrollButtons() const {
        return m_usesScrollButtons;
    }

    /**
     * @brief Returns the current count.
     * @return The current count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int count() const {
        return m_tabs.size();
    }

    /**
     * @brief Returns the current current Index.
     * @return The current current Index.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int currentIndex() const {
        return m_currentIndex;
    }

    /**
     * @brief Returns the current current Widget.
     * @return The current current Widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* currentWidget() const {
        if (m_currentIndex < 0 || m_currentIndex >= m_tabs.size()) {
            return nullptr;
        }
        return m_tabs[m_currentIndex].page;
    }

    /**
     * @brief Sets the current Index.
     * @param index Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCurrentIndex(int index) {
        if (index < 0 || index >= m_tabs.size()) {
            return;
        }
        if (index == m_currentIndex) {
            return;
        }

        if (m_currentIndex >= 0 && m_currentIndex < m_tabs.size()) {
            if (m_tabs[m_currentIndex].page) {
                m_tabs[m_currentIndex].page->setVisible(false);
            }
        }
        m_currentIndex = index;
        if (m_tabs[m_currentIndex].page) {
            m_tabs[m_currentIndex].page->setVisible(true);
        }

        updateLayout();
        ensureTabVisible_(m_currentIndex);
        currentChanged(m_currentIndex);
        update();
    }

protected:
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

        const bool layoutDirty = applyStyleSheetOverrides_();
        if (layoutDirty) {
            updateLayout();
        }

        const SwRect localRect = this->rect();
        recalcTabRects(localRect);

        paintContainer(painter, localRect);

        // Paint current page (and its children) first; tabs render on top.
        if (SwWidget* page = currentWidget()) {
            const SwRect clip = contentClipRect(localRect);
            painter->pushClipRect(clip);
            paintChild_(event, page);
            painter->popClipRect();
        }

        paintTabs(painter, localRect);
    }

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }

        SwRect rect = this->rect();
        recalcTabRects(rect);
        SwRect bar = tabBarRect(rect);
        if (containsPoint(bar, event->x(), event->y()) && m_scrollButtonsVisible && m_tabScrollMax > 0) {
            const int delta = event->delta();
            const int pixels = clampInt(delta / 3, -240, 240);
            scrollTabsBy_(-pixels);
            event->accept();
            return;
        }

        SwWidget::wheelEvent(event);
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

        SwRect rect = this->rect();
        recalcTabRects(rect);
        SwRect bar = tabBarRect(rect);
        if (containsPoint(bar, event->x(), event->y())) {
            if (m_scrollButtonsVisible) {
                if (containsPoint(m_scrollPrevRect, event->x(), event->y())) {
                    m_scrollPrevPressed = true;
                    scrollTabsBy_(-scrollStepPixels_());
                    event->accept();
                    update();
                    return;
                }
                if (containsPoint(m_scrollNextRect, event->x(), event->y())) {
                    m_scrollNextPressed = true;
                    scrollTabsBy_(scrollStepPixels_());
                    event->accept();
                    update();
                    return;
                }
            }

            int idx = tabIndexAt(event->x(), event->y());
            if (idx >= 0) {
                if (tabsClosable() && containsPoint(tabCloseRect_(m_tabs[idx].rect), event->x(), event->y())) {
                    m_closePressedIndex = idx;
                    event->accept();
                    update();
                    return;
                }

                m_pressedIndex = idx;
                m_pressX = event->x();
                m_pressY = event->y();
                m_dragIndex = idx;
                m_dragging = false;
                setCurrentIndex(idx);
                event->accept();
                update();
                return;
            }
        }

        SwWidget::mousePressEvent(event);
    }

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        bool changed = false;
        if (m_pressedIndex != -1) {
            m_pressedIndex = -1;
            changed = true;
        }
        if (m_closePressedIndex != -1) {
            const int idx = m_closePressedIndex;
            m_closePressedIndex = -1;
            changed = true;

            SwRect rect = this->rect();
            recalcTabRects(rect);
            if (idx >= 0 && idx < m_tabs.size() && tabsClosable() &&
                containsPoint(tabCloseRect_(m_tabs[idx].rect), event->x(), event->y())) {
                tabCloseRequested(idx);
            }
        }
        if (m_scrollPrevPressed) {
            m_scrollPrevPressed = false;
            changed = true;
        }
        if (m_scrollNextPressed) {
            m_scrollNextPressed = false;
            changed = true;
        }
        if (m_dragIndex != -1 || m_dragging) {
            m_dragIndex = -1;
            m_dragging = false;
            changed = true;
        }
        if (changed) {
            update();
            event->accept();
            return;
        }

        SwWidget::mouseReleaseEvent(event);
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

        SwRect rect = this->rect();
        recalcTabRects(rect);
        SwRect bar = tabBarRect(rect);

        if (containsPoint(bar, event->x(), event->y())) {
            bool hoverChanged = false;

            if (m_scrollButtonsVisible) {
                const bool overPrev = containsPoint(m_scrollPrevRect, event->x(), event->y());
                const bool overNext = containsPoint(m_scrollNextRect, event->x(), event->y());
                if (overPrev != m_scrollPrevHovered) {
                    m_scrollPrevHovered = overPrev;
                    hoverChanged = true;
                }
                if (overNext != m_scrollNextHovered) {
                    m_scrollNextHovered = overNext;
                    hoverChanged = true;
                }
                if (overPrev || overNext) {
                    if (m_hoverIndex != -1) {
                        m_hoverIndex = -1;
                        hoverChanged = true;
                    }
                    if (m_closeHoverIndex != -1) {
                        m_closeHoverIndex = -1;
                        hoverChanged = true;
                    }
                    SwWidgetPlatformAdapter::setCursor(CursorType::Hand);
                    if (hoverChanged) {
                        update();
                    }
                    event->accept();
                    return;
                }
            }

            if (m_scrollPrevHovered || m_scrollNextHovered) {
                m_scrollPrevHovered = false;
                m_scrollNextHovered = false;
                hoverChanged = true;
            }

            int idx = tabIndexAt(event->x(), event->y());
            if (idx != m_hoverIndex) {
                m_hoverIndex = idx;
                hoverChanged = true;
            }

            if (tabsClosable()) {
                const bool overClose = (idx >= 0 && idx < m_tabs.size() &&
                                       containsPoint(tabCloseRect_(m_tabs[idx].rect), event->x(), event->y()));
                const int newCloseHover = overClose ? idx : -1;
                if (newCloseHover != m_closeHoverIndex) {
                    m_closeHoverIndex = newCloseHover;
                    hoverChanged = true;
                }
            } else {
                if (m_closeHoverIndex != -1) {
                    m_closeHoverIndex = -1;
                    hoverChanged = true;
                }
            }

            if (getMovable() && m_dragIndex != -1 && m_pressedIndex == m_dragIndex) {
                const int dx = event->x() - m_pressX;
                const int dy = event->y() - m_pressY;
                const int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (!m_dragging && dist >= 6) {
                    m_dragging = true;
                    hoverChanged = true;
                }

                if (m_dragging && idx >= 0 && idx < m_tabs.size() && idx != m_dragIndex) {
                    moveTab_(m_dragIndex, idx);
                    m_dragIndex = idx;
                    m_pressedIndex = idx;
                    m_hoverIndex = idx;
                    ensureTabVisible_(idx);
                    hoverChanged = true;
                }
            }

            SwWidgetPlatformAdapter::setCursor(idx >= 0 ? CursorType::Hand : CursorType::Arrow);
            if (hoverChanged) {
                update();
            }
            event->accept();
            return;
        }

        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        if (m_closeHoverIndex != -1) {
            m_closeHoverIndex = -1;
            update();
        }
        if (m_scrollPrevHovered || m_scrollNextHovered) {
            m_scrollPrevHovered = false;
            m_scrollNextHovered = false;
            update();
        }
        SwWidget::mouseMoveEvent(event);
    }

    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateLayout();
    }

private:
    struct Tab {
        SwString text;
        SwWidget* page{nullptr};
        SwRect rect;
    };

    static SwColor mix(const SwColor& a, const SwColor& b, int t0_100) {
        int t = clampInt(t0_100, 0, 100);
        int inv = 100 - t;
        return clampColor(SwColor{
            (a.r * inv + b.r * t) / 100,
            (a.g * inv + b.g * t) / 100,
            (a.b * inv + b.b * t) / 100});
    }

    static SwRect insetRect(const SwRect& r, int dx, int dy) {
        return SwRect{r.x + dx, r.y + dy, r.width - dx * 2, r.height - dy * 2};
    }

    static SwRect insetRect(const SwRect& r, int left, int top, int right, int bottom) {
        return SwRect{r.x + left, r.y + top, r.width - left - right, r.height - top - bottom};
    }

    static bool parseBool_(const SwString& value, bool fallback) {
        SwString v = value.toLower().trimmed();
        if (v.isEmpty()) {
            return fallback;
        }
        if (v == "1" || v == "true" || v == "yes" || v == "on") {
            return true;
        }
        if (v == "0" || v == "false" || v == "no" || v == "off") {
            return false;
        }
        return fallback;
    }

    static int parsePixelValue_(const SwString& value, int defaultValue) {
        if (value.isEmpty()) {
            return defaultValue;
        }
        std::string s = value.toStdString();
        size_t pos = s.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        try {
            return std::stoi(s);
        } catch (...) {
            return defaultValue;
        }
    }

    SwString styleProp_(const char* propName) const {
        if (!propName) {
            return {};
        }
        StyleSheet* sheet = const_cast<SwTabWidget*>(this)->getToolSheet();
        if (!sheet) {
            return {};
        }

        const auto selectors = classHierarchy(); // most-derived first
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            SwString v = sheet->getStyleProperty(selector, propName);
            if (!v.isEmpty()) {
                return v;
            }
        }
        return {};
    }

    bool applyStyleSheetOverrides_() {
        bool changed = false;

        SwString v = styleProp_("tab-bar-full-bleed");
        if (!v.isEmpty()) {
            const bool on = parseBool_(v, m_tabBarFullBleed);
            if (on != m_tabBarFullBleed) {
                m_tabBarFullBleed = on;
                changed = true;
            }
        }

        v = styleProp_("indicator-padding");
        if (!v.isEmpty()) {
            int px = parsePixelValue_(v, m_indicatorPadding);
            if (px < 0) {
                px = 0;
            }
            if (px != m_indicatorPadding) {
                m_indicatorPadding = px;
                changed = true;
            }
        }

        v = styleProp_("tabs-fill-space");
        if (!v.isEmpty()) {
            const bool on = parseBool_(v, m_tabsFillSpace);
            if (on != m_tabsFillSpace) {
                m_tabsFillSpace = on;
                changed = true;
            }
        }

        return changed;
    }

    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px < (r.x + r.width) && py >= r.y && py < (r.y + r.height);
    }

    static int roundRadiusForRect(const SwRect& r) {
        int m = r.width < r.height ? r.width : r.height;
        return m > 0 ? (m / 2) : 0;
    }

    SwRect outerRect(const SwRect& widgetRect) const {
        return widgetRect;
    }

    SwRect innerRect(const SwRect& widgetRect) const {
        SwRect inner = insetRect(widgetRect, m_outerPadding, m_outerPadding);
        if (inner.width < 0) inner.width = 0;
        if (inner.height < 0) inner.height = 0;
        return inner;
    }

    SwRect layoutRect(const SwRect& widgetRect) const {
        if (!m_tabBarFullBleed) {
            return innerRect(widgetRect);
        }
        SwRect inner = insetRect(widgetRect, 1, 1);
        if (inner.width < 0) inner.width = 0;
        if (inner.height < 0) inner.height = 0;
        return inner;
    }

    bool isHorizontalTabs() const {
        return m_tabPosition == TabPosition::Top || m_tabPosition == TabPosition::Bottom;
    }

    int tabBarThickness() const {
        return isHorizontalTabs() ? m_tabBarHeight : m_tabBarWidth;
    }

    SwRect tabBarRect(const SwRect& widgetRect) const {
        SwRect inner = layoutRect(widgetRect);
        const int thickness = tabBarThickness();
        const int gap = m_tabToContentGap;

        if (isHorizontalTabs()) {
            SwRect bar = inner;
            bar.height = thickness;
            if (m_tabPosition == TabPosition::Bottom) {
                bar.y = inner.y + inner.height - thickness;
            }
            // Keep a small separation from content with internal padding, not by moving the bar.
            SW_UNUSED(gap)
            return bar;
        }

        SwRect bar = inner;
        bar.width = thickness;
        if (m_tabPosition == TabPosition::Right) {
            bar.x = inner.x + inner.width - thickness;
        }
        SW_UNUSED(gap)
        return bar;
    }

    SwRect contentRect(const SwRect& widgetRect) const {
        SwRect inner = layoutRect(widgetRect);
        const int thickness = tabBarThickness();
        const int gap = m_tabToContentGap;

        if (isHorizontalTabs()) {
            SwRect content = inner;
            content.height = inner.height - thickness - gap;
            if (content.height < 0) content.height = 0;
            if (m_tabPosition == TabPosition::Top) {
                content.y = inner.y + thickness + gap;
            }
            return content;
        }

        SwRect content = inner;
        content.width = inner.width - thickness - gap;
        if (content.width < 0) content.width = 0;
        if (m_tabPosition == TabPosition::Left) {
            content.x = inner.x + thickness + gap;
        }
        return content;
    }

    SwRect contentClipRect(const SwRect& widgetRect) const {
        // Clip the page to the inner area excluding the tab bar only.
        // The tab-to-content gap stays part of the clip so widget frames (borders)
        // can extend into it without being cut off.
        SwRect inner = layoutRect(widgetRect);
        const int thickness = tabBarThickness();

        if (isHorizontalTabs()) {
            SwRect clip = inner;
            clip.height = inner.height - thickness;
            if (clip.height < 0) clip.height = 0;
            if (m_tabPosition == TabPosition::Top) {
                clip.y = inner.y + thickness;
            }
            return clip;
        }

        SwRect clip = inner;
        clip.width = inner.width - thickness;
        if (clip.width < 0) clip.width = 0;
        if (m_tabPosition == TabPosition::Left) {
            clip.x = inner.x + thickness;
        }
        return clip;
    }

    SwRect pageRectForLayout(const SwRect& widgetRect) const {
        SwRect content = contentRect(widgetRect);
        content = insetRect(content, m_pagePadding, m_pagePadding);
        if (content.width < 0) content.width = 0;
        if (content.height < 0) content.height = 0;
        return content;
    }

    SwRect mapLocalRectToAbsolute_(const SwRect& localRect) const {
        return localRect;
    }

    void updateLayout() {
        SwRect rect = this->rect();

        SwRect pageRect = pageRectForLayout(rect);
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (!m_tabs[i].page) {
                continue;
            }
            m_tabs[i].page->move(pageRect.x, pageRect.y);
            m_tabs[i].page->resize(pageRect.width, pageRect.height);
        }

        recalcTabRects(rect);
    }

    void recalcTabRects(const SwRect& widgetRect) {
        const int tabCount = m_tabs.size();
        m_scrollButtonsVisible = false;
        m_tabScrollMax = 0;
        m_scrollPrevRect = SwRect{0, 0, 0, 0};
        m_scrollNextRect = SwRect{0, 0, 0, 0};
        m_tabViewportRect = SwRect{0, 0, 0, 0};

        if (tabCount <= 0) {
            m_tabScrollOffset = 0;
            return;
        }

        SwRect bar = tabBarRect(widgetRect);
        SwRect area = insetRect(bar, m_barPadding, m_barPadding);
        if (area.width < 0) area.width = 0;
        if (area.height < 0) area.height = 0;

        const SwFont font = getFont();
        const int defaultCharWidth = 8;

        auto layoutTabs_ = [&](const SwRect& viewport) -> int {
            if (isHorizontalTabs()) {
                if (m_tabsFillSpace) {
                    int baseW = (tabCount > 0) ? (viewport.width / tabCount) : 0;
                    int rem = (tabCount > 0) ? (viewport.width - baseW * tabCount) : 0;
                    int x = viewport.x;
                    for (int i = 0; i < tabCount; ++i) {
                        int w = baseW + (i < rem ? 1 : 0);
                        if (w < m_minTabWidth) {
                            w = m_minTabWidth;
                        }
                        m_tabs[i].rect = SwRect{x, viewport.y, w, viewport.height};
                        x += w;
                    }
                    return x - viewport.x;
                }

                int x = viewport.x;
                for (int i = 0; i < tabCount; ++i) {
                    const SwString& text = m_tabs[i].text;
                    int textWidth = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                            text,
                                                                            font,
                                                                            static_cast<size_t>(text.size()),
                                                                            defaultCharWidth);
                    if (textWidth < 0) {
                        textWidth = 0;
                    }
                    int w = textWidth + (m_tabPaddingX * 2);
                    if (w < m_minTabWidth) {
                        w = m_minTabWidth;
                    }
                    m_tabs[i].rect = SwRect{x, viewport.y, w, viewport.height};
                    x += w + m_tabSpacing;
                }
                const SwRect& last = m_tabs[tabCount - 1].rect;
                return (last.x + last.width) - viewport.x;
            }

            if (m_tabsFillSpace) {
                int baseH = (tabCount > 0) ? (viewport.height / tabCount) : 0;
                int rem = (tabCount > 0) ? (viewport.height - baseH * tabCount) : 0;
                int y = viewport.y;
                for (int i = 0; i < tabCount; ++i) {
                    int h = baseH + (i < rem ? 1 : 0);
                    if (h < m_minTabHeight) {
                        h = m_minTabHeight;
                    }
                    m_tabs[i].rect = SwRect{viewport.x, y, viewport.width, h};
                    y += h;
                }
                return y - viewport.y;
            }

            int y = viewport.y;
            for (int i = 0; i < tabCount; ++i) {
                int h = m_tabItemHeight;
                if (h < m_minTabHeight) {
                    h = m_minTabHeight;
                }
                m_tabs[i].rect = SwRect{viewport.x, y, viewport.width, h};
                y += h + m_tabSpacing;
            }
            const SwRect& last = m_tabs[tabCount - 1].rect;
            return (last.y + last.height) - viewport.y;
        };

        SwRect viewport = area;
        int totalLen = layoutTabs_(viewport);
        const int viewportLen0 = isHorizontalTabs() ? viewport.width : viewport.height;
        const bool overflow = (totalLen > viewportLen0);

        if (m_usesScrollButtons && overflow) {
            m_scrollButtonsVisible = true;
            int extent = isHorizontalTabs() ? viewport.height : m_tabItemHeight;
            extent = clampInt(extent, 20, 56);

            if (isHorizontalTabs()) {
                m_scrollPrevRect = SwRect{area.x, area.y, extent, area.height};
                m_scrollNextRect = SwRect{area.x + area.width - extent, area.y, extent, area.height};
                viewport = SwRect{area.x + extent, area.y, area.width - extent * 2, area.height};
            } else {
                m_scrollPrevRect = SwRect{area.x, area.y, area.width, extent};
                m_scrollNextRect = SwRect{area.x, area.y + area.height - extent, area.width, extent};
                viewport = SwRect{area.x, area.y + extent, area.width, area.height - extent * 2};
            }

            if (viewport.width < 0) viewport.width = 0;
            if (viewport.height < 0) viewport.height = 0;

            totalLen = layoutTabs_(viewport);
            const int viewportLen = isHorizontalTabs() ? viewport.width : viewport.height;
            m_tabScrollMax = (totalLen > viewportLen) ? (totalLen - viewportLen) : 0;
            m_tabScrollOffset = clampInt(m_tabScrollOffset, 0, m_tabScrollMax);

            for (int i = 0; i < tabCount; ++i) {
                if (isHorizontalTabs()) {
                    m_tabs[i].rect.x -= m_tabScrollOffset;
                } else {
                    m_tabs[i].rect.y -= m_tabScrollOffset;
                }
            }
        } else {
            m_tabScrollOffset = 0;
        }

        m_tabViewportRect = viewport;
    }

    int tabIndexAt(int px, int py) const {
        for (int i = 0; i < m_tabs.size(); ++i) {
            const SwRect& r = m_tabs[i].rect;
            if (px >= r.x && px < (r.x + r.width) && py >= r.y && py < (r.y + r.height)) {
                return i;
            }
        }
        return -1;
    }

    SwFont fontForTab(bool selected) const {
        SwFont f = getFont();
        const SwString family = SwString::fromWString(f.getFamily());
        if (family.contains("emoji", Sw::CaseInsensitive)) {
            return f;
        }
        f.setWeight(selected ? SemiBold : Medium);
        return f;
    }

    DrawTextFormats tabTextFormats() const {
        if (isHorizontalTabs()) {
            return DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine);
        }
        return DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine);
    }

    SwRect tabTextRect(const SwRect& tabRect) const {
        SwRect r = tabRect;
        if (!isHorizontalTabs()) {
            r = insetRect(tabRect, m_tabTextIndent, 0, m_tabPaddingX, 0);
        }

        if (tabsClosable()) {
            const SwRect close = tabCloseRect_(tabRect);
            const int gap = 8;
            const int rightCut = close.width + gap;
            if (rightCut > 0) {
                r.width -= rightCut;
                if (r.width < 0) r.width = 0;
            }
        }
        return r;
    }

    void paintContainer(SwPainter* painter, const SwRect& widgetRect) {
        if (!painter) {
            return;
        }

        const SwRect outer = outerRect(widgetRect);
        const int r = clampInt(m_cornerRadius, 0, roundRadiusForRect(outer));

        SwRect shadowBase = insetRect(outer, 2, 2);
        if (shadowBase.width < 0) shadowBase.width = 0;
        if (shadowBase.height < 0) shadowBase.height = 0;

        SwRect shadow1 = shadowBase;
        shadow1.y += 10;
        shadow1.height -= 10;

        SwRect shadow2 = shadowBase;
        shadow2.y += 4;
        shadow2.height -= 4;

        if (shadow1.height < 0) shadow1.height = 0;
        if (shadow2.height < 0) shadow2.height = 0;
        const SwColor shadowColor1 = SwColor{226, 230, 238};
        const SwColor shadowColor2 = SwColor{214, 219, 230};
        painter->fillRoundedRect(shadow1, r, shadowColor1, shadowColor1, 0);
        painter->fillRoundedRect(shadow2, r, shadowColor2, shadowColor2, 0);

        painter->fillRoundedRect(outer, r, m_surfaceColor, m_borderColor, 1);
    }

    void paintTabs(SwPainter* painter, const SwRect& widgetRect) {
        if (!painter) {
            return;
        }

        const int tabCount = m_tabs.size();
        if (tabCount <= 0) {
            return;
        }

        const SwRect barLocal = tabBarRect(widgetRect);
        SwRect areaLocal = insetRect(barLocal, m_barPadding, m_barPadding);
        if (areaLocal.width < 0) areaLocal.width = 0;
        if (areaLocal.height < 0) areaLocal.height = 0;

        const SwRect bar = mapLocalRectToAbsolute_(barLocal);
        const SwRect area = mapLocalRectToAbsolute_(areaLocal);

        const SwColor onSurface = SwColor{17, 24, 39};
        const SwColor muted = SwColor{107, 114, 128};
        const SwColor hoverBg = mix(m_tabBarColor, m_surfaceColor, 65);
        const SwColor accent = m_accentColor;

        const int barRadius = clampInt(m_tabBarRadius, 0, roundRadiusForRect(bar));
        const int itemRadius = clampInt(m_tabItemRadius, 0, roundRadiusForRect(area));

        const SwRect viewport = mapLocalRectToAbsolute_(m_tabViewportRect);
        const bool doClip = m_scrollButtonsVisible;

        if (m_tabStyle == TabStyle::Segmented) {
            painter->fillRoundedRect(bar, barRadius, m_tabBarColor, m_borderColor, 1);

            if (m_currentIndex >= 0 && m_currentIndex < tabCount) {
                if (doClip) {
                    painter->pushClipRect(viewport);
                }
                SwRect active = mapLocalRectToAbsolute_(m_tabs[m_currentIndex].rect);
                SwRect selector = insetRect(active, 2, 2);
                if (selector.width < 0) selector.width = 0;
                if (selector.height < 0) selector.height = 0;

                const int selRadius = clampInt(itemRadius - 2, 0, roundRadiusForRect(selector));
                SwRect selShadow = selector;
                if (isHorizontalTabs()) {
                    selShadow.y += 2;
                } else {
                    selShadow.x += (m_tabPosition == TabPosition::Left) ? 2 : -2;
                }
                painter->fillRoundedRect(selShadow, selRadius, SwColor{218, 223, 232}, SwColor{218, 223, 232}, 0);
                painter->fillRoundedRect(selector, selRadius, m_surfaceColor, m_borderColor, 1);

                const int indicator = clampInt(m_indicatorThickness + 1, 3, 10);
                SwRect indicatorRect = selector;
                if (isHorizontalTabs()) {
                    indicatorRect.x += m_indicatorPadding;
                    indicatorRect.width -= m_indicatorPadding * 2;
                    indicatorRect.height = indicator;
                    indicatorRect.y = (m_tabPosition == TabPosition::Top)
                                          ? (selector.y + selector.height - indicator)
                                          : selector.y;
                } else {
                    indicatorRect.y += m_indicatorPadding;
                    indicatorRect.height -= m_indicatorPadding * 2;
                    indicatorRect.width = indicator;
                    indicatorRect.x = (m_tabPosition == TabPosition::Left)
                                          ? (selector.x + selector.width - indicator)
                                          : selector.x;
                }
                if (indicatorRect.width < 0) indicatorRect.width = 0;
                if (indicatorRect.height < 0) indicatorRect.height = 0;
                const int indRadius = clampInt(roundRadiusForRect(indicatorRect), 0, 999);
                painter->fillRoundedRect(indicatorRect, indRadius, accent, accent, 0);
                if (doClip) {
                    painter->popClipRect();
                }
            }

            if (doClip) {
                painter->pushClipRect(viewport);
            }
            for (int i = 0; i < tabCount; ++i) {
                const bool selected = (i == m_currentIndex);
                const bool hovered = (i == m_hoverIndex);
                SwRect tabRect = mapLocalRectToAbsolute_(m_tabs[i].rect);

                if (!selected && hovered) {
                    SwRect hoverRect = insetRect(tabRect, 2, 2);
                    painter->fillRoundedRect(hoverRect, itemRadius, hoverBg, hoverBg, 0);
                }

                const SwRect textRect = tabTextRect(tabRect);
                painter->drawText(textRect,
                                  m_tabs[i].text,
                                  tabTextFormats(),
                                  selected ? onSurface : (hovered ? onSurface : muted),
                                  fontForTab(selected));
                if (tabsClosable()) {
                    paintTabCloseButton_(painter, i, onSurface, muted, hoverBg);
                }
            }
            if (doClip) {
                painter->popClipRect();
            }
            paintScrollButtons_(painter, onSurface, muted, hoverBg);
            return;
        }

        if (m_tabStyle == TabStyle::Underline) {
            // Tab row background.
            painter->fillRect(bar, m_surfaceColor, m_surfaceColor, 0);

            // Divider between tabs and content.
            if (isHorizontalTabs()) {
                SwRect divider = bar;
                divider.height = 1;
                divider.y = (m_tabPosition == TabPosition::Top) ? (bar.y + bar.height - 1) : bar.y;
                painter->fillRect(divider, m_borderColor, m_borderColor, 0);
            } else {
                SwRect divider = bar;
                divider.width = 1;
                divider.x = (m_tabPosition == TabPosition::Left) ? (bar.x + bar.width - 1) : bar.x;
                painter->fillRect(divider, m_borderColor, m_borderColor, 0);
            }

            const int indicatorThickness = clampInt(m_indicatorThickness, 2, 8);
            if (doClip) {
                painter->pushClipRect(viewport);
            }
            for (int i = 0; i < tabCount; ++i) {
                const bool selected = (i == m_currentIndex);
                const bool hovered = (i == m_hoverIndex);
                SwRect tabRect = mapLocalRectToAbsolute_(m_tabs[i].rect);

                if (hovered && !selected) {
                    painter->fillRoundedRect(insetRect(tabRect, 2, 2), itemRadius, hoverBg, hoverBg, 0);
                }

                if (selected) {
                    SwRect indicator = tabRect;
                    const int pad = clampInt(m_indicatorPadding, 0, 256);
                    if (isHorizontalTabs()) {
                        indicator.x += pad;
                        indicator.width -= pad * 2;
                        if (indicator.width < 0) indicator.width = 0;
                        indicator.height = indicatorThickness;
                        indicator.y = (m_tabPosition == TabPosition::Top) ? (bar.y + bar.height - indicatorThickness) : bar.y;
                    } else {
                        indicator.y += pad;
                        indicator.height -= pad * 2;
                        if (indicator.height < 0) indicator.height = 0;
                        indicator.width = indicatorThickness;
                        indicator.x = (m_tabPosition == TabPosition::Left) ? (bar.x + bar.width - indicatorThickness) : bar.x;
                    }
                    const int indRadius = clampInt(indicatorThickness, 0, roundRadiusForRect(indicator));
                    painter->fillRoundedRect(indicator, indRadius, accent, accent, 0);
                }

                const SwRect textRect = tabTextRect(tabRect);
                painter->drawText(textRect,
                                  m_tabs[i].text,
                                  tabTextFormats(),
                                  selected ? onSurface : (hovered ? onSurface : muted),
                                  fontForTab(selected));
                if (tabsClosable()) {
                    paintTabCloseButton_(painter, i, onSurface, muted, hoverBg);
                }
            }
            if (doClip) {
                painter->popClipRect();
            }
            paintScrollButtons_(painter, onSurface, muted, hoverBg);
            return;
        }

        // Pills
        painter->fillRect(bar, m_surfaceColor, m_surfaceColor, 0);
        if (doClip) {
            painter->pushClipRect(viewport);
        }
        for (int i = 0; i < tabCount; ++i) {
            const bool selected = (i == m_currentIndex);
            const bool hovered = (i == m_hoverIndex);
            SwRect tabRect = mapLocalRectToAbsolute_(m_tabs[i].rect);

            SwColor fill = selected ? accent : (hovered ? hoverBg : mix(m_tabBarColor, m_surfaceColor, 35));
            SwColor border = selected ? accent : m_borderColor;
            SwColor textColor = selected ? SwColor{255, 255, 255} : onSurface;

            SwRect pill = insetRect(tabRect, 2, 2);
            const int radius = clampInt(roundRadiusForRect(pill), 0, 999);
            painter->fillRoundedRect(pill, radius, fill, border, selected ? 0 : 1);
            const SwRect textRect = tabTextRect(tabRect);
            painter->drawText(textRect,
                              m_tabs[i].text,
                              tabTextFormats(),
                              textColor,
                              fontForTab(selected));
            if (tabsClosable()) {
                paintTabCloseButton_(painter, i, onSurface, muted, hoverBg);
            }
        }
        if (doClip) {
            painter->popClipRect();
        }
        paintScrollButtons_(painter, onSurface, muted, hoverBg);
    }

    SwRect tabCloseRect_(const SwRect& tabRect) const {
        const int extent = clampInt(tabRect.height - 24, 12, 18);
        const int pad = 10;
        const int x = tabRect.x + tabRect.width - pad - extent;
        const int y = tabRect.y + (tabRect.height - extent) / 2;
        return SwRect{x, y, extent, extent};
    }

    void paintTabCloseButton_(SwPainter* painter,
                              int tabIndex,
                              const SwColor& onSurface,
                              const SwColor& muted,
                              const SwColor& hoverBg) {
        if (!painter) {
            return;
        }
        if (tabIndex < 0 || tabIndex >= m_tabs.size()) {
            return;
        }

        const SwRect r = mapLocalRectToAbsolute_(tabCloseRect_(m_tabs[tabIndex].rect));
        const bool hovered = (m_closeHoverIndex == tabIndex);
        const bool pressed = (m_closePressedIndex == tabIndex);
        const bool enabled = tabsClosable();

        SwRect pad = insetRect(r, 1, 1);
        if (pad.width < 0) pad.width = 0;
        if (pad.height < 0) pad.height = 0;
        const int radius = clampInt(roundRadiusForRect(pad), 0, 999);
        SwColor fill = mix(m_tabBarColor, m_surfaceColor, 20);
        if (enabled) {
            if (pressed) {
                fill = mix(hoverBg, onSurface, 10);
            } else if (hovered) {
                fill = hoverBg;
            }
        }
        painter->fillRoundedRect(pad, radius, fill, fill, 0);

        const int x1 = r.x + 4;
        const int y1 = r.y + 4;
        const int x2 = r.x + r.width - 4;
        const int y2 = r.y + r.height - 4;
        const SwColor line = enabled ? onSurface : muted;
        const int w = 2;
        painter->drawLine(x1, y1, x2, y2, line, w);
        painter->drawLine(x1, y2, x2, y1, line, w);
    }

    void paintScrollButtons_(SwPainter* painter,
                             const SwColor& onSurface,
                             const SwColor& muted,
                             const SwColor& hoverBg) {
        if (!painter || !m_scrollButtonsVisible) {
            return;
        }

        const bool horizontal = isHorizontalTabs();

        const bool prevEnabled = (m_tabScrollOffset > 0);
        const bool nextEnabled = (m_tabScrollOffset < m_tabScrollMax);

        auto paintButton = [&](const SwRect& r, const SwString& glyph, bool enabled, bool hovered, bool pressed) {
            const SwRect absR = mapLocalRectToAbsolute_(r);
            SwRect pad = insetRect(absR, 2, 2);
            if (pad.width < 0) pad.width = 0;
            if (pad.height < 0) pad.height = 0;
            const int radius = clampInt(roundRadiusForRect(pad), 0, 999);

            SwColor fill = mix(m_tabBarColor, m_surfaceColor, 20);
            if (enabled) {
                if (pressed) {
                    fill = mix(hoverBg, onSurface, 10);
                } else if (hovered) {
                    fill = hoverBg;
                }
            } else {
                fill = mix(m_tabBarColor, m_surfaceColor, 10);
            }

            painter->fillRoundedRect(pad, radius, fill, fill, 0);

            SwFont f = getFont();
            f.setWeight(SemiBold);
            f.setPointSize(f.getPointSize() + 2);
            painter->drawText(absR,
                              glyph,
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              enabled ? onSurface : muted,
                              f);
        };

        const SwString prevGlyph = horizontal ? SwString::fromUtf8("\xE2\x80\xB9") : SwString::fromUtf8("\xE2\x96\xB2");
        const SwString nextGlyph = horizontal ? SwString::fromUtf8("\xE2\x80\xBA") : SwString::fromUtf8("\xE2\x96\xBC");

        paintButton(m_scrollPrevRect, prevGlyph, prevEnabled, m_scrollPrevHovered, m_scrollPrevPressed);
        paintButton(m_scrollNextRect, nextGlyph, nextEnabled, m_scrollNextHovered, m_scrollNextPressed);
    }

    void scrollTabsBy_(int deltaPixels) {
        if (!m_scrollButtonsVisible || m_tabScrollMax <= 0) {
            return;
        }
        if (deltaPixels == 0) {
            return;
        }
        const int newOffset = clampInt(m_tabScrollOffset + deltaPixels, 0, m_tabScrollMax);
        if (newOffset == m_tabScrollOffset) {
            return;
        }
        m_tabScrollOffset = newOffset;
        update();
    }

    int scrollStepPixels_() const {
        const int extent = isHorizontalTabs() ? m_tabViewportRect.width : m_tabViewportRect.height;
        return clampInt(extent / 2, 20, 200);
    }

    void ensureTabVisible_(int index) {
        if (index < 0 || index >= m_tabs.size()) {
            return;
        }

        SwRect rect = this->rect();
        recalcTabRects(rect);
        if (!m_scrollButtonsVisible || m_tabScrollMax <= 0) {
            return;
        }

        const SwRect viewport = m_tabViewportRect;
        const SwRect tabRect = m_tabs[index].rect;

        if (isHorizontalTabs()) {
            const int tabLeft = tabRect.x;
            const int tabRight = tabRect.x + tabRect.width;
            const int viewLeft = viewport.x;
            const int viewRight = viewport.x + viewport.width;

            if (tabLeft < viewLeft) {
                m_tabScrollOffset = clampInt(m_tabScrollOffset - (viewLeft - tabLeft), 0, m_tabScrollMax);
            } else if (tabRight > viewRight) {
                m_tabScrollOffset = clampInt(m_tabScrollOffset + (tabRight - viewRight), 0, m_tabScrollMax);
            }
        } else {
            const int tabTop = tabRect.y;
            const int tabBottom = tabRect.y + tabRect.height;
            const int viewTop = viewport.y;
            const int viewBottom = viewport.y + viewport.height;

            if (tabTop < viewTop) {
                m_tabScrollOffset = clampInt(m_tabScrollOffset - (viewTop - tabTop), 0, m_tabScrollMax);
            } else if (tabBottom > viewBottom) {
                m_tabScrollOffset = clampInt(m_tabScrollOffset + (tabBottom - viewBottom), 0, m_tabScrollMax);
            }
        }

        recalcTabRects(rect);
    }

    void moveTab_(int from, int to) {
        if (from < 0 || from >= m_tabs.size() || to < 0 || to >= m_tabs.size()) {
            return;
        }
        if (from == to) {
            return;
        }

        Tab moving = m_tabs[from];
        if (to > from) {
            for (int i = from; i < to; ++i) {
                m_tabs[i] = m_tabs[i + 1];
            }
        } else {
            for (int i = from; i > to; --i) {
                m_tabs[i] = m_tabs[i - 1];
            }
        }
        m_tabs[to] = moving;

        if (m_currentIndex == from) {
            m_currentIndex = to;
        } else if (m_currentIndex >= 0) {
            if (from < to) {
                if (m_currentIndex > from && m_currentIndex <= to) {
                    --m_currentIndex;
                }
            } else {
                if (m_currentIndex >= to && m_currentIndex < from) {
                    ++m_currentIndex;
                }
            }
        }

        tabMoved(from, to);
        updateLayout();
        update();
    }

private:
    SwVector<Tab> m_tabs;
    int m_currentIndex{-1};
    int m_hoverIndex{-1};
    int m_pressedIndex{-1};
    int m_closeHoverIndex{-1};
    int m_closePressedIndex{-1};

    int m_pressX{0};
    int m_pressY{0};
    int m_dragIndex{-1};
    bool m_dragging{false};

    TabPosition m_tabPosition{TabPosition::Top};
    TabStyle m_tabStyle{TabStyle::Underline};

    bool m_tabsFillSpace{true};
    bool m_tabBarFullBleed{true};

    SwColor m_surfaceColor{255, 255, 255};
    SwColor m_borderColor{226, 232, 240};
    SwColor m_tabBarColor{243, 244, 246};
    SwColor m_accentColor{59, 130, 246};

    int m_outerPadding{12};
    int m_pagePadding{14};
    int m_tabToContentGap{10};

    int m_cornerRadius{18};
    int m_tabBarRadius{14};
    int m_tabItemRadius{12};
    int m_indicatorThickness{3};
    int m_indicatorPadding{0};

    int m_tabBarHeight{54}; // horizontal
    int m_tabBarWidth{180}; // vertical

    int m_barPadding{6};
    int m_tabPaddingX{16};
    int m_tabTextIndent{22};
    int m_tabSpacing{8};

    int m_tabItemHeight{44};

    int m_minTabWidth{84};
    int m_minTabHeight{40};

    bool m_usesScrollButtons{true};
    bool m_scrollButtonsVisible{false};
    int m_tabScrollOffset{0};
    int m_tabScrollMax{0};
    SwRect m_tabViewportRect{0, 0, 0, 0};
    SwRect m_scrollPrevRect{0, 0, 0, 0};
    SwRect m_scrollNextRect{0, 0, 0, 0};
    bool m_scrollPrevHovered{false};
    bool m_scrollNextHovered{false};
    bool m_scrollPrevPressed{false};
    bool m_scrollNextPressed{false};
};

