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
 * Virtual joystick widget built on SwWidget.
 *
 * Provides normalized tilt vectors in the [-1, 1] range plus repeat modes to continuously emit
 * control commands while the knob is displaced.
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwPainter.h"
#include "SwTimer.h"

#include <algorithm>
#include <cmath>

class SwJoystick : public SwWidget {
    SW_OBJECT(SwJoystick, SwWidget)

public:
    enum class RepeatMode {
        ValueChangedOnly,
        RepeatIfNotZero,
        Repeat
    };

    explicit SwJoystick(SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_tiltX(0.0f)
        , m_tiltY(0.0f)
        , m_lastEmittedX(0.0f)
        , m_lastEmittedY(0.0f)
        , m_dragging(false)
        , m_zonePadding(24)
        , m_knobRadius(28)
        , m_repeatMode(RepeatMode::ValueChangedOnly)
        , m_repeatInterval(40)
        , m_repeatTimer(new SwTimer(m_repeatInterval, this))
        , m_dirty(false) {
        resize(320, 320);
        setCursor(CursorType::Cross);

        SwObject::connect(m_repeatTimer, &SwTimer::timeout, [this]() {
            emitIfNeeded(true);
            updateRepeatTimerState();
        });
    }

    void setRepeatMode(RepeatMode mode) {
        if (m_repeatMode == mode) {
            return;
        }
        m_repeatMode = mode;
        updateRepeatTimerState();
    }

    RepeatMode repeatMode() const { return m_repeatMode; }

    void setRepeatInterval(int intervalMs) {
        if (intervalMs <= 0) {
            return;
        }
        m_repeatInterval = intervalMs;
        m_repeatTimer->setInterval(m_repeatInterval);
        updateRepeatTimerState();
    }

    int repeatInterval() const { return m_repeatInterval; }

    float horizontalValue() const { return m_tiltX; }
    float verticalValue() const { return m_tiltY; }

    float magnitude() const {
        return std::min(1.0f, std::sqrt(m_tiltX * m_tiltX + m_tiltY * m_tiltY));
    }

    float angleDegrees() const {
        constexpr float kRadToDeg = 57.2957795f;
        if (magnitude() < 1e-3f) {
            return 0.0f;
        }
        return std::atan2(m_tiltY, m_tiltX) * kRadToDeg;
    }

    DECLARE_SIGNAL(moved, float, float);
    DECLARE_SIGNAL(directionChanged, float, float);

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();

        // Keep the joystick circular even if the widget is rectangular.
        const int diameter = std::max(10, std::min(bounds.width, bounds.height));
        const int circleX = bounds.x + (bounds.width - diameter) / 2;
        const int circleY = bounds.y + (bounds.height - diameter) / 2;
        const SwRect outerCircle{circleX, circleY, diameter, diameter};

        const SwColor outerFill{14, 18, 28};
        const SwColor outerBorder{28, 34, 52};
        painter->fillRoundedRect(outerCircle, outerCircle.width / 2, outerFill, outerBorder, 1);

        const int ringPad = 8;
        SwRect innerCircle{outerCircle.x + ringPad,
                           outerCircle.y + ringPad,
                           std::max(0, outerCircle.width - ringPad * 2),
                           std::max(0, outerCircle.height - ringPad * 2)};
        painter->fillRoundedRect(innerCircle,
                                 innerCircle.width / 2,
                                 SwColor{20, 26, 40},
                                 SwColor{46, 56, 84},
                                 1);

        SwRect zone = baseRect();
        const SwColor zoneFill{26, 32, 48};
        const SwColor zoneBorder{66, 88, 140};
        painter->fillRoundedRect(zone, zone.width / 2, zoneFill, zoneBorder, 2);

        const int centerX = zone.x + zone.width / 2;
        const int centerY = zone.y + zone.height / 2;

        // Direction line.
        const SwRect knob = knobRect();
        const int knobCenterX = knob.x + knob.width / 2;
        const int knobCenterY = knob.y + knob.height / 2;
        const SwColor accent{110, 200, 255};
        if (magnitude() > 0.02f) {
            painter->drawLine(centerX, centerY, knobCenterX, knobCenterY, SwColor{54, 140, 210}, 3);
        }

        // Crosshair (subtle).
        painter->drawLine(zone.x + 16, centerY, zone.x + zone.width - 16, centerY, SwColor{44, 56, 84}, 2);
        painter->drawLine(centerX, zone.y + 16, centerX, zone.y + zone.height - 16, SwColor{44, 56, 84}, 2);

        // Knob shadow (fake depth).
        SwRect shadow{knob.x + 2, knob.y + 3, knob.width, knob.height};
        painter->fillRoundedRect(shadow, shadow.width / 2, SwColor{10, 12, 18}, SwColor{10, 12, 18}, 0);

        // Knob body (double-layer for a "premium" feel).
        painter->fillRoundedRect(knob, knob.width / 2, accent, SwColor{18, 22, 32}, 2);
        SwRect knobInner{knob.x + 5, knob.y + 5, std::max(0, knob.width - 10), std::max(0, knob.height - 10)};
        painter->fillRoundedRect(knobInner,
                                 knobInner.width / 2,
                                 SwColor{180, 240, 255},
                                 SwColor{180, 240, 255},
                                 0);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }
        m_dragging = true;
        updateTiltFromPoint(event->x(), event->y());
        emitIfNeeded(m_repeatMode == RepeatMode::ValueChangedOnly);
        updateRepeatTimerState();
        event->accept();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (m_dragging) {
            updateTiltFromPoint(event->x(), event->y());
            emitIfNeeded(m_repeatMode == RepeatMode::ValueChangedOnly);
            event->accept();
        }
        SwWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (m_dragging) {
            m_dragging = false;
            resetStick(true);
            event->accept();
        }
        updateRepeatTimerState();
        SwWidget::mouseReleaseEvent(event);
    }

    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        m_knobRadius = std::max(20, std::min(width(), height()) / 8);
        update();
    }

