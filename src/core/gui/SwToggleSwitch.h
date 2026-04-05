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
 * @file src/core/gui/SwToggleSwitch.h
 * @ingroup core_gui
 * @brief iOS / Material-style toggle switch widget.
 *
 * A pill-shaped track with a sliding circular knob. The switch is either ON or OFF.
 * Clicking anywhere on the widget toggles the state.
 *
 * Styleable properties (via SwCheckBox-like stylesheet selectors on "SwToggleSwitch"):
 *   - background-color        : track colour when ON   (default: rgb(76, 154, 230))
 *   - background-color-off    : track colour when OFF  (default: rgb(140, 140, 140))
 *   - color                   : knob colour            (default: white)
 *   - border-color            : track border when ON
 *   - border-color-off        : track border when OFF
 *
 * The widget reports a fixed sizeHint of trackWidth x trackHeight and paints a rounded
 * pill track with a circular knob that snaps left (OFF) or right (ON).
 */

#include "SwWidget.h"

class SwToggleSwitch : public SwWidget {
    SW_OBJECT(SwToggleSwitch, SwWidget)

public:
    explicit SwToggleSwitch(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setCursor(CursorType::Hand);
    }

    bool isChecked() const { return m_checked; }

    void setChecked(bool on) {
        if (m_checked == on) {
            return;
        }
        m_checked = on;
        update();
        toggled(m_checked);
    }

    void toggle() { setChecked(!m_checked); }

    void setTrackSize(int w, int h) {
        m_trackW = (w > 20) ? w : 20;
        m_trackH = (h > 12) ? h : 12;
        update();
    }

    int trackWidth() const { return m_trackW; }
    int trackHeight() const { return m_trackH; }

    DECLARE_SIGNAL(toggled, bool);

protected:
    SwSize sizeHint() const override {
        return SwSize{m_trackW, m_trackH};
    }

    SwSize minimumSizeHint() const override {
        return sizeHint();
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (event && !event->isAccepted() && getEnable() &&
            isPointInside(event->x(), event->y())) {
            event->accept();
            toggle();
        }
        SwWidget::mouseReleaseEvent(event);
    }

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        const int tw = m_trackW;
        const int th = m_trackH;
        const int tx = bounds.x + (bounds.width - tw) / 2;
        const int ty = bounds.y + (bounds.height - th) / 2;
        const int radius = th / 2;

        // --- Resolve colours from stylesheet ---
        SwColor trackOn{76, 154, 230};
        SwColor trackOff{90, 100, 108};
        SwColor knobColor{255, 255, 255};
        SwColor borderOn{76, 154, 230};
        SwColor borderOff{70, 80, 86};

        StyleSheet* sheet = getToolSheet();
        if (sheet) {
            auto tryParse = [&](const char* sel, const char* prop, SwColor& out) {
                SwString v = sheet->getStyleProperty(sel, prop);
                if (!v.isEmpty()) {
                    try { out = sheet->parseColor(v, nullptr); } catch (...) {}
                }
            };
            tryParse("SwToggleSwitch", "background-color", trackOn);
            tryParse("SwToggleSwitch", "background-color-off", trackOff);
            tryParse("SwToggleSwitch", "color", knobColor);
            tryParse("SwToggleSwitch", "border-color", borderOn);
            tryParse("SwToggleSwitch", "border-color-off", borderOff);
        }

        if (!getEnable()) {
            trackOn = SwColor{60, 70, 75};
            trackOff = SwColor{50, 55, 60};
            knobColor = SwColor{140, 140, 140};
            borderOn = trackOn;
            borderOff = trackOff;
        }

        const SwColor& bg = m_checked ? trackOn : trackOff;
        const SwColor& border = m_checked ? borderOn : borderOff;

        // --- Track (pill) ---
        const SwRect track{tx, ty, tw, th};
        painter->fillRoundedRect(track, radius, bg, border, 1);

        // --- Knob (circle) ---
        const int knobPad = 2;
        const int knobD = th - knobPad * 2;
        const int knobY = ty + knobPad;
        const int knobX = m_checked ? (tx + tw - knobD - knobPad)
                                    : (tx + knobPad);
        const SwRect knob{knobX, knobY, knobD, knobD};
        painter->fillRoundedRect(knob, knobD / 2, knobColor, knobColor, 0);
    }

private:
    bool m_checked = false;
    int m_trackW = 44;
    int m_trackH = 24;
};
