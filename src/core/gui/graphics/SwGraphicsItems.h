#pragma once

/**
 * @file src/core/gui/graphics/SwGraphicsItems.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwGraphicsItems in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the graphics items interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwGraphicsRectItem, SwGraphicsEllipseItem,
 * SwGraphicsLineItem, SwGraphicsTextItem, SwGraphicsSimpleTextItem, SwGraphicsPixmapItem,
 * SwGraphicsPathItem, and SwGraphicsPolygonItem, plus related helper declarations.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Graphics-facing declarations here define the data flow from high-level UI state to lower-level
 * rendering backends.
 *
 */

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

/***************************************************************************************************
 * Concrete SwGraphicsItem subclasses  (Qt-like)
 *
 *   SwGraphicsRectItem        (QGraphicsRectItem)
 *   SwGraphicsEllipseItem     (QGraphicsEllipseItem)
 *   SwGraphicsLineItem        (QGraphicsLineItem)
 *   SwGraphicsTextItem        (QGraphicsTextItem)
 *   SwGraphicsSimpleTextItem  (QGraphicsSimpleTextItem)
 *   SwGraphicsPixmapItem      (QGraphicsPixmapItem)
 *   SwGraphicsPathItem        (QGraphicsPathItem)
 *   SwGraphicsPolygonItem     (QGraphicsPolygonItem)
 *   SwGraphicsProxyWidget     (QGraphicsProxyWidget)
 *   SwGraphicsItemGroup       (QGraphicsItemGroup)
 **************************************************************************************************/

/**
 * @file
 * @brief Declares the stock graphics-item subclasses shipped with the scene module.
 *
 * This header provides ready-to-use scene primitives such as rectangles,
 * ellipses, lines, text blocks, pixmaps, arbitrary paths, polygons, proxy
 * widgets, and grouping items. Each class specializes SwGraphicsItem with the
 * geometry, shape, and paint behavior appropriate for one visual primitive.
 */

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

// ===================================================================
// SwGraphicsRectItem
// ===================================================================
/**
 * @brief Scene item that renders an axis-aligned rectangle.
 */
