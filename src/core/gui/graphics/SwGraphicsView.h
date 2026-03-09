#pragma once

/**
 * @file src/core/gui/graphics/SwGraphicsView.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwGraphicsView in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the graphics view interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwGraphicsView.
 *
 * View-oriented declarations here mainly describe how underlying state is projected into a visual
 * or interactive surface, including how refresh, selection, or presentation concerns are exposed
 * at the API boundary.
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
 * SwGraphicsView  â€”  Qt-like QGraphicsView with full API coverage.
 *
 * - Scene association & viewport
 * - Drag modes (NoDrag, ScrollHandDrag, RubberBandDrag)
 * - Render hints
 * - fitInView, centerOn, ensureVisible
 * - Full transform (rotate, scale, translate, shear, resetTransform)
 * - Transformation / resize anchors
 * - Interactive mode & event forwarding to scene
 * - Alignment
 * - Viewport update modes, optimization flags
 * - Rubber band selection
 **************************************************************************************************/

/**
 * @file
 * @brief Declares the widget that visualizes and interacts with a graphics scene.
 *
 * SwGraphicsView bridges retained-mode scene content with the regular widget
 * hierarchy. It manages viewport state, scene attachment, coordinate mapping,
 * scrolling, zooming, and event forwarding so scene items can be rendered and
 * manipulated inside a standard UI surface.
 */

#include "SwWidget.h"

#include "graphics/SwGraphicsScene.h"
#include "graphics/SwGraphicsRenderContext.h"
#include "graphics/SwGraphicsTypes.h"

#include <algorithm>
#include <cmath>
#include <vector>

/**
 * @brief Widget facade for rendering a SwGraphicsScene.
 */
class SwGraphicsView : public SwWidget {
    SW_OBJECT(SwGraphicsView, SwWidget)

public:
    // --- DragMode (matches QGraphicsView::DragMode) ---
    enum DragMode {
        NoDrag,
        ScrollHandDrag,
        RubberBandDrag
    };

    // --- ViewportAnchor ---
    enum ViewportAnchor {
        NoAnchor,
        AnchorViewCenter,
        AnchorUnderMouse
    };

    // --- ViewportUpdateMode ---
    enum ViewportUpdateMode {
        FullViewportUpdate,
        MinimalViewportUpdate,
        SmartViewportUpdate,
        NoViewportUpdate,
        BoundingRectViewportUpdate
    };

    // --- OptimizationFlag ---
    enum OptimizationFlag {
        DontClipPainter            = 0x1,
        DontSavePainterState       = 0x2,
        DontAdjustForAntialiasing  = 0x4,
        IndirectPainting           = 0x8
    };

    // --- RenderHint ---
    enum RenderHint {
        Antialiasing               = 0x1,
        TextAntialiasing           = 0x2,
        SmoothPixmapTransform      = 0x4,
        HighQualityAntialiasing    = 0x8
    };

    // --- CacheModeFlag ---
    enum CacheModeFlag {
        CacheNone       = 0x0,
        CacheBackground = 0x1
    };

    // --- Alignment ---
    enum Alignment {
        AlignLeft    = 0x01,
        AlignRight   = 0x02,
        AlignHCenter = 0x04,
        AlignTop     = 0x20,
        AlignBottom  = 0x40,
        AlignVCenter = 0x80,
        AlignCenter  = AlignHCenter | AlignVCenter
    };

    // ===================================================================
    // Construction
    // ===================================================================
    /// Constructs a graphics view with no attached scene.
    explicit SwGraphicsView(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet(R"(
            SwGraphicsView {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 14px;
            }
        )");
    }

    /// Constructs a graphics view already attached to the supplied scene.
    explicit SwGraphicsView(SwGraphicsScene* scene, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet(R"(
            SwGraphicsView {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 14px;
            }
        )");
        setScene(scene);
    }

    /// Detaches the view from its scene registration list before destruction.
    ~SwGraphicsView() override {
        if (m_scene) m_scene->removeView_(this);
    }

