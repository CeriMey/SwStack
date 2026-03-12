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
 * @file src/core/gui/SwComboBox.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwComboBox in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the combo box interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwComboBox.
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
 * SwComboBox - combo box widget.
 *
 * Focus:
 * - Same core API (addItem, count, currentIndex/currentText, signals).
 * - Premium-ish look with a popup list (usable in snapshot via showPopup()).
 **************************************************************************************************/

#include "SwWidget.h"
#include "core/object/SwPointer.h"
#include "core/types/SwVector.h"

#if defined(_WIN32)
#include "core/runtime/SwCoreApplication.h"
#include "platform/win/SwWindows.h"
#endif

class SwComboBox : public SwWidget {
    SW_OBJECT(SwComboBox, SwWidget)

public:
    /**
     * @brief Constructs a `SwComboBox` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwComboBox(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Destroys the `SwComboBox` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwComboBox() {
#if defined(_WIN32)
        if (m_nativePopupHwnd) {
            SetWindowLongPtr(m_nativePopupHwnd, GWLP_USERDATA, 0);
            DestroyWindow(m_nativePopupHwnd);
            m_nativePopupHwnd = nullptr;
        }
#endif
    }

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const { return m_items.size(); }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        m_items.clear();
        setCurrentIndex(-1);
        updatePopupGeometry();
        update();
    }

    /**
     * @brief Adds the specified item.
     * @param text Value passed to the method.
     */
    void addItem(const SwString& text) {
        m_items.push_back(text);
        if (m_currentIndex < 0) {
            setCurrentIndex(0);
        }
        updatePopupGeometry();
        update();
    }

    /**
     * @brief Performs the `insertItem` operation.
     * @param index Value passed to the method.
     * @param text Value passed to the method.
     */
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

    /**
     * @brief Performs the `itemText` operation.
     * @param index Value passed to the method.
     * @return The requested item Text.
     */
    SwString itemText(int index) const {
        if (index < 0 || index >= m_items.size()) {
            return {};
        }
        return m_items[index];
    }

