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
 * @file src/core/gui/SwSlider.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwSlider in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the slider interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwSlider.
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
 * Simple cross-platform slider widget for SwCore.
 *
 * Provides horizontal and vertical orientations, range configuration and a valueChanged signal.
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwPainter.h"

#include <algorithm>
#include <cmath>

class SwSlider : public SwWidget {
    SW_OBJECT(SwSlider, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    /**
     * @brief Constructs a `SwSlider` instance.
     * @param orientation Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwSlider(Orientation orientation = Orientation::Horizontal, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation)
        , m_minimum(0)
        , m_maximum(100)
        , m_value(0)
        , m_step(1)
        , m_dragging(false)
        , m_handleSize(26) {
        if (m_orientation == Orientation::Horizontal) {
            resize(260, 48);
        } else {
            resize(60, 260);
        }
        setCursor(CursorType::Hand);
    }

    /**
     * @brief Sets the orientation.
     * @param orientation Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOrientation(Orientation orientation) {
        if (m_orientation == orientation) {
            return;
        }
        m_orientation = orientation;
        if (m_orientation == Orientation::Horizontal) {
            resize(std::max(width(), 200), std::max(height(), 40));
        } else {
            resize(std::max(width(), 55), std::max(height(), 200));
        }
        update();
    }

    /**
     * @brief Returns the current orientation.
     * @return The current orientation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Orientation orientation() const { return m_orientation; }

    /**
     * @brief Sets the range.
     * @param minimum Value passed to the method.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRange(int minimum, int maximum) {
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
    }

    /**
     * @brief Sets the minimum.
     * @param m_maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMinimum(int minimum) { setRange(minimum, m_maximum); }
    /**
     * @brief Sets the maximum.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaximum(int maximum) { setRange(m_minimum, maximum); }

    /**
     * @brief Returns the current minimum.
     * @return The current minimum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int minimum() const { return m_minimum; }
    /**
     * @brief Returns the current maximum.
     * @return The current maximum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maximum() const { return m_maximum; }

    /**
     * @brief Sets the step.
     * @param step Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStep(int step) {
        m_step = std::max(1, step);
    }

    /**
     * @brief Returns the current step.
     * @return The current step.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int step() const { return m_step; }

    /**
     * @brief Sets the value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setValue(int value) {
        int clamped = clampValue(value);
        if (clamped == m_value) {
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
    int value() const { return m_value; }

    DECLARE_SIGNAL(valueChanged, int);

protected:
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

        SwRect bounds = rect();
        SwColor background{28, 32, 48};
        SwColor grooveColor{54, 62, 86};
        SwColor accent{88, 140, 255};

        painter->fillRoundedRect(bounds, 8, background, SwColor{18, 20, 30}, 1);

        SwRect groove = grooveRect();
        painter->fillRoundedRect(groove, groove.height / 2, grooveColor, SwColor{24, 28, 40}, 1);

        SwRect handle = handleRect();
        painter->fillRoundedRect(handle, handle.width / 2, accent, SwColor{16, 18, 32}, 1);
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
        updateValueFromPosition(event->x(), event->y());
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
            updateValueFromPosition(event->x(), event->y());
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
            m_dragging = false;
            updateValueFromPosition(event->x(), event->y());
            event->accept();
        }
        SwWidget::mouseReleaseEvent(event);
    }

private:
    int clampValue(int value) const {
        if (m_maximum == m_minimum) {
            return m_minimum;
        }
        int clamped = std::min(std::max(value, m_minimum), m_maximum);
        int delta = clamped - m_minimum;
        if (m_step > 1) {
            float ratio = static_cast<float>(delta) / static_cast<float>(m_step);
            int steps = static_cast<int>(std::round(ratio));
            clamped = m_minimum + steps * m_step;
            if (clamped > m_maximum) {
                clamped = m_maximum;
            }
            if (clamped < m_minimum) {
                clamped = m_minimum;
            }
        }
        return clamped;
    }

    void updateValueFromPosition(int px, int py) {
        SwRect groove = grooveRect();
        if (m_orientation == Orientation::Horizontal) {
            int clampedX = std::min(std::max(px, groove.x), groove.x + groove.width);
            float ratio = groove.width > 0 ? static_cast<float>(clampedX - groove.x) / static_cast<float>(groove.width) : 0.0f;
            int rawValue = static_cast<int>(std::round(m_minimum + ratio * (m_maximum - m_minimum)));
            setValue(rawValue);
        } else {
            int clampedY = std::min(std::max(py, groove.y), groove.y + groove.height);
            float ratio = groove.height > 0 ? static_cast<float>(clampedY - groove.y) / static_cast<float>(groove.height) : 0.0f;
            // For vertical sliders, 0.0 ratio == top (maximum)
            int rawValue = static_cast<int>(std::round(m_maximum - ratio * (m_maximum - m_minimum)));
            setValue(rawValue);
        }
    }

    SwRect grooveRect() const {
        SwRect bounds = rect();
        const int padding = 18;
        if (m_orientation == Orientation::Horizontal) {
            int grooveHeight = std::max(6, bounds.height / 4);
            int centerY = bounds.y + bounds.height / 2;
            return SwRect{bounds.x + padding,
                          centerY - grooveHeight / 2,
                          bounds.width - padding * 2,
                          grooveHeight};
        }

        int grooveWidth = std::max(6, bounds.width / 3);
        int centerX = bounds.x + bounds.width / 2;
        return SwRect{centerX - grooveWidth / 2,
                      bounds.y + padding,
                      grooveWidth,
                      bounds.height - padding * 2};
    }

    SwRect handleRect() const {
        SwRect groove = grooveRect();
        SwRect bounds = rect();
        if (m_orientation == Orientation::Horizontal) {
            float ratio = (m_maximum == m_minimum)
                              ? 0.0f
                              : static_cast<float>(m_value - m_minimum) / static_cast<float>(m_maximum - m_minimum);
            int handleCenterX = groove.x + static_cast<int>(ratio * groove.width);
            int handleY = bounds.y + (bounds.height - m_handleSize) / 2;
            return SwRect{handleCenterX - m_handleSize / 2, handleY, m_handleSize, m_handleSize};
        }

        float ratio = (m_maximum == m_minimum)
                          ? 0.0f
                          : static_cast<float>(m_value - m_minimum) / static_cast<float>(m_maximum - m_minimum);
        int handleCenterY = groove.y + groove.height - static_cast<int>(ratio * groove.height);
        int handleX = bounds.x + (bounds.width - m_handleSize) / 2;
        return SwRect{handleX, handleCenterY - m_handleSize / 2, m_handleSize, m_handleSize};
    }

    Orientation m_orientation;
    int m_minimum;
    int m_maximum;
    int m_value;
    int m_step;
    bool m_dragging;
    int m_handleSize;
};