    // ===================================================================
    // Scene
    // ===================================================================
    /**
     * @brief Attaches the view to a scene.
     * @param scene New scene to display, or `nullptr` to detach the current one.
     *
     * The view updates signal connections, scene registration, proxy-widget
     * synchronization, and schedules a repaint.
     */
    void setScene(SwGraphicsScene* scene) {
        if (m_scene == scene) return;
        if (m_scene) {
            SwObject::disconnect(m_scene, this);
            m_scene->removeView_(this);
        }
        m_scene = scene;
        if (m_scene) {
            SwObject::connect(m_scene, &SwGraphicsScene::changed, this, &SwGraphicsView::onSceneChanged_);
            m_scene->addView_(this);
        }
        syncProxyWidgets_();
        update();
    }

    /// Returns the scene currently displayed by the view, or `nullptr`.
    SwGraphicsScene* scene() const { return m_scene; }

    // ===================================================================
    // Scene rect (override what part of the scene is visible)
    // ===================================================================
    /// Returns the explicit scene rectangle override when set, otherwise the attached scene rectangle.
    SwRectF sceneRect() const {
        if (!m_sceneRectOverride.isEmpty()) return m_sceneRectOverride;
        if (m_scene) return m_scene->sceneRect();
        return {};
    }

    /// Sets the scene rectangle override used by the view.
    void setSceneRect(const SwRectF& rect) { m_sceneRectOverride = rect; update(); }
    /// Convenience overload that builds the scene rectangle override from coordinates and size.
    void setSceneRect(double x, double y, double w, double h) { setSceneRect(SwRectF(x, y, w, h)); }

    // ===================================================================
    // Transform
    // ===================================================================
    /// Sets the scalar zoom factor used by the view.
    void setScale(double s) { m_scale = (s <= 0.0) ? 1.0 : s; syncProxyWidgets_(); update(); }
    /// Returns the scalar zoom factor currently applied by the view.
    double scale() const { return m_scale; }

    /// Returns the matrix-based view transform.
    SwTransform transform() const { return m_viewTransform; }

    /**
     * @brief Sets or combines the matrix-based view transform.
     * @param transform Transform to install or combine.
     * @param combine When `true`, multiplies the new transform with the current one.
     */
    void setTransform(const SwTransform& transform, bool combine = false) {
        m_viewTransform = combine ? (m_viewTransform * transform) : transform;
        syncProxyWidgets_(); update();
    }

    /// Resets the matrix transform and scalar zoom back to the identity state.
    void resetTransform() { m_viewTransform.reset(); m_scale = 1.0; syncProxyWidgets_(); update(); }

    /// Returns the effective viewport transform after combining matrix transform and scalar zoom.
    SwTransform viewportTransform() const {
        const SwPointF origin = mapFromScene(SwPointF(0.0, 0.0));
        const SwPointF ex = mapFromScene(SwPointF(1.0, 0.0));
        const SwPointF ey = mapFromScene(SwPointF(0.0, 1.0));
        return SwTransform(ex.x - origin.x, ex.y - origin.y, 0.0,
                           ey.x - origin.x, ey.y - origin.y, 0.0,
                           origin.x, origin.y, 1.0);
    }
    /// Returns whether any non-identity transform is currently applied.
    bool isTransformed() const { return !m_viewTransform.isIdentity() || m_scale != 1.0; }

    /// Applies an additional rotation to the view transform.
    void rotate(double angle) { m_viewTransform.rotate(angle); syncProxyWidgets_(); update(); }
    /// Applies an additional shear to the view transform.
    void shear(double sh, double sv) { m_viewTransform.shear(sh, sv); syncProxyWidgets_(); update(); }
    /// Applies an additional translation to the view transform.
    void translate(double dx, double dy) { m_viewTransform.translate(dx, dy); syncProxyWidgets_(); update(); }

    // ===================================================================
    // Coordinate mapping  (scene <-> view widget coordinates)
    // ===================================================================
    /// Maps a scene-space point into viewport-local widget coordinates.
    SwPointF mapFromScene(const SwPointF& scenePoint) const {
        SwPointF p = m_viewTransform.isIdentity() ? scenePoint : m_viewTransform.map(scenePoint);
        double x = (p.x - m_scroll.x) * m_scale;
        double y = (p.y - m_scroll.y) * m_scale;
        return SwPointF(x, y);
    }

    /// Convenience overload that maps scalar scene coordinates into viewport-local coordinates.
    SwPointF mapFromScene(double x, double y) const { return mapFromScene(SwPointF(x, y)); }

