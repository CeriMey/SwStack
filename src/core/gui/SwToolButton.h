#pragma once

/**
 * @file src/core/gui/SwToolButton.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwToolButton in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the tool button interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwToolButton.
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
#include "graphics/SwFontMetrics.h"

class SwToolButton : public SwWidget {
    SW_OBJECT(SwToolButton, SwWidget)

public:
    /**
     * @brief Constructs a `SwToolButton` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwToolButton(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Constructs a `SwToolButton` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwToolButton(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
        setText(text);
    }

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
        if (!m_checkable && m_checked) {
            setChecked(false);
        }
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
        if (!m_checkable) {
            checked = false;
        }
        if (m_checked == checked) {
            return;
        }
        m_checked = checked;
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

    DECLARE_SIGNAL(clicked, bool);
    DECLARE_SIGNAL(toggled, bool);

protected:
    CUSTOM_PROPERTY(SwString, Text, "ToolButton") { update(); }
    CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

    SwSize sizeHint() const override {
        StyleSheet* sheet = const_cast<SwToolButton*>(this)->getToolSheet();
        const SwFont font = resolvedStyledFont_(sheet);
        const SwFontMetrics metrics(font);
        const SwString label = getText().isEmpty() ? SwString("ToolButton") : getText();
        const SwSize minSize = minimumSize();
        const SwSize maxSize = maximumSize();
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();

        SwSize hint{
            metrics.horizontalAdvance(label) + 24,
            metrics.height() + 16
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
        const int radius = clampInt(std::min(bounds.width, bounds.height) / 5, 4, 8);

        SwColor bg{243, 243, 243};
        SwColor border{173, 173, 173};
        SwColor textColor{18, 18, 18};

        if (!getEnable()) {
            bg = SwColor{244, 244, 244};
            border = SwColor{205, 205, 205};
            textColor = SwColor{122, 122, 122};
        } else if (m_checkable && m_checked && getPressed()) {
            bg = SwColor{0, 84, 153};
            border = SwColor{0, 72, 131};
            textColor = SwColor{255, 255, 255};
        } else if (m_checkable && m_checked && getHover()) {
            bg = SwColor{0, 132, 239};
            border = SwColor{0, 120, 215};
            textColor = SwColor{255, 255, 255};
        } else if (m_checkable && m_checked) {
            bg = SwColor{0, 120, 215};
            border = SwColor{0, 99, 177};
            textColor = SwColor{255, 255, 255};
        } else if (getPressed()) {
            bg = SwColor{204, 228, 247};
            border = SwColor{0, 84, 153};
        } else if (getHover()) {
            bg = SwColor{229, 241, 251};
            border = SwColor{0, 120, 215};
        }

        painter->fillRoundedRect(bounds, radius, bg, border, 1);

        SwRect textRect = bounds;
        textRect.x += 8;
        textRect.width = std::max(0, textRect.width - 16);
        painter->drawText(textRect,
                          getText(),
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
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
        if (!getEnable() || !isPointInside(event->x(), event->y())) {
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

        if (wasPressed && getEnable() && isPointInside(event->x(), event->y())) {
            if (m_checkable) {
                setChecked(!m_checked);
            }
            clicked(m_checkable ? m_checked : false);
            event->accept();
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

private:
    void initDefaults() {
        resize(96, 34);
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFont(SwFont(L"Segoe UI", 9, Medium));
        setStyleSheet(R"(
            SwToolButton {
                background-color: rgb(243, 243, 243);
                border-color: rgb(173, 173, 173);
                border-width: 1px;
                border-radius: 4px;
                color: rgb(18, 18, 18);
                font-size: 13px;
                padding: 6px 10px;
            }
            SwToolButton:hover {
                background-color: rgb(229, 241, 251);
                border-color: rgb(0, 120, 215);
            }
            SwToolButton:pressed {
                background-color: rgb(204, 228, 247);
                border-color: rgb(0, 84, 153);
            }
            SwToolButton:checked {
                background-color: rgb(0, 120, 215);
                border-color: rgb(0, 99, 177);
                color: rgb(255, 255, 255);
            }
        )");
    }

    bool m_checkable{false};
    bool m_checked{false};
    SwColor m_accent{0, 120, 215};
};

