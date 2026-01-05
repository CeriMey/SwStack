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

#include "SwFrame.h"
#include "SwToolButton.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

class SwToolBox : public SwFrame {
    SW_OBJECT(SwToolBox, SwFrame)

public:
    explicit SwToolBox(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        setFrameShape(SwFrame::Shape::Box);
        setStyleSheet(R"(
            SwToolBox {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 12px;
            }
        )");
        m_headerStyleSheet = defaultHeaderStyleSheet_();
    }

    int count() const { return static_cast<int>(m_items.size()); }

    void setContentBasedLayout(bool on) {
        if (m_contentBasedLayout == on) {
            return;
        }
        m_contentBasedLayout = on;
        updateLayout_();
        update();
    }

    bool contentBasedLayout() const { return m_contentBasedLayout; }

    int contentHeightHint() const { return computeContentHeight_(); }

    void refreshLayout() {
        updateLayout_();
        update();
    }

    void setHeaderStyleSheet(const SwString& styleSheet) {
        SwString next = styleSheet;
        if (next.isEmpty()) {
            next = defaultHeaderStyleSheet_();
        }
        if (m_headerStyleSheet == next) {
            return;
        }
        m_headerStyleSheet = next;
        for (auto& it : m_items) {
            it.appliedHeaderStyle.clear();
        }
        updateLayout_();
        update();
    }

    SwString headerStyleSheet() const { return m_headerStyleSheet; }

    void setContentsMargin(int margin) {
        const int clamped = std::max(0, margin);
        if (m_contentsMargin == clamped) {
            return;
        }
        m_contentsMargin = clamped;
        updateLayout_();
        update();
    }

    int contentsMargin() const { return m_contentsMargin; }

    void setSpacing(int spacing) {
        const int clamped = std::max(0, spacing);
        if (m_spacing == clamped) {
            return;
        }
        m_spacing = clamped;
        updateLayout_();
        update();
    }

    int spacing() const { return m_spacing; }

