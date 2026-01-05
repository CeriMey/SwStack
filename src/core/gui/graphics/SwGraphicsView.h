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

#include "SwWidget.h"

#include "graphics/SwGraphicsScene.h"
#include "graphics/SwGraphicsRenderContext.h"
#include "graphics/SwGraphicsTypes.h"

#include <algorithm>
#include <cmath>
#include <vector>

class SwGraphicsView : public SwWidget {
    SW_OBJECT(SwGraphicsView, SwWidget)

public:
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

    void setScene(SwGraphicsScene* scene) {
        if (m_scene == scene) {
            return;
        }
        if (m_scene) {
            SwObject::disconnect(m_scene, this);
        }
        m_scene = scene;
        if (m_scene) {
            SwObject::connect(m_scene, &SwGraphicsScene::changed, this, &SwGraphicsView::onSceneChanged_);
        }
        syncProxyWidgets_();
        update();
    }

    SwGraphicsScene* scene() const { return m_scene; }

    void setScale(double s) {
        m_scale = (s <= 0.0) ? 1.0 : s;
        syncProxyWidgets_();
        update();
    }

    double scale() const { return m_scale; }

    // Scene->view mapping helpers (scene space to global widget coordinates).
    SwPointF mapFromScene(const SwPointF& scenePoint) const {
        const SwRect vr = getRect();
        const double x = static_cast<double>(vr.x) + (scenePoint.x - m_scroll.x) * m_scale;
        const double y = static_cast<double>(vr.y) + (scenePoint.y - m_scroll.y) * m_scale;
        return SwPointF(x, y);
    }

    SwRect mapFromScene(const SwRectF& sceneRect) const {
        const SwPointF tl = mapFromScene(SwPointF(sceneRect.x, sceneRect.y));
        const SwPointF br = mapFromScene(SwPointF(sceneRect.x + sceneRect.width, sceneRect.y + sceneRect.height));
        const int x = static_cast<int>(std::lround(std::min(tl.x, br.x)));
        const int y = static_cast<int>(std::lround(std::min(tl.y, br.y)));
        const int w = static_cast<int>(std::lround(std::abs(br.x - tl.x)));
        const int h = static_cast<int>(std::lround(std::abs(br.y - tl.y)));
        return SwRect{x, y, w, h};
    }

    SwPointF mapToScene(const SwPointF& viewPoint) const {
        const SwRect vr = getRect();
        const double sx = ((viewPoint.x - static_cast<double>(vr.x)) / m_scale) + m_scroll.x;
        const double sy = ((viewPoint.y - static_cast<double>(vr.y)) / m_scale) + m_scroll.y;
        return SwPointF(sx, sy);
    }

    void setScroll(double sx, double sy) {
        m_scroll.x = sx;
        m_scroll.y = sy;
        syncProxyWidgets_();
        update();
    }

protected:
    void paintEvent(PaintEvent* event) override {
        if (!event || !event->painter() || !isVisibleInHierarchy()) {
            return;
        }
        syncProxyWidgets_();

        SwPainter* painter = event->painter();
        const SwRect rect = getRect();

        // Background (same logic as SwWidget, but without painting children first).
        SwColor bgColor = {255, 255, 255};
        bool paintBackground = true;
        StyleSheet* sheet = getToolSheet();
        std::vector<SwString> selectors = classHierarchy();
        bool hasSwWidgetSelector = false;
        for (const SwString& selector : selectors) {
            if (selector == "SwWidget") {
                hasSwWidgetSelector = true;
                break;
            }
        }
        if (!hasSwWidgetSelector) {
            selectors.emplace_back("SwWidget");
        }
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            SwString value;
            if (sheet) {
                value = sheet->getStyleProperty(selector.toStdString(), "background-color");
            }
            if (!value.isEmpty()) {
                float alpha = 1.0f;
                try {
                    SwColor resolved = sheet ? sheet->parseColor(value.toStdString(), &alpha) : bgColor;
                    if (alpha <= 0.0f) {
                        paintBackground = false;
                    } else {
                        bgColor = resolved;
                        paintBackground = true;
                    }
                } catch (...) {
                }
                break;
            }
        }
        if (paintBackground) {
            this->m_style->drawBackground(rect, painter, bgColor);
        }

        painter->pushClipRect(rect);

