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
 * SwMenu - Qt-like popup menu (≈ QMenu).
 *
 * Notes:
 * - Implemented as an action container (SwObject) with an internal popup widget.
 * - Uses a transparent overlay to capture outside clicks and close the menu.
 * - Rendered using SwPainter; styleable via StyleSheet on the internal popup widget.
 **************************************************************************************************/

#include "SwAction.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"

#include "core/runtime/SwTimer.h"
#include "core/types/SwVector.h"

#include <algorithm>
#include <functional>

class SwMenu : public SwObject {
    SW_OBJECT(SwMenu, SwObject)

public:
    explicit SwMenu(SwObject* parent = nullptr)
        : SwObject(parent) {}

    SwAction* addAction(const SwString& text) {
        auto* action = new SwAction(text, this);
        addAction(action);
        return action;
    }

    SwAction* addAction(const SwImage& icon, const SwString& text) {
        SwAction* action = addAction(text);
        if (action) {
            action->setIcon(icon);
        }
        return action;
    }

    SwAction* addAction(const SwString& text, const std::function<void()>& callback) {
        SwAction* action = addAction(text);
        if (callback) {
            SwObject::connect(action, &SwAction::triggered, this, [callback](bool) { callback(); });
        }
        return action;
    }

    SwAction* addAction(const SwImage& icon, const SwString& text, const std::function<void()>& callback) {
        SwAction* action = addAction(text, callback);
        if (action) {
            action->setIcon(icon);
        }
        return action;
    }

    void addAction(SwAction* action) {
        if (!action) {
            return;
        }
        if (!action->parent()) {
            action->setParent(this);
        }
        m_actions.push_back(action);

        SwObject::connect(action, &SwAction::changed, this, [this]() {
            if (m_popup) {
                m_popup->updateGeometryFromActions();
                m_popup->update();
            }
        });

        if (m_popup) {
            m_popup->updateGeometryFromActions();
            m_popup->update();
        }
    }

    SwAction* addSeparator() {
        auto* action = new SwAction(SwString(), this);
        action->setSeparator(true);
        addAction(action);
        return action;
    }

    SwMenu* addMenu(const SwString& text) {
        auto* sub = new SwMenu(this);
        SwAction* a = addAction(text);
        if (a) {
            a->setMenu(sub);
        }
        return sub;
    }

    SwMenu* addMenu(const SwImage& icon, const SwString& text) {
        SwMenu* sub = addMenu(text);
        SwAction* a = m_actions.isEmpty() ? nullptr : m_actions.back();
        if (a) {
            a->setIcon(icon);
        }
        return sub;
    }

    SwVector<SwAction*> actions() const { return m_actions; }

    void popup(int x, int y) {
        ensurePopup();
        if (!m_root || !m_overlay || !m_popup) {
            return;
        }

        aboutToShow();

        m_overlay->move(0, 0);
        m_overlay->resize(m_root->width(), m_root->height());

        m_popup->updateGeometryFromActions();
        const SwRect size = m_popup->sizeHint();

        const int maxX = std::max(0, m_root->width() - size.width - 2);
        const int maxY = std::max(0, m_root->height() - size.height - 2);
        const int px = clampInt(x, 2, maxX);
        const int py = clampInt(y, 2, maxY);

        m_popup->move(px, py);
        m_popup->resize(size.width, size.height);

        m_overlay->show();
        m_popup->show();
        m_overlay->update();
        m_popup->update();
    }

    void popup(int x, int y, const SwRect& hoverKeepAliveRect) {
        m_hoverCloseEnabled = true;
        m_hoverKeepAliveRect = hoverKeepAliveRect;
        popup(x, y);
    }

    void hide() {
        closeSubMenu_();
        stopHoverCloseTimer_();
        m_hoverCloseEnabled = false;
        m_hoverKeepAliveRect = SwRect();

        if (m_popup && m_popup->getVisible()) {
            aboutToHide();
        }
        if (m_popup) {
            m_popup->hide();
        }
        if (m_overlay && m_ownsOverlay) {
            m_overlay->hide();
        }
    }