    void setExclusive(bool on) {
        if (m_exclusive == on) {
            return;
        }
        m_exclusive = on;

        if (m_exclusive) {
            int next = m_currentIndex;
            if (next < 0 || next >= static_cast<int>(m_items.size())) {
                next = -1;
            }
            if (next < 0) {
                for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                    if (m_items[static_cast<size_t>(i)].expanded) {
                        next = i;
                        break;
                    }
                }
            }
            if (next < 0 && !m_items.empty()) {
                next = 0;
            }
            setCurrentIndex(next);
        } else {
            for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                m_items[static_cast<size_t>(i)].expanded = (i == m_currentIndex);
            }
            updateLayout_();
            update();
        }
    }

    bool isExclusive() const { return m_exclusive; }

    int addItem(SwWidget* page, const SwString& title) { return insertItem(count(), page, title); }

    int insertItem(int index, SwWidget* page, const SwString& title) {
        if (!page) {
            return -1;
        }

        if (index < 0) {
            index = 0;
        }
        if (index > count()) {
            index = count();
        }

        auto* header = new SwToolBoxHeaderButton(title, this);
        header->setCheckable(true);
        header->setChecked(false);
        header->setFocusPolicy(FocusPolicyEnum::NoFocus);
        header->resize(120, m_headerHeight);
        header->setStyleSheet(m_headerStyleSheet);

        page->setParent(this);
        page->hide();

        Item item;
        item.header = header;
        item.page = page;
        item.expanded = false;
        item.basePageStyle = page->getStyleSheet();
        item.appliedPageStyle = item.basePageStyle;
        m_items.insert(m_items.begin() + index, item);

        SwObject::connect(header, &SwToolButton::clicked, this, [this, header](bool checked) {
            const int idx = indexOfHeader_(header);
            if (idx < 0) {
                return;
            }
            if (m_exclusive) {
                setCurrentIndex(idx);
            } else {
                setItemExpanded(idx, checked);
            }
        });

        // Keep currentIndex stable when inserting before it.
        if (m_currentIndex >= index) {
            ++m_currentIndex;
        }

        // Auto-expand the first page if the toolbox was empty.
        if (m_currentIndex < 0) {
            if (m_exclusive) {
                setCurrentIndex(0);
            } else {
                setItemExpanded(0, true);
            }
        } else {
            updateLayout_();
            update();
        }

        return index;
    }

    int currentIndex() const { return m_currentIndex; }

    void setCurrentIndex(int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
            idx = -1;
        }
        if (m_currentIndex == idx) {
            updateLayout_();
            update();
            return;
        }
        m_currentIndex = idx;
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            m_items[static_cast<size_t>(i)].expanded = (i == m_currentIndex);
        }
        currentChanged(m_currentIndex);
        updateLayout_();
        update();
    }

    bool isItemExpanded(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
            return false;
        }
        return m_items[static_cast<size_t>(idx)].expanded;
    }

    void setItemExpanded(int idx, bool expanded) {
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
            return;
        }

        if (m_exclusive) {
            setCurrentIndex(idx);
            return;
        }

        Item& it = m_items[static_cast<size_t>(idx)];
        if (it.expanded == expanded) {
            updateLayout_();
            update();
            return;
        }
        it.expanded = expanded;
        m_currentIndex = idx;
        updateLayout_();
        update();
    }

    void removeItem(int idx) {
        if (idx < 0 || idx >= count()) {
            return;
        }

        Item removed = m_items[static_cast<size_t>(idx)];

        if (removed.header) {
            removed.header->hide();
            delete removed.header;
            removed.header = nullptr;
        }
        if (removed.page) {
            removed.page->hide();
        }

        m_items.erase(m_items.begin() + idx);

        if (m_currentIndex == idx) {
            m_currentIndex = -1;
            if (m_exclusive && !m_items.empty()) {
                const int next = std::min(idx, count() - 1);
                setCurrentIndex(next);
                return;
            }
        } else if (m_currentIndex > idx) {
            --m_currentIndex;
        }

        updateLayout_();
        update();
    }

    int indexOf(const SwWidget* page) const {
        if (!page) {
            return -1;
        }
        for (int i = 0; i < count(); ++i) {
            if (m_items[static_cast<size_t>(i)].page == page) {
                return i;
            }
        }
        return -1;
    }

    SwWidget* currentWidget() const { return widget(m_currentIndex); }

    void setCurrentWidget(SwWidget* w) {
        const int idx = indexOf(w);
        if (idx < 0) {
            return;
        }
        setCurrentIndex(idx);
    }

    SwString itemText(int idx) const {
        auto* h = header(idx);
        return h ? h->getText() : SwString();
    }

    void setItemText(int idx, const SwString& text) {
        auto* h = header(idx);
        if (!h) {
            return;
        }
        h->setText(text);
        update();
    }

    void setItemEnabled(int idx, bool enabled) {
        Item* it = itemAt_(idx);
        if (!it) {
            return;
        }
        if (it->header) {
            it->header->setEnable(enabled);
        }
        if (it->page) {
            it->page->setEnable(enabled);
        }
        update();
    }

    bool isItemEnabled(int idx) const {
        const Item* it = itemAtConst_(idx);
        if (!it || !it->header) {
            return false;
        }
        return it->header->getEnable();
    }

    SwWidget* widget(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
            return nullptr;
        }
        return m_items[static_cast<size_t>(idx)].page;
    }

    SwToolButton* header(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
            return nullptr;
        }
        return m_items[static_cast<size_t>(idx)].header;
    }

signals:
    DECLARE_SIGNAL(currentChanged, int);
    DECLARE_SIGNAL(contentSizeChanged, int);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout_();
    }

    SwRect sizeHint() const override {
        SwRect hint = getRect();
        hint.height = computeContentHeight_();
        return hint;
    }

    SwRect minimumSizeHint() const override { return sizeHint(); }

