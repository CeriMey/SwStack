#pragma once
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

#include "graphics/SwBrush.h"
#include "graphics/SwFontMetrics.h"
#include "graphics/SwGraphicsItem.h"
#include "graphics/SwGraphicsRenderContext.h"
#include "graphics/SwPainterPath.h"
#include "graphics/SwPen.h"
#include "graphics/SwPixmap.h"

#include "SwPainter.h"
#include "SwString.h"
#include "SwWidget.h"

#include <algorithm>
#include <cmath>

class SwGraphicsRectItem : public SwGraphicsItem {
public:
    SwGraphicsRectItem() = default;
    explicit SwGraphicsRectItem(const SwRectF& r) : m_rect(r) {}

    void setRect(const SwRectF& r) {
        m_rect = r;
        update();
    }
    SwRectF rect() const { return m_rect; }

    void setPen(const SwPen& p) {
        m_pen = p;
        update();
    }
    SwPen pen() const { return m_pen; }

    void setBrush(const SwBrush& b) {
        m_brush = b;
        update();
    }
    SwBrush brush() const { return m_brush; }

    SwRectF boundingRect() const override { return m_rect.normalized(); }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwRectF m_rect{};
    SwPen m_pen{};
    SwBrush m_brush{};
};

class SwGraphicsEllipseItem : public SwGraphicsItem {
public:
    SwGraphicsEllipseItem() = default;
    explicit SwGraphicsEllipseItem(const SwRectF& r) : m_rect(r) {}

    void setRect(const SwRectF& r) {
        m_rect = r;
        update();
    }
    SwRectF rect() const { return m_rect; }

    void setPen(const SwPen& p) {
        m_pen = p;
        update();
    }
    SwPen pen() const { return m_pen; }

    void setBrush(const SwBrush& b) {
        m_brush = b;
        update();
    }
    SwBrush brush() const { return m_brush; }

    SwRectF boundingRect() const override { return m_rect.normalized(); }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwRectF m_rect{};
    SwPen m_pen{};
    SwBrush m_brush{};
};

class SwGraphicsLineItem : public SwGraphicsItem {
public:
    SwGraphicsLineItem() = default;
    explicit SwGraphicsLineItem(const SwLineF& l) : m_line(l) {}
    SwGraphicsLineItem(double x1, double y1, double x2, double y2) : m_line(x1, y1, x2, y2) {}

    void setLine(const SwLineF& l) {
        m_line = l;
        update();
    }
    SwLineF line() const { return m_line; }

    void setPen(const SwPen& p) {
        m_pen = p;
        update();
    }
    SwPen pen() const { return m_pen; }

    SwRectF boundingRect() const override {
        const double minX = std::min(m_line.p1.x, m_line.p2.x);
        const double minY = std::min(m_line.p1.y, m_line.p2.y);
        const double maxX = std::max(m_line.p1.x, m_line.p2.x);
        const double maxY = std::max(m_line.p1.y, m_line.p2.y);
        const double pad = std::max(1.0, static_cast<double>(std::max(1, m_pen.width())));
        return SwRectF(minX - pad, minY - pad, (maxX - minX) + 2.0 * pad, (maxY - minY) + 2.0 * pad);
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwLineF m_line{};
    SwPen m_pen{};
};

class SwGraphicsTextItem : public SwGraphicsItem {
public:
    SwGraphicsTextItem() = default;
    explicit SwGraphicsTextItem(const SwString& text) : m_text(text) {}

    void setPlainText(const SwString& text) {
        m_text = text;
        update();
    }
    SwString toPlainText() const { return m_text; }

    void setDefaultTextColor(const SwColor& c) {
        m_color = c;
        update();
    }
    SwColor defaultTextColor() const { return m_color; }

    void setFont(const SwFont& f) {
        m_font = f;
        update();
    }
    SwFont font() const { return m_font; }

    SwRectF boundingRect() const override {
        SwFontMetrics fm(m_font);
        const int w = std::max(0, fm.horizontalAdvance(m_text));
        const int h = std::max(0, fm.height());
        return SwRectF(0.0, 0.0, static_cast<double>(w), static_cast<double>(h));
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwString m_text{};
    SwColor m_color{30, 30, 30};
    SwFont m_font{};
};

class SwGraphicsPixmapItem : public SwGraphicsItem {
public:
    SwGraphicsPixmapItem() = default;
    explicit SwGraphicsPixmapItem(const SwPixmap& pix) : m_pixmap(pix) {}