    /**
     * @brief Returns the current current Index.
     * @return The current current Index.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int currentIndex() const { return m_currentIndex; }

    /**
     * @brief Returns the current current Text.
     * @return The current current Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString currentText() const {
        if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
            return {};
        }
        return m_items[m_currentIndex];
    }

    /**
     * @brief Sets the current Index.
     * @param index Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
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

    /**
     * @brief Sets the max Visible Items.
     * @param count Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
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

    /**
     * @brief Returns the current max Visible Items.
     * @return The current max Visible Items.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maxVisibleItems() const { return m_maxVisibleItems; }

    /**
     * @brief Sets the accent Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAccentColor(const SwColor& color) {
        m_accent = clampColor(color);
        update();
    }

    /**
     * @brief Returns the current accent Color.
     * @return The current accent Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor accentColor() const { return m_accent; }

    /**
     * @brief Performs the `showPopup` operation.
     * @param true Value passed to the method.
     */
    void showPopup() { setPopupVisible(true); }
    /**
     * @brief Performs the `hidePopup` operation.
     * @param false Value passed to the method.
     */
    void hidePopup() { setPopupVisible(false); }
    /**
     * @brief Returns whether the object reports popup Visible.
     * @return `true` when the object reports popup Visible; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isPopupVisible() const { return m_popupVisible; }

    DECLARE_SIGNAL(currentIndexChanged, int);
    DECLARE_SIGNAL(currentTextChanged, SwString);
    DECLARE_SIGNAL(activated, int);
    DECLARE_SIGNAL(highlighted, int);

protected:
    CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();

        SwColor bg{255, 255, 255};
        SwColor border{172, 172, 172};
        SwColor textColor{30, 30, 30};
        SwColor arrow{60, 60, 60};

        int borderWidth = 1;
        int radius = 10;
        SwColor focusColor = m_accent;

        const StyleSheet* sheet = getToolSheet();
        auto selectors = classHierarchy(); // most-derived first
        if (!selectors.contains("SwWidget")) {
            selectors.append("SwWidget");
        }

        if (sheet) {
            const SwString bw = getStyleValue_(sheet, selectors, "border-width");
            if (!bw.isEmpty()) {
                borderWidth = clampInt(parsePixelValue(bw, borderWidth), 0, 20);
            }

            const SwString br = getStyleValue_(sheet, selectors, "border-radius");
            if (!br.isEmpty()) {
                radius = clampInt(parsePixelValue(br, radius), 0, 32);
            }

            // Optional focus customization (stylesheet-friendly).
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

        // Match the usual behaviour: focus changes the border color while preserving border-radius.
        if (getFocus() && getEnable()) {
            border = focusColor;
        }

        // Dynamic continuity: when popup is open with square top corners and 0 offset,
        // flatten the combobox bottom corners to create visual continuity.
        if (m_popupVisible) {
            const auto pr = popupBorderRadii_();
            const int gap = popupOffset_();
            int blR = radius, brR = radius;
            if (pr.tl == 0 && gap == 0) { blR = 0; }
            if (pr.tr == 0 && gap == 0) { brR = 0; }
            if (blR != radius || brR != radius) {
                painter->fillRoundedRect(bounds, radius, radius, brR, blR, bg, border, borderWidth);
            } else {
                painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);
            }
        } else {
            painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);
        }

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
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }
        setPressed(true);
        event->accept();
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

        const bool wasPressed = getPressed();
        setPressed(false);

        if (wasPressed && isPointInside(event->x(), event->y())) {
#if defined(_WIN32)
            // Guard: don't reopen if the native popup just closed (e.g. user clicked the combobox
            // while popup was open â†’ WM_ACTIVATE closes it, then this release fires).
            if (GetTickCount64() - m_lastNativePopupCloseMs < 300) {
                event->accept();
                return;
            }
#endif
            setPopupVisible(!m_popupVisible);
            event->accept();
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

            // Simple type-ahead: use ASCII codepoint only (type-ahead matching is ASCII).
            const wchar_t wc = event->text();
            char ch = (wc > 0 && wc < 0x80) ? static_cast<char>(wc) : '\0';
            if (ch == '\0' && !event->isTextProvided()) {
                const bool caps = SwWidgetPlatformAdapter::isCapsLockKey(key);
                SwWidgetPlatformAdapter::translateCharacter(key, event->isShiftPressed(), caps, ch);
            }
            if (ch && ch != '\r' && ch != '\n' && ch != '\t') {
                const int from = (m_popupHighlightedIndex >= 0) ? (m_popupHighlightedIndex + 1) : 0;
                const int found = findNextByFirstChar_(ch, from);
                if (found >= 0) {
                    setPopupHighlightedIndex_(found);
                    event->accept();
                    return;
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
        const wchar_t typedChar = event->text();
        if (typedChar != L'\0' && typedChar <= 0xFF) {
            ch = static_cast<char>(typedChar);
        }
        if (!ch) {
            const bool caps = SwWidgetPlatformAdapter::isCapsLockKey(key);
            SwWidgetPlatformAdapter::translateCharacter(key, event->isShiftPressed(), caps, ch);
        }
        if (ch && ch != '\r' && ch != '\n' && ch != '\t') {
            const int from = (m_currentIndex >= 0) ? (m_currentIndex + 1) : 0;
            const int found = findNextByFirstChar_(ch, from);
            if (found >= 0) {
                setCurrentIndex(found);
                event->accept();
                return;
            }
        }

        SwWidget::keyPressEvent(event);
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
        if (!getEnable()) {
            SwWidget::wheelEvent(event);
            return;
        }
        if (event->isShiftPressed()) {
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

    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updatePopupGeometry();
    }

private:
    class PopupList;

    class PopupOverlay : public SwWidget {
        SW_OBJECT(PopupOverlay, SwWidget)

    public:
        /**
         * @brief Constructs a `PopupOverlay` instance.
         * @param owner Value passed to the method.
         * @param root Value passed to the method.
         * @param parent Optional parent object that owns this instance.
         * @param root Value passed to the method.
         *
         * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
         */
        PopupOverlay(SwComboBox* owner, SwWidget* root, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner)
            , m_root(root) {
            setFocusPolicy(FocusPolicyEnum::NoFocus);
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        /**
         * @brief Sets the list.
         * @param list Value passed to the method.
         *
         * @details Call this method to replace the currently stored value with the caller-provided one.
         */
        void setList(PopupList* list) { m_list = list; }

        /**
         * @brief Handles the mouse Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner) {
                return;
            }

            // If click is outside the list, close the popup.
            if (m_list) {
                const SwRect listRect = m_list->geometry();
                if (!containsPoint(listRect, event->x(), event->y())) {
                    const SwPoint ownerPos = m_owner->mapTo(m_root, SwPoint{0, 0});
                    const SwRect ownerRect{ownerPos.x, ownerPos.y, m_owner->width(), m_owner->height()};
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
                        m_root->dispatchMouseEventFromRoot(forwarded);
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
        /**
         * @brief Constructs a `PopupList` instance.
         * @param owner Value passed to the method.
         * @param parent Optional parent object that owns this instance.
         * @param owner Value passed to the method.
         *
         * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
         */
        explicit PopupList(SwComboBox* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner) {
            setCursor(CursorType::Hand);
            setFocusPolicy(FocusPolicyEnum::NoFocus);
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        /**
         * @brief Handles the paint Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter || !m_owner) {
                return;
            }

            const SwRect bounds = rect();
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

        /**
         * @brief Handles the mouse Move Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
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

        /**
         * @brief Handles the mouse Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
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

        /**
         * @brief Handles the wheel Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void wheelEvent(WheelEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            if (event->isShiftPressed()) {
                SwWidget::wheelEvent(event);
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
            const SwRect bounds = rect();
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

    static bool tryParseColor_(const StyleSheet* sheet, const SwString& value, SwColor& outColor) {
        if (!sheet || value.isEmpty()) {
            return false;
        }
        try {
            outColor = clampColor(const_cast<StyleSheet*>(sheet)->parseColor(value, nullptr));
            return true;
        } catch (...) {
            return false;
        }
    }

    static SwString getStyleValue_(const StyleSheet* sheet,
                                  const SwList<SwString>& hierarchy,
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
            SwString v = sheet->getStyleProperty(selector, propertyName);
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
            setFocus(true);
            syncPopupToCurrentIndex_();
#if defined(_WIN32)
            if (openNativePopup_()) {
                update();
                return;
            }
#endif
            // Fallback: in-widget overlay popup (non-Windows or no native HWND).
            ensurePopup();
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
#if defined(_WIN32)
            closeNativePopup_();
#endif
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
        if (!m_popupVisible) {
            if (auto* p = dynamic_cast<SwWidget*>(parent())) {
                p->update();
            }
        }
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

        const SwPoint anchorPos = mapTo(root, SwPoint{0, 0});
        const SwRect anchor{anchorPos.x, anchorPos.y, width(), height()};
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

    struct PopupRadii { int tl, tr, br, bl; };

    PopupRadii popupBorderRadii_() {
        int defRadius = 10;
        const StyleSheet* sheet = getToolSheet();
        if (!sheet) {
            return {defRadius, defRadius, defRadius, defRadius};
        }
        auto selectors = classHierarchy();
        if (!selectors.contains("SwWidget")) {
            selectors.append("SwWidget");
        }
        // Global border-radius as default.
        const SwString br = getStyleValue_(sheet, selectors, "border-radius");
        if (!br.isEmpty()) {
            defRadius = clampInt(parsePixelValue(br, defRadius), 0, 32);
        }
        // Per-corner overrides (popup-specific or standard CSS names).
        auto readCorner = [&](const char* popupProp, const char* cssProp, int def) -> int {
            SwString v = getStyleValue_(sheet, selectors, popupProp);
            if (v.isEmpty()) {
                v = getStyleValue_(sheet, selectors, cssProp);
            }
            if (v.isEmpty()) {
                return def;
            }
            return clampInt(parsePixelValue(v, def), 0, 32);
        };
        int tl = readCorner("popup-border-top-left-radius", "border-top-left-radius", defRadius);
        int tr = readCorner("popup-border-top-right-radius", "border-top-right-radius", defRadius);
        int brr = readCorner("popup-border-bottom-right-radius", "border-bottom-right-radius", defRadius);
        int bl = readCorner("popup-border-bottom-left-radius", "border-bottom-left-radius", defRadius);
        return {tl, tr, brr, bl};
    }

    int popupOffset_() {
        int offset = 4;
        const StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto selectors = classHierarchy();
            if (!selectors.contains("SwWidget")) {
                selectors.append("SwWidget");
            }
            const SwString v = getStyleValue_(sheet, selectors, "popup-offset");
            if (!v.isEmpty()) {
                offset = clampInt(parsePixelValue(v, offset), 0, 20);
            }
        }
        return offset;
    }

    SwColor popupBorderColor_() {
        const StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto selectors = classHierarchy();
            if (!selectors.contains("SwWidget")) { selectors.append("SwWidget"); }
            SwString v = getStyleValue_(sheet, selectors, "popup-border-color");
            if (!v.isEmpty()) {
                SwColor c{200, 200, 200};
                tryParseColor_(sheet, v, c);
                return c;
            }
        }
        // Fallback: resolved border color of the combobox (accounts for focus/hover/pressed state).
        return resolvedBorderColor_();
    }

    SwColor resolvedBorderColor_() {
        SwColor border{172, 172, 172};
        SwColor focusColor = m_accent;
        const StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto selectors = classHierarchy();
            if (!selectors.contains("SwWidget")) { selectors.append("SwWidget"); }
            SwString v = getStyleValue_(sheet, selectors, "border-color");
            if (!v.isEmpty()) { tryParseColor_(sheet, v, border); }
            SwString fc = getStyleValue_(sheet, selectors, "focus-border-color");
            if (fc.isEmpty()) { fc = getStyleValue_(sheet, selectors, "focus-color"); }
            if (!fc.isEmpty()) { tryParseColor_(sheet, fc, focusColor); }
        }
        if (!getEnable()) {
            border = SwColor{210, 210, 210};
        } else if (getPressed() || m_popupVisible) {
            border = SwColor{140, 140, 140};
        } else if (getHover()) {
            border = SwColor{150, 150, 150};
        }
        if (getFocus() && getEnable()) {
            border = focusColor;
        }
        return border;
    }

    SwColor popupBackgroundColor_() {
        SwColor def{255, 255, 255};
        const StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto selectors = classHierarchy();
            if (!selectors.contains("SwWidget")) { selectors.append("SwWidget"); }
            SwString v = getStyleValue_(sheet, selectors, "popup-background-color");
            if (v.isEmpty()) { v = getStyleValue_(sheet, selectors, "background-color"); }
            if (!v.isEmpty()) { tryParseColor_(sheet, v, def); }
        }
        return def;
    }

    int popupBorderWidth_() {
        int def = 1;
        const StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto selectors = classHierarchy();
            if (!selectors.contains("SwWidget")) { selectors.append("SwWidget"); }
            SwString v = getStyleValue_(sheet, selectors, "popup-border-width");
            if (v.isEmpty()) { v = getStyleValue_(sheet, selectors, "border-width"); }
            if (!v.isEmpty()) { def = clampInt(parsePixelValue(v, def), 0, 10); }
        }
        return def;
    }

    // â”€â”€ Native popup window (Windows) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#if defined(_WIN32)
    static constexpr const wchar_t* popupClassName_() { return L"SwComboBoxPopup"; }

    static void postGuardedPopupAction_(SwComboBox* comboBox, std::function<void(SwComboBox*)> fn) {
        if (!comboBox || !fn) {
            return;
        }
        if (auto* app = SwCoreApplication::instance(false)) {
            const SwPointer<SwComboBox> self(comboBox);
            std::function<void(SwComboBox*)> fnCopy = fn;
            app->postEvent([self, fnCopy]() mutable {
                if (!self) {
                    return;
                }
                SwComboBox* liveSelf = self.data();
                if (!SwObject::isLive(liveSelf)) {
                    return;
                }
                fnCopy(liveSelf);
            });
            return;
        }
        fn(comboBox);
    }

    static void ensurePopupClassRegistered_() {
        static bool done = false;
        if (done) {
            return;
        }
        done = true;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = popupWndProc_;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = popupClassName_();
        wc.style = CS_DROPSHADOW;
        RegisterClassExW(&wc);
    }

    static LRESULT CALLBACK popupWndProc_(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<SwComboBox*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<SwComboBox*>(cs ? cs->lpCreateParams : nullptr);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        if (!self) {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            self->nativePopupPaint_(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            self->nativePopupMouseMove_(static_cast<short>(LOWORD(lp)),
                                        static_cast<short>(HIWORD(lp)));
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int idx = self->nativePopupIndexAt_(static_cast<short>(HIWORD(lp)));
            if (idx >= 0 && idx < self->count()) {
                self->m_popupHighlightedIndex = idx;
                // Post to avoid DestroyWindow from inside WndProc.
                postGuardedPopupAction_(self, [](SwComboBox* liveSelf) { liveSelf->commitPopupHighlighted_(); });
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            const int steps = wheelSteps_(delta);
            if (steps != 0) {
                self->movePopupHighlightBy_(-steps);
                if (self->m_nativePopupHwnd) {
                    InvalidateRect(self->m_nativePopupHwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const int key = static_cast<int>(wp);
            if (key == VK_ESCAPE) {
                postGuardedPopupAction_(self, [](SwComboBox* liveSelf) { liveSelf->hidePopup(); });
                return 0;
            }
            if (key == VK_RETURN) {
                postGuardedPopupAction_(self, [](SwComboBox* liveSelf) { liveSelf->commitPopupHighlighted_(); });
                return 0;
            }
            if (key == VK_UP) {
                self->movePopupHighlightBy_(-1);
                if (self->m_nativePopupHwnd) InvalidateRect(self->m_nativePopupHwnd, nullptr, FALSE);
                return 0;
            }
            if (key == VK_DOWN) {
                self->movePopupHighlightBy_(+1);
                if (self->m_nativePopupHwnd) InvalidateRect(self->m_nativePopupHwnd, nullptr, FALSE);
                return 0;
            }
            if (key == VK_HOME && self->count() > 0) {
                self->setPopupHighlightedIndex_(0);
                if (self->m_nativePopupHwnd) InvalidateRect(self->m_nativePopupHwnd, nullptr, FALSE);
                return 0;
            }
            if (key == VK_END && self->count() > 0) {
                self->setPopupHighlightedIndex_(self->count() - 1);
                if (self->m_nativePopupHwnd) InvalidateRect(self->m_nativePopupHwnd, nullptr, FALSE);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_ACTIVATE: {
            if (LOWORD(wp) == WA_INACTIVE) {
                self->m_lastNativePopupCloseMs = GetTickCount64();
                postGuardedPopupAction_(self, [](SwComboBox* liveSelf) { liveSelf->hidePopup(); });
            }
            return 0;
        }
        case WM_NCDESTROY: {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    void nativePopupPaint_(HDC hdc) {
        if (!hdc || !m_nativePopupHwnd) {
            return;
        }
        RECT cr;
        GetClientRect(m_nativePopupHwnd, &cr);
        const int cw = cr.right - cr.left;
        const int ch = cr.bottom - cr.top;
        if (cw <= 0 || ch <= 0) {
            return;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp || !guiApp->platformIntegration()) {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            return;
        }

        SwScopedPlatformPainter painter(guiApp->platformIntegration(),
                                        SwMakePlatformPaintEvent(SwPlatformSize{cw, ch},
                                                                 memDC,
                                                                 m_nativePopupHwnd,
                                                                 nullptr,
                                                                 SwPlatformRect{0, 0, cw, ch}));
        if (!painter) {
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            return;
        }

        painter->clear(SwColor{0, 0, 0});
        const int bw = m_nativePopupBorderWidth;
        SwRect bounds{0, 0, cw, ch};
        const auto& rr = m_nativePopupRadii;
        painter->fillRoundedRect(bounds, rr.tl, rr.tr, rr.br, rr.bl,
                                 m_nativePopupBg, m_nativePopupBorderColor, bw);

        const int n = count();
        const int visible = std::min(n, m_maxVisibleItems);
        const int itemH = m_itemHeight;
        const int pad = m_popupPadding;

        for (int i = 0; i < visible; ++i) {
            const int index = m_popupFirstIndex + i;
            if (index < 0 || index >= n) {
                continue;
            }
            SwRect row{pad, pad + i * itemH, std::max(0, cw - 2 * pad), itemH};

            const bool hovered = (index == m_popupHighlightedIndex);
            const bool selected = (index == m_currentIndex);

            if (hovered) {
                painter->fillRoundedRect(row, 8, SwColor{244, 246, 250}, SwColor{244, 246, 250}, 0);
            }

            const bool hasSelection = (m_currentIndex >= 0);
            const int textPadL = hasSelection ? 16 : 8;
            SwColor textColor = selected ? SwColor{24, 28, 36} : SwColor{30, 30, 30};
            SwRect textRect{row.x + textPadL, row.y, std::max(0, row.width - textPadL), row.height};
            painter->drawText(textRect, itemText(index),
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor, getFont());

            if (selected) {
                SwRect pill{row.x + 4, row.y + 6, 3, std::max(0, row.height - 12)};
                painter->fillRoundedRect(pill, 2, m_accent, m_accent, 0);
            }
        }
        painter->finalize();
        painter->flush();

        BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
    }

    int nativePopupIndexAt_(int y) const {
        const int pad = m_popupPadding;
        if (y < pad) {
            return -1;
        }
        const int idx = (y - pad) / std::max(1, m_itemHeight);
        const int visible = std::min(count(), m_maxVisibleItems);
        if (idx < 0 || idx >= visible) {
            return -1;
        }
        const int absolute = m_popupFirstIndex + idx;
        return (absolute >= 0 && absolute < count()) ? absolute : -1;
    }

    void nativePopupMouseMove_(int /*x*/, int y) {
        const int idx = nativePopupIndexAt_(y);
        if (idx >= 0 && idx != m_popupHighlightedIndex) {
            m_popupHighlightedIndex = idx;
            ensurePopupHighlightVisible_();
            highlighted(m_popupHighlightedIndex);
            if (m_nativePopupHwnd) {
                InvalidateRect(m_nativePopupHwnd, nullptr, FALSE);
            }
        }
    }

    bool openNativePopup_() {
        HWND parentHwnd = SwWidgetPlatformAdapter::nativeHandleAs<HWND>(platformHandle());
        if (!parentHwnd) {
            return false;
        }
        ensurePopupClassRegistered_();

        const SwRect anchor{0, 0, width(), height()};
        const SwPoint popupOrigin = mapToGlobal(SwPoint{0, height()});
        POINT screenPt = {static_cast<LONG>(popupOrigin.x),
                          static_cast<LONG>(popupOrigin.y)};

        const int gap = popupOffset_();
        const int n = count();
        const int visible = std::min(n, m_maxVisibleItems);
        const int popH = m_popupPadding * 2 + visible * m_itemHeight;
        const int popW = anchor.width;

        int popY = screenPt.y + gap;
        // Open upward if not enough space below.
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (popY + popH > screenH) {
            const SwPoint topOrigin = mapToGlobal(SwPoint{0, 0});
            POINT topPt = {static_cast<LONG>(topOrigin.x), static_cast<LONG>(topOrigin.y)};
            popY = topPt.y - popH - gap;
            if (popY < 0) {
                popY = 0;
            }
        }

        m_nativePopupHwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            popupClassName_(),
            L"",
            WS_POPUP,
            screenPt.x, popY, popW, popH,
            parentHwnd,
            nullptr,
            GetModuleHandle(nullptr),
            this);

        if (!m_nativePopupHwnd) {
            return false;
        }

        // Read popup style from stylesheet.
        m_nativePopupRadii = popupBorderRadii_();
        m_nativePopupBorderColor = popupBorderColor_();
        m_nativePopupBg = popupBackgroundColor_();
        m_nativePopupBorderWidth = popupBorderWidth_();
        const int maxR = std::max({m_nativePopupRadii.tl, m_nativePopupRadii.tr,
                                   m_nativePopupRadii.br, m_nativePopupRadii.bl});
        if (maxR > 0) {
            // Build a per-corner region using GDI path.
            const auto& rr = m_nativePopupRadii;
            HDC tmpDC = GetDC(nullptr);
            Gdiplus::Graphics gfx(tmpDC);
            Gdiplus::GraphicsPath path;
            Gdiplus::REAL x = 0, y = 0;
            Gdiplus::REAL w = static_cast<Gdiplus::REAL>(popW);
            Gdiplus::REAL h = static_cast<Gdiplus::REAL>(popH);
            path.StartFigure();
            if (rr.tl > 0) { Gdiplus::REAL d = static_cast<Gdiplus::REAL>(rr.tl * 2); path.AddArc(x, y, d, d, 180.f, 90.f); }
            else { path.AddLine(x, y, x, y); }
            if (rr.tr > 0) { Gdiplus::REAL d = static_cast<Gdiplus::REAL>(rr.tr * 2); path.AddArc(w - d, y, d, d, 270.f, 90.f); }
            else { path.AddLine(w, y, w, y); }
            if (rr.br > 0) { Gdiplus::REAL d = static_cast<Gdiplus::REAL>(rr.br * 2); path.AddArc(w - d, h - d, d, d, 0.f, 90.f); }
            else { path.AddLine(w, h, w, h); }
            if (rr.bl > 0) { Gdiplus::REAL d = static_cast<Gdiplus::REAL>(rr.bl * 2); path.AddArc(x, h - d, d, d, 90.f, 90.f); }
            else { path.AddLine(x, h, x, h); }
            path.CloseFigure();
            Gdiplus::Region gdipRegion(&path);
            HRGN hRgn = gdipRegion.GetHRGN(&gfx);
            if (hRgn) {
                SetWindowRgn(m_nativePopupHwnd, hRgn, TRUE);
            }
            ReleaseDC(nullptr, tmpDC);
        }

        ShowWindow(m_nativePopupHwnd, SW_SHOW);
        UpdateWindow(m_nativePopupHwnd);
        SetFocus(m_nativePopupHwnd);
        return true;
    }

    void closeNativePopup_() {
        if (m_nativePopupHwnd) {
            HWND h = m_nativePopupHwnd;
            m_nativePopupHwnd = nullptr;
            SetWindowLongPtr(h, GWLP_USERDATA, 0);
            DestroyWindow(h);
        }
    }
#endif

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
                popup-border-top-left-radius: 0px;
                popup-border-top-right-radius: 0px;
                popup-border-bottom-left-radius: 10px;
                popup-border-bottom-right-radius: 10px;
                popup-offset: 0px;
                popup-background-color: rgb(255, 255, 255);
                popup-border-width: 1px;
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

#if defined(_WIN32)
    HWND m_nativePopupHwnd{nullptr};
    ULONGLONG m_lastNativePopupCloseMs{0};
    PopupRadii m_nativePopupRadii{10, 10, 10, 10};
    SwColor m_nativePopupBorderColor{200, 200, 200};
    SwColor m_nativePopupBg{255, 255, 255};
    int m_nativePopupBorderWidth{1};
#endif
};

