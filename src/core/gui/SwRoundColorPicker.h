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
 * @file src/core/gui/SwRoundColorPicker.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwRoundColorPicker in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the round color picker interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwRoundColorPicker.
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
 * SwRoundColorPicker - Hue wheel + saturation/value disk.
 *
 * No external GUI dependency: rendering is done via SwPainter::drawImage on SwImage buffers.
 *
 * Interaction:
 * - Drag on the ring to change Hue (0..360Â°)
 * - Drag inside the disk to change Saturation/Value (mapped to a circular area like the reference implementation)
 **************************************************************************************************/

#include "SwWidget.h"
#include "graphics/SwImage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

class SwRoundColorPicker : public SwWidget {
    SW_OBJECT(SwRoundColorPicker, SwWidget)

public:
    /**
     * @brief Constructs a `SwRoundColorPicker` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwRoundColorPicker(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setMinimumSize(140, 140);
        resize(260, 260);
        setCursor(CursorType::Cross);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setStyleSheet("SwRoundColorPicker { background-color: rgba(0,0,0,0); border-width: 0px; }");

        setColor(SwColor{59, 130, 246});
    }

    /**
     * @brief Sets the color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColor(const SwColor& color) {
        if (sameColor(m_color, color)) {
            return;
        }
        m_color = color;
        syncHsvFromColor();
        regenerateSvImage();
        update();
        colorChanged(m_color);
    }

    /**
     * @brief Returns the current color.
     * @return The current color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor color() const { return m_color; }

    DECLARE_SIGNAL(colorChanged, const SwColor&);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateMetrics();
        regenerateHueImage();
        regenerateSvImage();
    }

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

        const SwRect rect = this->rect();
        const int wheelSize = m_wheelSize;
        if (wheelSize <= 0) {
            return;
        }

        const int wheelX = rect.x + (rect.width - wheelSize) / 2;
        const int wheelY = rect.y + (rect.height - wheelSize) / 2;
        const SwRect wheelRect{wheelX, wheelY, wheelSize, wheelSize};

        if (!m_hueImage.isNull()) {
            painter->drawImage(wheelRect, m_hueImage);
        }
        if (!m_svImage.isNull()) {
            painter->drawImage(wheelRect, m_svImage);
        }

        const int cx = wheelX + wheelSize / 2;
        const int cy = wheelY + wheelSize / 2;
        const SwRect svRect{cx - m_svRadius, cy - m_svRadius, m_svRadius * 2, m_svRadius * 2};
        painter->drawEllipse(svRect, SwColor{226, 232, 240}, 1);

        drawHueCursor(painter, cx, cy);
        drawSvCursor(painter, cx, cy);
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

        const int hit = hitTest(event->x(), event->y());
        if (hit == 1) {
            m_draggingHue = true;
            m_draggingSv = false;
            updateHueFromPoint(event->x(), event->y());
            event->accept();
            return;
        }
        if (hit == 2) {
            m_draggingHue = false;
            m_draggingSv = true;
            updateSvFromPoint(event->x(), event->y());
            event->accept();
            return;
        }

        SwWidget::mousePressEvent(event);
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

        if (m_draggingHue) {
            updateHueFromPoint(event->x(), event->y());
            event->accept();
            return;
        }
        if (m_draggingSv) {
            updateSvFromPoint(event->x(), event->y());
            event->accept();
            return;
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
        if (m_draggingHue || m_draggingSv) {
            m_draggingHue = false;
            m_draggingSv = false;
            if (event) {
                event->accept();
            }
            return;
        }
        SwWidget::mouseReleaseEvent(event);
    }

private:
    struct Hsv {
        double h{0.0}; // degrees [0..360)
        double s{1.0}; // [0..1]
        double v{1.0}; // [0..1]
    };

    static bool sameColor(const SwColor& a, const SwColor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    static std::uint32_t packArgb(int a, int r, int g, int b) {
        const std::uint32_t aa = static_cast<std::uint32_t>(clampInt(a, 0, 255));
        const std::uint32_t rr = static_cast<std::uint32_t>(clampInt(r, 0, 255));
        const std::uint32_t gg = static_cast<std::uint32_t>(clampInt(g, 0, 255));
        const std::uint32_t bb = static_cast<std::uint32_t>(clampInt(b, 0, 255));
        return (aa << 24) | (rr << 16) | (gg << 8) | bb;
    }

    static Hsv rgbToHsv(const SwColor& rgb) {
        const double r = clampInt(rgb.r, 0, 255) / 255.0;
        const double g = clampInt(rgb.g, 0, 255) / 255.0;
        const double b = clampInt(rgb.b, 0, 255) / 255.0;

        const double maxv = std::max(r, std::max(g, b));
        const double minv = std::min(r, std::min(g, b));
        const double delta = maxv - minv;

        double h = 0.0;
        if (delta > 1e-9) {
            if (maxv == r) {
                h = 60.0 * std::fmod(((g - b) / delta), 6.0);
            } else if (maxv == g) {
                h = 60.0 * (((b - r) / delta) + 2.0);
            } else {
                h = 60.0 * (((r - g) / delta) + 4.0);
            }
        }
        if (h < 0.0) {
            h += 360.0;
        }

        double s = 0.0;
        if (maxv > 1e-9) {
            s = delta / maxv;
        }

        Hsv out;
        out.h = h;
        out.s = s;
        out.v = maxv;
        return out;
    }

    static SwColor hsvToRgb(double h, double s, double v) {
        const double hh = clampDouble(h, 0.0, 359.999);
        const double ss = clampDouble(s, 0.0, 1.0);
        const double vv = clampDouble(v, 0.0, 1.0);

        const double c = vv * ss;
        const double hPrime = hh / 60.0;
        const double x = c * (1.0 - std::abs(std::fmod(hPrime, 2.0) - 1.0));
        const double m = vv - c;

        double r1 = 0.0;
        double g1 = 0.0;
        double b1 = 0.0;

        if (hPrime >= 0.0 && hPrime < 1.0) {
            r1 = c;
            g1 = x;
            b1 = 0.0;
        } else if (hPrime < 2.0) {
            r1 = x;
            g1 = c;
            b1 = 0.0;
        } else if (hPrime < 3.0) {
            r1 = 0.0;
            g1 = c;
            b1 = x;
        } else if (hPrime < 4.0) {
            r1 = 0.0;
            g1 = x;
            b1 = c;
        } else if (hPrime < 5.0) {
            r1 = x;
            g1 = 0.0;
            b1 = c;
        } else {
            r1 = c;
            g1 = 0.0;
            b1 = x;
        }

        SwColor rgb;
        rgb.r = clampInt(static_cast<int>(std::round((r1 + m) * 255.0)), 0, 255);
        rgb.g = clampInt(static_cast<int>(std::round((g1 + m) * 255.0)), 0, 255);
        rgb.b = clampInt(static_cast<int>(std::round((b1 + m) * 255.0)), 0, 255);
        return rgb;
    }

    void syncHsvFromColor() {
        const Hsv hsv = rgbToHsv(m_color);
        m_hue = hsv.h;
        m_saturation = hsv.s;
        m_value = hsv.v;
    }

    void updateMetrics() {
        const int w = width();
        const int h = height();
        const int minDim = std::max(0, std::min(w, h));
        m_wheelSize = minDim;
        if (m_wheelSize <= 0) {
            m_outerRadius = 0;
            m_ringThickness = 0;
            m_margin = 0;
            m_svRadius = 0;
            return;
        }

        m_outerRadius = std::max(1, m_wheelSize / 2 - 1);
        m_ringThickness = std::max(10, m_wheelSize / 10);
        m_margin = std::max(6, m_wheelSize / 20);

        const int maxRing = std::max(1, m_outerRadius - 12);
        if (m_ringThickness > maxRing) {
            m_ringThickness = maxRing;
        }

        const int maxMargin = std::max(1, m_outerRadius - m_ringThickness - 6);
        if (m_margin > maxMargin) {
            m_margin = maxMargin;
        }

        m_svRadius = std::max(1, m_outerRadius - m_ringThickness - m_margin);
    }

    void regenerateHueImage() {
        if (m_wheelSize <= 0) {
            m_hueImage.create(0, 0);
            return;
        }

        m_hueImage.create(m_wheelSize, m_wheelSize, SwImage::Format_ARGB32);
        m_hueImage.fill(0x00000000u);

        const double cx = static_cast<double>(m_wheelSize) / 2.0;
        const double cy = static_cast<double>(m_wheelSize) / 2.0;
        const double outer = static_cast<double>(m_outerRadius);
        const double inner = static_cast<double>(m_outerRadius - m_ringThickness);
        const double aa = 1.0;
        const double pi = 3.14159265358979323846;

        for (int y = 0; y < m_wheelSize; ++y) {
            std::uint32_t* row = m_hueImage.scanLine(y);
            if (!row) {
                continue;
            }
            for (int x = 0; x < m_wheelSize; ++x) {
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double dist = std::sqrt(dx * dx + dy * dy);

                if (dist < inner - aa || dist > outer + aa) {
                    continue;
                }

                double alphaOuter = clampDouble((outer + aa - dist) / aa, 0.0, 1.0);
                double alphaInner = clampDouble((dist - (inner - aa)) / aa, 0.0, 1.0);
                const int alpha = static_cast<int>(std::round(255.0 * alphaOuter * alphaInner));
                if (alpha <= 0) {
                    continue;
                }

                double angle = std::atan2(dy, dx) * (180.0 / pi);
                if (angle < 0.0) {
                    angle += 360.0;
                }

                const SwColor rgb = hsvToRgb(angle, 1.0, 1.0);
                row[x] = packArgb(alpha, rgb.r, rgb.g, rgb.b);
            }
        }
    }

    void regenerateSvImage() {
        if (m_wheelSize <= 0) {
            m_svImage.create(0, 0);
            return;
        }

        m_svImage.create(m_wheelSize, m_wheelSize, SwImage::Format_ARGB32);
        m_svImage.fill(0x00000000u);

        const double cx = static_cast<double>(m_wheelSize) / 2.0;
        const double cy = static_cast<double>(m_wheelSize) / 2.0;
        const double radius = static_cast<double>(m_svRadius);
        const double aa = 1.0;
        const double invSqrt2 = 1.0 / 1.41421356237;

        for (int y = 0; y < m_wheelSize; ++y) {
            std::uint32_t* row = m_svImage.scanLine(y);
            if (!row) {
                continue;
            }
            for (int x = 0; x < m_wheelSize; ++x) {
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double dist = std::sqrt(dx * dx + dy * dy);

                if (dist > radius + aa) {
                    continue;
                }

                const double alphaF = clampDouble((radius + aa - dist) / aa, 0.0, 1.0);
                const int alpha = static_cast<int>(std::round(255.0 * alphaF));
                if (alpha <= 0) {
                    continue;
                }

                const double nx = radius > 0.0 ? dx / radius : 0.0;
                const double ny = radius > 0.0 ? dy / radius : 0.0;

                double s = 0.5 + (nx * invSqrt2);
                double v = 0.5 - (ny * invSqrt2);
                s = clampDouble(s, 0.0, 1.0);
                v = clampDouble(v, 0.0, 1.0);

                const SwColor rgb = hsvToRgb(m_hue, s, v);
                row[x] = packArgb(alpha, rgb.r, rgb.g, rgb.b);
            }
        }
    }

    void drawCursor(SwPainter* painter, int cx, int cy) const {
        if (!painter) {
            return;
        }
        const int size = 14;
        const SwRect r{cx - size / 2, cy - size / 2, size, size};
        painter->fillEllipse(r, SwColor{255, 255, 255}, SwColor{15, 23, 42}, 2);
    }

    void drawHueCursor(SwPainter* painter, int cx, int cy) const {
        const double pi = 3.14159265358979323846;
        const double rad = clampDouble(m_hue, 0.0, 359.999) * (pi / 180.0);

        const double ringCenter = static_cast<double>(m_outerRadius - m_ringThickness / 2);
        const int px = static_cast<int>(std::round(static_cast<double>(cx) + std::cos(rad) * ringCenter));
        const int py = static_cast<int>(std::round(static_cast<double>(cy) + std::sin(rad) * ringCenter));
        drawCursor(painter, px, py);
    }

    void drawSvCursor(SwPainter* painter, int cx, int cy) const {
        static const double sqrt2 = 1.41421356237;
        const double nx = (m_saturation - 0.5) * sqrt2;
        const double ny = (0.5 - m_value) * sqrt2;
        const int px = static_cast<int>(std::round(static_cast<double>(cx) + nx * static_cast<double>(m_svRadius)));
        const int py = static_cast<int>(std::round(static_cast<double>(cy) + ny * static_cast<double>(m_svRadius)));
        drawCursor(painter, px, py);
    }

    void updateHueFromPoint(int x, int y) {
        const SwRect rect = this->rect();
        const int wheelX = rect.x + (rect.width - m_wheelSize) / 2;
        const int wheelY = rect.y + (rect.height - m_wheelSize) / 2;
        const int cx = wheelX + m_wheelSize / 2;
        const int cy = wheelY + m_wheelSize / 2;

        const double dx = static_cast<double>(x - cx);
        const double dy = static_cast<double>(y - cy);
        if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) {
            return;
        }

        const double pi = 3.14159265358979323846;
        double deg = std::atan2(dy, dx) * (180.0 / pi);
        if (deg < 0.0) {
            deg += 360.0;
        }
        m_hue = deg;
        regenerateSvImage();
        applyHsvToColor();
    }

    void updateSvFromPoint(int x, int y) {
        const SwRect rect = this->rect();
        const int wheelX = rect.x + (rect.width - m_wheelSize) / 2;
        const int wheelY = rect.y + (rect.height - m_wheelSize) / 2;
        const int cx = wheelX + m_wheelSize / 2;
        const int cy = wheelY + m_wheelSize / 2;

        double dx = static_cast<double>(x - cx);
        double dy = static_cast<double>(y - cy);
        const double dist = std::sqrt(dx * dx + dy * dy);
        const double radius = static_cast<double>(m_svRadius);
        if (dist > radius && dist > 1e-9) {
            const double ratio = radius / dist;
            dx *= ratio;
            dy *= ratio;
        }

        const double invSqrt2 = 1.0 / 1.41421356237;
        const double nx = radius > 0.0 ? dx / radius : 0.0;
        const double ny = radius > 0.0 ? dy / radius : 0.0;

        double s = 0.5 + (nx * invSqrt2);
        double v = 0.5 - (ny * invSqrt2);
        s = clampDouble(s, 0.0, 1.0);
        v = clampDouble(v, 0.0, 1.0);

        m_saturation = s;
        m_value = v;
        applyHsvToColor();
    }

    void applyHsvToColor() {
        const SwColor rgb = hsvToRgb(m_hue, m_saturation, m_value);
        if (sameColor(m_color, rgb)) {
            update();
            return;
        }
        m_color = rgb;
        update();
        colorChanged(m_color);
    }

    int hitTest(int px, int py) const {
        if (m_wheelSize <= 0) {
            return 0;
        }
        const SwRect rect = this->rect();
        const int wheelX = rect.x + (rect.width - m_wheelSize) / 2;
        const int wheelY = rect.y + (rect.height - m_wheelSize) / 2;
        const int cx = wheelX + m_wheelSize / 2;
        const int cy = wheelY + m_wheelSize / 2;

        const double dx = static_cast<double>(px - cx);
        const double dy = static_cast<double>(py - cy);
        const double dist = std::sqrt(dx * dx + dy * dy);

        const double outer = static_cast<double>(m_outerRadius);
        const double inner = static_cast<double>(m_outerRadius - m_ringThickness);
        if (dist >= inner && dist <= outer) {
            return 1; // hue ring
        }
        if (dist <= static_cast<double>(m_svRadius)) {
            return 2; // sv disk
        }
        return 0;
    }

    SwColor m_color{0, 0, 0};
    double m_hue{0.0};
    double m_saturation{1.0};
    double m_value{1.0};

    bool m_draggingHue{false};
    bool m_draggingSv{false};

    int m_wheelSize{0};
    int m_outerRadius{0};
    int m_ringThickness{0};
    int m_margin{0};
    int m_svRadius{0};

    SwImage m_hueImage;
    SwImage m_svImage;
};

