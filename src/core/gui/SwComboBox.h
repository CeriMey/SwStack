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
 * SwComboBox - Qt-like combo box widget (≈ QComboBox).
 *
 * Focus:
 * - Same core API (addItem, count, currentIndex/currentText, signals).
 * - Premium-ish look with a popup list (usable in snapshot via showPopup()).
 **************************************************************************************************/

#include "SwWidget.h"
#include "core/types/SwVector.h"

class SwComboBox : public SwWidget {
    SW_OBJECT(SwComboBox, SwWidget)

public:
    explicit SwComboBox(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    int count() const { return m_items.size(); }

    void clear() {
        m_items.clear();
        setCurrentIndex(-1);
        updatePopupGeometry();
        update();
    }

    void addItem(const SwString& text) {
        m_items.push_back(text);
        if (m_currentIndex < 0) {
            setCurrentIndex(0);
        }
        updatePopupGeometry();
        update();
    }

    void insertItem(int index, const SwString& text) {
        const int n = m_items.size();
        if (index < 0) {
            index = 0;
        }
        if (index > n) {
            index = n;
        }
        SwVector<SwString> copy;
        copy.reserve(static_cast<SwVector<SwString>::size_type>(n + 1));
        for (int i = 0; i < n + 1; ++i) {
            if (i == index) {
                copy.push_back(text);
            } else {
                const int src = (i < index) ? i : (i - 1);
                if (src >= 0 && src < n) {
                    copy.push_back(m_items[src]);
                }
            }
        }
        m_items = copy;
        if (m_currentIndex < 0) {
            setCurrentIndex(0);
        } else if (m_currentIndex >= index) {
            setCurrentIndex(m_currentIndex + 1);
        } else {
            update();
        }
        updatePopupGeometry();
    }

    SwString itemText(int index) const {
        if (index < 0 || index >= m_items.size()) {
            return {};
        }
        return m_items[index];
    }

    int currentIndex() const { return m_currentIndex; }

    SwString currentText() const {
        if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
            return {};
        }
        return m_items[m_currentIndex];
    }

    void setCurrentIndex(int index) {
        if (index < -1) {
            index = -1;
        }
        if (index >= m_items.size()) {
            index = m_items.size() - 1;
        }
        if (m_currentIndex == index) {
            return;
        }
        m_currentIndex = index;
        if (m_popupVisible) {
            syncPopupToCurrentIndex_();
        }
        currentIndexChanged(m_currentIndex);
        currentTextChanged(currentText());
        update();
    }

    void setMaxVisibleItems(int count) {
        m_maxVisibleItems = std::max(1, count);
        if (m_popupVisible) {
            ensurePopupHighlightVisible_();
            if (m_list) {
                m_list->update();
            }
        }
        updatePopupGeometry();
    }

    int maxVisibleItems() const { return m_maxVisibleItems; }

    void setAccentColor(const SwColor& color) {
        m_accent = clampColor(color);
        update();
    }

    SwColor accentColor() const { return m_accent; }

    void showPopup() { setPopupVisible(true); }
    void hidePopup() { setPopupVisible(false); }
    bool isPopupVisible() const { return m_popupVisible; }

    DECLARE_SIGNAL(currentIndexChanged, int);
    DECLARE_SIGNAL(currentTextChanged, SwString);
    DECLARE_SIGNAL(activated, int);
    DECLARE_SIGNAL(highlighted, int);

protected:
    CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();

        SwColor bg{255, 255, 255};
        SwColor border{172, 172, 172};
        SwColor textColor{30, 30, 30};
        SwColor arrow{60, 60, 60};

        int borderWidth = 1;
        int radius = 10;
        SwColor focusColor = m_accent;

        const StyleSheet* sheet = getToolSheet();
        std::vector<SwString> selectors = classHierarchy(); // most-derived first
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

        if (sheet) {
            const SwString bw = getStyleValue_(sheet, selectors, "border-width");
            if (!bw.isEmpty()) {
                borderWidth = clampInt(parsePixelValue_(bw, borderWidth), 0, 20);
            }

            const SwString br = getStyleValue_(sheet, selectors, "border-radius");
            if (!br.isEmpty()) {
                radius = clampInt(parsePixelValue_(br, radius), 0, 32);
            }

            // Optional focus customization (non-Qt but stylesheet-friendly).
            SwString fc = getStyleValue_(sheet, selectors, "focus-border-color");
            if (fc.isEmpty()) {
                fc = getStyleValue_(sheet, selectors, "focus-color");
            }
            if (!fc.isEmpty()) {
                (void)tryParseColor_(sheet, fc, focusColor);
            }
        }

