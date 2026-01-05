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

class SwToolButton : public SwWidget {
    SW_OBJECT(SwToolButton, SwWidget)

public:
    explicit SwToolButton(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    SwToolButton(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
        setText(text);
    }

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

    bool isCheckable() const { return m_checkable; }

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

    bool isChecked() const { return m_checked; }

    DECLARE_SIGNAL(clicked, bool);
    DECLARE_SIGNAL(toggled, bool);

protected:
    CUSTOM_PROPERTY(SwString, Text, "ToolButton") { update(); }
    CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();
        const int radius = clampInt(std::min(bounds.width, bounds.height) / 4, 6, 12);

        SwColor bg{255, 255, 255};
        SwColor border{220, 224, 232};
        SwColor textColor{30, 30, 30};

        if (!getEnable()) {
            bg = SwColor{245, 245, 245};
            border = SwColor{230, 230, 230};
            textColor = SwColor{150, 150, 150};
        } else if (m_checkable && m_checked) {
            bg = m_accent;
            border = m_accent;
            textColor = SwColor{255, 255, 255};
        } else if (getPressed()) {
            bg = SwColor{236, 236, 236};
            border = SwColor{200, 200, 200};
        } else if (getHover()) {
            bg = SwColor{250, 251, 253};
            border = SwColor{200, 200, 200};
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
    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor(const SwColor& c) {
        return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
    }

    void initDefaults() {
        resize(96, 34);
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFont(SwFont(L"Segoe UI", 9, Medium));
        setStyleSheet(R"(
            SwToolButton {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 10px;
                color: rgb(30, 30, 30);
                font-size: 13px;
                padding: 6px 10px;
            }
        )");
    }

    bool m_checkable{false};
    bool m_checked{false};
    SwColor m_accent{59, 130, 246};
};

