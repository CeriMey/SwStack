#pragma once

/**
 * @file src/core/gui/graphics/SwGraphicsItem.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwGraphicsItem in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the graphics item interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwItemSelectionMode, SwGraphicsItemChange,
 * SwGraphicsCacheMode, SwGraphicsPanelModality, and SwGraphicsItem.
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
 * SwGraphicsItem  —  Qt-like QGraphicsItem with full API coverage.
 *
 * - Transforms (rotation, per-item scale, arbitrary SwTransform, origin point)
 * - Event handlers (mouse, hover, key, wheel, drag-drop, focus, context menu)
 * - Coordinate mapping (mapToScene, mapFromScene, mapToItem, mapFromItem, …)
 * - Shape / collision detection
 * - Opacity, cursor, tooltip, enabled, focus, grab
 * - Custom data (key → variant via SwAny)
 * - Item change notifications
 **************************************************************************************************/

/**
 * @file
 * @brief Declares the base item type used by the retained-mode graphics scene.
 *
 * SwGraphicsItem is the central abstraction behind the graphics-view subsystem.
 * It combines hierarchy management, geometry mapping, visibility, selection,
 * transforms, hit testing, and virtual paint or event hooks in a single base
 * class that concrete scene items can derive from.
 *
 * The goal is to let higher-level code build interactive scene graphs without
 * coupling item implementations to a specific platform widget backend.
 */

#include "graphics/SwGraphicsTypes.h"
#include "graphics/SwGraphicsSceneEvent.h"
#include "SwString.h"
#include "SwWidget.h"
#include "Sw.h"

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

class SwPainter;
class SwPainterPath;
class SwGraphicsScene;
class SwGraphicsItemGroup;
struct SwGraphicsRenderContext;

// ---------------------------------------------------------------------------
// Forward-declare SwAny-like variant for data()  (if SwAny.h not included)
// ---------------------------------------------------------------------------
#ifndef SW_ANY_FORWARD_DECLARED
#define SW_ANY_FORWARD_DECLARED
class SwAny;
#endif

// ---------------------------------------------------------------------------
// Item selection / collision mode  (matches Qt::ItemSelectionMode)
// ---------------------------------------------------------------------------
enum class SwItemSelectionMode {
    IntersectsItemShape,
    ContainsItemShape,
    IntersectsItemBoundingRect,
    ContainsItemBoundingRect
};

// ---------------------------------------------------------------------------
// GraphicsItemChange  (matches QGraphicsItem::GraphicsItemChange)
// ---------------------------------------------------------------------------
enum class SwGraphicsItemChange {
    ItemPositionChange,
    ItemPositionHasChanged,
    ItemTransformChange,
    ItemTransformHasChanged,
    ItemRotationChange,
    ItemRotationHasChanged,
    ItemScaleChange,
    ItemScaleHasChanged,
    ItemTransformOriginPointChange,
    ItemTransformOriginPointHasChanged,
    ItemSelectedChange,
    ItemSelectedHasChanged,
    ItemVisibleChange,
    ItemVisibleHasChanged,
    ItemEnabledChange,
    ItemEnabledHasChanged,
    ItemParentChange,
    ItemParentHasChanged,
    ItemChildAddedChange,
    ItemChildRemovedChange,
    ItemSceneChange,
    ItemSceneHasChanged,
    ItemCursorChange,
    ItemCursorHasChanged,
    ItemToolTipChange,
    ItemToolTipHasChanged,
    ItemFlagsChange,
    ItemFlagsHasChanged,
    ItemZValueChange,
    ItemZValueHasChanged,
    ItemOpacityChange,
    ItemOpacityHasChanged,
    ItemScenePositionHasChanged
};

// ---------------------------------------------------------------------------
// CacheMode  (matches QGraphicsItem::CacheMode)
// ---------------------------------------------------------------------------
enum class SwGraphicsCacheMode {
    NoCache,
    ItemCoordinateCache,
    DeviceCoordinateCache
};

// ---------------------------------------------------------------------------
// PanelModality
// ---------------------------------------------------------------------------
enum class SwGraphicsPanelModality {
    NonModal,
    PanelModal,
    SceneModal
};

// ---------------------------------------------------------------------------
// SwGraphicsItem
// ---------------------------------------------------------------------------
/**
 * @brief Base class for all drawable and interactive objects stored in a scene.
 */
class SwGraphicsItem {
public:
    // --- Flags (matches QGraphicsItem::GraphicsItemFlag) ---
    enum GraphicsItemFlag {
        ItemIsMovable                        = 0x1,
        ItemIsSelectable                     = 0x2,
        ItemIsFocusable                      = 0x4,
        ItemClipsToShape                     = 0x8,
        ItemClipsChildrenToShape             = 0x10,
        ItemIgnoresTransformations           = 0x20,
        ItemIgnoresParentOpacity             = 0x40,
        ItemDoesntPropagateOpacityToChildren = 0x80,
        ItemStacksBehindParent               = 0x100,
        ItemUsesExtendedStyleOption          = 0x200,
        ItemHasNoContents                    = 0x400,
        ItemSendsGeometryChanges             = 0x800,
        ItemAcceptsInputMethod               = 0x1000,
        ItemNegativeZStacksBehindParent      = 0x2000,
        ItemIsPanel                          = 0x4000,
        ItemSendsScenePositionChanges        = 0x8000,
        ItemContainsChildrenInShape          = 0x10000
    };

    // --- Type ---
    enum { Type = 1 };
    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual int type() const { return Type; }

