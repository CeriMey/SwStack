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
 * Header-only sliders with a video player inspired look.
 *
 * SwAbstractSlider factors the common behaviour (range handling, scrubbing logic and painting) and
 * SwHorizontalSlider / SwVerticalSlider simply pick the orientation.
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwPainter.h"

#include <algorithm>
#include <cmath>

class SwAbstractSlider : public SwWidget {
    SW_OBJECT(SwAbstractSlider, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    explicit SwAbstractSlider(Orientation orientation, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation)
        , m_minimum(0.0)
        , m_maximum(1.0)
        , m_value(0.0)
        , m_step(0.0)
        , m_bufferValue(0.0)
        , m_dragging(false)
        , m_trackThickness(6)
        , m_handleRadius(8)
        , m_padding(12)
        , m_trackColor{46, 52, 64}
        , m_trackBorder{28, 32, 40}
        , m_bufferColor{82, 92, 108}
        , m_progressColor{230, 70, 64}
        , m_handleColor{255, 255, 255}
        , m_handleBorder{26, 30, 38} {
        setCursor(CursorType::Hand);
        setFocusPolicy(FocusPolicyEnum::Strong);
        if (m_orientation == Orientation::Horizontal) {
            resize(320, 32);
            setMinimumSize(160, 24);
        } else {
            resize(48, 220);
            setMinimumSize(36, 120);
        }
        setStyleSheet("SwAbstractSlider { background-color: rgba(0,0,0,0); }");
    }

    void setRange(double minimum, double maximum) {
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
    }

    void setMinimum(double minimum) {
        setRange(minimum, m_maximum);
    }

    void setMaximum(double maximum) {
        setRange(m_minimum, maximum);
    }

    double minimum() const { return m_minimum; }
    double maximum() const { return m_maximum; }

    void setValue(double value) {
        double clamped = clampValue(value);
        if (std::abs(clamped - m_value) < 1e-9) {
            return;
        }
        m_value = clamped;
        valueChanged(m_value);
        update();
    }

    double value() const { return m_value; }

    void setStep(double step) {
        m_step = (step < 0.0) ? 0.0 : step;
    }

    double step() const { return m_step; }

    void setBufferedValue(double value) {
        double clamped = clampToRange(value);
        if (std::abs(clamped - m_bufferValue) < 1e-9) {
            return;
        }
        m_bufferValue = clamped;
        update();
    }

    double bufferedValue() const { return m_bufferValue; }

    void setAccentColor(const SwColor& color) {
        m_progressColor = color;
        update();
    }

    void setTrackColors(const SwColor& base, const SwColor& border) {
        m_trackColor = base;
        m_trackBorder = border;
        update();
    }

    void setBufferedColor(const SwColor& color) {
        m_bufferColor = color;
        update();
    }

    void setHandleColors(const SwColor& fill, const SwColor& border) {
        m_handleColor = fill;
        m_handleBorder = border;
        update();
    }

    Orientation orientation() const { return m_orientation; }

    DECLARE_SIGNAL(valueChanged, double);

protected:
    SwRect trackRect() const {
        const int thickness = visualTrackThickness();
        SwRect bounds = getRect();
        if (m_orientation == Orientation::Horizontal) {
            int centerY = bounds.y + bounds.height / 2;
            return SwRect{bounds.x + m_padding,
                          centerY - thickness / 2,
                          std::max(0, bounds.width - 2 * m_padding),
                          thickness};
        }

        int centerX = bounds.x + bounds.width / 2;
        return SwRect{centerX - thickness / 2,
                      bounds.y + m_padding,
                      thickness,
                      std::max(0, bounds.height - 2 * m_padding)};
    }

    SwRect progressRect() const {
        SwRect track = trackRect();
        double ratio = valueRatio(m_value);
        if (m_orientation == Orientation::Horizontal) {
            int filledWidth = std::max(0, std::min(track.width, static_cast<int>(std::round(track.width * ratio))));
            return SwRect{track.x, track.y, filledWidth, track.height};
        }

        int filledHeight = std::max(0, std::min(track.height, static_cast<int>(std::round(track.height * ratio))));
        int y = track.y + track.height - filledHeight;
        return SwRect{track.x, y, track.width, filledHeight};
    }

    SwRect bufferedRect() const {
        SwRect track = trackRect();
        double ratio = valueRatio(m_bufferValue);
        if (m_orientation == Orientation::Horizontal) {
            int filledWidth = std::max(0, std::min(track.width, static_cast<int>(std::round(track.width * ratio))));
            return SwRect{track.x, track.y, filledWidth, track.height};
        }

        int filledHeight = std::max(0, std::min(track.height, static_cast<int>(std::round(track.height * ratio))));
        int y = track.y + track.height - filledHeight;
        return SwRect{track.x, y, track.width, filledHeight};
    }

    SwRect handleRect() const {
        SwRect track = trackRect();
        double ratio = valueRatio(m_value);
        int visualRadius = visualHandleRadius();
        int size = visualRadius * 2;

        if (m_orientation == Orientation::Horizontal) {
            int centerX = track.x + static_cast<int>(std::round(track.width * ratio));
            int centerY = track.y + track.height / 2;
            return SwRect{centerX - visualRadius, centerY - visualRadius, size, size};
        }

        int centerY = track.y + track.height - static_cast<int>(std::round(track.height * ratio));
        int centerX = track.x + track.width / 2;
        return SwRect{centerX - visualRadius, centerY - visualRadius, size, size};
    }

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        SwRect track = trackRect();
        SwRect buffered = bufferedRect();
        SwRect progress = progressRect();
        SwRect handle = handleRect();

        int radius = (m_orientation == Orientation::Horizontal)
                         ? std::max(1, track.height / 2)
                         : std::max(1, track.width / 2);

        SwColor playedColor = m_progressColor;
        SwColor handleFill = m_handleColor;
        if (getHover() || m_dragging) {
            playedColor = lighten(playedColor, 24);
            handleFill = lighten(handleFill, 18);
        }

        painter->fillRoundedRect(track, radius, m_trackColor, m_trackColor, 0);

        if (m_bufferValue > m_minimum && m_maximum > m_minimum) {
            painter->fillRoundedRect(buffered, radius, m_bufferColor, m_bufferColor, 0);
        }

        painter->fillRoundedRect(progress, radius, playedColor, playedColor, 0);
        painter->fillRoundedRect(handle, visualHandleRadius(), handleFill, handleFill, 0);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        m_dragging = true;
        setValue(valueFromPosition(event->x(), event->y()));
        event->accept();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (m_dragging) {
            setValue(valueFromPosition(event->x(), event->y()));
            event->accept();
        }
        SwWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (m_dragging) {
            setValue(valueFromPosition(event->x(), event->y()));
            m_dragging = false;
            event->accept();
        }
        SwWidget::mouseReleaseEvent(event);
    }

