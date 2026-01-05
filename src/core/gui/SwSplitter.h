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
 * SwSplitter - Qt-like splitter widget (≈ QSplitter).
 *
 * Minimal implementation:
 * - Supports N children arranged in a row/column with draggable handles.
 * - Stores pixel sizes and keeps the sum consistent when dragging a handle.
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwVector.h"

class SwSplitter : public SwWidget {
    SW_OBJECT(SwSplitter, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    explicit SwSplitter(Orientation orientation = Orientation::Horizontal, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation) {
        initDefaults();
    }

    void setOrientation(Orientation orientation) {
        if (m_orientation == orientation) {
            return;
        }
        m_orientation = orientation;
        updateLayout();
        invalidateRect();
    }

    Orientation orientation() const { return m_orientation; }

    void setHandleWidth(int width) {
        const int clamped = clampInt(width, 0, 24);
        if (m_handleWidth == clamped) {
            return;
        }
        m_handleWidth = clamped;
        updateLayout();
        invalidateRect();
    }

    int handleWidth() const { return m_handleWidth; }

    void setOpaqueResize(bool enabled) { m_opaqueResize = enabled; }

    bool opaqueResize() const { return m_opaqueResize; }

    void addWidget(SwWidget* widget) {
        insertWidget(m_widgets.size(), widget);
    }

    void insertWidget(int index, SwWidget* widget) {
        if (!widget) {
            return;
        }

        if (index < 0) {
            index = 0;
        }

        const int n = m_widgets.size();
        if (index > n) {
            index = n;
        }

        if (widget->parent() != this) {
            widget->setParent(this);
        }

        SwVector<SwWidget*> newWidgets;
        newWidgets.reserve(static_cast<SwVector<SwWidget*>::size_type>(n + 1));
        for (int i = 0; i < n + 1; ++i) {
            if (i == index) {
                newWidgets.push_back(widget);
            } else {
                const int src = (i < index) ? i : (i - 1);
                if (src >= 0 && src < n) {
                    newWidgets.push_back(m_widgets[src]);
                }
            }
        }
        m_widgets = newWidgets;

        m_sizes.clear();
        updateLayout();
        invalidateRect();
    }

    int count() const { return m_widgets.size(); }

    SwWidget* widget(int index) const {
        if (index < 0 || index >= m_widgets.size()) {
            return nullptr;
        }
        return m_widgets[index];
    }

    void setSizes(const SwVector<int>& sizes) {
        if (sizes.size() != m_widgets.size()) {
            return;
        }
        m_sizes = sizes;
        updateLayout();
        invalidateRect();
    }

    SwVector<int> sizes() const { return m_sizes; }

    DECLARE_SIGNAL(splitterMoved, int, int);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateLayout();
    }