    // ===================================================================
    // Construction / destruction
    // ===================================================================
    /**
     * @brief Constructs a `SwGraphicsItem` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwGraphicsItem(SwGraphicsItem* parent = nullptr)
        : m_parent(nullptr) {
        if (parent) setParentItem(parent);
    }

    /**
     * @brief Destroys the `SwGraphicsItem` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwGraphicsItem() {
        // Remove from group
        setGroup(nullptr);
        // Detach from parent
        if (m_parent) {
            auto& siblings = m_parent->m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        }
        // Delete children
        auto children = m_children; // copy
        for (SwGraphicsItem* child : children) delete child;
        m_children.clear();
    }

    /**
     * @brief Constructs a `SwGraphicsItem` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGraphicsItem(const SwGraphicsItem&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwGraphicsItem& operator=(const SwGraphicsItem&) = delete;

    // ===================================================================
    // Position
    // ===================================================================
    /**
     * @brief Sets the pos.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(double x, double y) {
        if (m_pos.x == x && m_pos.y == y) return;
        m_pos = {x, y};
        notifyChanged_();
    }
    /**
     * @brief Sets the pos.
     * @param y Vertical coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(const SwPointF& p) { setPos(p.x, p.y); }
    /**
     * @brief Returns the current pos.
     * @return The current pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF pos() const { return m_pos; }
    /**
     * @brief Returns the current x.
     * @return The current x.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double x() const { return m_pos.x; }
    /**
     * @brief Returns the current y.
     * @return The current y.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double y() const { return m_pos.y; }
    /**
     * @brief Sets the x.
     * @param y Vertical coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setX(double v) { setPos(v, m_pos.y); }
    /**
     * @brief Sets the y.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setY(double v) { setPos(m_pos.x, v); }

    /**
     * @brief Performs the `moveBy` operation.
     * @param dx Value passed to the method.
     * @param dy Value passed to the method.
     */
    void moveBy(double dx, double dy) { setPos(m_pos.x + dx, m_pos.y + dy); }

    /**
     * @brief Returns the current scene Pos.
     * @return The current scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF scenePos() const {
        return mapToScene(SwPointF(0.0, 0.0));
    }

    // ===================================================================
    // Z ordering
    // ===================================================================
    /**
     * @brief Sets the zValue.
     * @param m_z Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setZValue(double z) { if (m_z == z) return; m_z = z; notifyChanged_(); }
    /**
     * @brief Returns the current z Value.
     * @return The current z Value.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double zValue() const { return m_z; }

    /**
     * @brief Performs the `stackBefore` operation.
     * @param sibling Value passed to the method.
     */
    void stackBefore(const SwGraphicsItem* sibling) {
        if (!m_parent || !sibling || sibling->m_parent != m_parent) return;
        auto& v = m_parent->m_children;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
        auto it = std::find(v.begin(), v.end(), sibling);
        v.insert(it, this);
    }

    // ===================================================================
    // Visibility
    // ===================================================================
    /**
     * @brief Sets the visible.
     * @param m_visible Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setVisible(bool on) { if (m_visible == on) return; m_visible = on; notifyChanged_(); }
    /**
     * @brief Returns whether the object reports visible.
     * @return `true` when the object reports visible; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isVisible() const { return m_visible; }
    /**
     * @brief Performs the `show` operation.
     * @param true Value passed to the method.
     */
    void show() { setVisible(true); }
    /**
     * @brief Performs the `hide` operation.
     * @param false Value passed to the method.
     */
    void hide() { setVisible(false); }

    // ===================================================================
    // Enabled
    // ===================================================================
    /**
     * @brief Sets the enabled.
     * @param m_enabled Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEnabled(bool on) { m_enabled = on; notifyChanged_(); }
    /**
     * @brief Returns whether the object reports enabled.
     * @return `true` when the object reports enabled; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEnabled() const { return m_enabled; }

    // ===================================================================
    // Selection
    // ===================================================================
    /**
     * @brief Sets the selected.
     * @param m_selected Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSelected(bool on) { if (m_selected == on) return; m_selected = on; notifyChanged_(); }
    /**
     * @brief Returns whether the object reports selected.
     * @return `true` when the object reports selected; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isSelected() const { return m_selected; }

    // ===================================================================
    // Flags
    // ===================================================================
    /**
     * @brief Sets the flags.
     * @param m_flags Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFlags(int flags) { if (m_flags == flags) return; m_flags = flags; notifyChanged_(); }
    /**
     * @brief Returns the current flags.
     * @return The current flags.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int flags() const { return m_flags; }
    /**
     * @brief Sets the flag.
     * @param flag Value passed to the method.
     * @param enabled Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFlag(GraphicsItemFlag flag, bool enabled = true) {
        if (enabled) m_flags |= static_cast<int>(flag);
        else m_flags &= ~static_cast<int>(flag);
        notifyChanged_();
    }
    /**
     * @brief Performs the `testFlag` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool testFlag(GraphicsItemFlag flag) const { return (m_flags & static_cast<int>(flag)) != 0; }

    // ===================================================================
    // Opacity
    // ===================================================================
    /**
     * @brief Sets the opacity.
     * @param m_opacity Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOpacity(double opacity) { m_opacity = std::max(0.0, std::min(1.0, opacity)); notifyChanged_(); }
    /**
     * @brief Returns the current opacity.
     * @return The current opacity.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double opacity() const { return m_opacity; }
    /**
     * @brief Returns the current effective Opacity.
     * @return The current effective Opacity.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double effectiveOpacity() const {
        double o = m_opacity;
        if (m_parent && !(m_flags & ItemIgnoresParentOpacity))
            o *= m_parent->effectiveOpacity();
        return o;
    }

    // ===================================================================
    // Transform
    // ===================================================================
    /**
     * @brief Sets the rotation.
     * @param m_rotation Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRotation(double angle) { m_rotation = angle; notifyChanged_(); }
    /**
     * @brief Returns the current rotation.
     * @return The current rotation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double rotation() const { return m_rotation; }

    /**
     * @brief Sets the scale.
     * @param m_scaleFactor Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScale(double factor) { m_scaleFactor = factor; notifyChanged_(); }
    /**
     * @brief Returns the current scale.
     * @return The current scale.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double scale() const { return m_scaleFactor; }

    /**
     * @brief Sets the transform Origin Point.
     * @param ox Value passed to the method.
     * @param m_transformOrigin Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTransformOriginPoint(double ox, double oy) { m_transformOrigin = {ox, oy}; notifyChanged_(); }
    /**
     * @brief Sets the transform Origin Point.
     * @param y Vertical coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTransformOriginPoint(const SwPointF& origin) { setTransformOriginPoint(origin.x, origin.y); }
    /**
     * @brief Returns the current transform Origin Point.
     * @return The current transform Origin Point.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF transformOriginPoint() const { return m_transformOrigin; }

    /**
     * @brief Sets the transform.
     * @param t Value passed to the method.
     * @param combine Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTransform(const SwTransform& t, bool combine = false) {
        m_transform = combine ? (m_transform * t) : t;
        notifyChanged_();
    }
    /**
     * @brief Returns the current transform.
     * @return The current transform.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwTransform transform() const { return m_transform; }
    /**
     * @brief Resets the object to a baseline state.
     * @param m_rotation Value passed to the method.
     */
    void resetTransform() { m_transform.reset(); m_rotation = 0; m_scaleFactor = 1; notifyChanged_(); }

