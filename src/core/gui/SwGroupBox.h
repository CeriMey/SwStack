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
 * SwGroupBox - Qt-like group box (≈ QGroupBox).
 *
 * Notes:
 * - Optional checkable header (like QGroupBox::setCheckable).
 * - Does not automatically manage child layout margins; use a layout margin if needed.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwWidgetPlatformAdapter.h"

class SwGroupBox : public SwFrame {
    SW_OBJECT(SwGroupBox, SwFrame)

public:
    explicit SwGroupBox(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
    }

    SwGroupBox(const SwString& title, SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        setTitle(title);
    }

    void setTitle(const SwString& title) { setText(title); }
    SwString title() const { return getText(); }

    void setCheckable(bool on) {
        if (m_checkable == on) {
            return;
        }
        m_checkable = on;
        updateEnabledState();
        update();
    }

    bool isCheckable() const { return m_checkable; }

    void setChecked(bool checked) {
        if (m_checked == checked) {
            return;
        }
        m_checked = checked;
        updateEnabledState();
        toggled(m_checked);
        update();
    }

    bool isChecked() const { return m_checked; }

    void setAccentColor(const SwColor& color) {
        m_accent = clampColor(color);
        update();
    }

    SwColor accentColor() const { return m_accent; }

    SwRect contentsRect() const {
        const SwRect r = getRect();
        const int top = r.y + titleHeight();
        return SwRect{r.x + m_padding,
                      top + m_padding,
                      std::max(0, r.width - 2 * m_padding),
                      std::max(0, r.height - (top - r.y) - 2 * m_padding)};
    }

    DECLARE_SIGNAL(toggled, bool);

protected:
    CUSTOM_PROPERTY(SwString, Text, "GroupBox") { update(); }
    CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = getRect();

        StyleSheet* sheet = getToolSheet();
        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRoundedRect(rect, radius, bg, border, borderWidth);
        } else {
            painter->drawRect(rect, border, borderWidth);
        }

        // Header
        const int headerH = titleHeight();
        const int headerY = rect.y;
        const int headerX = rect.x + m_padding;

        SwRect indicator = checkIndicatorRect(rect);
        SwRect titleRect = titleTextRect(rect);

        // Break the frame line behind the title for a Qt-like look.
        if (paintBackground && bgAlpha > 0.0f) {
            SwRect patch = titleRect;
            patch.x -= 4;
            patch.width += 8;
            painter->fillRect(patch, bg, bg, 0);
            if (m_checkable) {
                SwRect patch2 = indicator;
                patch2.x -= 4;
                patch2.y -= 2;
                patch2.width += 8;
                patch2.height += 4;
                painter->fillRect(patch2, bg, bg, 0);
            }
        }

        if (m_checkable) {
            paintCheckIndicator(painter, indicator);
        }

        SwColor titleColor{24, 28, 36};
        if (!getEnable() || (m_checkable && !m_checked)) {
            titleColor = SwColor{150, 150, 150};
        }

        painter->drawText(titleRect,
                          getText(),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          titleColor,
                          getFont());

        // Children, clipped to the inside rect for a clean group box.
        const SwRect clip = SwRect{rect.x + 1, rect.y + headerH, rect.width - 2, rect.height - headerH - 1};
        painter->pushClipRect(clip);
        paintChildren(event);
        painter->popClipRect();

        painter->finalize();
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwFrame::mousePressEvent(event);
            return;
        }

        if (m_checkable) {
            const SwRect rect = getRect();
            const SwRect indicator = checkIndicatorRect(rect);
            const SwRect titleRect = titleTextRect(rect);
            if (containsPoint(indicator, event->x(), event->y()) ||
                containsPoint(titleRect, event->x(), event->y())) {
                setPressed(true);
                event->accept();
                return;
            }
        }

        SwFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const bool wasPressed = getPressed();
        setPressed(false);

        if (wasPressed && m_checkable) {
            const SwRect rect = getRect();
            const SwRect indicator = checkIndicatorRect(rect);
            const SwRect titleRect = titleTextRect(rect);
            if (containsPoint(indicator, event->x(), event->y()) ||
                containsPoint(titleRect, event->x(), event->y())) {
                setChecked(!m_checked);
                event->accept();
                return;
            }
        }

        SwFrame::mouseReleaseEvent(event);
    }

private:
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

    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    int titleHeight() const {
        return clampInt(m_headerHeight, 18, 32);
    }

    int titleTextWidthEstimate(const SwString& text) const {
        const int fallbackWidth = 8;
        return SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), text, getFont(), text.size(), fallbackWidth);
    }

    SwRect checkIndicatorRect(const SwRect& rect) const {
        if (!m_checkable) {
            return SwRect{0, 0, 0, 0};
        }
        const int s = m_checkSize;
        const int y = rect.y + (titleHeight() - s) / 2;
        return SwRect{rect.x + m_padding, y, s, s};
    }

    SwRect titleTextRect(const SwRect& rect) const {
        const int headerH = titleHeight();
        const int y = rect.y;
        int x = rect.x + m_padding;
        if (m_checkable) {
            x += m_checkSize + m_checkSpacing;
        }
        const int textW = clampInt(titleTextWidthEstimate(getText()) + 6, 30, rect.width - 2 * m_padding);
        return SwRect{x, y, textW, headerH};
    }

    void paintCheckIndicator(SwPainter* painter, const SwRect& indicator) const {
        if (!painter) {
            return;
        }

        SwColor border{160, 160, 160};
        SwColor fill{255, 255, 255};
        if (!getEnable()) {
            border = SwColor{190, 190, 190};
            fill = SwColor{245, 245, 245};
        } else if (getHover()) {
            border = SwColor{120, 120, 120};
        }

        painter->fillRoundedRect(indicator, 4, fill, border, 1);

        if (m_checked) {
            SwRect inner{indicator.x + 2, indicator.y + 2, indicator.width - 4, indicator.height - 4};
            painter->fillRoundedRect(inner, 3, m_accent, m_accent, 0);

            const SwColor tick{255, 255, 255};
            const int x1 = indicator.x + indicator.width / 4;
            const int y1 = indicator.y + indicator.height / 2;
            const int x2 = indicator.x + indicator.width / 2 - 1;
            const int y2 = indicator.y + indicator.height - indicator.height / 4;
            const int x3 = indicator.x + indicator.width - indicator.width / 4;
            const int y3 = indicator.y + indicator.height / 4;
            painter->drawLine(x1, y1, x2, y2, tick, 2);
            painter->drawLine(x2, y2, x3, y3, tick, 2);
        }
    }

    void updateEnabledState() {
        if (!m_checkable) {
            return;
        }
        for (SwWidget* child : findChildren<SwWidget>()) {
            if (!child || child == this) {
                continue;
            }
            child->setEnable(m_checked);
        }
    }

    void initDefaults() {
        setCursor(CursorType::Arrow);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setFrameShape(Shape::Box);
        setStyleSheet(R"(
            SwGroupBox {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
            }
        )");
    }

    bool m_checkable{false};
    bool m_checked{true};
    SwColor m_accent{0, 120, 215};
    int m_headerHeight{24};
    int m_padding{12};
    int m_checkSize{16};
    int m_checkSpacing{8};
};

