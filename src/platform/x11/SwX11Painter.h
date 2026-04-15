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
#include <dlfcn.h>
#include <unistd.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>
#include <cmath>

#include "core/gui/graphics/SwImage.h"
#include "core/types/SwString.h"
#include "core/gui/SwWidgetPlatformAdapter.h"
#include "media/SwVideoFrame.h"
#include "platform/SwPlatformIntegration.h"

#if defined(__has_include)
#  if __has_include(<va/va.h>) && __has_include(<va/va_x11.h>) && __has_include(<va/va_drmcommon.h>)
#    include <va/va.h>
#    include <va/va_x11.h>
#    include <va/va_drmcommon.h>
#    define SW_PLATFORM_X11_HAS_VAAPI_PRESENT_HEADERS 1
#  else
#    define SW_PLATFORM_X11_HAS_VAAPI_PRESENT_HEADERS 0
#  endif
#else
#  define SW_PLATFORM_X11_HAS_VAAPI_PRESENT_HEADERS 0
#endif

class SwX11Painter : public SwPlatformPainter {
public:
    SwX11Painter() = default;

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

    static void releaseSharedBackbuffer(Display* display, ::Window window);
    static Pixmap sharedBackbufferHandle(Display* display, ::Window window);

    void begin(const SwPlatformPaintEvent& event) override {
        end();

        const void* nativeWindow = event.nativeWindowHandle ? event.nativeWindowHandle : event.nativePaintDevice;

        m_display = static_cast<Display*>(event.nativeDisplay);
        m_window = reinterpret_cast<::Window>(reinterpret_cast<std::uintptr_t>(nativeWindow));
        m_width = std::max(1, event.surfaceSize.width);
        m_height = std::max(1, event.surfaceSize.height);
        m_finalizeRequested = false;
        m_presented = false;
        m_clipStack.clear();
        ensureDrawable();
    }

    void end() override {
        m_finalizeRequested = true;
        presentIfNeeded_();
        m_gc = 0;
        m_presentGC = 0;
        m_backBuffer = 0;
        m_bufferWidth = 0;
        m_bufferHeight = 0;
        m_width = 0;
        m_height = 0;
        m_finalizeRequested = false;
        m_presented = false;
        m_clipStack.clear();
        m_display = nullptr;
        m_window = 0;
    }

    void flush() override {
        if (m_display) {
            XFlush(m_display);
        }
    }