    /**
     * @brief Returns the current scene Transform.
     * @return The current scene Transform.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwTransform sceneTransform() const {
        SwTransform t = itemTransform_();
        if (m_parent) t = t * m_parent->sceneTransform();
        return t;
    }

    /**
     * @brief Performs the `deviceTransform` operation.
     * @param viewTransform Value passed to the method.
     * @return The requested device Transform.
     */
    SwTransform deviceTransform(const SwTransform& viewTransform) const {
        return sceneTransform() * viewTransform;
    }

    /**
     * @brief Performs the `itemTransform` operation.
     * @param other Value passed to the method.
     * @param ok Optional flag updated to report success.
     * @return The requested item Transform.
     */
    SwTransform itemTransform(const SwGraphicsItem* other, bool* ok = nullptr) const {
        if (!other) { if (ok) *ok = false; return {}; }
        if (ok) *ok = true;
        return sceneTransform() * other->sceneTransform().inverted();
    }

    // ===================================================================
    // Coordinate mapping
    // ===================================================================
    /**
     * @brief Performs the `mapToScene` operation.
     * @param point Point used by the operation.
     * @return The requested map To Scene.
     */
    SwPointF mapToScene(const SwPointF& point) const { return sceneTransform().map(point); }
    /**
     * @brief Performs the `mapToScene` operation.
     * @param x Horizontal coordinate.
     * @return The requested map To Scene.
     */
    SwPointF mapToScene(double x, double y) const { return mapToScene({x, y}); }
    /**
     * @brief Performs the `mapRectToScene` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map Rect To Scene.
     */
    SwRectF mapRectToScene(const SwRectF& rect) const { return sceneTransform().mapRect(rect); }
    /**
     * @brief Performs the `mapToScene` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map To Scene.
     */
    SwPolygonF mapToScene(const SwRectF& rect) const { return sceneTransform().mapToPolygon(rect); }
    /**
     * @brief Performs the `mapToScene` operation.
     * @param polygon Value passed to the method.
     * @return The requested map To Scene.
     */
    SwPolygonF mapToScene(const SwPolygonF& polygon) const { return sceneTransform().map(polygon); }

    /**
     * @brief Performs the `mapFromScene` operation.
     * @param point Point used by the operation.
     * @return The requested map From Scene.
     */
    SwPointF mapFromScene(const SwPointF& point) const { return sceneTransform().inverted().map(point); }
    /**
     * @brief Performs the `mapFromScene` operation.
     * @param x Horizontal coordinate.
     * @return The requested map From Scene.
     */
    SwPointF mapFromScene(double x, double y) const { return mapFromScene({x, y}); }
    /**
     * @brief Performs the `mapRectFromScene` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map Rect From Scene.
     */
    SwRectF mapRectFromScene(const SwRectF& rect) const { return sceneTransform().inverted().mapRect(rect); }
    /**
     * @brief Performs the `mapFromScene` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map From Scene.
     */
    SwPolygonF mapFromScene(const SwRectF& rect) const { return sceneTransform().inverted().mapToPolygon(rect); }
    /**
     * @brief Performs the `mapFromScene` operation.
     * @param polygon Value passed to the method.
     * @return The requested map From Scene.
     */
    SwPolygonF mapFromScene(const SwPolygonF& polygon) const { return sceneTransform().inverted().map(polygon); }

