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
 * @file src/platform/win/SwWin32Painter.h
 * @ingroup platform_backends
 * @brief Declares the public interface exposed by SwWin32Painter in the CoreSw Win32 platform
 * integration layer.
 *
 * This header belongs to the CoreSw Win32 platform integration layer. It binds portable framework
 * abstractions to concrete Win32 windowing, painting, and input services.
 *
 * Within that layer, this file focuses on the win32 painter interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwWin32Painter.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Types here define the seam between portable APIs and the native event and rendering loop on
 * Windows.
 *
 */


#if defined(_WIN32)

#include "platform/win/SwWindows.h"

#include "core/gui/SwPainter.h"
#include "core/gui/graphics/SwImage.h"

#include <wrl/client.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <wincodec.h>

#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "msimg32.lib")

class SwWin32Painter : public SwPainter {
public:
    /**
     * @brief Constructs a `SwWin32Painter` instance.
     * @param dc Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwWin32Painter(HDC dc)
        : m_hdc(dc) {}

    /**
     * @brief Clears the current object state.
     * @param color Value passed to the method.
     */
    void clear(const SwColor& color) override {
        RECT rect;
        GetClipBox(m_hdc, &rect);
        HBRUSH brush = createBrush(color);
        FillRect(m_hdc, &rect, brush);
        DeleteObject(brush);
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
        RECT r = toRect(rect);
        HBRUSH brush = createBrush(fillColor);
        FillRect(m_hdc, &r, brush);
        DeleteObject(brush);
        if (borderWidth > 0) {
            HPEN pen = createPen(borderColor, borderWidth);
            HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(m_hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(m_hdc, r.left, r.top, r.right, r.bottom);
            SelectObject(m_hdc, oldPen);
            SelectObject(m_hdc, oldBrush);
            DeleteObject(pen);
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
        if (!m_hdc) {
            return;
        }

        if (radius <= 0) {
            fillRect(rect, fillColor, borderColor, borderWidth);
            return;
        }

        const RECT r = toRect(rect);
        const int w = std::max(0, static_cast<int>(r.right - r.left));
        const int h = std::max(0, static_cast<int>(r.bottom - r.top));
        if (w <= 0 || h <= 0) {
            return;
        }

        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() != Gdiplus::Ok) {
            // Fallback to GDI
            HBRUSH brush = createBrush(fillColor);
            HPEN pen = createPen(borderColor, borderWidth);
            HBRUSH oldBrush = (HBRUSH)SelectObject(m_hdc, brush);
            HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);
            RoundRect(m_hdc, r.left, r.top, r.right, r.bottom, radius, radius);
            SelectObject(m_hdc, oldBrush);
            SelectObject(m_hdc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
            return;
        }

        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

        auto clampRadius = [&](int rad) -> Gdiplus::REAL {
            const int maxRad = std::max(0, std::min(w, h) / 2);
            return static_cast<Gdiplus::REAL>(std::max(0, std::min(rad, maxRad)));
        };

        const Gdiplus::REAL rad = clampRadius(radius);
        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(r.left);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(r.top);
        const Gdiplus::REAL rw = static_cast<Gdiplus::REAL>(w);
        const Gdiplus::REAL rh = static_cast<Gdiplus::REAL>(h);
        const Gdiplus::REAL d = rad * 2.0f;

        Gdiplus::GraphicsPath path;
        path.StartFigure();
        path.AddArc(x, y, d, d, 180.0f, 90.0f);
        path.AddArc(x + rw - d, y, d, d, 270.0f, 90.0f);
        path.AddArc(x + rw - d, y + rh - d, d, d, 0.0f, 90.0f);
        path.AddArc(x, y + rh - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();

        Gdiplus::SolidBrush brush(Gdiplus::Color(255, fillColor.r, fillColor.g, fillColor.b));
        graphics.FillPath(&brush, &path);

        if (borderWidth > 0) {
            Gdiplus::Pen pen(Gdiplus::Color(255, borderColor.r, borderColor.g, borderColor.b),
                             static_cast<Gdiplus::REAL>(borderWidth));
            pen.SetAlignment(Gdiplus::PenAlignmentInset);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            graphics.DrawPath(&pen, &path);
        }
    }

    // Per-corner rounded rect: TL, TR, BR, BL radii.
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
     */
    void fillRoundedRect(const SwRect& rect,
                         int radiusTL, int radiusTR, int radiusBR, int radiusBL,
                         const SwColor& fillColor,
                         const SwColor& borderColor,
                         int borderWidth) override {
        if (!m_hdc) {
            return;
        }
        // If all radii are the same, use the uniform version.
        if (radiusTL == radiusTR && radiusTR == radiusBR && radiusBR == radiusBL) {
            fillRoundedRect(rect, radiusTL, fillColor, borderColor, borderWidth);
            return;
        }
        if (radiusTL <= 0 && radiusTR <= 0 && radiusBR <= 0 && radiusBL <= 0) {
            fillRect(rect, fillColor, borderColor, borderWidth);
            return;
        }

        const RECT r = toRect(rect);
        const int w = std::max(0, static_cast<int>(r.right - r.left));
        const int h = std::max(0, static_cast<int>(r.bottom - r.top));
        if (w <= 0 || h <= 0) {
            return;
        }

        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() != Gdiplus::Ok) {
            fillRoundedRect(rect, std::max({radiusTL, radiusTR, radiusBR, radiusBL}),
                            fillColor, borderColor, borderWidth);
            return;
        }

        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

        const int maxRad = std::max(0, std::min(w, h) / 2);
        auto clamp = [maxRad](int rad) -> Gdiplus::REAL {
            return static_cast<Gdiplus::REAL>(std::max(0, std::min(rad, maxRad)));
        };

        const Gdiplus::REAL rTL = clamp(radiusTL);
        const Gdiplus::REAL rTR = clamp(radiusTR);
        const Gdiplus::REAL rBR = clamp(radiusBR);
        const Gdiplus::REAL rBL = clamp(radiusBL);
        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(r.left);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(r.top);
        const Gdiplus::REAL rw = static_cast<Gdiplus::REAL>(w);
        const Gdiplus::REAL rh = static_cast<Gdiplus::REAL>(h);

        Gdiplus::GraphicsPath path;
        path.StartFigure();
        // Top-left
        if (rTL > 0) {
            path.AddArc(x, y, rTL * 2, rTL * 2, 180.0f, 90.0f);
        } else {
            path.AddLine(x, y, x, y);
        }
        // Top-right
        if (rTR > 0) {
            path.AddArc(x + rw - rTR * 2, y, rTR * 2, rTR * 2, 270.0f, 90.0f);
        } else {
            path.AddLine(x + rw, y, x + rw, y);
        }
        // Bottom-right
        if (rBR > 0) {
            path.AddArc(x + rw - rBR * 2, y + rh - rBR * 2, rBR * 2, rBR * 2, 0.0f, 90.0f);
        } else {
            path.AddLine(x + rw, y + rh, x + rw, y + rh);
        }
        // Bottom-left
        if (rBL > 0) {
            path.AddArc(x, y + rh - rBL * 2, rBL * 2, rBL * 2, 90.0f, 90.0f);
        } else {
            path.AddLine(x, y + rh, x, y + rh);
        }
        path.CloseFigure();

        Gdiplus::SolidBrush brush(Gdiplus::Color(255, fillColor.r, fillColor.g, fillColor.b));
        graphics.FillPath(&brush, &path);

        if (borderWidth > 0) {
            Gdiplus::Pen pen(Gdiplus::Color(255, borderColor.r, borderColor.g, borderColor.b),
                             static_cast<Gdiplus::REAL>(borderWidth));
            pen.SetAlignment(Gdiplus::PenAlignmentInset);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawPath(&pen, &path);
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
        RECT r = toRect(rect);
        HPEN pen = createPen(borderColor, borderWidth);
        HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(m_hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(m_hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(m_hdc, oldPen);
        SelectObject(m_hdc, oldBrush);
        DeleteObject(pen);
    }

    /**
     * @brief Performs the `drawDashedRect` operation.
     * @param rect Rectangle used by the operation.
     * @param color Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     */
    void drawDashedRect(const SwRect& rect,
                        const SwColor& color,
                        int borderWidth,
                        int /*dashLen*/,
                        int /*gapLen*/) override {
        if (rect.width <= 0 || rect.height <= 0) return;
        const int bw = borderWidth > 0 ? borderWidth : 1;
        LOGBRUSH lb{BS_SOLID, toColorRef(color), 0};
        HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_DASH | PS_ENDCAP_FLAT | PS_JOIN_MITER,
                                static_cast<DWORD>(bw), &lb, 0, nullptr);
        if (!pen) { drawRect(rect, color, bw); return; }
        HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(m_hdc, GetStockObject(HOLLOW_BRUSH));
        const int oldBkMode = SetBkMode(m_hdc, TRANSPARENT);
        Rectangle(m_hdc, rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
        SetBkMode(m_hdc, oldBkMode);
        SelectObject(m_hdc, oldPen);
        SelectObject(m_hdc, oldBrush);
        DeleteObject(pen);
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
        const int w = width > 0 ? width : 1;

        // Keep GDI for axis-aligned lines to preserve crisp 1px separators (no anti-alias blur).
        if (x1 == x2 || y1 == y2) {
            HPEN pen = createPen(color, w);
            HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);
            MoveToEx(m_hdc, x1, y1, nullptr);
            LineTo(m_hdc, x2, y2);
            SelectObject(m_hdc, oldPen);
            DeleteObject(pen);
            return;
        }

        // Use GDI+ anti-aliasing for diagonal lines (icons, chevrons, etc.).
        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() == Gdiplus::Ok) {
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

            Gdiplus::Pen pen(Gdiplus::Color(255, color.r, color.g, color.b), static_cast<Gdiplus::REAL>(w));
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawLine(&pen,
                              static_cast<Gdiplus::REAL>(x1),
                              static_cast<Gdiplus::REAL>(y1),
                              static_cast<Gdiplus::REAL>(x2),
                              static_cast<Gdiplus::REAL>(y2));
            return;
        }

        HPEN pen = createPen(color, width > 0 ? width : 1);
        HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);
        MoveToEx(m_hdc, x1, y1, nullptr);
        LineTo(m_hdc, x2, y2);
        SelectObject(m_hdc, oldPen);
        DeleteObject(pen);
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
        if (!m_hdc || !points || count < 3) {
            return;
        }

        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() == Gdiplus::Ok) {
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

            std::vector<Gdiplus::PointF> pts;
            pts.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                pts.push_back(Gdiplus::PointF(static_cast<Gdiplus::REAL>(points[i].x),
                                              static_cast<Gdiplus::REAL>(points[i].y)));
            }

            Gdiplus::SolidBrush brush(Gdiplus::Color(255, fillColor.r, fillColor.g, fillColor.b));
            graphics.FillPolygon(&brush, pts.data(), count);

            if (borderWidth > 0) {
                Gdiplus::Pen pen(Gdiplus::Color(255, borderColor.r, borderColor.g, borderColor.b),
                                 static_cast<Gdiplus::REAL>(borderWidth));
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                graphics.DrawPolygon(&pen, pts.data(), count);
            }
            return;
        }