class SwGraphicsRectItem : public SwGraphicsItem {
public:
    enum { Type = 3 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsRectItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsRectItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsRectItem` instance.
     * @param r Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param r Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsRectItem(const SwRectF& r, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_rect(r) {}
    /**
     * @brief Constructs a `SwGraphicsRectItem` instance.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param w Width value.
     * @param h Height value.
     * @param parent Optional parent object that owns this instance.
     * @param h Height value.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsRectItem(double x, double y, double w, double h, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_rect(x, y, w, h) {}

    /**
     * @brief Sets the rect.
     * @param m_rect Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRect(const SwRectF& r) { m_rect = r; update(); }
    /**
     * @brief Sets the rect.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param w Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRect(double x, double y, double w, double h) { setRect(SwRectF(x, y, w, h)); }
    /**
     * @brief Returns the current rect.
     * @return The current rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF rect() const { return m_rect; }

    /**
     * @brief Sets the pen.
     * @param m_pen Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPen(const SwPen& p) { m_pen = p; update(); }
    /**
     * @brief Returns the current pen.
     * @return The current pen.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPen pen() const { return m_pen; }

    /**
     * @brief Sets the brush.
     * @param m_brush Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBrush(const SwBrush& b) { m_brush = b; update(); }
    /**
     * @brief Returns the current brush.
     * @return The current brush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwBrush brush() const { return m_brush; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        double pw = static_cast<double>(m_pen.width());
        SwRectF r = m_rect.normalized();
        return r.adjusted(-pw * 0.5, -pw * 0.5, pw * 0.5, pw * 0.5);
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addRect(m_rect.normalized());
        return p;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param point Point used by the operation.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwPointF& point) const override {
        return m_rect.normalized().contains(point);
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwRectF m_rect{};
    SwPen m_pen{};
    SwBrush m_brush{};
};

// ===================================================================
// SwGraphicsEllipseItem
// ===================================================================
/**
 * @brief Scene item that renders an ellipse or pie-like arc segment.
 */
class SwGraphicsEllipseItem : public SwGraphicsItem {
public:
    enum { Type = 4 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsEllipseItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsEllipseItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsEllipseItem` instance.
     * @param r Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param r Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsEllipseItem(const SwRectF& r, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_rect(r) {}
    /**
     * @brief Constructs a `SwGraphicsEllipseItem` instance.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param w Width value.
     * @param h Height value.
     * @param parent Optional parent object that owns this instance.
     * @param h Height value.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsEllipseItem(double x, double y, double w, double h, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_rect(x, y, w, h) {}

    /**
     * @brief Sets the rect.
     * @param m_rect Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRect(const SwRectF& r) { m_rect = r; update(); }
    /**
     * @brief Sets the rect.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param w Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRect(double x, double y, double w, double h) { setRect(SwRectF(x, y, w, h)); }
    /**
     * @brief Returns the current rect.
     * @return The current rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF rect() const { return m_rect; }

    /**
     * @brief Sets the start Angle.
     * @param m_startAngle Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStartAngle(int angle) { m_startAngle = angle; update(); }
    /**
     * @brief Returns the current angle.
     * @return The current angle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int startAngle() const { return m_startAngle; }

    /**
     * @brief Sets the span Angle.
     * @param m_spanAngle Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSpanAngle(int angle) { m_spanAngle = angle; update(); }
    /**
     * @brief Returns the current span Angle.
     * @return The current span Angle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int spanAngle() const { return m_spanAngle; }

    /**
     * @brief Sets the pen.
     * @param m_pen Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPen(const SwPen& p) { m_pen = p; update(); }
    /**
     * @brief Returns the current pen.
     * @return The current pen.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPen pen() const { return m_pen; }

    /**
     * @brief Sets the brush.
     * @param m_brush Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBrush(const SwBrush& b) { m_brush = b; update(); }
    /**
     * @brief Returns the current brush.
     * @return The current brush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwBrush brush() const { return m_brush; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        double pw = static_cast<double>(m_pen.width());
        return m_rect.normalized().adjusted(-pw * 0.5, -pw * 0.5, pw * 0.5, pw * 0.5);
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addEllipse(m_rect.normalized());
        return p;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param point Point used by the operation.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwPointF& point) const override {
        SwRectF r = m_rect.normalized();
        if (r.isEmpty()) return false;
        double cx = r.x + r.width * 0.5;
        double cy = r.y + r.height * 0.5;
        double rx = r.width * 0.5;
        double ry = r.height * 0.5;
        if (rx <= 0 || ry <= 0) return false;
        double dx = (point.x - cx) / rx;
        double dy = (point.y - cy) / ry;
        return (dx * dx + dy * dy) <= 1.0;
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwRectF m_rect{};
    SwPen m_pen{};
    SwBrush m_brush{};
    int m_startAngle{0};
    int m_spanAngle{5760}; // 360 * 16 (Qt convention)
};

// ===================================================================
// SwGraphicsLineItem
// ===================================================================
/**
 * @brief Scene item that renders a single stroked line segment.
 */
class SwGraphicsLineItem : public SwGraphicsItem {
public:
    enum { Type = 6 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsLineItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsLineItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsLineItem` instance.
     * @param l Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param l Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsLineItem(const SwLineF& l, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_line(l) {}
    /**
     * @brief Constructs a `SwGraphicsLineItem` instance.
     * @param x1 Value passed to the method.
     * @param y1 Value passed to the method.
     * @param x2 Value passed to the method.
     * @param y2 Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param y2 Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsLineItem(double x1, double y1, double x2, double y2, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_line(x1, y1, x2, y2) {}

    /**
     * @brief Sets the line.
     * @param m_line Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLine(const SwLineF& l) { m_line = l; update(); }
    /**
     * @brief Sets the line.
     * @param x1 Value passed to the method.
     * @param y1 Value passed to the method.
     * @param x2 Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLine(double x1, double y1, double x2, double y2) { setLine(SwLineF(x1, y1, x2, y2)); }
    /**
     * @brief Returns the current line.
     * @return The current line.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwLineF line() const { return m_line; }

    /**
     * @brief Sets the pen.
     * @param m_pen Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPen(const SwPen& p) { m_pen = p; update(); }
    /**
     * @brief Returns the current pen.
     * @return The current pen.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPen pen() const { return m_pen; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        double minX = std::min(m_line.p1.x, m_line.p2.x);
        double minY = std::min(m_line.p1.y, m_line.p2.y);
        double maxX = std::max(m_line.p1.x, m_line.p2.x);
        double maxY = std::max(m_line.p1.y, m_line.p2.y);
        double pad = std::max(1.0, static_cast<double>(std::max(1, m_pen.width())));
        return SwRectF(minX - pad, minY - pad, (maxX - minX) + 2.0 * pad, (maxY - minY) + 2.0 * pad);
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.moveTo(m_line.p1);
        p.lineTo(m_line.p2);
        return p;
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwLineF m_line{};
    SwPen m_pen{};
};

// ===================================================================
// SwGraphicsTextItem
// ===================================================================
/**
 * @brief Scene item that renders selectable or editable text content.
 */
class SwGraphicsTextItem : public SwGraphicsItem {
public:
    enum { Type = 8 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    enum TextInteractionFlag {
        NoTextInteraction         = 0,
        TextSelectableByMouse     = 1,
        TextSelectableByKeyboard  = 2,
        TextEditable              = 4,
        TextEditorInteraction     = TextSelectableByMouse | TextSelectableByKeyboard | TextEditable,
        TextBrowserInteraction    = TextSelectableByMouse
    };

    /**
     * @brief Constructs a `SwGraphicsTextItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsTextItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsTextItem` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param text Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsTextItem(const SwString& text, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_text(text) {}

    /**
     * @brief Sets the plain Text.
     * @param m_text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPlainText(const SwString& text) { m_text = text; update(); }
    /**
     * @brief Returns the current to Plain Text.
     * @return The current to Plain Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toPlainText() const { return m_text; }

    /**
     * @brief Sets the html.
     * @param m_html Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHtml(const SwString& html) { m_html = html; update(); }
    /**
     * @brief Returns the current to Html.
     * @return The current to Html.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toHtml() const { return m_html; }

    /**
     * @brief Sets the default Text Color.
     * @param m_color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDefaultTextColor(const SwColor& c) { m_color = c; update(); }
    /**
     * @brief Returns the current default Text Color.
     * @return The current default Text Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor defaultTextColor() const { return m_color; }

    /**
     * @brief Sets the font.
     * @param m_font Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFont(const SwFont& f) { m_font = f; update(); }
    /**
     * @brief Returns the current font.
     * @return The current font.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwFont font() const { return m_font; }

    /**
     * @brief Sets the text Width.
     * @param m_textWidth Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTextWidth(double width) { m_textWidth = width; update(); }
    /**
     * @brief Returns the current text Width.
     * @return The current text Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double textWidth() const { return m_textWidth; }

    /**
     * @brief Sets the text Interaction Flags.
     * @param flags Flags that refine the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTextInteractionFlags(int flags) { m_textInteractionFlags = flags; }
    /**
     * @brief Returns the current text Interaction Flags.
     * @return The current text Interaction Flags.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int textInteractionFlags() const { return m_textInteractionFlags; }

    /**
     * @brief Sets the open External Links.
     * @param open Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOpenExternalLinks(bool open) { m_openExternalLinks = open; }
    /**
     * @brief Returns the current external Links.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool openExternalLinks() const { return m_openExternalLinks; }

    /**
     * @brief Sets the tab Changes Focus.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTabChangesFocus(bool on) { m_tabChangesFocus = on; }
    /**
     * @brief Returns the current tab Changes Focus.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool tabChangesFocus() const { return m_tabChangesFocus; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        SwFontMetrics fm(m_font);
        int w = std::max(0, fm.horizontalAdvance(m_text));
        int h = std::max(0, fm.height());
        if (m_textWidth > 0) w = static_cast<int>(m_textWidth);
        return SwRectF(0, 0, static_cast<double>(w), static_cast<double>(h));
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addRect(boundingRect());
        return p;
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwString m_text{};
    SwString m_html{};
    SwColor m_color{30, 30, 30};
    SwFont m_font{};
    double m_textWidth{-1.0};
    int m_textInteractionFlags{0};
    bool m_openExternalLinks{false};
    bool m_tabChangesFocus{false};
};

// ===================================================================
// SwGraphicsSimpleTextItem
// ===================================================================
class SwGraphicsSimpleTextItem : public SwGraphicsItem {
public:
    enum { Type = 9 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsSimpleTextItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsSimpleTextItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsSimpleTextItem` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param text Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsSimpleTextItem(const SwString& text, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_text(text) {}

    /**
     * @brief Sets the text.
     * @param m_text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setText(const SwString& text) { m_text = text; update(); }
    /**
     * @brief Returns the current text.
     * @return The current text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString text() const { return m_text; }

    /**
     * @brief Sets the font.
     * @param m_font Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFont(const SwFont& f) { m_font = f; update(); }
    /**
     * @brief Returns the current font.
     * @return The current font.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwFont font() const { return m_font; }

    /**
     * @brief Sets the pen.
     * @param m_pen Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPen(const SwPen& p) { m_pen = p; update(); }
    /**
     * @brief Returns the current pen.
     * @return The current pen.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPen pen() const { return m_pen; }

    /**
     * @brief Sets the brush.
     * @param m_brush Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBrush(const SwBrush& b) { m_brush = b; update(); }
    /**
     * @brief Returns the current brush.
     * @return The current brush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwBrush brush() const { return m_brush; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        SwFontMetrics fm(m_font);
        int w = std::max(0, fm.horizontalAdvance(m_text));
        int h = std::max(0, fm.height());
        return SwRectF(0, 0, static_cast<double>(w), static_cast<double>(h));
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addRect(boundingRect());
        return p;
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwString m_text{};
    SwFont m_font{};
    SwPen m_pen{};
    SwBrush m_brush{};
};

// ===================================================================
// SwGraphicsPixmapItem
// ===================================================================
class SwGraphicsPixmapItem : public SwGraphicsItem {
public:
    enum { Type = 7 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    enum ShapeMode { MaskShape, BoundingRectShape, HeuristicMaskShape };

    /**
     * @brief Constructs a `SwGraphicsPixmapItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsPixmapItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsPixmapItem` instance.
     * @param pix Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param pix Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsPixmapItem(const SwPixmap& pix, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_pixmap(pix) {}

    /**
     * @brief Sets the pixmap.
     * @param m_pixmap Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPixmap(const SwPixmap& pix) { m_pixmap = pix; update(); }
    /**
     * @brief Returns the current pixmap.
     * @return The current pixmap.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPixmap pixmap() const { return m_pixmap; }

    /**
     * @brief Sets the offset.
     * @param m_offset Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOffset(const SwPointF& offset) { m_offset = offset; update(); }
    /**
     * @brief Sets the offset.
     * @param x Horizontal coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOffset(double x, double y) { setOffset({x, y}); }
    /**
     * @brief Returns the current offset.
     * @return The current offset.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF offset() const { return m_offset; }

    /**
     * @brief Sets the shape Mode.
     * @param mode Mode value that controls the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setShapeMode(ShapeMode mode) { m_shapeMode = mode; }
    /**
     * @brief Returns the current shape Mode.
     * @return The current shape Mode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ShapeMode shapeMode() const { return m_shapeMode; }

    /**
     * @brief Sets the transformation Mode.
     * @param mode Mode value that controls the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTransformationMode(int mode) { m_transformationMode = mode; }
    /**
     * @brief Returns the current transformation Mode.
     * @return The current transformation Mode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int transformationMode() const { return m_transformationMode; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        if (m_pixmap.isNull()) return {};
        return SwRectF(m_offset.x, m_offset.y,
                       static_cast<double>(m_pixmap.width()),
                       static_cast<double>(m_pixmap.height()));
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addRect(boundingRect());
        return p;
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwPixmap m_pixmap{};
    SwPointF m_offset{};
    ShapeMode m_shapeMode{MaskShape};
    int m_transformationMode{0}; // 0 = FastTransformation, 1 = SmoothTransformation
};

// ===================================================================
// SwGraphicsPathItem
// ===================================================================
class SwGraphicsPathItem : public SwGraphicsItem {
public:
    enum { Type = 2 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsPathItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsPathItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsPathItem` instance.
     * @param path Path used by the operation.
     * @param parent Optional parent object that owns this instance.
     * @param path Path used by the operation.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsPathItem(const SwPainterPath& path, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_path(path) {}

    /**
     * @brief Sets the path.
     * @param m_path Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPath(const SwPainterPath& p) { m_path = p; update(); }
    /**
     * @brief Returns the current path.
     * @return The current path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath path() const { return m_path; }

    /**
     * @brief Sets the pen.
     * @param m_pen Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPen(const SwPen& p) { m_pen = p; update(); }
    /**
     * @brief Returns the current pen.
     * @return The current pen.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPen pen() const { return m_pen; }

    /**
     * @brief Sets the brush.
     * @param m_brush Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBrush(const SwBrush& b) { m_brush = b; update(); }
    /**
     * @brief Returns the current brush.
     * @return The current brush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwBrush brush() const { return m_brush; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        double pw = static_cast<double>(m_pen.width());
        SwRectF r = m_path.boundingRect();
        return r.adjusted(-pw * 0.5, -pw * 0.5, pw * 0.5, pw * 0.5);
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override { return m_path; }

    /**
     * @brief Performs the `contains` operation.
     * @param point Point used by the operation.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwPointF& point) const override {
        return m_path.contains(point);
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwPainterPath m_path{};
    SwPen m_pen{};
    SwBrush m_brush{};
};

// ===================================================================
// SwGraphicsPolygonItem
// ===================================================================
class SwGraphicsPolygonItem : public SwGraphicsItem {
public:
    enum { Type = 5 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsPolygonItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsPolygonItem(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsPolygonItem` instance.
     * @param polygon Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param polygon Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsPolygonItem(const SwPolygonF& polygon, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_polygon(polygon) {}

    /**
     * @brief Sets the polygon.
     * @param m_polygon Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPolygon(const SwPolygonF& polygon) { m_polygon = polygon; update(); }
    /**
     * @brief Returns the current polygon.
     * @return The current polygon.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPolygonF polygon() const { return m_polygon; }

    /**
     * @brief Sets the pen.
     * @param m_pen Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPen(const SwPen& p) { m_pen = p; update(); }
    /**
     * @brief Returns the current pen.
     * @return The current pen.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPen pen() const { return m_pen; }

    /**
     * @brief Sets the brush.
     * @param m_brush Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBrush(const SwBrush& b) { m_brush = b; update(); }
    /**
     * @brief Returns the current brush.
     * @return The current brush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwBrush brush() const { return m_brush; }

    /**
     * @brief Sets the fill Rule.
     * @param rule Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFillRule(SwPainterPath::FillRule rule) { m_fillRule = rule; }
    /**
     * @brief Returns the current fill Rule.
     * @return The current fill Rule.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath::FillRule fillRule() const { return m_fillRule; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        double pw = static_cast<double>(m_pen.width());
        SwRectF r = m_polygon.boundingRect();
        return r.adjusted(-pw * 0.5, -pw * 0.5, pw * 0.5, pw * 0.5);
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addPolygon(m_polygon);
        p.closeSubpath();
        return p;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param point Point used by the operation.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwPointF& point) const override {
        return m_polygon.containsPoint(point);
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override;

private:
    SwPolygonF m_polygon{};
    SwPen m_pen{};
    SwBrush m_brush{};
    SwPainterPath::FillRule m_fillRule{SwPainterPath::OddEvenFill};
};

// ===================================================================
// SwGraphicsProxyWidget
// ===================================================================
class SwGraphicsProxyWidget : public SwGraphicsItem {
public:
    enum { Type = 12 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsProxyWidget` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsProxyWidget(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}
    /**
     * @brief Constructs a `SwGraphicsProxyWidget` instance.
     * @param w Width value.
     * @param parent Optional parent object that owns this instance.
     * @param w Width value.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwGraphicsProxyWidget(SwWidget* w, SwGraphicsItem* parent = nullptr)
        : SwGraphicsItem(parent), m_widget(w) {
        syncBaseSizeFromWidget_();
    }

    /**
     * @brief Sets the widget.
     * @param m_widget Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWidget(SwWidget* w) { m_widget = w; syncBaseSizeFromWidget_(); update(); }
    /**
     * @brief Returns the current widget.
     * @return The current widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* widget() const { return m_widget; }

    /**
     * @brief Creates the requested proxy For Child Widget.
     * @return The resulting proxy For Child Widget.
     */
    SwGraphicsProxyWidget* createProxyForChildWidget(SwWidget* /*child*/) {
        return nullptr; // stub — Qt creates sub-proxies for embedded child widgets
    }

    /**
     * @brief Sets the widget Base Size.
     * @param w Width value.
     * @param h Height value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWidgetBaseSize(int w, int h) {
        m_baseWidth = std::max(0, w);
        m_baseHeight = std::max(0, h);
        update();
    }

    /**
     * @brief Returns the current widget Base Size.
     * @return The current widget Base Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSize widgetBaseSize() const { return SwSize{m_baseWidth, m_baseHeight}; }

    /**
     * @brief Performs the `subWidgetRect` operation.
     * @return The requested sub Widget Rect.
     */
    SwRectF subWidgetRect(const SwWidget* /*widget*/) const { return boundingRect(); }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        if (m_baseWidth > 0 && m_baseHeight > 0)
            return SwRectF(0, 0, static_cast<double>(m_baseWidth), static_cast<double>(m_baseHeight));
        if (!m_widget) return {};
        return SwRectF(0, 0, static_cast<double>(m_widget->width()), static_cast<double>(m_widget->height()));
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addRect(boundingRect());
        return p;
    }

    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     */
    void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) override {
        SW_UNUSED(painter)
        SW_UNUSED(ctx)
    }

private:
    void syncBaseSizeFromWidget_() {
        if (!m_widget) return;
        int w = m_widget->width();
        int h = m_widget->height();
        if (w > 0 && h > 0) { m_baseWidth = w; m_baseHeight = h; }
    }

    SwWidget* m_widget{nullptr};
    int m_baseWidth{0};
    int m_baseHeight{0};
};

// ===================================================================
// SwGraphicsItemGroup
// ===================================================================
class SwGraphicsItemGroup : public SwGraphicsItem {
public:
    enum { Type = 10 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int type() const override { return Type; }

    /**
     * @brief Constructs a `SwGraphicsItemGroup` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsItemGroup(SwGraphicsItem* parent = nullptr) : SwGraphicsItem(parent) {}

    /**
     * @brief Adds the specified to Group.
     * @param item Item affected by the operation.
     */
    void addToGroup(SwGraphicsItem* item) {
        if (!item || item == this) return;
        if (item->group() == this) return;
        item->setGroup(nullptr);
        item->setParentItem(this);
        item->m_group = this;
        m_groupItems.push_back(item);
        update();
    }

    /**
     * @brief Removes the specified from Group.
     * @param item Item affected by the operation.
     */
    void removeFromGroup(SwGraphicsItem* item) {
        if (!item || item->group() != this) return;
        item->m_group = nullptr;
        m_groupItems.erase(std::remove(m_groupItems.begin(), m_groupItems.end(), item), m_groupItems.end());
        item->setParentItem(parentItem());
        update();
    }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const override {
        SwRectF r;
        for (const auto* child : childItems()) {
            SwRectF cr = child->mapRectToParent(child->boundingRect());
            r = r.isEmpty() ? cr : r.united(cr);
        }
        return r;
    }

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath shape() const override {
        SwPainterPath p;
        p.addRect(boundingRect());
        return p;
    }

    /**
     * @brief Performs the `paint` operation.
     */
    void paint(SwPainter* /*painter*/, const SwGraphicsRenderContext& /*ctx*/) override {
        // Groups don't paint themselves
    }

private:
    std::vector<SwGraphicsItem*> m_groupItems;
};

// ===================================================================
// SwGraphicsItem deferred implementations  (need full class defs above)
// ===================================================================
inline SwPainterPath SwGraphicsItem::shape() const {
    SwPainterPath p;
    p.addRect(boundingRect());
    return p;
}

inline void SwGraphicsItem::setGroup(SwGraphicsItemGroup* grp) {
    if (m_group == grp) return;
    if (m_group) m_group->removeFromGroup(this);
    if (grp) grp->addToGroup(this);
}

// ===================================================================
// Paint helpers  (inline, keep header-only)
// ===================================================================
inline int swRoundToInt_(double v) {
    return static_cast<int>(std::lround(v));
}

inline void SwGraphicsRectItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) return;
    const SwRectF sceneRect = mapRectToScene(m_rect.normalized());
    const SwRect vr = ctx.mapFromScene(sceneRect);

    SwColor fill{0, 0, 0};
    bool hasFill = false;
    if (m_brush.style() == SwBrush::SolidPattern) { fill = m_brush.color(); hasFill = true; }

    const int bw = std::max(0, m_pen.width());
    const SwColor border = m_pen.color();

    if (hasFill) painter->fillRect(vr, fill, border, bw);
    else painter->drawRect(vr, border, bw);
}

inline void SwGraphicsEllipseItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) return;
    const SwRectF sceneRect = mapRectToScene(m_rect.normalized());
    const SwRect vr = ctx.mapFromScene(sceneRect);

