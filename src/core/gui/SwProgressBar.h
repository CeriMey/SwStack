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
 * SwProgressBar - Qt-like progress bar widget.
 *
 * Supported format tokens (Qt-like):
 * - %p : percentage (0-100)
 * - %v : current value
 * - %m : maximum value
 **************************************************************************************************/

#include "SwWidget.h"

class SwProgressBar : public SwWidget {
    SW_OBJECT(SwProgressBar, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    explicit SwProgressBar(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    void setRange(int minimum, int maximum) {
        if (minimum > maximum) {
            const int tmp = minimum;
            minimum = maximum;
            maximum = tmp;
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
        update();
    }

    void setMinimum(int minimum) { setRange(minimum, m_maximum); }
    void setMaximum(int maximum) { setRange(m_minimum, maximum); }

    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }

    void setValue(int value) {
        const int clamped = clampInt(value, m_minimum, m_maximum);
        if (m_value == clamped) {
            return;
        }
        m_value = clamped;
        valueChanged(m_value);
        update();
    }

    int value() const { return m_value; }

    void setTextVisible(bool on) {
        if (m_textVisible == on) {
            return;
        }
        m_textVisible = on;
        update();
    }

    bool isTextVisible() const { return m_textVisible; }

    void setFormat(const SwString& format) {
        m_format = format;
        update();
    }

    SwString format() const { return m_format; }

    SwString text() const {
        SwString out = m_format;
        out.replace("%p", SwString::number(percent()));
        out.replace("%v", SwString::number(m_value));
        out.replace("%m", SwString::number(m_maximum));
        return out;
    }

    void setOrientation(Orientation orientation) {
        if (m_orientation == orientation) {
            return;
        }
        m_orientation = orientation;
        update();
    }

    Orientation orientation() const { return m_orientation; }

    void setAccentColor(const SwColor& color) {
        m_accent = clampColor(color);
        update();
    }

    SwColor accentColor() const { return m_accent; }

    DECLARE_SIGNAL(valueChanged, int);

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();
        const int radius = clampInt(bounds.height / 3, 6, 10);

        SwColor frame{172, 172, 172};
        SwColor background{236, 236, 236};
        SwColor textColor{30, 30, 30};
        SwColor fillColor = m_accent;

        if (!getEnable()) {
            frame = SwColor{200, 200, 200};
            background = SwColor{245, 245, 245};
            textColor = SwColor{150, 150, 150};
            fillColor = SwColor{180, 200, 230};
        }

        painter->fillRoundedRect(bounds, radius, background, frame, 1);

        const int padding = 3;
        SwRect inner{bounds.x + padding,
                     bounds.y + padding,
                     clampInt(bounds.width - padding * 2, 0, 1000000),
                     clampInt(bounds.height - padding * 2, 0, 1000000)};

        const int range = m_maximum - m_minimum;
        int fillPixels = 0;
        if (range > 0) {
            const long long numerator = static_cast<long long>(m_value - m_minimum) * 1000LL;
            const long long ratio1000 = clampLL(numerator / range, 0LL, 1000LL);
            if (m_orientation == Orientation::Horizontal) {
                fillPixels = static_cast<int>((static_cast<long long>(inner.width) * ratio1000) / 1000LL);
            } else {
                fillPixels = static_cast<int>((static_cast<long long>(inner.height) * ratio1000) / 1000LL);
            }
        }

        if (fillPixels > 0) {
            if (m_orientation == Orientation::Horizontal) {
                SwRect fill{inner.x, inner.y, fillPixels, inner.height};
                painter->fillRoundedRect(fill, clampInt(radius - 2, 2, 10), fillColor, fillColor, 0);
            } else {
                SwRect fill{inner.x, inner.y + (inner.height - fillPixels), inner.width, fillPixels};
                painter->fillRoundedRect(fill, clampInt(radius - 2, 2, 10), fillColor, fillColor, 0);
            }
        }

        if (m_textVisible) {
            painter->drawText(bounds,
                              text(),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              getFont());
        }
    }

private:
    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static long long clampLL(long long value, long long minValue, long long maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor(const SwColor& c) {
        return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
    }

    int percent() const {
        const int range = m_maximum - m_minimum;
        if (range <= 0) {
            return 0;
        }
        const int clampedValue = clampInt(m_value, m_minimum, m_maximum);
        const long long numerator = static_cast<long long>(clampedValue - m_minimum) * 100LL;
        return static_cast<int>(clampLL(numerator / range, 0LL, 100LL));
    }

    void initDefaults() {
        resize(260, 22);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFont(SwFont(L"Segoe UI", 9, Medium));
    }

    int m_minimum{0};
    int m_maximum{100};
    int m_value{0};
    bool m_textVisible{true};
    SwString m_format{"%p%"};
    Orientation m_orientation{Orientation::Horizontal};
    SwColor m_accent{0, 120, 215};
};