        // Draw scene content.
        if (m_scene) {
            SwGraphicsRenderContext ctx;
            ctx.viewRect = rect;
            ctx.scroll = m_scroll;
            ctx.scale = m_scale;

            std::vector<SwGraphicsItem*> visible;
            for (SwGraphicsItem* item : m_scene->items()) {
                if (item && item->isVisible()) {
                    visible.push_back(item);
                }
            }
            std::stable_sort(visible.begin(), visible.end(), [](SwGraphicsItem* a, SwGraphicsItem* b) {
                if (!a || !b) {
                    return a < b;
                }
                return a->zValue() < b->zValue();
            });

            for (SwGraphicsItem* item : visible) {
                if (!item) {
                    continue;
                }
                item->paint(painter, ctx);
            }
        }

        // Paint embedded widgets on top.
        const SwRect& paintRect = event->paintRect();
        for (SwObject* objChild : getChildren()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child) {
                continue;
            }
            if (!child->isVisibleInHierarchy()) {
                continue;
            }
            const SwRect childRect = child->getRect();
            if (rectsIntersect(paintRect, childRect)) {
                static_cast<SwWidgetInterface*>(child)->paintEvent(event);
            }
        }

        painter->popClipRect();
    }

    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        syncProxyWidgets_();
    }

private:
    void onSceneChanged_() {
        syncProxyWidgets_();
        update();
    }

    void syncProxyWidgets_() {
        auto deleteOrDetachProxyWidget = [&](SwWidget* widget) {
            if (!widget) {
                return;
            }
            if (widget->parent() != this) {
                return;
            }

            // By default, SwGraphicsView owns proxy widgets and deletes them once the corresponding
            // SwGraphicsProxyWidget disappears from the scene. When a widget is owned externally
            // (e.g. by a node model via RAII), it can opt out via a dynamic property.
            bool deleteOnRemove = true;
            static const SwString kDeleteOnRemoveProp("sw.graphics.proxy.deleteOnRemove");
            if (widget->propertyExist(kDeleteOnRemoveProp)) {
                deleteOnRemove = widget->property(kDeleteOnRemoveProp).toBool();
            }

            if (deleteOnRemove) {
                delete widget;
            } else {
                widget->setParent(nullptr);
            }
        };

        if (!m_scene) {
            for (SwWidget* w : m_proxyWidgets) {
                deleteOrDetachProxyWidget(w);
            }
            m_proxyWidgets.clear();
            return;
        }
        SwGraphicsRenderContext ctx;
        ctx.viewRect = getRect();
        ctx.scroll = m_scroll;
        ctx.scale = m_scale;

        std::vector<SwWidget*> liveWidgets;
        for (SwGraphicsItem* item : m_scene->items()) {
            auto* proxy = dynamic_cast<SwGraphicsProxyWidget*>(item);
            if (!proxy) {
                continue;
            }
            SwWidget* w = proxy->widget();
            if (!w) {
                continue;
            }
            liveWidgets.push_back(w);
            if (w->parent() != this) {
                w->setParent(this);
            }

            const SwPointF sp = proxy->scenePos();
            const SwPointF vp = ctx.mapFromScene(sp);

            SwSize base = proxy->widgetBaseSize();
            int ww = base.width > 0 ? base.width : w->width();
            int wh = base.height > 0 ? base.height : w->height();
            if (ww <= 0 || wh <= 0) {
                ww = 160;
                wh = 40;
            }

            // Apply view scale to the widget geometry (best-effort).
            const int scaledW = std::max(1, static_cast<int>(std::lround(static_cast<double>(ww) * m_scale)));
            const int scaledH = std::max(1, static_cast<int>(std::lround(static_cast<double>(wh) * m_scale)));

            w->move(static_cast<int>(std::lround(vp.x)), static_cast<int>(std::lround(vp.y)));
            w->resize(scaledW, scaledH);
        }

        // Delete widgets that were previously managed by proxy items but are no longer in the scene.
        for (SwWidget* w : m_proxyWidgets) {
            if (!w) {
                continue;
            }
            if (std::find(liveWidgets.begin(), liveWidgets.end(), w) != liveWidgets.end()) {
                continue;
            }
            deleteOrDetachProxyWidget(w);
        }
        m_proxyWidgets = std::move(liveWidgets);
    }

    SwGraphicsScene* m_scene{nullptr};
    double m_scale{1.0};
    SwPointF m_scroll{0.0, 0.0};
    std::vector<SwWidget*> m_proxyWidgets;
};
