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

#include "graphics/SwGraphicsTypes.h"

#include <algorithm>
#include <functional>
#include <vector>

class SwPainter;
class SwGraphicsScene;
struct SwGraphicsRenderContext;

class SwGraphicsItem {
public:
    enum GraphicsItemFlag {
        ItemIsSelectable = 0x1,
        ItemIsMovable = 0x2,
        ItemClipsToShape = 0x4
    };

    SwGraphicsItem() = default;
    virtual ~SwGraphicsItem() {
        // Delete children (Qt-like ownership).
        for (SwGraphicsItem* child : m_children) {
            delete child;
        }
        m_children.clear();
    }

    SwGraphicsItem(const SwGraphicsItem&) = delete;
    SwGraphicsItem& operator=(const SwGraphicsItem&) = delete;

    void setPos(double x, double y) {
        if (m_pos.x == x && m_pos.y == y) {
            return;
        }
        m_pos.x = x;
        m_pos.y = y;
        notifyChanged_();
    }

    void setPos(const SwPointF& p) { setPos(p.x, p.y); }
    SwPointF pos() const { return m_pos; }

    SwPointF scenePos() const {
        SwPointF p = m_pos;
        const SwGraphicsItem* it = m_parent;
        while (it) {
            p.x += it->m_pos.x;
            p.y += it->m_pos.y;
            it = it->m_parent;
        }
        return p;
    }

    void setZValue(double z) {
        if (m_z == z) {
            return;
        }
        m_z = z;
        notifyChanged_();
    }
    double zValue() const { return m_z; }

    void setVisible(bool on) {
        if (m_visible == on) {
            return;
        }
        m_visible = on;
        notifyChanged_();
    }
    bool isVisible() const { return m_visible; }

    void setFlags(int flags) {
        if (m_flags == flags) {
            return;
        }
        m_flags = flags;
        notifyChanged_();
    }
    int flags() const { return m_flags; }
    bool testFlag(GraphicsItemFlag flag) const { return (m_flags & static_cast<int>(flag)) != 0; }

    void setSelected(bool on) {
        if (m_selected == on) {
            return;
        }
        m_selected = on;
        notifyChanged_();
    }
    bool isSelected() const { return m_selected; }

    SwGraphicsItem* parentItem() const { return m_parent; }

    void setParentItem(SwGraphicsItem* parent) {
        if (m_parent == parent) {
            return;
        }
        if (m_parent) {
            auto& siblings = m_parent->m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        }
        m_parent = parent;
        if (m_parent) {
            m_parent->m_children.push_back(this);
        }
        notifyChanged_();
    }

    const std::vector<SwGraphicsItem*>& childItems() const { return m_children; }

    SwGraphicsScene* scene() const { return m_scene; }

    // Geometry in local item coordinates.
    virtual SwRectF boundingRect() const = 0;

    // Geometry in scene coordinates (pos + local bounds).
    SwRectF sceneBoundingRect() const {
        SwRectF r = boundingRect();
        const SwPointF sp = scenePos();
        r.translate(sp.x, sp.y);
        return r;
    }

    virtual bool contains(const SwPointF& localPoint) const {
        return boundingRect().contains(localPoint);
    }

    bool containsScenePoint(const SwPointF& scenePoint) const {
        const SwPointF sp = scenePos();
        const SwPointF lp(scenePoint.x - sp.x, scenePoint.y - sp.y);
        return contains(lp);
    }

    // Paint the item. Mapping helpers are provided by the render context.
    virtual void paint(SwPainter* painter, const SwGraphicsRenderContext& ctx) = 0;

protected:
    friend class SwGraphicsScene;

    void setScene_(SwGraphicsScene* scene) { m_scene = scene; }
    void setChangeCallback_(std::function<void()> cb) { m_changeCallback = std::move(cb); }
    void update() { notifyChanged_(); }

private:
    void notifyChanged_() {
        if (m_changeCallback) {
            m_changeCallback();
        }
    }

    SwPointF m_pos{};
    double m_z{0.0};
    bool m_visible{true};
    int m_flags{0};
    bool m_selected{false};

    SwGraphicsItem* m_parent{nullptr};
    std::vector<SwGraphicsItem*> m_children;

    SwGraphicsScene* m_scene{nullptr};
    std::function<void()> m_changeCallback;
};
