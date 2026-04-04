#pragma once

#if defined(_WIN32)
inline BYTE SwLoadingOverlay::alphaByte_(float alpha) {
    return static_cast<BYTE>(clampInt(static_cast<int>(std::round(clampDouble(alpha, 0.0, 1.0) * 255.0)), 0, 255));
}

inline Gdiplus::Color SwLoadingOverlay::toGdiColor_(const ColorSpec_& spec, float alphaScale) {
    const float scaledAlpha = static_cast<float>(clampDouble(spec.alpha * alphaScale, 0.0, 1.0));
    return Gdiplus::Color(alphaByte_(scaledAlpha),
                          clampInt(spec.color.r, 0, 255),
                          clampInt(spec.color.g, 0, 255),
                          clampInt(spec.color.b, 0, 255));
}

inline void SwLoadingOverlay::addRoundedRectPath_(Gdiplus::GraphicsPath& path, const SwRect& rect, int radius) {
    const int width = std::max(0, rect.width);
    const int height = std::max(0, rect.height);
    const int maxRadius = std::max(0, std::min(width, height) / 2);
    const int rr = clampInt(radius, 0, maxRadius);

    const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(rect.x);
    const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(rect.y);
    const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(width);
    const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(height);

    if (rr <= 0) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        path.CloseFigure();
        return;
    }

    const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(rr * 2);
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

inline bool SwLoadingOverlay::paintNativeAlpha_(SwPainter* painter) {
    if (!painter) {
        return false;
    }

    void* native = painter->nativeHandle();
    HDC hdc = native ? reinterpret_cast<HDC>(native) : nullptr;
    if (!hdc) {
        return false;
    }

    Gdiplus::Graphics graphics(hdc);
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    const SwRect localBounds = rect();
    const LayoutSpec_ localSpec = layoutSpec_();
    const SwRect bounds = painter->mapToDevice(localBounds);
    const SwRect panelRect = painter->mapToDevice(localSpec.panelRect);
    const SwRect spinnerRect = painter->mapToDevice(localSpec.spinnerRect);

    graphics.SetClip(Gdiplus::RectF(static_cast<Gdiplus::REAL>(bounds.x),
                                    static_cast<Gdiplus::REAL>(bounds.y),
                                    static_cast<Gdiplus::REAL>(bounds.width),
                                    static_cast<Gdiplus::REAL>(bounds.height)));

    if (getShowBackdrop()) {
        Gdiplus::SolidBrush overlayBrush(toGdiColor_(overlayColor_()));
        graphics.FillRectangle(&overlayBrush,
                               static_cast<Gdiplus::REAL>(bounds.x),
                               static_cast<Gdiplus::REAL>(bounds.y),
                               static_cast<Gdiplus::REAL>(bounds.width),
                               static_cast<Gdiplus::REAL>(bounds.height));
    }

    if (getShowPanel()) {
        paintPanelNative_(graphics, panelRect, resolveBoxRadius_());
    }

    paintSpinnerNative_(graphics, spinnerRect);
    return true;
}

inline void SwLoadingOverlay::paintPanelNative_(Gdiplus::Graphics& graphics,
                                                const SwRect& panelRect,
                                                int radius) {
    const ColorSpec_ shadow = panelShadowColor_();
    const ColorSpec_ fill = panelColor_();
    const ColorSpec_ border = panelBorderColor_();

    for (int i = 6; i >= 1; --i) {
        const int spread = i * 2;
        const SwRect shadowRect{
            panelRect.x - spread,
            panelRect.y - spread + i * 2,
            panelRect.width + spread * 2,
            panelRect.height + spread * 2};
        Gdiplus::GraphicsPath shadowPath;
        addRoundedRectPath_(shadowPath, shadowRect, radius + spread);
        Gdiplus::SolidBrush shadowBrush(toGdiColor_(shadow, 0.10f + static_cast<float>(i) * 0.03f));
        graphics.FillPath(&shadowBrush, &shadowPath);
    }

    Gdiplus::GraphicsPath panelPath;
    addRoundedRectPath_(panelPath, panelRect, radius);

    Gdiplus::SolidBrush fillBrush(toGdiColor_(fill));
    graphics.FillPath(&fillBrush, &panelPath);

    Gdiplus::Pen borderPen(toGdiColor_(border), 1.0f);
    borderPen.SetAlignment(Gdiplus::PenAlignmentInset);
    borderPen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&borderPen, &panelPath);
}

