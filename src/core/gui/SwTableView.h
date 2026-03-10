#pragma once

/**
 * @file src/core/gui/SwTableView.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwTableView in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the table view interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwTableView.
 *
 * View-oriented declarations here mainly describe how underlying state is projected into a visual
 * or interactive surface, including how refresh, selection, or presentation concerns are exposed
 * at the API boundary.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */

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

#include "SwAbstractItemModel.h"
#include "SwHeaderView.h"
#include "SwItemSelectionModel.h"
#include "SwScrollBar.h"
#include "SwScrollBarPolicy.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwDragDrop.h"

#include "SwList.h"
#include <cstdlib>

class SwTableView : public SwWidget {
    SW_OBJECT(SwTableView, SwWidget)

private:
    void updateHeaderSortIndicator_();
    void updateHeaderGeometry_();

public:
    /**
     * @brief Constructs a `SwTableView` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwTableView(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Sets the model.
     * @param model Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModel(SwAbstractItemModel* model) {
        if (m_model == model) {
            updateLayout();
            update();
            return;
        }

        clearIndexWidgets();

        if (m_model) {
            SwObject::disconnect(m_model, this);
        }

        m_model = model;
        m_sortColumn = -1;
        m_sortOrder = SwSortOrder::AscendingOrder;
        if (m_header) {
            m_header->setModel(m_model);
            m_header->setSortIndicator(-1, m_sortOrder);
        }
        if (!m_selectionModel) {
            m_selectionModel = new SwItemSelectionModel(m_model, this);
        } else {
            m_selectionModel->setModel(m_model);
        }

        if (m_model) {
            SwObject::connect(m_model, &SwAbstractItemModel::modelReset, this, [this]() {
                validateSortStateAfterModelReset();
                remapCurrentIndexAfterModelReset();
                remapSelectionAfterModelReset();
                remapIndexWidgetsAfterModelReset();
                resetScrollBars();
                updateIndexWidgetsGeometry();
                update();
            });
            SwObject::connect(m_model, &SwAbstractItemModel::dataChanged, this, [this](const SwModelIndex&, const SwModelIndex&) {
                update();
            });
        }

        resetScrollBars();
        updateHeaderSortIndicator_();
        update();
    }

    /**
     * @brief Returns the current model.
     * @return The current model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwAbstractItemModel* model() const { return m_model; }

    /**
     * @brief Sets the selection Model.
     * @param selectionModel Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSelectionModel(SwItemSelectionModel* selectionModel) {
        if (m_selectionModel == selectionModel) {
            return;
        }
        if (m_selectionModel) {
            SwObject::disconnect(m_selectionModel, this);
        }
        m_selectionModel = selectionModel;
        if (m_selectionModel) {
            SwObject::connect(m_selectionModel, &SwItemSelectionModel::selectionChanged, this, [this]() { update(); });
            SwObject::connect(m_selectionModel, &SwItemSelectionModel::currentChanged, this, [this](const SwModelIndex&, const SwModelIndex&) { update(); });
        }
        update();
    }

    /**
     * @brief Returns the current selection Model.
     * @return The current selection Model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwItemSelectionModel* selectionModel() const { return m_selectionModel; }

    /**
     * @brief Sets the drag Enabled.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDragEnabled(bool on) { m_dragEnabled = on; }
    /**
     * @brief Returns the current drag Enabled.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool dragEnabled() const { return m_dragEnabled; }

    /**
     * @brief Sets the accept Drops.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAcceptDrops(bool on) { m_acceptDrops = on; }
    /**
     * @brief Returns the current accept Drops.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool acceptDrops() const { return m_acceptDrops; }

    /**
     * @brief Sets the drop Indicator Shown.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDropIndicatorShown(bool on) { m_dropIndicatorShown = on; }
    /**
     * @brief Returns the current drop Indicator Shown.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool dropIndicatorShown() const { return m_dropIndicatorShown; }

    /**
     * @brief Sets the index Widget.
     * @param index Value passed to the method.
     * @param widget Widget associated with the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIndexWidget(const SwModelIndex& index, SwWidget* widget) {
        if (!m_model || !index.isValid() || index.model() != m_model) {
            if (widget) {
                delete widget;
            }
            return;
        }

        // Replace existing widget for this index.
        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            if (m_indexWidgets[i].index == index) {
                if (m_indexWidgets[i].widget == widget) {
                    updateIndexWidgetsGeometry();
                    update();
                    return;
                }
                delete m_indexWidgets[i].widget;
                m_indexWidgets.removeAt(i);
                break;
            }
        }

        if (!widget) {
            updateIndexWidgetsGeometry();
            update();
            return;
        }

        // Ensure a widget is used only once.
        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            if (m_indexWidgets[i].widget == widget) {
                m_indexWidgets.removeAt(i);
                break;
            }
        }

        widget->setParent(this);

        IndexWidgetEntry entry;
        entry.index = index;
        entry.widget = widget;
        m_indexWidgets.append(entry);

        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Performs the `indexWidget` operation.
     * @param index Value passed to the method.
     * @return The requested index Widget.
     */
    SwWidget* indexWidget(const SwModelIndex& index) const {
        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            if (m_indexWidgets[i].index == index) {
                return m_indexWidgets[i].widget;
            }
        }
        return nullptr;
    }

    /**
     * @brief Clears the current object state.
     */
    void clearIndexWidgets() {
        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            delete m_indexWidgets[i].widget;
            m_indexWidgets[i].widget = nullptr;
        }
        m_indexWidgets.clear();
        update();
    }

