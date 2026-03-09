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
 * @file SwHeaderView.h
 * @ingroup core_gui
 * @brief Declares `SwHeaderView`, a custom header widget for model-backed views.
 *
 * @details
 * `SwHeaderView` renders horizontal or vertical section headers using data obtained from
 * `SwAbstractItemModel::headerData()`. It is designed as a lightweight companion for item views
 * that need explicit control over header painting, section metrics, and interaction policy.
 *
 * The implementation supports:
 * - fixed per-section sizes,
 * - optional width redistribution through stretch weights,
 * - optional expansion of the last visible section,
 * - interactive divider dragging,
 * - clickable sections for sorting or custom behaviors,
 * - sort indicator painting managed entirely by the toolkit.
 */

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

/**
 * @class SwHeaderView
 * @brief Paints and manages interactive header sections for a model-backed view.
 *
 * @details
 * The widget keeps enough layout state to answer geometry queries without depending on a specific
 * table or tree implementation. Section count may come from the bound model or from an explicit
 * hint when no model is attached yet. Size calculation follows this order:
 * - explicit per-section sizes when present,
 * - computed stretch sizes when "fit to width" mode is enabled,
 * - optional last-section expansion to consume remaining viewport space.
 *
 * Mouse interaction is also handled locally. Divider hit testing enables manual resizing, while
 * section hit testing emits `sectionClicked` and tracks hover state for richer painting.
 */