        if (!getEnable()) {
            bg = SwColor{245, 245, 245};
            border = SwColor{210, 210, 210};
            textColor = SwColor{150, 150, 150};
            arrow = SwColor{150, 150, 150};
        } else if (getPressed() || m_popupVisible) {
            bg = SwColor{244, 246, 250};
            border = SwColor{140, 140, 140};
        } else if (getHover()) {
            bg = SwColor{250, 251, 253};
            border = SwColor{150, 150, 150};
        }

        // Match Qt-ish behaviour: focus changes the border color, preserving border-radius.
        if (getFocus() && getEnable()) {
            border = focusColor;
        }

        painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);

        const int padL = 12;
        const int padR = 30;
        SwRect textRect{bounds.x + padL,
                        bounds.y,
                        std::max(0, bounds.width - padL - padR),
                        bounds.height};
        painter->drawText(textRect,
                          currentText(),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          getFont());

        // Arrow indicator (two lines)
        const int cx = bounds.x + bounds.width - 16;
        const int cy = bounds.y + bounds.height / 2;
        painter->drawLine(cx - 4, cy - 2, cx, cy + 2, arrow, 2);
        painter->drawLine(cx, cy + 2, cx + 4, cy - 2, arrow, 2);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }
        setPressed(true);
        event->accept();
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const bool wasPressed = getPressed();
        setPressed(false);

        if (wasPressed && isPointInside(event->x(), event->y())) {
            setPopupVisible(!m_popupVisible);
            event->accept();
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }
        if (!getEnable() || !getFocus()) {
            SwWidget::keyPressEvent(event);
            return;
        }

        const int n = count();
        const int key = event->key();

        if (m_popupVisible) {
            if (SwWidgetPlatformAdapter::isEscapeKey(key)) {
                hidePopup();
                event->accept();
                return;
            }

            if (SwWidgetPlatformAdapter::isReturnKey(key)) {
                commitPopupHighlighted_();
                event->accept();
                return;
            }

            if (SwWidgetPlatformAdapter::isUpArrowKey(key)) {
                movePopupHighlightBy_(-1);
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isDownArrowKey(key)) {
                movePopupHighlightBy_(+1);
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isHomeKey(key)) {
                if (n > 0) {
                    setPopupHighlightedIndex_(0);
                }
                event->accept();
                return;
            }
            if (SwWidgetPlatformAdapter::isEndKey(key)) {
                if (n > 0) {
                    setPopupHighlightedIndex_(n - 1);
                }
                event->accept();
                return;
            }

            // Simple type-ahead (single character) to match the next item starting with it.
            char ch = '\0';
            const bool caps = SwWidgetPlatformAdapter::isCapsLockKey(key);
            if (SwWidgetPlatformAdapter::translateCharacter(key, event->isShiftPressed(), caps, ch)) {
                if (ch != '\r' && ch != '\n' && ch != '\t') {
                    const int from = (m_popupHighlightedIndex >= 0) ? (m_popupHighlightedIndex + 1) : 0;
                    const int found = findNextByFirstChar_(ch, from);
                    if (found >= 0) {
                        setPopupHighlightedIndex_(found);
                        event->accept();
                        return;
                    }
                }
            }

            SwWidget::keyPressEvent(event);
            return;
        }

        if (SwWidgetPlatformAdapter::isUpArrowKey(key)) {
            stepCurrentIndexBy_(-1);
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::isDownArrowKey(key)) {
            stepCurrentIndexBy_(+1);
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::isHomeKey(key)) {
            if (n > 0) {
                setCurrentIndex(0);
            }
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::isEndKey(key)) {
            if (n > 0) {
                setCurrentIndex(n - 1);
            }
            event->accept();
            return;
        }

        if (SwWidgetPlatformAdapter::isReturnKey(key)) {
            showPopup();
            event->accept();
            return;
        }

        // Simple type-ahead (single character) on the closed combobox.
        char ch = '\0';
        const bool caps = SwWidgetPlatformAdapter::isCapsLockKey(key);
        if (SwWidgetPlatformAdapter::translateCharacter(key, event->isShiftPressed(), caps, ch)) {
            if (ch != '\r' && ch != '\n' && ch != '\t') {
                const int from = (m_currentIndex >= 0) ? (m_currentIndex + 1) : 0;
                const int found = findNextByFirstChar_(ch, from);
                if (found >= 0) {
                    setCurrentIndex(found);
                    event->accept();
                    return;
                }
            }
        }

        SwWidget::keyPressEvent(event);
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!getEnable()) {
            SwWidget::wheelEvent(event);
            return;
        }

        const int steps = wheelSteps_(event->delta());
        if (steps == 0) {
            SwWidget::wheelEvent(event);
            return;
        }

        if (m_popupVisible) {
            movePopupHighlightBy_(-steps);
            event->accept();
            return;
        }

        stepCurrentIndexBy_(-steps);
        event->accept();
    }

    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updatePopupGeometry();
    }

