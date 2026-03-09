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
 * @file src/core/gui/SwPainter.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwPainter in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the painter interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwPainter.
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
 * Platform-agnostic painter interface used by widgets and styles.
 **************************************************************************************************/

#include "Sw.h"
#include "SwString.h"
#include "SwFont.h"

#include <algorithm>
#include <vector>

class SwImage;

class SwPainter {
public:
    /**
     * @brief Destroys the `SwPainter` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwPainter() = default;

    /**
     * @brief Clears the current object state.
     * @param color Value passed to the method.
     * @return The requested clear.
     */
    virtual void clear(const SwColor& color) = 0;
    /**
     * @brief Performs the `fillRect` operation.
     * @param rect Rectangle used by the operation.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested fill Rect.
     */
    virtual void fillRect(const SwRect& rect,
                          const SwColor& fillColor,
                          const SwColor& borderColor,
                          int borderWidth) = 0;
    /**
     * @brief Performs the `fillRoundedRect` operation.
     * @param rect Rectangle used by the operation.
     * @param radius Value passed to the method.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested fill Rounded Rect.
     */
    virtual void fillRoundedRect(const SwRect& rect,
                                 int radius,
                                 const SwColor& fillColor,
                                 const SwColor& borderColor,
                                 int borderWidth) = 0;

    // Per-corner variant: radii are top-left, top-right, bottom-right, bottom-left.
    /**
     * @brief Performs the `fillRoundedRect` operation.
     * @param rect Rectangle used by the operation.
     * @param radiusTL Value passed to the method.
     * @param radiusTR Value passed to the method.
     * @param radiusBR Value passed to the method.
     * @param radiusBL Value passed to the method.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested fill Rounded Rect.
     */
    virtual void fillRoundedRect(const SwRect& rect,
                                 int radiusTL, int radiusTR, int radiusBR, int radiusBL,
                                 const SwColor& fillColor,
                                 const SwColor& borderColor,
                                 int borderWidth) {
        // Default: use max radius as uniform fallback.
        int maxR = std::max({radiusTL, radiusTR, radiusBR, radiusBL});
        fillRoundedRect(rect, maxR, fillColor, borderColor, borderWidth);
    }

    /**
     * @brief Performs the `drawRect` operation.
     * @param rect Rectangle used by the operation.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested draw Rect.
     */
    virtual void drawRect(const SwRect& rect,
                          const SwColor& borderColor,
                          int borderWidth) = 0;

    // Dashed rectangle outline. Default implementation uses drawLine segments.
    /**
     * @brief Performs the `drawDashedRect` operation.
     * @param rect Rectangle used by the operation.
     * @param color Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @param dashLen Value passed to the method.
     * @param gapLen Value passed to the method.
     * @return The requested draw Dashed Rect.
     */
    virtual void drawDashedRect(const SwRect& rect,
                                const SwColor& color,
                                int borderWidth = 1,
                                int dashLen = 4,
                                int gapLen = 4) {
        const int x0 = rect.x, y0 = rect.y;
        const int x1 = rect.x + rect.width, y1 = rect.y + rect.height;
        const int period = dashLen + gapLen;
        for (int x = x0; x < x1; x += period) { drawLine(x, y0, std::min(x + dashLen, x1), y0, color, borderWidth); }
        for (int x = x0; x < x1; x += period) { drawLine(x, y1, std::min(x + dashLen, x1), y1, color, borderWidth); }
        for (int y = y0; y < y1; y += period) { drawLine(x0, y, x0, std::min(y + dashLen, y1), color, borderWidth); }
        for (int y = y0; y < y1; y += period) { drawLine(x1, y, x1, std::min(y + dashLen, y1), color, borderWidth); }
    }

    /**
     * @brief Performs the `drawLine` operation.
     * @param x1 Value passed to the method.
     * @param y1 Value passed to the method.
     * @param x2 Value passed to the method.
     * @param y2 Value passed to the method.
     * @param color Value passed to the method.
     * @param width Width value.
     * @return The requested draw Line.
     */
    virtual void drawLine(int x1,
                          int y1,
                          int x2,
                          int y2,
                          const SwColor& color,
                          int width) = 0;

    /**
     * @brief Performs the `fillPolygon` operation.
     * @param points Value passed to the method.
     * @param count Value passed to the method.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested fill Polygon.
     */
    virtual void fillPolygon(const SwPoint* points,
                             int count,
                             const SwColor& fillColor,
                             const SwColor& borderColor,
                             int borderWidth) {
        if (!points || count <= 0) {
            return;
        }
        int minX = points[0].x;
        int maxX = points[0].x;
        int minY = points[0].y;
        int maxY = points[0].y;
        for (int i = 1; i < count; ++i) {
            if (points[i].x < minX) minX = points[i].x;
            if (points[i].x > maxX) maxX = points[i].x;
            if (points[i].y < minY) minY = points[i].y;
            if (points[i].y > maxY) maxY = points[i].y;
        }
        SwRect r{minX, minY, maxX - minX, maxY - minY};
        fillRect(r, fillColor, borderColor, borderWidth);
    }