class SwHeaderView : public SwWidget {
    SW_OBJECT(SwHeaderView, SwWidget)

public:
    /**
     * @brief Constructs a header view for the requested orientation.
     * @param orientation `Horizontal` for column headers or `Vertical` for row headers.
     * @param parent Optional parent widget.
     */
    explicit SwHeaderView(SwOrientation orientation, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_orientation(orientation) {
        initDefaults_();
    }

    /**
     * @brief Sets a fallback number of sections when no model is attached.
     * @param count Non-negative logical section count hint.
     */
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

    /**
     * @brief Returns the explicit section-count hint.
     * @return Fallback count used when the widget has no model.
     */
    int sectionCountHint() const { return m_sectionCountHint; }

    /**
     * @brief Binds the header to a model that provides section count and header labels.
     * @param model Model whose row or column metadata should be rendered.
     *
     * @details
     * The header reconnects to `modelReset` so section buffers and cached geometry stay aligned with
     * model structure changes.
     */
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

    /**
     * @brief Returns the currently bound model.
     * @return Model pointer, or `nullptr` when no model is attached.
     */
    SwAbstractItemModel* model() const { return m_model; }

    /**
     * @brief Sets the scroll offset applied when mapping viewport positions to logical sections.
     * @param px Offset in pixels.
     */
    void setOffset(int px) {
        px = std::max(0, px);
        if (m_offset == px) {
            return;
        }
        m_offset = px;
        update();
    }

    /**
     * @brief Returns the current pixel offset.
     * @return Horizontal or vertical header offset depending on orientation.
     */
    int offset() const { return m_offset; }

    /**
     * @brief Sets the default size used for sections without an explicit override.
     * @param px Requested section size in pixels.
     */
    void setDefaultSectionSize(int px) {
        px = clampInt(px, 24, 10000);
        if (m_defaultSectionSize == px) {
            return;
        }
        m_defaultSectionSize = px;
        ensureSectionBuffers_();
        update();
    }

    /**
     * @brief Returns the default section size.
     * @return Default section extent in pixels.
     */
    int defaultSectionSize() const { return m_defaultSectionSize; }

    /**
     * @brief Sets the minimum size allowed for any section.
     * @param px Minimum size in pixels.
     */
    void setMinimumSectionSize(int px) {
        px = clampInt(px, 12, 10000);
        if (m_minSectionSize == px) {
            return;
        }
        m_minSectionSize = px;
        if (m_maxSectionSize < m_minSectionSize) {
            m_maxSectionSize = m_minSectionSize;
        }
        update();
    }

    /**
     * @brief Returns the minimum section size.
     * @return Minimum extent in pixels.
     */
    int minimumSectionSize() const { return m_minSectionSize; }

    /**
     * @brief Sets the maximum size allowed for any section.
     * @param px Maximum size in pixels.
     */
    void setMaximumSectionSize(int px) {
        px = clampInt(px, 12, 50000);
        if (m_maxSectionSize == px) {
            return;
        }
        m_maxSectionSize = px;
        if (m_maxSectionSize < m_minSectionSize) {
            m_minSectionSize = m_maxSectionSize;
        }
        update();
    }

    /**
     * @brief Returns the maximum section size.
     * @return Maximum extent in pixels.
     */
    int maximumSectionSize() const { return m_maxSectionSize; }

    /**
     * @brief Enables or disables grid-line painting between sections.
     * @param on `true` to draw divider lines.
     */
    void setShowGrid(bool on) {
        if (m_showGrid == on) {
            return;
        }
        m_showGrid = on;
        update();
    }

    /**
     * @brief Returns whether grid lines are painted.
     * @return `true` when section dividers are drawn as part of the header.
     */
    bool showGrid() const { return m_showGrid; }

    /**
     * @brief Enables or disables click activation on sections.
     * @param on `true` to emit `sectionClicked` when the user clicks a section body.
     */
    void setSectionsClickable(bool on) { m_sectionsClickable = on; }

    /**
     * @brief Returns whether section clicks are currently enabled.
     * @return `true` when click hit testing on section bodies is active.
     */
    bool sectionsClickable() const { return m_sectionsClickable; }

    /**
     * @brief Enables automatic redistribution of section sizes to fill the viewport length.
     * @param on `true` to compute stretched sizes from the configured weights.
     *
     * @details
     * When enabled, the fixed section-size buffer is treated as the baseline and
     * `updateStretchedSections_()` computes a derived set of visible sizes. Disabling the feature
     * clears the derived cache and returns to fixed sizing.
     */
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

    /**
     * @brief Returns whether width-fitting mode is enabled.
     * @return `true` when section sizes are redistributed to match the viewport length.
     */
    bool sectionsFitToWidth() const { return m_sectionsFitToWidth; }

    /**
     * @brief Enables or disables last-section expansion in non-stretched mode.
     * @param on `true` to let the last section absorb leftover viewport space.
     *
     * @details
     * Unlike `setSectionsFitToWidth()`, this does not rescale every section. Only the final visible
     * section grows when the sum of fixed sizes is smaller than the available viewport length.
     */
    void setStretchLastSection(bool on) {
        if (m_stretchLastSection == on) {
            return;
        }
        m_stretchLastSection = on;
        update();
    }

    /**
     * @brief Returns whether the last section may expand to fill slack space.
     * @return `true` when last-section stretching is enabled.
     */
    bool stretchLastSection() const { return m_stretchLastSection; }

    /**
     * @brief Sets per-section stretch weights used by width-fitting mode.
     * @param stretches Weight list aligned with logical section indices.
     */
    void setSectionStretches(const SwList<int>& stretches) {
        m_sectionStretches = stretches;
        updateStretchedSections_();
        update();
    }

    /**
     * @brief Returns the configured stretch weights.
     * @return Weight list used when width-fitting mode is active.
     */
    SwList<int> sectionStretches() const { return m_sectionStretches; }

    /**
     * @brief Sets the viewport length used for width redistribution calculations.
     * @param px Available viewport size in pixels.
     */
    void setViewportLength(int px) {
        px = std::max(0, px);
        if (m_cachedViewportLength == px) {
            return;
        }
        m_cachedViewportLength = px;
        updateStretchedSections_();
        update();
    }

    /**
     * @brief Returns the cached viewport length.
     * @return Viewport size used by stretch calculations, or a negative value when not preset.
     */
    int viewportLength() const { return m_cachedViewportLength; }

    /**
     * @brief Returns the current logical section count.
     * @return Number of columns for a horizontal header, rows for a vertical header, or the fallback
     *         hint when no model is attached.
     */
    int sectionCount() const {
        if (!m_model) {
            return std::max(0, m_sectionCountHint);
        }
        if (m_orientation == SwOrientation::Horizontal) {
            return std::max(0, m_model->columnCount());
        }
        return std::max(0, m_model->rowCount());
    }

    /**
     * @brief Returns the visible size of one logical section.
     * @param logicalIndex Zero-based section index.
     * @return Section extent in pixels after fixed-size and stretch rules have been applied.
     */
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

    /**
     * @brief Assigns an explicit size to one section.
     * @param logicalIndex Zero-based section index.
     * @param px Requested size in pixels.
     *
     * @details
     * In width-fitting mode the stretched cache is first materialized back into the fixed-size
     * buffer so the explicit user resize becomes the new baseline.
     */
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
            px = clampInt(px, m_minSectionSize, m_maxSectionSize);
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

    /**
     * @brief Returns the cumulative visible length of all sections.
     * @return Total header length in pixels.
     */
    int length() const {
        const int n = sectionCount();
        int total = 0;
        for (int i = 0; i < n; ++i) {
            total += sectionSize(i);
        }
        return total;
    }

    /**
     * @brief Returns the starting pixel coordinate of a section.
     * @param logicalIndex Zero-based section index.
     * @return Pixel coordinate relative to the unshifted header content.
     */
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

    /**
     * @brief Returns the logical section covering a content-space position.
     * @param position Pixel position relative to the header content.
     * @return Section index, or `-1` when the position is outside all sections.
     */
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

    /**
     * @brief Enables or disables sort-indicator painting.
     * @param on `true` to paint the current sort arrow.
     */
    void setSortIndicatorShown(bool on) {
        if (m_sortIndicatorShown == on) {
            return;
        }
        m_sortIndicatorShown = on;
        update();
    }

    /**
     * @brief Returns whether the sort indicator is currently visible.
     * @return `true` when sort-arrow painting is enabled.
     */
    bool isSortIndicatorShown() const { return m_sortIndicatorShown; }

    /**
     * @brief Sets the section and order represented by the sort indicator.
     * @param section Section index that should show the indicator.
     * @param order Sort direction to paint.
     */
    void setSortIndicator(int section, SwSortOrder order) {
        if (m_sortSection == section && m_sortOrder == order) {
            return;
        }
        m_sortSection = section;
        m_sortOrder = order;
        update();
    }

    /**
     * @brief Returns the section that currently owns the sort indicator.
     * @return Logical section index, or `-1` when no section is selected for sorting.
     */
    int sortIndicatorSection() const { return m_sortSection; }

    /**
     * @brief Returns the direction represented by the sort indicator.
     * @return Current sort order used for arrow painting.
     */
    SwSortOrder sortIndicatorOrder() const { return m_sortOrder; }

signals:
    DECLARE_SIGNAL(sectionClicked, int);          ///< Emitted when the user clicks a logical section.
    DECLARE_SIGNAL(sectionResized, int, int, int); ///< Emitted after a user resize with section, old size, and new size.

protected:
    /**
     * @brief Recomputes stretched sizes when the header widget is resized.
     * @param event Resize event forwarded by the widget system.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateStretchedSections_();
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!event || !event->painter() || !isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event->painter();
        const SwRect bounds = rect();
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
            borderWidth = clampInt(parsePixelValue(bw, borderWidth), 0, 20);
        }

        SwString brv = styleValue_(sheet, hierarchy, "border-radius");
        if (!brv.isEmpty()) {
            radius = clampInt(parsePixelValue(brv, radius), 0, 64);
        }
        tl = tr = br = bl = radius;

        SwString v = styleValue_(sheet, hierarchy, "border-top-left-radius");
        if (!v.isEmpty()) tl = clampInt(parsePixelValue(v, tl), 0, 64);
        v = styleValue_(sheet, hierarchy, "border-top-right-radius");
        if (!v.isEmpty()) tr = clampInt(parsePixelValue(v, tr), 0, 64);
        v = styleValue_(sheet, hierarchy, "border-bottom-right-radius");
        if (!v.isEmpty()) br = clampInt(parsePixelValue(v, br), 0, 64);
        v = styleValue_(sheet, hierarchy, "border-bottom-left-radius");
        if (!v.isEmpty()) bl = clampInt(parsePixelValue(v, bl), 0, 64);

        SwString pad = styleValue_(sheet, hierarchy, "padding");
        if (!pad.isEmpty()) {
            padding = parsePadding_(pad);
        }
        v = styleValue_(sheet, hierarchy, "padding-left");
        if (!v.isEmpty()) padding.left = clampInt(parsePixelValue(v, padding.left), 0, 128);
        v = styleValue_(sheet, hierarchy, "padding-right");
        if (!v.isEmpty()) padding.right = clampInt(parsePixelValue(v, padding.right), 0, 128);
        v = styleValue_(sheet, hierarchy, "padding-top");
        if (!v.isEmpty()) padding.top = clampInt(parsePixelValue(v, padding.top), 0, 128);
        v = styleValue_(sheet, hierarchy, "padding-bottom");
        if (!v.isEmpty()) padding.bottom = clampInt(parsePixelValue(v, padding.bottom), 0, 128);

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

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseMoveEvent(MouseEvent* event) override {
        SwWidget::mouseMoveEvent(event);
        if (!event) {
            return;
        }

        if (m_resizing && m_resizeSection >= 0) {
            const int contentPos = contentPos_(event->x(), event->y());
            const int delta = contentPos - m_resizeStartPos;
            const int newSize = clampInt(m_resizeStartSize + delta, m_minSectionSize, m_maxSectionSize);
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

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseReleaseEvent(MouseEvent* event) override {
        SwWidget::mouseReleaseEvent(event);
        if (!event) {
            return;
        }
        m_resizing = false;
        m_resizeSection = -1;
    }

private:
    struct Padding {
        int top{0};
        int right{0};
        int bottom{0};
        int left{0};

        /**
         * @brief Constructs a `Padding` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Padding() {}
        /**
         * @brief Constructs a `Padding` instance.
         * @param topValue Value passed to the method.
         * @param rightValue Value passed to the method.
         * @param bottomValue Value passed to the method.
         * @param leftValue Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Padding(int topValue, int rightValue, int bottomValue, int leftValue)
            : top(topValue)
            , right(rightValue)
            , bottom(bottomValue)
            , left(leftValue) {}
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
            SwString v = sheet->getStyleProperty(selector, propertyName);
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
            out = clampColor(const_cast<StyleSheet*>(sheet)->parseColor(value, nullptr));
            return true;
        } catch (...) {
            return false;
        }
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
        r = clampInt(r, 0, maxRadius);
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
        const SwRect r = rect();
        if (m_orientation == SwOrientation::Horizontal) {
            return (px - r.x) + std::max(0, m_offset);
        }
        return (py - r.y) + std::max(0, m_offset);
    }

    int dividerAt_(int px, int py) const {
        if (!m_model) {
            return -1;
        }
        const SwRect r = rect();
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
                m_sectionSizes[idx] = clampInt(m_stretchedSectionSizes[idx], m_minSectionSize, m_maxSectionSize);
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

