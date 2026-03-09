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
 * @file src/core/gui/SwScrollBar.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwScrollBar in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the scroll bar interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwScrollBar.
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
 * SwScrollBar - scrollbar widget.
 **************************************************************************************************/

#include "SwWidget.h"

class SwScrollBar : public SwWidget {
    SW_OBJECT(SwScrollBar, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    /**
     * @brief Constructs a `SwScrollBar` instance.
     * @param orientation Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param orientation Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwScrollBar(Orientation orientation = Orientation::Vertical, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation) {
        initDefaults();
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
     * @brief Sets the page Step.
     * @param step Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPageStep(int step) {
        m_pageStep = std::max(0, step);
        update();
    }

    /**
     * @brief Returns the current page Step.
     * @return The current page Step.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int pageStep() const { return m_pageStep; }

    /**
     * @brief Sets the single Step.
     * @param step Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSingleStep(int step) {
        m_singleStep = std::max(1, step);
    }

    /**
     * @brief Returns the current single Step.
     * @return The current single Step.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int singleStep() const { return m_singleStep; }

    /**
     * @brief Sets the value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setValue(int value) {
        const int clamped = clampInt(value, m_minimum, m_maximum);
        if (m_value == clamped) {
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

    /**
     * @brief Returns whether the object reports slider Down.
     * @return `true` when the object reports slider Down; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isSliderDown() const { return m_dragging; }

    DECLARE_SIGNAL(valueChanged, int);
    DECLARE_SIGNAL(rangeChanged, int, int);
    DECLARE_SIGNAL(sliderPressed, int);
    DECLARE_SIGNAL(sliderMoved, int);
    DECLARE_SIGNAL(sliderReleased, int);

protected:
    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
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

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const SwRect bounds = rect();
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

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        const bool horizontalRequest = event->isShiftPressed();
        if ((horizontalRequest && m_orientation != Orientation::Horizontal)
            || (!horizontalRequest && m_orientation != Orientation::Vertical)) {
            SwWidget::wheelEvent(event);
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
        const SwRect bounds = rect();
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

