
/**
 * @file
 * @ingroup core_gui
 * @brief Declares `SwPushButton`, the clickable command button widget.
 *
 * The widget layers button-specific text, checked state, and press handling on top of the
 * generic widget base. It is the primary action trigger used by dialogs and forms, with
 * optional checkable behavior for toggle-style commands.
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

#pragma once



#include "SwWidget.h"
#include "graphics/SwFontMetrics.h"
#include <iostream>


class SwPushButton : public SwWidget {

    SW_OBJECT(SwPushButton, SwWidget)

    CUSTOM_PROPERTY(SwString, Text, "PushButton") {
        update();
    }
    CUSTOM_PROPERTY(bool, Pressed, false) {
        update();
    }
    CUSTOM_PROPERTY(bool, Checkable, false) {
        if (!value) {
            setChecked(false);
        }
        update();
    }
    CUSTOM_PROPERTY(DrawTextFormats, Alignment, DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine)) {
        update();
    }
public:
    /**
     * @brief Constructs a `SwPushButton` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwPushButton(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent){
        resize(150, 50);

        setText(text);

        SwString css = R"(
            SwPushButton {
                background-color: rgb(243, 243, 243);
                border-color: rgb(173, 173, 173);
                color: rgb(18, 18, 18);
                border-radius: 4px;
                padding: 8px 14px;
                border-width: 1px;
                font-size: 14px;
            }
            SwPushButton:hover {
                background-color: rgb(229, 241, 251);
                border-color: rgb(0, 120, 215);
            }
            SwPushButton:pressed {
                background-color: rgb(204, 228, 247);
                border-color: rgb(0, 84, 153);
                color: rgb(18, 18, 18);
            }
            SwPushButton:checked {
                background-color: rgb(214, 231, 248);
                border-color: rgb(0, 120, 215);
                color: rgb(18, 18, 18);
            }
            SwPushButton:disabled {
                background-color: rgb(244, 244, 244);
                border-color: rgb(205, 205, 205);
                color: rgb(122, 122, 122);
            }
        )";
        setDefaultStyleSheet(css);
    }

    /**
     * @brief Returns whether the object reports checkable.
     * @return `true` when the object reports checkable; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isCheckable() const { return getCheckable(); }

    /**
     * @brief Sets the checked.
     * @param checked Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setChecked(bool checked) {
        if (!getCheckable()) {
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

    /**
     * @brief Performs the `toggle` operation.
     * @param m_checked Value passed to the method.
     */
    void toggle() { setChecked(!m_checked); }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested paint Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = this->rect();

        WidgetState state = WidgetState::Normal;
        if (getPressed()) {
            state = WidgetStateHelper::setState(state, WidgetState::Pressed);
        }
        if (getHover()) {
            state = WidgetStateHelper::setState(state, WidgetState::Hovered);
        }
        if (getCheckable() && m_checked) {
            state = WidgetStateHelper::setState(state, WidgetState::Checked);
        }
        if (!getEnable()) {
            state = WidgetStateHelper::setState(state, WidgetState::Disabled);
        }
        if (getFocus()) {
            state = WidgetStateHelper::setState(state, WidgetState::Focused);
        }

        m_style->drawControl(WidgetStyle::PushButtonStyle, rect, painter, this, state);

    }




    // GÃ©rer le survol de la souris
    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Move Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseMoveEvent(MouseEvent* event) override {
        SwWidget::mouseMoveEvent(event);
    }

    // GÃ©rer le clic sur le bouton
    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mousePressEvent(MouseEvent* event) override {
        if (isPointInside(event->x(), event->y())) {
            setPressed(true);
            event->accept();
        }
        SwWidget::mousePressEvent(event);
    }

    // GÃ©rer le relÃ¢chement du bouton
    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseReleaseEvent(MouseEvent* event) override {
        if (getPressed() && isPointInside(event->x(), event->y())) {
            emit clicked();  // Ã‰mettre le signal 'clicked'
            event->accept();
            if (getCheckable()) {
                setChecked(!m_checked);
            }
        }
        setPressed(false);
        SwWidget::mouseReleaseEvent(event);
    }

    /**
     * @brief Returns the current size Hint.
     * @return The current size Hint.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwSize sizeHint() const override {
        StyleSheet* sheet = const_cast<SwPushButton*>(this)->getToolSheet();
        const SwFont font = resolvedStyledFont_(sheet);
        const SwFontMetrics metrics(font);
        const StyleSheet::BoxEdges padding = resolvePaddingEdges_(sheet);

        SwColor borderColor{0, 0, 0};
        int borderWidth = 1;
        int borderRadius = 0;
        resolveBorder(sheet, borderColor, borderWidth, borderRadius);

        const SwSize minSize = minimumSize();
        const SwSize maxSize = maximumSize();
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();

        SwSize hint{
            metrics.horizontalAdvance(getText()) + padding.left + padding.right + (borderWidth * 2),
            metrics.height() + padding.top + padding.bottom + (borderWidth * 2)
        };
        hint.width = std::max(hint.width, std::max(minSize.width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minSize.height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maxSize.width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maxSize.height, styleMax.height));
        return hint;
    }

    /**
     * @brief Returns the current minimum Size Hint.
     * @return The current minimum Size Hint.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwSize minimumSizeHint() const override {
        return sizeHint();
    }

signals:
    DECLARE_SIGNAL_VOID(clicked);
    DECLARE_SIGNAL(toggled, bool);

private:
    bool m_checked{false};

};

