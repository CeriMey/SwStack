#pragma once

/**
 * @file src/core/gui/SwTreeView.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwTreeView in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the tree view interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwTreeView.
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
#include <algorithm>
#include <cstdint>
#include <cstdlib>

class SwTreeView : public SwWidget {
    SW_OBJECT(SwTreeView, SwWidget)

public:
    /**
     * @brief Constructs a `SwTreeView` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwTreeView(SwWidget* parent = nullptr)
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
            rebuildVisible();
            resetScrollBars();
            update();
            return;
        }

        clearIndexWidgets();

        if (m_model) {
            SwObject::disconnect(m_model, this);
        }

        m_model = model;
        if (m_header) {
            m_header->setModel(m_model);
        }
        if (!m_selectionModel) {
            m_selectionModel = new SwItemSelectionModel(m_model, this);
        } else {
            m_selectionModel->setModel(m_model);
        }

        if (m_model) {
            SwObject::connect(m_model, &SwAbstractItemModel::modelReset, this, [this]() {
                const SwModelIndex prevCurrent = m_selectionModel ? m_selectionModel->currentIndex() : SwModelIndex();
                const SwModelIndex prevAnchor = m_selectionModel ? m_selectionModel->anchorIndex() : SwModelIndex();
                const SwList<SwModelIndex> prevSelected = m_selectionModel ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();

                m_expanded.clear();
                rebuildVisible();

                if (m_selectionModel) {
                    SwList<SwModelIndex> remappedSelected;
                    remappedSelected.reserve(prevSelected.size());
                    for (size_t i = 0; i < prevSelected.size(); ++i) {
                        const SwModelIndex& s = prevSelected[i];
                        if (!s.isValid() || s.model() != m_model) {
                            continue;
                        }
                        const SwModelIndex ri = findIndexForInternalPointer_(s.internalPointer(), 0);
                        if (ri.isValid()) {
                            remappedSelected.append(ri);
                        }
                    }
                    m_selectionModel->setSelectedIndexes(remappedSelected);

                    const SwModelIndex ra = prevAnchor.isValid() ? findIndexForInternalPointer_(prevAnchor.internalPointer(), 0) : SwModelIndex();
                    m_selectionModel->setAnchorIndex(ra);

                    if (prevCurrent.isValid() && prevCurrent.model() == m_model) {
                        const SwModelIndex rc = findIndexForInternalPointer_(prevCurrent.internalPointer(), prevCurrent.column());
                        m_selectionModel->setCurrentIndex(rc, false, false);
                    } else {
                        m_selectionModel->setCurrentIndex(SwModelIndex(), false, false);
                    }
                }

                resetScrollBars();
                update();
            });
            SwObject::connect(m_model, &SwAbstractItemModel::dataChanged, this, [this](const SwModelIndex&, const SwModelIndex&) {
                update();
            });
        }

        rebuildVisible();
        resetScrollBars();
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
    void setDragEnabled(bool on) {
        if (m_dragEnabled == on) {
            return;
        }
        m_dragEnabled = on;
        if (!m_dragEnabled) {
            resetDragDropState_(true);
            update();
        }
    }
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
    void setAcceptDrops(bool on) {
        if (m_acceptDrops == on) {
            return;
        }
        m_acceptDrops = on;
        if (!m_acceptDrops) {
            m_dropIndex = SwModelIndex();
            m_dropVisibleRow = -1;
            update();
        }
    }
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
    void setDropIndicatorShown(bool on) {
        if (m_dropIndicatorShown == on) {
            return;
        }
        m_dropIndicatorShown = on;
        if (!m_dropIndicatorShown) {
            m_dropIndex = SwModelIndex();
            m_dropVisibleRow = -1;
            update();
        }
    }
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

        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            if (m_indexWidgets[i].index == index) {
                if (m_indexWidgets[i].widget == widget) {
                    if (widget) {
                        m_indexWidgets[i].hint = widget->sizeHint();
                    }
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
        entry.hint = widget->sizeHint();
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

    /**
     * @brief Sets the icon Size.
     * @param m_iconSize Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIconSize(int px) { m_iconSize = std::max(8, px); update(); }
    /**
     * @brief Returns the current icon Size.
     * @return The current icon Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int  iconSize() const { return m_iconSize; }

signals:
    DECLARE_SIGNAL(clicked, const SwModelIndex&);
    DECLARE_SIGNAL(contextMenuRequested, const SwModelIndex&, int, int);
    DECLARE_SIGNAL(dragStarted, const SwModelIndex&);
    DECLARE_SIGNAL(dragMoved, const SwModelIndex&, int, int);
    DECLARE_SIGNAL(dragDropped, const SwModelIndex&, const SwModelIndex&);

    /**
     * @brief Performs the `expand` operation.
     * @param index Value passed to the method.
     */
    void expand(const SwModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        setExpandedInternal(index, true);
    }

    /**
     * @brief Performs the `collapse` operation.
     * @param index Value passed to the method.
     */
    void collapse(const SwModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        setExpandedInternal(index, false);
    }

    /**
     * @brief Returns whether the object reports expanded.
     * @param index Value passed to the method.
     * @return `true` when the object reports expanded; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isExpanded(const SwModelIndex& index) const {
        if (!index.isValid()) {
            return false;
        }
        return isExpandedInternal(index);
    }

    /**
     * @brief Sets the indentation.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIndentation(int px) {
        m_indent = std::max(10, px);
        update();
    }

    /**
     * @brief Returns the current indentation.
     * @return The current indentation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int indentation() const { return m_indent; }

    // Override the fixed left padding before item text/icon (default 0 = use built-in toggle area size).
    /**
     * @brief Sets the content Left Pad.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setContentLeftPad(int px) {
        m_itemContentLeftPad = std::max(0, px);
        update();
    }
    /**
     * @brief Returns the current content Left Pad.
     * @return The current content Left Pad.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int contentLeftPad() const { return m_itemContentLeftPad; }

    /**
     * @brief Sets the row Height.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRowHeight(int px) {
        m_rowHeight = std::max(18, px);
        rebuildVisible();
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

    // If false, hide expand arrows even for items with children
    /**
     * @brief Sets the root Is Decorated.
     * @param show Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRootIsDecorated(bool show) {
        if (m_rootIsDecorated == show) return;
        m_rootIsDecorated = show;
        update();
    }
    /**
     * @brief Returns the current root Is Decorated.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool rootIsDecorated() const { return m_rootIsDecorated; }

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
     * @brief Sets the header Hidden.
     * @param hidden Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHeaderHidden(bool hidden) {
        if (m_headerHidden == hidden) {
            return;
        }
        m_headerHidden = hidden;
        resetScrollBars();
        updateHeaderGeometry_();
        update();
    }

    /**
     * @brief Returns whether the object reports header Hidden.
     * @return `true` when the object reports header Hidden; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isHeaderHidden() const { return m_headerHidden; }

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

    // Header accessor.
    /**
     * @brief Returns the current header.
     * @return The current header.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHeaderView* header() const { return m_header; }

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

        for (size_t r = 0; r < m_visible.size(); ++r) {
            const VisibleRow& vr = m_visible[r];
            const SwModelIndex parentIndex = m_model->parent(vr.index);
            SwModelIndex idx = (column == 0) ? vr.index : m_model->index(vr.index.row(), column, parentIndex);
            if (!idx.isValid()) {
                continue;
            }

            int needed = 0;
            if (SwWidget* w = indexWidget(idx)) {
                needed = std::max(0, w->frameGeometry().width) + 16;
            } else {
                SwAny v = m_model->data(idx, SwItemDataRole::DisplayRole);
                SwString text = v.toString();
                needed = textWidthPx(text, cellFont) + 20;
            }

            if (column == 0) {
                const int togglePad = 8;
                const int toggleSize = 12;
                needed += vr.depth * m_indent + togglePad + toggleSize + 18;
            }

            maxW = std::max(maxW, needed);
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
     * @brief Returns the current vertical Scroll Bar.
     * @return The current vertical Scroll Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBar* verticalScrollBar() const { return m_vBar; }
    /**
     * @brief Returns the current horizontal Scroll Bar.
     * @return The current horizontal Scroll Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBar* horizontalScrollBar() const { return m_hBar; }

private:
    bool m_resizingRowHeight{false};
    int m_rowResizeStartY{0};
    int m_rowResizeStartHeight{0};

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

        painter->fillRoundedRect(bounds, 12, SwColor{255, 255, 255}, SwColor{220, 224, 232}, 1);

        const SwRect viewport = contentViewportRect(bounds);
        if (viewport.width <= 0 || viewport.height <= 0) {
            return;
        }

        const int headerH = m_headerHidden ? 0 : std::min(m_headerHeight, viewport.height);
        updateHeaderGeometry_();

        SwRect dataClip{viewport.x, viewport.y + headerH, viewport.width, viewport.height - headerH};
        if (dataClip.height < 0) {
            dataClip.height = 0;
        }

        painter->pushClipRect(dataClip);
        paintRows(painter, viewport, headerH);

        if (m_dropIndicatorShown && m_dragging && m_dropVisibleRow >= 0 && m_rowHeight > 0) {
            const int offsetY = m_vBar ? m_vBar->value() : 0;
            const int y = viewport.y + headerH + m_dropVisibleRow * m_rowHeight - offsetY;
            SwRect dropRect{viewport.x, y, viewport.width, m_rowHeight};
            painter->drawRect(dropRect, SwColor{59, 130, 246}, 2);
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

        resetDragDropState_(true);

        if (SwWidget* childWidget = getChildUnderCursor(event->x(), event->y())) {
            MouseEvent childEvent = mapMouseEventToChild_(*event, this, childWidget);
            static_cast<SwWidgetInterface*>(childWidget)->mousePressEvent(&childEvent);
            if (childEvent.isAccepted()) {
                event->accept();
                return;
            }
        }

        if (!m_model || !m_selectionModel) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const SwRect viewport = contentViewportRect(rect());
        if (!containsPoint(viewport, event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const int offsetY = m_vBar ? m_vBar->value() : 0;
        const int headerH = m_headerHidden ? 0 : std::min(m_headerHeight, viewport.height);
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

        const int localY = event->y() - (viewport.y + headerH) + offsetY;
        const int row = (m_rowHeight > 0) ? (localY / m_rowHeight) : -1;
        if (row < 0 || row >= static_cast<int>(m_visible.size())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const VisibleRow& vr = m_visible[static_cast<size_t>(row)];

        const int offsetX = m_hBar ? m_hBar->value() : 0;
        const int localX = event->x() - viewport.x + offsetX;

        const int col = columnAtX(localX);

        const SwModelIndex parentIndex = m_model->parent(vr.index);
        SwModelIndex clickedIdx = (col <= 0) ? vr.index : m_model->index(vr.index.row(), col, parentIndex);
        if (!clickedIdx.isValid()) {
            clickedIdx = vr.index;
        }

        if (event->button() == SwMouseButton::Right) {
            if (m_selectionModel->isSelected(vr.index)) {
                m_selectionModel->setCurrentIndex(clickedIdx, false, false);
            } else {
                SwList<SwModelIndex> selection;
                selection.append(vr.index);
                m_selectionModel->setSelectedIndexes(selection);
                m_selectionModel->setCurrentIndex(clickedIdx, false, false);
                m_selectionModel->setAnchorIndex(vr.index);
            }
            ensureRowVisible(row);
            update();
            contextMenuRequested(clickedIdx, event->x(), event->y());
            event->accept();
            return;
        }

        if (event->button() != SwMouseButton::Left) {
            SwWidget::mousePressEvent(event);
            return;
        }

        if (col == 0) {
            const int toggleStart = vr.depth * m_indent;
            const int toggleSize = 14;
            const int togglePad = 6;
            const int toggleX0 = toggleStart + togglePad;
            const int toggleX1 = toggleX0 + toggleSize;

            if (vr.hasChildren && (m_rootIsDecorated || vr.depth > 0) && localX >= toggleX0 && localX <= toggleX1) {
                toggleExpanded(vr.index);
                event->accept();
                return;
            }
        }

        const bool ctrl = event->isCtrlPressed();
        const bool shift = event->isShiftPressed();

        if (shift) {
            SwModelIndex anchor = m_selectionModel->anchorIndex();
            if (!anchor.isValid() || anchor.model() != m_model) {
                const SwModelIndex current = m_selectionModel->currentIndex();
                if (current.isValid() && current.model() == m_model) {
                    anchor = (current.column() == 0) ? current : m_model->index(current.row(), 0, m_model->parent(current));
                }
            }
            if (!anchor.isValid() || anchor.model() != m_model) {
                anchor = vr.index;
                m_selectionModel->setAnchorIndex(anchor);
            }

            int anchorRow = visibleRowIndexForInternalPointer_(anchor.internalPointer());
            if (anchorRow < 0) {
                anchorRow = row;
                m_selectionModel->setAnchorIndex(vr.index);
            }

            const int lo = std::max(0, std::min(anchorRow, row));
            const int hi = std::min(static_cast<int>(m_visible.size()) - 1, std::max(anchorRow, row));

            SwList<SwModelIndex> selection = ctrl ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();
            selection.reserve(selection.size() + static_cast<size_t>(hi - lo + 1));
            for (int i = lo; i <= hi; ++i) {
                const SwModelIndex ri = m_visible[static_cast<size_t>(i)].index;
                if (ri.isValid() && (!ctrl || !m_selectionModel->isSelected(ri))) {
                    selection.append(ri);
                }
            }

            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(clickedIdx, false, false);
        } else if (ctrl) {
            m_selectionModel->toggle(vr.index);
            m_selectionModel->setCurrentIndex(clickedIdx, false, false);
            m_selectionModel->setAnchorIndex(vr.index);
        } else {
            SwList<SwModelIndex> selection;
            selection.append(vr.index);
            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(clickedIdx, false, false);
            m_selectionModel->setAnchorIndex(vr.index);
        }
        ensureRowVisible(row);
        m_pressed = m_dragEnabled;
        m_dragging = false;
        m_pressX = event->x();
        m_pressY = event->y();
        m_pressIndex = vr.index;
        m_dragIndex = SwModelIndex();
        m_dropIndex = SwModelIndex();
        m_dropVisibleRow = -1;
        emit clicked(clickedIdx);
        event->accept();
        update();
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
            setToolTips(SwString());
            const int delta = event->y() - m_rowResizeStartY;
            const int newH = std::max(18, m_rowResizeStartHeight + delta);
            if (newH != m_rowHeight) {
                m_rowHeight = newH;
                rebuildVisible();
                resetScrollBars();
                updateIndexWidgetsGeometry();
                update();
            }
            event->accept();
            return;
        }

        if (!m_dragEnabled || !m_pressed) {
            if (m_header && !m_headerHidden && m_header->isVisibleInHierarchy() &&
                containsPoint(m_header->geometry(), event->x(), event->y())) {
                setToolTips(SwString());
                return;
            }

            if (m_model) {
                const int row = visibleRowAt_(event->x(), event->y());
                SwModelIndex hover;
                if (row >= 0 && row < static_cast<int>(m_visible.size())) {
                    const SwRect viewport = contentViewportRect(rect());
                    const int offsetX = m_hBar ? m_hBar->value() : 0;
                    const int localX = event->x() - viewport.x + offsetX;
                    const int col = columnAtX(localX);

                    const SwModelIndex row0 = m_visible[static_cast<size_t>(row)].index;
                    const SwModelIndex parentIndex = row0.isValid() ? m_model->parent(row0) : SwModelIndex();
                    hover = (col <= 0) ? row0 : m_model->index(row0.row(), col, parentIndex);
                    if (!hover.isValid()) {
                        hover = row0;
                    }
                }

                if (hover != m_hoverIndex) {
                    m_hoverIndex = hover;
                    SwString tip = hover.isValid() ? m_model->data(hover, SwItemDataRole::ToolTipRole).toString() : SwString();
                    setToolTips(tip);
                    update();
                }
            } else {
                if (m_hoverIndex.isValid()) {
                    m_hoverIndex = SwModelIndex();
                    setToolTips(SwString());
                    update();
                }
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
                text = SwString("Item");
            }

            SwDragDrop::instance().begin(nativeWindowHandle(), text, getFont(), event->x(), event->y(), true);
            dragStarted(m_dragIndex);
        }

        if (!m_dragging) {
            return;
        }

        SwDragDrop::instance().updatePosition(event->x(), event->y());
        dragMoved(m_dragIndex, event->x(), event->y());

        if (!m_acceptDrops || !m_dropIndicatorShown) {
            return;
        }

        const int nextRow = visibleRowAt_(event->x(), event->y());
        const SwModelIndex nextIdx = (nextRow >= 0 && nextRow < static_cast<int>(m_visible.size()))
                                         ? m_visible[static_cast<size_t>(nextRow)].index
                                         : SwModelIndex();

        if (nextRow != m_dropVisibleRow || nextIdx != m_dropIndex) {
            m_dropVisibleRow = nextRow;
            m_dropIndex = nextIdx;
            update();
        }
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
        m_dropVisibleRow = -1;

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

        int rows = static_cast<int>(m_visible.size());
        if (rows <= 0) {
            return;
        }

        if (event->isCtrlPressed() && SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'A')) {
            SwList<SwModelIndex> selection;
            selection.reserve(m_visible.size());
            for (size_t i = 0; i < m_visible.size(); ++i) {
                const SwModelIndex idx = m_visible[i].index;
                if (idx.isValid()) {
                    selection.append(idx);
                }
            }
            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setAnchorIndex(m_visible[0].index);
            if (!m_selectionModel->currentIndex().isValid()) {
                m_selectionModel->setCurrentIndex(m_visible[0].index, false, false);
            }
            ensureRowVisible(0);
            update();
            event->accept();
            return;
        }

        SwModelIndex current = m_selectionModel->currentIndex();
        int currentCol = current.isValid() ? current.column() : 0;

        int curRow = 0;
        if (current.isValid() && current.model() == m_model) {
            SwModelIndex row0 = current;
            if (row0.column() != 0) {
                row0 = m_model->index(row0.row(), 0, m_model->parent(row0));
            }
            const int found = visibleRowIndexForInternalPointer_(row0.internalPointer());
            if (found >= 0) {
                curRow = found;
            }
        }

        int targetRow = curRow;
        bool handled = true;

        if (SwWidgetPlatformAdapter::isUpArrowKey(event->key())) {
            targetRow = (targetRow > 0) ? (targetRow - 1) : 0;
        } else if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
            targetRow = (targetRow + 1 < rows) ? (targetRow + 1) : (rows - 1);
        } else if (SwWidgetPlatformAdapter::isHomeKey(event->key())) {
            targetRow = 0;
        } else if (SwWidgetPlatformAdapter::isEndKey(event->key())) {
            targetRow = rows - 1;
        } else if (SwWidgetPlatformAdapter::isLeftArrowKey(event->key())) {
            const SwModelIndex rowIdx = m_visible[static_cast<size_t>(curRow)].index;
            if (rowIdx.isValid() && rowIdx.model() == m_model && m_model->rowCount(rowIdx) > 0 && isExpandedInternal(rowIdx)) {
                setExpandedInternal(rowIdx, false);
                // Keep current on the same row after collapse.
                rows = static_cast<int>(m_visible.size());
                targetRow = visibleRowIndexForInternalPointer_(rowIdx.internalPointer());
                if (targetRow < 0) targetRow = 0;
            } else if (rowIdx.isValid() && rowIdx.model() == m_model) {
                const SwModelIndex parentIdx = m_model->parent(rowIdx);
                if (parentIdx.isValid()) {
                    const int parentRow = visibleRowIndexForInternalPointer_(parentIdx.internalPointer());
                    if (parentRow >= 0) {
                        targetRow = parentRow;
                    }
                }
            }
        } else if (SwWidgetPlatformAdapter::isRightArrowKey(event->key())) {
            const SwModelIndex rowIdx = m_visible[static_cast<size_t>(curRow)].index;
            if (rowIdx.isValid() && rowIdx.model() == m_model && m_model->rowCount(rowIdx) > 0) {
                if (!isExpandedInternal(rowIdx)) {
                    setExpandedInternal(rowIdx, true);
                    rows = static_cast<int>(m_visible.size());
                    targetRow = visibleRowIndexForInternalPointer_(rowIdx.internalPointer());
                    if (targetRow < 0) targetRow = 0;
                } else if (curRow + 1 < rows && m_visible[static_cast<size_t>(curRow + 1)].depth == m_visible[static_cast<size_t>(curRow)].depth + 1) {
                    targetRow = curRow + 1;
                }
            }
        } else {
            handled = false;
        }

        if (!handled) {
            return;
        }

        if (targetRow < 0) targetRow = 0;
        rows = static_cast<int>(m_visible.size());
        if (rows <= 0) {
            return;
        }
        if (targetRow >= rows) targetRow = rows - 1;

        const VisibleRow& vr = m_visible[static_cast<size_t>(targetRow)];
        SwModelIndex nextCurrent = vr.index;
        if (currentCol != 0 && vr.index.isValid() && vr.index.model() == m_model) {
            const SwModelIndex parentIdx = m_model->parent(vr.index);
            SwModelIndex colIdx = m_model->index(vr.index.row(), currentCol, parentIdx);
            if (colIdx.isValid()) {
                nextCurrent = colIdx;
            }
        }

        const bool ctrl = event->isCtrlPressed();
        const bool shift = event->isShiftPressed();

        if (shift) {
            SwModelIndex anchor = m_selectionModel->anchorIndex();
            if (!anchor.isValid() || anchor.model() != m_model) {
                anchor = m_visible[static_cast<size_t>(curRow)].index;
                m_selectionModel->setAnchorIndex(anchor);
            }

            int anchorRow = visibleRowIndexForInternalPointer_(anchor.internalPointer());
            if (anchorRow < 0) {
                anchorRow = targetRow;
                m_selectionModel->setAnchorIndex(vr.index);
            }

            const int lo = std::max(0, std::min(anchorRow, targetRow));
            const int hi = std::min(rows - 1, std::max(anchorRow, targetRow));

            SwList<SwModelIndex> selection = ctrl ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();
            selection.reserve(selection.size() + static_cast<size_t>(hi - lo + 1));
            for (int i = lo; i <= hi; ++i) {
                const SwModelIndex ri = m_visible[static_cast<size_t>(i)].index;
                if (ri.isValid() && (!ctrl || !m_selectionModel->isSelected(ri))) {
                    selection.append(ri);
                }
            }

            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(nextCurrent, false, false);
        } else if (ctrl) {
            m_selectionModel->setCurrentIndex(nextCurrent, false, false);
        } else {
            SwList<SwModelIndex> selection;
            selection.append(vr.index);
            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(nextCurrent, false, false);
            m_selectionModel->setAnchorIndex(vr.index);
        }

        ensureRowVisible(targetRow);
        update();
        event->accept();
    }

private:
    struct VisibleRow {
        SwModelIndex index;
        int depth{0};
        bool hasChildren{false};
    };

    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    static bool rectContains(const SwRect& outer, const SwRect& inner) {
        return inner.x >= outer.x &&
               inner.y >= outer.y &&
               (inner.x + inner.width) <= (outer.x + outer.width) &&
               (inner.y + inner.height) <= (outer.y + outer.height);
    }

    static bool rectIntersects(const SwRect& a, const SwRect& b) {
        if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) {
            return false;
        }
        return !((a.x + a.width) <= b.x ||
                 a.x >= (b.x + b.width) ||
                 (a.y + a.height) <= b.y ||
                 a.y >= (b.y + b.height));
    }

    static SwRect rectIntersection(const SwRect& a, const SwRect& b) {
        const int x1 = std::max(a.x, b.x);
        const int y1 = std::max(a.y, b.y);
        const int x2 = std::min(a.x + a.width, b.x + b.width);
        const int y2 = std::min(a.y + a.height, b.y + b.height);
        const int w = std::max(0, x2 - x1);
        const int h = std::max(0, y2 - y1);
        return SwRect{x1, y1, w, h};
    }

    void updateHeaderGeometry_() {
        if (!m_header) {
            return;
        }
        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        const int headerH = m_headerHidden ? 0 : std::min(m_headerHeight, viewport.height);

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

    bool hitRowResizeHandle_(int px, int py) const {
        if (!m_model || !m_vBar) {
            return false;
        }
        if (m_rowHeight <= 0) {
            return false;
        }
        const SwRect viewport = contentViewportRect(rect());
        if (!containsPoint(viewport, px, py)) {
            return false;
        }
        const int headerH = m_headerHidden ? 0 : std::min(m_headerHeight, viewport.height);
        if (py < viewport.y + headerH) {
            return false;
        }

        const int offsetY = m_vBar->value();
        const int localY = py - (viewport.y + headerH) + offsetY;
        if (localY < 0) {
            return false;
        }

        const int rows = static_cast<int>(m_visible.size());
        const int totalH = rows * m_rowHeight;
        if (localY >= totalH) {
            return false;
        }

        const int grab = 3;
        const int rem = (m_rowHeight > 0) ? (localY % m_rowHeight) : 0;
        return (m_rowHeight - rem) <= grab;
    }

    int visibleRowAt_(int px, int py) const {
        if (!m_model) {
            return -1;
        }
        const SwRect viewport = contentViewportRect(rect());
        if (!containsPoint(viewport, px, py)) {
            return -1;
        }
        const int offsetY = m_vBar ? m_vBar->value() : 0;
        const int headerH = m_headerHidden ? 0 : std::min(m_headerHeight, viewport.height);
        if (py < viewport.y + headerH) {
            return -1;
        }
        const int localY = py - (viewport.y + headerH) + offsetY;
        const int row = (m_rowHeight > 0) ? (localY / m_rowHeight) : -1;
        if (row < 0 || row >= static_cast<int>(m_visible.size())) {
            return -1;
        }
        return row;
    }

    int visibleRowIndexForInternalPointer_(void* ptr) const {
        if (!ptr) {
            return -1;
        }
        for (size_t i = 0; i < m_visible.size(); ++i) {
            const SwModelIndex idx = m_visible[i].index;
            if (idx.isValid() && idx.internalPointer() == ptr) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    SwModelIndex findIndexForInternalPointer_(void* ptr, int columnHint) const {
        if (!m_model || !ptr) {
            return SwModelIndex();
        }

        const int cols = std::max(1, columnCount());
        if (columnHint >= 0 && columnHint < cols) {
            for (size_t r = 0; r < m_visible.size(); ++r) {
                const SwModelIndex row0 = m_visible[r].index;
                if (!row0.isValid()) {
                    continue;
                }
                const SwModelIndex parentIndex = m_model->parent(row0);
                const SwModelIndex idx = (columnHint == 0) ? row0 : m_model->index(row0.row(), columnHint, parentIndex);
                if (idx.isValid() && idx.internalPointer() == ptr) {
                    return idx;
                }
            }
        }

        for (size_t r = 0; r < m_visible.size(); ++r) {
            const SwModelIndex row0 = m_visible[r].index;
            if (!row0.isValid()) {
                continue;
            }
            const SwModelIndex parentIndex = m_model->parent(row0);
            for (int c = 0; c < cols; ++c) {
                const SwModelIndex idx = (c == 0) ? row0 : m_model->index(row0.row(), c, parentIndex);
                if (idx.isValid() && idx.internalPointer() == ptr) {
                    return idx;
                }
            }
        }

        return SwModelIndex();
    }

    void initDefaults() {
        setStyleSheet("SwTreeView { background-color: rgba(0,0,0,0); border-width: 0px; }");

        m_header = new SwHeaderView(SwOrientation::Horizontal, this);
        m_header->setShowGrid(m_showGrid);
        m_header->setDefaultSectionSize(defaultColumnWidth());
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

        const int contentW = contentWidth();
        const int contentH = contentHeight();

        int viewportW = viewport.width;
        int viewportH = viewport.height;

        bool showH = false;
        bool showV = false;

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

    void resetScrollBars() {
        updateLayout();
        if (m_header) {
            const SwRect bounds = rect();
            const SwRect viewport = contentViewportRect(bounds);
            m_header->setViewportLength(std::max(0, viewport.width));
            m_header->setOffset(m_hBar ? m_hBar->value() : 0);
        }
        if (m_header && m_header->sectionsFitToWidth()) {
            updateLayout();
            const SwRect bounds = rect();
            const SwRect viewport = contentViewportRect(bounds);
            m_header->setViewportLength(std::max(0, viewport.width));
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

    int columnCount() const { return m_model ? std::max(0, m_model->columnCount()) : 0; }

    int contentHeight() const {
        const int headerH = m_headerHidden ? 0 : m_headerHeight;
        return headerH + static_cast<int>(m_visible.size()) * m_rowHeight;
    }

    int contentWidth() const {
        const int baseCol = columnsWidth();
        if (columnsFitToWidth()) {
            return baseCol;
        }
        int maxDepth = 0;
        for (const VisibleRow& r : m_visible) {
            if (r.depth > maxDepth) {
                maxDepth = r.depth;
            }
        }
        return baseCol + maxDepth * m_indent + 40;
    }

    int defaultColumnWidth() const { return 320; }

    void rebuildVisible() {
        m_visible.clear();
        if (!m_model) {
            return;
        }
        appendVisibleChildren(SwModelIndex(), 0);
    }

    void appendVisibleChildren(const SwModelIndex& parent, int depth) {
        if (!m_model) {
            return;
        }
        const int rows = std::max(0, m_model->rowCount(parent));
        const int cols = std::max(0, m_model->columnCount(parent));
        if (rows <= 0 || cols <= 0) {
            return;
        }

        for (int r = 0; r < rows; ++r) {
            SwModelIndex idx = m_model->index(r, 0, parent);
            if (!idx.isValid()) {
                continue;
            }
            const bool hasChildren = m_model->rowCount(idx) > 0;
            VisibleRow vr;
            vr.index = idx;
            vr.depth = depth;
            vr.hasChildren = hasChildren;
            m_visible.append(vr);

            if (hasChildren && isExpandedInternal(idx)) {
                appendVisibleChildren(idx, depth + 1);
            }
        }
    }

    int indexKey(const SwModelIndex& index) const {
        // Stable enough for the lifetime of the model: use internal pointer address.
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(index.internalPointer()) & 0x7FFFFFFF);
    }

    bool isExpandedInternal(const SwModelIndex& index) const {
        const int key = indexKey(index);
        for (size_t i = 0; i < m_expanded.size(); ++i) {
            if (m_expanded[i] == key) {
                return true;
            }
        }
        return false;
    }

    void setExpandedInternal(const SwModelIndex& index, bool on) {
        const int key = indexKey(index);
        bool found = false;
        for (size_t i = 0; i < m_expanded.size(); ++i) {
            if (m_expanded[i] == key) {
                found = true;
                if (!on) {
                    m_expanded.removeAt(i);
                }
                break;
            }
        }
        if (on && !found) {
            m_expanded.append(key);
        }
        rebuildVisible();
        resetScrollBars();
        update();
    }

    void toggleExpanded(const SwModelIndex& index) {
        setExpandedInternal(index, !isExpandedInternal(index));
    }

    void paintRows(SwPainter* painter, const SwRect& viewport, int headerH) {
        if (!painter || !m_model) {
            return;
        }
        const int rows = static_cast<int>(m_visible.size());
        if (rows <= 0) {
            return;
        }

        const int cols = std::max(1, columnCount());

        const int offsetY = m_vBar ? m_vBar->value() : 0;
        const int offsetX = m_hBar ? m_hBar->value() : 0;

        const int firstRow = (m_rowHeight > 0) ? std::max(0, offsetY / m_rowHeight) : 0;
        const int yWithin = (m_rowHeight > 0) ? (offsetY - firstRow * m_rowHeight) : 0;

        const SwRect dataArea{viewport.x, viewport.y + headerH, viewport.width, std::max(0, viewport.height - headerH)};

        int y = dataArea.y - yWithin;

        const SwColor altFill{249, 249, 249};
        const SwColor selFill{204, 228, 247};
        const SwColor selBorder{0, 103, 192};
        const SwColor hoverFill{229, 229, 229};
        const SwColor textColor{32, 32, 32};
        const SwColor toggleStroke{96, 96, 96};
        const SwColor gridColor{226, 232, 240};
        const SwFont font(L"Segoe UI", 9, Normal);

        for (int row = firstRow; row < rows && y < dataArea.y + dataArea.height; ++row) {
            const VisibleRow& vr = m_visible[static_cast<size_t>(row)];

            SwRect rowRect{dataArea.x, y, dataArea.width, m_rowHeight};
            if (m_alternating && (row % 2) == 1) {
                painter->fillRect(rowRect, altFill, altFill, 0);
            }

            // Separator / section-header rows: special rendering
            if (vr.index.isValid()) {
                SwAny sepTip = m_model->data(vr.index, SwItemDataRole::ToolTipRole);
                const SwString tipStr = sepTip.toString();
                if (tipStr == "__separator__") {
                    const int lineY = y + m_rowHeight / 2;
                    painter->drawLine(dataArea.x + 8, lineY, dataArea.x + dataArea.width - 8, lineY,
                                      gridColor, 1);
                    y += m_rowHeight;
                    continue;
                }
                if (tipStr == "__section__") {
                    SwAny v = m_model->data(vr.index, SwItemDataRole::DisplayRole);
                    SwRect sectionRect{dataArea.x + 10, y, std::max(0, dataArea.width - 10), m_rowHeight};
                    painter->drawText(sectionRect,
                                      v.toString(),
                                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      SwColor{128, 128, 128},
                                      SwFont(L"Segoe UI", 8, Normal));
                    y += m_rowHeight;
                    continue;
                }
            }

            const bool selected = m_selectionModel && vr.index.isValid() && m_selectionModel->isSelected(vr.index);
            const bool hovered = !selected && m_hoverIndex.isValid() && m_hoverIndex == vr.index;
            if (selected) {
                SwRect highlight{rowRect.x + 2, rowRect.y + 2, std::max(0, rowRect.width - 4), std::max(0, rowRect.height - 4)};
                painter->fillRoundedRect(highlight, 6, selFill, selBorder, 1);
            } else if (hovered) {
                SwRect hi{rowRect.x + 2, rowRect.y + 2, std::max(0, rowRect.width - 4), std::max(0, rowRect.height - 4)};
                painter->fillRoundedRect(hi, 6, hoverFill, hoverFill, 0);
            }

            const int indentX = vr.depth * m_indent;
            const int toggleSize = 12;
            const int togglePad = 8;
            // If a custom left pad is set, use it instead of the standard toggle area width
            const int itemBasePad = (m_itemContentLeftPad > 0) ? m_itemContentLeftPad : (togglePad + toggleSize + 8);
            const int toggleX = dataArea.x + indentX + togglePad - offsetX;
            const int toggleY = y + (m_rowHeight - toggleSize) / 2;

            const SwModelIndex parentIndex = m_model->parent(vr.index);
            int xContent = 0;
            for (int c = 0; c < cols; ++c) {
                const int colW = columnWidthInternal(c);
                const int x = dataArea.x + xContent - offsetX;
                SwRect cell{x, y, colW, m_rowHeight};
                if (cell.x + cell.width < dataArea.x || cell.x > dataArea.x + dataArea.width) {
                    xContent += colW;
                    continue;
                }

                SwModelIndex idx = (c == 0) ? vr.index : m_model->index(vr.index.row(), c, parentIndex);
                const bool hasWidget = idx.isValid() && indexWidget(idx);

                if (!hasWidget) {
                    if (c == 0) {
                        if (vr.hasChildren && (m_rootIsDecorated || vr.depth > 0)) {
                            const bool expanded = isExpandedInternal(vr.index);
                            if (expanded) {
                                painter->drawLine(toggleX + 2, toggleY + 4, toggleX + toggleSize - 2, toggleY + 4, toggleStroke, 2);
                                painter->drawLine(toggleX + 3, toggleY + 5, toggleX + toggleSize / 2, toggleY + toggleSize - 2, toggleStroke, 2);
                                painter->drawLine(toggleX + toggleSize - 3, toggleY + 5, toggleX + toggleSize / 2, toggleY + toggleSize - 2, toggleStroke, 2);
                            } else {
                                painter->drawLine(toggleX + 4, toggleY + 2, toggleX + 4, toggleY + toggleSize - 2, toggleStroke, 2);
                                painter->drawLine(toggleX + 5, toggleY + 3, toggleX + toggleSize - 2, toggleY + toggleSize / 2, toggleStroke, 2);
                                painter->drawLine(toggleX + 5, toggleY + toggleSize - 3, toggleX + toggleSize - 2, toggleY + toggleSize / 2, toggleStroke, 2);
                            }
                        }

                        SwAny v = m_model->data(vr.index, SwItemDataRole::DisplayRole);
                        SwString text = v.toString();

                        int iconShift = 0;
                        SwAny decoData = m_model->data(vr.index, SwItemDataRole::DecorationRole);
                        if (decoData.canConvert<SwImage>()) {
                            const SwImage& ico = decoData.get<SwImage>();
                            if (!ico.isNull()) {
                                const int iSz = m_iconSize;
                                const int ix = cell.x + indentX + itemBasePad;
                                const int iy = y + (m_rowHeight - iSz) / 2;
                                painter->drawImage(SwRect{ix, iy, iSz, iSz}, ico, nullptr);
                                iconShift = iSz + 4;
                            }
                        }

                        const int textX = cell.x + indentX + itemBasePad + iconShift;
                        SwRect textRect{textX, y, std::max(0, cell.width - (textX - cell.x) - 10), m_rowHeight};
                        painter->drawText(textRect,
                                          text,
                                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                          textColor,
                                          font);
                    } else if (idx.isValid()) {
                        SwAny v = m_model->data(idx, SwItemDataRole::DisplayRole);
                        SwString text = v.toString();
                        SwRect textRect{cell.x + 10, cell.y, std::max(0, cell.width - 20), cell.height};
                        painter->drawText(textRect,
                                          text,
                                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                          textColor,
                                          font);
                    }
                }

                if (m_showGrid) {
                    painter->drawLine(cell.x + cell.width, cell.y, cell.x + cell.width, cell.y + cell.height, gridColor, 1);
                    painter->drawLine(cell.x, cell.y + cell.height, cell.x + cell.width, cell.y + cell.height, gridColor, 1);
                }
                xContent += colW;
            }

            y += m_rowHeight;
        }
    }

    void ensureRowVisible(int row) {
        if (!m_vBar) {
            return;
        }
        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        const int thickness = m_scrollBarThickness;
        const bool showH = m_hBar && m_hBar->getVisible();
        const bool showV = m_vBar->getVisible();
        const int viewportH = viewport.height - (showH ? thickness : 0);

        const int headerH = m_headerHidden ? 0 : m_headerHeight;
        const int rowY = headerH + row * m_rowHeight;
        const int viewY = m_vBar->value();

        if (rowY < viewY) {
            m_vBar->setValue(rowY);
        } else if (rowY + m_rowHeight > viewY + viewportH) {
            m_vBar->setValue(std::max(0, rowY + m_rowHeight - viewportH));
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

        const int headerH = m_headerHidden ? 0 : std::min(m_headerHeight, viewport.height);
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

            const int col = idx.column();

            SwModelIndex rowIndex = idx;
            if (rowIndex.column() != 0) {
                rowIndex = m_model->index(rowIndex.row(), 0, m_model->parent(rowIndex));
            }
            if (!rowIndex.isValid()) {
                w->hide();
                continue;
            }

            int visibleRow = -1;
            int depth = 0;
            for (size_t r = 0; r < m_visible.size(); ++r) {
                if (m_visible[r].index == rowIndex) {
                    visibleRow = static_cast<int>(r);
                    depth = m_visible[r].depth;
                    break;
                }
            }
            if (visibleRow < 0) {
                w->hide();
                continue;
            }

            const int colW = columnWidthInternal(col);
            const int cellX = viewport.x + columnStartX(col) - offsetX;
            const int cellY = viewport.y + headerH + visibleRow * m_rowHeight - offsetY;
            SwRect cell{cellX, cellY, colW, m_rowHeight};

            if (!rectIntersects(dataClip, cell)) {
                w->hide();
                continue;
            }

            const int padX = 8;
            const int padY = 4;

            int insetX = cell.x + padX;
            int insetW = std::max(0, cell.width - padX * 2);
            if (col == 0) {
                // Keep space for indentation + chevron.
                const int indentX = depth * m_indent;
                const int togglePad = 8;
                const int toggleSize = 12;
                insetX = cell.x + indentX + togglePad + toggleSize + 10;
                insetW = std::max(0, cell.width - (insetX - cell.x) - padX);
            }

            const int maxW = insetW;
            const int maxH = std::max(0, cell.height - padY * 2);
            const SwSize hint = m_indexWidgets[i].hint;
            int newW = std::max(0, maxW);
            int newH = hint.height > 0 ? std::min(hint.height, maxH) : maxH;
            if (newH < 0) newH = 0;

            const int x = insetX;
            const int y = cell.y + (cell.height - newH) / 2;

            SwRect widgetRect{x, y, newW, newH};
            widgetRect = rectIntersection(widgetRect, dataClip);
            if (widgetRect.width <= 0 || widgetRect.height <= 0) {
                w->hide();
                continue;
            }

            w->resize(widgetRect.width, widgetRect.height);
            w->move(widgetRect.x, widgetRect.y);
            w->show();
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
        if (x <= 0) {
            return 0;
        }
        if (!m_header) {
            return 0;
        }
        const int col = m_header->sectionAt(x);
        if (col >= 0) {
            return col;
        }
        return std::max(1, columnCount()) - 1;
    }

    int textWidthPx(const SwString& text, const SwFont& font) const {
        const int fallback = static_cast<int>(text.size()) * 7;
        int w = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), text, font, text.size(), fallback);
        if (w < 0) {
            w = 0;
        }
        return w;
    }

    struct IndexWidgetEntry {
        SwModelIndex index;
        SwWidget* widget{nullptr};
        SwSize hint;
    };

    void resetDragDropState_(bool endVisual) {
        if (endVisual && m_dragging) {
            SwDragDrop::instance().end();
        }
        m_pressed = false;
        m_dragging = false;
        m_pressIndex = SwModelIndex();
        m_dragIndex = SwModelIndex();
        m_dropIndex = SwModelIndex();
        m_dropVisibleRow = -1;
    }

    SwAbstractItemModel* m_model{nullptr};
    SwItemSelectionModel* m_selectionModel{nullptr};
    SwHeaderView* m_header{nullptr};
    SwScrollBar* m_hBar{nullptr};
    SwScrollBar* m_vBar{nullptr};

    int m_rowHeight{28};
    int m_indent{22};
    int m_iconSize{16};
    int m_itemContentLeftPad{0};
    bool m_rootIsDecorated{true};
    bool m_alternating{true};
    bool m_showGrid{true};
    SwScrollBarPolicy m_hPolicy{SwScrollBarPolicy::ScrollBarAsNeeded};
    SwScrollBarPolicy m_vPolicy{SwScrollBarPolicy::ScrollBarAsNeeded};
    int m_headerHeight{32};
    bool m_headerHidden{false};
    int m_scrollBarThickness{14};

    bool m_dragEnabled{false};
    bool m_acceptDrops{false};
    bool m_dropIndicatorShown{false};
    bool m_pressed{false};
    bool m_dragging{false};
    int m_pressX{0};
    int m_pressY{0};
    SwModelIndex m_pressIndex;
    SwModelIndex m_dragIndex;
    SwModelIndex m_dropIndex;
    int m_dropVisibleRow{-1};
    SwModelIndex m_hoverIndex;

    SwList<VisibleRow> m_visible;
    SwList<int> m_expanded;
    SwList<IndexWidgetEntry> m_indexWidgets;
};