    /**
     * @brief Performs the `mapToParent` operation.
     * @param point Point used by the operation.
     * @return The requested map To Parent.
     */
    SwPointF mapToParent(const SwPointF& point) const { return itemTransform_().map(point); }
    /**
     * @brief Performs the `mapToParent` operation.
     * @param x Horizontal coordinate.
     * @return The requested map To Parent.
     */
    SwPointF mapToParent(double x, double y) const { return mapToParent({x, y}); }
    /**
     * @brief Performs the `mapRectToParent` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map Rect To Parent.
     */
    SwRectF mapRectToParent(const SwRectF& rect) const { return itemTransform_().mapRect(rect); }
    /**
     * @brief Performs the `mapToParent` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map To Parent.
     */
    SwPolygonF mapToParent(const SwRectF& rect) const { return itemTransform_().mapToPolygon(rect); }
    /**
     * @brief Performs the `mapToParent` operation.
     * @param polygon Value passed to the method.
     * @return The requested map To Parent.
     */
    SwPolygonF mapToParent(const SwPolygonF& polygon) const { return itemTransform_().map(polygon); }

    /**
     * @brief Performs the `mapFromParent` operation.
     * @param point Point used by the operation.
     * @return The requested map From Parent.
     */
    SwPointF mapFromParent(const SwPointF& point) const { return itemTransform_().inverted().map(point); }
    /**
     * @brief Performs the `mapFromParent` operation.
     * @param x Horizontal coordinate.
     * @return The requested map From Parent.
     */
    SwPointF mapFromParent(double x, double y) const { return mapFromParent({x, y}); }
    /**
     * @brief Performs the `mapRectFromParent` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map Rect From Parent.
     */
    SwRectF mapRectFromParent(const SwRectF& rect) const { return itemTransform_().inverted().mapRect(rect); }
    /**
     * @brief Performs the `mapFromParent` operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map From Parent.
     */
    SwPolygonF mapFromParent(const SwRectF& rect) const { return itemTransform_().inverted().mapToPolygon(rect); }
    /**
     * @brief Performs the `mapFromParent` operation.
     * @param polygon Value passed to the method.
     * @return The requested map From Parent.
     */
    SwPolygonF mapFromParent(const SwPolygonF& polygon) const { return itemTransform_().inverted().map(polygon); }

    /**
     * @brief Performs the `mapToItem` operation.
     * @param item Item affected by the operation.
     * @param point Point used by the operation.
     * @return The requested map To Item.
     */
    SwPointF mapToItem(const SwGraphicsItem* item, const SwPointF& point) const {
        if (!item) return mapToScene(point);
        return item->mapFromScene(mapToScene(point));
    }
    /**
     * @brief Performs the `mapToItem` operation.
     * @param item Item affected by the operation.
     * @param x Horizontal coordinate.
     * @return The requested map To Item.
     */
    SwPointF mapToItem(const SwGraphicsItem* item, double x, double y) const { return mapToItem(item, {x, y}); }
    /**
     * @brief Performs the `mapRectToItem` operation.
     * @param item Item affected by the operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map Rect To Item.
     */
    SwRectF mapRectToItem(const SwGraphicsItem* item, const SwRectF& rect) const {
        if (!item) return mapRectToScene(rect);
        return item->mapRectFromScene(mapRectToScene(rect));
    }
    /**
     * @brief Performs the `mapToItem` operation.
     * @param item Item affected by the operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map To Item.
     */
    SwPolygonF mapToItem(const SwGraphicsItem* item, const SwRectF& rect) const {
        if (!item) return mapToScene(rect);
        return item->mapFromScene(mapRectToScene(rect));
    }
    /**
     * @brief Performs the `mapToItem` operation.
     * @param item Item affected by the operation.
     * @param polygon Value passed to the method.
     * @return The requested map To Item.
     */
    SwPolygonF mapToItem(const SwGraphicsItem* item, const SwPolygonF& polygon) const {
        if (!item) return mapToScene(polygon);
        return item->mapFromScene(mapToScene(polygon));
    }

    /**
     * @brief Performs the `mapFromItem` operation.
     * @param item Item affected by the operation.
     * @param point Point used by the operation.
     * @return The requested map From Item.
     */
    SwPointF mapFromItem(const SwGraphicsItem* item, const SwPointF& point) const {
        if (!item) return mapFromScene(point);
        return mapFromScene(item->mapToScene(point));
    }
    /**
     * @brief Performs the `mapFromItem` operation.
     * @param item Item affected by the operation.
     * @param x Horizontal coordinate.
     * @return The requested map From Item.
     */
    SwPointF mapFromItem(const SwGraphicsItem* item, double x, double y) const { return mapFromItem(item, {x, y}); }
    /**
     * @brief Performs the `mapRectFromItem` operation.
     * @param item Item affected by the operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map Rect From Item.
     */
    SwRectF mapRectFromItem(const SwGraphicsItem* item, const SwRectF& rect) const {
        if (!item) return mapRectFromScene(rect);
        return mapRectFromScene(item->mapRectToScene(rect));
    }
    /**
     * @brief Performs the `mapFromItem` operation.
     * @param item Item affected by the operation.
     * @param rect Rectangle used by the operation.
     * @return The requested map From Item.
     */
    SwPolygonF mapFromItem(const SwGraphicsItem* item, const SwRectF& rect) const {
        if (!item) return mapFromScene(rect);
        return mapFromScene(item->mapRectToScene(rect));
    }
    /**
     * @brief Performs the `mapFromItem` operation.
     * @param item Item affected by the operation.
     * @param polygon Value passed to the method.
     * @return The requested map From Item.
     */
    SwPolygonF mapFromItem(const SwGraphicsItem* item, const SwPolygonF& polygon) const {
        if (!item) return mapFromScene(polygon);
        return mapFromScene(item->mapToScene(polygon));
    }