    /// Maps a scene-space rectangle into viewport-local widget coordinates.
    SwPolygonF mapFromScene(const SwRectF& sceneRect) const {
        SwPolygonF r;
        r.append(mapFromScene(SwPointF(sceneRect.x, sceneRect.y)));
        r.append(mapFromScene(SwPointF(sceneRect.x + sceneRect.width, sceneRect.y)));
        r.append(mapFromScene(SwPointF(sceneRect.x + sceneRect.width,
                                       sceneRect.y + sceneRect.height)));
        r.append(mapFromScene(SwPointF(sceneRect.x, sceneRect.y + sceneRect.height)));
        return r;
    }

    /// Maps a scene-space polygon into viewport-local widget coordinates.
    SwPolygonF mapFromScene(const SwPolygonF& polygon) const {
        SwPolygonF r;
        for (int i = 0; i < polygon.size(); ++i) r.append(mapFromScene(polygon.at(i)));
        return r;
    }

    /// Maps a viewport-local widget-space point into scene coordinates.
    SwPointF mapToScene(const SwPointF& viewPoint) const {
        double sx = (viewPoint.x / m_scale) + m_scroll.x;
        double sy = (viewPoint.y / m_scale) + m_scroll.y;
        SwPointF p(sx, sy);
        if (!m_viewTransform.isIdentity()) p = m_viewTransform.inverted().map(p);
        return p;
    }

    /// Convenience overload that maps scalar widget coordinates into scene space.
    SwPointF mapToScene(int x, int y) const { return mapToScene(SwPointF(static_cast<double>(x), static_cast<double>(y))); }

    /// Maps a widget-space rectangle into scene coordinates.
    SwPolygonF mapToScene(const SwRect& viewRect) const {
        SwPolygonF r;
        r.append(mapToScene(SwPointF(viewRect.x, viewRect.y)));
        r.append(mapToScene(SwPointF(viewRect.x + viewRect.width, viewRect.y)));
        r.append(mapToScene(SwPointF(viewRect.x + viewRect.width,
                                     viewRect.y + viewRect.height)));
        r.append(mapToScene(SwPointF(viewRect.x, viewRect.y + viewRect.height)));
        return r;
    }

    /// Maps a widget-space polygon into scene coordinates.
    SwPolygonF mapToScene(const SwPolygonF& polygon) const {
        SwPolygonF r;
        for (int i = 0; i < polygon.size(); ++i) r.append(mapToScene(polygon.at(i)));
        return r;
    }

    /// Returns the bounding viewport rectangle that covers a mapped scene rectangle.
    SwRect mapRectFromScene(const SwRectF& sceneRect) const {
        const SwRectF bounds = mapFromScene(sceneRect).boundingRect();
        int x = static_cast<int>(std::lround(bounds.x));
        int y = static_cast<int>(std::lround(bounds.y));
        int w = static_cast<int>(std::lround(bounds.width));
        int h = static_cast<int>(std::lround(bounds.height));
        return SwRect{x, y, w, h};
    }

    /// Returns the bounding scene rectangle that covers a mapped viewport rectangle.
    SwRectF mapRectToScene(const SwRect& viewRect) const {
        return mapToScene(viewRect).boundingRect();
    }

    // ===================================================================
    // Scroll (pan)
    // ===================================================================
    /// Sets the scroll offset expressed in scene coordinates.
    void setScroll(double sx, double sy) { m_scroll = {sx, sy}; syncProxyWidgets_(); update(); }
    /// Returns the current scroll offset expressed in scene coordinates.
    SwPointF scrollOffset() const { return m_scroll; }

    /// Scrolls the view contents by a device-space delta converted back into scene space.
    void scrollContentsBy(int dx, int dy) {
        m_scroll.x -= static_cast<double>(dx) / m_scale;
        m_scroll.y -= static_cast<double>(dy) / m_scale;
        syncProxyWidgets_(); update();
    }

    // ===================================================================
    // fitInView / centerOn / ensureVisible
    // ===================================================================
    /**
     * @brief Adjusts the view so the supplied scene rectangle fits inside the viewport.
     * @param rect Scene rectangle to fit.
     * @param aspectRatioMode Reserved aspect ratio policy parameter.
     */
    void fitInView(const SwRectF& rect, int /*aspectRatioMode*/ = 0 /*IgnoreAspectRatio*/) {
        if (rect.isEmpty()) return;
        const double vw = static_cast<double>(width());
        const double vh = static_cast<double>(height());
        if (vw <= 0 || vh <= 0) return;
        double sx = vw / rect.width;
        double sy = vh / rect.height;
        m_scale = std::min(sx, sy);
        m_scroll = {rect.x + rect.width * 0.5 - vw * 0.5 / m_scale,
                    rect.y + rect.height * 0.5 - vh * 0.5 / m_scale};
        syncProxyWidgets_(); update();
    }