    void hideAll() {
        if (SwMenu* root = rootMenu_()) {
            root->hide();
        }
    }

    bool isVisible() const { return m_popup && m_popup->getVisible(); }

signals:
    DECLARE_SIGNAL_VOID(aboutToShow);
    DECLARE_SIGNAL_VOID(aboutToHide);
    DECLARE_SIGNAL(triggered, SwAction*);

private:
    class PopupList;

    class PopupOverlay final : public SwWidget {
        SW_OBJECT(PopupOverlay, SwWidget)

    public:
        PopupOverlay(SwMenu* owner, SwWidget* root, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner)
            , m_root(root) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
            setFocusPolicy(FocusPolicyEnum::Strong);
        }

        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }

            const int px = event->x();
            const int py = event->y();

            if (m_owner->isPointInsideAnyPopup_(px, py)) {
                SwWidget::mousePressEvent(event);
                return;
            }

            if (m_owner->isPointInsideHoverKeepAlive_(px, py)) {
                if (m_root) {
                    MouseEvent forwarded(EventType::MousePressEvent,
                                         px,
                                         py,
                                         event->button(),
                                         event->isCtrlPressed(),
                                         event->isShiftPressed(),
                                         event->isAltPressed());
                    static_cast<SwWidgetInterface*>(m_root)->mousePressEvent(&forwarded);
                }
                event->accept();
                return;
            }

            if (!m_owner->isPointInsideAnyPopup_(px, py)) {
                m_owner->hideAll();
                if (m_root) {
                    MouseEvent forwarded(EventType::MousePressEvent,
                                         px,
                                         py,
                                         event->button(),
                                         event->isCtrlPressed(),
                                         event->isShiftPressed(),
                                         event->isAltPressed());
                    static_cast<SwWidgetInterface*>(m_root)->mousePressEvent(&forwarded);
                }
                event->accept();
                return;
            }

            SwWidget::mousePressEvent(event);
        }

        void mouseMoveEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            SwWidget::mouseMoveEvent(event);
            m_owner->onOverlayMouseMove_(event->x(), event->y());
        }

        void keyPressEvent(KeyEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            SwWidget::keyPressEvent(event);
            if (event->isAccepted()) {
                return;
            }
            if (SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
                m_owner->hideAll();
                event->accept();
            }
        }

    private:
        SwMenu* m_owner{nullptr};
        SwWidget* m_root{nullptr};
    };

    class PopupList final : public SwWidget {
        SW_OBJECT(PopupList, SwWidget)

    public:
        explicit PopupList(SwMenu* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner) {
            setCursor(CursorType::Hand);
            setFocusPolicy(FocusPolicyEnum::Strong);
            setStyleSheet(R"(
                SwMenuPopup {
                    background-color: rgb(255, 255, 255);
                    border-color: rgb(220, 224, 232);
                    border-width: 1px;
                    border-radius: 12px;
                    padding: 8px;
                    color: rgb(24, 28, 36);
                    font-size: 13px;
                }
            )");
        }

        void updateGeometryFromActions() {
            const int n = m_owner ? m_owner->m_actions.size() : 0;
            const int h = m_padding * 2 + n * m_itemHeight;
            m_cachedHeight = std::max(0, h);

            const int kRowPad = 8;
            const int kCheckAreaW = 18;
            const int kIconAreaW = 18;
            const int kIconTextGap = 6;
            const int kArrowAreaW = 14;
            const int kRightPad = 8;
            m_textLeftPad = kRowPad + kCheckAreaW + kIconAreaW + kIconTextGap;
            m_textRightPad = kRightPad + kArrowAreaW;

            int w = m_minWidth;
            if (m_owner) {
                const SwFont font = getFont();
                const auto handle = nativeWindowHandle();
                for (SwAction* action : m_owner->m_actions) {
                    if (!action || action->getSeparator()) {
                        continue;
                    }
                    const SwString t = action->getText();
                    const int tw = SwWidgetPlatformAdapter::textWidthUntil(handle, t, font, t.size(), 5000);
                    w = std::max(w, tw + m_textLeftPad + m_textRightPad);
                }
            }
            m_cachedWidth = std::max(m_minWidth, w);
        }

        SwRect sizeHint() const override {
            return SwRect{0, 0, m_cachedWidth, m_cachedHeight};
        }

        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }

            StyleSheet* sheet = getToolSheet();
            SwColor bg{255, 255, 255};
            float bgAlpha = 1.0f;
            bool paintBg = true;

            SwColor border{220, 224, 232};
            int borderWidth = 1;
            int radius = 12;

            resolveBackground(sheet, bg, bgAlpha, paintBg);
            resolveBorder(sheet, border, borderWidth, radius);

            const SwRect bounds = getRect();
            painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);

            const SwColor textColor = resolveTextColor(sheet, SwColor{24, 28, 36});
            const SwColor disabledText{148, 163, 184};
            const SwColor hoverFill{241, 245, 249};
            const SwColor hoverBorder{241, 245, 249};
            const SwColor sep{226, 232, 240};

            const int kRowPad = 8;
            const int kCheckAreaW = 18;
            const int kIconAreaW = 18;
            const int kIconSize = 16;
            const int kArrowAreaW = 14;
            const int kRightPad = 8;

            const int startY = bounds.y + m_padding;
            const int startX = bounds.x + m_padding;
            const int contentW = std::max(0, bounds.width - 2 * m_padding);

            for (int i = 0; m_owner && i < m_owner->m_actions.size(); ++i) {
                SwAction* action = m_owner->m_actions[i];
                SwRect row{startX, startY + i * m_itemHeight, contentW, m_itemHeight};

                if (!action) {
                    continue;
                }

                if (action->getSeparator()) {
                    const int y = row.y + row.height / 2;
                    painter->drawLine(row.x + 6, y, row.x + row.width - 6, y, sep, 1);
                    continue;
                }

                const bool enabled = action->getEnabled();
                const bool hovered = (i == m_hoverIndex);

                if (hovered && enabled) {
                    SwRect hi{row.x + 2, row.y + 2, std::max(0, row.width - 4), std::max(0, row.height - 4)};
                    painter->fillRoundedRect(hi, 10, hoverFill, hoverBorder, 0);
                }

                if (action->getCheckable() && action->getChecked()) {
                    const int cx = row.x + kRowPad + kCheckAreaW / 2;
                    const int cy = row.y + row.height / 2;
                    painter->drawLine(cx - 4, cy, cx - 1, cy + 3, enabled ? textColor : disabledText, 2);
                    painter->drawLine(cx - 1, cy + 3, cx + 5, cy - 3, enabled ? textColor : disabledText, 2);
                }

                if (action->hasIcon()) {
                    const SwRect iconRect{row.x + kRowPad + kCheckAreaW,
                                          row.y + (row.height - kIconSize) / 2,
                                          kIconSize,
                                          kIconSize};
                    painter->drawImage(iconRect, action->icon(), nullptr);
                }

                SwRect textRect{row.x + m_textLeftPad,
                                row.y,
                                std::max(0, row.width - m_textLeftPad - m_textRightPad),
                                row.height};
                painter->drawText(textRect,
                                  action->getText(),
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  enabled ? textColor : disabledText,
                                  getFont());

                if (action->hasMenu()) {
                    const int midY = row.y + row.height / 2;
                    const int tipX = row.x + row.width - kRightPad - 2;
                    const int leftX = tipX - 6;
                    painter->drawLine(leftX, midY - 5, tipX, midY, enabled ? textColor : disabledText, 2);
                    painter->drawLine(leftX, midY + 5, tipX, midY, enabled ? textColor : disabledText, 2);
                }
            }
        }

        void mouseMoveEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }

            const int px = event->x();
            const int py = event->y();
            const int idx = indexAt(px, py);

            if (idx >= 0) {
                if (idx != m_hoverIndex) {
                    m_hoverIndex = idx;
                    update();
                    m_owner->onPopupHoverChanged_(idx, rowRectForIndex_(idx));
                }
                event->accept();
                return;
            }

            if (m_owner->isPointInsideOpenSubMenuBridge_(px, py)) {
                event->accept();
                return;
            }

            if (m_hoverIndex != -1) {
                m_hoverIndex = -1;
                update();
                m_owner->onPopupHoverChanged_(-1, SwRect());
            }
            event->accept();
        }

        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            const int idx = indexAt(event->x(), event->y());
            SwAction* action = actionAt(idx);
            if (action && !action->getSeparator() && action->getEnabled()) {
                if (action->hasMenu()) {
                    m_hoverIndex = idx;
                    update();
                    m_owner->onPopupHoverChanged_(idx, rowRectForIndex_(idx));
                    event->accept();
                    return;
                }

                action->trigger();
                m_owner->triggered(action);
                m_owner->hideAll();
                event->accept();
                return;
            }
            SwWidget::mousePressEvent(event);
        }

        void keyPressEvent(KeyEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            SwWidget::keyPressEvent(event);
            if (event->isAccepted()) {
                return;
            }

            const int key = event->key();
            if (SwWidgetPlatformAdapter::isEscapeKey(key)) {
                m_owner->hideAll();
                event->accept();
                return;
            }

            if (SwWidgetPlatformAdapter::isUpArrowKey(key)) {
                moveHover(-1);
                if (m_hoverIndex >= 0) {
                    m_owner->onPopupHoverChanged_(m_hoverIndex, rowRectForIndex_(m_hoverIndex));
                }
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isDownArrowKey(key)) {
                moveHover(1);
                if (m_hoverIndex >= 0) {
                    m_owner->onPopupHoverChanged_(m_hoverIndex, rowRectForIndex_(m_hoverIndex));
                }
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isReturnKey(key)) {
                SwAction* action = actionAt(m_hoverIndex);
                if (action && !action->getSeparator() && action->getEnabled()) {
                    if (action->hasMenu()) {
                        m_owner->onPopupHoverChanged_(m_hoverIndex, rowRectForIndex_(m_hoverIndex));
                    } else {
                        action->trigger();
                        m_owner->triggered(action);
                        m_owner->hideAll();
                    }
                }
                event->accept();
                return;
            }
        }

    private:
        static int clampInt(int value, int minValue, int maxValue) {
            if (value < minValue) return minValue;
            if (value > maxValue) return maxValue;
            return value;
        }

        static SwColor clampColor(const SwColor& c) {
            return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
        }

        static int parsePixelValue(const SwString& value, int defaultValue) {
            if (value.isEmpty()) {
                return defaultValue;
            }
            SwString cleaned = value;
            cleaned.replace("px", "");
            bool ok = false;
            int v = cleaned.toInt(&ok);
            return ok ? v : defaultValue;
        }

        void resolveBackground(const StyleSheet* sheet,
                               SwColor& outColor,
                               float& outAlpha,
                               bool& outPaint) const {
            if (!sheet) {
                return;
            }
            auto selectors = classHierarchy();
            selectors.insert(selectors.begin(), "SwMenuPopup");
            bool hasSwWidgetSelector = false;
            for (const SwString& selector : selectors) {
                if (selector == "SwWidget") {
                    hasSwWidgetSelector = true;
                    break;
                }
            }
            if (!hasSwWidgetSelector) {
                selectors.emplace_back("SwWidget");
            }
            for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
                const SwString& selector = selectors[i];
                if (selector.isEmpty()) {
                    continue;
                }
                SwString value = sheet->getStyleProperty(selector.toStdString(), "background-color");
                if (value.isEmpty()) {
                    continue;
                }
                float alpha = 1.0f;
                try {
                    SwColor resolved = const_cast<StyleSheet*>(sheet)->parseColor(value.toStdString(), &alpha);
                    if (alpha <= 0.0f) {
                        outPaint = false;
                    } else {
                        outColor = clampColor(resolved);
                        outPaint = true;
                    }
                    outAlpha = alpha;
                } catch (...) {
                }
                return;
            }
        }

        void resolveBorder(const StyleSheet* sheet,
                           SwColor& outColor,
                           int& outWidth,
                           int& outRadius) const {
            if (!sheet) {
                return;
            }
            auto selectors = classHierarchy();
            selectors.insert(selectors.begin(), "SwMenuPopup");
            bool hasSwWidgetSelector = false;
            for (const SwString& selector : selectors) {
                if (selector == "SwWidget") {
                    hasSwWidgetSelector = true;
                    break;
                }
            }
            if (!hasSwWidgetSelector) {
                selectors.emplace_back("SwWidget");
            }

            for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
                const SwString& selector = selectors[i];
                if (selector.isEmpty()) {
                    continue;
                }

                SwString borderColor = sheet->getStyleProperty(selector.toStdString(), "border-color");
                if (!borderColor.isEmpty()) {
                    try {
                        SwColor resolved = const_cast<StyleSheet*>(sheet)->parseColor(borderColor.toStdString(), nullptr);
                        outColor = clampColor(resolved);
                    } catch (...) {
                    }
                }

                SwString borderWidth = sheet->getStyleProperty(selector.toStdString(), "border-width");
                if (!borderWidth.isEmpty()) {
                    outWidth = clampInt(parsePixelValue(borderWidth, outWidth), 0, 20);
                }

                SwString borderRadius = sheet->getStyleProperty(selector.toStdString(), "border-radius");
                if (!borderRadius.isEmpty()) {
                    outRadius = clampInt(parsePixelValue(borderRadius, outRadius), 0, 32);
                }
            }
        }

        SwColor resolveTextColor(StyleSheet* sheet, const SwColor& fallback) const {
            if (!sheet) {
                return fallback;
            }
            auto selectors = classHierarchy();
            selectors.insert(selectors.begin(), "SwMenuPopup");
            bool hasSwWidgetSelector = false;
            for (const SwString& selector : selectors) {
                if (selector == "SwWidget") {
                    hasSwWidgetSelector = true;
                    break;
                }
            }
            if (!hasSwWidgetSelector) {
                selectors.emplace_back("SwWidget");
            }
            for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
                const SwString& selector = selectors[i];
                if (selector.isEmpty()) {
                    continue;
                }
                SwString value = sheet->getStyleProperty(selector.toStdString(), "color");
                if (value.isEmpty()) {
                    continue;
                }
                try {
                    SwColor resolved = sheet->parseColor(value.toStdString(), nullptr);
                    return clampColor(resolved);
                } catch (...) {
                    return fallback;
                }
            }
            return fallback;
        }

        int indexAt(int px, int py) const {
            const SwRect bounds = getRect();
            const int y0 = bounds.y + m_padding;
            const int x0 = bounds.x + m_padding;
            if (py < y0 || px < x0 || px > (x0 + std::max(0, bounds.width - 2 * m_padding))) {
                return -1;
            }
            const int idx = (py - y0) / std::max(1, m_itemHeight);
            if (!m_owner || idx < 0 || idx >= m_owner->m_actions.size()) {
                return -1;
            }
            return idx;
        }

        SwAction* actionAt(int idx) const {
            if (!m_owner || idx < 0 || idx >= m_owner->m_actions.size()) {
                return nullptr;
            }
            return m_owner->m_actions[idx];
        }

        void moveHover(int delta) {
            if (!m_owner || m_owner->m_actions.size() == 0) {
                return;
            }
            int idx = m_hoverIndex;
            if (idx < 0 || idx >= m_owner->m_actions.size()) {
                idx = (delta >= 0) ? -1 : m_owner->m_actions.size();
            }
            for (int step = 0; step < m_owner->m_actions.size(); ++step) {
                idx += delta;
                if (idx < 0) idx = m_owner->m_actions.size() - 1;
                if (idx >= m_owner->m_actions.size()) idx = 0;
                SwAction* a = m_owner->m_actions[idx];
                if (a && !a->getSeparator()) {
                    m_hoverIndex = idx;
                    update();
                    break;
                }
            }
        }

        SwRect rowRectForIndex_(int idx) const {
            const SwRect bounds = getRect();
            const int startY = bounds.y + m_padding;
            const int startX = bounds.x + m_padding;
            const int contentW = std::max(0, bounds.width - 2 * m_padding);
            return SwRect{startX, startY + idx * m_itemHeight, contentW, m_itemHeight};
        }

        SwMenu* m_owner{nullptr};
        int m_hoverIndex{-1};
        int m_itemHeight{28};
        int m_padding{8};
        int m_minWidth{220};
        int m_textLeftPad{28};
        int m_textRightPad{12};
        int m_cachedWidth{220};
        int m_cachedHeight{0};
    };

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwWidget* findRootWidget(SwObject* start) {
        SwWidget* lastWidget = nullptr;
        for (SwObject* p = start; p; p = p->parent()) {
            if (auto* w = dynamic_cast<SwWidget*>(p)) {
                lastWidget = w;
            }
        }
        return lastWidget;
    }

    static bool containsPoint_(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    static SwRect inflate_(SwRect rect, int margin) {
        rect.x -= margin;
        rect.y -= margin;
        rect.width = std::max(0, rect.width + margin * 2);
        rect.height = std::max(0, rect.height + margin * 2);
        return rect;
    }

    SwMenu* parentMenu_() const { return dynamic_cast<SwMenu*>(parent()); }

    SwMenu* rootMenu_() const {
        SwMenu* m = const_cast<SwMenu*>(this);
        while (m) {
            SwMenu* p = dynamic_cast<SwMenu*>(m->parent());
            if (!p) {
                break;
            }
            m = p;
        }
        return m;
    }

    bool isPointInsideHoverKeepAlive_(int px, int py) const {
        if (!m_hoverCloseEnabled) {
            return false;
        }
        return containsPoint_(m_hoverKeepAliveRect, px, py);
    }

    bool isPointInsideAnyPopupRecursive_(int px, int py) const {
        if (m_popup && m_popup->getVisible()) {
            if (containsPoint_(m_popup->getRect(), px, py)) {
                return true;
            }
        }
        if (m_openSubMenu) {
            return m_openSubMenu->isPointInsideAnyPopupRecursive_(px, py);
        }
        return false;
    }

    bool isPointInsideAnyPopup_(int px, int py) const {
        const SwMenu* root = rootMenu_();
        return root ? root->isPointInsideAnyPopupRecursive_(px, py) : false;
    }

    bool isPointInsideOpenSubMenuBridge_(int px, int py) const {
        if (!m_popup || !m_openSubMenu) {
            return false;
        }

        SwRect bounds = m_popup->getRect();
        int minX = bounds.x;
        int minY = bounds.y;
        int maxX = bounds.x + bounds.width;
        int maxY = bounds.y + bounds.height;

        const SwMenu* cur = m_openSubMenu;
        while (cur && cur->m_popup && cur->isVisible()) {
            const SwRect r = cur->m_popup->getRect();
            minX = std::min(minX, r.x);
            minY = std::min(minY, r.y);
            maxX = std::max(maxX, r.x + r.width);
            maxY = std::max(maxY, r.y + r.height);
            cur = cur->m_openSubMenu;
        }

        bounds = SwRect{minX, minY, std::max(0, maxX - minX), std::max(0, maxY - minY)};
        return containsPoint_(inflate_(bounds, 8), px, py);
    }

    void closeSubMenu_() {
        if (!m_openSubMenu) {
            return;
        }
        m_openSubMenu->hide();
        m_openSubMenu = nullptr;
        m_openSubMenuIndex = -1;
    }

    void openSubMenu_(SwAction* action, const SwRect& row, int index) {
        if (!action || !action->hasMenu()) {
            closeSubMenu_();
            return;
        }

        SwMenu* sub = action->menu();
        if (!sub) {
            closeSubMenu_();
            return;
        }

        if (m_openSubMenu != sub) {
            closeSubMenu_();
            m_openSubMenu = sub;
            m_openSubMenuIndex = index;
        }

        sub->ensurePopup();
        if (!sub->m_popup || !m_popup) {
            return;
        }
        sub->m_popup->updateGeometryFromActions();
        const SwRect size = sub->m_popup->sizeHint();
        const SwRect parentPopup = m_popup->getRect();

        int px = parentPopup.x + parentPopup.width + 4;
        const int leftCandidate = parentPopup.x - size.width - 4;
        if (m_root) {
            const int maxX = std::max(0, m_root->width() - size.width - 2);
            if (px > maxX && leftCandidate >= 2) {
                px = leftCandidate;
            }
        }

        sub->popup(px, row.y);
    }

    void onPopupHoverChanged_(int idx, const SwRect& row) {
        if (idx < 0 || idx >= m_actions.size()) {
            closeSubMenu_();
            return;
        }
        SwAction* action = m_actions[idx];
        if (!action || action->getSeparator() || !action->getEnabled()) {
            closeSubMenu_();
            return;
        }
        if (action->hasMenu()) {
            openSubMenu_(action, row, idx);
        } else {
            closeSubMenu_();
        }
    }

    void ensureHoverCloseTimer_() {
        if (m_hoverCloseTimer) {
            return;
        }
        m_hoverCloseTimer = new SwTimer(180, this);
        m_hoverCloseTimer->setSingleShot(true);
        SwObject::connect(m_hoverCloseTimer, &SwTimer::timeout, this, [this]() {
            if (!m_hoverCloseEnabled) {
                return;
            }
            if (isPointInsideAnyPopup_(m_lastMouseX, m_lastMouseY) || isPointInsideHoverKeepAlive_(m_lastMouseX, m_lastMouseY)) {
                return;
            }
            hideAll();
        });
    }

    void startHoverCloseTimer_() {
        if (!m_hoverCloseEnabled) {
            return;
        }
        ensureHoverCloseTimer_();
        if (m_hoverCloseTimer && !m_hoverCloseTimer->isActive()) {
            m_hoverCloseTimer->start();
        }
    }

    void stopHoverCloseTimer_() {
        if (m_hoverCloseTimer && m_hoverCloseTimer->isActive()) {
            m_hoverCloseTimer->stop();
        }
    }

    void onOverlayMouseMove_(int px, int py) {
        m_lastMouseX = px;
        m_lastMouseY = py;

        if (!m_hoverCloseEnabled) {
            return;
        }

        if (isPointInsideAnyPopup_(px, py) || isPointInsideHoverKeepAlive_(px, py)) {
            stopHoverCloseTimer_();
            return;
        }
        startHoverCloseTimer_();
    }

    void ensurePopup() {
        if (m_popup && m_overlay && m_root) {
            return;
        }

        SwMenu* rootMenu = rootMenu_();
        if (rootMenu && rootMenu != this) {
            rootMenu->ensurePopup();
            m_root = rootMenu->m_root;
            m_overlay = rootMenu->m_overlay;
            m_ownsOverlay = false;
            if (!m_popup && m_overlay) {
                m_popup = new PopupList(this, m_overlay);
            }
            return;
        }

        m_root = findRootWidget(this);
        if (!m_root) {
            return;
        }

        if (!m_overlay) {
            m_overlay = new PopupOverlay(this, m_root, m_root);
            m_overlay->move(0, 0);
            m_overlay->resize(m_root->width(), m_root->height());
            m_ownsOverlay = true;
        }
        if (!m_popup) {
            m_popup = new PopupList(this, m_overlay);
        }

        if (!m_rootResizeHooked) {
            m_rootResizeHooked = true;
            SwObject::connect(m_root, &SwWidget::resized, this, [this](int w, int h) {
                if (m_overlay) {
                    m_overlay->move(0, 0);
                    m_overlay->resize(w, h);
                }
            });
        }
    }

    SwVector<SwAction*> m_actions;
    SwWidget* m_root{nullptr};
    PopupOverlay* m_overlay{nullptr};
    PopupList* m_popup{nullptr};

    bool m_ownsOverlay{true};
    bool m_rootResizeHooked{false};

    SwMenu* m_openSubMenu{nullptr};
    int m_openSubMenuIndex{-1};

    bool m_hoverCloseEnabled{false};
    SwRect m_hoverKeepAliveRect;
    SwTimer* m_hoverCloseTimer{nullptr};
    int m_lastMouseX{0};
    int m_lastMouseY{0};
};