    SwColor fill{0, 0, 0};
    bool hasFill = false;
    if (m_brush.style() == SwBrush::SolidPattern) { fill = m_brush.color(); hasFill = true; }

    const int bw = std::max(0, m_pen.width());
    const SwColor border = m_pen.color();

    if (hasFill) painter->fillEllipse(vr, fill, border, bw);
    else painter->drawEllipse(vr, border, bw);
}

inline void SwGraphicsLineItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) return;
    const SwPointF aScene = mapToScene(m_line.p1);
    const SwPointF bScene = mapToScene(m_line.p2);

    const SwPointF aView = ctx.mapFromScene(aScene);
    const SwPointF bView = ctx.mapFromScene(bScene);

    const int w = std::max(1, m_pen.width());
    painter->drawLine(swRoundToInt_(aView.x), swRoundToInt_(aView.y),
                      swRoundToInt_(bView.x), swRoundToInt_(bView.y),
                      m_pen.color(), w);
}

inline void SwGraphicsTextItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) return;
    const SwPointF pView = ctx.mapFromScene(mapToScene(SwPointF(0.0, 0.0)));

    SwFontMetrics fm(m_font);
    int w = std::max(0, fm.horizontalAdvance(m_text));
    int h = std::max(0, fm.height());
    SwRect r{swRoundToInt_(pView.x), swRoundToInt_(pView.y), w, h};

    painter->drawText(r, m_text, DrawTextFormat::Left | DrawTextFormat::Top, m_color, m_font);
}

inline void SwGraphicsSimpleTextItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) return;
    const SwPointF pView = ctx.mapFromScene(mapToScene(SwPointF(0.0, 0.0)));

    SwFontMetrics fm(m_font);
    int w = std::max(0, fm.horizontalAdvance(m_text));
    int h = std::max(0, fm.height());
    SwRect r{swRoundToInt_(pView.x), swRoundToInt_(pView.y), w, h};

    SwColor c = m_pen.width() > 0 ? m_pen.color() : SwColor{30, 30, 30};
    painter->drawText(r, m_text, DrawTextFormat::Left | DrawTextFormat::Top, c, m_font);
}

inline void SwGraphicsPixmapItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible() || m_pixmap.isNull()) return;
    SwRectF r(m_offset.x, m_offset.y,
              static_cast<double>(m_pixmap.width()),
              static_cast<double>(m_pixmap.height()));
    const SwRect vr = ctx.mapFromScene(mapRectToScene(r));
    painter->drawImage(vr, m_pixmap.image());
}

inline void SwGraphicsPathItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible()) return;
    const std::vector<SwPainterPath::Element>& elems = m_path.elements();
    if (elems.empty()) return;

    SwPointF start{};
    SwPointF prev{};
    bool hasPrev = false;

    const int w = std::max(1, m_pen.width());
    const SwColor c = m_pen.color();

    for (size_t i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        if (e.type == SwPainterPath::MoveToElement) {
            start = mapToScene(e.p);
            prev = start;
            hasPrev = true;
        } else if (e.type == SwPainterPath::LineToElement) {
            if (!hasPrev) { prev = mapToScene(e.p); start = prev; hasPrev = true; continue; }
            SwPointF curScene = mapToScene(e.p);
            SwPointF aView = ctx.mapFromScene(prev);
            SwPointF bView = ctx.mapFromScene(curScene);
            painter->drawLine(swRoundToInt_(aView.x), swRoundToInt_(aView.y),
                              swRoundToInt_(bView.x), swRoundToInt_(bView.y), c, w);
            prev = curScene;
        } else if (e.type == SwPainterPath::CurveToElement && i + 2 < elems.size()) {
            // Flatten cubic to line segments for rendering
            if (!hasPrev) { prev = mapToScene(SwPointF(0.0, 0.0)); start = prev; hasPrev = true; }
            SwPointF c1 = mapToScene(elems[i].p);
            SwPointF c2 = mapToScene(elems[i+1].p);
            SwPointF ep = mapToScene(elems[i+2].p);
            SwPolygonF poly;
            SwTransform identity;
            SwPainterPath::flattenCubic_(prev, c1, c2, ep, poly, identity, true);
            for (int j = 1; j < poly.size(); ++j) {
                SwPointF aView = ctx.mapFromScene(poly.at(j-1));
                SwPointF bView = ctx.mapFromScene(poly.at(j));
                painter->drawLine(swRoundToInt_(aView.x), swRoundToInt_(aView.y),
                                  swRoundToInt_(bView.x), swRoundToInt_(bView.y), c, w);
            }
            prev = ep;
            i += 2;
        } else if (e.type == SwPainterPath::CloseSubpathElement) {
            if (hasPrev) {
                SwPointF aView = ctx.mapFromScene(prev);
                SwPointF bView = ctx.mapFromScene(start);
                painter->drawLine(swRoundToInt_(aView.x), swRoundToInt_(aView.y),
                                  swRoundToInt_(bView.x), swRoundToInt_(bView.y), c, w);
            }
            hasPrev = false;
        }
    }
}

