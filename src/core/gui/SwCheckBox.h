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

/***************************************************************************************************
 * SwCheckBox - Qt-like checkbox widget.
 *
 * API goals:
 * - Similar naming to QCheckBox (setChecked/isChecked, setText/text, stateChanged/toggled/clicked).
 * - Minimal dependencies (prefer Sw* types, avoid std:: in widget code).
 **************************************************************************************************/

#include "SwWidget.h"

class SwCheckBox : public SwWidget {
    SW_OBJECT(SwCheckBox, SwWidget)

public:
    enum CheckState {
        Unchecked = 0,
        PartiallyChecked = 1,
        Checked = 2
    };

    explicit SwCheckBox(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    SwCheckBox(const SwString& text, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
        setText(text);
    }

    void setAccentColor(const SwColor& color) {
        m_accent = clampColor(color);
        update();
    }

    SwColor accentColor() const { return m_accent; }

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

    bool isTristate() const { return m_tristate; }

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

    CheckState checkState() const { return m_state; }

    void setChecked(bool checked) { setCheckState(checked ? Checked : Unchecked); }
    bool isChecked() const { return m_state == Checked; }

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

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();
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

        if (!getEnable()) {
            border = SwColor{190, 190, 190};
            indicatorFill = SwColor{245, 245, 245};
            textColor = SwColor{150, 150, 150};
        } else if (getHover()) {
            border = SwColor{120, 120, 120};
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

        if (getFocus()) {
            const SwColor focusColor = m_accent;
            SwRect focusRect{bounds.x, bounds.y, bounds.width - 1, bounds.height - 1};
            painter->drawRect(focusRect, focusColor, 1);
        }
    }

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

    void mouseMoveEvent(MouseEvent* event) override {
        SwWidget::mouseMoveEvent(event);
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