    void setPixmap(const SwPixmap& pix) {
        m_pixmap = pix;
        update();
    }
    SwPixmap pixmap() const { return m_pixmap; }

    SwRectF boundingRect() const override {
        if (m_pixmap.isNull()) {
            return {};
        }
        return SwRectF(0.0, 0.0, static_cast<double>(m_pixmap.width()), static_cast<double>(m_pixmap.height()));
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwPixmap m_pixmap{};
};

class SwGraphicsPathItem : public SwGraphicsItem {
public:
    SwGraphicsPathItem() = default;
    explicit SwGraphicsPathItem(const SwPainterPath& path) : m_path(path) {}

    void setPath(const SwPainterPath& p) {
        m_path = p;
        update();
    }
    SwPainterPath path() const { return m_path; }

    void setPen(const SwPen& p) {
        m_pen = p;
        update();
    }
    SwPen pen() const { return m_pen; }

    void setBrush(const SwBrush& b) {
        m_brush = b;
        update();
    }
    SwBrush brush() const { return m_brush; }

    SwRectF boundingRect() const override { return m_path.boundingRect(); }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwPainterPath m_path{};
    SwPen m_pen{};
    SwBrush m_brush{};
};

class SwGraphicsProxyWidget : public SwGraphicsItem {
public:
    SwGraphicsProxyWidget() = default;
    explicit SwGraphicsProxyWidget(SwWidget* w) : m_widget(w) {
        syncBaseSizeFromWidget_();
    }

    void setWidget(SwWidget* w) {
        m_widget = w;
        syncBaseSizeFromWidget_();
        update();
    }
    SwWidget* widget() const { return m_widget; }

    void setWidgetBaseSize(int w, int h) {
        m_baseWidth = std::max(0, w);
        m_baseHeight = std::max(0, h);
        update();
    }

    SwSize widgetBaseSize() const {
        return SwSize{m_baseWidth, m_baseHeight};
    }

    SwRectF boundingRect() const override {
        if (m_baseWidth > 0 && m_baseHeight > 0) {
            return SwRectF(0.0,
                           0.0,
                           static_cast<double>(m_baseWidth),
                           static_cast<double>(m_baseHeight));
        }
        if (!m_widget) {
            return {};
        }
        return SwRectF(0.0, 0.0, static_cast<double>(m_widget->width()), static_cast<double>(m_widget->height()));
    }

    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        SW_UNUSED(painter)
        SW_UNUSED(ctx)
        // Rendering is handled by the underlying SwWidget (child of the view).
    }

private:
    void syncBaseSizeFromWidget_() {
        if (!m_widget) {
            return;
        }
        const int w = m_widget->width();
        const int h = m_widget->height();
        if (w > 0 && h > 0) {
            m_baseWidth = w;
            m_baseHeight = h;
        }
    }

    SwWidget* m_widget{nullptr};
    int m_baseWidth{0};
    int m_baseHeight{0};
};

// --------------------------------------------------------------------------------------
// Inline paint helpers (implemented here to keep the module header-only).
// --------------------------------------------------------------------------------------

inline int swRoundToInt_(double v) {
    return static_cast<int>(std::lround(v));
}

inline void SwGraphicsRectItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) {
        return;
    }
    SwRectF r = m_rect.normalized();
    const SwPointF sp = scenePos();
    r.translate(sp.x, sp.y);
    const SwRect vr = ctx.mapFromScene(r);

    SwColor fill = SwColor{0, 0, 0};
    bool hasFill = false;
    if (m_brush.style() == SwBrush::SolidPattern) {
        fill = m_brush.color();
        hasFill = true;
    }

    const int bw = std::max(0, m_pen.width());
    const SwColor border = m_pen.color();

    if (hasFill) {
        painter->fillRect(vr, fill, border, bw);
    } else {
        painter->drawRect(vr, border, bw);
    }
}