    void paintEvent(PaintEvent* event) override {
        SwWidget::paintEvent(event);

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const int handleCount = std::max(0, count() - 1);
        for (int i = 0; i < handleCount; ++i) {
            const SwRect h = handleRect(i);
            if (h.width <= 0 || h.height <= 0) {
                continue;
            }

            const bool hot = (i == m_hoverHandle) || (i == m_dragHandle);
            SwColor fill = hot ? SwColor{220, 224, 232} : SwColor{236, 236, 236};
            SwColor border = hot ? SwColor{180, 186, 198} : SwColor{210, 210, 210};
            const int radius = clampInt(std::min(h.width, h.height) / 4, 2, 8);
            painter->fillRoundedRect(h, radius, fill, border, 1);

            // Grip (3 small lines)
            const int cx = h.x + h.width / 2;
            const int cy = h.y + h.height / 2;
            const int len = (m_orientation == Orientation::Horizontal) ? clampInt(h.height / 4, 6, 18)
                                                                       : clampInt(h.width / 4, 6, 18);
            SwColor grip{150, 150, 150};
            if (m_orientation == Orientation::Horizontal) {
                painter->drawLine(cx - 4, cy - len / 2, cx - 4, cy + len / 2, grip, 1);
                painter->drawLine(cx, cy - len / 2, cx, cy + len / 2, grip, 1);
                painter->drawLine(cx + 4, cy - len / 2, cx + 4, cy + len / 2, grip, 1);
            } else {
                painter->drawLine(cx - len / 2, cy - 4, cx + len / 2, cy - 4, grip, 1);
                painter->drawLine(cx - len / 2, cy, cx + len / 2, cy, grip, 1);
                painter->drawLine(cx - len / 2, cy + 4, cx + len / 2, cy + 4, grip, 1);
            }
        }
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const int handleIdx = handleAt(event->x(), event->y());
        if (handleIdx >= 0) {
            m_dragHandle = handleIdx;
            const SwRect h = handleRect(handleIdx);
            const int axis = (m_orientation == Orientation::Horizontal) ? event->x() : event->y();
            const int handleAxis = (m_orientation == Orientation::Horizontal) ? h.x : h.y;
            m_dragOffset = axis - handleAxis;
            event->accept();
            invalidateHandle_(handleIdx);
            return;
        }

        SwWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (m_dragHandle >= 0) {
            const SwRect oldHandle = handleRect(m_dragHandle);
            dragHandle(m_dragHandle, (m_orientation == Orientation::Horizontal) ? event->x() : event->y());
            const SwRect newHandle = handleRect(m_dragHandle);
            invalidateRect_(unionRect_(oldHandle, newHandle));
            SwWidgetPlatformAdapter::setCursor(m_orientation == Orientation::Horizontal ? CursorType::SizeWE : CursorType::SizeNS);
            event->accept();
            return;
        }

        const int handleIdx = handleAt(event->x(), event->y());
        if (handleIdx != m_hoverHandle) {
            const int oldHover = m_hoverHandle;
            m_hoverHandle = handleIdx;
            if (oldHover >= 0) {
                invalidateHandle_(oldHover);
            }
            if (m_hoverHandle >= 0) {
                invalidateHandle_(m_hoverHandle);
            }
        }

        if (m_hoverHandle >= 0) {
            SwWidgetPlatformAdapter::setCursor(m_orientation == Orientation::Horizontal ? CursorType::SizeWE : CursorType::SizeNS);
            event->accept();
            return;
        }

        SwWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (m_dragHandle >= 0) {
            const int releasedHandle = m_dragHandle;
            if (!m_opaqueResize) {
                updateLayout();
            }
            m_dragHandle = -1;
            m_dragOffset = 0;
            event->accept();
            invalidateHandle_(releasedHandle);
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

private:
    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwRect unionRect_(const SwRect& a, const SwRect& b) {
        if (a.width <= 0 || a.height <= 0) return b;
        if (b.width <= 0 || b.height <= 0) return a;
        const int left = std::min(a.x, b.x);
        const int top = std::min(a.y, b.y);
        const int right = std::max(a.x + a.width, b.x + b.width);
        const int bottom = std::max(a.y + a.height, b.y + b.height);
        return SwRect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }

    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    int primaryLength(const SwRect& r) const {
        return (m_orientation == Orientation::Horizontal) ? r.width : r.height;
    }

    int handleCount() const {
        return std::max(0, count() - 1);
    }

    int sizesSum() const {
        int sum = 0;
        for (int i = 0; i < m_sizes.size(); ++i) {
            sum += std::max(0, m_sizes[i]);
        }
        return sum;
    }

    void ensureSizesInitialized(int available) {
        const int n = count();
        if (n <= 0) {
            m_sizes.clear();
            return;
        }

        if (m_sizes.size() != n) {
            m_sizes.clear();
        }

        if (m_sizes.size() == n) {
            const int sum = sizesSum();
            if (sum == available) {
                enforceMinimumSizes_(available);
                return;
            }
            if (sum > 0 && available > 0) {
                // Scale to fit.
                SwVector<int> scaled;
                scaled.reserve(static_cast<SwVector<int>::size_type>(n));
                int used = 0;
                for (int i = 0; i < n; ++i) {
                    const int v = std::max(0, m_sizes[i]);
                    const int s = static_cast<int>((static_cast<long long>(v) * available) / sum);
                    scaled.push_back(s);
                    used += s;
                }
                int remainder = available - used;
                for (int i = 0; remainder > 0 && i < n; ++i, --remainder) {
                    scaled[i] += 1;
                }
                m_sizes = scaled;
                enforceMinimumSizes_(available);
                return;
            }
        }

        // Default: equal distribution.
        m_sizes.clear();
        const int each = (n > 0) ? (available / n) : 0;
        int remainder = (n > 0) ? (available % n) : 0;
        for (int i = 0; i < n; ++i) {
            int s = each;
            if (remainder > 0) {
                ++s;
                --remainder;
            }
            m_sizes.push_back(s);
        }
        enforceMinimumSizes_(available);
    }

    void enforceMinimumSizes_(int available) {
        const int n = count();
        if (n <= 0 || m_sizes.size() != n) {
            return;
        }

        SwVector<int> mins;
        mins.reserve(static_cast<SwVector<int>::size_type>(n));
        long long totalMin = 0;
        for (int i = 0; i < n; ++i) {
            SwWidget* w = m_widgets[i];
            int minPrimary = 0;
            if (w) {
                const SwRect r = w->minimumSizeHint();
                minPrimary = std::max(0, (m_orientation == Orientation::Horizontal) ? r.width : r.height);
            }
            mins.push_back(minPrimary);
            totalMin += minPrimary;
        }

        if (available <= 0) {
            for (int i = 0; i < n; ++i) {
                m_sizes[i] = 0;
            }
            return;
        }

        if (totalMin > available && totalMin > 0) {
            SwVector<int> scaled;
            scaled.reserve(static_cast<SwVector<int>::size_type>(n));
            long long used = 0;
            for (int i = 0; i < n; ++i) {
                const int s = static_cast<int>((static_cast<long long>(mins[i]) * available) / totalMin);
                scaled.push_back(s);
                used += s;
            }
            int remainder = static_cast<int>(available - used);
            for (int i = 0; remainder > 0 && i < n; ++i, --remainder) {
                scaled[i] += 1;
            }
            m_sizes = scaled;
            return;
        }

        long long deficit = 0;
        for (int i = 0; i < n; ++i) {
            const int minV = mins[i];
            const int current = std::max(0, m_sizes[i]);
            if (current < minV) {
                deficit += static_cast<long long>(minV - current);
                m_sizes[i] = minV;
            } else {
                m_sizes[i] = current;
            }
        }

        while (deficit > 0) {
            int best = -1;
            int bestReducible = 0;
            for (int i = 0; i < n; ++i) {
                const int reducible = m_sizes[i] - mins[i];
                if (reducible > bestReducible) {
                    bestReducible = reducible;
                    best = i;
                }
            }
            if (best < 0 || bestReducible <= 0) {
                break;
            }
            const int take = static_cast<int>(std::min<long long>(deficit, bestReducible));
            m_sizes[best] -= take;
            deficit -= take;
        }

        // If we couldn't satisfy all minimums (shouldn't happen when totalMin <= available), fall back to scaling.
        if (deficit > 0) {
            SwVector<int> scaled;
            scaled.reserve(static_cast<SwVector<int>::size_type>(n));
            long long used = 0;
            for (int i = 0; i < n; ++i) {
                const int s = static_cast<int>((static_cast<long long>(mins[i]) * available) / std::max<long long>(1, totalMin));
                scaled.push_back(s);
                used += s;
            }
            int remainder = static_cast<int>(available - used);
            for (int i = 0; remainder > 0 && i < n; ++i, --remainder) {
                scaled[i] += 1;
            }
            m_sizes = scaled;
        }
    }

    int handleAt(int px, int py) const {
        const int hc = handleCount();
        for (int i = 0; i < hc; ++i) {
            if (containsPoint(handleRect(i), px, py)) {
                return i;
            }
        }
        return -1;
    }

    int prefixSize(int widgetIndex) const {
        int sum = 0;
        for (int i = 0; i < widgetIndex && i < m_sizes.size(); ++i) {
            sum += std::max(0, m_sizes[i]);
        }
        return sum;
    }

    SwRect handleRect(int handleIndex) const {
        const SwRect bounds = getRect();
        const int n = count();
        if (n <= 1 || handleIndex < 0 || handleIndex >= n - 1) {
            return SwRect{0, 0, 0, 0};
        }

        const int before = prefixSize(handleIndex + 1);
        const int handleOffset = handleIndex * m_handleWidth;

        if (m_orientation == Orientation::Horizontal) {
            const int x = bounds.x + before + handleOffset;
            return SwRect{x, bounds.y, m_handleWidth, bounds.height};
        }

        const int y = bounds.y + before + handleOffset;
        return SwRect{bounds.x, y, bounds.width, m_handleWidth};
    }

    void invalidateRect_(const SwRect& r) {
        if (!isVisibleInHierarchy() || r.width <= 0 || r.height <= 0) {
            return;
        }
        SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, r);
    }

    void invalidateHandle_(int handleIdx) {
        if (handleIdx < 0) {
            return;
        }
        invalidateRect_(handleRect(handleIdx));
    }

    void updateLayout() {
        const int n = count();
        if (n <= 0) {
            return;
        }

        const SwRect bounds = getRect();
        const int handles = handleCount();
        const int available = std::max(0, primaryLength(bounds) - handles * m_handleWidth);
        ensureSizesInitialized(available);

        int pos = (m_orientation == Orientation::Horizontal) ? bounds.x : bounds.y;

        for (int i = 0; i < n; ++i) {
            SwWidget* w = m_widgets[i];
            if (!w) {
                continue;
            }
            const int sizePrimary = (i < m_sizes.size()) ? std::max(0, m_sizes[i]) : 0;
            if (m_orientation == Orientation::Horizontal) {
                w->move(pos, bounds.y);
                w->resize(sizePrimary, bounds.height);
                pos += sizePrimary + m_handleWidth;
            } else {
                w->move(bounds.x, pos);
                w->resize(bounds.width, sizePrimary);
                pos += sizePrimary + m_handleWidth;
            }
        }
    }

    void dragHandle(int handleIdx, int axisPos) {
        const int n = count();
        if (n <= 1 || handleIdx < 0 || handleIdx >= n - 1) {
            return;
        }

        const SwRect bounds = getRect();
        const int handles = handleCount();
        const int available = std::max(0, primaryLength(bounds) - handles * m_handleWidth);
        ensureSizesInitialized(available);

        const int minSize = 24;

        const int startAxis = (m_orientation == Orientation::Horizontal) ? bounds.x : bounds.y;
        const int leftStart = startAxis + prefixSize(handleIdx) + handleIdx * m_handleWidth;

        const int pairTotal = std::max(0, m_sizes[handleIdx]) + std::max(0, m_sizes[handleIdx + 1]);

        int newHandleStart = axisPos - m_dragOffset;
        int newLeftSize = newHandleStart - leftStart;
        newLeftSize = clampInt(newLeftSize, minSize, std::max(minSize, pairTotal - minSize));
        int newRightSize = pairTotal - newLeftSize;

        if (newLeftSize == m_sizes[handleIdx] && newRightSize == m_sizes[handleIdx + 1]) {
            return;
        }

        m_sizes[handleIdx] = newLeftSize;
        m_sizes[handleIdx + 1] = newRightSize;
        if (m_opaqueResize) {
            updateLayout();
        }
        splitterMoved(leftStart + newLeftSize, handleIdx);
    }

    void initDefaults() {
        setStyleSheet("SwSplitter { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        m_handleWidth = 10;
        resize(520, 280);
    }

    SwVector<SwWidget*> m_widgets;
    SwVector<int> m_sizes;
    Orientation m_orientation{Orientation::Horizontal};

    int m_handleWidth{10};
    int m_hoverHandle{-1};
    int m_dragHandle{-1};
    int m_dragOffset{0};
    bool m_opaqueResize{true};
};