    // ===================================================================
    // Parent / children
    // ===================================================================
    /**
     * @brief Returns the current parent Item.
     * @return The current parent Item.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsItem* parentItem() const { return m_parent; }

    /**
     * @brief Sets the parent Item.
     * @param parent Optional parent object that owns this instance.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setParentItem(SwGraphicsItem* parent) {
        if (m_parent == parent) return;
        if (m_parent) {
            auto& siblings = m_parent->m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        }
        m_parent = parent;
        if (m_parent) m_parent->m_children.push_back(this);
        notifyChanged_();
    }

    /**
     * @brief Returns the current top Level Item.
     * @return The current top Level Item.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsItem* topLevelItem() const {
        SwGraphicsItem* top = const_cast<SwGraphicsItem*>(this);
        while (top->m_parent) top = top->m_parent;
        return top;
    }

    /**
     * @brief Returns the current child Items.
     * @return The current child Items.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const std::vector<SwGraphicsItem*>& childItems() const { return m_children; }

    // ===================================================================
    // Group
    // ===================================================================
    /**
     * @brief Returns the current group.
     * @return The current group.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsItemGroup* group() const { return m_group; }
    /**
     * @brief Sets the group.
     * @param group Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setGroup(SwGraphicsItemGroup* group);  // implemented after SwGraphicsItemGroup definition

    // ===================================================================
    // Scene
    // ===================================================================
    /**
     * @brief Returns the current scene.
     * @return The current scene.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsScene* scene() const { return m_scene; }

    // ===================================================================
    // Geometry (pure virtual)
    // ===================================================================
    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwRectF boundingRect() const = 0;

    /**
     * @brief Returns the current shape.
     * @return The current shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwPainterPath shape() const;  // implemented after SwPainterPath is available

    /**
     * @brief Performs the `contains` operation.
     * @param localPoint Value passed to the method.
     * @return The requested contains.
     */
    virtual bool contains(const SwPointF& localPoint) const {
        return boundingRect().contains(localPoint);
    }

