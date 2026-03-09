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
 * @file src/platform/x11/SwX11Painter.h
 * @ingroup platform_backends
 * @brief Declares the public interface exposed by SwX11Painter in the CoreSw X11 platform
 * integration layer.
 *
 * This header belongs to the CoreSw X11 platform integration layer. It binds portable framework
 * abstractions to concrete X11 windowing, painting, and input services.
 *
 * Within that layer, this file focuses on the X11 painter interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwX11Painter.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Types here define the seam between portable APIs and the native event and rendering loop on X11
 * systems.
 *
 */


/***************************************************************************************************
 * Minimal X11-backed implementation of SwPainter so widgets can render without touching GDI.
 **************************************************************************************************/

#if defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string>
#include <vector>
#include <cmath>

#include "core/gui/SwPainter.h"
#include "core/gui/graphics/SwImage.h"
#include "core/types/SwString.h"
#include "core/gui/SwWidgetPlatformAdapter.h"

class SwX11Painter : public SwPainter {
public:
    /**
     * @brief Constructs a `SwX11Painter` instance.
     * @param display Value passed to the method.
     * @param window Value passed to the method.
     * @param width Width value.
     * @param height Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwX11Painter(Display* display, ::Window window, int width, int height)
        : m_display(display), m_window(window), m_width(width), m_height(height) {
        ensureDrawable();
    }

    /**
     * @brief Destroys the `SwX11Painter` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwX11Painter() override {
        presentIfNeeded_();
        if (m_display && m_gc) {
            XFreeGC(m_display, m_gc);
        }
        if (m_display && m_presentGC) {
            XFreeGC(m_display, m_presentGC);
        }
        if (m_display && m_backBuffer) {
            XFreePixmap(m_display, m_backBuffer);
        }
    }

    /**
     * @brief Clears the current object state.
     * @param color Value passed to the method.
     */
    void clear(const SwColor& color) override {
        if (!ensureDrawable()) {
            return;
        }
        setForeground(color);
        XFillRectangle(m_display,
                       targetDrawable(),
                       m_gc,
                       0,
                       0,
                       static_cast<unsigned int>(m_width),
                       static_cast<unsigned int>(m_height));
    }

