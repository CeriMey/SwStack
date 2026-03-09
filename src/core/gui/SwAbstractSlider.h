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
 * @file src/core/gui/SwAbstractSlider.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwAbstractSlider in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the abstract slider interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwAbstractSlider, SwHorizontalSlider, and
 * SwVerticalSlider.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


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

    /**
     * @brief Constructs a `SwAbstractSlider` instance.
     * @param orientation Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
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

    /**
     * @brief Sets the range.
     * @param minimum Value passed to the method.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRange(double minimum, double maximum) {
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
    }

    /**
     * @brief Sets the minimum.
     * @param minimum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMinimum(double minimum) {
        setRange(minimum, m_maximum);
    }

    /**
     * @brief Sets the maximum.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaximum(double maximum) {
        setRange(m_minimum, maximum);
    }

    /**
     * @brief Returns the current minimum.
     * @return The current minimum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double minimum() const { return m_minimum; }
    /**
     * @brief Returns the current maximum.
     * @return The current maximum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double maximum() const { return m_maximum; }

    /**
     * @brief Sets the value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setValue(double value) {
        double clamped = clampValue(value);
        if (std::abs(clamped - m_value) < 1e-9) {
            return;
        }
        m_value = clamped;
        valueChanged(m_value);
        update();
    }

    /**
     * @brief Returns the current value.
     * @return The current value.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double value() const { return m_value; }

    /**
     * @brief Sets the step.
     * @param step Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStep(double step) {
        m_step = (step < 0.0) ? 0.0 : step;
    }

    /**
     * @brief Returns the current step.
     * @return The current step.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double step() const { return m_step; }

    /**
     * @brief Sets the buffered Value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBufferedValue(double value) {
        double clamped = clampToRange(value);
        if (std::abs(clamped - m_bufferValue) < 1e-9) {
            return;
        }
        m_bufferValue = clamped;
        update();
    }

    /**
     * @brief Returns the current buffered Value.
     * @return The current buffered Value.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double bufferedValue() const { return m_bufferValue; }

    /**
     * @brief Sets the accent Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAccentColor(const SwColor& color) {
        m_progressColor = color;
        update();
    }

    /**
     * @brief Sets the track Colors.
     * @param base Value passed to the method.
     * @param border Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTrackColors(const SwColor& base, const SwColor& border) {
        m_trackColor = base;
        m_trackBorder = border;
        update();
    }

    /**
     * @brief Sets the buffered Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBufferedColor(const SwColor& color) {
        m_bufferColor = color;
        update();
    }

    /**
     * @brief Sets the handle Colors.
     * @param fill Value passed to the method.
     * @param border Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHandleColors(const SwColor& fill, const SwColor& border) {
        m_handleColor = fill;
        m_handleBorder = border;
        update();
    }

    /**
     * @brief Returns the current orientation.
     * @return The current orientation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Orientation orientation() const { return m_orientation; }

    DECLARE_SIGNAL(valueChanged, double);

protected:
    /**
     * @brief Returns the current track Rect.
     * @return The current track Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRect trackRect() const {
        const int thickness = visualTrackThickness();
        SwRect bounds = rect();
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

    /**
     * @brief Returns the current progress Rect.
     * @return The current progress Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Returns the current buffered Rect.
     * @return The current buffered Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Returns the current handle Rect.
     * @return The current handle Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mousePressEvent(MouseEvent* event) override {
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        m_dragging = true;
        setValue(valueFromPosition(event->x(), event->y()));
        event->accept();
    }

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseMoveEvent(MouseEvent* event) override {
        if (m_dragging) {
            setValue(valueFromPosition(event->x(), event->y()));
            event->accept();
        }
        SwWidget::mouseMoveEvent(event);
    }

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseReleaseEvent(MouseEvent* event) override {
        if (m_dragging) {
            setValue(valueFromPosition(event->x(), event->y()));
            m_dragging = false;
            event->accept();
        }
        SwWidget::mouseReleaseEvent(event);
    }

    /**
     * @brief Performs the `valueFromPosition` operation.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     * @return The requested value From Position.
     */
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

    /**
     * @brief Performs the `clampValue` operation.
     * @param value Value passed to the method.
     * @return The requested clamp Value.
     */
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

    /**
     * @brief Performs the `valueRatio` operation.
     * @param value Value passed to the method.
     * @return The requested value Ratio.
     */
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

    /**
     * @brief Returns the current visual Track Thickness.
     * @return The current visual Track Thickness.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int visualTrackThickness() const {
        return (getHover() || m_dragging) ? m_trackThickness + 2 : m_trackThickness;
    }

    /**
     * @brief Returns the current visual Handle Radius.
     * @return The current visual Handle Radius.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int visualHandleRadius() const {
        if (m_dragging) {
            return m_handleRadius + 2;
        }
        if (getHover()) {
            return m_handleRadius + 1;
        }
        return m_handleRadius;
    }

    /**
     * @brief Performs the `lighten` operation.
     * @param c Value passed to the method.
     * @param delta Value passed to the method.
     * @return The requested lighten.
     */
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
    /**
     * @brief Constructs a `SwHorizontalSlider` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwHorizontalSlider(SwWidget* parent = nullptr)
        : SwAbstractSlider(Orientation::Horizontal, parent) {}
};

class SwVerticalSlider : public SwAbstractSlider {
    SW_OBJECT(SwVerticalSlider, SwAbstractSlider)

public:
    /**
     * @brief Constructs a `SwVerticalSlider` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwVerticalSlider(SwWidget* parent = nullptr)
        : SwAbstractSlider(Orientation::Vertical, parent) {}
};

