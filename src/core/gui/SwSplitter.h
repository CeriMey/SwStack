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

/**
 * @file src/core/gui/SwSplitter.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwSplitter in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the splitter interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwSplitter.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwSplitter - splitter widget.
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

    /**
     * @brief Constructs a `SwSplitter` instance.
     * @param orientation Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param orientation Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwSplitter(Orientation orientation = Orientation::Horizontal, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation) {
        initDefaults();
    }

    /**
     * @brief Sets the orientation.
     * @param orientation Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOrientation(Orientation orientation) {
        if (m_orientation == orientation) {
            return;
        }
        m_orientation = orientation;
        updateLayout();
        invalidateRect();
    }

    /**
     * @brief Returns the current orientation.
     * @return The current orientation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Orientation orientation() const { return m_orientation; }

    /**
     * @brief Sets the handle Width.
     * @param width Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHandleWidth(int width) {
        const int clamped = clampInt(width, 0, 24);
        if (m_handleWidth == clamped) {
            return;
        }
        m_handleWidth = clamped;
        updateLayout();
        invalidateRect();
    }

    /**
     * @brief Returns the current handle Width.
     * @return The current handle Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int handleWidth() const { return m_handleWidth; }

    /**
     * @brief Sets the opaque Resize.
     * @param enabled Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOpaqueResize(bool enabled) { m_opaqueResize = enabled; }

    /**
     * @brief Returns the current opaque Resize.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool opaqueResize() const { return m_opaqueResize; }

    /**
     * @brief Adds the specified widget.
     * @param widget Widget associated with the operation.
     */
    void addWidget(SwWidget* widget) {
        insertWidget(m_widgets.size(), widget);
    }

    /**
     * @brief Performs the `insertWidget` operation.
     * @param index Value passed to the method.
     * @param widget Widget associated with the operation.
     */
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

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const { return m_widgets.size(); }

    /**
     * @brief Performs the `widget` operation.
     * @param index Value passed to the method.
     * @return The requested widget.
     */
    SwWidget* widget(int index) const {
        if (index < 0 || index >= m_widgets.size()) {
            return nullptr;
        }
        return m_widgets[index];
    }

    /**
     * @brief Sets the sizes.
     * @param sizes Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSizes(const SwVector<int>& sizes) {
        if (sizes.size() != m_widgets.size()) {
            return;
        }
        m_sizes = sizes;
        updateLayout();
        invalidateRect();
    }

    /**
     * @brief Returns the current sizes.
     * @return The current sizes.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwVector<int> sizes() const { return m_sizes; }

    DECLARE_SIGNAL(splitterMoved, int, int);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateLayout();
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        SwWidget::paintEvent(event);

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const HandleStyle_ style = resolveHandleStyle_();
        const int handleCount = std::max(0, count() - 1);
        for (int i = 0; i < handleCount; ++i) {
            const SwRect h = handleRect(i);
            if (h.width <= 0 || h.height <= 0) {
                continue;
            }

            const bool hovered = getHover() && (i == m_hoverHandle);
            const bool pressed = (i == m_dragHandle);
            SwColor handleColor = style.handleColor;
            SwColor borderColor = style.handleBorderColor;
            SwColor gripColor = style.gripColor;
            if (pressed) {
                handleColor = style.handleColorPressed;
                borderColor = style.handleBorderColorPressed;
                gripColor = style.gripColorPressed;
            } else if (hovered) {
                handleColor = style.handleColorHover;
                borderColor = style.handleBorderColorHover;
                gripColor = style.gripColorHover;
            }

            const int hoverGrow = pressed ? 3 : (hovered ? 2 : 0);
            const SwRect visualRect = visualHandleRect_(i, style, hoverGrow);

            const int primaryThickness = (m_orientation == Orientation::Horizontal) ? visualRect.width : visualRect.height;
            if (primaryThickness <= 2) {
                if (m_orientation == Orientation::Horizontal) {
                    const int cx = visualRect.x + visualRect.width / 2;
                    painter->drawLine(cx, visualRect.y, cx, visualRect.y + visualRect.height, handleColor, std::max(1, primaryThickness));
                } else {
                    const int cy = visualRect.y + visualRect.height / 2;
                    painter->drawLine(visualRect.x, cy, visualRect.x + visualRect.width, cy, handleColor, std::max(1, primaryThickness));
                }
            } else {
                painter->fillRoundedRect(visualRect,
                                         radiusFor_(visualRect, style.radius),
                                         handleColor,
                                         borderColor,
                                         style.borderWidth);

                if ((m_orientation == Orientation::Horizontal && visualRect.width >= 6 && visualRect.height >= 18) ||
                    (m_orientation == Orientation::Vertical && visualRect.height >= 6 && visualRect.width >= 18)) {
                    const int cx = visualRect.x + visualRect.width / 2;
                    const int cy = visualRect.y + visualRect.height / 2;
                    const int len = (m_orientation == Orientation::Horizontal)
                                        ? clampInt(visualRect.height / 4, 6, 18)
                                        : clampInt(visualRect.width / 4, 6, 18);
                    if (m_orientation == Orientation::Horizontal) {
                        painter->drawLine(cx - 4, cy - len / 2, cx - 4, cy + len / 2, gripColor, 1);
                        painter->drawLine(cx, cy - len / 2, cx, cy + len / 2, gripColor, 1);
                        painter->drawLine(cx + 4, cy - len / 2, cx + 4, cy + len / 2, gripColor, 1);
                    } else {
                        painter->drawLine(cx - len / 2, cy - 4, cx + len / 2, cy - 4, gripColor, 1);
                        painter->drawLine(cx - len / 2, cy, cx + len / 2, cy, gripColor, 1);
                        painter->drawLine(cx - len / 2, cy + 4, cx + len / 2, cy + 4, gripColor, 1);
                    }
                }
            }
        }
    }

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        cancelHoverReset_();
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

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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
        if (handleIdx >= 0) {
            cancelHoverReset_();
        }

        if (handleIdx >= 0 && handleIdx != m_hoverHandle) {
            const int oldHover = m_hoverHandle;
            m_hoverHandle = handleIdx;
            if (oldHover >= 0) {
                invalidateHandle_(oldHover);
            }
            if (m_hoverHandle >= 0) {
                invalidateHandle_(m_hoverHandle);
            }
        }

        if (handleIdx >= 0) {
            SwWidgetPlatformAdapter::setCursor(m_orientation == Orientation::Horizontal ? CursorType::SizeWE : CursorType::SizeNS);
            event->accept();
            return;
        }

        SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
        scheduleHoverReset_();
        SwWidget::mouseMoveEvent(event);
    }

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (m_dragHandle >= 0) {
            const int releasedHandle = m_dragHandle;
            const int previousHoverHandle = m_hoverHandle;
            if (!m_opaqueResize) {
                updateLayout();
            }
            m_dragHandle = -1;
            m_dragOffset = 0;
            const int handleUnderCursor = handleAt(event->x(), event->y());
            m_hoverHandle = (handleUnderCursor >= 0) ? handleUnderCursor : releasedHandle;
            event->accept();
            invalidateHandle_(releasedHandle);
            if (previousHoverHandle >= 0 && previousHoverHandle != releasedHandle) {
                invalidateHandle_(previousHoverHandle);
            }
            if (m_hoverHandle >= 0 && m_hoverHandle != releasedHandle && m_hoverHandle != previousHoverHandle) {
                invalidateHandle_(m_hoverHandle);
            }
            if (handleUnderCursor < 0) {
                SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
                scheduleHoverReset_();
            } else {
                cancelHoverReset_();
            }
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

private:
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

    struct HandleStyle_ {
        SwColor handleColor{236, 236, 236};
        SwColor handleColorHover{220, 224, 232};
        SwColor handleColorPressed{210, 216, 228};
        SwColor handleBorderColor{210, 210, 210};
        SwColor handleBorderColorHover{180, 186, 198};
        SwColor handleBorderColorPressed{160, 168, 182};
        SwColor gripColor{150, 150, 150};
        SwColor gripColorHover{138, 144, 154};
        SwColor gripColorPressed{125, 132, 144};
        int visualWidth{-1};
        int borderWidth{-1};
        int radius{-1};
    };

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
                const SwSize r = w->minimumSizeHint();
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
        const HandleStyle_ style = resolveHandleStyle_();
        const int hc = handleCount();
        for (int i = 0; i < hc; ++i) {
            if (containsPoint(hoverHitRect_(i, style), px, py)) {
                return i;
            }
        }
        return -1;
    }

    int radiusFor_(const SwRect& r, int explicitRadius) const {
        if (explicitRadius >= 0) {
            return explicitRadius;
        }
        const int radius = std::min(r.width, r.height) / 2;
        return std::max(1, std::min(8, radius));
    }

    HandleStyle_ resolveHandleStyle_() const {
        HandleStyle_ style;
        const bool thinDefault = m_handleWidth <= 5;
        if (thinDefault) {
            style.handleColor = SwColor{200, 205, 215};
            style.handleColorHover = SwColor{140, 150, 170};
            style.handleColorPressed = SwColor{120, 132, 154};
            style.handleBorderColor = style.handleColor;
            style.handleBorderColorHover = style.handleColorHover;
            style.handleBorderColorPressed = style.handleColorPressed;
            style.gripColor = style.handleColor;
            style.gripColorHover = style.handleColorHover;
            style.gripColorPressed = style.handleColorPressed;
            style.visualWidth = 1;
            style.borderWidth = 0;
            style.radius = 0;
        } else {
            style.visualWidth = m_handleWidth;
            style.borderWidth = 1;
        }

        const StyleSheet* sheet = const_cast<SwSplitter*>(this)->getToolSheet();
        const auto hierarchy = classHierarchy();

        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "handle-color"), style.handleColor);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "handle-color-hover"), style.handleColorHover);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "handle-color-pressed"), style.handleColorPressed);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "handle-border-color"), style.handleBorderColor);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "handle-border-color-hover"), style.handleBorderColorHover);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "handle-border-color-pressed"), style.handleBorderColorPressed);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "grip-color"), style.gripColor);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "grip-color-hover"), style.gripColorHover);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "grip-color-pressed"), style.gripColorPressed);

        SwString value = styleValue_(sheet, hierarchy, "handle-visual-width");
        if (!value.isEmpty()) {
            style.visualWidth = clampInt(parsePixelValue(value, style.visualWidth), 1, 24);
        }
        value = styleValue_(sheet, hierarchy, "handle-border-width");
        if (!value.isEmpty()) {
            style.borderWidth = clampInt(parsePixelValue(value, style.borderWidth), 0, 8);
        }
        value = styleValue_(sheet, hierarchy, "handle-radius");
        if (!value.isEmpty()) {
            style.radius = clampInt(parsePixelValue(value, style.radius), 0, 32);
        }

        if (style.visualWidth <= 2) {
            style.borderWidth = 0;
        }
        return style;
    }

    int prefixSize(int widgetIndex) const {
        int sum = 0;
        for (int i = 0; i < widgetIndex && i < m_sizes.size(); ++i) {
            sum += std::max(0, m_sizes[i]);
        }
        return sum;
    }

    SwRect handleRect(int handleIndex) const {
        const SwRect bounds = rect();
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

    SwRect visualHandleRect_(int handleIndex, const HandleStyle_& style, int extraPrimaryThickness = 0) const {
        const SwRect h = handleRect(handleIndex);
        if (h.width <= 0 || h.height <= 0) {
            return h;
        }

        if (m_orientation == Orientation::Horizontal) {
            const int visualWidth = std::max(1, style.visualWidth + std::max(0, extraPrimaryThickness));
            const int cx = h.x + (h.width / 2);
            return SwRect{cx - (visualWidth / 2), h.y, visualWidth, h.height};
        }

        const int visualHeight = std::max(1, style.visualWidth + std::max(0, extraPrimaryThickness));
        const int cy = h.y + (h.height / 2);
        return SwRect{h.x, cy - (visualHeight / 2), h.width, visualHeight};
    }

    SwRect hoverHitRect_(int handleIndex, const HandleStyle_& style) const {
        SwRect rect = visualHandleRect_(handleIndex, style, 0);
        const int tolerance = 2;
        if (m_orientation == Orientation::Horizontal) {
            rect.x -= tolerance;
            rect.width += tolerance * 2;
        } else {
            rect.y -= tolerance;
            rect.height += tolerance * 2;
        }
        return rect;
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
        const HandleStyle_ style = resolveHandleStyle_();
        invalidateRect_(hoverHitRect_(handleIdx, style));
        invalidateRect_(visualHandleRect_(handleIdx, style, 3));
    }

    void cancelHoverReset_() {
        if (m_hoverResetTimerId >= 0) {
            if (SwCoreApplication::instance(false)) {
                SwCoreApplication::instance(false)->removeTimer(m_hoverResetTimerId);
            }
            m_hoverResetTimerId = -1;
        }
        ++m_hoverResetGeneration;
    }

    void scheduleHoverReset_() {
        if (m_hoverHandle < 0 || m_dragHandle >= 0 || m_hoverResetTimerId >= 0) {
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            const int oldHover = m_hoverHandle;
            m_hoverHandle = -1;
            invalidateHandle_(oldHover);
            return;
        }

        const unsigned long long generation = ++m_hoverResetGeneration;
        SwSplitter* self = this;
        m_hoverResetTimerId = app->addTimer([self, generation]() {
            if (!SwObject::isLive(self)) {
                return;
            }
            if (self->m_hoverResetGeneration != generation) {
                return;
            }
            self->m_hoverResetTimerId = -1;
            if (self->m_dragHandle >= 0 || self->m_hoverHandle < 0) {
                return;
            }
            const int oldHover = self->m_hoverHandle;
            self->m_hoverHandle = -1;
            self->invalidateHandle_(oldHover);
        }, 3000000, true);
    }

    void updateLayout() {
        const int n = count();
        if (n <= 0) {
            return;
        }

        const SwRect bounds = rect();
        const int handles = handleCount();
        const int available = std::max(0, primaryLength(bounds) - handles * m_handleWidth);
        ensureSizesInitialized(available);

        int pos = 0;

        for (int i = 0; i < n; ++i) {
            SwWidget* w = m_widgets[i];
            if (!w) {
                continue;
            }
            const int sizePrimary = (i < m_sizes.size()) ? std::max(0, m_sizes[i]) : 0;
            if (m_orientation == Orientation::Horizontal) {
                w->move(pos, 0);
                w->resize(sizePrimary, bounds.height);
                pos += sizePrimary + m_handleWidth;
            } else {
                w->move(0, pos);
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

        const SwRect bounds = rect();
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

    static SwString styleValue_(const StyleSheet* sheet,
                                const SwList<SwString>& hierarchy,
                                const char* propertyName) {
        if (!sheet || !propertyName) {
            return SwString();
        }

        SwString out;
        for (int i = static_cast<int>(hierarchy.size()) - 1; i >= 0; --i) {
            const SwString& selector = hierarchy[static_cast<size_t>(i)];
            if (selector.isEmpty()) {
                continue;
            }
            SwString value = sheet->getStyleProperty(selector, propertyName);
            if (!value.isEmpty()) {
                out = value;
            }
        }
        return out;
    }

    static bool tryParseColor_(const StyleSheet* sheet, const SwString& value, SwColor& out) {
        if (!sheet || value.isEmpty()) {
            return false;
        }
        try {
            out = clampColor(const_cast<StyleSheet*>(sheet)->parseColor(value, nullptr));
            return true;
        } catch (...) {
            return false;
        }
    }

    SwVector<SwWidget*> m_widgets;
    SwVector<int> m_sizes;
    Orientation m_orientation{Orientation::Horizontal};

    int m_handleWidth{10};
    int m_hoverHandle{-1};
    int m_dragHandle{-1};
    int m_dragOffset{0};
    bool m_opaqueResize{true};
    int m_hoverResetTimerId{-1};
    unsigned long long m_hoverResetGeneration{0};
};