    /**
     * @brief Performs the `fillRect` operation.
     * @param rect Rectangle used by the operation.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     */
    void fillRect(const SwRect& rect,
                  const SwColor& fillColor,
                  const SwColor& borderColor,
                  int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }
        setForeground(fillColor);
        XFillRectangle(m_display,
                       targetDrawable(),
                       m_gc,
                       rect.x,
                       rect.y,
                       static_cast<unsigned int>(rect.width),
                       static_cast<unsigned int>(rect.height));
        if (borderWidth > 0) {
            drawRect(rect, borderColor, borderWidth);
        }
    }

    /**
     * @brief Performs the `fillRoundedRect` operation.
     * @param rect Rectangle used by the operation.
     * @param radius Value passed to the method.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     */
    void fillRoundedRect(const SwRect& rect,
                         int radius,
                         const SwColor& fillColor,
                         const SwColor& borderColor,
                         int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }

        const int r = std::max(0, radius);
        const int diameter = std::min({rect.width, rect.height, r * 2});
        const int actualRadius = diameter / 2;

        if (actualRadius == 0) {
            fillRect(rect, fillColor, borderColor, borderWidth);
            return;
        }

        const int segments = std::max(12, actualRadius * 3);
        auto buildOutline = [&](std::vector<XPoint>& points) {
            points.clear();
            points.reserve(segments * 4 + 1);

            const int left = rect.x;
            const int right = rect.x + rect.width;
            const int top = rect.y;
            const int bottom = rect.y + rect.height;

            auto addArc = [&](double start, double end, int cx, int cy) {
                for (int i = 0; i <= segments; ++i) {
                    double t = start + (end - start) * (static_cast<double>(i) / segments);
                    double px = cx + actualRadius * std::cos(t);
                    double py = cy + actualRadius * std::sin(t);
                    points.push_back(XPoint{static_cast<short>(std::lround(px)),
                                            static_cast<short>(std::lround(py))});
                }
            };

            addArc(M_PI, M_PI * 1.5, left + actualRadius, top + actualRadius);                 // top-left
            addArc(M_PI * 1.5, M_PI * 2.0, right - actualRadius, top + actualRadius);          // top-right
            addArc(0.0, M_PI_2, right - actualRadius, bottom - actualRadius);                  // bottom-right
            addArc(M_PI_2, M_PI, left + actualRadius, bottom - actualRadius);                  // bottom-left
        };

        std::vector<XPoint> outline;
        buildOutline(outline);

        setForeground(fillColor);
        XFillPolygon(m_display,
                     targetDrawable(),
                     m_gc,
                     outline.data(),
                     static_cast<int>(outline.size()),
                     Complex,
                     CoordModeOrigin);

        if (borderWidth > 0) {
            setForeground(borderColor);
            XSetLineAttributes(m_display,
                               m_gc,
                               borderWidth > 0 ? borderWidth : 1,
                               LineSolid,
                               CapRound,
                               JoinRound);
            outline.push_back(outline.front());
            XDrawLines(m_display,
                       targetDrawable(),
                       m_gc,
                       outline.data(),
                       static_cast<int>(outline.size()),
                       CoordModeOrigin);
        }
    }

    /**
     * @brief Performs the `drawRect` operation.
     * @param rect Rectangle used by the operation.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     */
    void drawRect(const SwRect& rect,
                  const SwColor& borderColor,
                  int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }
        setForeground(borderColor);
        XSetLineAttributes(m_display, m_gc, borderWidth > 0 ? borderWidth : 1, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(m_display,
                       targetDrawable(),
                       m_gc,
                       rect.x,
                       rect.y,
                       static_cast<unsigned int>(rect.width),
                       static_cast<unsigned int>(rect.height));
    }

    /**
     * @brief Performs the `drawLine` operation.
     * @param x1 Value passed to the method.
     * @param y1 Value passed to the method.
     * @param x2 Value passed to the method.
     * @param y2 Value passed to the method.
     * @param color Value passed to the method.
     * @param width Width value.
     */
    void drawLine(int x1,
                  int y1,
                  int x2,
                  int y2,
                  const SwColor& color,
                  int width) override {
        if (!ensureDrawable()) {
            return;
        }
        setForeground(color);
        XSetLineAttributes(m_display, m_gc, width > 0 ? width : 1, LineSolid, CapRound, JoinRound);
        XDrawLine(m_display, targetDrawable(), m_gc, x1, y1, x2, y2);
    }

    /**
     * @brief Performs the `fillPolygon` operation.
     * @param points Value passed to the method.
     * @param count Value passed to the method.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     */
    void fillPolygon(const SwPoint* points,
                     int count,
                     const SwColor& fillColor,
                     const SwColor& borderColor,
                     int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }
        if (!points || count < 3) {
            return;
        }

        std::vector<XPoint> pts;
        pts.reserve(static_cast<size_t>(count) + 1);
        for (int i = 0; i < count; ++i) {
            pts.push_back(XPoint{static_cast<short>(points[i].x),
                                 static_cast<short>(points[i].y)});
        }

        setForeground(fillColor);
        XFillPolygon(m_display,
                     targetDrawable(),
                     m_gc,
                     pts.data(),
                     count,
                     Complex,
                     CoordModeOrigin);

        if (borderWidth > 0) {
            setForeground(borderColor);
            XSetLineAttributes(m_display,
                               m_gc,
                               borderWidth,
                               LineSolid,
                               CapRound,
                               JoinRound);
            pts.push_back(pts.front());
            XDrawLines(m_display,
                       targetDrawable(),
                       m_gc,
                       pts.data(),
                       static_cast<int>(pts.size()),
                       CoordModeOrigin);
        }
    }

    /**
     * @brief Performs the `pushClipRect` operation.
     * @param rect Rectangle used by the operation.
     */
    void pushClipRect(const SwRect& rect) override {
        if (!ensureDrawable()) {
            return;
        }

        XRectangle clip;
        clip.x = static_cast<short>(rect.x);
        clip.y = static_cast<short>(rect.y);
        clip.width = static_cast<unsigned short>(std::max(0, rect.width));
        clip.height = static_cast<unsigned short>(std::max(0, rect.height));

        if (!m_clipStack.empty()) {
            clip = intersectClip(m_clipStack.back(), clip);
        }

        m_clipStack.push_back(clip);
        XSetClipRectangles(m_display, m_gc, 0, 0, &clip, 1, Unsorted);
    }

    /**
     * @brief Performs the `popClipRect` operation.
     */
    void popClipRect() override {
        if (!m_display || !m_gc) {
            return;
        }
        if (m_clipStack.empty()) {
            return;
        }

        m_clipStack.pop_back();
        if (m_clipStack.empty()) {
            XSetClipMask(m_display, m_gc, None);
        } else {
            XRectangle clip = m_clipStack.back();
            XSetClipRectangles(m_display, m_gc, 0, 0, &clip, 1, Unsorted);
        }
    }

    /**
     * @brief Performs the `fillEllipse` operation.
     * @param rect Rectangle used by the operation.
     * @param fillColor Value passed to the method.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     */
    void fillEllipse(const SwRect& rect,
                     const SwColor& fillColor,
                     const SwColor& borderColor,
                     int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }

        setForeground(fillColor);
        XFillArc(m_display,
                 targetDrawable(),
                 m_gc,
                 rect.x,
                 rect.y,
                 static_cast<unsigned int>(std::max(0, rect.width)),
                 static_cast<unsigned int>(std::max(0, rect.height)),
                 0,
                 360 * 64);

        if (borderWidth > 0) {
            drawEllipse(rect, borderColor, borderWidth);
        }
    }

    /**
     * @brief Performs the `drawEllipse` operation.
     * @param rect Rectangle used by the operation.
     * @param borderColor Value passed to the method.
     * @param borderWidth Value passed to the method.
     */
    void drawEllipse(const SwRect& rect,
                     const SwColor& borderColor,
                     int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }
        const int w = borderWidth > 0 ? borderWidth : 1;
        setForeground(borderColor);
        XSetLineAttributes(m_display, m_gc, w, LineSolid, CapRound, JoinRound);
        XDrawArc(m_display,
                 targetDrawable(),
                 m_gc,
                 rect.x,
                 rect.y,
                 static_cast<unsigned int>(std::max(0, rect.width)),
                 static_cast<unsigned int>(std::max(0, rect.height)),
                 0,
                 360 * 64);
    }

    /**
     * @brief Performs the `drawImage` operation.
     * @param targetRect Value passed to the method.
     * @param image Value passed to the method.
     * @param sourceRect Value passed to the method.
     */
    void drawImage(const SwRect& targetRect,
                   const SwImage& image,
                   const SwRect* sourceRect = nullptr) override {
        if (!ensureDrawable()) {
            return;
        }
        if (image.isNull()) {
            return;
        }

        const int imgW = image.width();
        const int imgH = image.height();
        if (imgW <= 0 || imgH <= 0) {
            return;
        }

        int srcX = 0, srcY = 0, srcW = imgW, srcH = imgH;
        if (sourceRect) {
            srcX = sourceRect->x;
            srcY = sourceRect->y;
            srcW = sourceRect->width;
            srcH = sourceRect->height;
        }

        const int dstW = targetRect.width;
        const int dstH = targetRect.height;
        if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) {
            return;
        }

        const int screen = DefaultScreen(m_display);
        const int depth  = DefaultDepth(m_display, screen);
        Visual* visual   = DefaultVisual(m_display, screen);

        // Build a 32-bpp pixel buffer (native RGB, alpha blended against back-buffer)
        const int stride = dstW * 4;
        std::vector<uint32_t> pixelData(static_cast<size_t>(dstW * dstH));

        // Snapshot the existing back-buffer region so we can alpha-blend against it
        XImage* bgSnap = nullptr;
        if (m_backBuffer) {
            bgSnap = XGetImage(m_display, m_backBuffer,
                               targetRect.x, targetRect.y,
                               static_cast<unsigned int>(dstW),
                               static_cast<unsigned int>(dstH),
                               AllPlanes, ZPixmap);
        }

        for (int row = 0; row < dstH; ++row) {
            const int srcRow = srcY + (dstH == srcH ? row : row * srcH / dstH);
            const int clampedRow = std::min(srcRow, imgH - 1);
            const uint32_t* srcLine = image.constScanLine(clampedRow);
            if (!srcLine) {
                continue;
            }
            uint32_t* dstLine = pixelData.data() + row * dstW;
            for (int col = 0; col < dstW; ++col) {
                const int srcCol = srcX + (dstW == srcW ? col : col * srcW / dstW);
                const int clampedCol = std::min(srcCol, imgW - 1);
                const uint32_t px = srcLine[clampedCol];

                const uint32_t a = (px >> 24) & 0xFFu;
                uint32_t r = (px >> 16) & 0xFFu;
                uint32_t g = (px >>  8) & 0xFFu;
                uint32_t b = (px      ) & 0xFFu;

                // Alpha blend against back-buffer content (read via XGetImage snapshot)
                if (a < 255u) {
                    uint32_t bgR = 249u, bgG = 249u, bgB = 249u;
                    if (bgSnap) {
                        const int bx = col;
                        const int by = row;
                        if (bx >= 0 && bx < dstW && by >= 0 && by < dstH) {
                            const unsigned long bgPx = XGetPixel(bgSnap, bx, by);
                            bgR = (bgPx >> 16) & 0xFFu;
                            bgG = (bgPx >>  8) & 0xFFu;
                            bgB = (bgPx      ) & 0xFFu;
                        }
                    }
                    const uint32_t inv = 255u - a;
                    r = (r * a + bgR * inv) / 255u;
                    g = (g * a + bgG * inv) / 255u;
                    b = (b * a + bgB * inv) / 255u;
                }

                dstLine[col] = (r << 16) | (g << 8) | b;
            }
        }

        if (bgSnap) {
            XDestroyImage(bgSnap);
        }

        XImage* ximg = XCreateImage(m_display,
                                    visual,
                                    static_cast<unsigned int>(depth),
                                    ZPixmap,
                                    0,
                                    reinterpret_cast<char*>(pixelData.data()),
                                    static_cast<unsigned int>(dstW),
                                    static_cast<unsigned int>(dstH),
                                    32,
                                    stride);
        if (!ximg) {
            return;
        }

        XPutImage(m_display,
                  targetDrawable(),
                  m_gc,
                  ximg,
                  0, 0,
                  targetRect.x, targetRect.y,
                  static_cast<unsigned int>(dstW),
                  static_cast<unsigned int>(dstH));

        ximg->data = nullptr; // Don't let XDestroyImage free our buffer
        XDestroyImage(ximg);
    }

    /**
     * @brief Performs the `drawText` operation.
     * @param rect Rectangle used by the operation.
     * @param text Value passed to the method.
     * @param alignment Value passed to the method.
     * @param color Value passed to the method.
     * @param font Font value used by the operation.
     */
    void drawText(const SwRect& rect,
                  const SwString& text,
                  DrawTextFormats alignment,
                  const SwColor& color,
                  const SwFont& font) override {
        if (!ensureDrawable()) {
            return;
        }
        setForeground(color);
        std::string utf8 = text.toStdString();
        if (utf8.empty()) {
            return;
        }

        XFontStruct* resolvedFont = SwLinuxFontCache::acquire(m_display, font);
        int ascent = 0;
        int descent = 0;
        int textWidth = 0;
        int textHeight = 0;
        if (resolvedFont) {
            XSetFont(m_display, m_gc, resolvedFont->fid);
            XCharStruct overall{};
            int direction = 0;
            XTextExtents(resolvedFont,
                         utf8.c_str(),
                         static_cast<int>(utf8.size()),
                         &direction,
                         &ascent,
                         &descent,
                         &overall);
            textWidth = overall.width;
            textHeight = ascent + descent;
        } else {
            textWidth = static_cast<int>(utf8.size()) * 8;
            textHeight = rect.height;
            ascent = textHeight;
        }

        int drawX = rect.x;
        if (alignment.testFlag(DrawTextFormat::Center)) {
            drawX = rect.x + (rect.width - textWidth) / 2;
        } else if (alignment.testFlag(DrawTextFormat::Right)) {
            drawX = rect.x + rect.width - textWidth;
        }

        int baselineY = rect.y + ascent;
        if (alignment.testFlag(DrawTextFormat::VCenter)) {
            baselineY = rect.y + (rect.height - textHeight) / 2 + ascent;
        } else if (alignment.testFlag(DrawTextFormat::Bottom)) {
            baselineY = rect.y + rect.height - descent;
        }

        if (rect.width > 0 && rect.height > 0) {
            pushClipRect(rect);
        }

        XDrawString(m_display,
                    targetDrawable(),
                    m_gc,
                    drawX,
                    baselineY,
                    utf8.c_str(),
                    static_cast<int>(utf8.size()));

        if (rect.width > 0 && rect.height > 0) {
            popClipRect();
        }
    }

    /**
     * @brief Performs the `finalize` operation.
     */
    void finalize() override {
        m_finalizeRequested = true;
        if (!m_deferPresent) {
            presentIfNeeded_();
        }
    }

