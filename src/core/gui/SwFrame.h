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
 * @file src/core/gui/SwFrame.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwFrame in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the frame interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwFrame.
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
 * SwFrame - frame/container widget.
 **************************************************************************************************/

#include "SwWidget.h"

class SwFrame : public SwWidget {
    SW_OBJECT(SwFrame, SwWidget)

public:
    enum class Shape {
        NoFrame,
        Box,
        Panel,
        StyledPanel,
        HLine,
        VLine
    };

    enum class Shadow {
        Plain,
        Raised,
        Sunken
    };

    /**
     * @brief Constructs a `SwFrame` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwFrame(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Sets the frame Shape.
     * @param shape Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFrameShape(Shape shape) {
        if (m_shape == shape) {
            return;
        }
        m_shape = shape;
        update();
    }

    /**
     * @brief Returns the current frame Shape.
     * @return The current frame Shape.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Shape frameShape() const { return m_shape; }

    /**
     * @brief Sets the frame Shadow.
     * @param shadow Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFrameShadow(Shadow shadow) {
        if (m_shadow == shadow) {
            return;
        }
        m_shadow = shadow;
        update();
    }

    /**
     * @brief Returns the current frame Shadow.
     * @return The current frame Shadow.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Shadow frameShadow() const { return m_shadow; }

    /**
     * @brief Sets the line Width.
     * @param width Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLineWidth(int width) {
        m_lineWidth = clampInt(width, 0, 20);
        update();
    }

    /**
     * @brief Returns the current line Width.
     * @return The current line Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int lineWidth() const { return m_lineWidth; }

    /**
     * @brief Sets the mid Line Width.
     * @param width Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMidLineWidth(int width) {
        m_midLineWidth = clampInt(width, 0, 20);
        update();
    }

    /**
     * @brief Returns the current mid Line Width.
     * @return The current mid Line Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int midLineWidth() const { return m_midLineWidth; }

    /**
     * @brief Returns the current frame Width.
     * @return The current frame Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int frameWidth() const { return m_lineWidth + m_midLineWidth; }

protected:
    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        NormalizedPaintEvent_ normalized = normalizePaintEvent_(event);
        SwPainter* painter = normalized.painter;
        if (!painter) {
            return;
        }
        PaintEvent effectiveEvent(painter, normalized.paintRect);
        PaintEvent* localEvent = event;
        const bool needsSyntheticEvent =
            !event ||
            normalized.painter != event->painter() ||
            normalized.paintRect.x != event->paintRect().x ||
            normalized.paintRect.y != event->paintRect().y ||
            normalized.paintRect.width != event->paintRect().width ||
            normalized.paintRect.height != event->paintRect().height;
        if (needsSyntheticEvent) {
            localEvent = &effectiveEvent;
        }

        const SwRect rect = this->rect();
        const StyleSheet* sheet = getToolSheet();
        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = m_lineWidth > 0 ? m_lineWidth : 1;
        int radius = 10;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);
        int tl = radius;
        int tr = radius;
        int br = radius;
        int bl = radius;
        resolveBorderCornerRadii(sheet, tl, tr, br, bl);

        if (m_shape == Shape::NoFrame) {
            paintChildren(localEvent);
            return;
        }

        if (m_shape == Shape::HLine) {
            const int y = rect.y + rect.height / 2;
            painter->drawLine(rect.x, y, rect.x + rect.width, y, border, std::max(1, borderWidth));
            return;
        }

        if (m_shape == Shape::VLine) {
            const int x = rect.x + rect.width / 2;
            painter->drawLine(x, rect.y, x, rect.y + rect.height, border, std::max(1, borderWidth));
            return;
        }

        if (paintBackground && bgAlpha > 0.0f) {
            if (tl == tr && tr == br && br == bl) {
                painter->fillRoundedRect(rect, tl, bg, border, borderWidth);
            } else {
                paintRoundedRectWithCorners(painter, rect, tl, tr, br, bl, bg, border, borderWidth);
            }
        } else if (borderWidth > 0) {
            painter->drawRect(rect, border, borderWidth);
        }

        paintChildren(localEvent);
    }

protected:
    /**
     * @brief Performs the `paintRoundedRectWithCorners` operation.
     * @param painter Value passed to the method.
     * @param rect Rectangle used by the operation.
     * @param tl Value passed to the method.
     * @param tr Value passed to the method.
     * @param br Value passed to the method.
     * @param bl Value passed to the method.
     * @param fill Value passed to the method.
     * @param border Value passed to the method.
     * @param borderWidth Value passed to the method.
     * @return The requested paint Rounded Rect With Corners.
     */
    static void paintRoundedRectWithCorners(SwPainter* painter,
                                           const SwRect& rect,
                                           int tl,
                                           int tr,
                                           int br,
                                           int bl,
                                           const SwColor& fill,
                                           const SwColor& border,
                                           int borderWidth) {
        if (!painter) {
            return;
        }

        int r = std::max(std::max(tl, tr), std::max(br, bl));
        if (r <= 0) {
            painter->fillRect(rect, fill, border, borderWidth);
            return;
        }

        const int maxRadius = std::max(0, std::min(rect.width, rect.height) / 2);
        r = clampInt(r, 0, maxRadius);
        if (r <= 0) {
            painter->fillRect(rect, fill, border, borderWidth);
            return;
        }

        const bool ok = (tl == 0 || tl == r) && (tr == 0 || tr == r) && (br == 0 || br == r) && (bl == 0 || bl == r);
        if (!ok) {
            painter->fillRoundedRect(rect, r, fill, border, borderWidth);
            return;
        }

        painter->fillRoundedRect(rect, r, fill, border, borderWidth);

        auto fillCorner = [&](int x, int y) {
            SwRect c{x, y, r, r};
            painter->fillRect(c, fill, fill, 0);
        };

        if (tl == 0) {
            fillCorner(rect.x, rect.y);
        }
        if (tr == 0) {
            fillCorner(rect.x + rect.width - r, rect.y);
        }
        if (bl == 0) {
            fillCorner(rect.x, rect.y + rect.height - r);
        }
        if (br == 0) {
            fillCorner(rect.x + rect.width - r, rect.y + rect.height - r);
        }

        if (borderWidth <= 0) {
            return;
        }

        const int x1 = rect.x;
        const int x2 = rect.x + rect.width;
        const int y1 = rect.y;
        const int y2 = rect.y + rect.height;

        const int topStart = x1 + (tl > 0 ? r : 0);
        const int topEnd = x2 - (tr > 0 ? r : 0);
        const int bottomStart = x1 + (bl > 0 ? r : 0);
        const int bottomEnd = x2 - (br > 0 ? r : 0);
        const int leftStart = y1 + (tl > 0 ? r : 0);
        const int leftEnd = y2 - (bl > 0 ? r : 0);
        const int rightStart = y1 + (tr > 0 ? r : 0);
        const int rightEnd = y2 - (br > 0 ? r : 0);

        if (topEnd > topStart) {
            painter->drawLine(topStart, y1, topEnd, y1, border, borderWidth);
        }
        if (bottomEnd > bottomStart) {
            painter->drawLine(bottomStart, y2, bottomEnd, y2, border, borderWidth);
        }
        if (leftEnd > leftStart) {
            painter->drawLine(x1, leftStart, x1, leftEnd, border, borderWidth);
        }
        if (rightEnd > rightStart) {
            painter->drawLine(x2, rightStart, x2, rightEnd, border, borderWidth);
        }
    }

    // clampInt, clampColor, parsePixelValue, resolveBackground, resolveBorder,
    // resolveBorderCornerRadii â€” inherited from SwWidget

    /**
     * @brief Performs the `paintChildren` operation.
     * @param event Event object forwarded by the framework.
     */
    void paintChildren(PaintEvent* event) {
        const SwRect& paintRect = event->paintRect();
        const auto& directChildren = children();
        for (SwObject* objChild : directChildren) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }
            const SwRect childRect = child->geometry();
            if (rectsIntersect(paintRect, childRect)) {
                paintChild_(event, child);
            }
        }
    }

private:
    void initDefaults() {
        setStyleSheet(R"(
            SwFrame {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
            }
        )");
        setFrameShape(Shape::NoFrame);
        setFrameShadow(Shadow::Plain);
        setLineWidth(1);
        setMidLineWidth(0);
    }

    Shape m_shape{Shape::NoFrame};
    Shadow m_shadow{Shadow::Plain};
    int m_lineWidth{1};
    int m_midLineWidth{0};
};