    /**
     * @brief Performs the `pushClipRect` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested push Clip Rect.
     */
    virtual void pushClipRect(const SwRect& rect) { SW_UNUSED(rect) }
    /**
     * @brief Returns the current pop Clip Rect.
     * @return The current pop Clip Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void popClipRect() {}

    /**
     * @brief Performs the `fillEllipse` operation.
     * @param rect Rectangle used by the operation.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested fill Ellipse.
     */
    virtual void fillEllipse(const SwRect& rect,
                             const SwColor& fillColor,
                             const SwColor& borderColor,
                             int borderWidth) {
        fillRect(rect, fillColor, borderColor, borderWidth);
    }

    /**
     * @brief Performs the `drawEllipse` operation.
     * @param rect Rectangle used by the operation.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested draw Ellipse.
     */
    virtual void drawEllipse(const SwRect& rect,
                             const SwColor& borderColor,
                             int borderWidth) {
        drawRect(rect, borderColor, borderWidth);
    }

    // Minimal image hook for the graphics module (ARGB32).
    /**
     * @brief Performs the `drawImage` operation.
     * @param targetRect Value passed to the method.
     * @param image Value passed to the method.
     * @param sourceRect Value passed to the method.
     * @return The requested draw Image.
     */
    virtual void drawImage(const SwRect& targetRect,
                           const SwImage& image,
                           const SwRect* sourceRect = nullptr) {
        SW_UNUSED(targetRect)
        SW_UNUSED(image)
        SW_UNUSED(sourceRect)
    }

    /**
     * @brief Performs the `drawText` operation.
     * @param rect Rectangle used by the operation.
     * @param text Value passed to the method.
     * @param alignment Value passed to the method.
     * @param color Value passed to the method.
     * @param font Font value used by the operation.
     * @return The requested draw Text.
     */
    virtual void drawText(const SwRect& rect,
                          const SwString& text,
                          DrawTextFormats alignment,
                          const SwColor& color,
                          const SwFont& font) = 0;
    /**
     * @brief Returns the current finalize.
     * @return The current finalize.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void finalize() {}
    /**
     * @brief Returns the current native Handle.
     * @return The current native Handle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void* nativeHandle() { return nullptr; }
};

class SwOffsetPainter final : public SwPainter {
public:
    SwOffsetPainter(SwPainter* base, int dx, int dy)
        : m_base(base), m_dx(dx), m_dy(dy) {}

    void clear(const SwColor& color) override {
        if (m_base) {
            m_base->clear(color);
        }
    }

    void fillRect(const SwRect& rect,
                  const SwColor& fillColor,
                  const SwColor& borderColor,
                  int borderWidth) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->fillRect(r, fillColor, borderColor, borderWidth);
    }

    void fillRoundedRect(const SwRect& rect,
                         int radius,
                         const SwColor& fillColor,
                         const SwColor& borderColor,
                         int borderWidth) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->fillRoundedRect(r, radius, fillColor, borderColor, borderWidth);
    }

    void drawRect(const SwRect& rect,
                  const SwColor& borderColor,
                  int borderWidth) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->drawRect(r, borderColor, borderWidth);
    }

    void drawDashedRect(const SwRect& rect,
                        const SwColor& color,
                        int borderWidth,
                        int dashLen,
                        int gapLen) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->drawDashedRect(r, color, borderWidth, dashLen, gapLen);
    }

    void drawLine(int x1,
                  int y1,
                  int x2,
                  int y2,
                  const SwColor& color,
                  int width) override {
        if (!m_base) {
            return;
        }
        m_base->drawLine(x1 + m_dx, y1 + m_dy, x2 + m_dx, y2 + m_dy, color, width);
    }

    void fillPolygon(const SwPoint* points,
                     int count,
                     const SwColor& fillColor,
                     const SwColor& borderColor,
                     int borderWidth) override {
        if (!m_base || !points || count <= 0) {
            return;
        }
        std::vector<SwPoint> pts;
        pts.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            pts.push_back(SwPoint{points[i].x + m_dx, points[i].y + m_dy});
        }
        m_base->fillPolygon(pts.data(), count, fillColor, borderColor, borderWidth);
    }

    void pushClipRect(const SwRect& rect) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->pushClipRect(r);
    }

    void popClipRect() override {
        if (m_base) {
            m_base->popClipRect();
        }
    }

    void fillEllipse(const SwRect& rect,
                     const SwColor& fillColor,
                     const SwColor& borderColor,
                     int borderWidth) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->fillEllipse(r, fillColor, borderColor, borderWidth);
    }

    void drawEllipse(const SwRect& rect,
                     const SwColor& borderColor,
                     int borderWidth) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->drawEllipse(r, borderColor, borderWidth);
    }

    void drawImage(const SwRect& targetRect,
                   const SwImage& image,
                   const SwRect* sourceRect = nullptr) override {
        if (!m_base) {
            return;
        }
        SwRect r = targetRect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->drawImage(r, image, sourceRect);
    }

    void drawText(const SwRect& rect,
                  const SwString& text,
                  DrawTextFormats alignment,
                  const SwColor& color,
                  const SwFont& font) override {
        if (!m_base) {
            return;
        }
        SwRect r = rect;
        r.x += m_dx;
        r.y += m_dy;
        m_base->drawText(r, text, alignment, color, font);
    }

    void finalize() override {
        if (m_base) {
            m_base->finalize();
        }
    }

    void* nativeHandle() override {
        return m_base ? m_base->nativeHandle() : nullptr;
    }

private:
    SwPainter* m_base{nullptr};
    int m_dx{0};
    int m_dy{0};
};
