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
 * @file src/core/gui/SwGroupBox.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwGroupBox in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the group box interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwGroupBox.
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
 * SwGroupBox - group box.
 *
 * Notes:
 * - Optional checkable header.
 * - Does not automatically manage child layout margins; use a layout margin if needed.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwWidgetPlatformAdapter.h"

class SwGroupBox : public SwFrame {
    SW_OBJECT(SwGroupBox, SwFrame)

public:
    /**
     * @brief Constructs a `SwGroupBox` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGroupBox(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
    }

    /**
     * @brief Constructs a `SwGroupBox` instance.
     * @param title Title text applied by the operation.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGroupBox(const SwString& title, SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        setTitle(title);
    }

    /**
     * @brief Sets the title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitle(const SwString& title) { setText(title); }
    /**
     * @brief Performs the `title` operation.
     * @return The requested title.
     */
    SwString title() const { return getText(); }

    /**
     * @brief Sets the checkable.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCheckable(bool on) {
        if (m_checkable == on) {
            return;
        }
        m_checkable = on;
        updateEnabledState();
        update();
    }

    /**
     * @brief Returns whether the object reports checkable.
     * @return `true` when the object reports checkable; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isCheckable() const { return m_checkable; }

    /**
     * @brief Sets the checked.
     * @param checked Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setChecked(bool checked) {
        if (m_checked == checked) {
            return;
        }
        m_checked = checked;
        updateEnabledState();
        toggled(m_checked);
        update();
    }

    /**
     * @brief Returns whether the object reports checked.
     * @return `true` when the object reports checked; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isChecked() const { return m_checked; }

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
     * @brief Returns the current contents Rect.
     * @return The current contents Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRect contentsRect() const {
        const SwRect r = rect();
        const int top = r.y + titleHeight();
        return SwRect{r.x + m_padding,
                      top + m_padding,
                      std::max(0, r.width - 2 * m_padding),
                      std::max(0, r.height - (top - r.y) - 2 * m_padding)};
    }

    DECLARE_SIGNAL(toggled, bool);

protected:
    CUSTOM_PROPERTY(SwString, Text, "GroupBox") { update(); }
    CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

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

        const SwRect rect = this->rect();

        StyleSheet* sheet = getToolSheet();
        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRoundedRect(rect, radius, bg, border, borderWidth);
        } else {
            painter->drawRect(rect, border, borderWidth);
        }

        // Header
        const int headerH = titleHeight();
        const int headerY = rect.y;
        const int headerX = rect.x + m_padding;

        SwRect indicator = checkIndicatorRect(rect);
        SwRect titleRect = titleTextRect(rect);

        // Break the frame line behind the title for a cleaner look.
        if (paintBackground && bgAlpha > 0.0f) {
            SwRect patch = titleRect;
            patch.x -= 4;
            patch.width += 8;
            painter->fillRect(patch, bg, bg, 0);
            if (m_checkable) {
                SwRect patch2 = indicator;
                patch2.x -= 4;
                patch2.y -= 2;
                patch2.width += 8;
                patch2.height += 4;
                painter->fillRect(patch2, bg, bg, 0);
            }
        }

        if (m_checkable) {
            paintCheckIndicator(painter, indicator);
        }

        SwColor titleColor{24, 28, 36};
        if (!getEnable() || (m_checkable && !m_checked)) {
            titleColor = SwColor{150, 150, 150};
        }

        painter->drawText(titleRect,
                          getText(),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          titleColor,
                          getFont());

        // Children, clipped to the inside rect for a clean group box.
        const SwRect clip = SwRect{rect.x + 1, rect.y + headerH, rect.width - 2, rect.height - headerH - 1};
        painter->pushClipRect(clip);
        paintChildren(event);
        painter->popClipRect();

        painter->finalize();
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
            SwFrame::mousePressEvent(event);
            return;
        }

        if (m_checkable) {
            const SwRect rect = this->rect();
            const SwRect indicator = checkIndicatorRect(rect);
            const SwRect titleRect = titleTextRect(rect);
            if (containsPoint(indicator, event->x(), event->y()) ||
                containsPoint(titleRect, event->x(), event->y())) {
                setPressed(true);
                event->accept();
                return;
            }
        }

        SwFrame::mousePressEvent(event);
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

        if (wasPressed && m_checkable) {
            const SwRect rect = this->rect();
            const SwRect indicator = checkIndicatorRect(rect);
            const SwRect titleRect = titleTextRect(rect);
            if (containsPoint(indicator, event->x(), event->y()) ||
                containsPoint(titleRect, event->x(), event->y())) {
                setChecked(!m_checked);
                event->accept();
                return;
            }
        }

        SwFrame::mouseReleaseEvent(event);
    }

private:
    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    int titleHeight() const {
        return clampInt(m_headerHeight, 18, 32);
    }

    int titleTextWidthEstimate(const SwString& text) const {
        const int fallbackWidth = 8;
        return SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), text, getFont(), text.size(), fallbackWidth);
    }

    SwRect checkIndicatorRect(const SwRect& rect) const {
        if (!m_checkable) {
            return SwRect{0, 0, 0, 0};
        }
        const int s = m_checkSize;
        const int y = rect.y + (titleHeight() - s) / 2;
        return SwRect{rect.x + m_padding, y, s, s};
    }

    SwRect titleTextRect(const SwRect& rect) const {
        const int headerH = titleHeight();
        const int y = rect.y;
        int x = rect.x + m_padding;
        if (m_checkable) {
            x += m_checkSize + m_checkSpacing;
        }
        const int textW = clampInt(titleTextWidthEstimate(getText()) + 6, 30, rect.width - 2 * m_padding);
        return SwRect{x, y, textW, headerH};
    }

    void paintCheckIndicator(SwPainter* painter, const SwRect& indicator) const {
        if (!painter) {
            return;
        }

        SwColor border{160, 160, 160};
        SwColor fill{255, 255, 255};
        if (!getEnable()) {
            border = SwColor{190, 190, 190};
            fill = SwColor{245, 245, 245};
        } else if (getHover()) {
            border = SwColor{120, 120, 120};
        }

        painter->fillRoundedRect(indicator, 4, fill, border, 1);

        if (m_checked) {
            SwRect inner{indicator.x + 2, indicator.y + 2, indicator.width - 4, indicator.height - 4};
            painter->fillRoundedRect(inner, 3, m_accent, m_accent, 0);

            const SwColor tick{255, 255, 255};
            const int x1 = indicator.x + indicator.width / 4;
            const int y1 = indicator.y + indicator.height / 2;
            const int x2 = indicator.x + indicator.width / 2 - 1;
            const int y2 = indicator.y + indicator.height - indicator.height / 4;
            const int x3 = indicator.x + indicator.width - indicator.width / 4;
            const int y3 = indicator.y + indicator.height / 4;
            painter->drawLine(x1, y1, x2, y2, tick, 2);
            painter->drawLine(x2, y2, x3, y3, tick, 2);
        }
    }

    void updateEnabledState() {
        if (!m_checkable) {
            return;
        }
        for (SwWidget* child : findChildren<SwWidget>()) {
            if (!child || child == this) {
                continue;
            }
            child->setEnable(m_checked);
        }
    }

    void initDefaults() {
        setCursor(CursorType::Arrow);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setFrameShape(Shape::Box);
        setStyleSheet(R"(
            SwGroupBox {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
            }
        )");
    }

    bool m_checkable{false};
    bool m_checked{true};
    SwColor m_accent{0, 120, 215};
    int m_headerHeight{24};
    int m_padding{12};
    int m_checkSize{16};
    int m_checkSpacing{8};
};

