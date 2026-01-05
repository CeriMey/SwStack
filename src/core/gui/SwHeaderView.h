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
 * SwHeaderView - Qt-like header view (≈ QHeaderView).
 *
 * Scope (v1):
 * - Horizontal/vertical headers backed by a SwAbstractItemModel (headerData()).
 * - Section sizing (fixed + optional "fit to width" using stretch weights).
 * - Interactive resizing (drag section dividers).
 * - Clickable sections (emits sectionClicked) and sort indicator helpers.
 **************************************************************************************************/

#include "SwAbstractItemModel.h"
#include "SwPainter.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"

#include "core/types/SwList.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

class SwHeaderView : public SwWidget {
    SW_OBJECT(SwHeaderView, SwWidget)

public:
    explicit SwHeaderView(SwOrientation orientation, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation) {
        initDefaults_();
    }

    void setSectionCountHint(int count) {
        count = std::max(0, count);
        if (m_sectionCountHint == count) {
            return;
        }
        m_sectionCountHint = count;
        ensureSectionBuffers_();
        updateStretchedSections_();
        update();
    }

    int sectionCountHint() const { return m_sectionCountHint; }

    void setModel(SwAbstractItemModel* model) {
        if (m_model == model) {
            ensureSectionBuffers_();
            update();
            return;
        }
        if (m_model) {
            SwObject::disconnect(m_model, this);
        }
        m_model = model;
        ensureSectionBuffers_();
        if (m_model) {
            SwObject::connect(m_model, &SwAbstractItemModel::modelReset, this, [this]() {
                ensureSectionBuffers_();
                update();
            });
        }
        update();
    }

    SwAbstractItemModel* model() const { return m_model; }

    void setOffset(int px) {
        px = std::max(0, px);
        if (m_offset == px) {
            return;
        }
        m_offset = px;
        update();
    }

    int offset() const { return m_offset; }

    void setDefaultSectionSize(int px) {
        px = clampInt_(px, 24, 10000);
        if (m_defaultSectionSize == px) {
            return;
        }
        m_defaultSectionSize = px;
        ensureSectionBuffers_();
        update();
    }

    int defaultSectionSize() const { return m_defaultSectionSize; }

    void setMinimumSectionSize(int px) {
        px = clampInt_(px, 12, 10000);
        if (m_minSectionSize == px) {
            return;
        }
        m_minSectionSize = px;
        if (m_maxSectionSize < m_minSectionSize) {
            m_maxSectionSize = m_minSectionSize;
        }
        update();
    }

    int minimumSectionSize() const { return m_minSectionSize; }

    void setMaximumSectionSize(int px) {
        px = clampInt_(px, 12, 50000);
        if (m_maxSectionSize == px) {
            return;
        }
        m_maxSectionSize = px;
        if (m_maxSectionSize < m_minSectionSize) {
            m_minSectionSize = m_maxSectionSize;
        }
        update();
    }

    int maximumSectionSize() const { return m_maxSectionSize; }

    void setShowGrid(bool on) {
        if (m_showGrid == on) {
            return;
        }
        m_showGrid = on;
        update();
    }

    bool showGrid() const { return m_showGrid; }

    void setSectionsClickable(bool on) { m_sectionsClickable = on; }
    bool sectionsClickable() const { return m_sectionsClickable; }

    void setSectionsFitToWidth(bool on) {
        if (m_sectionsFitToWidth == on) {
            return;
        }
        m_sectionsFitToWidth = on;
        if (!m_sectionsFitToWidth) {
            m_stretchedSectionSizes.clear();
        }
        updateStretchedSections_();
        update();
    }

    bool sectionsFitToWidth() const { return m_sectionsFitToWidth; }

    // Qt-like behavior: if true, the last section expands to consume remaining space (no scaling/shrinking).
    void setStretchLastSection(bool on) {
        if (m_stretchLastSection == on) {
            return;
        }
        m_stretchLastSection = on;
        update();
    }

    bool stretchLastSection() const { return m_stretchLastSection; }

