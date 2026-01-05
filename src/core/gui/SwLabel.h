#pragma once
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
#include "SwString.h"

class SwLabel : public SwWidget {

    SW_OBJECT(SwLabel, SwWidget)

    CUSTOM_PROPERTY(SwString, Text, "Label") {
        update();
    }

    CUSTOM_PROPERTY(DrawTextFormats, Alignment, DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter)) {
        update();
    }

public:
    SwLabel(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        resize(200, 30);  // Dimensions par défaut

        // Définition d'un style CSS pour le Label
        this->setFocusPolicy(FocusPolicyEnum::NoFocus);
        this->setText(text);
    }

    SwLabel(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        resize(200, 30);  // Dimensions par défaut
        this->setFocusPolicy(FocusPolicyEnum::NoFocus);
    }

    // Surcharge de la méthode paintEvent pour dessiner le Label
    virtual void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        SwRect rect = getRect();

        WidgetState state = WidgetState::Normal;
        //if (getHover()) {
        //    state = WidgetStateHelper::setState(state, WidgetState::Hovered);
        //}
        //if (getFocus()) {
        //    state = WidgetStateHelper::setState(state, WidgetState::Focused);
        //}

        // Dessiner le label avec le style CSS et l'état défini
        m_style->drawControl(WidgetStyle::LabelStyle, rect, painter, this, WidgetState::Normal);
    }

    virtual void mouseMoveEvent(MouseEvent* event) override {
        SwWidget::mouseMoveEvent(event);
    }

    virtual void mousePressEvent(MouseEvent* event) override {
        SwWidget::mousePressEvent(event);
    }

    virtual void mouseReleaseEvent(MouseEvent* event) override {
        SwWidget::mouseReleaseEvent(event);
    }

    virtual SwRect sizeHint() const override {
        SwRect rect = getRect();
        rect.width = width();
        rect.height = height();
        return rect;
    }

    virtual SwRect minimumSizeHint() const override {
        SwRect rect = sizeHint();
        rect.width = width();
        rect.height = height();
        return rect;
    }
};