    /// Convenience overload that fits a rectangle expressed as coordinates and size.
    void fitInView(double x, double y, double w, double h, int mode = 0) {
        fitInView(SwRectF(x, y, w, h), mode);
    }

    /// Fits the scene bounding rectangle of the supplied item inside the viewport.
    void fitInView(const SwGraphicsItem* item, int aspectRatioMode = 0) {
        if (item) fitInView(item->sceneBoundingRect(), aspectRatioMode);
    }

    /// Centers the view on a scene-space point.
    void centerOn(const SwPointF& pos) {
        const double vw = static_cast<double>(width());
        const double vh = static_cast<double>(height());
        m_scroll = {pos.x - vw * 0.5 / m_scale, pos.y - vh * 0.5 / m_scale};
        syncProxyWidgets_(); update();
    }

    /// Convenience overload that centers the view from scalar scene coordinates.
    void centerOn(double x, double y) { centerOn(SwPointF(x, y)); }

    /// Centers the view on the scene bounding rectangle of the supplied item.
    void centerOn(const SwGraphicsItem* item) {
        if (item) centerOn(item->sceneBoundingRect().center());
    }

    /**
     * @brief Scrolls the view enough to make a scene rectangle visible.
     * @param rect Rectangle that should become visible.
     * @param xmargin Horizontal visibility margin in device pixels.
     * @param ymargin Vertical visibility margin in device pixels.
     */
    void ensureVisible(const SwRectF& rect, int xmargin = 50, int ymargin = 50) {
        SwRectF visible = mapRectToScene(this->rect());
        double mx = static_cast<double>(xmargin) / m_scale;
        double my = static_cast<double>(ymargin) / m_scale;
        if (rect.left() < visible.left() + mx) m_scroll.x = rect.left() - mx;
        if (rect.right() > visible.right() - mx) m_scroll.x = rect.right() + mx - visible.width;
        if (rect.top() < visible.top() + my) m_scroll.y = rect.top() - my;
        if (rect.bottom() > visible.bottom() - my) m_scroll.y = rect.bottom() + my - visible.height;
        syncProxyWidgets_(); update();
    }

    /// Convenience overload that ensures visibility for coordinates and size.
    void ensureVisible(double x, double y, double w, double h, int xm = 50, int ym = 50) {
        ensureVisible(SwRectF(x, y, w, h), xm, ym);
    }

    /// Ensures that the scene bounding rectangle of the supplied item becomes visible.
    void ensureVisible(const SwGraphicsItem* item, int xmargin = 50, int ymargin = 50) {
        if (item) ensureVisible(item->sceneBoundingRect(), xmargin, ymargin);
    }

    // ===================================================================
    // Item lookup through the view
    // ===================================================================
    /// Returns the top-most scene item located under a widget-space point.
    SwGraphicsItem* itemAt(const SwPoint& pos) const {
        if (!m_scene) return nullptr;
        return m_scene->itemAt(mapToScene(SwPointF(pos.x, pos.y)));
    }
    /// Convenience overload that takes the query position as scalar widget coordinates.
    SwGraphicsItem* itemAt(int x, int y) const { return itemAt(SwPoint{x, y}); }

    /// Returns all items currently registered in the attached scene.
    std::vector<SwGraphicsItem*> items() const {
        return m_scene ? m_scene->items() : std::vector<SwGraphicsItem*>{};
    }
    /// Returns items found under a widget-space point.
    std::vector<SwGraphicsItem*> items(const SwPoint& pos) const {
        if (!m_scene) return {};
        return m_scene->items(mapToScene(SwPointF(pos.x, pos.y)));
    }
    /// Returns items intersecting a widget-space rectangle.
    std::vector<SwGraphicsItem*> items(const SwRect& rect) const {
        if (!m_scene) return {};
        return m_scene->items(mapToScene(rect));
    }

    // ===================================================================
    // Drag mode
    // ===================================================================
    /// Selects how mouse dragging should be interpreted by the view.
    void setDragMode(DragMode mode) { m_dragMode = mode; }
    /// Returns the current drag interaction mode.
    DragMode dragMode() const { return m_dragMode; }