    void setSectionStretches(const SwList<int>& stretches) {
        m_sectionStretches = stretches;
        updateStretchedSections_();
        update();
    }

    SwList<int> sectionStretches() const { return m_sectionStretches; }

    void setViewportLength(int px) {
        px = std::max(0, px);
        if (m_cachedViewportLength == px) {
            return;
        }
        m_cachedViewportLength = px;
        updateStretchedSections_();
        update();
    }

    int viewportLength() const { return m_cachedViewportLength; }

    int sectionCount() const {
        if (!m_model) {
            return std::max(0, m_sectionCountHint);
        }
        if (m_orientation == SwOrientation::Horizontal) {
            return std::max(0, m_model->columnCount());
        }
        return std::max(0, m_model->rowCount());
    }

    int sectionSize(int logicalIndex) const {
        if (logicalIndex < 0) {
            return m_defaultSectionSize;
        }
        ensureSectionBuffers_();

        const int n = sectionCount();
        if (n <= 0) {
            return m_defaultSectionSize;
        }

        const size_t idx = static_cast<size_t>(logicalIndex);
        if (m_sectionsFitToWidth && idx < m_stretchedSectionSizes.size()) {
            const int w = m_stretchedSectionSizes[idx];
            if (w > 0) {
                return w;
            }
        }

        auto fixedSizeFor_ = [this](int section) -> int {
            if (section < 0) {
                return m_defaultSectionSize;
            }
            const size_t sidx = static_cast<size_t>(section);
            if (sidx < m_sectionSizes.size()) {
                const int w = m_sectionSizes[sidx];
                if (w > 0) {
                    return w;
                }
            }
            return m_defaultSectionSize;
        };

        int base = fixedSizeFor_(logicalIndex);

        // If not in "fit to width" mode, optionally expand the last section to avoid a trailing blank area.
        if (!m_sectionsFitToWidth && m_stretchLastSection && logicalIndex == (n - 1)) {
            const int available = std::max(0,
                                           m_cachedViewportLength >= 0
                                               ? m_cachedViewportLength
                                               : (m_orientation == SwOrientation::Horizontal ? width() : height()));
            if (available > 0) {
                int total = 0;
                for (int i = 0; i < n; ++i) {
                    total += fixedSizeFor_(i);
                }
                if (total < available) {
                    base += (available - total);
                }
            }
        }

        return base;
    }

    void setSectionSize(int logicalIndex, int px) {
        if (logicalIndex < 0) {
            return;
        }
        if (!m_model && logicalIndex >= m_sectionCountHint) {
            m_sectionCountHint = logicalIndex + 1;
        }
        ensureSectionBuffers_();
        const size_t idx = static_cast<size_t>(logicalIndex);
        if (idx >= m_sectionSizes.size()) {
            return;
        }
        if (px <= 0) {
            px = 0; // 0 means "use default section size"
        } else {
            px = clampInt_(px, m_minSectionSize, m_maxSectionSize);
        }
        if (m_sectionSizes[idx] == px && !m_sectionsFitToWidth) {
            return;
        }

        // In stretch mode, manual resizing switches back to fixed sizes using the current stretched sizes.
        if (m_sectionsFitToWidth) {
            materializeStretchedIntoFixed_();
            m_sectionsFitToWidth = false;
            m_stretchedSectionSizes.clear();
        }

        const int oldSize = m_sectionSizes[idx];
        m_sectionSizes[idx] = px;
        sectionResized(logicalIndex, oldSize, px);
        update();
    }

    int length() const {
        const int n = sectionCount();
        int total = 0;
        for (int i = 0; i < n; ++i) {
            total += sectionSize(i);
        }
        return total;
    }

    int sectionStart(int logicalIndex) const {
        if (logicalIndex <= 0) {
            return 0;
        }
        const int n = sectionCount();
        int x = 0;
        for (int i = 0; i < n && i < logicalIndex; ++i) {
            x += sectionSize(i);
        }
        return x;
    }

    int sectionAt(int position) const {
        if (position < 0) {
            return -1;
        }
        const int n = sectionCount();
        int x = 0;
        for (int i = 0; i < n; ++i) {
            const int w = sectionSize(i);
            if (position < x + w) {
                return i;
            }
            x += w;
        }
        return -1;
    }