    /**
     * @brief Returns whether the object reports obscured By.
     * @return The requested obscured By.
     *
     * @details This query does not modify the object state.
     */
    virtual bool isObscuredBy(const SwGraphicsItem* /*item*/) const { return false; }
    /**
     * @brief Returns whether the object reports obscured.
     * @return `true` when the object reports obscured; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isObscured(const SwRectF& /*rect*/ = SwRectF()) const { return false; }

    /**
     * @brief Returns the current scene Bounding Rect.
     * @return The current scene Bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF sceneBoundingRect() const {
        return sceneTransform().mapRect(boundingRect());
    }

    /**
     * @brief Returns the current children Bounding Rect.
     * @return The current children Bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF childrenBoundingRect() const {
        SwRectF r;
        for (const auto* child : m_children) {
            SwRectF cr = child->mapRectToParent(child->boundingRect().united(child->childrenBoundingRect()));
            r = r.isEmpty() ? cr : r.united(cr);
        }
        return r;
    }

    /**
     * @brief Performs the `containsScenePoint` operation.
     * @param scenePoint Value passed to the method.
     * @return `true` when the object reports contains Scene Point; otherwise `false`.
     */
    bool containsScenePoint(const SwPointF& scenePoint) const {
        return contains(mapFromScene(scenePoint));
    }

    // ===================================================================
    // Collision detection
    // ===================================================================
    /**
     * @brief Performs the `collidesWithItem` operation.
     * @param other Value passed to the method.
     * @param mode Mode value that controls the operation.
     * @return The requested collides With Item.
     */
    virtual bool collidesWithItem(const SwGraphicsItem* other,
                                 SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) const {
        if (!other) return false;
        switch (mode) {
        case SwItemSelectionMode::IntersectsItemBoundingRect:
        case SwItemSelectionMode::ContainsItemBoundingRect:
            return sceneBoundingRect().intersects(other->sceneBoundingRect());
        default:
            return sceneBoundingRect().intersects(other->sceneBoundingRect());
        }
    }

    /**
     * @brief Performs the `collidesWithPath` operation.
     * @param SwItemSelectionMode Value passed to the method.
     * @return The requested collides With Path.
     */
    virtual bool collidesWithPath(const SwPainterPath& /*path*/,
                                  SwItemSelectionMode /*mode*/ = SwItemSelectionMode::IntersectsItemShape) const {
        return false;  // requires SwPainterPath — implemented inline below
    }

    /**
     * @brief Performs the `collidingItems` operation.
     * @param mode Mode value that controls the operation.
     * @return The requested colliding Items.
     */
    std::vector<SwGraphicsItem*> collidingItems(
        SwItemSelectionMode mode = SwItemSelectionMode::IntersectsItemShape) const;  // needs scene

    /**
     * @brief Returns whether the object reports under Mouse.
     * @return `true` when the object reports under Mouse; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isUnderMouse() const { return m_isUnderMouse; }

    // ===================================================================
    // Cursor
    // ===================================================================
    /**
     * @brief Sets the cursor.
     * @param cursor Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCursor(CursorType cursor) { m_cursor = cursor; m_hasCursor = true; }
    /**
     * @brief Returns the current cursor.
     * @return The current cursor.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    CursorType cursor() const { return m_cursor; }
    /**
     * @brief Returns whether the object reports cursor.
     * @return `true` when the object reports cursor; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasCursor() const { return m_hasCursor; }
    /**
     * @brief Performs the `unsetCursor` operation.
     */
    void unsetCursor() { m_cursor = CursorType::Arrow; m_hasCursor = false; }

    // ===================================================================
    // Tooltip
    // ===================================================================
    /**
     * @brief Sets the tool Tip.
     * @param tip Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setToolTip(const SwString& tip) { m_toolTip = tip; }
    /**
     * @brief Returns the current tool Tip.
     * @return The current tool Tip.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toolTip() const { return m_toolTip; }

    // ===================================================================
    // Focus
    // ===================================================================
    /**
     * @brief Returns whether the object reports focus.
     * @return `true` when the object reports focus; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasFocus() const { return m_hasFocus; }
    /**
     * @brief Sets the focus.
     * @param reason Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFocus(SwFocusEvent::FocusReason reason = SwFocusEvent::OtherFocusReason);  // needs scene
    /**
     * @brief Clears the current object state.
     */
    void clearFocus();  // needs scene

    // ===================================================================
    // Hover
    // ===================================================================
    /**
     * @brief Sets the accept Hover Events.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAcceptHoverEvents(bool on) { m_acceptHover = on; }
    /**
     * @brief Returns the current accept Hover Events.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool acceptHoverEvents() const { return m_acceptHover; }

    // ===================================================================
    // Drop
    // ===================================================================
    /**
     * @brief Sets the accept Drops.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAcceptDrops(bool on) { m_acceptDrops = on; }
    /**
     * @brief Returns the current accept Drops.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool acceptDrops() const { return m_acceptDrops; }

    // ===================================================================
    // Grab
    // ===================================================================
    /**
     * @brief Performs the `grabMouse` operation.
     */
    void grabMouse();    // needs scene
    /**
     * @brief Performs the `ungrabMouse` operation.
     */
    void ungrabMouse();  // needs scene
    /**
     * @brief Performs the `grabKeyboard` operation.
     */
    void grabKeyboard();    // needs scene
    /**
     * @brief Performs the `ungrabKeyboard` operation.
     */
    void ungrabKeyboard();  // needs scene

    // ===================================================================
    // Touch / input method  (stubs for API completeness)
    // ===================================================================
    /**
     * @brief Sets the accept Touch Events.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAcceptTouchEvents(bool on) { m_acceptTouch = on; }
    /**
     * @brief Returns the current accept Touch Events.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool acceptTouchEvents() const { return m_acceptTouch; }
    /**
     * @brief Sets the filters Child Events.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFiltersChildEvents(bool on) { m_filtersChildEvents = on; }
    /**
     * @brief Returns the current filters Child Events.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool filtersChildEvents() const { return m_filtersChildEvents; }

    // ===================================================================
    // Panel / modality
    // ===================================================================
    /**
     * @brief Returns whether the object reports panel.
     * @param ItemIsPanel Value passed to the method.
     * @return `true` when the object reports panel; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isPanel() const { return testFlag(ItemIsPanel); }
    /**
     * @brief Returns the current panel Modality.
     * @return The current panel Modality.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsPanelModality panelModality() const { return m_panelModality; }
    /**
     * @brief Sets the panel Modality.
     * @param modality Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPanelModality(SwGraphicsPanelModality modality) { m_panelModality = modality; }
    /**
     * @brief Returns whether the object reports blocked By Modal Panel.
     * @return `true` when the object reports blocked By Modal Panel; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isBlockedByModalPanel(SwGraphicsItem** /*blockingPanel*/ = nullptr) const { return false; }

    /**
     * @brief Returns whether the object reports active.
     * @return `true` when the object reports active; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isActive() const { return m_active; }
    /**
     * @brief Sets the active.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setActive(bool on) { m_active = on; }

    /**
     * @brief Returns the current panel.
     * @return The current panel.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsItem* panel() const {
        const SwGraphicsItem* item = this;
        while (item) { if (item->isPanel()) return const_cast<SwGraphicsItem*>(item); item = item->m_parent; }
        return nullptr;
    }

    /**
     * @brief Performs the `window` operation.
     * @return The requested window.
     */
    SwGraphicsItem* window() const { return panel(); }

    /**
     * @brief Returns the current focus Proxy.
     * @return The current focus Proxy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsItem* focusProxy() const { return m_focusProxy; }
    /**
     * @brief Sets the focus Proxy.
     * @param item Item affected by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFocusProxy(SwGraphicsItem* item) { m_focusProxy = item; }
    /**
     * @brief Returns the current focus Item.
     * @return The current focus Item.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsItem* focusItem() const {
        if (m_focusProxy) return m_focusProxy->focusItem();
        return m_hasFocus ? const_cast<SwGraphicsItem*>(this) : nullptr;
    }

    // ===================================================================
    // Cache mode
    // ===================================================================
    /**
     * @brief Returns the current cache Mode.
     * @return The current cache Mode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsCacheMode cacheMode() const { return m_cacheMode; }
    /**
     * @brief Sets the cache Mode.
     * @param mode Mode value that controls the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCacheMode(SwGraphicsCacheMode mode, const SwSize& /*logicalCacheSize*/ = {0, 0}) { m_cacheMode = mode; }

    // ===================================================================
    // Custom data
    // ===================================================================
    /**
     * @brief Sets the data.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setData(int key, const SwString& value) { m_data[key] = value; }
    /**
     * @brief Performs the `data` operation.
     * @param key Value passed to the method.
     * @return The requested data.
     */
    SwString data(int key) const {
        auto it = m_data.find(key);
        return it != m_data.end() ? it->second : SwString();
    }

    // ===================================================================
    // Update / geometry change
    // ===================================================================
    /**
     * @brief Updates the update managed by the object.
     */
    void update(const SwRectF& /*rect*/ = SwRectF()) { notifyChanged_(); }
    /**
     * @brief Performs the `prepareGeometryChange` operation.
     */
    void prepareGeometryChange() { notifyChanged_(); }

    // ===================================================================
    // Scene event filter
    // ===================================================================
    /**
     * @brief Performs the `installSceneEventFilter` operation.
     * @param filterItem Value passed to the method.
     */
    void installSceneEventFilter(SwGraphicsItem* filterItem) {
        if (filterItem && filterItem != this) m_sceneEventFilters.push_back(filterItem);
    }
    /**
     * @brief Removes the specified scene Event Filter.
     * @param filterItem Value passed to the method.
     */
    void removeSceneEventFilter(SwGraphicsItem* filterItem) {
        m_sceneEventFilters.erase(
            std::remove(m_sceneEventFilters.begin(), m_sceneEventFilters.end(), filterItem),
            m_sceneEventFilters.end());
    }

    // ===================================================================
    // Painting (pure virtual)
    // ===================================================================
    /**
     * @brief Performs the `paint` operation.
     * @param painter Value passed to the method.
     * @param ctx Value passed to the method.
     * @return The requested paint.
     */
    virtual void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) = 0;

    // ===================================================================
    // ensureVisible
    // ===================================================================
    /**
     * @brief Performs the `ensureVisible` operation.
     * @param rect Rectangle used by the operation.
     * @param xmargin Value passed to the method.
     * @param ymargin Value passed to the method.
     */
    void ensureVisible(const SwRectF& rect = SwRectF(), int xmargin = 50, int ymargin = 50);  // needs scene/view

