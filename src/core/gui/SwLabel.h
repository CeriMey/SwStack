#pragma once

/**
 * @file
 * @ingroup core_gui
 * @brief Declares `SwLabel`, the basic text and image display widget.
 *
 * `SwLabel` is the lightweight presentation widget used for static captions, status text,
 * and simple rich display content inside layouts and dialogs. It focuses on rendering and
 * size hinting rather than editing, making it the passive text primitive of the GUI stack.
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
#include "SwString.h"

class SwLabel : public SwWidget {

    SW_OBJECT(SwLabel, SwWidget)

    CUSTOM_PROPERTY(SwString, Text, "Label") {
        update();
    }

    CUSTOM_PROPERTY(DrawTextFormats, Alignment, DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine)) {
        update();
    }

public:
    /**
     * @brief Constructs a `SwLabel` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwLabel(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        resize(200, 30);  // Dimensions par dÃ©faut

        // DÃ©finition d'un style CSS pour le Label
        this->setFocusPolicy(FocusPolicyEnum::NoFocus);
        this->setText(text);
    }

    /**
     * @brief Constructs a `SwLabel` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwLabel(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        resize(200, 30);  // Dimensions par dÃ©faut
        this->setFocusPolicy(FocusPolicyEnum::NoFocus);
    }

    // Surcharge de la mÃ©thode paintEvent pour dessiner le Label
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
        if (getHover()) {
            state = WidgetStateHelper::setState(state, WidgetState::Hovered);
        }
        if (getFocus()) {
            state = WidgetStateHelper::setState(state, WidgetState::Focused);
        }
        if (!getEnable()) {
            state = WidgetStateHelper::setState(state, WidgetState::Disabled);
        }

        m_style->drawControl(WidgetStyle::LabelStyle, rect, painter, this, state);
    }

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

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mousePressEvent(MouseEvent* event) override {
        SwWidget::mousePressEvent(event);
    }

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseReleaseEvent(MouseEvent* event) override {
        SwWidget::mouseReleaseEvent(event);
    }

    /**
     * @brief Returns the current size Hint.
     * @return The current size Hint.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwSize sizeHint() const override {
        StyleSheet* sheet = const_cast<SwLabel*>(this)->getToolSheet();
        const SwFont font = resolvedStyledFont_(sheet);
        const SwFontMetrics metrics(font);
        const StyleSheet::BoxEdges padding = resolvePaddingEdges_(sheet);

        SwColor borderColor{0, 0, 0};
        int borderWidth = 0;
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
};