inline void SwGraphicsEllipseItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) {
        return;
    }
    SwRectF r = m_rect.normalized();
    const SwPointF sp = scenePos();
    r.translate(sp.x, sp.y);
    const SwRect vr = ctx.mapFromScene(r);

    SwColor fill = SwColor{0, 0, 0};
    bool hasFill = false;
    if (m_brush.style() == SwBrush::SolidPattern) {
        fill = m_brush.color();
        hasFill = true;
    }

    const int bw = std::max(0, m_pen.width());
    const SwColor border = m_pen.color();

    if (hasFill) {
        painter->fillEllipse(vr, fill, border, bw);
    } else {
        painter->drawEllipse(vr, border, bw);
    }
}

inline void SwGraphicsLineItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) {
        return;
    }
    const SwPointF sp = scenePos();
    const SwPointF aScene(m_line.p1.x + sp.x, m_line.p1.y + sp.y);
    const SwPointF bScene(m_line.p2.x + sp.x, m_line.p2.y + sp.y);

    const SwPointF aView = ctx.mapFromScene(aScene);
    const SwPointF bView = ctx.mapFromScene(bScene);

    const int w = std::max(1, m_pen.width());
    painter->drawLine(swRoundToInt_(aView.x),
                      swRoundToInt_(aView.y),
                      swRoundToInt_(bView.x),
                      swRoundToInt_(bView.y),
                      m_pen.color(),
                      w);
}

inline void SwGraphicsTextItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) {
        return;
    }
    const SwPointF sp = scenePos();
    const SwPointF pScene(sp.x, sp.y);
    const SwPointF pView = ctx.mapFromScene(pScene);

    SwFontMetrics fm(m_font);
    const int w = std::max(0, fm.horizontalAdvance(m_text));
    const int h = std::max(0, fm.height());
    SwRect r{swRoundToInt_(pView.x), swRoundToInt_(pView.y), w, h};

    painter->drawText(r,
                      m_text,
                      DrawTextFormat::Left | DrawTextFormat::Top,
                      m_color,
                      m_font);
}

inline void SwGraphicsPixmapItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) {
        return;
    }
    if (m_pixmap.isNull()) {
        return;
    }
    const SwPointF sp = scenePos();
    SwRectF r(0.0, 0.0, static_cast<double>(m_pixmap.width()), static_cast<double>(m_pixmap.height()));
    r.translate(sp.x, sp.y);
    const SwRect vr = ctx.mapFromScene(r);
    painter->drawImage(vr, m_pixmap.image());
}

inline void SwGraphicsPathItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) {
        return;
    }
    const std::vector<SwPainterPath::Element>& elems = m_path.elements();
    if (elems.empty()) {
        return;
    }

    const SwPointF sp = scenePos();
    SwPointF start{};
    SwPointF prev{};
    bool hasPrev = false;

    const int w = std::max(1, m_pen.width());
    const SwColor c = m_pen.color();

    for (size_t i = 0; i < elems.size(); ++i) {
        const SwPainterPath::Element& e = elems[i];
        if (e.type == SwPainterPath::MoveToElement) {
            start = SwPointF(e.p.x + sp.x, e.p.y + sp.y);
            prev = start;
            hasPrev = true;
            continue;
        }
        if (e.type == SwPainterPath::LineToElement) {
            if (!hasPrev) {
                prev = SwPointF(e.p.x + sp.x, e.p.y + sp.y);
                start = prev;
                hasPrev = true;
                continue;
            }
            const SwPointF curScene(e.p.x + sp.x, e.p.y + sp.y);
            const SwPointF aView = ctx.mapFromScene(prev);
            const SwPointF bView = ctx.mapFromScene(curScene);
            painter->drawLine(swRoundToInt_(aView.x),
                              swRoundToInt_(aView.y),
                              swRoundToInt_(bView.x),
                              swRoundToInt_(bView.y),
                              c,
                              w);
            prev = curScene;
            continue;
        }
        if (e.type == SwPainterPath::CloseSubpathElement) {
            if (hasPrev) {
                const SwPointF aView = ctx.mapFromScene(prev);
                const SwPointF bView = ctx.mapFromScene(start);
                painter->drawLine(swRoundToInt_(aView.x),
                                  swRoundToInt_(aView.y),
                                  swRoundToInt_(bView.x),
                                  swRoundToInt_(bView.y),
                                  c,
                                  w);
            }
            hasPrev = false;
            continue;
        }
    }
}