    /// Returns the current rubber-band rectangle in scene coordinates.
    SwRectF rubberBandRect() const { return m_rubberBandRect; }

    // ===================================================================
    // Interactive
    // ===================================================================
    /// Enables or disables forwarding of interactive input to the attached scene.
    void setInteractive(bool on) { m_interactive = on; }
    /// Returns whether interactive input is forwarded to the attached scene.
    bool isInteractive() const { return m_interactive; }

    // ===================================================================
    // Render hints
    // ===================================================================
    /// Enables or disables a single render-hint flag.
    void setRenderHint(RenderHint hint, bool on = true) {
        if (on) m_renderHints |= static_cast<int>(hint);
        else m_renderHints &= ~static_cast<int>(hint);
    }
    /// Replaces the full render-hint bitmask.
    void setRenderHints(int hints) { m_renderHints = hints; }
    /// Returns the active render-hint bitmask.
    int renderHints() const { return m_renderHints; }

    // ===================================================================
    // Viewport update mode
    // ===================================================================
    /// Sets the viewport update strategy used by the view.
    void setViewportUpdateMode(ViewportUpdateMode mode) { m_viewportUpdateMode = mode; }
    /// Returns the viewport update strategy used by the view.
    ViewportUpdateMode viewportUpdateMode() const { return m_viewportUpdateMode; }

    // ===================================================================
    // Optimization flags
    // ===================================================================
    /// Enables or disables one optimization flag.
    void setOptimizationFlag(OptimizationFlag flag, bool on = true) {
        if (on) m_optimizationFlags |= static_cast<int>(flag);
        else m_optimizationFlags &= ~static_cast<int>(flag);
    }
    /// Replaces the full optimization-flag bitmask.
    void setOptimizationFlags(int flags) { m_optimizationFlags = flags; }
    /// Returns the active optimization-flag bitmask.
    int optimizationFlags() const { return m_optimizationFlags; }

    // ===================================================================
    // Anchors
    // ===================================================================
    /// Sets the anchor used when applying view transformations.
    void setTransformationAnchor(ViewportAnchor anchor) { m_transformationAnchor = anchor; }
    /// Returns the anchor used when applying view transformations.
    ViewportAnchor transformationAnchor() const { return m_transformationAnchor; }

    /// Sets the anchor used when the view is resized.
    void setResizeAnchor(ViewportAnchor anchor) { m_resizeAnchor = anchor; }
    /// Returns the anchor used when the view is resized.
    ViewportAnchor resizeAnchor() const { return m_resizeAnchor; }

    // ===================================================================
    // Alignment
    // ===================================================================
    /// Sets the content alignment policy used by the view.
    void setAlignment(int alignment) { m_alignment = alignment; update(); }
    /// Returns the content alignment policy used by the view.
    int alignment() const { return m_alignment; }

    // ===================================================================
    // Cache mode
    // ===================================================================
    /// Sets the cache mode bitmask used by the view.
    void setCacheMode(int mode) { m_cacheMode = mode; }
    /// Returns the cache mode bitmask used by the view.
    int cacheMode() const { return m_cacheMode; }
    /// Drops cached content and schedules a repaint.
    void resetCachedContent() { update(); }