signals:
    DECLARE_SIGNAL(contextMenuRequested, const SwModelIndex&, int, int);
    DECLARE_SIGNAL(dragStarted, const SwModelIndex&);
    DECLARE_SIGNAL(dragMoved, const SwModelIndex&, int, int);
    DECLARE_SIGNAL(dragDropped, const SwModelIndex&, const SwModelIndex&);

    /**
     * @brief Sets the alternating Row Colors.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAlternatingRowColors(bool on) {
        if (m_alternating == on) {
            return;
        }
        m_alternating = on;
        update();
    }

    /**
     * @brief Returns the current alternating Row Colors.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool alternatingRowColors() const { return m_alternating; }

    /**
     * @brief Sets the show Grid.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setShowGrid(bool on) {
        if (m_showGrid == on) {
            return;
        }
        m_showGrid = on;
        if (m_header) {
            m_header->setShowGrid(on);
        }
        update();
    }

    /**
     * @brief Returns the current show Grid.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool showGrid() const { return m_showGrid; }

    /**
     * @brief Sets the sorting Enabled.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSortingEnabled(bool on) {
        if (m_sortingEnabled == on) {
            return;
        }
        m_sortingEnabled = on;
        updateHeaderSortIndicator_();
        update();
    }

    /**
     * @brief Returns the current sorting Enabled.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool sortingEnabled() const { return m_sortingEnabled; }

    /**
     * @brief Returns the current sort Column.
     * @return The current sort Column.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int sortColumn() const { return m_sortColumn; }
    /**
     * @brief Returns the current sort Order.
     * @return The current sort Order.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSortOrder sortOrder() const { return m_sortOrder; }

    /**
     * @brief Performs the `sortByColumn` operation.
     * @param column Value passed to the method.
     * @param order Value passed to the method.
     */
    void sortByColumn(int column, SwSortOrder order = SwSortOrder::AscendingOrder) {
        if (!m_model) {
            return;
        }
        if (column < 0 || column >= columnCount()) {
            return;
        }
        m_sortColumn = column;
        m_sortOrder = order;
        updateHeaderSortIndicator_();
        m_model->sort(column, order);
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Sets the default Column Width.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDefaultColumnWidth(int px) {
        px = std::max(24, px);
        if (m_defaultColumnWidth == px) {
            return;
        }
        m_defaultColumnWidth = px;
        if (m_header) {
            m_header->setDefaultSectionSize(px);
        }
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current default Column Width.
     * @return The current default Column Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int defaultColumnWidth() const { return m_defaultColumnWidth; }

    /**
     * @brief Sets the column Width.
     * @param column Value passed to the method.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColumnWidth(int column, int px) {
        if (!m_header) {
            return;
        }
        m_header->setSectionSize(column, px);
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Performs the `columnWidth` operation.
     * @param column Value passed to the method.
     * @return The requested column Width.
     */
    int columnWidth(int column) const { return columnWidthInternal(column); }

    /**
     * @brief Sets the columns Fit To Width.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColumnsFitToWidth(bool on) {
        if (!m_header) {
            return;
        }
        if (m_header->sectionsFitToWidth() == on) {
            return;
        }
        m_header->setSectionsFitToWidth(on);
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Performs the `columnsFitToWidth` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool columnsFitToWidth() const { return m_header ? m_header->sectionsFitToWidth() : false; }

    /**
     * @brief Sets the column Stretch.
     * @param column Value passed to the method.
     * @param stretch Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColumnStretch(int column, int stretch) {
        if (!m_header || column < 0) {
            return;
        }
        stretch = std::max(0, stretch);
        SwList<int> stretches = m_header->sectionStretches();
        while (static_cast<int>(stretches.size()) <= column) {
            stretches.append(0);
        }
        const size_t idx = static_cast<size_t>(column);
        if (stretches[idx] == stretch) {
            return;
        }
        stretches[idx] = stretch;
        m_header->setSectionStretches(stretches);
        if (columnsFitToWidth()) {
            resetScrollBars();
            updateIndexWidgetsGeometry();
            update();
        }
    }

    /**
     * @brief Performs the `columnStretch` operation.
     * @param column Value passed to the method.
     * @return The requested column Stretch.
     */
    int columnStretch(int column) const {
        if (!m_header || column < 0) {
            return 0;
        }
        const size_t idx = static_cast<size_t>(column);
        SwList<int> stretches = m_header->sectionStretches();
        if (idx >= stretches.size()) {
            return 0;
        }
        return stretches[idx];
    }

    /**
     * @brief Sets the column Stretches.
     * @param stretches Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColumnStretches(const SwList<int>& stretches) {
        if (!m_header) {
            return;
        }
        m_header->setSectionStretches(stretches);
        if (columnsFitToWidth()) {
            resetScrollBars();
            updateIndexWidgetsGeometry();
            update();
        }
    }

    /**
     * @brief Performs the `resizeColumnToContents` operation.
     * @param column Value passed to the method.
     */
    void resizeColumnToContents(int column) {
        if (!m_model || column < 0 || column >= columnCount()) {
            return;
        }

        const SwFont headerFont(L"Segoe UI", 9, SemiBold);
        const SwFont cellFont(L"Segoe UI", 9, Normal);

        int maxW = 0;

        SwAny hd = m_model->headerData(column, SwOrientation::Horizontal, SwItemDataRole::DisplayRole);
        SwString headerText = hd.toString();
        if (headerText.isEmpty()) {
            headerText = SwString("Column %1").arg(SwString::number(column + 1));
        }
        maxW = std::max(maxW, textWidthPx(headerText, headerFont) + 20);

        const int rows = rowCount();
        for (int r = 0; r < rows; ++r) {
            const SwModelIndex idx = m_model->index(r, column, SwModelIndex());
            if (!idx.isValid()) {
                continue;
            }

            if (SwWidget* w = indexWidget(idx)) {
                maxW = std::max(maxW, std::max(0, w->frameGeometry().width) + 16);
                continue;
            }

            SwAny v = m_model->data(idx, SwItemDataRole::DisplayRole);
            SwString text = v.toString();
            maxW = std::max(maxW, textWidthPx(text, cellFont) + 20);
        }

        setColumnWidth(column, maxW);
    }

    /**
     * @brief Performs the `resizeColumnsToContents` operation.
     */
    void resizeColumnsToContents() {
        const int cols = columnCount();
        for (int c = 0; c < cols; ++c) {
            resizeColumnToContents(c);
        }
    }

    /**
     * @brief Sets the horizontal Scroll Bar Policy.
     * @param policy Policy value applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHorizontalScrollBarPolicy(SwScrollBarPolicy policy) {
        if (m_hPolicy == policy) {
            return;
        }
        m_hPolicy = policy;
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current horizontal Scroll Bar Policy.
     * @return The current horizontal Scroll Bar Policy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBarPolicy horizontalScrollBarPolicy() const { return m_hPolicy; }

    /**
     * @brief Sets the vertical Scroll Bar Policy.
     * @param policy Policy value applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setVerticalScrollBarPolicy(SwScrollBarPolicy policy) {
        if (m_vPolicy == policy) {
            return;
        }
        m_vPolicy = policy;
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current vertical Scroll Bar Policy.
     * @return The current vertical Scroll Bar Policy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBarPolicy verticalScrollBarPolicy() const { return m_vPolicy; }

    /**
     * @brief Sets the row Height.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRowHeight(int px) {
        m_rowHeight = std::max(18, px);
        resetScrollBars();
        update();
    }

    /**
     * @brief Returns the current row Height.
     * @return The current row Height.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int rowHeight() const { return m_rowHeight; }

    /**
     * @brief Sets the header Height.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHeaderHeight(int px) {
        m_headerHeight = std::max(24, px);
        resetScrollBars();
        updateHeaderGeometry_();
        update();
    }

    /**
     * @brief Returns the current header Height.
     * @return The current header Height.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int headerHeight() const { return m_headerHeight; }

    // Horizontal header accessor.
    /**
     * @brief Returns the current horizontal Header.
     * @return The current horizontal Header.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHeaderView* horizontalHeader() const { return m_header; }

    /**
     * @brief Returns the current horizontal Scroll Bar.
     * @return The current horizontal Scroll Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBar* horizontalScrollBar() const { return m_hBar; }
    /**
     * @brief Returns the current vertical Scroll Bar.
     * @return The current vertical Scroll Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBar* verticalScrollBar() const { return m_vBar; }

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        resetScrollBars();
        updateHeaderGeometry_();
        updateIndexWidgetsGeometry();
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

        // Frame
        painter->fillRoundedRect(bounds, 12, SwColor{255, 255, 255}, SwColor{220, 224, 232}, 1);

        const SwRect viewport = viewportRect(bounds);
        if (viewport.width <= 0 || viewport.height <= 0) {
            return;
        }

        const SwRect contentViewport = contentViewportRect(bounds);
        const int headerH = std::min(m_headerHeight, contentViewport.height);
        updateHeaderGeometry_();

        // Clip to data area (below header).
        SwRect dataClip{contentViewport.x,
                        contentViewport.y + headerH,
                        contentViewport.width,
                        contentViewport.height - headerH};
        if (dataClip.height < 0) {
            dataClip.height = 0;
        }

        painter->pushClipRect(dataClip);
        paintRows(painter, contentViewport, headerH);

        if (m_dropIndicatorShown && m_dragging && m_dropIndex.isValid() && m_rowHeight > 0) {
            const int offsetX = m_hBar ? m_hBar->value() : 0;
            const int offsetY = m_vBar ? m_vBar->value() : 0;
            const int row = m_dropIndex.row();
            const int col = m_dropIndex.column();
            if (row >= 0 && row < rowCount() && col >= 0 && col < columnCount()) {
                const int x = contentViewport.x + columnStartX(col) - offsetX;
                const int y = contentViewport.y + headerH + row * m_rowHeight - offsetY;
                SwRect dropRect{x, y, columnWidthInternal(col), m_rowHeight};
                painter->drawRect(dropRect, SwColor{59, 130, 246}, 2);
            }
        }

        painter->popClipRect();
        updateIndexWidgetsGeometry();

        // Paint child widgets (scrollbars) on top.
        for (SwObject* objChild : children()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }
            paintChild_(event, child);
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
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        // Let child widgets (scrollbars / index widgets) handle the event first.
        if (SwWidget* childWidget = getChildUnderCursor(event->x(), event->y())) {
            MouseEvent childEvent = mapMouseEventToChild_(*event, this, childWidget);
            SwCoreApplication::sendEvent(childWidget, &childEvent);
            if (childEvent.isAccepted()) {
                event->accept();
                return;
            }
        }

        if (!m_model || !m_selectionModel) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        if (!containsPoint(viewport, event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const int headerH = std::min(m_headerHeight, viewport.height);
        if (event->y() < viewport.y + headerH) {
            event->accept();
            return;
        }

        if (event->button() == SwMouseButton::Left && hitRowResizeHandle_(event->x(), event->y())) {
            m_resizingRowHeight = true;
            m_rowResizeStartY = event->y();
            m_rowResizeStartHeight = m_rowHeight;
            m_pressed = false;
            m_dragging = false;
            event->accept();
            return;
        }

        const int contentX = event->x() - viewport.x + m_hBar->value();
        const int contentY = event->y() - (viewport.y + headerH) + m_vBar->value();

        const int row = (m_rowHeight > 0) ? (contentY / m_rowHeight) : -1;
        if (row < 0) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const int column = columnAtContentX(contentX);
        if (column < 0) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const SwModelIndex idx = m_model->index(row, column, SwModelIndex());
        if (idx.isValid()) {
            const SwModelIndex rowIdx = m_model->index(row, 0, SwModelIndex());
            if (event->button() == SwMouseButton::Right) {
                if (rowIdx.isValid() && m_selectionModel->isSelected(rowIdx)) {
                    m_selectionModel->setCurrentIndex(idx, false, false);
                } else {
                    SwList<SwModelIndex> selection;
                    if (rowIdx.isValid()) {
                        selection.append(rowIdx);
                    }
                    m_selectionModel->setSelectedIndexes(selection);
                    m_selectionModel->setCurrentIndex(idx, false, false);
                    m_selectionModel->setAnchorIndex(rowIdx);
                }
                ensureIndexVisible(idx);
                update();
                contextMenuRequested(idx, event->x(), event->y());
                event->accept();
                return;
            }
            if (event->button() != SwMouseButton::Left) {
                SwWidget::mousePressEvent(event);
                return;
            }

            const bool ctrl = event->isCtrlPressed();
            const bool shift = event->isShiftPressed();

            if (shift && rowIdx.isValid()) {
                SwModelIndex anchor = m_selectionModel->anchorIndex();
                if (!anchor.isValid() || anchor.model() != m_model) {
                    const SwModelIndex current = m_selectionModel->currentIndex();
                    anchor = current.isValid() && current.model() == m_model ? m_model->index(current.row(), 0, SwModelIndex()) : rowIdx;
                    m_selectionModel->setAnchorIndex(anchor);
                }

                const int a = anchor.row();
                const int b = rowIdx.row();
                const int lo = std::max(0, std::min(a, b));
                const int hi = std::min(rowCount() - 1, std::max(a, b));

                SwList<SwModelIndex> selection = ctrl ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();
                selection.reserve(selection.size() + static_cast<size_t>(hi - lo + 1));
                for (int r = lo; r <= hi; ++r) {
                    const SwModelIndex ri = m_model->index(r, 0, SwModelIndex());
                    if (ri.isValid() && (!ctrl || !m_selectionModel->isSelected(ri))) {
                        selection.append(ri);
                    }
                }

                m_selectionModel->setSelectedIndexes(selection);
                m_selectionModel->setCurrentIndex(idx, false, false);
            } else if (ctrl && rowIdx.isValid()) {
                m_selectionModel->toggle(rowIdx);
                m_selectionModel->setCurrentIndex(idx, false, false);
                m_selectionModel->setAnchorIndex(rowIdx);
            } else {
                SwList<SwModelIndex> selection;
                if (rowIdx.isValid()) {
                    selection.append(rowIdx);
                }
                m_selectionModel->setSelectedIndexes(selection);
                m_selectionModel->setCurrentIndex(idx, false, false);
                m_selectionModel->setAnchorIndex(rowIdx);
            }
            ensureIndexVisible(idx);
            m_pressed = m_dragEnabled;
            m_dragging = false;
            m_pressX = event->x();
            m_pressY = event->y();
            m_pressIndex = idx;
            m_dragIndex = SwModelIndex();
            m_dropIndex = SwModelIndex();
            event->accept();
            update();
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

        if (m_resizingRowHeight) {
            const int delta = event->y() - m_rowResizeStartY;
            const int newH = std::max(18, m_rowResizeStartHeight + delta);
            if (newH != m_rowHeight) {
                m_rowHeight = newH;
                resetScrollBars();
                updateIndexWidgetsGeometry();
                update();
            }
            event->accept();
            return;
        }

        if (!m_dragEnabled || !m_pressed) {
            if (m_header && m_header->isVisibleInHierarchy() && containsPoint(m_header->geometry(), event->x(), event->y())) {
                setToolTips(SwString());
                return;
            }

            if (m_model) {
                const SwModelIndex hover = indexAt_(event->x(), event->y());
                if (hover != m_hoverIndex) {
                    m_hoverIndex = hover;
                    SwString tip = hover.isValid() ? m_model->data(hover, SwItemDataRole::ToolTipRole).toString() : SwString();
                    setToolTips(tip);
                }
            } else {
                m_hoverIndex = SwModelIndex();
                setToolTips(SwString());
            }

            if (hitRowResizeHandle_(event->x(), event->y())) {
                setCursor(CursorType::SizeNS);
                event->accept();
            } else {
                setCursor(CursorType::Arrow);
            }
            return;
        }

        setToolTips(SwString());
        if (!m_model || !m_pressIndex.isValid() || m_pressIndex.model() != m_model) {
            return;
        }

        const int dx = std::abs(event->x() - m_pressX);
        const int dy = std::abs(event->y() - m_pressY);
        const int threshold = 6;
        if (!m_dragging && (dx + dy) >= threshold) {
            m_dragging = true;
            m_dragIndex = m_pressIndex;

            SwString text = m_model->data(m_dragIndex, SwItemDataRole::DisplayRole).toString();
            if (text.isEmpty()) {
                text = SwString("Cell");
            }
            SwDragDrop::instance().begin(nativeWindowHandle(), text, getFont(), event->x(), event->y(), true);
            dragStarted(m_dragIndex);
        }

        if (!m_dragging) {
            return;
        }

        SwDragDrop::instance().updatePosition(event->x(), event->y());
        dragMoved(m_dragIndex, event->x(), event->y());

        if (m_acceptDrops && m_dropIndicatorShown) {
            const SwModelIndex next = indexAt_(event->x(), event->y());
            if (next != m_dropIndex) {
                m_dropIndex = next;
                update();
            }
        }

        event->accept();
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

        if (m_resizingRowHeight) {
            m_resizingRowHeight = false;
            event->accept();
            return;
        }

        const bool wasDragging = m_dragging;
        const SwModelIndex dragged = m_dragIndex;
        const SwModelIndex drop = m_dropIndex;

        m_pressed = false;
        m_dragging = false;
        m_pressIndex = SwModelIndex();
        m_dragIndex = SwModelIndex();
        m_dropIndex = SwModelIndex();

        if (wasDragging) {
            SwDragDrop::instance().end();
            if (m_acceptDrops) {
                dragDropped(dragged, drop);
            }
            update();
            event->accept();
            return;
        }
    }

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::wheelEvent(event);
            return;
        }

        int steps = event->delta() / 120;
        if (steps == 0) {
            steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
        }
        if (steps == 0) {
            SwWidget::wheelEvent(event);
            return;
        }

        const int stepY = std::max(1, std::max(m_rowHeight, (m_vBar ? (m_vBar->pageStep() / 10) : 0)));
        const int stepX = std::max(1, std::max(24, (m_hBar ? (m_hBar->pageStep() / 10) : 0)));
        const bool horizontalRequest = event->isShiftPressed();

        auto scrollBy = [&](SwScrollBar* bar, int stepPx) -> bool {
            if (!bar || stepPx <= 0) {
                return false;
            }
            const int old = bar->value();
            bar->setValue(old - steps * stepPx);
            return bar->value() != old;
        };

        bool scrolled = false;
        if (horizontalRequest) {
            if (m_hBar && m_hBar->getVisible()) {
                scrolled = scrollBy(m_hBar, stepX);
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

        SwWidget::wheelEvent(event);
    }

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }
        SwWidget::keyPressEvent(event);
        if (event->isAccepted()) {
            return;
        }
        if (!getFocus()) {
            return;
        }
        if (!m_model || !m_selectionModel) {
            return;
        }

        const int rows = rowCount();
        const int cols = columnCount();
        if (rows <= 0 || cols <= 0) {
            return;
        }

        SwModelIndex current = m_selectionModel->currentIndex();
        int row = current.isValid() ? current.row() : 0;
        int col = current.isValid() ? current.column() : 0;

        if (event->isCtrlPressed() && SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'A')) {
            SwList<SwModelIndex> selection;
            selection.reserve(static_cast<size_t>(rows));
            for (int r = 0; r < rows; ++r) {
                const SwModelIndex ri = m_model->index(r, 0, SwModelIndex());
                if (ri.isValid()) {
                    selection.append(ri);
                }
            }
            m_selectionModel->setSelectedIndexes(selection);
            if (!current.isValid()) {
                current = m_model->index(0, 0, SwModelIndex());
                if (current.isValid()) {
                    m_selectionModel->setCurrentIndex(current, false, false);
                }
            }
            m_selectionModel->setAnchorIndex(m_model->index(0, 0, SwModelIndex()));
            ensureIndexVisible(current.isValid() ? current : m_model->index(0, 0, SwModelIndex()));
            update();
            event->accept();
            return;
        }

        bool handled = true;
        if (SwWidgetPlatformAdapter::isUpArrowKey(event->key())) {
            row = (row > 0) ? (row - 1) : 0;
        } else if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
            row = (row + 1 < rows) ? (row + 1) : (rows - 1);
        } else if (SwWidgetPlatformAdapter::isLeftArrowKey(event->key())) {
            col = (col > 0) ? (col - 1) : 0;
        } else if (SwWidgetPlatformAdapter::isRightArrowKey(event->key())) {
            col = (col + 1 < cols) ? (col + 1) : (cols - 1);
        } else if (SwWidgetPlatformAdapter::isHomeKey(event->key())) {
            row = 0;
        } else if (SwWidgetPlatformAdapter::isEndKey(event->key())) {
            row = rows - 1;
        } else {
            handled = false;
        }

        if (!handled) {
            return;
        }

        if (row < 0) row = 0;
        if (row >= rows) row = rows - 1;
        if (col < 0) col = 0;
        if (col >= cols) col = cols - 1;

        const SwModelIndex next = m_model->index(row, col, SwModelIndex());
        const SwModelIndex nextRowIdx = m_model->index(row, 0, SwModelIndex());
        if (!next.isValid()) {
            return;
        }

        const bool ctrl = event->isCtrlPressed();
        const bool shift = event->isShiftPressed();

        if (shift && nextRowIdx.isValid()) {
            SwModelIndex anchor = m_selectionModel->anchorIndex();
            if (!anchor.isValid() || anchor.model() != m_model) {
                anchor = current.isValid() && current.model() == m_model ? m_model->index(current.row(), 0, SwModelIndex()) : nextRowIdx;
                m_selectionModel->setAnchorIndex(anchor);
            }

            const int a = anchor.row();
            const int b = nextRowIdx.row();
            const int lo = std::max(0, std::min(a, b));
            const int hi = std::min(rows - 1, std::max(a, b));

            SwList<SwModelIndex> selection = ctrl ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();
            selection.reserve(selection.size() + static_cast<size_t>(hi - lo + 1));
            for (int r = lo; r <= hi; ++r) {
                const SwModelIndex ri = m_model->index(r, 0, SwModelIndex());
                if (ri.isValid() && (!ctrl || !m_selectionModel->isSelected(ri))) {
                    selection.append(ri);
                }
            }

            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(next, false, false);
        } else if (ctrl) {
            m_selectionModel->setCurrentIndex(next, false, false);
        } else {
            SwList<SwModelIndex> selection;
            if (nextRowIdx.isValid()) {
                selection.append(nextRowIdx);
            }
            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(next, false, false);
            m_selectionModel->setAnchorIndex(nextRowIdx);
        }

        ensureIndexVisible(next);
        update();
        event->accept();
    }

private:
    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    static bool rectContains(const SwRect& outer, const SwRect& inner) {
        return inner.x >= outer.x &&
               inner.y >= outer.y &&
               (inner.x + inner.width) <= (outer.x + outer.width) &&
               (inner.y + inner.height) <= (outer.y + outer.height);
    }

    bool hitRowResizeHandle_(int px, int py) const {
        if (!m_model || !m_vBar) {
            return false;
        }
        if (m_rowHeight <= 0) {
            return false;
        }
        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        if (!containsPoint(viewport, px, py)) {
            return false;
        }
        const int headerH = std::min(m_headerHeight, viewport.height);
        if (py < viewport.y + headerH) {
            return false;
        }

        const int offsetY = m_vBar->value();
        const int contentY = py - (viewport.y + headerH) + offsetY;
        if (contentY < 0) {
            return false;
        }

        const int rows = rowCount();
        const int totalH = rows * m_rowHeight;
        if (contentY >= totalH) {
            return false;
        }

        const int grab = 3;
        const int rem = (m_rowHeight > 0) ? (contentY % m_rowHeight) : 0;
        return (m_rowHeight - rem) <= grab;
    }

    SwModelIndex indexAt_(int px, int py) const {
        if (!m_model) {
            return SwModelIndex();
        }

        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        if (!containsPoint(viewport, px, py)) {
            return SwModelIndex();
        }

        const int headerH = std::min(m_headerHeight, viewport.height);
        if (py < viewport.y + headerH) {
            return SwModelIndex();
        }

        const int offsetX = m_hBar ? m_hBar->value() : 0;
        const int offsetY = m_vBar ? m_vBar->value() : 0;

        const int contentX = px - viewport.x + offsetX;
        const int contentY = py - (viewport.y + headerH) + offsetY;
        if (contentY < 0) {
            return SwModelIndex();
        }

        const int row = (m_rowHeight > 0) ? (contentY / m_rowHeight) : -1;
        const int column = columnAtContentX(contentX);
        if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount()) {
            return SwModelIndex();
        }

        return m_model->index(row, column, SwModelIndex());
    }

    void validateSortStateAfterModelReset() {
        if (!m_model) {
            m_sortColumn = -1;
            updateHeaderSortIndicator_();
            return;
        }
        const int cols = columnCount();
        if (m_sortColumn < 0 || m_sortColumn >= cols) {
            m_sortColumn = -1;
        }
        updateHeaderSortIndicator_();
    }

    SwModelIndex findIndexForInternalPointer(void* ptr, int columnHint) const {
        if (!m_model || !ptr) {
            return SwModelIndex();
        }

        const int rows = rowCount();
        const int cols = columnCount();

        if (columnHint >= 0 && columnHint < cols) {
            for (int r = 0; r < rows; ++r) {
                const SwModelIndex idx = m_model->index(r, columnHint, SwModelIndex());
                if (idx.isValid() && idx.internalPointer() == ptr) {
                    return idx;
                }
            }
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const SwModelIndex idx = m_model->index(r, c, SwModelIndex());
                if (idx.isValid() && idx.internalPointer() == ptr) {
                    return idx;
                }
            }
        }

        return SwModelIndex();
    }

    void remapCurrentIndexAfterModelReset() {
        if (!m_model || !m_selectionModel) {
            return;
        }

        const SwModelIndex current = m_selectionModel->currentIndex();
        if (!current.isValid() || current.model() != m_model) {
            return;
        }

        const SwModelIndex remapped = findIndexForInternalPointer(current.internalPointer(), current.column());
        if (remapped.isValid()) {
            m_selectionModel->setCurrentIndex(remapped, false, false);
        } else {
            m_selectionModel->setCurrentIndex(SwModelIndex(), false, false);
        }
    }

    void remapSelectionAfterModelReset() {
        if (!m_model || !m_selectionModel) {
            return;
        }

        SwList<SwModelIndex> selected = m_selectionModel->selectedIndexes();
        if (selected.isEmpty()) {
            return;
        }

        SwList<SwModelIndex> remapped;
        remapped.reserve(selected.size());
        for (size_t i = 0; i < selected.size(); ++i) {
            const SwModelIndex& s = selected[i];
            if (!s.isValid() || s.model() != m_model) {
                continue;
            }
            SwModelIndex ri = findIndexForInternalPointer(s.internalPointer(), 0);
            if (ri.isValid()) {
                remapped.append(ri);
            }
        }
        m_selectionModel->setSelectedIndexes(remapped);

        const SwModelIndex anchor = m_selectionModel->anchorIndex();
        if (anchor.isValid() && anchor.model() == m_model) {
            SwModelIndex ra = findIndexForInternalPointer(anchor.internalPointer(), 0);
            m_selectionModel->setAnchorIndex(ra.isValid() ? ra : SwModelIndex());
        }
    }

    void remapIndexWidgetsAfterModelReset() {
        if (!m_model) {
            for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
                if (m_indexWidgets[i].widget) {
                    m_indexWidgets[i].widget->hide();
                }
            }
            return;
        }

        for (size_t i = 0; i < m_indexWidgets.size();) {
            IndexWidgetEntry& entry = m_indexWidgets[i];
            if (!entry.widget) {
                m_indexWidgets.removeAt(i);
                continue;
            }
            if (!entry.index.isValid() || entry.index.model() != m_model) {
                delete entry.widget;
                m_indexWidgets.removeAt(i);
                continue;
            }

            SwModelIndex remapped = findIndexForInternalPointer(entry.index.internalPointer(), entry.index.column());
            if (!remapped.isValid()) {
                delete entry.widget;
                m_indexWidgets.removeAt(i);
                continue;
            }

            entry.index = remapped;
            ++i;
        }
    }

    void initDefaults() {
        setStyleSheet(R"(
            SwTableView { background-color: rgba(0,0,0,0); border-width: 0px; }
        )");

        m_header = new SwHeaderView(SwOrientation::Horizontal, this);
        m_header->setShowGrid(m_showGrid);
        m_header->setDefaultSectionSize(m_defaultColumnWidth);
        m_header->setSortIndicatorShown(true);
        SwObject::connect(m_header, &SwHeaderView::sectionClicked, this, [this](int section) {
            if (!m_sortingEnabled) {
                return;
            }
            SwSortOrder order = SwSortOrder::AscendingOrder;
            if (m_sortColumn == section) {
                order = (m_sortOrder == SwSortOrder::AscendingOrder) ? SwSortOrder::DescendingOrder : SwSortOrder::AscendingOrder;
            }
            sortByColumn(section, order);
        });
        SwObject::connect(m_header, &SwHeaderView::sectionResized, this, [this](int, int, int) {
            resetScrollBars();
            updateIndexWidgetsGeometry();
            update();
        });

        m_hBar = new SwScrollBar(SwScrollBar::Orientation::Horizontal, this);
        m_vBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);
        m_hBar->hide();
        m_vBar->hide();

        SwObject::connect(m_hBar, &SwScrollBar::valueChanged, this, [this](int) {
            if (m_header) {
                m_header->setOffset(m_hBar ? m_hBar->value() : 0);
            }
            updateIndexWidgetsGeometry();
            update();
        });
        SwObject::connect(m_vBar, &SwScrollBar::valueChanged, this, [this](int) {
            updateIndexWidgetsGeometry();
            update();
        });
    }

    SwRect viewportRect(const SwRect& bounds) const {
        const int pad = 6;
        return SwRect{bounds.x + pad, bounds.y + pad, std::max(0, bounds.width - pad * 2), std::max(0, bounds.height - pad * 2)};
    }

    SwRect contentViewportRect(const SwRect& bounds) const {
        SwRect viewport = viewportRect(bounds);
        const int thickness = m_scrollBarThickness;
        const bool showH = m_hBar && m_hBar->getVisible();
        const bool showV = m_vBar && m_vBar->getVisible();
        viewport.width = std::max(0, viewport.width - (showV ? thickness : 0));
        viewport.height = std::max(0, viewport.height - (showH ? thickness : 0));
        return viewport;
    }

    void updateLayout() {
        const SwRect bounds = rect();
        SwRect viewport = viewportRect(bounds);
        const int thickness = m_scrollBarThickness;

        bool showH = false;
        bool showV = false;

        int contentW = contentWidth();
        int contentH = contentHeight();

        int viewportW = viewport.width;
        int viewportH = viewport.height;

        for (int pass = 0; pass < 2; ++pass) {
            showV = (m_vPolicy == SwScrollBarPolicy::ScrollBarAlwaysOn)
                        ? true
                        : (m_vPolicy == SwScrollBarPolicy::ScrollBarAlwaysOff) ? false : (contentH > viewportH);
            showH = (m_hPolicy == SwScrollBarPolicy::ScrollBarAlwaysOn)
                        ? true
                        : (m_hPolicy == SwScrollBarPolicy::ScrollBarAlwaysOff) ? false : (contentW > viewportW);
            viewportW = viewport.width - (showV ? thickness : 0);
            viewportH = viewport.height - (showH ? thickness : 0);
            viewportW = std::max(0, viewportW);
            viewportH = std::max(0, viewportH);
        }

        if (showH) {
            m_hBar->show();
            m_hBar->move(viewport.x - bounds.x, viewport.y + viewportH - bounds.y);
            m_hBar->resize(viewportW, thickness);
        } else {
            m_hBar->hide();
            m_hBar->setValue(0);
        }

        if (showV) {
            m_vBar->show();
            m_vBar->move(viewport.x + viewportW - bounds.x, viewport.y - bounds.y);
            m_vBar->resize(thickness, viewportH);
        } else {
            m_vBar->hide();
            m_vBar->setValue(0);
        }
    }

    int columnWidthInternal(int column) const {
        if (column < 0) {
            return defaultColumnWidth();
        }
        return m_header ? m_header->sectionSize(column) : defaultColumnWidth();
    }

    int columnsWidth() const {
        return m_header ? m_header->length() : 0;
    }

    int columnStartX(int column) const {
        if (column <= 0) {
            return 0;
        }
        return m_header ? m_header->sectionStart(column) : 0;
    }

    int columnAtX(int x) const {
        if (x < 0) {
            return -1;
        }
        return m_header ? m_header->sectionAt(x) : -1;
    }

    int textWidthPx(const SwString& text, const SwFont& font) const {
        const int fallback = static_cast<int>(text.size()) * 7;
        int w = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), text, font, text.size(), fallback);
        if (w < 0) {
            w = 0;
        }
        return w;
    }

    int columnCount() const { return m_model ? std::max(0, m_model->columnCount()) : 0; }
    int rowCount() const { return m_model ? std::max(0, m_model->rowCount()) : 0; }

    int contentWidth() const {
        return columnsWidth();
    }

    int contentHeight() const {
        const int rows = rowCount();
        const int headerH = m_headerHeight;
        return headerH + rows * m_rowHeight;
    }

    void resetScrollBars() {
        updateLayout();
        if (m_header) {
            const SwRect bounds = rect();
            const SwRect contentViewport = contentViewportRect(bounds);
            m_header->setViewportLength(std::max(0, contentViewport.width));
            m_header->setOffset(m_hBar ? m_hBar->value() : 0);
        }
        if (m_header && m_header->sectionsFitToWidth()) {
            updateLayout();
            const SwRect bounds = rect();
            const SwRect contentViewport = contentViewportRect(bounds);
            m_header->setViewportLength(std::max(0, contentViewport.width));
        }

        updateHeaderGeometry_();

        const SwRect bounds = rect();
        SwRect viewport = viewportRect(bounds);
        const int thickness = m_scrollBarThickness;

        const bool showH = m_hBar && m_hBar->getVisible();
        const bool showV = m_vBar && m_vBar->getVisible();

        const int viewportW = viewport.width - (showV ? thickness : 0);
        const int viewportH = viewport.height - (showH ? thickness : 0);

        const int maxX = std::max(0, contentWidth() - std::max(0, viewportW));
        const int maxY = std::max(0, contentHeight() - std::max(0, viewportH));

        if (m_hBar) {
            m_hBar->setRange(0, maxX);
            m_hBar->setPageStep(std::max(1, viewportW));
        }
        if (m_vBar) {
            m_vBar->setRange(0, maxY);
            m_vBar->setPageStep(std::max(1, viewportH));
        }
    }

    void paintRows(SwPainter* painter, const SwRect& viewport, int headerH) {
        if (!painter) {
            return;
        }

        const int cols = columnCount();
        const int rows = rowCount();
        if (!m_model || cols <= 0 || rows <= 0) {
            return;
        }

        const int offsetX = m_hBar ? m_hBar->value() : 0;
        const int offsetY = m_vBar ? m_vBar->value() : 0;

        const SwRect dataArea{viewport.x, viewport.y + headerH, viewport.width, std::max(0, viewport.height - headerH)};

        const int firstRow = (m_rowHeight > 0) ? std::max(0, offsetY / m_rowHeight) : 0;
        const int yWithin = (m_rowHeight > 0) ? (offsetY - firstRow * m_rowHeight) : 0;

        int y = dataArea.y - yWithin;
        const SwColor textColor{30, 41, 59};
        const SwColor gridColor{226, 232, 240};
        const SwColor altFill{248, 250, 252};
        const SwColor selFill{219, 234, 254};
        const SwColor selBorder{147, 197, 253};
        const SwFont font(L"Segoe UI", 9, Normal);

        for (int r = firstRow; r < rows && y < dataArea.y + dataArea.height; ++r) {
            SwRect rowRect{dataArea.x, y, dataArea.width, m_rowHeight};
            if (m_alternating && (r % 2) == 1) {
                painter->fillRect(rowRect, altFill, altFill, 0);
            }

            const SwModelIndex rowIdx = m_model ? m_model->index(r, 0, SwModelIndex()) : SwModelIndex();
            const bool rowSelected = m_selectionModel && rowIdx.isValid() && m_selectionModel->isSelected(rowIdx);
            if (rowSelected) {
                SwRect highlight{rowRect.x + 2, rowRect.y + 2, std::max(0, rowRect.width - 4), std::max(0, rowRect.height - 4)};
                painter->fillRoundedRect(highlight, 8, selFill, selBorder, 1);
            }

            int xContent = 0;
            for (int c = 0; c < cols; ++c) {
                const int colW = columnWidthInternal(c);
                const int x = dataArea.x + xContent - offsetX;
                SwRect cell{x, y, colW, m_rowHeight};
                if (cell.x + cell.width < dataArea.x || cell.x > dataArea.x + dataArea.width) {
                    xContent += colW;
                    continue;
                }

                const SwModelIndex idx = m_model->index(r, c, SwModelIndex());
                const bool hasWidget = idx.isValid() && indexWidget(idx);
                if (!hasWidget) {
                    SwAny v = m_model->data(idx, SwItemDataRole::DisplayRole);
                    SwString text = v.toString();

                    SwRect textRect{cell.x + 10, cell.y, std::max(0, cell.width - 20), cell.height};
                    painter->drawText(textRect,
                                      text,
                                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      textColor,
                                      font);
                }

                if (m_showGrid) {
                    painter->drawLine(cell.x, cell.y + cell.height, cell.x + cell.width, cell.y + cell.height, gridColor, 1);
                    painter->drawLine(cell.x + cell.width, cell.y, cell.x + cell.width, cell.y + cell.height, gridColor, 1);
                }

                xContent += colW;
            }

            y += m_rowHeight;
        }
    }

    int columnAtContentX(int x) const {
        return columnAtX(x);
    }

    void ensureIndexVisible(const SwModelIndex& index) {
        if (!index.isValid() || !m_hBar || !m_vBar) {
            return;
        }
        const SwRect bounds = rect();
        const SwRect viewport = viewportRect(bounds);
        const int thickness = m_scrollBarThickness;
        const bool showH = m_hBar->getVisible();
        const bool showV = m_vBar->getVisible();
        const int viewportW = viewport.width - (showV ? thickness : 0);
        const int viewportH = viewport.height - (showH ? thickness : 0);

        const int headerH = m_headerHeight;

        const int cellW = columnWidthInternal(index.column());
        const int cellX = columnStartX(index.column());
        const int cellY = headerH + index.row() * m_rowHeight;

        const int viewX = m_hBar->value();
        const int viewY = m_vBar->value();

        if (cellX < viewX) {
            m_hBar->setValue(cellX);
        } else if (cellX + cellW > viewX + viewportW) {
            m_hBar->setValue(std::max(0, cellX + cellW - viewportW));
        }

        if (cellY < viewY) {
            m_vBar->setValue(cellY);
        } else if (cellY + m_rowHeight > viewY + viewportH) {
            m_vBar->setValue(std::max(0, cellY + m_rowHeight - viewportH));
        }
    }

    void updateIndexWidgetsGeometry() {
        if (!m_model) {
            for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
                if (m_indexWidgets[i].widget) {
                    m_indexWidgets[i].widget->hide();
                }
            }
            return;
        }

        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        if (viewport.width <= 0 || viewport.height <= 0) {
            return;
        }

        const int headerH = std::min(m_headerHeight, viewport.height);
        const int offsetX = m_hBar ? m_hBar->value() : 0;
        const int offsetY = m_vBar ? m_vBar->value() : 0;

        SwRect dataClip{viewport.x, viewport.y + headerH, viewport.width, viewport.height - headerH};
        if (dataClip.height < 0) {
            dataClip.height = 0;
        }

        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            SwWidget* w = m_indexWidgets[i].widget;
            const SwModelIndex idx = m_indexWidgets[i].index;
            if (!w) {
                continue;
            }
            if (!idx.isValid() || idx.model() != m_model) {
                w->hide();
                continue;
            }

            const int colW = columnWidthInternal(idx.column());
            const int cellX = viewport.x + columnStartX(idx.column()) - offsetX;
            const int cellY = viewport.y + headerH + idx.row() * m_rowHeight - offsetY;
            SwRect cell{cellX, cellY, colW, m_rowHeight};

            if (!rectContains(dataClip, cell)) {
                w->hide();
                continue;
            }

            const int padX = 8;
            const int padY = 4;
            int newW = w->frameGeometry().width;
            int newH = w->frameGeometry().height;
            const int maxW = std::max(0, cell.width - padX * 2);
            const int maxH = std::max(0, cell.height - padY * 2);
            if (newW > maxW) newW = maxW;
            if (newH > maxH) newH = maxH;
            if (newW < 0) newW = 0;
            if (newH < 0) newH = 0;

            int x = cell.x + (cell.width - newW) / 2;
            int y = cell.y + (cell.height - newH) / 2;

            const int minX = cell.x + padX;
            const int maxX = cell.x + cell.width - padX - newW;
            if (x < minX) x = minX;
            if (x > maxX) x = maxX;

            const int minY = cell.y + padY;
            const int maxY = cell.y + cell.height - padY - newH;
            if (y < minY) y = minY;
            if (y > maxY) y = maxY;

            w->resize(newW, newH);
            w->move(x, y);
            w->show();
        }
    }

    struct IndexWidgetEntry {
        SwModelIndex index;
        SwWidget* widget{nullptr};
    };

    SwAbstractItemModel* m_model{nullptr};
    SwItemSelectionModel* m_selectionModel{nullptr};
    SwHeaderView* m_header{nullptr};
    SwScrollBar* m_hBar{nullptr};
    SwScrollBar* m_vBar{nullptr};
    bool m_alternating{true};
    bool m_showGrid{true};
    bool m_sortingEnabled{true};
    int m_sortColumn{-1};
    SwSortOrder m_sortOrder{SwSortOrder::AscendingOrder};
    SwScrollBarPolicy m_hPolicy{SwScrollBarPolicy::ScrollBarAsNeeded};
    SwScrollBarPolicy m_vPolicy{SwScrollBarPolicy::ScrollBarAsNeeded};
    int m_defaultColumnWidth{160};
    int m_rowHeight{26};
    int m_headerHeight{32};
    int m_scrollBarThickness{14};

    bool m_dragEnabled{false};
    bool m_acceptDrops{false};
    bool m_dropIndicatorShown{false};
    bool m_pressed{false};
    bool m_dragging{false};
    bool m_resizingRowHeight{false};
    int m_pressX{0};
    int m_pressY{0};
    int m_rowResizeStartY{0};
    int m_rowResizeStartHeight{0};
    SwModelIndex m_pressIndex;
    SwModelIndex m_dragIndex;
    SwModelIndex m_dropIndex;
    SwModelIndex m_hoverIndex;

    SwList<IndexWidgetEntry> m_indexWidgets;
};

inline void SwTableView::updateHeaderSortIndicator_() {
    if (!m_header) {
        return;
    }
    m_header->setSortIndicatorShown(m_sortingEnabled);
    m_header->setSortIndicator(m_sortingEnabled ? m_sortColumn : -1, m_sortOrder);
}

inline void SwTableView::updateHeaderGeometry_() {
    if (!m_header) {
        return;
    }
    const SwRect bounds = rect();
    const SwRect viewport = contentViewportRect(bounds);
    const int headerH = std::min(m_headerHeight, viewport.height);

    m_header->move(viewport.x - bounds.x, viewport.y - bounds.y);
    m_header->resize(viewport.width, headerH);
    m_header->setViewportLength(std::max(0, viewport.width));
    m_header->setOffset(m_hBar ? m_hBar->value() : 0);
    if (headerH > 0) {
        m_header->show();
    } else {
        m_header->hide();
    }
}

