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

/***************************************************************************************************
 * SwGraphicsScene  —  Qt-like QGraphicsScene with full API coverage.
 *
 * - Item management: addItem, removeItem, clear, items (sorted)
 * - Hit-testing: itemAt, items(point), items(rect), items(path), items(polygon)
 * - Selection: selectedItems, setSelectionArea, clearSelection
 * - Focus: focusItem, setFocusItem, clearFocus
 * - Mouse grab: mouseGrabberItem
 * - Event dispatch to items (mouse, hover, key, wheel, drag-drop, context menu)
 * - Background / foreground brush
 * - Convenience helpers (addRect, addEllipse, addLine, addText, …)
 * - Signals: changed, selectionChanged, focusItemChanged, sceneRectChanged
 **************************************************************************************************/

/**
 * @file
 * @ingroup core_graphics
 * @brief Declares the retained-mode scene that owns graphics items and dispatches interaction.
 *
 * @details
 * `SwGraphicsScene` stores items, defines the logical scene rectangle, performs hit testing,
 * manages selection and focus state, and routes input events toward the appropriate scene item.
 * It is the model-side counterpart of `SwGraphicsView`, which is responsible for viewport
 * presentation.
 */

#include "SwObject.h"

#include "graphics/SwGraphicsItems.h"
#include "graphics/SwGraphicsSceneEvent.h"
#include "graphics/SwGraphicsTypes.h"
#include "graphics/SwPainterPath.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

class SwGraphicsView;

/**
 * @class SwGraphicsScene
 * @brief Owns a collection of graphics items together with scene-wide state.
 *
 * @details
 * `SwGraphicsScene` is the retained-mode scene container behind `SwGraphicsView`. It keeps track
 * of graphics items, selection, focus, hover state, grabbers, background / foreground brushes, and
 * attached views, while also offering a Qt-like convenience API for creating common scene items.
 *
 * Responsibilities are split intentionally:
 * - items own their own geometry, painting, and item-local interaction policy,
 * - the scene resolves hit testing and scene-wide state transitions,
 * - the view maps the scene into a concrete viewport and forwards input into scene coordinates.
 *
 * This separation keeps item code reusable and independent from any single widget or platform
 * backend.
 */
class SwGraphicsScene : public SwObject {
    SW_OBJECT(SwGraphicsScene, SwObject)

public:
    // --- Item sort order ---
    enum ItemIndexMethod { NoIndex, BspTreeIndex };