private:
    class PopupList;

    class PopupOverlay : public SwWidget {
        SW_OBJECT(PopupOverlay, SwWidget)

    public:
        PopupOverlay(SwComboBox* owner, SwWidget* root, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner)
            , m_root(root) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        void setList(PopupList* list) { m_list = list; }

        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }

            // If click is outside the list, close the popup.
            if (m_list) {
                const SwRect listRect = m_list->getRect();
                if (!containsPoint(listRect, event->x(), event->y())) {
                    const SwRect ownerRect = m_owner->getRect();
                    m_owner->hidePopup();

                    // Forward the click to the underlying widgets unless user clicked the anchor.
                    if (m_root && !containsPoint(ownerRect, event->x(), event->y())) {
                        MouseEvent forwarded(EventType::MousePressEvent,
                                             event->x(),
                                             event->y(),
                                             event->button(),
                                             event->isCtrlPressed(),
                                             event->isShiftPressed(),
                                             event->isAltPressed());
                        static_cast<SwWidgetInterface*>(m_root)->mousePressEvent(&forwarded);
                    }
                    event->accept();
                    return;
                }
            }

            SwWidget::mousePressEvent(event);
        }

    private:
        static bool containsPoint(const SwRect& r, int px, int py) {
            return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
        }

        SwComboBox* m_owner{nullptr};
        SwWidget* m_root{nullptr};
        PopupList* m_list{nullptr};
    };

    class PopupList : public SwWidget {
        SW_OBJECT(PopupList, SwWidget)

    public:
        explicit PopupList(SwComboBox* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner) {
            setCursor(CursorType::Hand);
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter || !m_owner) {
                return;
            }

            const SwRect bounds = getRect();
            painter->fillRoundedRect(bounds, 12, SwColor{255, 255, 255}, SwColor{200, 200, 200}, 1);

            const int n = m_owner->count();
            const int visible = std::min(n, m_owner->m_maxVisibleItems);
            const int itemH = m_owner->m_itemHeight;
            const int pad = m_owner->m_popupPadding;
            const int start = m_owner->m_popupFirstIndex;

            for (int i = 0; i < visible; ++i) {
                const int index = start + i;
                if (index < 0 || index >= n) {
                    continue;
                }
                SwRect row{bounds.x + pad,
                           bounds.y + pad + i * itemH,
                           std::max(0, bounds.width - 2 * pad),
                           itemH};

                const bool hovered = (index == m_owner->m_popupHighlightedIndex);
                const bool selected = (index == m_owner->m_currentIndex);

                if (hovered) {
                    painter->fillRoundedRect(row, 8, SwColor{244, 246, 250}, SwColor{244, 246, 250}, 0);
                }

                SwColor textColor = selected ? SwColor{24, 28, 36} : SwColor{30, 30, 30};
                painter->drawText(row,
                                  m_owner->itemText(index),
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  textColor,
                                  m_owner->getFont());

                if (selected) {
                    // Small selection pill on the left.
                    SwRect pill{row.x + 6, row.y + 6, 4, std::max(0, row.height - 12)};
                    painter->fillRoundedRect(pill, 2, m_owner->m_accent, m_owner->m_accent, 0);
                }
            }
        }

        void mouseMoveEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            const int idx = indexAt(event->x(), event->y());
            if (idx >= 0) {
                m_owner->setPopupHighlightedIndex_(idx);
                update();
            }
            event->accept();
        }

        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            const int idx = indexAt(event->x(), event->y());
            if (idx >= 0 && idx < m_owner->count()) {
                m_owner->setPopupHighlightedIndex_(idx);
                m_owner->commitPopupHighlighted_();
                event->accept();
                return;
            }
            SwWidget::mousePressEvent(event);
        }

        void wheelEvent(WheelEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            const int steps = SwComboBox::wheelSteps_(event->delta());
            if (steps == 0) {
                return;
            }
            m_owner->movePopupHighlightBy_(-steps);
            event->accept();
        }

    private:
        int indexAt(int px, int py) const {
            const SwRect bounds = getRect();
            const int pad = m_owner->m_popupPadding;
            const int itemH = m_owner->m_itemHeight;
            const int y0 = bounds.y + pad;
            if (py < y0) {
                return -1;
            }
            const int idx = (py - y0) / std::max(1, itemH);
            const int visible = std::min(m_owner->count(), m_owner->m_maxVisibleItems);
            if (idx < 0 || idx >= visible) {
                return -1;
            }
            const int absolute = m_owner->m_popupFirstIndex + idx;
            if (absolute < 0 || absolute >= m_owner->count()) {
                return -1;
            }
            return absolute;
        }

        SwComboBox* m_owner{nullptr};
    };

    static int wheelSteps_(int delta) {
        if (delta == 0) {
            return 0;
        }
        int steps = delta / 120;
        if (steps == 0) {
            steps = (delta > 0) ? 1 : -1;
        }
        return steps;
    }

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor(const SwColor& c) {
        return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
    }

    static int parsePixelValue_(const SwString& value, int defaultValue) {
        if (value.isEmpty()) {
            return defaultValue;
        }
        SwString cleaned = value;
        cleaned.replace("px", "");
        bool ok = false;
        const int v = cleaned.toInt(&ok);
        return ok ? v : defaultValue;
    }

    static bool tryParseColor_(const StyleSheet* sheet, const SwString& value, SwColor& outColor) {
        if (!sheet || value.isEmpty()) {
            return false;
        }
        try {
            outColor = clampColor(const_cast<StyleSheet*>(sheet)->parseColor(value.toStdString(), nullptr));
            return true;
        } catch (...) {
            return false;
        }
    }

    static SwString getStyleValue_(const StyleSheet* sheet,
                                  const std::vector<SwString>& hierarchy,
                                  const char* propertyName) {
        if (!sheet || !propertyName) {
            return SwString();
        }

        SwString out;
        for (int i = static_cast<int>(hierarchy.size()) - 1; i >= 0; --i) {
            const SwString& selector = hierarchy[static_cast<size_t>(i)];
            if (selector.isEmpty()) {
                continue;
            }
            SwString v = sheet->getStyleProperty(selector.toStdString(), propertyName);
            if (!v.isEmpty()) {
                out = v;
            }
        }
        return out;
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

    int popupVisibleCount_() const {
        return std::min(count(), m_maxVisibleItems);
    }

    void clampPopupFirstIndex_() {
        const int n = count();
        const int visible = popupVisibleCount_();
        const int maxFirst = std::max(0, n - visible);
        m_popupFirstIndex = clampInt(m_popupFirstIndex, 0, maxFirst);
    }

    void ensurePopupHighlightVisible_() {
        const int n = count();
        const int visible = popupVisibleCount_();
        if (n <= 0 || visible <= 0 || m_popupHighlightedIndex < 0) {
            m_popupFirstIndex = 0;
            return;
        }

        clampPopupFirstIndex_();

        if (m_popupHighlightedIndex < m_popupFirstIndex) {
            m_popupFirstIndex = m_popupHighlightedIndex;
        } else if (m_popupHighlightedIndex >= m_popupFirstIndex + visible) {
            m_popupFirstIndex = m_popupHighlightedIndex - visible + 1;
        }

        clampPopupFirstIndex_();
    }

    void syncPopupToCurrentIndex_() {
        const int n = count();
        if (n <= 0) {
            m_popupHighlightedIndex = -1;
            m_popupFirstIndex = 0;
        } else {
            if (m_currentIndex >= 0 && m_currentIndex < n) {
                m_popupHighlightedIndex = m_currentIndex;
            } else if (m_popupHighlightedIndex < 0) {
                m_popupHighlightedIndex = 0;
            } else {
                m_popupHighlightedIndex = clampInt(m_popupHighlightedIndex, 0, n - 1);
            }
            ensurePopupHighlightVisible_();
        }
        if (m_list) {
            m_list->update();
        }
    }

    void setPopupHighlightedIndex_(int index) {
        const int n = count();
        if (n <= 0) {
            m_popupHighlightedIndex = -1;
            m_popupFirstIndex = 0;
            return;
        }
        index = clampInt(index, 0, n - 1);
        if (m_popupHighlightedIndex == index) {
            return;
        }
        m_popupHighlightedIndex = index;
        ensurePopupHighlightVisible_();
        highlighted(m_popupHighlightedIndex);
        if (m_list) {
            m_list->update();
        }
    }

    void movePopupHighlightBy_(int delta) {
        const int n = count();
        if (n <= 0) {
            return;
        }

        int idx = m_popupHighlightedIndex;
        if (idx < 0 || idx >= n) {
            idx = (delta >= 0) ? 0 : (n - 1);
        }

        idx = clampInt(idx + delta, 0, n - 1);
        setPopupHighlightedIndex_(idx);
    }

    void commitPopupHighlighted_() {
        const int n = count();
        if (n <= 0) {
            hidePopup();
            return;
        }
        int idx = m_popupHighlightedIndex;
        if (idx < 0 || idx >= n) {
            idx = (m_currentIndex >= 0 && m_currentIndex < n) ? m_currentIndex : 0;
        }
        setCurrentIndex(idx);
        activated(idx);
        hidePopup();
    }

    void stepCurrentIndexBy_(int delta) {
        const int n = count();
        if (n <= 0 || delta == 0) {
            return;
        }

        int idx = m_currentIndex;
        if (idx < 0 || idx >= n) {
            idx = (delta >= 0) ? 0 : (n - 1);
        } else {
            idx = clampInt(idx + delta, 0, n - 1);
        }
        setCurrentIndex(idx);
    }

    int findNextByFirstChar_(char ch, int fromIndex) const {
        const int n = count();
        if (n <= 0) {
            return -1;
        }
        if (fromIndex < 0) {
            fromIndex = 0;
        }
        if (fromIndex >= n) {
            fromIndex = 0;
        }

        auto toLowerAscii = [](char c) -> char {
            if (c >= 'A' && c <= 'Z') {
                return static_cast<char>(c + ('a' - 'A'));
            }
            return c;
        };

        const char target = toLowerAscii(ch);
        for (int i = 0; i < n; ++i) {
            const int idx = (fromIndex + i) % n;
            const std::string text = itemText(idx).toStdString();
            if (!text.empty() && toLowerAscii(text[0]) == target) {
                return idx;
            }
        }
        return -1;
    }

    void setPopupVisible(bool on) {
        if (m_popupVisible == on) {
            return;
        }

        m_popupVisible = on;
        if (m_popupVisible) {
            ensurePopup();
            syncPopupToCurrentIndex_();
            updatePopupGeometry();
            if (m_overlay) {
                m_overlay->show();
                m_overlay->update();
            }
            if (m_list) {
                m_list->show();
                m_list->update();
            }
        } else {
            if (m_list) {
                m_list->hide();
            }
            if (m_overlay) {
                m_overlay->hide();
            }
            m_popupFirstIndex = 0;
            m_popupHighlightedIndex = -1;
        }
        update();
    }

    void ensurePopup() {
        if (m_overlay && m_list) {
            return;
        }
        SwWidget* root = findRootWidget(this);
        if (!root) {
            return;
        }

        if (!m_overlay) {
            m_overlay = new PopupOverlay(this, root, root);
            m_overlay->move(0, 0);
            m_overlay->resize(root->width(), root->height());
        }
        if (!m_list) {
            m_list = new PopupList(this, m_overlay);
        }
        m_overlay->setList(m_list);
    }

    void updatePopupGeometry() {
        if (!m_overlay || !m_list) {
            return;
        }
        SwWidget* root = findRootWidget(this);
        if (!root) {
            return;
        }

        m_overlay->move(0, 0);
        m_overlay->resize(root->width(), root->height());

        const SwRect anchor = getRect();
        const int n = count();
        const int visible = std::min(n, m_maxVisibleItems);
        const int h = m_popupPadding * 2 + visible * m_itemHeight;
        const int w = anchor.width;

        int x = anchor.x;
        int y = anchor.y + anchor.height + 4;

        // Clamp to window bounds.
        const int maxX = std::max(0, root->width() - w - 2);
        const int maxY = std::max(0, root->height() - h - 2);
        x = clampInt(x, 2, maxX);
        y = clampInt(y, 2, maxY);

        m_list->move(x, y);
        m_list->resize(w, h);
    }

    void initDefaults() {
        resize(220, 34);
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setStyleSheet(R"(
            SwComboBox {
                background-color: rgb(255, 255, 255);
                border-color: rgb(172, 172, 172);
                color: rgb(30, 30, 30);
                border-radius: 10px;
                border-width: 1px;
                font-size: 14px;
                padding: 6px 10px;
            }
        )");
    }

    SwVector<SwString> m_items;
    int m_currentIndex{-1};
    bool m_popupVisible{false};
    SwColor m_accent{0, 120, 215};
    int m_maxVisibleItems{8};
    int m_itemHeight{28};
    int m_popupPadding{8};

    int m_popupFirstIndex{0};
    int m_popupHighlightedIndex{-1};

    PopupOverlay* m_overlay{nullptr};
    PopupList* m_list{nullptr};
};