inline void SwLoadingOverlay::paintSpinnerNative_(Gdiplus::Graphics& graphics, const SwRect& spinnerRect) {
    if (resolveSpinnerStyle_() != "segments") {
        paintRingSpinnerNative_(graphics, spinnerRect);
        return;
    }

    const int count = resolveSegmentCount_();
    const int thickness = resolveSpinnerThickness_();
    const int segmentLength = std::min(resolveSegmentLength_(), spinnerRect.width / 2);
    const double radiusOuter = static_cast<double>(std::min(spinnerRect.width, spinnerRect.height)) * 0.5 - 2.0;
    const double radiusInner = std::max(3.0, radiusOuter - segmentLength);
    const double glowRadius = radiusOuter + std::max(2.0, static_cast<double>(thickness) * 0.45);
    const int cx = spinnerRect.x + spinnerRect.width / 2;
    const int cy = spinnerRect.y + spinnerRect.height / 2;

    const ColorSpec_ head = spinnerHeadColor_();
    const ColorSpec_ trail = spinnerTrailColor_();
    const ColorSpec_ glow = spinnerGlowColor_();

    const int haloDiameter = std::max(12, std::min(spinnerRect.width, spinnerRect.height) + thickness * 2);
    for (int i = 0; i < 3; ++i) {
        const int size = haloDiameter - i * (thickness + 4);
        const float alpha = 0.08f + static_cast<float>(i) * 0.05f;
        Gdiplus::SolidBrush haloBrush(toGdiColor_(glow, alpha));
        graphics.FillEllipse(&haloBrush,
                             static_cast<Gdiplus::REAL>(cx - size / 2),
                             static_cast<Gdiplus::REAL>(cy - size / 2),
                             static_cast<Gdiplus::REAL>(size),
                             static_cast<Gdiplus::REAL>(size));
    }

    static const double kPi = 3.14159265358979323846;
    for (int i = 0; i < count; ++i) {
        const double strength = segmentStrength_(m_animationFrame, i, count);
        const double angle = (-kPi * 0.5) + (static_cast<double>(i) * 2.0 * kPi / static_cast<double>(count));

        const Gdiplus::PointF innerPoint(
            static_cast<Gdiplus::REAL>(cx + std::cos(angle) * radiusInner),
            static_cast<Gdiplus::REAL>(cy + std::sin(angle) * radiusInner));
        const Gdiplus::PointF outerPoint(
            static_cast<Gdiplus::REAL>(cx + std::cos(angle) * radiusOuter),
            static_cast<Gdiplus::REAL>(cy + std::sin(angle) * radiusOuter));
        const Gdiplus::PointF glowPoint(
            static_cast<Gdiplus::REAL>(cx + std::cos(angle) * glowRadius),
            static_cast<Gdiplus::REAL>(cy + std::sin(angle) * glowRadius));

        const ColorSpec_ segmentColor = mixColorSpec_(trail, head, strength);

        Gdiplus::Pen glowPen(toGdiColor_(glow, static_cast<float>(0.12 + strength * 0.38)),
                             static_cast<Gdiplus::REAL>(thickness + 5));
        glowPen.SetStartCap(Gdiplus::LineCapRound);
        glowPen.SetEndCap(Gdiplus::LineCapRound);
        glowPen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawLine(&glowPen, innerPoint, glowPoint);

        Gdiplus::Pen segmentPen(toGdiColor_(segmentColor), static_cast<Gdiplus::REAL>(thickness));
        segmentPen.SetStartCap(Gdiplus::LineCapRound);
        segmentPen.SetEndCap(Gdiplus::LineCapRound);
        segmentPen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawLine(&segmentPen, innerPoint, outerPoint);

        const float orbAlpha = static_cast<float>(0.12 + strength * 0.52);
        Gdiplus::SolidBrush orbBrush(toGdiColor_(segmentColor, orbAlpha));
        const float orbRadius = static_cast<float>(std::max(2, thickness / 2 + 1));
        graphics.FillEllipse(&orbBrush,
                             outerPoint.X - orbRadius,
                             outerPoint.Y - orbRadius,
                             orbRadius * 2.0f,
                             orbRadius * 2.0f);
    }

    const float coreRadius = static_cast<float>(std::max(3, thickness));
    Gdiplus::SolidBrush coreBrush(toGdiColor_(head, 0.95f));
    graphics.FillEllipse(&coreBrush,
                         static_cast<Gdiplus::REAL>(cx) - coreRadius,
                         static_cast<Gdiplus::REAL>(cy) - coreRadius,
                         coreRadius * 2.0f,
                         coreRadius * 2.0f);
}