    /// Constructs an empty scene and optionally attaches it to a parent object.
    explicit SwGraphicsScene(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /// Constructs a scene with an explicit logical rectangle.
    SwGraphicsScene(const SwRectF& sceneRect, SwObject* parent = nullptr)
        : SwObject(parent), m_sceneRect(sceneRect) {}

    /// Convenience constructor that takes the scene rectangle as coordinates and size.
    SwGraphicsScene(double x, double y, double w, double h, SwObject* parent = nullptr)
        : SwObject(parent), m_sceneRect(x, y, w, h) {}

    /// Destroys the scene and deletes any remaining top-level items that it still owns.
    ~SwGraphicsScene() override {
        for (SwGraphicsItem* item : m_items) {
            if (item && item->parentItem() == nullptr) delete item;
        }
        m_items.clear();
    }

    // ===================================================================
    // Scene rect
    // ===================================================================
    /**
     * @brief Sets the explicit logical rectangle associated with the scene.
     * @param rect New scene bounds in scene coordinates.
     *
     * Once set, this rectangle becomes the value returned by sceneRect()
     * instead of deriving bounds from the currently registered items.
     */
    void setSceneRect(const SwRectF& rect) { m_sceneRect = rect; emit sceneRectChanged(m_sceneRect); changed(); }

    /// Convenience overload that builds the scene rectangle from coordinates and size.
    void setSceneRect(double x, double y, double w, double h) { setSceneRect(SwRectF(x, y, w, h)); }

    /**
     * @brief Returns the scene rectangle exposed by the scene.
     * @return The explicit rectangle if one was set, otherwise the union of item bounds.
     */
    SwRectF sceneRect() const {
        if (!m_sceneRect.isEmpty()) return m_sceneRect;
        return itemsBoundingRect();
    }

    /// Returns the current scene width in scene units.
    double width() const { return sceneRect().width; }
    /// Returns the current scene height in scene units.
    double height() const { return sceneRect().height; }

    // ===================================================================
    // Item management
    // ===================================================================
    /**
     * @brief Adds an item to the scene if it is not already present.
     * @param item Item to register.
     *
     * The scene installs its internal scene pointer and change callback so item
     * updates can invalidate the scene and any attached views.
     */
    void addItem(SwGraphicsItem* item) {
        if (!item) return;
        if (std::find(m_items.begin(), m_items.end(), item) != m_items.end()) return;
        item->setScene_(this);
        item->setChangeCallback_([this]() { changed(); });
        m_items.push_back(item);
        changed();
    }

    /**
     * @brief Removes an item from the scene and deletes it.
     * @param item Item to remove.
     *
     * When the item owns child items, the whole subtree is detached first.
     * Focus, hover, and grabber references targeting the removed item are also cleared.
     */
    void removeItem(SwGraphicsItem* item) {
        if (!item) return;
        if (std::find(m_items.begin(), m_items.end(), item) == m_items.end()) return;
        // Remove subtree
        std::vector<SwGraphicsItem*> stack;
        stack.push_back(item);
        std::vector<SwGraphicsItem*> subtree;
        while (!stack.empty()) {
            SwGraphicsItem* cur = stack.back(); stack.pop_back();
            if (!cur) continue;
            subtree.push_back(cur);
            for (SwGraphicsItem* child : cur->childItems()) if (child) stack.push_back(child);
        }
        for (SwGraphicsItem* cur : subtree) {
            auto it = std::find(m_items.begin(), m_items.end(), cur);
            if (it != m_items.end()) m_items.erase(it);
            cur->setScene_(nullptr);
            cur->setChangeCallback_(std::function<void()>());
        }
        // Cleanup references
        if (m_focusItem == item) m_focusItem = nullptr;
        if (m_mouseGrabberItem == item) m_mouseGrabberItem = nullptr;
        if (m_keyboardGrabberItem == item) m_keyboardGrabberItem = nullptr;
        if (m_lastHoverItem == item) m_lastHoverItem = nullptr;
        item->setParentItem(nullptr);
        delete item;
        changed();
    }

    /**
     * @brief Destroys a group item while preserving the child items it contains.
     * @param group Group item to dismantle.
     */
    void destroyItemGroup(SwGraphicsItemGroup* group) {
        if (!group) return;
        // Re-parent children
        auto children = group->childItems();
        for (auto* child : children) child->setParentItem(group->parentItem());
        removeItem(group);
    }

    /**
     * @brief Creates a new group item and adds the supplied items to it.
     * @param items Items that should become children of the created group.
     * @return The newly created group item.
     */
    SwGraphicsItemGroup* createItemGroup(const std::vector<SwGraphicsItem*>& items) {
        auto* group = new SwGraphicsItemGroup();
        addItem(group);
        for (auto* item : items) group->addToGroup(item);
        return group;
    }

    /// Removes all items from the scene, resets scene-level interaction state, and emits change.
    void clear() {
        m_focusItem = nullptr;
        m_mouseGrabberItem = nullptr;
        m_keyboardGrabberItem = nullptr;
        m_lastHoverItem = nullptr;
        for (SwGraphicsItem* item : m_items) {
            if (item && item->parentItem() == nullptr) delete item;
        }
        m_items.clear();
        changed();
    }

    // ===================================================================
    // Item queries
    // ===================================================================
    /// Returns the raw list of items currently registered in the scene.
    const std::vector<SwGraphicsItem*>& items() const { return m_items; }

    /**
     * @brief Returns all items sorted by z value.
     * @param sortOrder `0` for ascending z order, `1` for descending z order.
     * @return A copied and sorted item list.
     */
    std::vector<SwGraphicsItem*> items(int sortOrder) const {
        std::vector<SwGraphicsItem*> sorted = m_items;
        if (sortOrder == 0)
            std::stable_sort(sorted.begin(), sorted.end(), [](SwGraphicsItem* a, SwGraphicsItem* b) { return a->zValue() < b->zValue(); });
        else
            std::stable_sort(sorted.begin(), sorted.end(), [](SwGraphicsItem* a, SwGraphicsItem* b) { return a->zValue() > b->zValue(); });
        return sorted;
    }

    /**
     * @brief Returns the items located at a scene position.
     * @param pos Query position in scene coordinates.
     * @param mode Selection mode used for hit testing.
     * @param sortOrder `0` for ascending z order, `1` for descending z order.
     * @return Matching items sorted using the requested z-order policy.
     */
    std::vector<SwGraphicsItem*> items(const SwPointF& pos,
                                       SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape,
                                       int sortOrder = 1 /*descending*/) const {
        std::vector<SwGraphicsItem*> result;
        for (SwGraphicsItem* item : m_items) {
            if (!item || !item->isVisible()) continue;
            if (mode == SwItemSelectionMode::IntersectsItemBoundingRect ||
                mode == SwItemSelectionMode::ContainsItemBoundingRect) {
                if (item->sceneBoundingRect().contains(pos)) result.push_back(item);
            } else {
                if (item->containsScenePoint(pos)) result.push_back(item);
            }
        }
        sortItemsByZ_(result, sortOrder);
        return result;
    }

    /**
     * @brief Returns the items that intersect or are contained in a rectangle.
     * @param rect Query rectangle in scene coordinates.
     * @param mode Selection mode controlling containment versus intersection semantics.
     * @return Matching items.
     */
    std::vector<SwGraphicsItem*> items(const SwRectF& rect,
                                       SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) const {
        std::vector<SwGraphicsItem*> result;
        for (SwGraphicsItem* item : m_items) {
            if (!item || !item->isVisible()) continue;
            SwRectF sbr = item->sceneBoundingRect();
            if (mode == SwItemSelectionMode::ContainsItemBoundingRect ||
                mode == SwItemSelectionMode::ContainsItemShape) {
                if (rect.contains(sbr)) result.push_back(item);
            } else {
                if (rect.intersects(sbr)) result.push_back(item);
            }
        }
        return result;
    }

    /// Returns items selected by the bounding rectangle of the supplied polygon.
    std::vector<SwGraphicsItem*> items(const SwPolygonF& polygon,
                                       SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) const {
        return items(polygon.boundingRect(), mode);
    }

    /// Returns items selected by the bounding rectangle of the supplied painter path.
    std::vector<SwGraphicsItem*> items(const SwPainterPath& path,
                                       SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) const {
        return items(path.boundingRect(), mode);
    }

    /**
     * @brief Returns the top-most item at a scene position.
     * @param pos Query position in scene coordinates.
     * @param deviceTransform Reserved compatibility parameter currently ignored.
     * @return The front-most matching item, or `nullptr` if none is found.
     */
    SwGraphicsItem* itemAt(const SwPointF& pos, const SwTransform& /*deviceTransform*/ = SwTransform()) const {
        auto found = items(pos, SwItemSelectionMode::IntersectsItemShape, 1);
        return found.empty() ? nullptr : found.front();
    }

    /// Convenience overload that takes the query position as scalar coordinates.
    SwGraphicsItem* itemAt(double x, double y, const SwTransform& t = SwTransform()) const {
        return itemAt({x, y}, t);
    }

    /**
     * @brief Computes the union of the scene bounding rectangles of all items.
     * @return Bounding rectangle covering every registered item.
     */
    SwRectF itemsBoundingRect() const {
        SwRectF r;
        for (auto* item : m_items) {
            if (!item) continue;
            SwRectF sbr = item->sceneBoundingRect();
            r = r.isEmpty() ? sbr : r.united(sbr);
        }
        return r;
    }

    // ===================================================================
    // Selection
    // ===================================================================
    /// Returns the items whose selected flag is currently set.
    std::vector<SwGraphicsItem*> selectedItems() const {
        std::vector<SwGraphicsItem*> result;
        for (auto* item : m_items) {
            if (item && item->isSelected()) result.push_back(item);
        }
        return result;
    }

    /// Returns the path most recently stored as the scene selection area.
    SwPainterPath selectionArea() const { return m_selectionArea; }

    /**
     * @brief Replaces the selection area and updates item selection from it.
     * @param path Selection shape expressed in scene coordinates.
     * @param mode Selection mode controlling containment versus intersection semantics.
     */
    void setSelectionArea(const SwPainterPath& path,
                          SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) {
        m_selectionArea = path;
        auto areaItems = items(path, mode);
        for (auto* item : m_items) {
            if (!item) continue;
            bool inArea = std::find(areaItems.begin(), areaItems.end(), item) != areaItems.end();
            if (item->testFlag(SwGraphicsItem::ItemIsSelectable))
                item->setSelected(inArea);
        }
        emit selectionChanged();
    }

    /// Compatibility overload that currently ignores the device transform parameter.
    void setSelectionArea(const SwPainterPath& path, const SwTransform& /*deviceTransform*/,
                          SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) {
        setSelectionArea(path, mode);
    }

    /// Clears the selected state of all items and emits `selectionChanged` when necessary.
    void clearSelection() {
        bool had = false;
        for (auto* item : m_items) {
            if (item && item->isSelected()) { item->setSelected(false); had = true; }
        }
        if (had) emit selectionChanged();
    }

    // ===================================================================
    // Focus
    // ===================================================================
    /// Returns the item that currently owns scene focus, or `nullptr`.
    SwGraphicsItem* focusItem() const { return m_focusItem; }

    /**
     * @brief Moves scene focus from the current item to another item.
     * @param item New focused item, or `nullptr` to clear focus.
     * @param reason Focus reason forwarded to generated focus events.
     */
    void setFocusItem(SwGraphicsItem* item,
                      SwFocusEvent::FocusReason reason = SwFocusEvent::OtherFocusReason) {
        if (m_focusItem == item) return;
        SwGraphicsItem* old = m_focusItem;
        if (old) {
            old->setHasFocus_(false);
            SwFocusEvent ev(SwGraphicsSceneEventType::FocusOut, reason);
            old->focusOutEvent(&ev);
        }
        m_focusItem = item;
        if (item) {
            item->setHasFocus_(true);
            SwFocusEvent ev(SwGraphicsSceneEventType::FocusIn, reason);
            item->focusInEvent(&ev);
        }
        emit focusItemChanged();
    }

    /// Clears scene focus by removing the currently focused item.
    void clearFocus() { setFocusItem(nullptr); }

    /// Returns `true` when an item currently owns focus in the scene.
    bool hasFocus() const { return m_focusItem != nullptr; }

    /**
     * @brief Requests scene-level focus.
     * @param reason Reserved focus reason parameter.
     *
     * This placeholder mirrors the Qt API shape. Actual native focus activation
     * is expected to happen at the view or widget layer.
     */
    void setFocus(SwFocusEvent::FocusReason /*reason*/ = SwFocusEvent::OtherFocusReason) {
        // Scene-level focus — Qt activates the scene's view window
    }

    // ===================================================================
    // Sticky focus  (Qt 5+ API)
    // ===================================================================
    /// Returns whether focus should remain when the user clicks on empty scene space.
    bool stickyFocus() const { return m_stickyFocus; }
    /// Enables or disables sticky focus behavior.
    void setStickyFocus(bool enabled) { m_stickyFocus = enabled; }

    // ===================================================================
    // Mouse grabber
    // ===================================================================
    /// Returns the item currently grabbing mouse events, if any.
    SwGraphicsItem* mouseGrabberItem() const { return m_mouseGrabberItem; }

    // ===================================================================
    // Item index method
    // ===================================================================
    /// Returns the indexing strategy currently selected for the scene.
    ItemIndexMethod itemIndexMethod() const { return m_indexMethod; }
    /// Selects the indexing strategy used by the scene.
    void setItemIndexMethod(ItemIndexMethod method) { m_indexMethod = method; }

    /// Returns the configured BSP tree depth hint.
    int bspTreeDepth() const { return m_bspTreeDepth; }
    /// Sets the BSP tree depth hint.
    void setBspTreeDepth(int depth) { m_bspTreeDepth = depth; }

    // ===================================================================
    // Minimum render size
    // ===================================================================
    /// Returns the minimum render size threshold stored on the scene.
    double minimumRenderSize() const { return m_minimumRenderSize; }
    /// Sets the minimum render size threshold used by clients that honor it.
    void setMinimumRenderSize(double minSize) { m_minimumRenderSize = minSize; }

    // ===================================================================
    // Background / foreground
    // ===================================================================
    /// Returns the brush used by the default background drawing implementation.
    SwBrush backgroundBrush() const { return m_backgroundBrush; }
    /// Sets the brush used by the default background drawing implementation.
    void setBackgroundBrush(const SwBrush& brush) { m_backgroundBrush = brush; changed(); }

    /// Returns the brush associated with scene foreground decoration.
    SwBrush foregroundBrush() const { return m_foregroundBrush; }
    /// Sets the brush associated with scene foreground decoration.
    void setForegroundBrush(const SwBrush& brush) { m_foregroundBrush = brush; changed(); }

    /**
     * @brief Draws the scene background for the given exposed rectangle.
     * @param painter Painter used for rendering.
     * @param rect Rectangle to paint in scene coordinates.
     *
     * The default implementation fills the region when a solid background brush
     * is configured. Subclasses can override this to draw grids or backdrops.
     */
    virtual void drawBackground(SwPainter* painter, const SwRectF& rect) {
        if (m_backgroundBrush.style() == SwBrush::SolidPattern) {
            SwRect ir{static_cast<int>(rect.x), static_cast<int>(rect.y),
                      static_cast<int>(rect.width), static_cast<int>(rect.height)};
            painter->fillRect(ir, m_backgroundBrush.color(), SwColor{0,0,0}, 0);
        }
    }

    /**
     * @brief Draws additional scene content after items have been painted.
     * @param painter Painter used for rendering.
     * @param rect Rectangle to paint in scene coordinates.
     */
    virtual void drawForeground(SwPainter* /*painter*/, const SwRectF& /*rect*/) {
        // Override in subclass for grid lines, rulers, etc.
    }

    // ===================================================================
    // Font
    // ===================================================================
    /// Returns the default scene font.
    SwFont font() const { return m_font; }
    /// Sets the default scene font and emits a generic scene change notification.
    void setFont(const SwFont& font) { m_font = font; changed(); }

    // ===================================================================
    // Views
    // ===================================================================
    /// Returns the graphics views currently attached to this scene.
    std::vector<SwGraphicsView*> views() const { return m_views; }
    /// Internal helper used by SwGraphicsView to register itself with the scene.
    void addView_(SwGraphicsView* v) {
        if (std::find(m_views.begin(), m_views.end(), v) == m_views.end())
            m_views.push_back(v);
    }
    /// Internal helper used by SwGraphicsView to unregister itself from the scene.
    void removeView_(SwGraphicsView* v) {
        m_views.erase(std::remove(m_views.begin(), m_views.end(), v), m_views.end());
    }

    // ===================================================================
    // Update / invalidate
    // ===================================================================
    /// Marks the scene as changed and requests dependent views to refresh.
    void update(const SwRectF& /*rect*/ = SwRectF()) { changed(); }
    /// Invalidates cached scene content and emits a generic change notification.
    void invalidate(const SwRectF& /*rect*/ = SwRectF(), int /*layers*/ = 0x01 /*AllLayers*/) { changed(); }
    /// Runs the scene advance placeholder and emits a change notification.
    void advance() {
        // Two-phase advance like Qt: phase 0 then phase 1
        changed();
    }

    // ===================================================================
    // Render
    // ===================================================================
    /**
     * @brief Renders the scene into an arbitrary painter target.
     * @param painter Destination painter.
     * @param target Optional target rectangle in destination coordinates.
     * @param source Optional source rectangle in scene coordinates.
     * @param aspectRatioMode Reserved aspect ratio policy parameter.
     *
     * This inline version is currently a stub kept for API completeness.
     */
    void render(SwPainter* /*painter*/,
                const SwRectF& /*target*/ = SwRectF(),
                const SwRectF& /*source*/ = SwRectF(),
                int /*aspectRatioMode*/ = 0) {
        // Render scene to arbitrary painter (stub for now — full impl would clip and paint items)
    }

    // ===================================================================
    // Convenience helpers  (same as before)
    // ===================================================================
    /// Creates a rectangle item, applies the supplied pen and brush, adds it to the scene, and returns it.
    SwGraphicsRectItem* addRect(const SwRectF& rect, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsRectItem(rect);
        item->setPen(pen); item->setBrush(brush);
        addItem(item); return item;
    }
    /// Convenience overload that creates a rectangle item from coordinates and size.
    SwGraphicsRectItem* addRect(double x, double y, double w, double h,
                                const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        return addRect(SwRectF(x, y, w, h), pen, brush);
    }

    /// Creates an ellipse item, applies the supplied pen and brush, adds it to the scene, and returns it.
    SwGraphicsEllipseItem* addEllipse(const SwRectF& rect, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsEllipseItem(rect);
        item->setPen(pen); item->setBrush(brush);
        addItem(item); return item;
    }
    /// Convenience overload that creates an ellipse item from coordinates and size.
    SwGraphicsEllipseItem* addEllipse(double x, double y, double w, double h,
                                      const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        return addEllipse(SwRectF(x, y, w, h), pen, brush);
    }

    /// Creates a line item, applies the supplied pen, adds it to the scene, and returns it.
    SwGraphicsLineItem* addLine(const SwLineF& line, const SwPen& pen = SwPen()) {
        auto* item = new SwGraphicsLineItem(line);
        item->setPen(pen);
        addItem(item); return item;
    }
    /// Convenience overload that creates a line item from scalar endpoints.
    SwGraphicsLineItem* addLine(double x1, double y1, double x2, double y2, const SwPen& pen = SwPen()) {
        return addLine(SwLineF(x1, y1, x2, y2), pen);
    }

    /// Creates a text item, applies the supplied font, adds it to the scene, and returns it.
    SwGraphicsTextItem* addText(const SwString& text, const SwFont& font = SwFont()) {
        auto* item = new SwGraphicsTextItem(text);
        item->setFont(font);
        addItem(item); return item;
    }

    /// Creates a simple text item, applies the supplied font, adds it to the scene, and returns it.
    SwGraphicsSimpleTextItem* addSimpleText(const SwString& text, const SwFont& font = SwFont()) {
        auto* item = new SwGraphicsSimpleTextItem(text);
        item->setFont(font);
        addItem(item); return item;
    }

    /// Creates a pixmap item, adds it to the scene, and returns it.
    SwGraphicsPixmapItem* addPixmap(const SwPixmap& pixmap) {
        auto* item = new SwGraphicsPixmapItem(pixmap);
        addItem(item); return item;
    }

    /// Creates a path item, applies the supplied pen and brush, adds it to the scene, and returns it.
    SwGraphicsPathItem* addPath(const SwPainterPath& path, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsPathItem(path);
        item->setPen(pen); item->setBrush(brush);
        addItem(item); return item;
    }

    /// Creates a polygon item, applies the supplied pen and brush, adds it to the scene, and returns it.
    SwGraphicsPolygonItem* addPolygon(const SwPolygonF& polygon, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsPolygonItem(polygon);
        item->setPen(pen); item->setBrush(brush);
        addItem(item); return item;
    }

    /// Wraps a widget in a proxy item, adds it to the scene, and returns the proxy.
    SwGraphicsProxyWidget* addWidget(SwWidget* widget) {
        auto* item = new SwGraphicsProxyWidget(widget);
        addItem(item); return item;
    }

    // ===================================================================
    // Event dispatch  (called by SwGraphicsView)
    // ===================================================================
    /**
     * @brief Dispatches a mouse press that originated from an attached view.
     * @param scenePos Mouse position in scene coordinates.
     * @param button Button that triggered the event.
     * @param modifiers Keyboard modifier bitmask.
     */
    void mousePressEvent_(const SwPointF& scenePos, const SwPoint& screenPos,
                          SwMouseButton button, int modifiers) {
        SwGraphicsItem* target = itemAt(scenePos);

        // Handle selection
        if (!m_stickyFocus || target) {
            if (!(modifiers & SwKeyboardModifier::ControlModifier)) {
                // Clear selection unless ctrl is held
                bool hadSelection = false;
                for (auto* item : m_items) {
                    if (item && item->isSelected()) { item->setSelected(false); hadSelection = true; }
                }
                if (hadSelection) emit selectionChanged();
            }
        }

        if (target) {
            if (target->testFlag(SwGraphicsItem::ItemIsSelectable))
                target->setSelected(!target->isSelected());
            if (target->testFlag(SwGraphicsItem::ItemIsFocusable))
                setFocusItem(target, SwFocusEvent::MouseFocusReason);

            m_mouseGrabberItem = target;
            m_lastMouseScenePos = scenePos;
            m_lastMouseScreenPos = screenPos;

            SwGraphicsSceneMouseEvent ev(SwGraphicsSceneEventType::GraphicsSceneMousePress);
            ev.setScenePos(scenePos);
            ev.setPos(target->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setButton(button);
            ev.setModifiers(modifiers);
            ev.setButtonDownPos(button, target->mapFromScene(scenePos));
            ev.setButtonDownScenePos(button, scenePos);
            ev.setButtonDownScreenPos(button, screenPos);
            target->mousePressEvent(&ev);

            // Handle ItemIsMovable
            if (target->testFlag(SwGraphicsItem::ItemIsMovable))
                m_movingItem = target;
        }
    }

    /**
     * @brief Dispatches a mouse move that originated from an attached view.
     * @param scenePos Mouse position in scene coordinates.
     * @param buttons Currently pressed mouse buttons as a bitmask.
     * @param modifiers Keyboard modifier bitmask.
     */
    void mouseMoveEvent_(const SwPointF& scenePos, const SwPoint& screenPos,
                         int buttons, int modifiers) {
        // Hover tracking
        SwGraphicsItem* hoverTarget = itemAt(scenePos);
        if (hoverTarget != m_lastHoverItem) {
            if (m_lastHoverItem && m_lastHoverItem->acceptHoverEvents()) {
                m_lastHoverItem->setIsUnderMouse_(false);
                SwGraphicsSceneHoverEvent ev(SwGraphicsSceneEventType::GraphicsSceneHoverLeave);
                ev.setScenePos(scenePos);
                ev.setPos(m_lastHoverItem->mapFromScene(scenePos));
                ev.setScreenPos(screenPos);
                ev.setLastScenePos(m_lastMouseScenePos);
                ev.setLastPos(m_lastHoverItem->mapFromScene(m_lastMouseScenePos));
                ev.setLastScreenPos(m_lastMouseScreenPos);
                m_lastHoverItem->hoverLeaveEvent(&ev);
            }
            if (hoverTarget && hoverTarget->acceptHoverEvents()) {
                hoverTarget->setIsUnderMouse_(true);
                SwGraphicsSceneHoverEvent ev(SwGraphicsSceneEventType::GraphicsSceneHoverEnter);
                ev.setScenePos(scenePos);
                ev.setPos(hoverTarget->mapFromScene(scenePos));
                ev.setScreenPos(screenPos);
                ev.setLastScenePos(m_lastMouseScenePos);
                ev.setLastPos(hoverTarget->mapFromScene(m_lastMouseScenePos));
                ev.setLastScreenPos(m_lastMouseScreenPos);
                hoverTarget->hoverEnterEvent(&ev);
            }
            m_lastHoverItem = hoverTarget;
        } else if (hoverTarget && hoverTarget->acceptHoverEvents()) {
            SwGraphicsSceneHoverEvent ev(SwGraphicsSceneEventType::GraphicsSceneHoverMove);
            ev.setScenePos(scenePos);
            ev.setPos(hoverTarget->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setLastScenePos(m_lastMouseScenePos);
            ev.setLastPos(hoverTarget->mapFromScene(m_lastMouseScenePos));
            ev.setLastScreenPos(m_lastMouseScreenPos);
            hoverTarget->hoverMoveEvent(&ev);
        }

        // ItemIsMovable drag
        if (m_movingItem && (buttons & SwMouseButtons::LeftButton)) {
            double dx = scenePos.x - m_lastMouseScenePos.x;
            double dy = scenePos.y - m_lastMouseScenePos.y;
            m_movingItem->moveBy(dx, dy);
        }

        // Mouse grab target
        SwGraphicsItem* target = m_mouseGrabberItem ? m_mouseGrabberItem : hoverTarget;
        if (target) {
            SwGraphicsSceneMouseEvent ev(SwGraphicsSceneEventType::GraphicsSceneMouseMove);
            ev.setScenePos(scenePos);
            ev.setPos(target->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setLastScenePos(m_lastMouseScenePos);
            ev.setLastPos(target->mapFromScene(m_lastMouseScenePos));
            ev.setLastScreenPos(m_lastMouseScreenPos);
            ev.setButtons(buttons);
            ev.setModifiers(modifiers);
            target->mouseMoveEvent(&ev);
        }

        m_lastMouseScenePos = scenePos;
        m_lastMouseScreenPos = screenPos;
    }

    /**
     * @brief Dispatches a mouse release that originated from an attached view.
     * @param scenePos Mouse position in scene coordinates.
     * @param button Button that triggered the release.
     * @param modifiers Keyboard modifier bitmask.
     */
    void mouseReleaseEvent_(const SwPointF& scenePos, const SwPoint& screenPos,
                            SwMouseButton button, int modifiers) {
        m_movingItem = nullptr;
        SwGraphicsItem* target = m_mouseGrabberItem;
        m_mouseGrabberItem = nullptr;

        if (!target) target = itemAt(scenePos);
        if (target) {
            SwGraphicsSceneMouseEvent ev(SwGraphicsSceneEventType::GraphicsSceneMouseRelease);
            ev.setScenePos(scenePos);
            ev.setPos(target->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setButton(button);
            ev.setModifiers(modifiers);
            ev.setLastScenePos(m_lastMouseScenePos);
            ev.setLastPos(target->mapFromScene(m_lastMouseScenePos));
            ev.setLastScreenPos(m_lastMouseScreenPos);
            target->mouseReleaseEvent(&ev);
        }
    }

    /**
     * @brief Dispatches a mouse double-click that originated from an attached view.
     * @param scenePos Mouse position in scene coordinates.
     * @param button Button that triggered the event.
     * @param modifiers Keyboard modifier bitmask.
     */
    void mouseDoubleClickEvent_(const SwPointF& scenePos, const SwPoint& screenPos,
                                SwMouseButton button, int modifiers) {
        SwGraphicsItem* target = itemAt(scenePos);
        if (target) {
            SwGraphicsSceneMouseEvent ev(SwGraphicsSceneEventType::GraphicsSceneMouseDoubleClick);
            ev.setScenePos(scenePos);
            ev.setPos(target->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setButton(button);
            ev.setModifiers(modifiers);
            target->mouseDoubleClickEvent(&ev);
        }
    }

    /**
     * @brief Dispatches a wheel event that originated from an attached view.
     * @param scenePos Mouse position in scene coordinates.
     * @param delta Wheel delta value.
     * @param modifiers Keyboard modifier bitmask.
     */
    void wheelEvent_(const SwPointF& scenePos, const SwPoint& screenPos,
                     int delta, int modifiers) {
        SwGraphicsItem* target = itemAt(scenePos);
        if (target) {
            SwGraphicsSceneWheelEvent ev;
            ev.setScenePos(scenePos);
            ev.setPos(target->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setDelta(delta);
            ev.setModifiers(modifiers);
            target->wheelEvent(&ev);
        }
    }

    /// Forwards a key press to the keyboard grabber item or the focused item.
    void keyPressEvent_(KeyEvent* event) {
        if (m_keyboardGrabberItem) {
            m_keyboardGrabberItem->keyPressEvent(event);
        } else if (m_focusItem) {
            m_focusItem->keyPressEvent(event);
        }
    }

    /// Forwards a key release to the keyboard grabber item or the focused item.
    void keyReleaseEvent_(KeyEvent* event) {
        if (m_keyboardGrabberItem) {
            m_keyboardGrabberItem->keyReleaseEvent(event);
        } else if (m_focusItem) {
            m_focusItem->keyReleaseEvent(event);
        }
    }

    /**
     * @brief Dispatches a context-menu request that originated from an attached view.
     * @param scenePos Trigger position in scene coordinates.
     * @param modifiers Keyboard modifier bitmask.
     */
    void contextMenuEvent_(const SwPointF& scenePos, const SwPoint& screenPos, int modifiers) {
        SwGraphicsItem* target = itemAt(scenePos);
        if (target) {
            SwGraphicsSceneContextMenuEvent ev;
            ev.setScenePos(scenePos);
            ev.setPos(target->mapFromScene(scenePos));
            ev.setScreenPos(screenPos);
            ev.setModifiers(modifiers);
            target->contextMenuEvent(&ev);
        }
    }

    // ===================================================================
    // Grab management  (called by SwGraphicsItem)
    // ===================================================================
    /// Internal helper used by items to claim or release mouse grabbing.
    void setMouseGrabberItem_(SwGraphicsItem* item) { m_mouseGrabberItem = item; }
    /// Internal helper used by items to claim or release keyboard grabbing.
    void setKeyboardGrabberItem_(SwGraphicsItem* item) { m_keyboardGrabberItem = item; }
    /// Returns the item currently grabbing keyboard events, if any.
    SwGraphicsItem* keyboardGrabberItem() const { return m_keyboardGrabberItem; }

signals:
    DECLARE_SIGNAL_VOID(changed);                    ///< Emitted when scene content or scene-wide visual state changes.
    DECLARE_SIGNAL_VOID(selectionChanged);           ///< Emitted after the set of selected items changes.
    DECLARE_SIGNAL(sceneRectChanged, const SwRectF&); ///< Emitted when an explicit scene rectangle is assigned.
    DECLARE_SIGNAL_VOID(focusItemChanged);           ///< Emitted after focus moves to another scene item or is cleared.

private:
    void sortItemsByZ_(std::vector<SwGraphicsItem*>& v, int order) const {
        if (order == 1) // descending
            std::stable_sort(v.begin(), v.end(), [](SwGraphicsItem* a, SwGraphicsItem* b) { return a->zValue() > b->zValue(); });
        else
            std::stable_sort(v.begin(), v.end(), [](SwGraphicsItem* a, SwGraphicsItem* b) { return a->zValue() < b->zValue(); });
    }

    SwRectF m_sceneRect{};
    std::vector<SwGraphicsItem*> m_items;
    std::vector<SwGraphicsView*> m_views;

    // Selection
    SwPainterPath m_selectionArea{};

    // Focus
    SwGraphicsItem* m_focusItem{nullptr};
    bool m_stickyFocus{false};

    // Mouse
    SwGraphicsItem* m_mouseGrabberItem{nullptr};
    SwGraphicsItem* m_keyboardGrabberItem{nullptr};
    SwGraphicsItem* m_movingItem{nullptr};
    SwGraphicsItem* m_lastHoverItem{nullptr};
    SwPointF m_lastMouseScenePos{};
    SwPoint m_lastMouseScreenPos{0, 0};

    // Background / foreground
    SwBrush m_backgroundBrush{};
    SwBrush m_foregroundBrush{};
    SwFont m_font{};

    // Index
    ItemIndexMethod m_indexMethod{NoIndex};
    int m_bspTreeDepth{0};
    double m_minimumRenderSize{0.0};
};

// ===================================================================
// SwGraphicsItem deferred implementations that need SwGraphicsScene
// ===================================================================
inline std::vector<SwGraphicsItem*> SwGraphicsItem::collidingItems(SwItemSelectionMode mode) const {
    std::vector<SwGraphicsItem*> result;
    if (!m_scene) return result;
    for (auto* other : m_scene->items()) {
        if (other == this || !other) continue;
        if (collidesWithItem(other, mode)) result.push_back(other);
    }
    return result;
}

inline void SwGraphicsItem::setFocus(SwFocusEvent::FocusReason reason) {
    if (m_scene) m_scene->setFocusItem(this, reason);
}

inline void SwGraphicsItem::clearFocus() {
    if (m_scene && m_scene->focusItem() == this) m_scene->setFocusItem(nullptr);
    m_hasFocus = false;
}

inline void SwGraphicsItem::grabMouse() {
    if (m_scene) m_scene->setMouseGrabberItem_(this);
}

inline void SwGraphicsItem::ungrabMouse() {
    if (m_scene && m_scene->mouseGrabberItem() == this) m_scene->setMouseGrabberItem_(nullptr);
}

inline void SwGraphicsItem::grabKeyboard() {
    if (m_scene) m_scene->setKeyboardGrabberItem_(this);
}

inline void SwGraphicsItem::ungrabKeyboard() {
    if (m_scene && m_scene->keyboardGrabberItem() == this) m_scene->setKeyboardGrabberItem_(nullptr);
}

inline void SwGraphicsItem::ensureVisible(const SwRectF& /*rect*/, int /*xmargin*/, int /*ymargin*/) {
    // Requires view — implemented in SwGraphicsView
}
