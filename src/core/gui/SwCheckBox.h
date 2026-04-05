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
 * @file src/core/gui/SwCheckBox.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwCheckBox in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the check box interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwCheckBox.
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
 * SwCheckBox - checkbox widget.
 *
 * API goals:
 * - Familiar naming (setChecked/isChecked, setText/text, stateChanged/toggled/clicked).
 * - Minimal dependencies (prefer Sw* types, avoid std:: in widget code).
 **************************************************************************************************/

#include "SwWidget.h"
#include "graphics/SwFontMetrics.h"

class SwCheckBox : public SwWidget {
    SW_OBJECT(SwCheckBox, SwWidget)

public:
    enum CheckState {
        Unchecked = 0,
        PartiallyChecked = 1,
        Checked = 2
    };

    /**
     * @brief Constructs a `SwCheckBox` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwCheckBox(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Constructs a `SwCheckBox` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwCheckBox(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
        setText(text);
    }

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
     * @brief Sets the tristate.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTristate(bool on) {
        if (m_tristate == on) {
            return;
        }
        m_tristate = on;
        if (!m_tristate && m_state == PartiallyChecked) {
            setCheckState(Unchecked);
        }
        update();
    }

    /**
     * @brief Returns whether the object reports tristate.
     * @return `true` when the object reports tristate; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isTristate() const { return m_tristate; }

    /**
     * @brief Sets the check State.
     * @param state Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCheckState(CheckState state) {
        if (state != Unchecked && state != PartiallyChecked && state != Checked) {
            state = Unchecked;
        }

        if (m_state == state) {
            return;
        }

        const bool wasChecked = (m_state == Checked);
        m_state = state;

        stateChanged(static_cast<int>(m_state));
        const bool nowChecked = (m_state == Checked);
        if (wasChecked != nowChecked) {
            toggled(nowChecked);
        }
        update();
    }

    /**
     * @brief Returns the current check State.
     * @return The current check State.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    CheckState checkState() const { return m_state; }

    /**
     * @brief Sets the checked.
     * @param Unchecked Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setChecked(bool checked) { setCheckState(checked ? Checked : Unchecked); }
    /**
     * @brief Returns whether the object reports checked.
     * @return `true` when the object reports checked; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isChecked() const { return m_state == Checked; }

    /**
     * @brief Performs the `toggle` operation.
     */
    void toggle() {
        if (m_tristate) {
            switch (m_state) {
            case Unchecked: setCheckState(PartiallyChecked); break;
            case PartiallyChecked: setCheckState(Checked); break;
            default: setCheckState(Unchecked); break;
            }
            return;
        }
        setCheckState(isChecked() ? Unchecked : Checked);
    }

    /**
     * @brief Performs the `text` operation.
     * @return The requested text.
     */
    SwString text() const { return getText(); }

    DECLARE_SIGNAL(stateChanged, int);
    DECLARE_SIGNAL(toggled, bool);
    DECLARE_SIGNAL(clicked, bool);

protected:
    CUSTOM_PROPERTY(SwString, Text, "CheckBox") {
        update();
    }

    CUSTOM_PROPERTY(bool, Pressed, false) {
        update();
    }

    SwSize sizeHint() const override {
        StyleSheet* sheet = const_cast<SwCheckBox*>(this)->getToolSheet();
        const SwFont font = resolvedStyledFont_(sheet);
        const SwFontMetrics metrics(font);
        const SwString label = getText().isEmpty() ? SwString("CheckBox") : getText();
        const SwSize minSize = minimumSize();
        const SwSize maxSize = maximumSize();
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        const int indicatorSize = clampInt(m_indicatorSize, 12, 28);

        SwSize hint{
            indicatorSize + m_textSpacing + metrics.horizontalAdvance(label),
            std::max(indicatorSize, metrics.height())
        };
        hint.width = std::max(hint.width, std::max(minSize.width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minSize.height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maxSize.width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maxSize.height, styleMax.height));
        return hint;
    }