        // Fallback: classic GDI polygon.
        std::vector<POINT> pts;
        pts.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            pts.push_back(POINT{points[i].x, points[i].y});
        }

        HBRUSH brush = createBrush(fillColor);
        HBRUSH oldBrush = (HBRUSH)SelectObject(m_hdc, brush);

        HPEN pen = nullptr;
        if (borderWidth > 0) {
            pen = createPen(borderColor, borderWidth);
        } else {
            pen = (HPEN)GetStockObject(NULL_PEN);
        }
        HPEN oldPen = (HPEN)SelectObject(m_hdc, pen);

        Polygon(m_hdc, pts.data(), count);

        SelectObject(m_hdc, oldPen);
        SelectObject(m_hdc, oldBrush);
        DeleteObject(brush);
        if (borderWidth > 0 && pen) {
            DeleteObject(pen);
        }
    }

    /**
     * @brief Performs the `pushClipRect` operation.
     * @param rect Rectangle used by the operation.
     */
    void pushClipRect(const SwRect& rect) override {
        if (!m_hdc) {
            return;
        }
        SaveDC(m_hdc);
        ++m_clipDepth;
        IntersectClipRect(m_hdc,
                          rect.x,
                          rect.y,
                          rect.x + rect.width,
                          rect.y + rect.height);
    }

    /**
     * @brief Performs the `popClipRect` operation.
     */
    void popClipRect() override {
        if (!m_hdc) {
            return;
        }
        if (m_clipDepth <= 0) {
            return;
        }
        RestoreDC(m_hdc, -1);
        --m_clipDepth;
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
        if (!m_hdc) {
            return;
        }

        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }

        const std::wstring wText = text.toStdWString();
        if (wText.empty()) {
            return;
        }

        // Emoji: prefer DirectWrite/Direct2D for color fonts, fallback to classic GDI when unavailable.
        if (likelyEmojiText_(wText)) {
            if (drawTextD2D_(rect, wText, alignment, color, font)) {
                return;
            }
            if (drawTextColorEmoji_(rect, wText, alignment, color, font)) {
                return;
            }
        }

        // Fallback: classic GDI text (no color-font support).
        RECT r = toRect(rect);
        HFONT hFont = const_cast<SwFont&>(font).handle(m_hdc);
        HFONT oldFont = (HFONT)SelectObject(m_hdc, hFont);
        SetBkMode(m_hdc, TRANSPARENT);
        SetTextColor(m_hdc, toColorRef(color));
        DrawTextW(m_hdc,
                  wText.c_str(),
                  -1,
                  &r,
                  translateAlignment(alignment));
        SelectObject(m_hdc, oldFont);
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
        if (!m_hdc) {
            return;
        }
        const RECT r = toRect(rect);
        const int w = std::max(0, static_cast<int>(r.right - r.left));
        const int h = std::max(0, static_cast<int>(r.bottom - r.top));
        if (w <= 0 || h <= 0) {
            return;
        }

        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() != Gdiplus::Ok) {
            // Fallback: approximate with rounded rect
            fillRoundedRect(rect, std::min(w, h) / 2, fillColor, borderColor, borderWidth);
            return;
        }

        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(r.left);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(r.top);
        const Gdiplus::REAL rw = static_cast<Gdiplus::REAL>(w);
        const Gdiplus::REAL rh = static_cast<Gdiplus::REAL>(h);

        Gdiplus::SolidBrush brush(Gdiplus::Color(255, fillColor.r, fillColor.g, fillColor.b));
        graphics.FillEllipse(&brush, x, y, rw, rh);

        if (borderWidth > 0) {
            Gdiplus::Pen pen(Gdiplus::Color(255, borderColor.r, borderColor.g, borderColor.b),
                             static_cast<Gdiplus::REAL>(borderWidth));
            pen.SetAlignment(Gdiplus::PenAlignmentInset);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawEllipse(&pen, x, y, rw, rh);
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
        if (!m_hdc) {
            return;
        }
        const RECT r = toRect(rect);
        const int w = std::max(0, static_cast<int>(r.right - r.left));
        const int h = std::max(0, static_cast<int>(r.bottom - r.top));
        if (w <= 0 || h <= 0) {
            return;
        }

        const int bw = borderWidth > 0 ? borderWidth : 1;

        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() != Gdiplus::Ok) {
            drawRect(rect, borderColor, bw);
            return;
        }

        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(r.left);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(r.top);
        const Gdiplus::REAL rw = static_cast<Gdiplus::REAL>(w);
        const Gdiplus::REAL rh = static_cast<Gdiplus::REAL>(h);

        Gdiplus::Pen pen(Gdiplus::Color(255, borderColor.r, borderColor.g, borderColor.b),
                         static_cast<Gdiplus::REAL>(bw));
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawEllipse(&pen, x, y, rw, rh);
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
        if (!m_hdc) {
            return;
        }
        if (image.isNull() || image.format() != SwImage::Format_ARGB32) {
            return;
        }

        Gdiplus::Graphics graphics(m_hdc);
        if (graphics.GetLastStatus() != Gdiplus::Ok) {
            return;
        }
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

        const int imgW = image.width();
        const int imgH = image.height();
        const int stride = image.bytesPerLine();
        if (imgW <= 0 || imgH <= 0 || stride <= 0) {
            return;
        }

        Gdiplus::Bitmap bmp(imgW,
                            imgH,
                            stride,
                            PixelFormat32bppARGB,
                            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.constBits())));

        Gdiplus::Rect dest(targetRect.x, targetRect.y, targetRect.width, targetRect.height);
        if (!sourceRect) {
            graphics.DrawImage(&bmp, dest);
            return;
        }

        SwRect src = *sourceRect;
        if (src.width <= 0 || src.height <= 0) {
            return;
        }
        if (src.x < 0) {
            src.width += src.x;
            src.x = 0;
        }
        if (src.y < 0) {
            src.height += src.y;
            src.y = 0;
        }
        if (src.x + src.width > imgW) {
            src.width = imgW - src.x;
        }
        if (src.y + src.height > imgH) {
            src.height = imgH - src.y;
        }
        if (src.width <= 0 || src.height <= 0) {
            return;
        }

        graphics.DrawImage(&bmp,
                           dest,
                           static_cast<INT>(src.x),
                           static_cast<INT>(src.y),
                           static_cast<INT>(src.width),
                           static_cast<INT>(src.height),
                           Gdiplus::UnitPixel);
    }

    /**
     * @brief Returns the current native Handle.
     * @return The current native Handle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void* nativeHandle() override {
        return m_hdc;
    }

private:
    using ComPtrD2DFactory = Microsoft::WRL::ComPtr<ID2D1Factory>;
    using ComPtrDWriteFactory = Microsoft::WRL::ComPtr<IDWriteFactory>;
    using ComPtrDWriteFactory2 = Microsoft::WRL::ComPtr<IDWriteFactory2>;
    using ComPtrDWriteFactory4 = Microsoft::WRL::ComPtr<IDWriteFactory4>;
    using ComPtrDWriteFactory8 = Microsoft::WRL::ComPtr<IDWriteFactory8>;
    using ComPtrDWriteGdiInterop = Microsoft::WRL::ComPtr<IDWriteGdiInterop>;
    using ComPtrDWriteRenderingParams = Microsoft::WRL::ComPtr<IDWriteRenderingParams>;
    using ComPtrWicFactory = Microsoft::WRL::ComPtr<IWICImagingFactory>;

    static ComPtrD2DFactory& sharedD2DFactory_() {
        static ComPtrD2DFactory s_factory;
        static std::once_flag once;
        std::call_once(once, []() {
            D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
            options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
            ID2D1Factory* rawFactory = nullptr;
            HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                           __uuidof(ID2D1Factory),
                                           &options,
                                           reinterpret_cast<void**>(&rawFactory));
            if (SUCCEEDED(hr) && rawFactory) {
                s_factory.Attach(rawFactory);
            }
        });
        return s_factory;
    }

    static ComPtrDWriteFactory& sharedDWriteFactory_() {
        static ComPtrDWriteFactory s_factory;
        static std::once_flag once;
        std::call_once(once, []() {
            IDWriteFactory* rawFactory = nullptr;
            HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                             __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(&rawFactory));
            if (SUCCEEDED(hr) && rawFactory) {
                s_factory.Attach(rawFactory);
            }
        });
        return s_factory;
    }

    static ComPtrDWriteFactory2& sharedDWriteFactory2_() {
        static ComPtrDWriteFactory2 s_factory2;
        static std::once_flag once;
        std::call_once(once, []() {
            IDWriteFactory* base = sharedDWriteFactory_().Get();
            if (!base) {
                return;
            }
            IDWriteFactory2* raw = nullptr;
            HRESULT hr = base->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&raw));
            if (SUCCEEDED(hr) && raw) {
                s_factory2.Attach(raw);
            }
        });
        return s_factory2;
    }

    static ComPtrDWriteFactory4& sharedDWriteFactory4_() {
        static ComPtrDWriteFactory4 s_factory4;
        static std::once_flag once;
        std::call_once(once, []() {
            IDWriteFactory* base = sharedDWriteFactory_().Get();
            if (!base) {
                return;
            }
            IDWriteFactory4* raw = nullptr;
            HRESULT hr = base->QueryInterface(__uuidof(IDWriteFactory4), reinterpret_cast<void**>(&raw));
            if (SUCCEEDED(hr) && raw) {
                s_factory4.Attach(raw);
            }
        });
        return s_factory4;
    }

    static ComPtrDWriteFactory8& sharedDWriteFactory8_() {
        static ComPtrDWriteFactory8 s_factory8;
        static std::once_flag once;
        std::call_once(once, []() {
            IDWriteFactory* base = sharedDWriteFactory_().Get();
            if (!base) {
                return;
            }
            IDWriteFactory8* raw = nullptr;
            HRESULT hr = base->QueryInterface(__uuidof(IDWriteFactory8), reinterpret_cast<void**>(&raw));
            if (SUCCEEDED(hr) && raw) {
                s_factory8.Attach(raw);
            }
        });
        return s_factory8;
    }

    static ComPtrDWriteGdiInterop& sharedDWriteGdiInterop_() {
        static ComPtrDWriteGdiInterop s_interop;
        static std::once_flag once;
        std::call_once(once, []() {
            IDWriteFactory* factory = sharedDWriteFactory_().Get();
            if (!factory) {
                return;
            }
            IDWriteGdiInterop* raw = nullptr;
            HRESULT hr = factory->GetGdiInterop(&raw);
            if (SUCCEEDED(hr) && raw) {
                s_interop.Attach(raw);
            }
        });
        return s_interop;
    }

    static ComPtrDWriteRenderingParams& sharedDWriteRenderingParams_() {
        static ComPtrDWriteRenderingParams s_params;
        static std::once_flag once;
        std::call_once(once, []() {
            IDWriteFactory* factory = sharedDWriteFactory_().Get();
            if (!factory) {
                return;
            }
            IDWriteRenderingParams* raw = nullptr;
            HRESULT hr = factory->CreateRenderingParams(&raw);
            if (SUCCEEDED(hr) && raw) {
                s_params.Attach(raw);
            }
        });
        return s_params;
    }

    static ComPtrWicFactory& sharedWicFactory_() {
        static ComPtrWicFactory s_factory;
        static std::once_flag once;
        std::call_once(once, []() {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (hr == RPC_E_CHANGED_MODE) {
                hr = S_OK;
            }
            if (FAILED(hr)) {
                return;
            }

            IWICImagingFactory* raw = nullptr;
            hr = CoCreateInstance(CLSID_WICImagingFactory2,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IWICImagingFactory),
                                  reinterpret_cast<void**>(&raw));
            if (FAILED(hr) || !raw) {
                hr = CoCreateInstance(CLSID_WICImagingFactory,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      __uuidof(IWICImagingFactory),
                                      reinterpret_cast<void**>(&raw));
            }
            if (SUCCEEDED(hr) && raw) {
                s_factory.Attach(raw);
            }
        });
        return s_factory;
    }

    static bool likelyEmojiText_(const std::wstring& wText) {
        // Surrogate pairs (U+1Fxxx emoji live there) or VS16 (emoji presentation) or misc symbols.
        for (wchar_t ch : wText) {
            if (ch >= 0xD800 && ch <= 0xDBFF) {
                return true;
            }
            if (ch == 0xFE0F) { // Variation Selector-16
                return true;
            }
            if (ch >= 0x2600 && ch <= 0x27BF) { // Misc symbols + dingbats (BMP emojis)
                return true;
            }
        }
        return false;
    }

    bool ensureDWriteBitmapTarget_(UINT32 width, UINT32 height) {
        if (width == 0 || height == 0) {
            return false;
        }

        if (m_dwriteBitmapTarget && m_bitmapTargetWidth == width && m_bitmapTargetHeight == height) {
            return true;
        }

        if (m_dwriteBitmapTarget) {
            HRESULT hr = m_dwriteBitmapTarget->Resize(width, height);
            if (SUCCEEDED(hr)) {
                m_bitmapTargetWidth = width;
                m_bitmapTargetHeight = height;
                return true;
            }
            m_dwriteBitmapTarget.Reset();
        }

        IDWriteGdiInterop* interop = sharedDWriteGdiInterop_().Get();
        if (!interop) {
            return false;
        }

        Microsoft::WRL::ComPtr<IDWriteBitmapRenderTarget> target;
        HRESULT hr = interop->CreateBitmapRenderTarget(m_hdc, width, height, target.GetAddressOf());
        if (FAILED(hr) || !target) {
            return false;
        }

        // We operate in "pixel == DIP" coordinates (96 DPI). Keep pixelsPerDip = 1.
        target->SetPixelsPerDip(1.0f);
        target->SetCurrentTransform(nullptr);

        // Prefer grayscale AA for correct alpha blending (ClearType is not suitable for transparent intermediates).
        Microsoft::WRL::ComPtr<IDWriteBitmapRenderTarget1> target1;
        if (SUCCEEDED(target.As(&target1)) && target1) {
            target1->SetTextAntialiasMode(DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }

        m_dwriteBitmapTarget = target;
        m_bitmapTargetWidth = width;
        m_bitmapTargetHeight = height;
        return true;
    }

    bool clearDWriteBitmapTargetTransparent_() const {
        if (!m_dwriteBitmapTarget) {
            return false;
        }
        HDC dc = m_dwriteBitmapTarget->GetMemoryDC();
        if (!dc) {
            return false;
        }
        HBITMAP bmp = static_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP));
        if (!bmp) {
            return false;
        }
        DIBSECTION ds{};
        if (GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || !ds.dsBm.bmBits) {
            return false;
        }

        const int h = std::abs(ds.dsBm.bmHeight);
        const int stride = ds.dsBm.bmWidthBytes;
        if (h <= 0 || stride <= 0) {
            return false;
        }
        std::memset(ds.dsBm.bmBits, 0, static_cast<std::size_t>(h) * static_cast<std::size_t>(stride));
        return true;
    }

    class BitmapColorTextRenderer final : public IDWriteTextRenderer {
    public:
        /**
         * @brief Performs the `BitmapColorTextRenderer` operation.
         * @param target Value passed to the method.
         * @param factory8 Value passed to the method.
         * @param renderingParams Value passed to the method.
         * @param textColor Value passed to the method.
         * @param colorPaletteIndex Value passed to the method.
         */
        BitmapColorTextRenderer(IDWriteBitmapRenderTarget3* target,
                                IDWriteFactory8* factory8,
                                IDWriteRenderingParams* renderingParams,
                                COLORREF textColor,
                                UINT32 colorPaletteIndex)
            : m_target(target)
            , m_factory8(factory8)
            , m_renderingParams(renderingParams)
            , m_textColor(textColor)
            , m_colorPaletteIndex(colorPaletteIndex) {
            if (m_target) m_target->AddRef();
            if (m_factory8) m_factory8->AddRef();
            if (m_renderingParams) m_renderingParams->AddRef();
        }

        /**
         * @brief Destroys the `final` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~BitmapColorTextRenderer() {
            if (m_target) m_target->Release();
            if (m_factory8) m_factory8->Release();
            if (m_renderingParams) m_renderingParams->Release();
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) override {
            if (!ppvObject) {
                return E_POINTER;
            }
            *ppvObject = nullptr;
            if (iid == __uuidof(IUnknown) ||
                iid == __uuidof(IDWritePixelSnapping) ||
                iid == __uuidof(IDWriteTextRenderer)) {
                *ppvObject = this;
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
        }

        ULONG STDMETHODCALLTYPE Release() override {
            const ULONG ref = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
            if (ref == 0) {
                delete this;
            }
            return ref;
        }

        HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override {
            if (!isDisabled) {
                return E_POINTER;
            }
            *isDisabled = FALSE;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
            if (!transform) {
                return E_POINTER;
            }
            if (!m_target) {
                *transform = DWRITE_MATRIX{1, 0, 0, 1, 0, 0};
                return S_OK;
            }
            return m_target->GetCurrentTransform(transform);
        }

        HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
            if (!pixelsPerDip) {
                return E_POINTER;
            }
            *pixelsPerDip = m_target ? m_target->GetPixelsPerDip() : 1.0f;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,
                                              FLOAT baselineOriginX,
                                              FLOAT baselineOriginY,
                                              DWRITE_MEASURING_MODE measuringMode,
                                              const DWRITE_GLYPH_RUN* glyphRun,
                                              const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
                                              IUnknown*) override {
            if (!m_target || !glyphRun || !m_renderingParams) {
                return E_FAIL;
            }

            if (!m_factory8) {
                return m_target->DrawGlyphRunWithColorSupport(baselineOriginX,
                                                             baselineOriginY,
                                                             measuringMode,
                                                             glyphRun,
                                                             m_renderingParams,
                                                             m_textColor,
                                                             m_colorPaletteIndex,
                                                             nullptr);
            }

            DWRITE_MATRIX currentTransform{};
            if (FAILED(m_target->GetCurrentTransform(&currentTransform))) {
                currentTransform = DWRITE_MATRIX{1, 0, 0, 1, 0, 0};
            }

            const FLOAT pixelsPerDip = m_target->GetPixelsPerDip();
            DWRITE_MATRIX worldToDevice = currentTransform;
            worldToDevice.m11 *= pixelsPerDip;
            worldToDevice.m12 *= pixelsPerDip;
            worldToDevice.m21 *= pixelsPerDip;
            worldToDevice.m22 *= pixelsPerDip;
            worldToDevice.dx *= pixelsPerDip;
            worldToDevice.dy *= pixelsPerDip;

            Microsoft::WRL::ComPtr<IDWriteColorGlyphRunEnumerator1> enumRuns;
            const DWRITE_GLYPH_IMAGE_FORMATS desiredFormats =
                static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(
                    DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE |
                    DWRITE_GLYPH_IMAGE_FORMATS_COLR |
                    DWRITE_GLYPH_IMAGE_FORMATS_SVG |
                    DWRITE_GLYPH_IMAGE_FORMATS_PNG |
                    DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
                    DWRITE_GLYPH_IMAGE_FORMATS_TIFF |
                    DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8);

            const DWRITE_PAINT_FEATURE_LEVEL paintFeatureLevel = m_target->GetPaintFeatureLevel();
            const DWRITE_PAINT_FEATURE_LEVEL requestedPaintLevel =
                (paintFeatureLevel < DWRITE_PAINT_FEATURE_LEVEL_COLR_V1) ? DWRITE_PAINT_FEATURE_LEVEL_COLR_V1 : paintFeatureLevel;
            HRESULT hr = m_factory8->TranslateColorGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                                            glyphRun,
                                                            glyphRunDescription,
                                                            desiredFormats,
                                                            requestedPaintLevel,
                                                            measuringMode,
                                                            &worldToDevice,
                                                            m_colorPaletteIndex,
                                                            enumRuns.GetAddressOf());

            if (hr == DWRITE_E_NOCOLOR || !enumRuns) {
                return m_target->DrawGlyphRunWithColorSupport(baselineOriginX,
                                                             baselineOriginY,
                                                             measuringMode,
                                                             glyphRun,
                                                             m_renderingParams,
                                                             m_textColor,
                                                             m_colorPaletteIndex,
                                                             nullptr);
            }

            if (FAILED(hr)) {
                return m_target->DrawGlyphRunWithColorSupport(baselineOriginX,
                                                             baselineOriginY,
                                                             measuringMode,
                                                             glyphRun,
                                                             m_renderingParams,
                                                             m_textColor,
                                                             m_colorPaletteIndex,
                                                             nullptr);
            }

            BOOL hasRun = FALSE;
            while (SUCCEEDED(enumRuns->MoveNext(&hasRun)) && hasRun) {
                const DWRITE_COLOR_GLYPH_RUN1* run = nullptr;
                if (FAILED(enumRuns->GetCurrentRun(&run)) || !run) {
                    break;
                }

                if (run->glyphImageFormat == DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE) {
                    // Render COLR v1 paint tree.
                    m_target->DrawPaintGlyphRun(run->baselineOriginX,
                                                run->baselineOriginY,
                                                run->measuringMode,
                                                &run->glyphRun,
                                                run->glyphImageFormat,
                                                m_textColor,
                                                m_colorPaletteIndex,
                                                nullptr);
                    continue;
                }

                if (run->glyphImageFormat == DWRITE_GLYPH_IMAGE_FORMATS_COLR) {
                    // Render COLR v0 layers using the provided palette color.
                    COLORREF layerColor = m_textColor;
                    if (run->paletteIndex != DWRITE_NO_PALETTE_INDEX) {
                        const int r = std::max(0, std::min(255, static_cast<int>(run->runColor.r * 255.0f + 0.5f)));
                        const int g = std::max(0, std::min(255, static_cast<int>(run->runColor.g * 255.0f + 0.5f)));
                        const int b = std::max(0, std::min(255, static_cast<int>(run->runColor.b * 255.0f + 0.5f)));
                        layerColor = RGB(r, g, b);
                    }
                    m_target->DrawGlyphRun(run->baselineOriginX,
                                           run->baselineOriginY,
                                           run->measuringMode,
                                           &run->glyphRun,
                                           m_renderingParams,
                                           layerColor,
                                           nullptr);
                    continue;
                }

                // Let DirectWrite render any bitmap/SVG fallbacks it supports.
                m_target->DrawGlyphRunWithColorSupport(run->baselineOriginX,
                                                      run->baselineOriginY,
                                                      run->measuringMode,
                                                      &run->glyphRun,
                                                      m_renderingParams,
                                                      m_textColor,
                                                      m_colorPaletteIndex,
                                                      nullptr);
            }

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawUnderline(void*,
                                                FLOAT,
                                                FLOAT,
                                                const DWRITE_UNDERLINE*,
                                                IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,
                                                     FLOAT,
                                                     FLOAT,
                                                     const DWRITE_STRIKETHROUGH*,
                                                     IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,
                                                   FLOAT,
                                                   FLOAT,
                                                   IDWriteInlineObject*,
                                                   BOOL,
                                                   BOOL,
                                                   IUnknown*) override {
            return S_OK;
        }

    private:
        LONG m_refCount{1};
        IDWriteBitmapRenderTarget3* m_target{nullptr};
        IDWriteFactory8* m_factory8{nullptr};
        IDWriteRenderingParams* m_renderingParams{nullptr};
        COLORREF m_textColor{RGB(0, 0, 0)};
        UINT32 m_colorPaletteIndex{0};
    };

    bool drawTextColorEmoji_(const SwRect& rect,
                             const std::wstring& wText,
                             DrawTextFormats formats,
                             const SwColor& color,
                             const SwFont& font) {
        if (!m_hdc) {
            return false;
        }
        if (rect.width <= 0 || rect.height <= 0) {
            return false;
        }

        if (!ensureDWriteFormat_(font)) {
            return false;
        }
        configureDWriteFormat_(formats);

        if (!ensureDWriteBitmapTarget_(static_cast<UINT32>(rect.width), static_cast<UINT32>(rect.height))) {
            return false;
        }

        Microsoft::WRL::ComPtr<IDWriteBitmapRenderTarget3> target3;
        if (FAILED(m_dwriteBitmapTarget.As(&target3)) || !target3) {
            return false;
        }

        if (!clearDWriteBitmapTargetTransparent_()) {
            return false;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        HRESULT hr = sharedDWriteFactory_()->CreateTextLayout(wText.c_str(),
                                                              static_cast<UINT32>(wText.size()),
                                                              m_dwriteFormat.Get(),
                                                              static_cast<FLOAT>(rect.width),
                                                              static_cast<FLOAT>(rect.height),
                                                              layout.GetAddressOf());
        if (FAILED(hr) || !layout) {
            return false;
        }

        if (font.isUnderline()) {
            DWRITE_TEXT_RANGE range{};
            range.startPosition = 0;
            range.length = static_cast<UINT32>(wText.size());
            layout->SetUnderline(TRUE, range);
        }

        IDWriteRenderingParams* renderingParams = sharedDWriteRenderingParams_().Get();
        if (!renderingParams) {
            return false;
        }

        IDWriteFactory8* factory8 = sharedDWriteFactory8_().Get();
        BitmapColorTextRenderer* renderer =
            new BitmapColorTextRenderer(target3.Get(),
                                        factory8,
                                        renderingParams,
                                        toColorRef(color),
                                        0);

        hr = layout->Draw(nullptr, renderer, 0.0f, 0.0f);
        renderer->Release();
        if (FAILED(hr)) {
            return false;
        }

        HDC srcDc = m_dwriteBitmapTarget->GetMemoryDC();
        if (!srcDc) {
            return false;
        }

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        const BOOL blended = AlphaBlend(m_hdc,
                                        rect.x,
                                        rect.y,
                                        rect.width,
                                        rect.height,
                                        srcDc,
                                        0,
                                        0,
                                        rect.width,
                                        rect.height,
                                        blend);
        return blended != FALSE;
    }

    struct PngGlyphCacheKey {
        std::uintptr_t fontFacePtr{0};
        UINT16 glyphId{0};
        UINT32 requestedPixelsPerEm{0};

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const PngGlyphCacheKey& other) const {
            return fontFacePtr == other.fontFacePtr &&
                   glyphId == other.glyphId &&
                   requestedPixelsPerEm == other.requestedPixelsPerEm;
        }
    };

    struct PngGlyphCacheKeyHash {
        /**
         * @brief Performs the `operator` operation.
         * @param key Value passed to the method.
         * @return The requested operator.
         */
        std::size_t operator()(const PngGlyphCacheKey& key) const noexcept {
            std::size_t h = static_cast<std::size_t>(key.fontFacePtr);
            h ^= static_cast<std::size_t>(key.glyphId) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.requestedPixelsPerEm) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct PngGlyphCacheEntry {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        UINT32 pixelsPerEm{0};
        D2D1_SIZE_U pixelSize{0, 0};
        D2D1_POINT_2L horizontalLeftOrigin{0, 0};
    };

    bool createD2DBitmapFromPngData_(const void* data, UINT32 size, ID2D1Bitmap** outBitmap) const {
        if (!outBitmap) {
            return false;
        }
        *outBitmap = nullptr;

        if (!m_d2dTarget || !data || size == 0) {
            return false;
        }

        IWICImagingFactory* wic = sharedWicFactory_().Get();
        if (!wic) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICStream> stream;
        HRESULT hr = wic->CreateStream(stream.GetAddressOf());
        if (FAILED(hr) || !stream) {
            return false;
        }

        hr = stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(data)),
                                          static_cast<DWORD>(size));
        if (FAILED(hr)) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = wic->CreateDecoderFromStream(stream.Get(),
                                          nullptr,
                                          WICDecodeMetadataCacheOnLoad,
                                          decoder.GetAddressOf());
        if (FAILED(hr) || !decoder) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr) || !frame) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = wic->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr) || !converter) {
            return false;
        }

        hr = converter->Initialize(frame.Get(),
                                   GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            return false;
        }

        hr = m_d2dTarget->CreateBitmapFromWicBitmap(converter.Get(), nullptr, outBitmap);
        return SUCCEEDED(hr) && *outBitmap;
    }

    const PngGlyphCacheEntry* getOrCreatePngGlyph_(IDWriteFontFace4* fontFace4,
                                                   UINT16 glyphId,
                                                   UINT32 requestedPixelsPerEm) {
        if (!fontFace4 || !m_d2dTarget) {
            return nullptr;
        }
        if (requestedPixelsPerEm == 0) {
            requestedPixelsPerEm = 1;
        }

        PngGlyphCacheKey key;
        key.fontFacePtr = reinterpret_cast<std::uintptr_t>(fontFace4);
        key.glyphId = glyphId;
        key.requestedPixelsPerEm = requestedPixelsPerEm;

        auto it = m_pngGlyphCache.find(key);
        if (it != m_pngGlyphCache.end()) {
            return &it->second;
        }

        DWRITE_GLYPH_IMAGE_DATA glyphData{};
        void* glyphDataContext = nullptr;
        HRESULT hr = fontFace4->GetGlyphImageData(glyphId,
                                                  requestedPixelsPerEm,
                                                  DWRITE_GLYPH_IMAGE_FORMATS_PNG,
                                                  &glyphData,
                                                  &glyphDataContext);
        if (FAILED(hr)) {
            return nullptr;
        }

        if (!glyphData.imageData || glyphData.imageDataSize == 0) {
            if (glyphDataContext) {
                fontFace4->ReleaseGlyphImageData(glyphDataContext);
            }
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        if (!createD2DBitmapFromPngData_(glyphData.imageData, glyphData.imageDataSize, bitmap.GetAddressOf()) || !bitmap) {
            fontFace4->ReleaseGlyphImageData(glyphDataContext);
            return nullptr;
        }

        PngGlyphCacheEntry entry;
        entry.bitmap = bitmap;
        entry.pixelsPerEm = glyphData.pixelsPerEm;
        entry.pixelSize = glyphData.pixelSize;
        entry.horizontalLeftOrigin = glyphData.horizontalLeftOrigin;

        fontFace4->ReleaseGlyphImageData(glyphDataContext);

        auto inserted = m_pngGlyphCache.emplace(key, std::move(entry));
        if (!inserted.second) {
            return nullptr;
        }
        return &inserted.first->second;
    }

    bool ensureD2DTarget_() {
        if (m_d2dTarget) {
            return true;
        }
        ID2D1Factory* factory = sharedD2DFactory_().Get();
        if (!factory) {
            return false;
        }
        // NOTE: DCRenderTarget requires an explicit pixel format on some setups (default/unknown can fail).
        D2D1_RENDER_TARGET_PROPERTIES props =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                         0.0f,
                                         0.0f,
                                         D2D1_RENDER_TARGET_USAGE_NONE,
                                         D2D1_FEATURE_LEVEL_DEFAULT);
        HRESULT hr = factory->CreateDCRenderTarget(&props, m_d2dTarget.GetAddressOf());
        if (FAILED(hr)) {
            m_d2dTarget.Reset();
            return false;
        }
        return true;
    }

    bool ensureDWriteFormat_(const SwFont& font) {
        if (m_dwriteFormat && m_hasDWriteFontCache && font == m_cachedDWriteFont) {
            return true;
        }

        IDWriteFactory* factory = sharedDWriteFactory_().Get();
        if (!factory) {
            return false;
        }

        std::wstring family = font.getFamily();
        if (family.empty()) {
            family = L"Segoe UI";
        }

        const int pt = std::max(1, font.getPointSize());
        const FLOAT fontSizeDip = static_cast<FLOAT>(pt) * (96.0f / 72.0f);

        const DWRITE_FONT_WEIGHT weight = static_cast<DWRITE_FONT_WEIGHT>(font.getWeight());
        const DWRITE_FONT_STYLE style = font.isItalic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        HRESULT hr = factory->CreateTextFormat(family.c_str(),
                                              nullptr,
                                              weight,
                                              style,
                                              DWRITE_FONT_STRETCH_NORMAL,
                                              fontSizeDip,
                                              L"",
                                              fmt.GetAddressOf());
        if (FAILED(hr) || !fmt) {
            m_dwriteFormat.Reset();
            m_hasDWriteFontCache = false;
            return false;
        }

        m_dwriteFormat = fmt;
        m_cachedDWriteFont = font;
        m_hasDWriteFontCache = true;
        return true;
    }

    void configureDWriteFormat_(DrawTextFormats formats) {
        if (!m_dwriteFormat) {
            return;
        }

        DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
        if (formats.testFlag(DrawTextFormat::Center)) {
            textAlign = DWRITE_TEXT_ALIGNMENT_CENTER;
        } else if (formats.testFlag(DrawTextFormat::Right)) {
            textAlign = DWRITE_TEXT_ALIGNMENT_TRAILING;
        }
        m_dwriteFormat->SetTextAlignment(textAlign);

        // Vertical alignment (matches Win32 DrawText: only meaningful with DT_SINGLELINE).
        DWRITE_PARAGRAPH_ALIGNMENT paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        if (formats.testFlag(DrawTextFormat::SingleLine)) {
            if (formats.testFlag(DrawTextFormat::VCenter)) {
                paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
            } else if (formats.testFlag(DrawTextFormat::Bottom)) {
                paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_FAR;
            }
        }
        m_dwriteFormat->SetParagraphAlignment(paraAlign);

        const bool wrap = formats.testFlag(DrawTextFormat::WordBreak) && !formats.testFlag(DrawTextFormat::SingleLine);
        m_dwriteFormat->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    bool ensureTextBrush_(const SwColor& color) {
        if (!m_d2dTarget) {
            return false;
        }
        if (m_textBrush && m_hasBrushColorCache &&
            color.r == m_cachedBrushColor.r &&
            color.g == m_cachedBrushColor.g &&
            color.b == m_cachedBrushColor.b) {
            return true;
        }

        m_textBrush.Reset();
        D2D1_COLOR_F c = D2D1::ColorF(static_cast<FLOAT>(color.r) / 255.0f,
                                      static_cast<FLOAT>(color.g) / 255.0f,
                                      static_cast<FLOAT>(color.b) / 255.0f,
                                      1.0f);
        HRESULT hr = m_d2dTarget->CreateSolidColorBrush(c, m_textBrush.GetAddressOf());
        if (FAILED(hr) || !m_textBrush) {
            m_textBrush.Reset();
            m_hasBrushColorCache = false;
            return false;
        }
        m_cachedBrushColor = color;
        m_hasBrushColorCache = true;
        return true;
    }

    class ColorTextRenderer final : public IDWriteTextRenderer {
    public:
        /**
         * @brief Performs the `ColorTextRenderer` operation.
         * @param owner Value passed to the method.
         * @param target Value passed to the method.
         * @param factory4 Value passed to the method.
         * @param factory2 Value passed to the method.
         * @param fallbackBrush Value passed to the method.
         */
        ColorTextRenderer(SwWin32Painter* owner,
                          ID2D1RenderTarget* target,
                          IDWriteFactory4* factory4,
                          IDWriteFactory2* factory2,
                          ID2D1Brush* fallbackBrush)
            : m_owner(owner)
            , m_target(target)
            , m_factory4(factory4)
            , m_factory2(factory2)
            , m_fallbackBrush(fallbackBrush) {
            if (m_target) m_target->AddRef();
            if (m_factory4) m_factory4->AddRef();
            if (m_factory2) m_factory2->AddRef();
            if (m_fallbackBrush) m_fallbackBrush->AddRef();
        }

        /**
         * @brief Destroys the `final` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~ColorTextRenderer() {
            if (m_target) m_target->Release();
            if (m_factory4) m_factory4->Release();
            if (m_factory2) m_factory2->Release();
            if (m_fallbackBrush) m_fallbackBrush->Release();
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) override {
            if (!ppvObject) {
                return E_POINTER;
            }
            *ppvObject = nullptr;
            if (iid == __uuidof(IUnknown) ||
                iid == __uuidof(IDWritePixelSnapping) ||
                iid == __uuidof(IDWriteTextRenderer)) {
                *ppvObject = this;
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
        }

        ULONG STDMETHODCALLTYPE Release() override {
            const ULONG ref = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
            if (ref == 0) {
                delete this;
            }
            return ref;
        }

        HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override {
            if (!isDisabled) {
                return E_POINTER;
            }
            *isDisabled = FALSE;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
            if (!transform) {
                return E_POINTER;
            }
            if (!m_target) {
                *transform = DWRITE_MATRIX{1, 0, 0, 1, 0, 0};
                return S_OK;
            }
            D2D1_MATRIX_3X2_F m{};
            m_target->GetTransform(&m);
            transform->m11 = m._11;
            transform->m12 = m._12;
            transform->m21 = m._21;
            transform->m22 = m._22;
            transform->dx = m._31;
            transform->dy = m._32;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
            if (!pixelsPerDip) {
                return E_POINTER;
            }
            *pixelsPerDip = 1.0f;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,
                                              FLOAT baselineOriginX,
                                              FLOAT baselineOriginY,
                                              DWRITE_MEASURING_MODE measuringMode,
                                              const DWRITE_GLYPH_RUN* glyphRun,
                                              const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
                                              IUnknown*) override {
            if (!m_target || !glyphRun) {
                return S_OK;
            }

            if (!glyphRunDescription) {
                m_target->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                       glyphRun,
                                       m_fallbackBrush,
                                       measuringMode);
                return S_OK;
            }

            // Build the world+DPI transform so DirectWrite can choose the best glyph image size and align to pixels.
            FLOAT dpiX = 96.0f;
            FLOAT dpiY = 96.0f;
            m_target->GetDpi(&dpiX, &dpiY);
            DWRITE_MATRIX worldAndDpiTransform{};
            worldAndDpiTransform.m11 = dpiX / 96.0f;
            worldAndDpiTransform.m12 = 0.0f;
            worldAndDpiTransform.m21 = 0.0f;
            worldAndDpiTransform.m22 = dpiY / 96.0f;
            worldAndDpiTransform.dx = 0.0f;
            worldAndDpiTransform.dy = 0.0f;

            // Preferred path: Factory4 returns PNG/bitmap glyph runs for emoji fonts (Segoe UI Emoji).
            if (m_factory4) {
                const DWRITE_GLYPH_IMAGE_FORMATS desiredFormats =
                    static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(
                        DWRITE_GLYPH_IMAGE_FORMATS_PNG |
                        DWRITE_GLYPH_IMAGE_FORMATS_COLR);

                Microsoft::WRL::ComPtr<IDWriteColorGlyphRunEnumerator1> enumRuns1;
                HRESULT hr = m_factory4->TranslateColorGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                                                glyphRun,
                                                                glyphRunDescription,
                                                                desiredFormats,
                                                                measuringMode,
                                                                &worldAndDpiTransform,
                                                                0,
                                                                enumRuns1.GetAddressOf());

                if (hr == DWRITE_E_NOCOLOR || !enumRuns1) {
                    m_target->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                           glyphRun,
                                           m_fallbackBrush,
                                           measuringMode);
                    return S_OK;
                }
                if (SUCCEEDED(hr) && enumRuns1) {
                    BOOL hasRun = FALSE;
                    while (SUCCEEDED(enumRuns1->MoveNext(&hasRun)) && hasRun) {
                        const DWRITE_COLOR_GLYPH_RUN1* run = nullptr;
                        if (FAILED(enumRuns1->GetCurrentRun(&run)) || !run) {
                            break;
                        }

                        const bool isPng = (run->glyphImageFormat == DWRITE_GLYPH_IMAGE_FORMATS_PNG);
                        if (isPng) {
                            drawPngGlyphRun_(*run, worldAndDpiTransform, dpiY);
                            continue;
                        }

                        ID2D1Brush* brush = m_fallbackBrush;
                        if (run->runColor.a > 0.0f) {
                            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> layerBrush;
                            D2D1_COLOR_F c = D2D1::ColorF(run->runColor.r, run->runColor.g, run->runColor.b, run->runColor.a);
                            if (SUCCEEDED(m_target->CreateSolidColorBrush(c, layerBrush.GetAddressOf())) && layerBrush) {
                                brush = layerBrush.Get();
                            }
                        }

                        m_target->DrawGlyphRun(D2D1::Point2F(run->baselineOriginX, run->baselineOriginY),
                                               &run->glyphRun,
                                               brush,
                                               run->measuringMode);
                    }
                    return S_OK;
                }
            }

            // Fallback: Factory2 only supports COLR/CPAL layered glyphs.
            if (m_factory2) {
                Microsoft::WRL::ComPtr<IDWriteColorGlyphRunEnumerator> enumRuns;
                HRESULT hr = m_factory2->TranslateColorGlyphRun(baselineOriginX,
                                                                baselineOriginY,
                                                                glyphRun,
                                                                glyphRunDescription,
                                                                measuringMode,
                                                                nullptr,
                                                                0,
                                                                enumRuns.GetAddressOf());

                if (hr == DWRITE_E_NOCOLOR || !enumRuns) {
                    m_target->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                           glyphRun,
                                           m_fallbackBrush,
                                           measuringMode);
                    return S_OK;
                }

                if (FAILED(hr)) {
                    m_target->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                           glyphRun,
                                           m_fallbackBrush,
                                           measuringMode);
                    return S_OK;
                }

                BOOL hasRun = FALSE;
                while (SUCCEEDED(enumRuns->MoveNext(&hasRun)) && hasRun) {
                    const DWRITE_COLOR_GLYPH_RUN* run = nullptr;
                    if (FAILED(enumRuns->GetCurrentRun(&run)) || !run) {
                        break;
                    }

                    ID2D1Brush* brush = m_fallbackBrush;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> layerBrush;
                    D2D1_COLOR_F c = D2D1::ColorF(run->runColor.r, run->runColor.g, run->runColor.b, run->runColor.a);
                    if (SUCCEEDED(m_target->CreateSolidColorBrush(c, layerBrush.GetAddressOf())) && layerBrush) {
                        brush = layerBrush.Get();
                    }

                    m_target->DrawGlyphRun(D2D1::Point2F(run->baselineOriginX, run->baselineOriginY),
                                           &run->glyphRun,
                                           brush,
                                           measuringMode);
                }

                return S_OK;
            }

            m_target->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                                   glyphRun,
                                   m_fallbackBrush,
                                   measuringMode);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawUnderline(void*,
                                                FLOAT,
                                                FLOAT,
                                                const DWRITE_UNDERLINE*,
                                                IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,
                                                     FLOAT,
                                                     FLOAT,
                                                     const DWRITE_STRIKETHROUGH*,
                                                     IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,
                                                   FLOAT,
                                                   FLOAT,
                                                   IDWriteInlineObject*,
                                                   BOOL,
                                                   BOOL,
                                                   IUnknown*) override {
            return S_OK;
        }

    private:
        void drawPngGlyphRun_(const DWRITE_COLOR_GLYPH_RUN1& run,
                              const DWRITE_MATRIX& worldAndDpiTransform,
                              FLOAT dpiY) const {
            if (!m_owner || !m_target || !m_factory4) {
                return;
            }
            if (!run.glyphRun.fontFace || run.glyphRun.glyphCount == 0) {
                return;
            }

            Microsoft::WRL::ComPtr<IDWriteFontFace4> fontFace4;
            if (FAILED(run.glyphRun.fontFace->QueryInterface(__uuidof(IDWriteFontFace4), reinterpret_cast<void**>(fontFace4.GetAddressOf()))) || !fontFace4) {
                return;
            }

            const FLOAT fontEmSizeDip = run.glyphRun.fontEmSize;
            const UINT32 requestedPixelsPerEm = static_cast<UINT32>(std::max(1.0f, std::floor((fontEmSizeDip * dpiY / 96.0f) + 0.5f)));

            std::vector<D2D1_POINT_2F> glyphOrigins(static_cast<std::size_t>(run.glyphRun.glyphCount));
            HRESULT hr = m_factory4->ComputeGlyphOrigins(&run.glyphRun,
                                                         run.measuringMode,
                                                         D2D1::Point2F(run.baselineOriginX, run.baselineOriginY),
                                                         &worldAndDpiTransform,
                                                         glyphOrigins.data());
            if (FAILED(hr)) {
                return;
            }

            for (UINT32 i = 0; i < run.glyphRun.glyphCount; ++i) {
                const UINT16 glyphId = run.glyphRun.glyphIndices ? run.glyphRun.glyphIndices[i] : 0;
                const PngGlyphCacheEntry* entry = m_owner->getOrCreatePngGlyph_(fontFace4.Get(), glyphId, requestedPixelsPerEm);
                if (!entry || !entry->bitmap || entry->pixelsPerEm == 0) {
                    continue;
                }

                const FLOAT dipPerImagePixel = fontEmSizeDip / static_cast<FLOAT>(entry->pixelsPerEm);
                const FLOAT x = glyphOrigins[i].x - (static_cast<FLOAT>(entry->horizontalLeftOrigin.x) * dipPerImagePixel);
                const FLOAT y = glyphOrigins[i].y - (static_cast<FLOAT>(entry->horizontalLeftOrigin.y) * dipPerImagePixel);
                const FLOAT w = static_cast<FLOAT>(entry->pixelSize.width) * dipPerImagePixel;
                const FLOAT h = static_cast<FLOAT>(entry->pixelSize.height) * dipPerImagePixel;

                m_target->DrawBitmap(entry->bitmap.Get(),
                                     D2D1::RectF(x, y, x + w, y + h),
                                     1.0f,
                                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }

        LONG m_refCount{1};
        SwWin32Painter* m_owner{nullptr};
        ID2D1RenderTarget* m_target{nullptr};
        IDWriteFactory4* m_factory4{nullptr};
        IDWriteFactory2* m_factory2{nullptr};
        ID2D1Brush* m_fallbackBrush{nullptr};
    };

    bool drawTextD2D_(const SwRect& rect,
                      const std::wstring& wText,
                      DrawTextFormats formats,
                      const SwColor& color,
                      const SwFont& font) {
        if (!ensureD2DTarget_()) {
            return false;
        }
        if (!ensureDWriteFormat_(font)) {
            return false;
        }
        configureDWriteFormat_(formats);

        RECT clip{};
        if (GetClipBox(m_hdc, &clip) == ERROR) {
            clip = toRect(rect);
        }
        if (clip.right <= clip.left || clip.bottom <= clip.top) {
            clip = toRect(rect);
        }

        const int originX = rect.x - clip.left;
        const int originY = rect.y - clip.top;

        HRESULT hr = m_d2dTarget->BindDC(m_hdc, &clip);
        if (FAILED(hr)) {
            m_d2dTarget.Reset();
            m_textBrush.Reset();
            m_pngGlyphCache.clear();
            return false;
        }

        if (!ensureTextBrush_(color)) {
            return false;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        hr = sharedDWriteFactory_()->CreateTextLayout(wText.c_str(),
                                                      static_cast<UINT32>(wText.size()),
                                                      m_dwriteFormat.Get(),
                                                       static_cast<FLOAT>(rect.width),
                                                       static_cast<FLOAT>(rect.height),
                                                       layout.GetAddressOf());
        if (FAILED(hr) || !layout) {
            return false;
        }

        if (font.isUnderline()) {
            DWRITE_TEXT_RANGE range{};
            range.startPosition = 0;
            range.length = static_cast<UINT32>(wText.size());
            layout->SetUnderline(TRUE, range);
        }

        D2D1_MATRIX_3X2_F oldTransform{};
        m_d2dTarget->GetTransform(&oldTransform);

        m_d2dTarget->BeginDraw();
        m_d2dTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        m_d2dTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);

        // Prefer Direct2D's built-in color-font support (COLR v1, SVG, bitmap emoji).
        m_d2dTarget->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(originX), static_cast<FLOAT>(originY)),
                                    layout.Get(),
                                    m_textBrush.Get(),
                                    static_cast<D2D1_DRAW_TEXT_OPTIONS>(D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT));

        hr = m_d2dTarget->EndDraw();
        m_d2dTarget->SetTransform(oldTransform);

        if (FAILED(hr)) {
            m_d2dTarget.Reset();
            m_textBrush.Reset();
            m_pngGlyphCache.clear();
            return false;
        }
        return true;
    }

    static RECT toRect(const SwRect& rect) {
        RECT r;
        r.left = rect.x;
        r.top = rect.y;
        r.right = rect.x + rect.width;
        r.bottom = rect.y + rect.height;
        return r;
    }

    static COLORREF toColorRef(const SwColor& color) {
        return RGB(color.r, color.g, color.b);
    }

    static HBRUSH createBrush(const SwColor& color) {
        return CreateSolidBrush(toColorRef(color));
    }

    static HPEN createPen(const SwColor& color, int width) {
        return CreatePen(PS_SOLID, width, toColorRef(color));
    }

    static UINT translateAlignment(DrawTextFormats formats) {
        return static_cast<UINT>(formats.raw());
    }

    HDC m_hdc{nullptr};
    int m_clipDepth{0};

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_d2dTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_dwriteFormat;
    SwFont m_cachedDWriteFont;
    bool m_hasDWriteFontCache{false};
    SwColor m_cachedBrushColor{0, 0, 0};
    bool m_hasBrushColorCache{false};
    std::unordered_map<PngGlyphCacheKey, PngGlyphCacheEntry, PngGlyphCacheKeyHash> m_pngGlyphCache;
    Microsoft::WRL::ComPtr<IDWriteBitmapRenderTarget> m_dwriteBitmapTarget;
    UINT32 m_bitmapTargetWidth{0};
    UINT32 m_bitmapTargetHeight{0};
};

#endif
