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
 * SwScrollBar - Qt-like scrollbar widget (≈ QScrollBar).
 **************************************************************************************************/

#include "SwWidget.h"

class SwScrollBar : public SwWidget {
    SW_OBJECT(SwScrollBar, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    explicit SwScrollBar(Orientation orientation = Orientation::Vertical, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation) {
        initDefaults();
    }

    void setOrientation(Orientation orientation) {
        if (m_orientation == orientation) {
            return;
        }
        m_orientation = orientation;
        update();
    }

    Orientation orientation() const { return m_orientation; }

    void setRange(int minimum, int maximum) {
        if (minimum > maximum) {
            const int tmp = minimum;
            minimum = maximum;
            maximum = tmp;
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
        rangeChanged(m_minimum, m_maximum);
        update();
    }

    void setMinimum(int minimum) { setRange(minimum, m_maximum); }
    void setMaximum(int maximum) { setRange(m_minimum, maximum); }

    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }

    void setPageStep(int step) {
        m_pageStep = std::max(0, step);
        update();
    }

    int pageStep() const { return m_pageStep; }

    void setSingleStep(int step) {
        m_singleStep = std::max(1, step);
    }

    int singleStep() const { return m_singleStep; }

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

    bool isSliderDown() const { return m_dragging; }

    DECLARE_SIGNAL(valueChanged, int);
    DECLARE_SIGNAL(rangeChanged, int, int);
    DECLARE_SIGNAL(sliderPressed, int);
    DECLARE_SIGNAL(sliderMoved, int);
    DECLARE_SIGNAL(sliderReleased, int);

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();
        const SwRect groove = grooveRect(bounds);
        const SwRect thumb = thumbRect(bounds);

        SwColor grooveFill{236, 236, 236};
        SwColor grooveBorder{210, 210, 210};
        SwColor thumbFill{200, 200, 200};
        SwColor thumbBorder{170, 170, 170};

        if (!getEnable()) {
            grooveFill = SwColor{245, 245, 245};
            grooveBorder = SwColor{230, 230, 230};
            thumbFill = SwColor{230, 230, 230};
            thumbBorder = SwColor{210, 210, 210};
        } else if (m_dragging) {
            thumbFill = SwColor{160, 160, 160};
            thumbBorder = SwColor{140, 140, 140};
        } else if (getHover()) {
            thumbFill = SwColor{186, 186, 186};
            thumbBorder = SwColor{160, 160, 160};
        }

        painter->fillRoundedRect(groove, radiusFor(groove), grooveFill, grooveBorder, 1);
        painter->fillRoundedRect(thumb, radiusFor(thumb), thumbFill, thumbBorder, 1);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const SwRect bounds = getRect();
        const SwRect thumb = thumbRect(bounds);

        if (containsPoint(thumb, event->x(), event->y())) {
            m_dragging = true;
            m_dragOffset = (m_orientation == Orientation::Horizontal)
                               ? (event->x() - thumb.x)
                               : (event->y() - thumb.y);
            sliderPressed(m_value);
            event->accept();
            update();
            return;
        }

        // Page step when clicking the track.
        const SwRect groove = grooveRect(bounds);
        if (!containsPoint(groove, event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        if (m_orientation == Orientation::Horizontal) {
            if (event->x() < thumb.x) {
                setValue(m_value - std::max(1, m_pageStep));
            } else if (event->x() > thumb.x + thumb.width) {
                setValue(m_value + std::max(1, m_pageStep));
            }
        } else {
            if (event->y() < thumb.y) {
                setValue(m_value - std::max(1, m_pageStep));
            } else if (event->y() > thumb.y + thumb.height) {
                setValue(m_value + std::max(1, m_pageStep));
            }
        }

        event->accept();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!m_dragging) {
            SwWidget::mouseMoveEvent(event);
            return;
        }

        updateValueFromPosition(event->x(), event->y());
        sliderMoved(m_value);
        event->accept();
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (m_dragging) {
            m_dragging = false;
            sliderReleased(m_value);
            event->accept();
            update();
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!getEnable() || !isPointInside(event->x(), event->y())) {
            SwWidget::wheelEvent(event);
            return;
        }

        int steps = event->delta() / 120;
        if (steps == 0) {
            steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
        }
        if (steps == 0) {
            SwWidget::wheelEvent(event);
            return;
        }

        const int old = m_value;
        const int wheelStep = std::max(1, std::max(m_singleStep, m_pageStep / 10));
        setValue(old - steps * wheelStep);
        if (m_value != old) {
            event->accept();
            return;
        }

        SwWidget::wheelEvent(event);
    }

private:
    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    static int radiusFor(const SwRect& r) {
        const int radius = std::min(r.width, r.height) / 2;
        return std::max(2, std::min(10, radius));
    }

    int range() const {
        return std::max(0, m_maximum - m_minimum);
    }

    SwRect grooveRect(const SwRect& bounds) const {
        const int padding = 2;
        if (m_orientation == Orientation::Horizontal) {
            const int h = std::max(8, bounds.height - padding * 2);
            const int y = bounds.y + (bounds.height - h) / 2;
            return SwRect{bounds.x + padding, y, std::max(0, bounds.width - padding * 2), h};
        }

        const int w = std::max(8, bounds.width - padding * 2);
        const int x = bounds.x + (bounds.width - w) / 2;
        return SwRect{x, bounds.y + padding, w, std::max(0, bounds.height - padding * 2)};
    }

    int thumbLength(const SwRect& groove) const {
        const int trackLen = (m_orientation == Orientation::Horizontal) ? groove.width : groove.height;
        const int minLen = std::max(16, trackLen / 8);
        if (trackLen <= 0) {
            return minLen;
        }

        const int r = range();
        const int p = std::max(0, m_pageStep);
        if (r <= 0) {
            return trackLen;
        }

        const int denom = r + std::max(1, p);
        const int len = static_cast<int>((static_cast<long long>(trackLen) * std::max(1, p)) / denom);
        return clampInt(len, minLen, trackLen);
    }

    SwRect thumbRect(const SwRect& bounds) const {
        const SwRect groove = grooveRect(bounds);
        const int len = thumbLength(groove);
        const int r = range();
        if (m_orientation == Orientation::Horizontal) {
            const int trackLen = std::max(1, groove.width - len);
            const int pos = (r <= 0) ? 0 : static_cast<int>((static_cast<long long>(m_value - m_minimum) * trackLen) / r);
            return SwRect{groove.x + pos, groove.y, len, groove.height};
        }

        const int trackLen = std::max(1, groove.height - len);
        const int pos = (r <= 0) ? 0 : static_cast<int>((static_cast<long long>(m_value - m_minimum) * trackLen) / r);
        return SwRect{groove.x, groove.y + pos, groove.width, len};
    }

    void updateValueFromPosition(int px, int py) {
        const SwRect bounds = getRect();
        const SwRect groove = grooveRect(bounds);
        const int len = thumbLength(groove);
        const int r = range();
        if (r <= 0) {
            setValue(m_minimum);
            return;
        }

        if (m_orientation == Orientation::Horizontal) {
            const int trackLen = std::max(1, groove.width - len);
            int pos = px - groove.x - m_dragOffset;
            pos = clampInt(pos, 0, trackLen);
            const int v = m_minimum + static_cast<int>((static_cast<long long>(pos) * r) / trackLen);
            setValue(v);
            return;
        }

        const int trackLen = std::max(1, groove.height - len);
        int pos = py - groove.y - m_dragOffset;
        pos = clampInt(pos, 0, trackLen);
        const int v = m_minimum + static_cast<int>((static_cast<long long>(pos) * r) / trackLen);
        setValue(v);
    }

    void initDefaults() {
        if (m_orientation == Orientation::Horizontal) {
            resize(180, 14);
        } else {
            resize(14, 180);
        }
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setStyleSheet("SwScrollBar { background-color: rgba(0,0,0,0); border-width: 0px; }");
        m_minimum = 0;
        m_maximum = 100;
        m_value = 0;
        m_pageStep = 10;
        m_singleStep = 1;
    }

    Orientation m_orientation{Orientation::Vertical};
    int m_minimum{0};
    int m_maximum{100};
    int m_value{0};
    int m_pageStep{10};
    int m_singleStep{1};
    bool m_dragging{false};
    int m_dragOffset{0};
};