    // ===================================================================
    // Scene event forwarding helpers (for advanced use)
    // ===================================================================
    /// Requests a refresh after a scene update affecting multiple rectangles.
    void updateScene(const std::vector<SwRectF>& /*rects*/) { update(); }
    /// Requests a refresh after a scene-rectangle update.
    void updateSceneRect(const SwRectF& /*rect*/) { update(); }
    /// Requests a refresh after explicit scene invalidation.
    void invalidateScene(const SwRectF& /*rect*/ = SwRectF(), int /*layers*/ = 0) { update(); }

protected:
    // ===================================================================
    // Paint
    // ===================================================================
    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!event || !event->painter() || !isVisibleInHierarchy()) return;
        syncProxyWidgets_();

        SwPainter* painter = event->painter();
        const SwRect deviceRect = this->rect();
        const SwRect viewRect = this->rect();

        // Background
        SwColor bgColor = {255, 255, 255};
        bool paintBackground = true;
        StyleSheet* sheet = getToolSheet();
        auto selectors = classHierarchy();
        if (!selectors.contains("SwWidget")) selectors.append(SwString("SwWidget"));
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) continue;
            SwString value;
            if (sheet) value = sheet->getStyleProperty(selector, "background-color");
            if (!value.isEmpty()) {
                float alpha = 1.0f;
                try {
                    SwColor resolved = sheet ? sheet->parseColor(value, &alpha) : bgColor;
                    if (alpha <= 0.0f) paintBackground = false;
                    else { bgColor = resolved; paintBackground = true; }
                } catch (...) {}
                break;
            }
        }
        if (paintBackground) this->m_style->drawBackground(deviceRect, painter, bgColor);

        painter->pushClipRect(deviceRect);

        // Scene background
        if (m_scene) {
            m_scene->drawBackground(painter, mapRectToScene(viewRect));
        }

        // Draw scene content
        if (m_scene) {
            SwGraphicsRenderContext ctx;
            ctx.viewRect = deviceRect;
            ctx.scroll = m_scroll;
            ctx.scale = m_scale;
            ctx.viewTransform = m_viewTransform;

            std::vector<SwGraphicsItem*> visible;
            for (SwGraphicsItem* item : m_scene->items()) {
                if (item && item->isVisible()) visible.push_back(item);
            }
            std::stable_sort(visible.begin(), visible.end(), [](SwGraphicsItem* a, SwGraphicsItem* b) {
                if (!a || !b) return a < b;
                return a->zValue() < b->zValue();
            });

            for (SwGraphicsItem* item : visible) {
                if (!item) continue;
                item->paint(painter, ctx);
            }
        }

        // Scene foreground
        if (m_scene) {
            m_scene->drawForeground(painter, mapRectToScene(viewRect));
        }

        // Rubber band
        if (m_dragMode == RubberBandDrag && !m_rubberBandRect.isEmpty()) {
            SwRect rb = mapRectFromScene(m_rubberBandRect);
            painter->drawRect(rb, SwColor{70, 130, 180}, 1);
        }

        // Paint embedded widgets on top
        const SwRect& paintRect = event->paintRect();
        for (SwObject* objChild : children()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child || !child->isVisibleInHierarchy()) continue;
            SwRect childRect = child->geometry();
            if (rectsIntersect(paintRect, childRect))
                paintChild_(event, child);
        }

        painter->popClipRect();
    }

    // ===================================================================
    // Resize
    // ===================================================================
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        syncProxyWidgets_();
    }

    // ===================================================================
    // Mouse events â€” forward to scene
    // ===================================================================
    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mousePressEvent(MouseEvent* event) override {
        if (!m_scene || !m_interactive) { SwWidget::mousePressEvent(event); return; }

        SwPointF viewPos(static_cast<double>(event->x()), static_cast<double>(event->y()));
        SwPointF scenePos = mapToScene(viewPos);
        int modifiers = buildModifiers_(event);

        if (m_dragMode == ScrollHandDrag) {
            m_dragging = true;
            m_lastDragPos = viewPos;
            setCursor(CursorType::Hand);
            event->accept();
            return;
        }

        if (m_dragMode == RubberBandDrag) {
            m_rubberBanding = true;
            m_rubberBandOrigin = scenePos;
            m_rubberBandRect = SwRectF(scenePos.x, scenePos.y, 0, 0);
        }

        m_scene->mousePressEvent_(scenePos, event->globalPos(), event->button(), modifiers);
        event->accept();
    }

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseMoveEvent(MouseEvent* event) override {
        if (!m_scene || !m_interactive) { SwWidget::mouseMoveEvent(event); return; }

        SwPointF viewPos(static_cast<double>(event->x()), static_cast<double>(event->y()));
        SwPointF scenePos = mapToScene(viewPos);
        int modifiers = buildModifiers_(event);
        int buttons = 0;
        if (event->button() == SwMouseButton::Left) buttons |= SwMouseButtons::LeftButton;

        if (m_dragging) {
            double dx = viewPos.x - m_lastDragPos.x;
            double dy = viewPos.y - m_lastDragPos.y;
            m_scroll.x -= dx / m_scale;
            m_scroll.y -= dy / m_scale;
            m_lastDragPos = viewPos;
            syncProxyWidgets_();
            update();
            event->accept();
            return;
        }

        if (m_rubberBanding) {
            double x = std::min(m_rubberBandOrigin.x, scenePos.x);
            double y = std::min(m_rubberBandOrigin.y, scenePos.y);
            double w = std::abs(scenePos.x - m_rubberBandOrigin.x);
            double h = std::abs(scenePos.y - m_rubberBandOrigin.y);
            m_rubberBandRect = SwRectF(x, y, w, h);

            // Update selection
            SwPainterPath path;
            path.addRect(m_rubberBandRect);
            m_scene->setSelectionArea(path);
            update();
            event->accept();
            return;
        }

        m_scene->mouseMoveEvent_(scenePos, event->globalPos(), buttons, modifiers);
        event->accept();
    }

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseReleaseEvent(MouseEvent* event) override {
        if (!m_scene || !m_interactive) { SwWidget::mouseReleaseEvent(event); return; }

        SwPointF viewPos(static_cast<double>(event->x()), static_cast<double>(event->y()));
        SwPointF scenePos = mapToScene(viewPos);
        int modifiers = buildModifiers_(event);

        if (m_dragging) {
            m_dragging = false;
            setCursor(CursorType::Arrow);
            event->accept();
            return;
        }

        if (m_rubberBanding) {
            m_rubberBanding = false;
            m_rubberBandRect = {};
            update();
        }

        m_scene->mouseReleaseEvent_(scenePos, event->globalPos(), event->button(), modifiers);
        event->accept();
    }

    /**
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!m_scene || !m_interactive) { SwWidget::mouseDoubleClickEvent(event); return; }
        SwPointF viewPos(static_cast<double>(event->x()), static_cast<double>(event->y()));
        SwPointF scenePos = mapToScene(viewPos);
        int modifiers = buildModifiers_(event);
        m_scene->mouseDoubleClickEvent_(scenePos, event->globalPos(), event->button(), modifiers);
        event->accept();
    }

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void wheelEvent(WheelEvent* event) override {
        if (!m_scene || !m_interactive) { SwWidget::wheelEvent(event); return; }
        SwPointF viewPos(static_cast<double>(event->x()), static_cast<double>(event->y()));
        SwPointF scenePos = mapToScene(viewPos);
        int modifiers = 0;
        if (event->isCtrlPressed()) modifiers |= SwKeyboardModifier::ControlModifier;
        if (event->isShiftPressed()) modifiers |= SwKeyboardModifier::ShiftModifier;
        if (event->isAltPressed()) modifiers |= SwKeyboardModifier::AltModifier;
        m_scene->wheelEvent_(scenePos, event->globalPos(), event->delta(), modifiers);
        event->accept();
    }

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void keyPressEvent(KeyEvent* event) override {
        if (!m_scene || !m_interactive) { SwWidget::keyPressEvent(event); return; }
        m_scene->keyPressEvent_(event);
    }

    // ===================================================================
    // Virtual drawing hooks (override for custom background/foreground)
    // ===================================================================
    /**
     * @brief Performs the `drawBackground` operation.
     * @param painter Value passed to the method.
     * @param rect Rectangle used by the operation.
     * @return The requested draw Background.
     */
    virtual void drawBackground(SwPainter* painter, const SwRectF& rect) {
        if (m_scene) m_scene->drawBackground(painter, rect);
    }

    /**
     * @brief Performs the `drawForeground` operation.
     * @param painter Value passed to the method.
     * @param rect Rectangle used by the operation.
     * @return The requested draw Foreground.
     */
    virtual void drawForeground(SwPainter* painter, const SwRectF& rect) {
        if (m_scene) m_scene->drawForeground(painter, rect);
    }

