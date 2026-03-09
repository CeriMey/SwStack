#pragma once

/**
 * @file
 * @ingroup core_swizio_nodes
 * @brief Declares the helper that paints node-editor connections.
 *
 * `ConnectionPainter` centralizes how connection curves, end points, and styling rules are
 * drawn so the scene items do not duplicate low-level painting code. It is the rendering
 * policy layer between connection geometry and the underlying `SwPainter` API.
 */




#include "Export.hpp"
#include "StyleCollection.hpp"

#include "graphics/SwGraphicsRenderContext.h"

#include <algorithm>
#include <cmath>

class SwPainter;

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC ConnectionPainter
{
public:
    struct PaintData
    {
        SwPointF startScene{};
        SwPointF endScene{};

        // When set, overrides ConnectionStyle colors for the main stroke.
        bool useCustomColor{false};
        SwColor customColor{0, 0, 0};

        bool hovered{false};
        bool selected{false};
        bool complete{true};

        bool flowActive{false};
        double flowPhase{0.0}; // [0..1)
    };

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     * @param data Value passed to the method.
     * @return The requested paint.
     */
    static void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx, const PaintData& data);

private:
    static int roundToInt_(double v) { return static_cast<int>(std::lround(v)); }

    static int clampInt_(int v, int lo, int hi)
    {
        return std::max(lo, std::min(hi, v));
    }

    static SwPointF cubicBezier_(const SwPointF& p0,
                                 const SwPointF& p1,
                                 const SwPointF& p2,
                                 const SwPointF& p3,
                                 double t)
    {
        const double u = 1.0 - t;
        const double tt = t * t;
        const double uu = u * u;
        const double uuu = uu * u;
        const double ttt = tt * t;

        SwPointF p;
        p.x = uuu * p0.x;
        p.x += 3.0 * uu * t * p1.x;
        p.x += 3.0 * u * tt * p2.x;
        p.x += ttt * p3.x;

        p.y = uuu * p0.y;
        p.y += 3.0 * uu * t * p1.y;
        p.y += 3.0 * u * tt * p2.y;
        p.y += ttt * p3.y;
        return p;
    }

    static SwColor clampColorOffset_(const SwColor& c, int dr, int dg, int db)
    {
        auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
        return SwColor{clamp(c.r + dr), clamp(c.g + dg), clamp(c.b + db)};
    }
};

inline void ConnectionPainter::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx, const PaintData& data)
{
    if (!painter) {
        return;
    }

    const double s = std::max(0.0001, ctx.scale);
    if (s < 0.06) {
        return;
    }

    const auto px1 = [&](double v) -> int { return std::max(1, roundToInt_(v * s)); };

    const SwPointF a = ctx.mapFromScene(data.startScene);
    const SwPointF b = ctx.mapFromScene(data.endScene);

    const double dx = std::max(80.0 * s, std::abs(b.x - a.x) * 0.5);
    const SwPointF c1(a.x + dx, a.y);
    const SwPointF c2(b.x - dx, b.y);

    const ConnectionStyle& style = StyleCollection::connectionStyle();

    const SwColor mainBase = data.useCustomColor ? data.customColor : (data.complete ? style.normalColor() : style.constructionColor());
    SwColor main = mainBase;
    if (data.selected) {
        main = data.useCustomColor ? clampColorOffset_(mainBase, 30, 30, 30) : style.selectedColor();
    } else if (data.hovered) {
        main = data.useCustomColor ? clampColorOffset_(mainBase, 30, 30, 30) : style.hoveredColor();
    }

    const SwColor halo = style.selectedHaloColor();
    const SwColor outline = SwColor{15, 23, 42};

    const double baseWidth = data.complete ? static_cast<double>(style.lineWidth()) : static_cast<double>(style.constructionLineWidth());
    const double extra = data.selected ? 2.0 : (data.hovered ? 1.0 : 0.0);
    const int w = px1(baseWidth + extra);
    const int haloW = data.selected ? std::max(w + 2, px1(baseWidth + extra + 4.0)) : 0;

    const double dist = std::hypot(b.x - a.x, b.y - a.y);
    const int segments = clampInt_(static_cast<int>(std::lround(dist / 10.0)), 24, 160);

    auto drawDot = [&](int x, int y, const SwColor& fill, int radiusPx) {
        const int d = radiusPx * 2;
        painter->fillEllipse(SwRect{x - radiusPx, y - radiusPx, d, d}, fill, outline, px1(1.0));
    };

    int lastX = roundToInt_(a.x);
    int lastY = roundToInt_(a.y);

    // Start cap.
    const int pr = std::max(2, px1(style.pointDiameter() * 0.5));
    drawDot(lastX, lastY, main, pr);

    for (int i = 1; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const SwPointF p = cubicBezier_(a, c1, c2, b, t);
        const int x = roundToInt_(p.x);
        const int y = roundToInt_(p.y);
        if (x == lastX && y == lastY) {
            continue;
        }

        if (haloW > 0) {
            painter->drawLine(lastX, lastY, x, y, halo, haloW);
        }
        painter->drawLine(lastX, lastY, x, y, outline, std::max(w + 1, px1(baseWidth + extra + 2.0)));
        painter->drawLine(lastX, lastY, x, y, main, w);

        lastX = x;
        lastY = y;
    }

    // End cap.
    drawDot(lastX, lastY, main, pr);

    if (data.flowActive && data.complete) {
        const double phase = data.flowPhase - std::floor(data.flowPhase);
        const SwPointF p = cubicBezier_(a, c1, c2, b, phase);
        const SwColor glow = clampColorOffset_(mainBase, 120, 120, 120);
        const int rr = std::max(2, px1(style.pointDiameter()));
        drawDot(roundToInt_(p.x), roundToInt_(p.y), glow, rr);
    }
}

} // namespace SwizioNodes