protected:
    friend class SwGraphicsScene;
    friend class SwGraphicsView;
    friend class SwGraphicsItemGroup;

    // --- Internal setters used by SwGraphicsScene ---
    /**
     * @brief Sets the scene.
     * @param scene Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScene_(SwGraphicsScene* scene) { m_scene = scene; }
    /**
     * @brief Sets the change Callback.
     * @param m_changeCallback Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setChangeCallback_(std::function<void()> cb) { m_changeCallback = std::move(cb); }
    /**
     * @brief Sets the is Under Mouse.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIsUnderMouse_(bool on) { m_isUnderMouse = on; }
    /**
     * @brief Sets the has Focus.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHasFocus_(bool on) { m_hasFocus = on; }

    // --- Event handlers (virtual, override in subclasses) ---
    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @return The requested mouse Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mousePressEvent(SwGraphicsSceneMouseEvent* event) { event->ignore(); }
    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @return The requested mouse Move Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseMoveEvent(SwGraphicsSceneMouseEvent* event) { event->ignore(); }
    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @return The requested mouse Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseReleaseEvent(SwGraphicsSceneMouseEvent* event) { event->ignore(); }
    /**
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Double Click Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseDoubleClickEvent(SwGraphicsSceneMouseEvent* event) {
        mousePressEvent(event);
    }

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @return The requested wheel Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void wheelEvent(SwGraphicsSceneWheelEvent* event) { event->ignore(); }

    /**
     * @brief Handles the hover Enter Event forwarded by the framework.
     * @return The requested hover Enter Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void hoverEnterEvent(SwGraphicsSceneHoverEvent* event) { event->accept(); }
    /**
     * @brief Handles the hover Move Event forwarded by the framework.
     * @return The requested hover Move Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void hoverMoveEvent(SwGraphicsSceneHoverEvent* event) { event->accept(); }
    /**
     * @brief Handles the hover Leave Event forwarded by the framework.
     * @return The requested hover Leave Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void hoverLeaveEvent(SwGraphicsSceneHoverEvent* event) { event->accept(); }

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @return The requested key Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void keyPressEvent(KeyEvent* event) { event->ignore(); }
    virtual void keyPressEvent(SwGraphicsSceneKeyEvent* event) {
        if (!event) {
            return;
        }
        KeyEvent keyEvent(event->key(),
                          event->isCtrlPressed(),
                          event->isShiftPressed(),
                          event->isAltPressed(),
                          event->text(),
                          event->isTextProvided());
        if (event->isAccepted()) {
            keyEvent.accept();
        }
        keyPressEvent(&keyEvent);
        event->setAccepted(keyEvent.isAccepted());
    }
    /**
     * @brief Handles the key Release Event forwarded by the framework.
     * @return The requested key Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void keyReleaseEvent(KeyEvent* event) { event->ignore(); }
    virtual void keyReleaseEvent(SwGraphicsSceneKeyEvent* event) {
        if (!event) {
            return;
        }
        KeyEvent keyEvent(event->key(),
                          event->isCtrlPressed(),
                          event->isShiftPressed(),
                          event->isAltPressed(),
                          event->text(),
                          event->isTextProvided(),
                          EventType::KeyReleaseEvent);
        if (event->isAccepted()) {
            keyEvent.accept();
        }
        keyReleaseEvent(&keyEvent);
        event->setAccepted(keyEvent.isAccepted());
    }

    /**
     * @brief Handles the focus In Event forwarded by the framework.
     * @return The requested focus In Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void focusInEvent(SwFocusEvent* event) { event->accept(); }
    /**
     * @brief Handles the focus Out Event forwarded by the framework.
     * @return The requested focus Out Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void focusOutEvent(SwFocusEvent* event) { event->accept(); }

    /**
     * @brief Handles the context Menu Event forwarded by the framework.
     * @return The requested context Menu Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void contextMenuEvent(SwGraphicsSceneContextMenuEvent* event) { event->ignore(); }

    /**
     * @brief Handles the drag Enter Event forwarded by the framework.
     * @return The requested drag Enter Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void dragEnterEvent(SwGraphicsSceneDragDropEvent* event) { event->ignore(); }
    /**
     * @brief Handles the drag Move Event forwarded by the framework.
     * @return The requested drag Move Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void dragMoveEvent(SwGraphicsSceneDragDropEvent* event) { event->ignore(); }
    /**
     * @brief Handles the drag Leave Event forwarded by the framework.
     * @return The requested drag Leave Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void dragLeaveEvent(SwGraphicsSceneDragDropEvent* event) { event->ignore(); }
    /**
     * @brief Handles the drop Event forwarded by the framework.
     * @return The requested drop Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void dropEvent(SwGraphicsSceneDragDropEvent* event) { event->ignore(); }

    /**
     * @brief Performs the `sceneEventFilter` operation.
     * @return The requested scene Event Filter.
     */
    virtual bool sceneEventFilter(SwGraphicsItem* /*watched*/, SwGraphicsSceneEvent* /*event*/) { return false; }
    /**
     * @brief Handles the scene Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested scene Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual bool sceneEvent(SwGraphicsSceneEvent* event) {
        // Default dispatcher — subclasses may override for custom routing.
        switch (event->type()) {
        case SwGraphicsSceneEventType::GraphicsSceneMousePress:
            mousePressEvent(static_cast<SwGraphicsSceneMouseEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneMouseMove:
            mouseMoveEvent(static_cast<SwGraphicsSceneMouseEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneMouseRelease:
            mouseReleaseEvent(static_cast<SwGraphicsSceneMouseEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneMouseDoubleClick:
            mouseDoubleClickEvent(static_cast<SwGraphicsSceneMouseEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneKeyPress:
            keyPressEvent(static_cast<SwGraphicsSceneKeyEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneKeyRelease:
            keyReleaseEvent(static_cast<SwGraphicsSceneKeyEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneWheel:
            wheelEvent(static_cast<SwGraphicsSceneWheelEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneHoverEnter:
            hoverEnterEvent(static_cast<SwGraphicsSceneHoverEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneHoverMove:
            hoverMoveEvent(static_cast<SwGraphicsSceneHoverEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneHoverLeave:
            hoverLeaveEvent(static_cast<SwGraphicsSceneHoverEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneContextMenu:
            contextMenuEvent(static_cast<SwGraphicsSceneContextMenuEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneDragEnter:
            dragEnterEvent(static_cast<SwGraphicsSceneDragDropEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneDragMove:
            dragMoveEvent(static_cast<SwGraphicsSceneDragDropEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneDragLeave:
            dragLeaveEvent(static_cast<SwGraphicsSceneDragDropEvent*>(event)); break;
        case SwGraphicsSceneEventType::GraphicsSceneDrop:
            dropEvent(static_cast<SwGraphicsSceneDragDropEvent*>(event)); break;
        case SwGraphicsSceneEventType::FocusIn:
            focusInEvent(static_cast<SwFocusEvent*>(event)); break;
        case SwGraphicsSceneEventType::FocusOut:
            focusOutEvent(static_cast<SwFocusEvent*>(event)); break;
        default: break;
        }
        return event->isAccepted();
    }

    // --- Item change notification (override in subclass) ---
    /**
     * @brief Performs the `itemChange` operation.
     * @param SwGraphicsItemChange Value passed to the method.
     * @return The requested item Change.
     */
    virtual void itemChange(SwGraphicsItemChange /*change*/, const SwPointF& /*value*/) {}