private:
    int buildModifiers_(MouseEvent* event) const {
        int m = 0;
        if (event->isCtrlPressed()) m |= SwKeyboardModifier::ControlModifier;
        if (event->isShiftPressed()) m |= SwKeyboardModifier::ShiftModifier;
        if (event->isAltPressed()) m |= SwKeyboardModifier::AltModifier;
        return m;
    }

    void onSceneChanged_() { syncProxyWidgets_(); update(); }

    void syncProxyWidgets_() {
        auto deleteOrDetachProxyWidget = [&](SwWidget* widget) {
            if (!widget || widget->parent() != this) return;
            bool deleteOnRemove = true;
            static const SwString kDeleteOnRemoveProp("sw.graphics.proxy.deleteOnRemove");
            if (widget->propertyExist(kDeleteOnRemoveProp))
                deleteOnRemove = widget->property(kDeleteOnRemoveProp).toBool();
            if (deleteOnRemove) delete widget;
            else widget->setParent(nullptr);
        };

        if (!m_scene) {
            for (SwWidget* w : m_proxyWidgets) deleteOrDetachProxyWidget(w);
            m_proxyWidgets.clear();
            return;
        }

        std::vector<SwWidget*> liveWidgets;
        for (SwGraphicsItem* item : m_scene->items()) {
            auto* proxy = dynamic_cast<SwGraphicsProxyWidget*>(item);
            if (!proxy) continue;
            SwWidget* w = proxy->widget();
            if (!w) continue;
            liveWidgets.push_back(w);
            if (w->parent() != this) w->setParent(this);

            const SwPointF sp = proxy->scenePos();
            const SwPointF vp = mapFromScene(sp);

            SwSize base = proxy->widgetBaseSize();
            int ww = base.width > 0 ? base.width : w->width();
            int wh = base.height > 0 ? base.height : w->height();
            if (ww <= 0 || wh <= 0) { ww = 160; wh = 40; }

            int scaledW = std::max(1, static_cast<int>(std::lround(static_cast<double>(ww) * m_scale)));
            int scaledH = std::max(1, static_cast<int>(std::lround(static_cast<double>(wh) * m_scale)));

            w->move(static_cast<int>(std::lround(vp.x)), static_cast<int>(std::lround(vp.y)));
            w->resize(scaledW, scaledH);
        }

        for (SwWidget* w : m_proxyWidgets) {
            if (!w) continue;
            if (std::find(liveWidgets.begin(), liveWidgets.end(), w) != liveWidgets.end()) continue;
            deleteOrDetachProxyWidget(w);
        }
        m_proxyWidgets = std::move(liveWidgets);
    }

    SwPointF mapFromSceneDevice_(const SwPointF& scenePoint) const {
        return mapFromScene(scenePoint);
    }

    SwRect mapFromSceneDevice_(const SwRectF& sceneRect) const {
        const SwPointF p1 = mapFromSceneDevice_(SwPointF(sceneRect.x, sceneRect.y));
        const SwPointF p2 = mapFromSceneDevice_(SwPointF(sceneRect.x + sceneRect.width, sceneRect.y));
        const SwPointF p3 = mapFromSceneDevice_(SwPointF(sceneRect.x + sceneRect.width,
                                                         sceneRect.y + sceneRect.height));
        const SwPointF p4 = mapFromSceneDevice_(SwPointF(sceneRect.x,
                                                         sceneRect.y + sceneRect.height));
        const double minX = std::min(std::min(p1.x, p2.x), std::min(p3.x, p4.x));
        const double minY = std::min(std::min(p1.y, p2.y), std::min(p3.y, p4.y));
        const double maxX = std::max(std::max(p1.x, p2.x), std::max(p3.x, p4.x));
        const double maxY = std::max(std::max(p1.y, p2.y), std::max(p3.y, p4.y));
        int x = static_cast<int>(std::lround(minX));
        int y = static_cast<int>(std::lround(minY));
        int w = static_cast<int>(std::lround(maxX - minX));
        int h = static_cast<int>(std::lround(maxY - minY));
        return SwRect{x, y, w, h};
    }

    // --- Members ---
    SwGraphicsScene* m_scene{nullptr};
    double m_scale{1.0};
    SwPointF m_scroll{0.0, 0.0};
    SwTransform m_viewTransform{};
    SwRectF m_sceneRectOverride{};

    // Drag mode
    DragMode m_dragMode{NoDrag};
    bool m_interactive{true};
    bool m_dragging{false};
    SwPointF m_lastDragPos{};

    // Rubber band
    bool m_rubberBanding{false};
    SwPointF m_rubberBandOrigin{};
    SwRectF m_rubberBandRect{};

    // Render hints / optimization
    int m_renderHints{0};
    ViewportUpdateMode m_viewportUpdateMode{MinimalViewportUpdate};
    int m_optimizationFlags{0};

    // Anchors / alignment
    ViewportAnchor m_transformationAnchor{AnchorViewCenter};
    ViewportAnchor m_resizeAnchor{NoAnchor};
    int m_alignment{AlignCenter};
    int m_cacheMode{CacheNone};

    // Proxy widgets
    std::vector<SwWidget*> m_proxyWidgets;
};