    double valueFromPosition(int px, int py) const {
        SwRect track = trackRect();
        if (m_orientation == Orientation::Horizontal) {
            int clampedX = std::max(track.x, std::min(px, track.x + track.width));
            double ratio = track.width > 0 ? static_cast<double>(clampedX - track.x) / static_cast<double>(track.width) : 0.0;
            return clampValue(m_minimum + ratio * (m_maximum - m_minimum));
        }

        int clampedY = std::max(track.y, std::min(py, track.y + track.height));
        double ratio = track.height > 0 ? static_cast<double>(track.height - (clampedY - track.y)) / static_cast<double>(track.height) : 0.0;
        return clampValue(m_minimum + ratio * (m_maximum - m_minimum));
    }

    double clampValue(double value) const {
        double clamped = clampToRange(value);

        if (m_step > 0.0) {
            double steps = std::round((clamped - m_minimum) / m_step);
            clamped = m_minimum + steps * m_step;
            if (clamped < m_minimum) {
                clamped = m_minimum;
            }
            if (clamped > m_maximum) {
                clamped = m_maximum;
            }
        }

        return clamped;
    }

    double valueRatio(double value) const {
        double range = m_maximum - m_minimum;
        if (range <= 0.0) {
            return 0.0;
        }
        double ratio = (value - m_minimum) / range;
        if (ratio < 0.0) {
            ratio = 0.0;
        }
        if (ratio > 1.0) {
            ratio = 1.0;
        }
        return ratio;
    }

    int visualTrackThickness() const {
        return (getHover() || m_dragging) ? m_trackThickness + 2 : m_trackThickness;
    }

    int visualHandleRadius() const {
        if (m_dragging) {
            return m_handleRadius + 2;
        }
        if (getHover()) {
            return m_handleRadius + 1;
        }
        return m_handleRadius;
    }

    SwColor lighten(const SwColor& c, int delta) const {
        return SwColor{
            std::min(255, c.r + delta),
            std::min(255, c.g + delta),
            std::min(255, c.b + delta)};
    }

private:
    double clampToRange(double value) const {
        double clamped = value;
        if (m_maximum > m_minimum) {
            if (clamped < m_minimum) {
                clamped = m_minimum;
            }
            if (clamped > m_maximum) {
                clamped = m_maximum;
            }
        } else {
            clamped = m_minimum;
        }
        return clamped;
    }

    Orientation m_orientation;
    double m_minimum;
    double m_maximum;
    double m_value;
    double m_step;
    double m_bufferValue;
    bool m_dragging;
    int m_trackThickness;
    int m_handleRadius;
    int m_padding;
    SwColor m_trackColor;
    SwColor m_trackBorder;
    SwColor m_bufferColor;
    SwColor m_progressColor;
    SwColor m_handleColor;
    SwColor m_handleBorder;
};

class SwHorizontalSlider : public SwAbstractSlider {
    SW_OBJECT(SwHorizontalSlider, SwAbstractSlider)

public:
    explicit SwHorizontalSlider(SwWidget* parent = nullptr)
        : SwAbstractSlider(Orientation::Horizontal, parent) {}
};

class SwVerticalSlider : public SwAbstractSlider {
    SW_OBJECT(SwVerticalSlider, SwAbstractSlider)

public:
    explicit SwVerticalSlider(SwWidget* parent = nullptr)
        : SwAbstractSlider(Orientation::Vertical, parent) {}
};