    /**
     * @brief Destroys the `SwX11Painter` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwX11Painter() override {
        end();
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

        const int segments = std::min(64, std::max(12, actualRadius * 3));
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
    void fillRoundedRect(const SwRect& rect,
                         int radiusTL, int radiusTR, int radiusBR, int radiusBL,
                         const SwColor& fillColor,
                         const SwColor& borderColor,
                         int borderWidth) override {
        if (!ensureDrawable()) {
            return;
        }
        if (radiusTL == radiusTR && radiusTR == radiusBR && radiusBR == radiusBL) {
            fillRoundedRect(rect, radiusTL, fillColor, borderColor, borderWidth);
            return;
        }
        if (radiusTL <= 0 && radiusTR <= 0 && radiusBR <= 0 && radiusBL <= 0) {
            fillRect(rect, fillColor, borderColor, borderWidth);
            return;
        }

        const int maxRad = std::min(rect.width, rect.height) / 2;
        auto clamp = [maxRad](int r) { return std::max(0, std::min(r, maxRad)); };
        const int rTL = clamp(radiusTL);
        const int rTR = clamp(radiusTR);
        const int rBR = clamp(radiusBR);
        const int rBL = clamp(radiusBL);

        const int segments = std::min(64, std::max(12, maxRad * 3));
        const int left = rect.x;
        const int right = rect.x + rect.width;
        const int top = rect.y;
        const int bottom = rect.y + rect.height;

        std::vector<XPoint> outline;
        outline.reserve(static_cast<size_t>(segments) * 4 + 1);

        auto addArc = [&](double start, double end, int cx, int cy, int rad) {
            if (rad <= 0) {
                outline.push_back(XPoint{static_cast<short>(cx + static_cast<int>(rad * std::cos(start))),
                                         static_cast<short>(cy + static_cast<int>(rad * std::sin(start)))});
                return;
            }
            for (int i = 0; i <= segments; ++i) {
                double t = start + (end - start) * (static_cast<double>(i) / segments);
                outline.push_back(XPoint{static_cast<short>(std::lround(cx + rad * std::cos(t))),
                                         static_cast<short>(std::lround(cy + rad * std::sin(t)))});
            }
        };

        addArc(M_PI, M_PI * 1.5, left + rTL, top + rTL, rTL);
        addArc(M_PI * 1.5, M_PI * 2.0, right - rTR, top + rTR, rTR);
        addArc(0.0, M_PI_2, right - rBR, bottom - rBR, rBR);
        addArc(M_PI_2, M_PI, left + rBL, bottom - rBL, rBL);

        setForeground(fillColor);
        XFillPolygon(m_display, targetDrawable(), m_gc,
                     outline.data(), static_cast<int>(outline.size()),
                     Complex, CoordModeOrigin);

        if (borderWidth > 0) {
            setForeground(borderColor);
            XSetLineAttributes(m_display, m_gc, borderWidth, LineSolid, CapRound, JoinRound);
            outline.push_back(outline.front());
            XDrawLines(m_display, targetDrawable(), m_gc,
                       outline.data(), static_cast<int>(outline.size()),
                       CoordModeOrigin);
        }
    }

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
    void drawDashedRect(const SwRect& rect,
                        const SwColor& color,
                        int borderWidth,
                        int dashLen,
                        int gapLen) override {
        if (!ensureDrawable() || rect.width <= 0 || rect.height <= 0) {
            return;
        }
        const int bw = borderWidth > 0 ? borderWidth : 1;
        const char dashPattern[2] = {
            static_cast<char>(dashLen > 0 ? dashLen : 4),
            static_cast<char>(gapLen > 0 ? gapLen : 4)
        };
        setForeground(color);
        XSetDashes(m_display, m_gc, 0, dashPattern, 2);
        XSetLineAttributes(m_display, m_gc, bw, LineOnOffDash, CapButt, JoinMiter);
        XDrawRectangle(m_display,
                       targetDrawable(),
                       m_gc,
                       rect.x,
                       rect.y,
                       static_cast<unsigned int>(rect.width),
                       static_cast<unsigned int>(rect.height));
        XSetLineAttributes(m_display, m_gc, 1, LineSolid, CapButt, JoinMiter);
    }

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

    bool drawBgra32(const SwRect& targetRect,
                    const uint8_t* pixels,
                    int width,
                    int height,
                    int stride) override {
        if (!ensureDrawable() || !pixels || width <= 0 || height <= 0 || targetRect.width <= 0 ||
            targetRect.height <= 0 || stride < width * 4) {
            return false;
        }

        const int screen = DefaultScreen(m_display);
        const int depth = DefaultDepth(m_display, screen);
        Visual* visual = DefaultVisual(m_display, screen);
        if (!visual) {
            return false;
        }

        const VisualPacking packing = makeVisualPacking_(visual);
        const int dstW = targetRect.width;
        const int dstH = targetRect.height;
        const int srcW = width;
        const int srcH = height;

        std::lock_guard<std::mutex> lock(sharedBackbufferMutex_());
        SharedBackbufferMap::iterator it =
            sharedBackbuffers_().find(sharedBackbufferKey_(m_display, m_window));
        if (it == sharedBackbuffers_().end()) {
            return false;
        }

        SharedBackbuffer& shared = it->second;
        const std::size_t pixelCount = static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH);
        if (shared.videoUploadPixels.size() != pixelCount) {
            shared.videoUploadPixels.resize(pixelCount);
        }

        uint32_t* dstPixels = shared.videoUploadPixels.data();
        if (srcW == dstW && srcH == dstH) {
            for (int y = 0; y < dstH; ++y) {
                const uint8_t* srcRow = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
                uint32_t* dstRow = dstPixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW);
                for (int x = 0; x < dstW; ++x) {
                    const uint8_t* srcPixel = srcRow + static_cast<std::size_t>(x) * 4u;
                    dstRow[x] = packRgbPixel_(packing, srcPixel[2], srcPixel[1], srcPixel[0]);
                }
            }
        } else {
            for (int y = 0; y < dstH; ++y) {
                const int srcY = std::min(srcH - 1, (y * srcH) / dstH);
                const uint8_t* srcRow =
                    pixels + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(stride);
                uint32_t* dstRow = dstPixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW);
                for (int x = 0; x < dstW; ++x) {
                    const int srcX = std::min(srcW - 1, (x * srcW) / dstW);
                    const uint8_t* srcPixel = srcRow + static_cast<std::size_t>(srcX) * 4u;
                    dstRow[x] = packRgbPixel_(packing, srcPixel[2], srcPixel[1], srcPixel[0]);
                }
            }
        }

        XImage* ximg = XCreateImage(m_display,
                                    visual,
                                    static_cast<unsigned int>(depth),
                                    ZPixmap,
                                    0,
                                    reinterpret_cast<char*>(dstPixels),
                                    static_cast<unsigned int>(dstW),
                                    static_cast<unsigned int>(dstH),
                                    32,
                                    dstW * 4);
        if (!ximg) {
            return false;
        }

        XPutImage(m_display,
                  targetDrawable(),
                  m_gc,
                  ximg,
                  0,
                  0,
                  targetRect.x,
                  targetRect.y,
                  static_cast<unsigned int>(dstW),
                  static_cast<unsigned int>(dstH));

        ximg->data = nullptr;
        XDestroyImage(ximg);
        return true;
    }

    bool drawNativeVideoFrame(const SwRect& targetRect,
                              const SwVideoFrame& frame) override {
#if !SW_PLATFORM_X11_HAS_VAAPI_PRESENT_HEADERS
        SW_UNUSED(targetRect)
        SW_UNUSED(frame)
        return false;
#else
        if (!ensureDrawable() || !frame.isNativeVaapiPrime()) {
            return false;
        }
        return drawVaapiPrimeFrame_(targetRect, frame);
#endif
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
    struct ChannelPacking {
        unsigned long mask{0};
        int shift{0};
        unsigned long maxValue{0};
    };

    struct VisualPacking {
        ChannelPacking red{};
        ChannelPacking green{};
        ChannelPacking blue{};
    };

#if SW_PLATFORM_X11_HAS_VAAPI_PRESENT_HEADERS
    struct VaapiX11LibraryState {
        void* vaLibrary{nullptr};
        void* vaX11Library{nullptr};
        bool attempted{false};
        bool loaded{false};
        VADisplay (*vaGetDisplayFn)(Display*){nullptr};
        VAStatus (*vaInitializeFn)(VADisplay, int*, int*){nullptr};
        VAStatus (*vaTerminateFn)(VADisplay){nullptr};
        VAStatus (*vaCreateSurfacesFn)(VADisplay,
                                       unsigned int,
                                       unsigned int,
                                       unsigned int,
                                       VASurfaceID*,
                                       unsigned int,
                                       VASurfaceAttrib*,
                                       unsigned int){nullptr};
        VAStatus (*vaDestroySurfacesFn)(VADisplay, VASurfaceID*, int){nullptr};
        VAStatus (*vaPutSurfaceFn)(VADisplay,
                                   VASurfaceID,
                                   Drawable,
                                   short,
                                   short,
                                   unsigned short,
                                   unsigned short,
                                   short,
                                   short,
                                   unsigned short,
                                   unsigned short,
                                   VARectangle*,
                                   unsigned int,
                                   unsigned int){nullptr};
    };

    struct SharedVaapiDisplay {
        VADisplay vaDisplay{nullptr};
        bool initialized{false};
    };
#endif

    struct SharedBackbuffer {
        Pixmap backBuffer{0};
        GC gc{0};
        GC presentGC{0};
        int width{0};
        int height{0};
        std::vector<uint32_t> videoUploadPixels;
    };

    using SharedBackbufferKey = std::pair<std::uintptr_t, std::uintptr_t>;
    using SharedBackbufferMap = std::map<SharedBackbufferKey, SharedBackbuffer>;

    static SharedBackbufferKey sharedBackbufferKey_(Display* display, ::Window window) {
        return SharedBackbufferKey(reinterpret_cast<std::uintptr_t>(display),
                                   static_cast<std::uintptr_t>(window));
    }

    static SharedBackbufferMap& sharedBackbuffers_() {
        static SharedBackbufferMap buffers;
        return buffers;
    }

    static std::mutex& sharedBackbufferMutex_() {
        static std::mutex mutex;
        return mutex;
    }

#if SW_PLATFORM_X11_HAS_VAAPI_PRESENT_HEADERS
    using SharedVaapiDisplayMap = std::map<std::uintptr_t, SharedVaapiDisplay>;

    static std::uintptr_t sharedVaapiDisplayKey_(Display* display) {
        return reinterpret_cast<std::uintptr_t>(display);
    }

    static SharedVaapiDisplayMap& sharedVaapiDisplays_() {
        static SharedVaapiDisplayMap displays;
        return displays;
    }

    static std::mutex& sharedVaapiDisplayMutex_() {
        static std::mutex mutex;
        return mutex;
    }

    static VaapiX11LibraryState& vaapiX11Library_() {
        static VaapiX11LibraryState state;
        return state;
    }

    static bool ensureVaapiX11Library_() {
        VaapiX11LibraryState& state = vaapiX11Library_();
        if (state.loaded) {
            return true;
        }
        if (state.attempted) {
            return false;
        }
        state.attempted = true;

        static const char* kVaCandidates[] = {"libva.so.2", "libva.so"};
        static const char* kVaX11Candidates[] = {"libva-x11.so.2", "libva-x11.so"};
        for (std::size_t i = 0; i < (sizeof(kVaCandidates) / sizeof(kVaCandidates[0])); ++i) {
            state.vaLibrary = dlopen(kVaCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (state.vaLibrary) {
                break;
            }
        }
        for (std::size_t i = 0; i < (sizeof(kVaX11Candidates) / sizeof(kVaX11Candidates[0])); ++i) {
            state.vaX11Library = dlopen(kVaX11Candidates[i], RTLD_NOW | RTLD_LOCAL);
            if (state.vaX11Library) {
                break;
            }
        }
        if (!state.vaLibrary || !state.vaX11Library) {
            return false;
        }

        state.vaGetDisplayFn = reinterpret_cast<VADisplay(*)(Display*)>(
            dlsym(state.vaX11Library, "vaGetDisplay"));
        state.vaInitializeFn = reinterpret_cast<VAStatus(*)(VADisplay, int*, int*)>(
            dlsym(state.vaLibrary, "vaInitialize"));
        state.vaTerminateFn = reinterpret_cast<VAStatus(*)(VADisplay)>(
            dlsym(state.vaLibrary, "vaTerminate"));
        state.vaCreateSurfacesFn = reinterpret_cast<VAStatus(*)(VADisplay,
                                                                unsigned int,
                                                                unsigned int,
                                                                unsigned int,
                                                                VASurfaceID*,
                                                                unsigned int,
                                                                VASurfaceAttrib*,
                                                                unsigned int)>(
            dlsym(state.vaLibrary, "vaCreateSurfaces"));
        state.vaDestroySurfacesFn = reinterpret_cast<VAStatus(*)(VADisplay, VASurfaceID*, int)>(
            dlsym(state.vaLibrary, "vaDestroySurfaces"));
        state.vaPutSurfaceFn = reinterpret_cast<VAStatus(*)(VADisplay,
                                                            VASurfaceID,
                                                            Drawable,
                                                            short,
                                                            short,
                                                            unsigned short,
                                                            unsigned short,
                                                            short,
                                                            short,
                                                            unsigned short,
                                                            unsigned short,
                                                            VARectangle*,
                                                            unsigned int,
                                                            unsigned int)>(
            dlsym(state.vaX11Library, "vaPutSurface"));
        state.loaded = state.vaGetDisplayFn && state.vaInitializeFn && state.vaTerminateFn &&
                       state.vaCreateSurfacesFn && state.vaDestroySurfacesFn &&
                       state.vaPutSurfaceFn;
        return state.loaded;
    }

    VADisplay ensureVaapiDisplay_() {
        if (!m_display || !ensureVaapiX11Library_()) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(sharedVaapiDisplayMutex_());
        SharedVaapiDisplay& shared = sharedVaapiDisplays_()[sharedVaapiDisplayKey_(m_display)];
        if (shared.initialized && shared.vaDisplay) {
            return shared.vaDisplay;
        }

        VaapiX11LibraryState& lib = vaapiX11Library_();
        shared.vaDisplay = lib.vaGetDisplayFn ? lib.vaGetDisplayFn(m_display) : nullptr;
        if (!shared.vaDisplay) {
            return nullptr;
        }
        int major = 0;
        int minor = 0;
        if (!lib.vaInitializeFn || lib.vaInitializeFn(shared.vaDisplay, &major, &minor) != VA_STATUS_SUCCESS) {
            shared.vaDisplay = nullptr;
            shared.initialized = false;
            return nullptr;
        }
        shared.initialized = true;
        return shared.vaDisplay;
    }

    static unsigned int inferVaapiRtFormat_(const SwVideoFrame& frame,
                                            const SwVideoFrame::NativeVaapiPrimeStorage& storage) {
        if (storage.rtFormat != 0u) {
            return storage.rtFormat;
        }
        switch (frame.pixelFormat()) {
        case SwVideoPixelFormat::NV12:
        case SwVideoPixelFormat::YUV420P:
            return VA_RT_FORMAT_YUV420;
        case SwVideoPixelFormat::P010:
#ifdef VA_RT_FORMAT_YUV420_10BPP
            return VA_RT_FORMAT_YUV420_10BPP;
#else
            return 0u;
#endif
        default:
            return 0u;
        }
    }

    bool drawVaapiPrimeFrame_(const SwRect& targetRect, const SwVideoFrame& frame) {
        const SwVideoFrame::NativeVaapiPrimeStorage* storage = frame.nativeVaapiPrimeStorage();
        if (!storage || storage->numObjects == 0 || storage->numLayers == 0) {
            return false;
        }

        VADisplay vaDisplay = ensureVaapiDisplay_();
        if (!vaDisplay) {
            return false;
        }

        const unsigned int rtFormat = inferVaapiRtFormat_(frame, *storage);
        if (rtFormat == 0u) {
            return false;
        }

        VADRMPRIMESurfaceDescriptor descriptor;
        std::memset(&descriptor, 0, sizeof(descriptor));
        descriptor.fourcc = storage->fourcc;
        descriptor.width = storage->width;
        descriptor.height = storage->height;
        descriptor.num_objects = storage->numObjects;
        descriptor.num_layers = storage->numLayers;
        for (std::size_t i = 0; i < storage->objects.size(); ++i) {
            descriptor.objects[i].fd = storage->objects[i].fd;
            descriptor.objects[i].size = storage->objects[i].size;
            descriptor.objects[i].drm_format_modifier = storage->objects[i].drmFormatModifier;
        }
        for (std::size_t i = 0; i < storage->layers.size(); ++i) {
            descriptor.layers[i].drm_format = storage->layers[i].drmFormat;
            descriptor.layers[i].num_planes = storage->layers[i].numPlanes;
            for (int plane = 0; plane < 4; ++plane) {
                descriptor.layers[i].object_index[plane] = storage->layers[i].objectIndex[plane];
                descriptor.layers[i].offset[plane] = storage->layers[i].offset[plane];
                descriptor.layers[i].pitch[plane] = storage->layers[i].pitch[plane];
            }
        }

        VASurfaceAttrib attrs[2];
        std::memset(attrs, 0, sizeof(attrs));
        attrs[0].type = VASurfaceAttribMemoryType;
        attrs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrs[0].value.type = VAGenericValueTypeInteger;
        attrs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
        attrs[1].type = VASurfaceAttribExternalBufferDescriptor;
        attrs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrs[1].value.type = VAGenericValueTypePointer;
        attrs[1].value.value.p = &descriptor;

        VASurfaceID importedSurface = VA_INVALID_SURFACE;
        VaapiX11LibraryState& lib = vaapiX11Library_();
        if (!lib.vaCreateSurfacesFn ||
            lib.vaCreateSurfacesFn(vaDisplay,
                                   rtFormat,
                                   storage->width,
                                   storage->height,
                                   &importedSurface,
                                   1,
                                   attrs,
                                   2) != VA_STATUS_SUCCESS ||
            importedSurface == VA_INVALID_SURFACE) {
            return false;
        }

        const bool ok =
            lib.vaPutSurfaceFn &&
            lib.vaPutSurfaceFn(vaDisplay,
                               importedSurface,
                               targetDrawable(),
                               0,
                               0,
                               static_cast<unsigned short>(storage->width),
                               static_cast<unsigned short>(storage->height),
                               static_cast<short>(targetRect.x),
                               static_cast<short>(targetRect.y),
                               static_cast<unsigned short>(targetRect.width),
                               static_cast<unsigned short>(targetRect.height),
                               nullptr,
                               0,
                               0) == VA_STATUS_SUCCESS;

        if (lib.vaDestroySurfacesFn) {
            (void)lib.vaDestroySurfacesFn(vaDisplay, &importedSurface, 1);
        }
        return ok;
    }
#endif

    static ChannelPacking makeChannelPacking_(unsigned long mask) {
        ChannelPacking packing;
        packing.mask = mask;
        if (!mask) {
            return packing;
        }

        while (((mask >> packing.shift) & 0x1ul) == 0x0ul) {
            ++packing.shift;
        }

        unsigned long trimmedMask = mask >> packing.shift;
        while (trimmedMask != 0) {
            ++packing.maxValue;
            trimmedMask >>= 1;
        }
        packing.maxValue = packing.maxValue ? ((1ul << packing.maxValue) - 1ul) : 0ul;
        return packing;
    }

    static VisualPacking makeVisualPacking_(const Visual* visual) {
        VisualPacking packing;
        if (!visual) {
            return packing;
        }
        packing.red = makeChannelPacking_(visual->red_mask);
        packing.green = makeChannelPacking_(visual->green_mask);
        packing.blue = makeChannelPacking_(visual->blue_mask);
        return packing;
    }

    static unsigned long packChannel_(const ChannelPacking& packing, uint8_t value) {
        if (!packing.mask || !packing.maxValue) {
            return 0ul;
        }
        const unsigned long scaled =
            (static_cast<unsigned long>(value) * packing.maxValue + 127ul) / 255ul;
        return (scaled << packing.shift) & packing.mask;
    }

    static uint32_t packRgbPixel_(const VisualPacking& packing,
                                  uint8_t r,
                                  uint8_t g,
                                  uint8_t b) {
        const unsigned long pixel = packChannel_(packing.red, r) |
                                    packChannel_(packing.green, g) |
                                    packChannel_(packing.blue, b);
        return static_cast<uint32_t>(pixel);
    }

    static void clearBackbuffer_(Display* display, Pixmap backBuffer, int width, int height) {
        if (!display || !backBuffer || width <= 0 || height <= 0) {
            return;
        }

        GC clearGc = XCreateGC(display, backBuffer, 0, nullptr);
        if (!clearGc) {
            return;
        }

        const unsigned long win11Surface = (249u << 16) | (249u << 8) | 249u;
        XSetForeground(display, clearGc, win11Surface);
        XFillRectangle(display,
                       backBuffer,
                       clearGc,
                       0,
                       0,
                       static_cast<unsigned int>(width),
                       static_cast<unsigned int>(height));
        XFreeGC(display, clearGc);
    }

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

        std::lock_guard<std::mutex> lock(sharedBackbufferMutex_());
        SharedBackbuffer& shared = sharedBackbuffers_()[sharedBackbufferKey_(m_display, m_window)];

        if (shared.backBuffer &&
            (shared.width != m_width || shared.height != m_height)) {
            XFreePixmap(m_display, shared.backBuffer);
            shared.backBuffer = 0;
            shared.width = 0;
            shared.height = 0;
        }

        if (!shared.backBuffer) {
            const int depth = DefaultDepth(m_display, DefaultScreen(m_display));
            shared.backBuffer = XCreatePixmap(m_display,
                                              m_window,
                                              static_cast<unsigned int>(m_width),
                                              static_cast<unsigned int>(m_height),
                                              static_cast<unsigned int>(depth));
            shared.width = m_width;
            shared.height = m_height;
            clearBackbuffer_(m_display, shared.backBuffer, shared.width, shared.height);
        }

        if (!shared.gc) {
            shared.gc = XCreateGC(m_display, m_window, 0, nullptr);
            if (shared.gc) {
                XSetGraphicsExposures(m_display, shared.gc, False);
            }
        }

        if (!shared.presentGC) {
            shared.presentGC = XCreateGC(m_display, m_window, 0, nullptr);
            if (shared.presentGC) {
                XSetGraphicsExposures(m_display, shared.presentGC, False);
            }
        }

        m_gc = shared.gc;
        m_presentGC = shared.presentGC;
        m_backBuffer = shared.backBuffer;
        m_bufferWidth = shared.width;
        m_bufferHeight = shared.height;
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

inline void SwX11Painter::releaseSharedBackbuffer(Display* display, ::Window window) {
    if (!display || !window) {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedBackbufferMutex_());
    SharedBackbufferMap::iterator it = sharedBackbuffers_().find(sharedBackbufferKey_(display, window));
    if (it == sharedBackbuffers_().end()) {
        return;
    }

    if (it->second.gc) {
        XFreeGC(display, it->second.gc);
    }
    if (it->second.presentGC) {
        XFreeGC(display, it->second.presentGC);
    }
    if (it->second.backBuffer) {
        XFreePixmap(display, it->second.backBuffer);
    }
    sharedBackbuffers_().erase(it);
}

inline Pixmap SwX11Painter::sharedBackbufferHandle(Display* display, ::Window window) {
    if (!display || !window) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(sharedBackbufferMutex_());
    SharedBackbufferMap::const_iterator it =
        sharedBackbuffers_().find(sharedBackbufferKey_(display, window));
    if (it == sharedBackbuffers_().end()) {
        return 0;
    }
    return it->second.backBuffer;
}

#endif
