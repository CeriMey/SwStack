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

#pragma once

/***************************************************************************************************
 * SwScrollArea - Qt-like scroll area (≈ QScrollArea).
 *
 * Requires painter clipping support to avoid drawing outside the viewport.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwScrollBar.h"

class SwScrollArea : public SwFrame {
    SW_OBJECT(SwScrollArea, SwFrame)

public:
    explicit SwScrollArea(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
    }

    void refreshLayout() {
        updateLayout();
        update();
    }

    void setWidget(SwWidget* widget) {
        if (m_widget == widget) {
            updateLayout();
            return;
        }

        if (m_widget) {
            SwObject::disconnect(m_widget, this);
            m_widget->setParent(nullptr);
        }

        m_widget = widget;
        if (m_widget) {
            m_widget->setParent(m_viewport);
            SwObject::connect(m_widget, &SwWidget::resized, this, &SwScrollArea::onContentResized_);
        }

        updateLayout();
        update();
    }

    SwWidget* widget() const { return m_widget; }

    void setWidgetResizable(bool on) {
        if (m_widgetResizable == on) {
            return;
        }
        m_widgetResizable = on;
        updateLayout();
        update();
    }

    bool widgetResizable() const { return m_widgetResizable; }

    SwScrollBar* horizontalScrollBar() const { return m_hBar; }
    SwScrollBar* verticalScrollBar() const { return m_vBar; }

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwFrame::wheelEvent(event);
            return;
        }

        int steps = event->delta() / 120;
        if (steps == 0) {
            steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
        }
        if (steps == 0) {
            SwFrame::wheelEvent(event);
            return;
        }

        const int stepY = std::max(1, std::max(24, (m_vBar ? (m_vBar->pageStep() / 10) : 0)));
        const int stepX = std::max(1, std::max(24, (m_hBar ? (m_hBar->pageStep() / 10) : 0)));

        auto scrollBy = [&](SwScrollBar* bar, int stepPx) -> bool {
            if (!bar || stepPx <= 0) {
                return false;
            }
            const int old = bar->value();
            bar->setValue(old - steps * stepPx);
            return bar->value() != old;
        };

        bool scrolled = false;
        if (event->isShiftPressed()) {
            if (m_hBar && m_hBar->getVisible()) {
                scrolled = scrollBy(m_hBar, stepX);
            } else if (m_vBar && m_vBar->getVisible()) {
                scrolled = scrollBy(m_vBar, stepY);
            }
        } else {
            if (m_vBar && m_vBar->getVisible()) {
                scrolled = scrollBy(m_vBar, stepY);
            } else if (m_hBar && m_hBar->getVisible()) {
                scrolled = scrollBy(m_hBar, stepX);
            }
        }

        if (scrolled) {
            event->accept();
            return;
        }

        // If a scrollbar is visible, keep the wheel focus on this scroll area even when already at
        // the boundary, to avoid "scroll chaining" to parent widgets.
        if ((m_vBar && m_vBar->getVisible()) || (m_hBar && m_hBar->getVisible())) {
            event->accept();
            return;
        }

        SwFrame::wheelEvent(event);
    }

private:
    class Viewport : public SwWidget {
        SW_OBJECT(Viewport, SwWidget)

    public:
        explicit Viewport(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        void paintEvent(PaintEvent* event) override {
            if (!isVisibleInHierarchy()) {
                return;
            }
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }

            const SwRect rect = getRect();
            painter->pushClipRect(rect);
            SwWidget::paintEvent(event);
            painter->popClipRect();
        }
    };

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    void initDefaults() {
        setFrameShape(Shape::Box);
        setStyleSheet(R"(
            SwScrollArea {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
            }
        )");

        m_viewport = new Viewport(this);
        m_hBar = new SwScrollBar(SwScrollBar::Orientation::Horizontal, this);
        m_vBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);

        m_hBar->hide();
        m_vBar->hide();

        SwObject::connect(m_hBar, &SwScrollBar::valueChanged, [this](int) { updateContentPosition(); });
        SwObject::connect(m_vBar, &SwScrollBar::valueChanged, [this](int) { updateContentPosition(); });
    }

    void updateLayout() {
        if (m_inUpdateLayout) {
            m_updateLayoutQueued = true;
            return;
        }

        m_inUpdateLayout = true;
        do {
            m_updateLayoutQueued = false;
            updateLayoutOnce_();
        } while (m_updateLayoutQueued);
        m_inUpdateLayout = false;
    }

    void updateLayoutOnce_() {
        const SwRect outer = getRect();
        int borderWidth = lineWidth() > 0 ? lineWidth() : 1;
        int radius = 10;
        SwColor border{220, 224, 232};
        resolveBorder(getToolSheet(), border, borderWidth, radius);
        const int borderPad = std::max(0, borderWidth);
        SwRect inner{outer.x + borderPad,
                     outer.y + borderPad,
                     std::max(0, outer.width - borderPad * 2),
                     std::max(0, outer.height - borderPad * 2)};

        const int thickness = m_scrollBarThickness;

        int hintW = 0;
        int hintH = 0;
        if (m_widget) {
            const SwRect hint = m_widget->sizeHint();
            const SwRect minHint = m_widget->minimumSizeHint();
            hintW = std::max(0, std::max(hint.width, minHint.width));
            hintH = std::max(0, std::max(hint.height, minHint.height));
        }

        int viewportW = inner.width;
        int viewportH = inner.height;

        bool showH = false;
        bool showV = false;

        int contentW = 0;
        int contentH = 0;

        // Determine visible scrollbars with a small iteration (since adding one may require the other).
        for (int pass = 0; pass < 2; ++pass) {
            if (m_widgetResizable) {
                // Qt-like: when resizable, the widget fills the viewport horizontally but keeps its
                // preferred/minimum height, so the scroll area provides a single (usually vertical)
                // scrollbar when content is taller than the viewport.
                contentW = std::max(0, viewportW);
                contentH = std::max(viewportH, hintH);
            } else if (m_widget) {
                SwRect r = m_widget->getRect();
                contentW = std::max(0, r.width);
                contentH = std::max(0, r.height);
            } else {
                contentW = 0;
                contentH = 0;
            }

            showV = contentH > viewportH;
            showH = !m_widgetResizable && (contentW > viewportW);
            viewportW = inner.width - (showV ? thickness : 0);
            viewportH = inner.height - (showH ? thickness : 0);
            viewportW = std::max(0, viewportW);
            viewportH = std::max(0, viewportH);
        }

        SwRect viewportRect{inner.x, inner.y, viewportW, viewportH};
        m_viewport->move(viewportRect.x, viewportRect.y);
        m_viewport->resize(viewportRect.width, viewportRect.height);

        if (m_widget && m_widgetResizable) {
            contentW = std::max(0, viewportRect.width);
            contentH = std::max(viewportRect.height, hintH);
            m_widget->move(viewportRect.x, viewportRect.y);
            m_widget->resize(contentW, contentH);
        }

        // Scrollbars geometry
        if (showH) {
            m_hBar->show();
            m_hBar->move(inner.x, inner.y + viewportH);
            m_hBar->resize(viewportW, thickness);
        } else {
            m_hBar->hide();
            m_hBar->setValue(0);
        }

        if (showV) {
            m_vBar->show();
            m_vBar->move(inner.x + viewportW, inner.y);
            m_vBar->resize(thickness, viewportH);
        } else {
            m_vBar->hide();
            m_vBar->setValue(0);
        }

        // Range configuration
        const int hMax = std::max(0, contentW - viewportW);
        const int vMax = std::max(0, contentH - viewportH);

        m_hBar->setRange(0, hMax);
        m_hBar->setPageStep(std::max(1, viewportW));
        m_vBar->setRange(0, vMax);
        m_vBar->setPageStep(std::max(1, viewportH));

        updateContentPosition();
    }

    void onContentResized_(int, int) {
        updateLayout();
        update();
    }

    void updateContentPosition() {
        if (!m_widget) {
            return;
        }

        const SwRect viewportRect = m_viewport->getRect();
        const int x = viewportRect.x - m_hBar->value();
        const int y = viewportRect.y - m_vBar->value();
        m_widget->move(x, y);
        m_viewport->update();
    }

    Viewport* m_viewport{nullptr};
    SwWidget* m_widget{nullptr};
    SwScrollBar* m_hBar{nullptr};
    SwScrollBar* m_vBar{nullptr};
    bool m_widgetResizable{false};
    int m_scrollBarThickness{14};
    bool m_inUpdateLayout{false};
    bool m_updateLayoutQueued{false};
};
