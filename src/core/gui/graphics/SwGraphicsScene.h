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

#include "SwObject.h"

#include "graphics/SwGraphicsItems.h"
#include "graphics/SwGraphicsTypes.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

class SwGraphicsScene : public SwObject {
    SW_OBJECT(SwGraphicsScene, SwObject)

public:
    explicit SwGraphicsScene(SwObject* parent = nullptr)
        : SwObject(parent) {}

    ~SwGraphicsScene() override {
        // Delete only top-level items; children are owned by their parent items.
        for (SwGraphicsItem* item : m_items) {
            if (item && item->parentItem() == nullptr) {
                delete item;
            }
        }
        m_items.clear();
    }

    void setSceneRect(const SwRectF& rect) {
        m_sceneRect = rect;
        changed();
    }

    SwRectF sceneRect() const { return m_sceneRect; }

    const std::vector<SwGraphicsItem*>& items() const { return m_items; }

    void addItem(SwGraphicsItem* item) {
        if (!item) {
            return;
        }
        if (std::find(m_items.begin(), m_items.end(), item) != m_items.end()) {
            return;
        }
        item->setScene_(this);
        item->setChangeCallback_([this]() { changed(); });
        m_items.push_back(item);
        changed();
    }

    void removeItem(SwGraphicsItem* item) {
        if (!item) {
            return;
        }
        if (std::find(m_items.begin(), m_items.end(), item) == m_items.end()) {
            return;
        }
        // Remove the full subtree from the scene list to avoid dangling pointers when deleting a parent item.
        std::vector<SwGraphicsItem*> stack;
        stack.push_back(item);
        std::vector<SwGraphicsItem*> subtree;
        while (!stack.empty()) {
            SwGraphicsItem* current = stack.back();
            stack.pop_back();
            if (!current) {
                continue;
            }
            subtree.push_back(current);
            for (SwGraphicsItem* child : current->childItems()) {
                if (child) {
                    stack.push_back(child);
                }
            }
        }

        for (SwGraphicsItem* current : subtree) {
            auto it = std::find(m_items.begin(), m_items.end(), current);
            if (it != m_items.end()) {
                m_items.erase(it);
            }
            current->setScene_(nullptr);
            current->setChangeCallback_(std::function<void()>());
        }

        // Detach from parent before deletion so the parent does not keep a stale child pointer.
        item->setParentItem(nullptr);
        delete item;
        changed();
    }

    void clear() {
        for (SwGraphicsItem* item : m_items) {
            if (item && item->parentItem() == nullptr) {
                delete item;
            }
        }
        m_items.clear();
        changed();
    }

    // Qt-like convenience helpers.
    SwGraphicsRectItem* addRect(const SwRectF& rect, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsRectItem(rect);
        item->setPen(pen);
        item->setBrush(brush);
        addItem(item);
        return item;
    }

    SwGraphicsEllipseItem* addEllipse(const SwRectF& rect, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsEllipseItem(rect);
        item->setPen(pen);
        item->setBrush(brush);
        addItem(item);
        return item;
    }

    SwGraphicsLineItem* addLine(const SwLineF& line, const SwPen& pen = SwPen()) {
        auto* item = new SwGraphicsLineItem(line);
        item->setPen(pen);
        addItem(item);
        return item;
    }

    SwGraphicsTextItem* addText(const SwString& text, const SwFont& font = SwFont()) {
        auto* item = new SwGraphicsTextItem(text);
        item->setFont(font);
        addItem(item);
        return item;
    }

    SwGraphicsPixmapItem* addPixmap(const SwPixmap& pixmap) {
        auto* item = new SwGraphicsPixmapItem(pixmap);
        addItem(item);
        return item;
    }

    SwGraphicsPathItem* addPath(const SwPainterPath& path, const SwPen& pen = SwPen(), const SwBrush& brush = SwBrush()) {
        auto* item = new SwGraphicsPathItem(path);
        item->setPen(pen);
        item->setBrush(brush);
        addItem(item);
        return item;
    }

    SwGraphicsProxyWidget* addWidget(SwWidget* widget) {
        auto* item = new SwGraphicsProxyWidget(widget);
        addItem(item);
        return item;
    }

signals:
    DECLARE_SIGNAL_VOID(changed);

private:
    SwRectF m_sceneRect{0.0, 0.0, 0.0, 0.0};
    std::vector<SwGraphicsItem*> m_items;
};