private:
    void notifyChanged_() { if (m_changeCallback) m_changeCallback(); }

    SwTransform itemTransform_() const {
        SwTransform t;
        if (m_rotation != 0 || m_scaleFactor != 1) {
            t.translate(m_transformOrigin.x, m_transformOrigin.y);
            if (m_rotation != 0) t.rotate(m_rotation);
            if (m_scaleFactor != 1) t.scale(m_scaleFactor, m_scaleFactor);
            t.translate(-m_transformOrigin.x, -m_transformOrigin.y);
        }
        t *= m_transform;
        t.translate(m_pos.x, m_pos.y);
        return t;
    }

    // Position / transform
    SwPointF m_pos{};
    double m_z{0.0};
    double m_rotation{0.0};
    double m_scaleFactor{1.0};
    SwPointF m_transformOrigin{};
    SwTransform m_transform{};

    // State
    bool m_visible{true};
    bool m_enabled{true};
    bool m_selected{false};
    double m_opacity{1.0};
    int m_flags{0};

    // Cursor / tooltip
    CursorType m_cursor{CursorType::Arrow};
    bool m_hasCursor{false};
    SwString m_toolTip{};

    // Hover / drop / touch
    bool m_acceptHover{false};
    bool m_acceptDrops{false};
    bool m_acceptTouch{false};
    bool m_filtersChildEvents{false};

    // Focus / grab
    bool m_hasFocus{false};
    bool m_isUnderMouse{false};
    bool m_active{true};

    // Hierarchy
    SwGraphicsItem* m_parent{nullptr};
    std::vector<SwGraphicsItem*> m_children;
    SwGraphicsItemGroup* m_group{nullptr};
    SwGraphicsItem* m_focusProxy{nullptr};

    // Scene
    SwGraphicsScene* m_scene{nullptr};
    std::function<void()> m_changeCallback;

    // Cache
    SwGraphicsCacheMode m_cacheMode{SwGraphicsCacheMode::NoCache};
    SwGraphicsPanelModality m_panelModality{SwGraphicsPanelModality::NonModal};

    // Scene event filters
    std::vector<SwGraphicsItem*> m_sceneEventFilters;

    // Custom data
    std::map<int, SwString> m_data;
};
