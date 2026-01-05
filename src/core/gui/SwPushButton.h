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
    SwPushButton(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent){
        resize(150, 50);

        setText(text);

        SwString css = R"(
            SwPushButton {
                background-color: rgb(236, 236, 236);
                border-color: rgb(172, 172, 172);
                color: rgb(30, 30, 30);
                border-radius: 6px;
                padding: 8px 14px;
                border-width: 1px;
                font-size: 14px;
            }
            SwPushButton:hover {
                background-color: rgb(224, 224, 224);
                border-color: rgb(160, 160, 160);
            }
            SwPushButton:pressed {
                background-color: rgb(210, 210, 210);
                border-color: rgb(140, 140, 140);
                color: rgb(20, 20, 20);
            }
        )";
        this->setStyleSheet(css);
    }

    bool isCheckable() const { return getCheckable(); }

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

    bool isChecked() const { return m_checked; }

    void toggle() { setChecked(!m_checked); }

    virtual void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        SwRect rect = getRect();

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
        //if (getEnable()) {
        //    state = WidgetStateHelper::setState(state, WidgetState::Disabled);
        //}
        //if (getFocus()) {
        //    state = WidgetStateHelper::setState(state, WidgetState::Focused);
        //}

        m_style->drawControl(WidgetStyle::PushButtonStyle, rect, painter, this, state);

    }




    // Gérer le survol de la souris
    virtual void mouseMoveEvent(MouseEvent* event) override {
        SwWidget::mouseMoveEvent(event);
    }

    // Gérer le clic sur le bouton
    virtual void mousePressEvent(MouseEvent* event) override {
        if (isPointInside(event->x(), event->y())) {
            setPressed(true);
            event->accept();
        }
        SwWidget::mousePressEvent(event);
    }

    // Gérer le relâchement du bouton
    virtual void mouseReleaseEvent(MouseEvent* event) override {
        if (getPressed() && isPointInside(event->x(), event->y())) {
            emit clicked();  // Émettre le signal 'clicked'
            event->accept();
            if (getCheckable()) {
                setChecked(!m_checked);
            }
        }
        setPressed(false);
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

signals:
    DECLARE_SIGNAL_VOID(clicked);
    DECLARE_SIGNAL(toggled, bool);

private:
    bool m_checked{false};

};