inline void SwGraphicsPolygonItem::paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) {
    if (!painter || !isVisible() || m_polygon.isEmpty()) return;

    // Build screen-space points array
    std::vector<SwPoint> viewPoints;
    viewPoints.reserve(static_cast<size_t>(m_polygon.size()));
    for (int i = 0; i < m_polygon.size(); ++i) {
        SwPointF vp = ctx.mapFromScene(mapToScene(m_polygon.at(i)));
        viewPoints.push_back({swRoundToInt_(vp.x), swRoundToInt_(vp.y)});
    }

    // Fill if brush set
    if (m_brush.style() == SwBrush::SolidPattern && viewPoints.size() >= 3) {
        painter->fillPolygon(viewPoints.data(), static_cast<int>(viewPoints.size()),
                             m_brush.color(), m_pen.color(), std::max(0, m_pen.width()));
    } else {
        // Stroke only
        const int w = std::max(1, m_pen.width());
        const SwColor c = m_pen.color();
        for (size_t i = 0; i + 1 < viewPoints.size(); ++i) {
            painter->drawLine(viewPoints[i].x, viewPoints[i].y,
                              viewPoints[i+1].x, viewPoints[i+1].y, c, w);
        }
        if (viewPoints.size() > 2) {
            painter->drawLine(viewPoints.back().x, viewPoints.back().y,
                              viewPoints.front().x, viewPoints.front().y, c, w);
        }
    }
}