private:
    SwRect baseRect() const {
        SwRect bounds = getRect();
        int diameter = std::min(bounds.width, bounds.height) - m_zonePadding * 2;
        if (diameter < 10) {
            diameter = 10;
        }
        int baseX = bounds.x + (bounds.width - diameter) / 2;
        int baseY = bounds.y + (bounds.height - diameter) / 2;
        return SwRect{baseX, baseY, diameter, diameter};
    }

    SwRect knobRect() const {
        SwRect zone = baseRect();
        int centerX = zone.x + zone.width / 2;
        int centerY = zone.y + zone.height / 2;
        float radius = static_cast<float>(zone.width / 2 - m_knobRadius);
        if (radius < 1.0f) {
            radius = 1.0f;
        }
        int knobCenterX = centerX + static_cast<int>(m_tiltX * radius);
        int knobCenterY = centerY + static_cast<int>(m_tiltY * radius);
        int diameter = m_knobRadius * 2;
        return SwRect{knobCenterX - m_knobRadius, knobCenterY - m_knobRadius, diameter, diameter};
    }

    void resetStick(bool notify) {
        m_tiltX = 0.0f;
        m_tiltY = 0.0f;
        m_dirty = true;
        update();
        if (notify) {
            emitIfNeeded(true);
        }
    }

    void updateTiltFromPoint(int px, int py) {
        SwRect zone = baseRect();
        int centerX = zone.x + zone.width / 2;
        int centerY = zone.y + zone.height / 2;
        float dx = static_cast<float>(px - centerX);
        float dy = static_cast<float>(py - centerY);
        float radius = static_cast<float>(zone.width / 2 - m_knobRadius);
        if (radius <= 0.5f) {
            radius = 0.5f;
        }
        float newX = dx / radius;
        float newY = dy / radius;
        float len = std::sqrt(newX * newX + newY * newY);
        if (len > 1.0f) {
            newX /= len;
            newY /= len;
        }

        if (std::fabs(newX - m_tiltX) > 0.001f || std::fabs(newY - m_tiltY) > 0.001f) {
            m_tiltX = std::max(-1.0f, std::min(1.0f, newX));
            m_tiltY = std::max(-1.0f, std::min(1.0f, newY));
            m_dirty = true;
            update();
        }
    }

    void emitIfNeeded(bool forced) {
        if (!forced && !m_dirty) {
            return;
        }
        float mag = magnitude();
        float angleDeg = angleDegrees();
        moved(m_tiltX, m_tiltY);
        directionChanged(angleDeg, mag);
        m_lastEmittedX = m_tiltX;
        m_lastEmittedY = m_tiltY;
        m_dirty = false;
    }

    void updateRepeatTimerState() {
        bool shouldRun = false;
        switch (m_repeatMode) {
        case RepeatMode::ValueChangedOnly:
            shouldRun = false;
            break;
        case RepeatMode::RepeatIfNotZero:
            shouldRun = (magnitude() > 0.001f);
            break;
        case RepeatMode::Repeat:
            shouldRun = m_dragging;
            break;
        }

        if (shouldRun) {
            if (!m_repeatTimer->isActive()) {
                m_repeatTimer->start(m_repeatInterval);
            }
        } else {
            if (m_repeatTimer->isActive()) {
                m_repeatTimer->stop();
            }
        }
    }

    float m_tiltX;
    float m_tiltY;
    float m_lastEmittedX;
    float m_lastEmittedY;
    bool m_dragging;
    int m_zonePadding;
    int m_knobRadius;
    RepeatMode m_repeatMode;
    int m_repeatInterval;
    SwTimer* m_repeatTimer;
    bool m_dirty;
};
