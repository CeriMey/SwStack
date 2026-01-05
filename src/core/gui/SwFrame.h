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
 * SwFrame - Qt-like frame/container widget (≈ QFrame).
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

    explicit SwFrame(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    void setFrameShape(Shape shape) {
        if (m_shape == shape) {
            return;
        }
        m_shape = shape;
        update();
    }

    Shape frameShape() const { return m_shape; }

    void setFrameShadow(Shadow shadow) {
        if (m_shadow == shadow) {
            return;
        }
        m_shadow = shadow;
        update();
    }

    Shadow frameShadow() const { return m_shadow; }

    void setLineWidth(int width) {
        m_lineWidth = clampInt(width, 0, 20);
        update();
    }

    int lineWidth() const { return m_lineWidth; }

    void setMidLineWidth(int width) {
        m_midLineWidth = clampInt(width, 0, 20);
        update();
    }

    int midLineWidth() const { return m_midLineWidth; }

    int frameWidth() const { return m_lineWidth + m_midLineWidth; }

protected:
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = getRect();
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
            paintChildren(event);
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

        paintChildren(event);
    }

protected:
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

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor(const SwColor& c) {
        return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
    }

    static int parsePixelValue(const SwString& value, int defaultValue) {
        if (value.isEmpty()) {
            return defaultValue;
        }
        SwString cleaned = value;
        cleaned.replace("px", "");
        bool ok = false;
        int v = cleaned.toInt(&ok);
        return ok ? v : defaultValue;
    }

    void resolveBackground(const StyleSheet* sheet,
                           SwColor& outColor,
                           float& outAlpha,
                           bool& outPaint) const {
        if (!sheet) {
            return;
        }

        auto selectors = classHierarchy();
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

        for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
            const SwString& selector = selectors[i];
            if (selector.isEmpty()) {
                continue;
            }
            SwString value = sheet->getStyleProperty(selector.toStdString(), "background-color");
            if (value.isEmpty()) {
                continue;
            }
            float alpha = 1.0f;
            try {
                SwColor resolved = const_cast<StyleSheet*>(sheet)->parseColor(value.toStdString(), &alpha);
                if (alpha <= 0.0f) {
                    outPaint = false;
                } else {
                    outColor = clampColor(resolved);
                    outPaint = true;
                }
                outAlpha = alpha;
            } catch (...) {
                // ignore invalid colors
            }
            return;
        }
    }

    void resolveBorder(const StyleSheet* sheet,
                       SwColor& outColor,
                       int& outWidth,
                       int& outRadius) const {
        if (!sheet) {
            return;
        }

        auto selectors = classHierarchy();
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

        for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
            const SwString& selector = selectors[i];
            if (selector.isEmpty()) {
                continue;
            }

            SwString borderColor = sheet->getStyleProperty(selector.toStdString(), "border-color");
            if (!borderColor.isEmpty()) {
                try {
                    SwColor resolved = const_cast<StyleSheet*>(sheet)->parseColor(borderColor.toStdString(), nullptr);
                    outColor = clampColor(resolved);
                } catch (...) {
                }
            }

            SwString borderWidth = sheet->getStyleProperty(selector.toStdString(), "border-width");
            if (!borderWidth.isEmpty()) {
                outWidth = clampInt(parsePixelValue(borderWidth, outWidth), 0, 20);
            }

            SwString borderRadius = sheet->getStyleProperty(selector.toStdString(), "border-radius");
            if (!borderRadius.isEmpty()) {
                outRadius = clampInt(parsePixelValue(borderRadius, outRadius), 0, 32);
            }
        }
    }

    void resolveBorderCornerRadii(const StyleSheet* sheet,
                                  int& outTopLeft,
                                  int& outTopRight,
                                  int& outBottomRight,
                                  int& outBottomLeft) const {
        if (!sheet) {
            return;
        }

        auto selectors = classHierarchy();
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

        for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
            const SwString& selector = selectors[i];
            if (selector.isEmpty()) {
                continue;
            }

            SwString v = sheet->getStyleProperty(selector.toStdString(), "border-top-left-radius");
            if (!v.isEmpty()) {
                outTopLeft = clampInt(parsePixelValue(v, outTopLeft), 0, 32);
            }
            v = sheet->getStyleProperty(selector.toStdString(), "border-top-right-radius");
            if (!v.isEmpty()) {
                outTopRight = clampInt(parsePixelValue(v, outTopRight), 0, 32);
            }
            v = sheet->getStyleProperty(selector.toStdString(), "border-bottom-right-radius");
            if (!v.isEmpty()) {
                outBottomRight = clampInt(parsePixelValue(v, outBottomRight), 0, 32);
            }
            v = sheet->getStyleProperty(selector.toStdString(), "border-bottom-left-radius");
            if (!v.isEmpty()) {
                outBottomLeft = clampInt(parsePixelValue(v, outBottomLeft), 0, 32);
            }
        }
    }

    void paintChildren(PaintEvent* event) {
        const SwRect& paintRect = event->paintRect();
        const std::vector<SwObject*>& directChildren = getChildren();
        for (SwObject* objChild : directChildren) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }
            const SwRect childRect = child->getRect();
            if (rectsIntersect(paintRect, childRect)) {
                static_cast<SwWidgetInterface*>(child)->paintEvent(event);
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