private:
    class SwToolBoxHeaderButton final : public SwToolButton {
        SW_OBJECT(SwToolBoxHeaderButton, SwToolButton)

    public:
        explicit SwToolBoxHeaderButton(const SwString& text, SwWidget* parent = nullptr)
            : SwToolButton(text, parent) {
            setFocusPolicy(FocusPolicyEnum::NoFocus);
        }

    protected:
        static int clampInt_(int value, int minValue, int maxValue) {
            if (value < minValue) return minValue;
            if (value > maxValue) return maxValue;
            return value;
        }

        static SwColor clampColor_(const SwColor& c) {
            return SwColor{clampInt_(c.r, 0, 255), clampInt_(c.g, 0, 255), clampInt_(c.b, 0, 255)};
        }

        static SwColor lighten_(SwColor c, int amount) {
            c.r = clampInt_(c.r + amount, 0, 255);
            c.g = clampInt_(c.g + amount, 0, 255);
            c.b = clampInt_(c.b + amount, 0, 255);
            return c;
        }

        static SwColor darken_(SwColor c, int amount) {
            c.r = clampInt_(c.r - amount, 0, 255);
            c.g = clampInt_(c.g - amount, 0, 255);
            c.b = clampInt_(c.b - amount, 0, 255);
            return c;
        }

        static int parsePixelValue_(const SwString& value, int defaultValue) {
            if (value.isEmpty()) {
                return defaultValue;
            }
            SwString cleaned = value;
            cleaned.replace("px", "");
            bool ok = false;
            const int v = cleaned.toInt(&ok);
            return ok ? v : defaultValue;
        }

        struct Padding {
            int top{0};
            int right{0};
            int bottom{0};
            int left{0};
        };

        static Padding parsePadding_(const SwString& value) {
            Padding padding;
            if (value.isEmpty()) {
                return padding;
            }

            std::vector<std::string> tokens;
            std::istringstream ss(value.toStdString());
            std::string token;
            while (ss >> token) {
                tokens.push_back(token);
            }
            if (tokens.empty()) {
                return padding;
            }

            auto resolve = [](const std::string& t) -> int {
                std::string copy = t;
                size_t pos = copy.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
                if (pos != std::string::npos) {
                    copy = copy.substr(0, pos);
                }
                try {
                    return std::stoi(copy);
                } catch (...) {
                    return 0;
                }
            };

            if (tokens.size() == 1) {
                const int v = resolve(tokens[0]);
                padding.top = padding.right = padding.bottom = padding.left = v;
            } else if (tokens.size() == 2) {
                const int v = resolve(tokens[0]);
                const int h = resolve(tokens[1]);
                padding.top = padding.bottom = v;
                padding.left = padding.right = h;
            } else if (tokens.size() == 3) {
                padding.top = resolve(tokens[0]);
                padding.left = padding.right = resolve(tokens[1]);
                padding.bottom = resolve(tokens[2]);
            } else {
                padding.top = resolve(tokens[0]);
                padding.right = resolve(tokens[1]);
                padding.bottom = resolve(tokens[2]);
                padding.left = resolve(tokens[3]);
            }

            return padding;
        }

        static SwString styleValue_(const StyleSheet* sheet,
                                   const std::vector<SwString>& hierarchy,
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
                SwString v = sheet->getStyleProperty(selector.toStdString(), propertyName);
                if (!v.isEmpty()) {
                    out = v;
                }
            }
            return out;
        }

        static bool tryParseColor_(const StyleSheet* sheet, const SwString& value, SwColor& out) {
            if (!sheet || value.isEmpty()) {
                return false;
            }
            try {
                out = clampColor_(const_cast<StyleSheet*>(sheet)->parseColor(value.toStdString(), nullptr));
                return true;
            } catch (...) {
                return false;
            }
        }

        void paintEvent(PaintEvent* event) override {
            if (!isVisibleInHierarchy()) {
                return;
            }

            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }

            const SwRect bounds = getRect();
            const StyleSheet* sheet = getToolSheet();
            const auto hierarchy = classHierarchy(); // most-derived first

            // Defaults (Qt-ish neutral).
            SwColor bg{248, 250, 252};
            SwColor bgHover = lighten_(bg, 6);
            SwColor bgPressed = darken_(bg, 10);
            SwColor bgChecked{241, 245, 249};

            SwColor border{226, 232, 240};
            SwColor textColor{15, 23, 42};
            SwColor textColorDisabled{148, 163, 184};

            SwColor indicator{71, 85, 105};
            SwColor indicatorDisabled{148, 163, 184};

            int borderWidth = 1;
            int radius = 10;
            int tl = radius;
            int tr = radius;
            int br = radius;
            int bl = radius;
            Padding padding{6, 10, 6, 10};

            // Base style overrides.
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "background-color"), bg);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "background-color-hover"), bgHover);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "background-color-pressed"), bgPressed);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "background-color-checked"), bgChecked);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "border-color"), border);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "color"), textColor);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "color-disabled"), textColorDisabled);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "indicator-color"), indicator);
            (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "indicator-color-disabled"), indicatorDisabled);

            const SwString bw = styleValue_(sheet, hierarchy, "border-width");
            if (!bw.isEmpty()) {
                borderWidth = clampInt_(parsePixelValue_(bw, borderWidth), 0, 20);
            }

            const SwString brValue = styleValue_(sheet, hierarchy, "border-radius");
            if (!brValue.isEmpty()) {
                radius = clampInt_(parsePixelValue_(brValue, radius), 0, 64);
            }
            tl = tr = br = bl = radius;

            SwString v = styleValue_(sheet, hierarchy, "border-top-left-radius");
            if (!v.isEmpty()) {
                tl = clampInt_(parsePixelValue_(v, tl), 0, 64);
            }
            v = styleValue_(sheet, hierarchy, "border-top-right-radius");
            if (!v.isEmpty()) {
                tr = clampInt_(parsePixelValue_(v, tr), 0, 64);
            }
            v = styleValue_(sheet, hierarchy, "border-bottom-right-radius");
            if (!v.isEmpty()) {
                br = clampInt_(parsePixelValue_(v, br), 0, 64);
            }
            v = styleValue_(sheet, hierarchy, "border-bottom-left-radius");
            if (!v.isEmpty()) {
                bl = clampInt_(parsePixelValue_(v, bl), 0, 64);
            }

            const SwString pad = styleValue_(sheet, hierarchy, "padding");
            if (!pad.isEmpty()) {
                padding = parsePadding_(pad);
            }
            const SwString padLeft = styleValue_(sheet, hierarchy, "padding-left");
            if (!padLeft.isEmpty()) {
                padding.left = clampInt_(parsePixelValue_(padLeft, padding.left), 0, 64);
            }
            const SwString padRight = styleValue_(sheet, hierarchy, "padding-right");
            if (!padRight.isEmpty()) {
                padding.right = clampInt_(parsePixelValue_(padRight, padding.right), 0, 64);
            }

            // State tweaks.
            SwColor fill = bg;
            SwColor text = textColor;
            SwColor icon = indicator;

            if (!getEnable()) {
                fill = lighten_(bg, 2);
                text = textColorDisabled;
                icon = indicatorDisabled;
            } else if (getPressed()) {
                fill = bgPressed;
            } else if (getHover()) {
                fill = bgHover;
            }

            if (isChecked() && getEnable()) {
                fill = bgChecked;
            }

            paintRoundedRectWithCorners_(painter, bounds, tl, tr, br, bl, fill, border, borderWidth);

            // Chevron indicator (collapsed: right, expanded: down).
            const int indicatorSize = clampInt_(std::min(bounds.width, bounds.height) / 4, 8, 14);
            const int cx = bounds.x + padding.left + indicatorSize / 2;
            const int cy = bounds.y + bounds.height / 2;
            const int half = indicatorSize / 2;

            SwPoint pts[3];
            if (isChecked()) {
                pts[0] = SwPoint{cx - half, cy - indicatorSize / 4};
                pts[1] = SwPoint{cx + half, cy - indicatorSize / 4};
                pts[2] = SwPoint{cx, cy + half};
            } else {
                pts[0] = SwPoint{cx - indicatorSize / 4, cy - half};
                pts[1] = SwPoint{cx - indicatorSize / 4, cy + half};
                pts[2] = SwPoint{cx + half, cy};
            }
            painter->fillPolygon(pts, 3, icon, icon, 0);

            const int gap = 8;
            SwRect textRect = bounds;
            textRect.x = cx + half + gap;
            textRect.width = std::max(0, (bounds.x + bounds.width - padding.right) - textRect.x);
            textRect.y += padding.top;
            textRect.height = std::max(0, textRect.height - padding.top - padding.bottom);

            painter->drawText(textRect,
                              getText(),
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              text,
                              getFont());
        }

        static void paintRoundedRectWithCorners_(SwPainter* painter,
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
            r = clampInt_(r, 0, maxRadius);
            if (r <= 0) {
                painter->fillRect(rect, fill, border, borderWidth);
                return;
            }

            const bool ok =
                (tl == 0 || tl == r) && (tr == 0 || tr == r) && (br == 0 || br == r) && (bl == 0 || bl == r);
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
    };

    struct Item {
        SwToolButton* header{nullptr};
        SwWidget* page{nullptr};
        bool expanded{false};
        SwString appliedHeaderStyle;
        SwString basePageStyle;
        SwString appliedPageStyle;
    };

    int indexOfHeader_(const SwToolButton* header) const {
        if (!header) {
            return -1;
        }
        for (int i = 0; i < count(); ++i) {
            if (m_items[static_cast<size_t>(i)].header == header) {
                return i;
            }
        }
        return -1;
    }

    Item* itemAt_(int idx) {
        if (idx < 0 || idx >= count()) {
            return nullptr;
        }
        return &m_items[static_cast<size_t>(idx)];
    }

    const Item* itemAtConst_(int idx) const {
        if (idx < 0 || idx >= count()) {
            return nullptr;
        }
        return &m_items[static_cast<size_t>(idx)];
    }

    void updateLayout_() {
        const SwRect r = getRect();
        const int margin = std::max(0, m_contentsMargin);
        const SwRect inner{r.x + margin,
                           r.y + margin,
                           std::max(0, r.width - 2 * margin),
                           std::max(0, r.height - 2 * margin)};
        const int n = static_cast<int>(m_items.size());
        if (n <= 0) {
            return;
        }

        int expandedCount = 0;
        if (m_exclusive) {
            expandedCount = (m_currentIndex >= 0) ? 1 : 0;
        } else {
            for (const auto& it : m_items) {
                if (it.expanded) {
                    ++expandedCount;
                }
            }
        }

        int basePageH = 0;
        int remainder = 0;
        if (!m_contentBasedLayout) {
            const int pagesVisible = expandedCount;
            const int visibleCount = n + pagesVisible;
            const int gaps = std::max(0, visibleCount - 1);
            const int totalHeaders = n * m_headerHeight;
            const int totalSpacing = gaps * m_spacing;
            const int contentH = std::max(0, inner.height - totalHeaders - totalSpacing);

            basePageH = (expandedCount > 0) ? (contentH / expandedCount) : 0;
            remainder = (expandedCount > 0) ? (contentH % expandedCount) : 0;
        }

        int y = inner.y;

        const bool wantsEdgeOnlyRadii = (m_contentsMargin == 0 && m_spacing == 0);
        int toolboxRadius = 0;
        const int innerBottom = inner.y + inner.height;
        if (wantsEdgeOnlyRadii) {
            SwColor b{226, 232, 240};
            int bw = 1;
            toolboxRadius = 12;
            resolveBorder(getToolSheet(), b, bw, toolboxRadius);
            toolboxRadius = std::max(0, toolboxRadius);
        }

        for (int i = 0; i < n; ++i) {
            Item& it = m_items[static_cast<size_t>(i)];
            const bool showPage = m_exclusive ? (i == m_currentIndex) : it.expanded;
            if (it.page) {
                const SwString currentPageStyle = it.page->getStyleSheet();
                if (currentPageStyle != it.appliedPageStyle) {
                    it.basePageStyle = currentPageStyle;
                    it.appliedPageStyle = currentPageStyle;
                }
            }
            if (it.header) {
                it.header->move(inner.x, y);
                it.header->resize(inner.width, m_headerHeight);
                if (m_exclusive) {
                    it.header->setChecked(i == m_currentIndex);
                } else {
                    it.header->setChecked(it.expanded);
                }

                SwString css = m_headerStyleSheet;
                if (wantsEdgeOnlyRadii) {
                    const bool isTop = (i == 0) && (y <= inner.y);
                    const bool isBottom = (i == n - 1) && !showPage && ((y + m_headerHeight) >= innerBottom);
                    const int tl = isTop ? toolboxRadius : 0;
                    const int tr = isTop ? toolboxRadius : 0;
                    const int br = isBottom ? toolboxRadius : 0;
                    const int bl = isBottom ? toolboxRadius : 0;

                    SwString extra;
                    extra.append("\nSwToolButton { ");
                    extra.append("border-top-left-radius: ");
                    extra.append(SwString::number(tl));
                    extra.append("px; ");
                    extra.append("border-top-right-radius: ");
                    extra.append(SwString::number(tr));
                    extra.append("px; ");
                    extra.append("border-bottom-right-radius: ");
                    extra.append(SwString::number(br));
                    extra.append("px; ");
                    extra.append("border-bottom-left-radius: ");
                    extra.append(SwString::number(bl));
                    extra.append("px; ");
                    extra.append("}\n");
                    css = css + extra;
                }

                if (it.appliedHeaderStyle != css) {
                    it.header->setStyleSheet(css);
                    it.appliedHeaderStyle = css;
                }
            }
            y += m_headerHeight;

            if (!it.page) {
                if (i < n - 1) {
                    y += m_spacing;
                }
                continue;
            }
            if (!showPage) {
                it.page->hide();
                if (i < n - 1) {
                    y += m_spacing;
                }
                continue;
            }

            y += m_spacing;

            int pageH = 0;
            if (m_contentBasedLayout) {
                SwRect hint = it.page->sizeHint();
                pageH = std::max(0, hint.height);
            } else {
                pageH = basePageH;
                if (remainder > 0) {
                    ++pageH;
                    --remainder;
                }
            }
            it.page->move(inner.x, y);
            it.page->resize(inner.width, pageH);
            it.page->show();

            SwString pageCss = it.basePageStyle;
            if (wantsEdgeOnlyRadii) {
                const bool isBottomPage = (i == n - 1) && ((y + pageH) >= innerBottom);
                if (isBottomPage && it.page) {
                    SwString processed = pageCss;
                    if (!processed.contains("{")) {
                        processed = SwString("%1 { %2 }").arg(it.page->className()).arg(processed);
                    }

                    SwString extra;
                    extra.append("\n");
                    extra.append(it.page->className());
                    extra.append(" { ");
                    extra.append("border-top-left-radius: 0px; ");
                    extra.append("border-top-right-radius: 0px; ");
                    extra.append("border-bottom-right-radius: ");
                    extra.append(SwString::number(toolboxRadius));
                    extra.append("px; ");
                    extra.append("border-bottom-left-radius: ");
                    extra.append(SwString::number(toolboxRadius));
                    extra.append("px; ");
                    extra.append("}\n");
                    pageCss = processed + extra;
                }
            }

            if (it.appliedPageStyle != pageCss) {
                it.page->setStyleSheet(pageCss);
                it.appliedPageStyle = pageCss;
            }
            y += pageH;

            if (i < n - 1) {
                y += m_spacing;
            }
        }

        const int nextContentH = computeContentHeight_();
        if (nextContentH != m_cachedContentHeight) {
            m_cachedContentHeight = nextContentH;
            contentSizeChanged(nextContentH);
        }
    }

    int computeContentHeight_() const {
        if (!m_contentBasedLayout) {
            return getRect().height;
        }

        const int n = static_cast<int>(m_items.size());
        const int margin = std::max(0, m_contentsMargin);
        if (n <= 0) {
            return margin * 2;
        }

        int pageCount = 0;
        int pagesHeight = 0;
        for (int i = 0; i < n; ++i) {
            const Item& it = m_items[static_cast<size_t>(i)];
            const bool showPage = m_exclusive ? (i == m_currentIndex) : it.expanded;
            if (!showPage || !it.page) {
                continue;
            }
            ++pageCount;
            pagesHeight += std::max(0, it.page->sizeHint().height);
        }

        const int visibleCount = n + pageCount;
        const int gaps = std::max(0, visibleCount - 1);
        const int totalHeaders = n * m_headerHeight;
        const int totalSpacing = gaps * m_spacing;

        return std::max(0, margin * 2 + totalHeaders + totalSpacing + pagesHeight);
    }

    std::vector<Item> m_items;
    int m_currentIndex{-1};
    int m_headerHeight{34};
    bool m_exclusive{true};
    bool m_contentBasedLayout{false};
    int m_cachedContentHeight{-1};

    SwString m_headerStyleSheet;
    int m_contentsMargin{8};
    int m_spacing{6};

    static SwString defaultHeaderStyleSheet_() {
        return SwString(R"(
            SwToolButton {
                background-color: rgb(248, 250, 252);
                background-color-hover: rgb(241, 245, 249);
                background-color-pressed: rgb(226, 232, 240);
                background-color-checked: rgb(241, 245, 249);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 10px;
                color: rgb(15, 23, 42);
                color-disabled: rgb(148, 163, 184);
                indicator-color: rgb(71, 85, 105);
                indicator-color-disabled: rgb(148, 163, 184);
                padding: 6px 10px;
            }
        )");
    }
};