inline void SwLoadingOverlay::paintRingSpinnerNative_(Gdiplus::Graphics& graphics, const SwRect& spinnerRect) {
    const bool minimal = resolveSpinnerStyle_() == "minimal-ring";
    const int thickness = resolveSpinnerThickness_();
    const int cx = spinnerRect.x + spinnerRect.width / 2;
    const int cy = spinnerRect.y + spinnerRect.height / 2;
    const double radius = static_cast<double>(std::min(spinnerRect.width, spinnerRect.height)) * 0.5 -
                          std::max(3.0, static_cast<double>(thickness));
    const int trailSteps = std::max(18, resolveSegmentCount_() * 3);
    const double sweep = static_cast<double>(resolveSpinnerSweepDegrees_()) * 3.14159265358979323846 / 180.0;
    const double headAngle =
        (-3.14159265358979323846 * 0.5) +
        (static_cast<double>(m_animationFrame) * 2.0 * 3.14159265358979323846 /
         static_cast<double>(resolveSegmentCount_()));
    const ColorSpec_ head = spinnerHeadColor_();
    const ColorSpec_ trail = spinnerTrailColor_();
    const ColorSpec_ glow = spinnerGlowColor_();

    if (!minimal) {
        const int haloDiameter = std::max(12, std::min(spinnerRect.width, spinnerRect.height) + thickness * 2);
        for (int i = 0; i < 3; ++i) {
            const int size = haloDiameter - i * (thickness + 4);
            const float alpha = 0.05f + static_cast<float>(i) * 0.03f;
            Gdiplus::SolidBrush haloBrush(toGdiColor_(glow, alpha));
            graphics.FillEllipse(&haloBrush,
                                 static_cast<Gdiplus::REAL>(cx - size / 2),
                                 static_cast<Gdiplus::REAL>(cy - size / 2),
                                 static_cast<Gdiplus::REAL>(size),
                                 static_cast<Gdiplus::REAL>(size));
        }
    }

    for (int i = 0; i < trailSteps; ++i) {
        const double t0 = static_cast<double>(i) / static_cast<double>(trailSteps);
        const double t1 = static_cast<double>(i + 1) / static_cast<double>(trailSteps);
        const double a0 = headAngle - t0 * sweep;
        const double a1 = headAngle - t1 * sweep;
        const double strength = std::pow(1.0 - t0, minimal ? 2.2 : 1.6);
        const ColorSpec_ color = mixColorSpec_(trail, head, strength);
        const float width = static_cast<float>(std::max(
            1,
            static_cast<int>(std::round(thickness * (minimal ? 0.72 + strength * 0.18 : 0.82 + strength * 0.30)))));

        Gdiplus::Pen pen(toGdiColor_(color), width);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawLine(&pen,
                          Gdiplus::PointF(static_cast<Gdiplus::REAL>(cx + std::cos(a0) * radius),
                                          static_cast<Gdiplus::REAL>(cy + std::sin(a0) * radius)),
                          Gdiplus::PointF(static_cast<Gdiplus::REAL>(cx + std::cos(a1) * radius),
                                          static_cast<Gdiplus::REAL>(cy + std::sin(a1) * radius)));
    }

    if (!minimal) {
        const float orbRadius = static_cast<float>(std::max(3, thickness / 2 + 1));
        const float orbX = static_cast<float>(cx + std::cos(headAngle) * radius);
        const float orbY = static_cast<float>(cy + std::sin(headAngle) * radius);
        Gdiplus::SolidBrush orbBrush(toGdiColor_(head, 0.95f));
        graphics.FillEllipse(&orbBrush, orbX - orbRadius, orbY - orbRadius, orbRadius * 2.0f, orbRadius * 2.0f);
    }
}
#endif
