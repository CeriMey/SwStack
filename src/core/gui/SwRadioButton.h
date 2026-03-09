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
 * @file src/core/gui/SwRadioButton.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwRadioButton in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the radio button interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwRadioButton.
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
 * SwRadioButton - radio button widget.
 *
 * Notes:
 * - Auto-exclusive behaviour is implemented at the parent level (siblings under the same parent).
 **************************************************************************************************/

#include "SwWidget.h"

class SwRadioButton : public SwWidget {
    SW_OBJECT(SwRadioButton, SwWidget)

public:
    /**
     * @brief Constructs a `SwRadioButton` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwRadioButton(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Constructs a `SwRadioButton` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwRadioButton(const SwString& text, SwWidget* parent = nullptr)
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
     * @brief Sets the auto Exclusive.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAutoExclusive(bool on) { m_autoExclusive = on; }
    /**
     * @brief Returns the current auto Exclusive.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool autoExclusive() const { return m_autoExclusive; }

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
        if (m_checked && m_autoExclusive) {
            uncheckSiblings();
        }
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
     * @brief Performs the `text` operation.
     * @return The requested text.
     */
    SwString text() const { return getText(); }

    DECLARE_SIGNAL(toggled, bool);
    DECLARE_SIGNAL(clicked, bool);

protected:
    CUSTOM_PROPERTY(SwString, Text, "RadioButton") {
        update();
    }

    CUSTOM_PROPERTY(bool, Pressed, false) {
        update();
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

        if (!getEnable()) {
            border = SwColor{190, 190, 190};
            indicatorFill = SwColor{245, 245, 245};
            textColor = SwColor{150, 150, 150};
        } else if (getHover()) {
            border = SwColor{120, 120, 120};
        }

        painter->fillRoundedRect(indicator, indicatorSize / 2, indicatorFill, border, 1);

        if (m_checked) {
            const int dotSize = clampInt(indicatorSize / 2, 6, indicatorSize - 6);
            const int dotX = indicator.x + (indicator.width - dotSize) / 2;
            const int dotY = indicator.y + (indicator.height - dotSize) / 2;
            const SwRect dot{dotX, dotY, dotSize, dotSize};
            painter->fillRoundedRect(dot, dotSize / 2, m_accent, m_accent, 0);
        }

        painter->drawText(labelRect,
                          getText(),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          getFont());

        if (getFocus()) {
            SwRect focusRect{bounds.x, bounds.y, bounds.width - 1, bounds.height - 1};
            painter->drawRect(focusRect, m_accent, 1);
        }
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
            // Radio buttons typically only go to "checked" on click.
            if (!m_checked) {
                setChecked(true);
            }
            clicked(m_checked);
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
            SwRadioButton {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(30, 30, 30);
                font-size: 14px;
            }
        )");
    }

    void uncheckSiblings() {
        SwWidget* parentWidget = dynamic_cast<SwWidget*>(parent());
        if (!parentWidget) {
            return;
        }

        const auto& siblings = parentWidget->children();
        for (SwObject* child : siblings) {
            if (!child || child == this) {
                continue;
            }
            SwRadioButton* radio = dynamic_cast<SwRadioButton*>(child);
            if (!radio) {
                continue;
            }
            if (radio->autoExclusive() && radio->isChecked()) {
                radio->setChecked(false);
            }
        }
    }

    bool m_checked{false};
    bool m_autoExclusive{true};
    SwColor m_accent{0, 120, 215};
    int m_indicatorSize{18};
    int m_textSpacing{10};
};