    void setSortIndicatorShown(bool on) {
        if (m_sortIndicatorShown == on) {
            return;
        }
        m_sortIndicatorShown = on;
        update();
    }

    bool isSortIndicatorShown() const { return m_sortIndicatorShown; }

    void setSortIndicator(int section, SwSortOrder order) {
        if (m_sortSection == section && m_sortOrder == order) {
            return;
        }
        m_sortSection = section;
        m_sortOrder = order;
        update();
    }

    int sortIndicatorSection() const { return m_sortSection; }
    SwSortOrder sortIndicatorOrder() const { return m_sortOrder; }

signals:
    DECLARE_SIGNAL(sectionClicked, int);
    DECLARE_SIGNAL(sectionResized, int, int, int);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateStretchedSections_();
    }

    void paintEvent(PaintEvent* event) override {
        if (!event || !event->painter() || !isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event->painter();
        const SwRect bounds = getRect();
        if (bounds.width <= 0 || bounds.height <= 0) {
            return;
        }

        const StyleSheet* sheet = getToolSheet();
        const auto hierarchy = classHierarchy(); // most-derived first

        SwColor bg{248, 250, 252};
        SwColor border{226, 232, 240};
        SwColor textColor{15, 23, 42};
        SwColor divider{226, 232, 240};
        SwColor indicator{100, 116, 139};

        int borderWidth = 1;
        int radius = 12;
        int tl = radius;
        int tr = radius;
        int br = 0;
        int bl = 0;

        Padding padding{0, 10, 0, 10};

        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "background-color"), bg);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "border-color"), border);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "color"), textColor);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "divider-color"), divider);
        (void)tryParseColor_(sheet, styleValue_(sheet, hierarchy, "indicator-color"), indicator);

        SwString bw = styleValue_(sheet, hierarchy, "border-width");
        if (!bw.isEmpty()) {
            borderWidth = clampInt_(parsePixelValue_(bw, borderWidth), 0, 20);
        }

        SwString brv = styleValue_(sheet, hierarchy, "border-radius");
        if (!brv.isEmpty()) {
            radius = clampInt_(parsePixelValue_(brv, radius), 0, 64);
        }
        tl = tr = br = bl = radius;

        SwString v = styleValue_(sheet, hierarchy, "border-top-left-radius");
        if (!v.isEmpty()) tl = clampInt_(parsePixelValue_(v, tl), 0, 64);
        v = styleValue_(sheet, hierarchy, "border-top-right-radius");
        if (!v.isEmpty()) tr = clampInt_(parsePixelValue_(v, tr), 0, 64);
        v = styleValue_(sheet, hierarchy, "border-bottom-right-radius");
        if (!v.isEmpty()) br = clampInt_(parsePixelValue_(v, br), 0, 64);
        v = styleValue_(sheet, hierarchy, "border-bottom-left-radius");
        if (!v.isEmpty()) bl = clampInt_(parsePixelValue_(v, bl), 0, 64);

        SwString pad = styleValue_(sheet, hierarchy, "padding");
        if (!pad.isEmpty()) {
            padding = parsePadding_(pad);
        }
        v = styleValue_(sheet, hierarchy, "padding-left");
        if (!v.isEmpty()) padding.left = clampInt_(parsePixelValue_(v, padding.left), 0, 128);
        v = styleValue_(sheet, hierarchy, "padding-right");
        if (!v.isEmpty()) padding.right = clampInt_(parsePixelValue_(v, padding.right), 0, 128);
        v = styleValue_(sheet, hierarchy, "padding-top");
        if (!v.isEmpty()) padding.top = clampInt_(parsePixelValue_(v, padding.top), 0, 128);
        v = styleValue_(sheet, hierarchy, "padding-bottom");
        if (!v.isEmpty()) padding.bottom = clampInt_(parsePixelValue_(v, padding.bottom), 0, 128);

        // Header background
        paintRoundedRectWithCorners_(painter, bounds, tl, tr, br, bl, bg, border, borderWidth);

        if (!m_model) {
            return;
        }

        const int n = sectionCount();
        if (n <= 0) {
            return;
        }

        const SwFont font = getFont();

        const int off = std::max(0, m_offset);
        const int headerH = bounds.height;

        int posContent = 0;
        for (int s = 0; s < n; ++s) {
            const int sz = sectionSize(s);
            const int pos = (m_orientation == SwOrientation::Horizontal)
                                ? (bounds.x + posContent - off)
                                : (bounds.y + posContent - off);

            SwRect cell = bounds;
            if (m_orientation == SwOrientation::Horizontal) {
                cell.x = pos;
                cell.width = sz;
            } else {
                cell.y = pos;
                cell.height = sz;
            }

            const bool visible =
                (m_orientation == SwOrientation::Horizontal)
                    ? !(cell.x + cell.width < bounds.x || cell.x > bounds.x + bounds.width)
                    : !(cell.y + cell.height < bounds.y || cell.y > bounds.y + bounds.height);

            if (!visible) {
                posContent += sz;
                continue;
            }

            SwAny hd = m_model->headerData(s, m_orientation, SwItemDataRole::DisplayRole);
            SwString label = hd.toString();
            if (label.isEmpty()) {
                label = (m_orientation == SwOrientation::Horizontal)
                            ? SwString("Column %1").arg(SwString::number(s + 1))
                            : SwString::number(s + 1);
            }

            const bool showSort = m_sortIndicatorShown && (m_sortSection == s) && (m_orientation == SwOrientation::Horizontal);
            const int sortReserve = showSort ? 18 : 0;

            SwRect textRect = cell;
            textRect.x += padding.left;
            textRect.y += padding.top;
            textRect.width = std::max(0, textRect.width - padding.left - padding.right - sortReserve);
            textRect.height = std::max(0, textRect.height - padding.top - padding.bottom);
            painter->drawText(textRect,
                              label,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              font);

            if (showSort) {
                const SwColor arrowColor = indicator;
                const int cx = cell.x + cell.width - padding.right - 8;
                const int cy = cell.y + headerH / 2;
                const int a = 5;
                if (m_sortOrder == SwSortOrder::AscendingOrder) {
                    painter->drawLine(cx - a, cy + 2, cx, cy - 3, arrowColor, 2);
                    painter->drawLine(cx + a, cy + 2, cx, cy - 3, arrowColor, 2);
                } else {
                    painter->drawLine(cx - a, cy - 2, cx, cy + 3, arrowColor, 2);
                    painter->drawLine(cx + a, cy - 2, cx, cy + 3, arrowColor, 2);
                }
            }

            if (m_showGrid && m_orientation == SwOrientation::Horizontal) {
                painter->drawLine(cell.x + cell.width, cell.y + 6, cell.x + cell.width, cell.y + cell.height - 6, divider, 1);
            }

            posContent += sz;
        }
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event || !isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        if (event->button() != SwMouseButton::Left) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const int divider = dividerAt_(event->x(), event->y());
        if (divider >= 0) {
            beginResize_(divider, event->x(), event->y());
            event->accept();
            return;
        }

        if (!m_sectionsClickable) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const int section = sectionAtPoint_(event->x(), event->y());
        if (section >= 0) {
            sectionClicked(section);
            event->accept();
            return;
        }

        SwWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(MouseEvent* event) override {
        SwWidget::mouseMoveEvent(event);
        if (!event) {
            return;
        }

        if (m_resizing && m_resizeSection >= 0) {
            const int contentPos = contentPos_(event->x(), event->y());
            const int delta = contentPos - m_resizeStartPos;
            const int newSize = clampInt_(m_resizeStartSize + delta, m_minSectionSize, m_maxSectionSize);
            setSectionSize(m_resizeSection, newSize);
            event->accept();
            return;
        }

        const int section = sectionAtPoint_(event->x(), event->y());
        if (section != m_hoverSection) {
            m_hoverSection = section;
            SwString tip;
            if (m_model && section >= 0) {
                tip = m_model->headerData(section, m_orientation, SwItemDataRole::ToolTipRole).toString();
            }
            setToolTips(tip);
        }

        const int divider = dividerAt_(event->x(), event->y());
        if (divider >= 0) {
            setCursor(m_orientation == SwOrientation::Horizontal ? CursorType::SizeWE : CursorType::SizeNS);
            event->accept();
            return;
        }

        setCursor(CursorType::Arrow);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        SwWidget::mouseReleaseEvent(event);
        if (!event) {
            return;
        }
        m_resizing = false;
        m_resizeSection = -1;
    }