private:
    void presentIfNeeded_() {
        if (!m_finalizeRequested || m_presented || !m_display) {
            return;
        }
        if (m_backBuffer && m_presentGC) {
            XCopyArea(m_display,
                      m_backBuffer,
                      m_window,
                      m_presentGC,
                      0,
                      0,
                      static_cast<unsigned int>(m_width),
                      static_cast<unsigned int>(m_height),
                      0,
                      0);
        }
        XFlush(m_display);
        m_presented = true;
    }

    static XRectangle intersectClip(const XRectangle& a, const XRectangle& b) {
        const int ax1 = a.x;
        const int ay1 = a.y;
        const int ax2 = a.x + static_cast<int>(a.width);
        const int ay2 = a.y + static_cast<int>(a.height);

        const int bx1 = b.x;
        const int by1 = b.y;
        const int bx2 = b.x + static_cast<int>(b.width);
        const int by2 = b.y + static_cast<int>(b.height);

        const int x1 = std::max(ax1, bx1);
        const int y1 = std::max(ay1, by1);
        const int x2 = std::min(ax2, bx2);
        const int y2 = std::min(ay2, by2);

        XRectangle out;
        out.x = static_cast<short>(x1);
        out.y = static_cast<short>(y1);
        out.width = static_cast<unsigned short>(std::max(0, x2 - x1));
        out.height = static_cast<unsigned short>(std::max(0, y2 - y1));
        return out;
    }

    bool ensureDrawable() {
        if (!m_display || !m_window || m_width <= 0 || m_height <= 0) {
            return false;
        }

        if (m_backBuffer &&
            (m_bufferWidth != m_width || m_bufferHeight != m_height)) {
            XFreePixmap(m_display, m_backBuffer);
            m_backBuffer = 0;
        }

        if (!m_backBuffer) {
            const int depth = DefaultDepth(m_display, DefaultScreen(m_display));
            m_backBuffer = XCreatePixmap(m_display,
                                         m_window,
                                         static_cast<unsigned int>(m_width),
                                         static_cast<unsigned int>(m_height),
                                         static_cast<unsigned int>(depth));
            m_bufferWidth = m_width;
            m_bufferHeight = m_height;

            // Clear new back-buffer to white so unpainted regions aren't black
            GC clearGc = XCreateGC(m_display, m_backBuffer, 0, nullptr);
            if (clearGc) {
                // Windows 11 default surface: rgb(249, 249, 249)
                const unsigned long win11Surface = (249u << 16) | (249u << 8) | 249u;
                XSetForeground(m_display, clearGc, win11Surface);
                XFillRectangle(m_display, m_backBuffer, clearGc, 0, 0,
                               static_cast<unsigned int>(m_width),
                               static_cast<unsigned int>(m_height));
                XFreeGC(m_display, clearGc);
            }
        }

        if (!m_gc) {
            m_gc = XCreateGC(m_display, m_window, 0, nullptr);
            if (m_gc) {
                XSetGraphicsExposures(m_display, m_gc, False);
            }
        }

        if (!m_presentGC) {
            m_presentGC = XCreateGC(m_display, m_window, 0, nullptr);
            if (m_presentGC) {
                XSetGraphicsExposures(m_display, m_presentGC, False);
            }
        }
        return m_gc != 0;
    }

    Drawable targetDrawable() const {
        return m_backBuffer ? static_cast<Drawable>(m_backBuffer)
                            : static_cast<Drawable>(m_window);
    }

    void setForeground(const SwColor& color) {
        if (!m_display || !m_gc) {
            return;
        }
        unsigned long pixel =
            (static_cast<unsigned long>(color.r) << 16) |
            (static_cast<unsigned long>(color.g) << 8) |
            static_cast<unsigned long>(color.b);
        XSetForeground(m_display, m_gc, pixel);
    }

    Display* m_display{nullptr};
    ::Window m_window{0};
    GC m_gc{0};
    GC m_presentGC{0};
    Pixmap m_backBuffer{0};
    int m_bufferWidth{0};
    int m_bufferHeight{0};
    int m_width{0};
    int m_height{0};
    bool m_finalizeRequested{false};
    bool m_presented{false};
    bool m_deferPresent{true};
    std::vector<XRectangle> m_clipStack;
};

#endif