    SwSize minimumSizeHint() const override {
        return sizeHint();
    }

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
        const int indicatorSize = clampInt(m_indicatorSize, 12, 28);
        const int indicatorRadius = clampInt(indicatorSize / 4, 2, 8);
        const int indicatorX = bounds.x;
        const int indicatorY = bounds.y + (bounds.height - indicatorSize) / 2;

        const SwRect indicator{indicatorX, indicatorY, indicatorSize, indicatorSize};
        const SwRect labelRect{indicator.x + indicator.width + m_textSpacing,
                               bounds.y,
                               clampInt(bounds.width - (indicator.width + m_textSpacing), 0, 1000000),
                               bounds.height};

        SwColor border{160, 160, 160};
        SwColor indicatorFill{255, 255, 255};
        SwColor textColor{30, 30, 30};

        // Read colors from stylesheet if available
        StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto tryParse = [&](const char* prop, SwColor& out) {
                SwString v = sheet->getStyleProperty("SwCheckBox", prop);
                if (v.isEmpty()) v = sheet->getStyleProperty("SwWidget", prop);
                if (!v.isEmpty()) {
                    try { out = sheet->parseColor(v, nullptr); } catch (...) {}
                }
            };
            tryParse("background-color-unchecked", indicatorFill);
            tryParse("border-color-unchecked", border);
            tryParse("color", textColor);
        }

        if (!getEnable()) {
            bool isDark = (indicatorFill.r + indicatorFill.g + indicatorFill.b) < 384;
            indicatorFill = isDark ? SwColor{50, 50, 50} : SwColor{245, 245, 245};
            border = isDark ? SwColor{55, 55, 55} : SwColor{190, 190, 190};
            textColor = SwColor{150, 150, 150};
        } else if (getHover()) {
            bool isDark = (border.r + border.g + border.b) < 384;
            border = isDark ? SwColor{100, 100, 100} : SwColor{120, 120, 120};
        }

        painter->fillRoundedRect(indicator, indicatorRadius, indicatorFill, border, 1);

        if (m_state == Checked) {
            SwRect fillRect{indicator.x + 2,
                            indicator.y + 2,
                            clampInt(indicator.width - 4, 0, 1000000),
                            clampInt(indicator.height - 4, 0, 1000000)};
            painter->fillRoundedRect(fillRect, clampInt(indicatorRadius - 1, 1, 8), m_accent, m_accent, 0);

            const SwColor tickColor{255, 255, 255};
            const int x1 = indicator.x + indicator.width / 4;
            const int y1 = indicator.y + indicator.height / 2;
            const int x2 = indicator.x + indicator.width / 2 - 1;
            const int y2 = indicator.y + indicator.height - indicator.height / 4;
            const int x3 = indicator.x + indicator.width - indicator.width / 4;
            const int y3 = indicator.y + indicator.height / 4;
            painter->drawLine(x1, y1, x2, y2, tickColor, 2);
            painter->drawLine(x2, y2, x3, y3, tickColor, 2);
        } else if (m_state == PartiallyChecked) {
            const int barHeight = 3;
            SwRect bar{indicator.x + indicator.width / 5,
                       indicator.y + (indicator.height - barHeight) / 2,
                       clampInt(indicator.width - 2 * (indicator.width / 5), 0, 1000000),
                       barHeight};
            painter->fillRoundedRect(bar, 1, m_accent, m_accent, 0);
        }

        painter->drawText(labelRect,
                          getText(),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          getFont());
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
            toggle();
            clicked(isChecked());
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
        SwWidget::mouseMoveEvent(event);
    }

private:
    void initDefaults() {
        resize(200, 28);
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setStyleSheet(R"(
            SwCheckBox {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(30, 30, 30);
                font-size: 14px;
            }
        )");
    }

    CheckState m_state{Unchecked};
    bool m_tristate{false};
    SwColor m_accent{0, 120, 215};
    int m_indicatorSize{18};
    int m_textSpacing{10};
};