private:
    static int clampInt_(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static int parsePixelValue_(const SwString& value, int defaultValue) {
        if (value.isEmpty()) {
            return defaultValue;
        }
        std::string s = value.toStdString();
        size_t pos = s.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        try {
            return std::stoi(s);
        } catch (...) {
            return defaultValue;
        }
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

    static SwColor clampColor_(const SwColor& c) {
        return SwColor{clampInt_(c.r, 0, 255), clampInt_(c.g, 0, 255), clampInt_(c.b, 0, 255)};
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

    void initDefaults_() {
        setFont(SwFont(L"Segoe UI", 9, SemiBold));
        setStyleSheet(R"(
            SwHeaderView {
                background-color: rgb(248, 250, 252);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-top-left-radius: 12px;
                border-top-right-radius: 12px;
                border-bottom-left-radius: 0px;
                border-bottom-right-radius: 0px;
                padding: 0px 10px;
                color: rgb(15, 23, 42);
                divider-color: rgb(226, 232, 240);
                indicator-color: rgb(100, 116, 139);
            }
        )");
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setCursor(CursorType::Arrow);
    }

    int contentPos_(int px, int py) const {
        const SwRect r = getRect();
        if (m_orientation == SwOrientation::Horizontal) {
            return (px - r.x) + std::max(0, m_offset);
        }
        return (py - r.y) + std::max(0, m_offset);
    }

    int dividerAt_(int px, int py) const {
        if (!m_model) {
            return -1;
        }
        const SwRect r = getRect();
        if (r.width <= 0 || r.height <= 0) {
            return -1;
        }

        const int n = sectionCount();
        if (n <= 0) {
            return -1;
        }

        const int pos = contentPos_(px, py);
        const int grab = 4;

        int x = 0;
        for (int s = 0; s < n; ++s) {
            const int w = sectionSize(s);
            x += w;
            if (std::abs(pos - x) <= grab) {
                return s;
            }
        }
        return -1;
    }

    int sectionAtPoint_(int px, int py) const {
        if (!m_model) {
            return -1;
        }
        const int pos = contentPos_(px, py);
        return sectionAt(pos);
    }

    void beginResize_(int section, int px, int py) {
        ensureSectionBuffers_();
        m_resizing = true;
        m_resizeSection = section;
        m_resizeStartPos = contentPos_(px, py);
        m_resizeStartSize = sectionSize(section);
    }

    void ensureSectionBuffers_() const {
        const int n = sectionCount();
        if (n <= 0) {
            m_sectionSizes.clear();
            m_stretchedSectionSizes.clear();
            return;
        }
        while (static_cast<int>(m_sectionSizes.size()) < n) {
            m_sectionSizes.append(0);
        }
        while (static_cast<int>(m_sectionSizes.size()) > n) {
            m_sectionSizes.removeAt(m_sectionSizes.size() - 1);
        }
        if (!m_sectionsFitToWidth) {
            return;
        }
        while (static_cast<int>(m_stretchedSectionSizes.size()) < n) {
            m_stretchedSectionSizes.append(m_defaultSectionSize);
        }
        while (static_cast<int>(m_stretchedSectionSizes.size()) > n) {
            m_stretchedSectionSizes.removeAt(m_stretchedSectionSizes.size() - 1);
        }
    }

    void materializeStretchedIntoFixed_() {
        ensureSectionBuffers_();
        if (m_stretchedSectionSizes.size() == 0) {
            return;
        }
        const int n = sectionCount();
        for (int i = 0; i < n; ++i) {
            const size_t idx = static_cast<size_t>(i);
            if (idx < m_sectionSizes.size() && idx < m_stretchedSectionSizes.size()) {
                m_sectionSizes[idx] = clampInt_(m_stretchedSectionSizes[idx], m_minSectionSize, m_maxSectionSize);
            }
        }
    }

    void updateStretchedSections_() const {
        if (!m_sectionsFitToWidth) {
            return;
        }
        ensureSectionBuffers_();
        if (!m_model) {
            return;
        }
        const int n = sectionCount();
        if (n <= 0) {
            m_stretchedSectionSizes.clear();
            return;
        }

        const int available = std::max(0, m_cachedViewportLength >= 0 ? m_cachedViewportLength
                                                                      : (m_orientation == SwOrientation::Horizontal ? width() : height()));
        if (available <= 0) {
            return;
        }

        bool hasAnyStretch = false;
        for (int i = 0; i < n; ++i) {
            const size_t idx = static_cast<size_t>(i);
            if (idx < m_sectionStretches.size() && m_sectionStretches[idx] > 0) {
                hasAnyStretch = true;
                break;
            }
        }

        SwList<int> weights;
        weights.reserve(static_cast<size_t>(n));
        long long totalWeight = 0;
        if (hasAnyStretch) {
            for (int i = 0; i < n; ++i) {
                const size_t idx = static_cast<size_t>(i);
                const int w = (idx < m_sectionStretches.size() && m_sectionStretches[idx] > 0) ? m_sectionStretches[idx] : 1;
                weights.append(w);
                totalWeight += w;
            }
        } else {
            for (int i = 0; i < n; ++i) {
                const int w = std::max(1, sectionSize(i));
                weights.append(w);
                totalWeight += w;
            }
        }

        if (totalWeight <= 0) {
            totalWeight = n;
            weights.clear();
            for (int i = 0; i < n; ++i) {
                weights.append(1);
            }
        }

        int totalPx = 0;
        for (int i = 0; i < n; ++i) {
            const long long w = static_cast<long long>(weights[static_cast<size_t>(i)]);
            int px = static_cast<int>((static_cast<long long>(available) * w) / totalWeight);
            px = std::max(m_minSectionSize, px);
            px = std::min(m_maxSectionSize, px);
            m_stretchedSectionSizes[static_cast<size_t>(i)] = px;
            totalPx += px;
        }

        if (totalPx < available && n > 0) {
            m_stretchedSectionSizes[static_cast<size_t>(n - 1)] += (available - totalPx);
            totalPx = available;
        }

        if (totalPx > available && n > 0) {
            int delta = totalPx - available;
            for (int i = n - 1; i >= 0 && delta > 0; --i) {
                const size_t idx = static_cast<size_t>(i);
                const int currentW = m_stretchedSectionSizes[idx];
                const int reducible = currentW - m_minSectionSize;
                if (reducible <= 0) {
                    continue;
                }
                const int reduce = std::min(reducible, delta);
                m_stretchedSectionSizes[idx] = currentW - reduce;
                delta -= reduce;
            }
        }
    }

    SwOrientation m_orientation{SwOrientation::Horizontal};
    SwAbstractItemModel* m_model{nullptr};

    bool m_showGrid{true};
    bool m_sectionsClickable{true};

    int m_offset{0};
    int m_defaultSectionSize{160};
    int m_minSectionSize{24};
    int m_maxSectionSize{10000};

    bool m_sectionsFitToWidth{false};
    bool m_stretchLastSection{true};
    mutable int m_cachedViewportLength{-1};
    mutable SwList<int> m_sectionSizes;
    SwList<int> m_sectionStretches;
    mutable SwList<int> m_stretchedSectionSizes;

    int m_sectionCountHint{0};

    bool m_sortIndicatorShown{false};
    int m_sortSection{-1};
    SwSortOrder m_sortOrder{SwSortOrder::AscendingOrder};

    bool m_resizing{false};
    int m_resizeSection{-1};
    int m_resizeStartPos{0};
    int m_resizeStartSize{0};

    int m_hoverSection{-1};
};
