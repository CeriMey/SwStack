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
 * @file src/core/gui/SwListView.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwListView in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the list view interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwListView.
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
 * SwListView - list view for SwAbstractItemModel.
 *
 * Focus:
 * - Premium default look (web-ish spacing, hover/selection).
 * - Simple single-selection via SwItemSelectionModel.
 * - Delegate-based rendering via SwStyledItemDelegate.
 **************************************************************************************************/

#include "SwAbstractItemModel.h"
#include "SwItemSelectionModel.h"
#include "SwScrollBar.h"
#include "SwScrollBarPolicy.h"
#include "SwStyledItemDelegate.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwDragDrop.h"

#include "core/types/SwVector.h"
#include <algorithm>
#include <cstdlib>

class SwListView : public SwWidget {
    SW_OBJECT(SwListView, SwWidget)

public:
    enum class ViewMode {
        ListMode,
        IconMode,
    };

    /**
     * @brief Constructs a `SwListView` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwListView(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
        ensureDefaultDelegate();
    }

    /**
     * @brief Sets the view Mode.
     * @param mode Mode value that controls the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setViewMode(ViewMode mode) {
        if (m_viewMode == mode) {
            return;
        }
        m_viewMode = mode;
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current view Mode.
     * @return The current view Mode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ViewMode viewMode() const { return m_viewMode; }

    // Only used in IconMode. In ListMode it is ignored.
    /**
     * @brief Sets the grid Size.
     * @param size Size value used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setGridSize(const SwSize& size) {
        const int w = clampInt(size.width, 8, 512);
        const int h = clampInt(size.height, 8, 512);
        if (m_gridSize.width == w && m_gridSize.height == h) {
            return;
        }
        m_gridSize = SwSize{w, h};
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current grid Size.
     * @return The current grid Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSize gridSize() const { return m_gridSize; }

    /**
     * @brief Sets the spacing.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSpacing(int px) {
        px = clampInt(px, 0, 64);
        if (m_spacing == px) {
            return;
        }
        m_spacing = px;
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current spacing.
     * @return The current spacing.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int spacing() const { return m_spacing; }

    /**
     * @brief Sets the model.
     * @param model Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModel(SwAbstractItemModel* model) {
        if (m_model == model) {
            invalidateRowMetrics_();
            updateLayout();
            resetScrollBars();
            updateIndexWidgetsGeometry();
            update();
            return;
        }

        clearIndexWidgets();

        if (m_model) {
            SwObject::disconnect(m_model, this);
        }

        m_model = model;
        invalidateRowMetrics_();

        if (!m_selectionModel) {
            m_selectionModel = new SwItemSelectionModel(m_model, this);
        } else {
            m_selectionModel->setModel(m_model);
        }

        if (m_model) {
            SwObject::connect(m_model, &SwAbstractItemModel::modelReset, this, [this]() {
                clearIndexWidgets();
                invalidateRowMetrics_();
                resetScrollBars();
                updateIndexWidgetsGeometry();
                update();
            });
            SwObject::connect(m_model, &SwAbstractItemModel::dataChanged, this, [this](const SwModelIndex&, const SwModelIndex&) {
                invalidateRowMetrics_();
                resetScrollBars();
                update();
            });
        }

        resetScrollBars();
        updateIndexWidgetsGeometry();
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
            SwObject::connect(m_selectionModel, &SwItemSelectionModel::currentChanged, this, [this](const SwModelIndex&, const SwModelIndex&) {
                update();
            });
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
            m_dropRow = -1;
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
            m_dropRow = -1;
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
     * @brief Sets the item Delegate.
     * @param delegate Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setItemDelegate(SwStyledItemDelegate* delegate) {
        if (m_delegate == delegate) {
            return;
        }
        if (delegate && !delegate->parent()) {
            delegate->setParent(this);
        }
        m_delegate = delegate;
        ensureDefaultDelegate();
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current item Delegate.
     * @return The current item Delegate.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStyledItemDelegate* itemDelegate() const { return m_delegate; }

    /**
     * @brief Sets the uniform Row Heights.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUniformRowHeights(bool on) {
        if (m_uniformRowHeights == on) {
            return;
        }
        m_uniformRowHeights = on;
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current uniform Row Heights.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool uniformRowHeights() const { return m_uniformRowHeights; }

    /**
     * @brief Sets the viewport Padding.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setViewportPadding(int px) {
        px = clampInt(px, 0, 64);
        if (m_viewportPadding == px) {
            return;
        }
        m_viewportPadding = px;
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current viewport Padding.
     * @return The current viewport Padding.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int viewportPadding() const { return m_viewportPadding; }

    /**
     * @brief Sets the row Height.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRowHeight(int px) {
        if (px < 18) {
            px = 18;
        }
        if (m_rowHeight == px) {
            return;
        }
        m_rowHeight = px;
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
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
     * @brief Sets the scroll Bar Thickness.
     * @param px Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScrollBarThickness(int px) {
        px = clampInt(px, 0, 48);
        if (m_scrollBarThickness == px) {
            return;
        }
        m_scrollBarThickness = px;
        invalidateRowMetrics_();
        resetScrollBars();
        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Returns the current scroll Bar Thickness.
     * @return The current scroll Bar Thickness.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int scrollBarThickness() const { return m_scrollBarThickness; }

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
     * @brief Returns the current vertical Scroll Bar.
     * @return The current vertical Scroll Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwScrollBar* verticalScrollBar() const { return m_vBar; }

    /**
     * @brief Performs the `scrollToTop` operation.
     */
    void scrollToTop() {
        if (!m_vBar) {
            return;
        }
        m_vBar->setValue(0);
        update();
    }

    /**
     * @brief Performs the `scrollToBottom` operation.
     */
    void scrollToBottom() {
        if (!m_vBar) {
            return;
        }
        resetScrollBars();
        m_vBar->setValue(m_vBar->maximum());
        update();
    }

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

        // List view = column 0.
        if (index.column() != 0) {
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
                m_indexWidgets.removeAt(static_cast<int>(i));
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
                m_indexWidgets.removeAt(static_cast<int>(i));
                break;
            }
        }

        widget->setParent(this);

        IndexWidgetEntry entry;
        entry.index = index;
        entry.widget = widget;
        m_indexWidgets.push_back(entry);

        updateIndexWidgetsGeometry();
        update();
    }

    /**
     * @brief Performs the `indexAt` operation.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     * @return The requested index At.
     */
    SwModelIndex indexAt(int px, int py) const {
        if (!m_model) {
            return SwModelIndex();
        }
        const int row = rowAt(px, py);
        if (row < 0) {
            return SwModelIndex();
        }
        return m_model->index(row, 0);
    }

signals:
    DECLARE_SIGNAL(clicked, const SwModelIndex&);
    DECLARE_SIGNAL(doubleClicked, const SwModelIndex&);
    DECLARE_SIGNAL(activated, const SwModelIndex&);
    DECLARE_SIGNAL(contextMenuRequested, const SwModelIndex&, int, int);
    DECLARE_SIGNAL(dragStarted, const SwModelIndex&);
    DECLARE_SIGNAL(dragMoved, const SwModelIndex&, int, int);
    DECLARE_SIGNAL(dragDropped, const SwModelIndex&, const SwModelIndex&);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        invalidateRowMetrics_();
        resetScrollBars();
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

        // Frame styling via basic stylesheet properties (same subset as SwFrame).
        const StyleSheet* sheet = getToolSheet();
        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);
        } else if (borderWidth > 0) {
            painter->drawRect(bounds, border, borderWidth);
        }

        const SwRect viewport = contentViewportRect(bounds);
        if (viewport.width <= 0 || viewport.height <= 0) {
            return;
        }

        painter->pushClipRect(viewport);
        paintRows(painter, viewport);

        if (m_dropIndicatorShown && m_dragging && m_dropRow >= 0) {
            const int offsetY = m_vBar ? m_vBar->value() : 0;
            SwRect dropRect{viewport.x, viewport.y, viewport.width, 0};

            if (m_viewMode == ViewMode::IconMode) {
                dropRect = iconCellRect_(viewport, m_dropRow, offsetY);
            } else if (m_uniformRowHeights) {
                dropRect.y = viewport.y + m_dropRow * m_rowHeight - offsetY;
                dropRect.height = m_rowHeight;
            } else {
                ensureRowMetrics_();
                const int rows = rowCount();
                if (m_dropRow >= 0 && m_dropRow < rows && m_rowOffsets.size() >= (rows + 1)) {
                    dropRect.y = viewport.y + m_rowOffsets[m_dropRow] - offsetY;
                    dropRect.height = m_rowOffsets[m_dropRow + 1] - m_rowOffsets[m_dropRow];
                }
            }

            if (dropRect.width > 0 && dropRect.height > 0) {
                painter->drawRect(dropRect, SwColor{59, 130, 246}, 2);
            }
        }

        painter->popClipRect();

        updateIndexWidgetsGeometry();

        // Paint child widgets (scrollbar / index widgets) on top.
        for (SwObject* objChild : children()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }
            paintChild_(event, child);
        }
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

        const int row = rowAt(event->x(), event->y());
        if (row != m_hoverRow) {
            m_hoverRow = row;
            update();
        }

        if (m_pressed || m_dragging) {
            setToolTips(SwString());
        } else {
            SwString tip;
            if (m_model && row >= 0) {
                const SwModelIndex idx = m_model->index(row, 0);
                if (idx.isValid()) {
                    tip = m_model->data(idx, SwItemDataRole::ToolTipRole).toString();
                }
            }
            setToolTips(tip);
        }

        if (!m_dragEnabled || !m_pressed) {
            return;
        }
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

        if (m_acceptDrops && m_dropIndicatorShown) {
            const int nextRow = rowAt(event->x(), event->y());
            const SwModelIndex nextIdx = (nextRow >= 0) ? m_model->index(nextRow, 0) : SwModelIndex();
            if (nextRow != m_dropRow || nextIdx != m_dropIndex) {
                m_dropRow = nextRow;
                m_dropIndex = nextIdx;
                update();
            }
        }

        event->accept();
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

        // Let child widgets (scrollbar / index widgets) handle the event first.
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

        const SwModelIndex idx = indexAt(event->x(), event->y());
        if (!idx.isValid()) {
            SwWidget::mousePressEvent(event);
            return;
        }

        if (event->button() == SwMouseButton::Right) {
            if (m_selectionModel->isSelected(idx)) {
                m_selectionModel->setCurrentIndex(idx, false, false);
            } else {
                m_selectionModel->setCurrentIndex(idx, true, true);
            }
            ensureRowVisible(idx.row());
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

        if (shift) {
            SwModelIndex anchor = m_selectionModel->anchorIndex();
            if (!anchor.isValid() || anchor.model() != m_model) {
                anchor = m_selectionModel->currentIndex();
            }
            if (!anchor.isValid() || anchor.model() != m_model) {
                anchor = idx;
                m_selectionModel->setAnchorIndex(anchor);
            }

            const int a = anchor.row();
            const int b = idx.row();
            const int lo = std::max(0, std::min(a, b));
            const int hi = std::min(rowCount() - 1, std::max(a, b));

            SwList<SwModelIndex> selection = ctrl ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();
            selection.reserve(selection.size() + static_cast<size_t>(hi - lo + 1));

            for (int r = lo; r <= hi; ++r) {
                const SwModelIndex ri = m_model->index(r, 0);
                if (ri.isValid() && (!ctrl || !m_selectionModel->isSelected(ri))) {
                    selection.append(ri);
                }
            }

            m_selectionModel->setSelectedIndexes(selection);
            m_selectionModel->setCurrentIndex(idx, false, false);
        } else if (ctrl) {
            if (m_selectionModel->isSelected(idx)) {
                m_selectionModel->select(idx, false);
            } else {
                m_selectionModel->select(idx, true);
            }
            m_selectionModel->setCurrentIndex(idx, false, true);
        } else {
            m_selectionModel->setCurrentIndex(idx, true, true);
        }
        ensureRowVisible(idx.row());
        m_pressed = m_dragEnabled;
        m_dragging = false;
        m_pressX = event->x();
        m_pressY = event->y();
        m_pressIndex = idx;
        m_dragIndex = SwModelIndex();
        m_dropIndex = SwModelIndex();
        m_dropRow = -1;
        clicked(idx);
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

        const bool wasDragging = m_dragging;
        const SwModelIndex dragged = m_dragIndex;
        const SwModelIndex drop = m_dropIndex;

        m_pressed = false;
        m_dragging = false;
        m_pressIndex = SwModelIndex();
        m_dragIndex = SwModelIndex();
        m_dropIndex = SwModelIndex();
        m_dropRow = -1;

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
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwWidget::mouseDoubleClickEvent(event);
            return;
        }

        if (!m_model || !m_selectionModel) {
            SwWidget::mouseDoubleClickEvent(event);
            return;
        }

        const SwModelIndex idx = indexAt(event->x(), event->y());
        if (!idx.isValid()) {
            SwWidget::mouseDoubleClickEvent(event);
            return;
        }

        m_selectionModel->setCurrentIndex(idx);
        ensureRowVisible(idx.row());
        doubleClicked(idx);
        activated(idx);
        event->accept();
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
        if (!m_vBar) {
            SwWidget::wheelEvent(event);
            return;
        }
        if (event->isShiftPressed()) {
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

        int stepPx = std::max(1, m_rowHeight);
        if (m_viewMode == ViewMode::IconMode) {
            stepPx = std::max(1, iconStepY_());
        }
        stepPx = std::max(stepPx, m_vBar->pageStep() / 10);

        const int old = m_vBar->value();
        m_vBar->setValue(old - steps * stepPx);
        if (m_vBar->value() != old) {
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

        if (!m_model || !m_selectionModel) {
            return;
        }
        if (!getFocus()) {
            return;
        }

        const int rows = rowCount();
        if (rows <= 0) {
            return;
        }

        SwModelIndex current = m_selectionModel->currentIndex();
        int row = current.isValid() ? current.row() : 0;

        if (event->isCtrlPressed() && SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'A')) {
            SwList<SwModelIndex> all;
            all.reserve(static_cast<size_t>(rows));
            for (int r = 0; r < rows; ++r) {
                const SwModelIndex idx = m_model->index(r, 0);
                if (idx.isValid()) {
                    all.append(idx);
                }
            }
            m_selectionModel->setSelectedIndexes(all);
            if (!current.isValid()) {
                current = m_model->index(0, 0);
                if (current.isValid()) {
                    m_selectionModel->setCurrentIndex(current, false, true);
                }
            }
            ensureRowVisible(current.isValid() ? current.row() : 0);
            update();
            event->accept();
            return;
        }

        if (m_viewMode == ViewMode::IconMode) {
            const int columns = iconColumns_(contentViewportRect(rect()));
            if (SwWidgetPlatformAdapter::isLeftArrowKey(event->key())) {
                row = (row > 0) ? (row - 1) : 0;
            } else if (SwWidgetPlatformAdapter::isRightArrowKey(event->key())) {
                row = (row + 1 < rows) ? (row + 1) : (rows - 1);
            } else if (SwWidgetPlatformAdapter::isUpArrowKey(event->key())) {
                row = (row - columns >= 0) ? (row - columns) : 0;
            } else if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
                row = (row + columns < rows) ? (row + columns) : (rows - 1);
            } else if (SwWidgetPlatformAdapter::isHomeKey(event->key())) {
                row = 0;
            } else if (SwWidgetPlatformAdapter::isEndKey(event->key())) {
                row = rows - 1;
            } else if (SwWidgetPlatformAdapter::isReturnKey(event->key())) {
                if (current.isValid()) {
                    activated(current);
                    event->accept();
                }
                return;
            } else {
                return;
            }
        } else {
            if (SwWidgetPlatformAdapter::isUpArrowKey(event->key())) {
                row = (row > 0) ? (row - 1) : 0;
            } else if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
                row = (row + 1 < rows) ? (row + 1) : (rows - 1);
            } else if (SwWidgetPlatformAdapter::isHomeKey(event->key())) {
                row = 0;
            } else if (SwWidgetPlatformAdapter::isEndKey(event->key())) {
                row = rows - 1;
            } else if (SwWidgetPlatformAdapter::isReturnKey(event->key())) {
                if (current.isValid()) {
                    activated(current);
                    event->accept();
                }
                return;
            } else {
                return;
            }
        }

        SwModelIndex next = m_model->index(row, 0);
        if (next.isValid()) {
            const bool ctrl = event->isCtrlPressed();
            const bool shift = event->isShiftPressed();

            if (shift) {
                SwModelIndex anchor = m_selectionModel->anchorIndex();
                if (!anchor.isValid() || anchor.model() != m_model) {
                    anchor = current.isValid() ? current : next;
                    m_selectionModel->setAnchorIndex(anchor);
                }

                const int a = anchor.row();
                const int b = next.row();
                const int lo = std::max(0, std::min(a, b));
                const int hi = std::min(rows - 1, std::max(a, b));

                SwList<SwModelIndex> selection = ctrl ? m_selectionModel->selectedIndexes() : SwList<SwModelIndex>();
                selection.reserve(selection.size() + static_cast<size_t>(hi - lo + 1));
                for (int r = lo; r <= hi; ++r) {
                    const SwModelIndex ri = m_model->index(r, 0);
                    if (ri.isValid() && (!ctrl || !m_selectionModel->isSelected(ri))) {
                        selection.append(ri);
                    }
                }

                m_selectionModel->setSelectedIndexes(selection);
                m_selectionModel->setCurrentIndex(next, false, false);
            } else if (ctrl) {
                m_selectionModel->setCurrentIndex(next, false, false);
            } else {
                m_selectionModel->setCurrentIndex(next, true, true);
            }
            ensureRowVisible(row);
            update();
            event->accept();
        }
    }

private:
    struct IndexWidgetEntry {
        SwModelIndex index;
        SwWidget* widget{nullptr};
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
        m_dropRow = -1;
    }

    static bool containsPoint(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    static bool rectContains(const SwRect& outer, const SwRect& inner) {
        return inner.x >= outer.x &&
               inner.y >= outer.y &&
               (inner.x + inner.width) <= (outer.x + outer.width) &&
               (inner.y + inner.height) <= (outer.y + outer.height);
    }

    int rowCount() const { return m_model ? clampInt(m_model->rowCount(), 0, 1 << 30) : 0; }

    int contentHeight() const {
        if (m_viewMode == ViewMode::IconMode) {
            const SwRect bounds = rect();
            const SwRect viewport = contentViewportRect(bounds);
            return iconContentHeightForWidth_(viewport.width);
        }

        const int rows = rowCount();
        if (rows <= 0) {
            return 0;
        }
        if (m_uniformRowHeights) {
            return rows * m_rowHeight;
        }
        ensureRowMetrics_();
        return m_contentHeight;
    }

    void initDefaults() {
        setStyleSheet(R"(
            SwListView {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
            }
        )");
        setFocusPolicy(FocusPolicyEnum::Strong);

        m_vBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);
        m_vBar->hide();

        SwObject::connect(m_vBar, &SwScrollBar::valueChanged, this, [this](int) {
            updateIndexWidgetsGeometry();
            update();
        });
    }

    void ensureDefaultDelegate() {
        if (m_delegate) {
            return;
        }
        m_delegate = new SwStyledItemDelegate(this);
    }

    SwRect viewportRect(const SwRect& bounds) const {
        const int pad = m_viewportPadding;
        SwRect r{bounds.x + pad,
                 bounds.y + pad,
                 bounds.width - pad * 2,
                 bounds.height - pad * 2};
        if (r.width < 0) r.width = 0;
        if (r.height < 0) r.height = 0;
        return r;
    }

    SwRect contentViewportRect(const SwRect& bounds) const {
        SwRect viewport = viewportRect(bounds);
        const bool showV = m_vBar && m_vBar->getVisible();
        if (showV) {
            viewport.width -= m_scrollBarThickness;
            if (viewport.width < 0) {
                viewport.width = 0;
            }
        }
        return viewport;
    }

    void updateLayout() {
        const SwRect bounds = rect();
        SwRect viewport = viewportRect(bounds);

        const int viewportH = viewport.height;

        bool showV = (m_vPolicy == SwScrollBarPolicy::ScrollBarAlwaysOn)
                         ? true
                         : (m_vPolicy == SwScrollBarPolicy::ScrollBarAlwaysOff) ? false : false;

        if (m_vPolicy == SwScrollBarPolicy::ScrollBarAsNeeded) {
            if (m_viewMode == ViewMode::IconMode) {
                // contentHeight depends on viewport width (wrapping). Iterate once for scrollbar width.
                const int w0 = std::max(0, viewport.width);
                int contentH = iconContentHeightForWidth_(w0);
                bool needV = contentH > viewportH;
                if (needV) {
                    const int w1 = std::max(0, viewport.width - m_scrollBarThickness);
                    contentH = iconContentHeightForWidth_(w1);
                    needV = contentH > viewportH;
                }
                showV = needV;
            } else {
                const int contentH = contentHeight();
                showV = contentH > viewportH;
            }
        }

        const int innerW = viewport.width - (showV ? m_scrollBarThickness : 0);
        const int innerH = viewport.height;

        if (showV) {
            m_vBar->show();
            m_vBar->move(viewport.x + innerW - bounds.x, viewport.y - bounds.y);
            m_vBar->resize(m_scrollBarThickness, innerH);
        } else {
            m_vBar->hide();
            m_vBar->setValue(0);
        }
    }

    void resetScrollBars() {
        updateLayout();

        const SwRect bounds = rect();
        const SwRect viewport = viewportRect(bounds);
        const bool showV = m_vBar && m_vBar->getVisible();
        const int viewportH = viewport.height;

        const int visibleH = viewportH;
        int contentH = contentHeight();
        if (m_viewMode == ViewMode::IconMode) {
            const int innerW = std::max(0, viewport.width - (showV ? m_scrollBarThickness : 0));
            contentH = iconContentHeightForWidth_(innerW);
        }
        const int maxY = clampInt(contentH - visibleH, 0, 1 << 30);

        if (m_vBar) {
            m_vBar->setRange(0, maxY);
            m_vBar->setPageStep(visibleH > 1 ? visibleH : 1);
        }
    }

    int rowAt(int px, int py) const {
        if (!m_model) {
            return -1;
        }

        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        if (!containsPoint(viewport, px, py)) {
            return -1;
        }

        const int offsetY = m_vBar ? m_vBar->value() : 0;

        if (m_viewMode == ViewMode::IconMode) {
            const int stepX = iconStepX_();
            const int stepY = iconStepY_();
            if (stepX <= 0 || stepY <= 0) {
                return -1;
            }

            const int columns = iconColumns_(viewport);
            if (columns <= 0) {
                return -1;
            }

            const int contentX = px - viewport.x;
            const int contentY = py - viewport.y + offsetY;
            if (contentX < 0 || contentY < 0) {
                return -1;
            }

            const int col = (stepX > 0) ? (contentX / stepX) : 0;
            const int row = (stepY > 0) ? (contentY / stepY) : 0;
            if (col < 0 || col >= columns) {
                return -1;
            }

            const int xInCell = contentX - col * stepX;
            const int yInCell = contentY - row * stepY;
            if (xInCell < 0 || xInCell >= iconCellW_() || yInCell < 0 || yInCell >= iconCellH_()) {
                return -1;
            }

            const int idx = row * columns + col;
            if (idx < 0 || idx >= rowCount()) {
                return -1;
            }
            return idx;
        }

        const int contentY = py - viewport.y + offsetY;
        if (contentY < 0) {
            return -1;
        }

        if (m_uniformRowHeights) {
            const int row = (m_rowHeight > 0) ? (contentY / m_rowHeight) : -1;
            if (row < 0 || row >= rowCount()) {
                return -1;
            }
            return row;
        }

        ensureRowMetrics_();
        const int rows = rowCount();
        if (rows <= 0 || contentY >= m_contentHeight || m_rowOffsets.size() < (rows + 1)) {
            return -1;
        }

        int lo = 0;
        int hi = rows;
        while (lo < hi) {
            const int mid = lo + ((hi - lo) / 2);
            const int top = m_rowOffsets[mid];
            const int bottom = m_rowOffsets[mid + 1];
            if (contentY < top) {
                hi = mid;
            } else if (contentY >= bottom) {
                lo = mid + 1;
            } else {
                return mid;
            }
        }
        return -1;
    }

    void ensureRowVisible(int row) {
        if (!m_vBar) {
            return;
        }
        if (row < 0 || row >= rowCount()) {
            return;
        }

        const SwRect bounds = rect();
        const SwRect viewport = contentViewportRect(bounds);
        const int viewportH = viewport.height;

        const int viewY = m_vBar->value();

        if (m_viewMode == ViewMode::IconMode) {
            const int stepY = iconStepY_();
            if (stepY <= 0) {
                return;
            }
            const int columns = iconColumns_(viewport);
            if (columns <= 0) {
                return;
            }

            const int gridRow = row / columns;
            const int top = gridRow * stepY;
            const int bottom = top + iconCellH_();

            if (top < viewY) {
                m_vBar->setValue(top);
            } else if (bottom > viewY + viewportH) {
                m_vBar->setValue(clampInt(bottom - viewportH, 0, 1 << 30));
            }
            return;
        }

        if (m_uniformRowHeights) {
            const int rowY = row * m_rowHeight;
            if (rowY < viewY) {
                m_vBar->setValue(rowY);
            } else if (rowY + m_rowHeight > viewY + viewportH) {
                m_vBar->setValue(clampInt(rowY + m_rowHeight - viewportH, 0, 1 << 30));
            }
            return;
        }

        ensureRowMetrics_();
        const int rows = rowCount();
        if (rows <= 0 || m_rowOffsets.size() < (rows + 1)) {
            return;
        }

        const int rowTop = m_rowOffsets[row];
        const int rowBottom = m_rowOffsets[row + 1];

        if (rowTop < viewY) {
            m_vBar->setValue(rowTop);
        } else if (rowBottom > viewY + viewportH) {
            m_vBar->setValue(clampInt(rowBottom - viewportH, 0, 1 << 30));
        }
    }

    void paintRows(SwPainter* painter, const SwRect& viewport) {
        if (!painter || !m_model || !m_delegate) {
            return;
        }

        const SwFont font = getFont();
        const int offsetY = m_vBar ? m_vBar->value() : 0;
        const int rows = rowCount();

        if (m_viewMode == ViewMode::IconMode) {
            const int columns = iconColumns_(viewport);
            if (columns <= 0) {
                return;
            }

            const int stepY = iconStepY_();
            if (stepY <= 0) {
                return;
            }

            const int firstGridRow = (stepY > 0) ? (offsetY / stepY) : 0;
            const int yWithin = (stepY > 0) ? (offsetY - firstGridRow * stepY) : 0;
            int y = viewport.y - yWithin;

            int idx = firstGridRow * columns;
            if (idx < 0) {
                idx = 0;
            }

            const int stepX = iconStepX_();
            if (stepX <= 0) {
                return;
            }

            while (idx < rows && y <= (viewport.y + viewport.height)) {
                for (int col = 0; col < columns && idx < rows; ++col, ++idx) {
                    const int x = viewport.x + col * stepX;
                    SwRect cell{x, y, iconCellW_(), iconCellH_()};

                    const SwModelIndex modelIdx = m_model->index(idx, 0);
                    SwItemFlags flags = m_model->flags(modelIdx);
                    const bool enabled = flags.testFlag(SwItemFlag::ItemIsEnabled);
                    const bool selected = m_selectionModel && m_selectionModel->isSelected(modelIdx);
                    const bool hovered = (idx == m_hoverRow);

                    SwStyleOptionViewItem opt;
                    opt.rect = cell;
                    opt.font = font;
                    opt.enabled = enabled;
                    opt.selected = selected;
                    opt.hovered = hovered;
                    opt.hasFocus = getFocus();
                    opt.alternate = false;

                    m_delegate->paint(painter, opt, modelIdx);
                }
                y += stepY;
            }
            return;
        }

        if (m_uniformRowHeights) {
            const int firstRow = (m_rowHeight > 0) ? (offsetY / m_rowHeight) : 0;
            const int yWithin = (m_rowHeight > 0) ? (offsetY - firstRow * m_rowHeight) : 0;

            int y = viewport.y - yWithin;
            for (int r = firstRow; r < rows; ++r) {
                if (y > viewport.y + viewport.height) {
                    break;
                }

                const SwModelIndex idx = m_model->index(r, 0);
                SwItemFlags flags = m_model->flags(idx);
                const bool enabled = flags.testFlag(SwItemFlag::ItemIsEnabled);
                const bool selected = m_selectionModel && m_selectionModel->isSelected(idx);
                const bool hovered = (r == m_hoverRow);

                SwStyleOptionViewItem opt;
                opt.rect = SwRect{viewport.x, y, viewport.width, m_rowHeight};
                opt.font = font;
                opt.enabled = enabled;
                opt.selected = selected;
                opt.hovered = hovered;
                opt.hasFocus = getFocus();
                opt.alternate = m_alternating && ((r % 2) == 1);

                m_delegate->paint(painter, opt, idx);
                y += m_rowHeight;
            }
            return;
        }

        ensureRowMetrics_();
        if (rows <= 0 || m_rowOffsets.size() < (rows + 1) || offsetY >= m_contentHeight) {
            return;
        }

        // Find first row whose bottom is below the scroll offset.
        int lo = 0;
        int hi = rows;
        while (lo < hi) {
            const int mid = lo + ((hi - lo) / 2);
            const int bottom = m_rowOffsets[mid + 1];
            if (bottom <= offsetY) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        int firstRow = lo;
        if (firstRow < 0) {
            firstRow = 0;
        }
        if (firstRow >= rows) {
            firstRow = rows - 1;
        }

        int y = viewport.y - (offsetY - m_rowOffsets[firstRow]);
        for (int r = firstRow; r < rows; ++r) {
            if (y > viewport.y + viewport.height) {
                break;
            }

            int rowH = m_rowOffsets[r + 1] - m_rowOffsets[r];
            if (rowH <= 0) {
                rowH = m_rowHeight;
            }

            const SwModelIndex idx = m_model->index(r, 0);
            SwItemFlags flags = m_model->flags(idx);
            const bool enabled = flags.testFlag(SwItemFlag::ItemIsEnabled);
            const bool selected = m_selectionModel && m_selectionModel->isSelected(idx);
            const bool hovered = (r == m_hoverRow);

            SwStyleOptionViewItem opt;
            opt.rect = SwRect{viewport.x, y, viewport.width, rowH};
            opt.font = font;
            opt.enabled = enabled;
            opt.selected = selected;
            opt.hovered = hovered;
            opt.hasFocus = getFocus();
            opt.alternate = m_alternating && ((r % 2) == 1);

            m_delegate->paint(painter, opt, idx);
            y += rowH;
        }
    }

    void clearIndexWidgets() {
        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            if (m_indexWidgets[i].widget) {
                delete m_indexWidgets[i].widget;
                m_indexWidgets[i].widget = nullptr;
            }
        }
        m_indexWidgets.clear();
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

        const int offsetY = m_vBar ? m_vBar->value() : 0;
        const int rows = rowCount();
        const bool iconMode = (m_viewMode == ViewMode::IconMode);
        const bool useVariableHeights = !m_uniformRowHeights && !iconMode;
        if (useVariableHeights) {
            ensureRowMetrics_();
        }

        for (size_t i = 0; i < m_indexWidgets.size(); ++i) {
            SwWidget* w = m_indexWidgets[i].widget;
            const SwModelIndex idx = m_indexWidgets[i].index;
            if (!w) {
                continue;
            }
            if (!idx.isValid() || idx.model() != m_model || idx.column() != 0) {
                w->hide();
                continue;
            }

            const int row = idx.row();
            SwRect cell{viewport.x, viewport.y, viewport.width, m_rowHeight};
            if (iconMode) {
                cell = iconCellRect_(viewport, row, offsetY);
            } else {
                int cellY = viewport.y + row * m_rowHeight - offsetY;
                int cellH = m_rowHeight;

                if (useVariableHeights && row >= 0 && row < rows && m_rowOffsets.size() >= (rows + 1)) {
                    cellY = viewport.y + m_rowOffsets[row] - offsetY;
                    cellH = m_rowOffsets[row + 1] - m_rowOffsets[row];
                    if (cellH <= 0) {
                        cellH = m_rowHeight;
                    }
                }

                cell = SwRect{viewport.x, cellY, viewport.width, cellH};
            }
            if (!rectContains(viewport, cell)) {
                w->hide();
                continue;
            }

            bool fillCell = false;
            if (w->propertyExist("FillCell")) {
                try {
                    fillCell = w->property("FillCell").get<bool>();
                } catch (...) {
                    fillCell = false;
                }
            }

            if (fillCell) {
                w->move(cell.x - bounds.x, cell.y - bounds.y);
                w->resize(cell.width, cell.height);
                w->show();
                continue;
            }

            const int padX = 8;
            const int padY = 4;
            const int maxW = cell.width - padX * 2;
            const int maxH = cell.height - padY * 2;

            SwSize hint = w->sizeHint();
            int ww = hint.width > 0 ? hint.width : w->size().width;
            int hh = hint.height > 0 ? hint.height : w->size().height;

            ww = clampInt(ww, 0, maxW);
            hh = clampInt(hh, 0, maxH);

            w->move(cell.x + padX - bounds.x, cell.y + (cell.height - hh) / 2 - bounds.y);
            w->resize(ww, hh);
            w->show();
        }
    }

    SwAbstractItemModel* m_model{nullptr};
    SwItemSelectionModel* m_selectionModel{nullptr};
    SwStyledItemDelegate* m_delegate{nullptr};

    SwScrollBar* m_vBar{nullptr};
    SwScrollBarPolicy m_vPolicy{SwScrollBarPolicy::ScrollBarAsNeeded};

    SwVector<IndexWidgetEntry> m_indexWidgets;

    ViewMode m_viewMode{ViewMode::ListMode};
    SwSize m_gridSize{72, 72};
    int m_spacing{6};

    int m_viewportPadding{6};
    int m_rowHeight{34};
    int m_scrollBarThickness{14};
    bool m_uniformRowHeights{true};

    mutable bool m_rowMetricsValid{false};
    mutable SwVector<int> m_rowOffsets; // prefix sums: size = rows + 1
    mutable int m_contentHeight{0};

    bool m_alternating{false};
    int m_hoverRow{-1};

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
    int m_dropRow{-1};

    int iconCellW_() const { return clampInt(m_gridSize.width, 8, 512); }
    int iconCellH_() const { return clampInt(m_gridSize.height, 8, 512); }

    int iconStepX_() const { return iconCellW_() + m_spacing; }
    int iconStepY_() const { return iconCellH_() + m_spacing; }

    int iconColumns_(const SwRect& viewport) const {
        const int w = viewport.width;
        if (w <= 0) {
            return 1;
        }
        const int stepX = iconStepX_();
        if (stepX <= 0) {
            return 1;
        }
        // "+spacing" mirrors the usual spacing behaviour (no trailing spacing needed).
        const int cols = (w + m_spacing) / stepX;
        return clampInt(cols, 1, 1024);
    }

    int iconContentHeightForWidth_(int viewportW) const {
        const int count = rowCount();
        if (count <= 0) {
            return 0;
        }

        const int w = std::max(0, viewportW);
        const int cellH = iconCellH_();
        const int stepY = iconStepY_();
        if (cellH <= 0 || stepY <= 0) {
            return 0;
        }

        const int cols = clampInt((w + m_spacing) / iconStepX_(), 1, 1024);
        const int gridRows = (count + cols - 1) / cols;
        if (gridRows <= 0) {
            return 0;
        }

        const int totalH = gridRows * cellH + (gridRows - 1) * m_spacing;
        return clampInt(totalH, 0, 1 << 30);
    }

    SwRect iconCellRect_(const SwRect& viewport, int row, int offsetY) const {
        const int cols = iconColumns_(viewport);
        if (cols <= 0) {
            return SwRect{viewport.x, viewport.y, 0, 0};
        }
        const int gridRow = row / cols;
        const int col = row % cols;

        const int stepX = iconStepX_();
        const int stepY = iconStepY_();
        const int x = viewport.x + col * stepX;
        const int y = viewport.y + gridRow * stepY - offsetY;
        return SwRect{x, y, iconCellW_(), iconCellH_()};
    }

    void invalidateRowMetrics_() {
        m_rowMetricsValid = false;
    }

    void ensureRowMetrics_() const {
        if (m_viewMode == ViewMode::IconMode) {
            // Icon mode uses uniform grid metrics.
            m_rowOffsets.clear();
            m_contentHeight = 0;
            m_rowMetricsValid = true;
            return;
        }
        if (m_uniformRowHeights || m_rowMetricsValid) {
            return;
        }
        if (!m_model || !m_delegate) {
            m_rowOffsets.clear();
            m_contentHeight = 0;
            m_rowMetricsValid = true;
            return;
        }

        const int rows = rowCount();
        m_rowOffsets.clear();
        if (rows <= 0) {
            m_contentHeight = 0;
            m_rowMetricsValid = true;
            return;
        }

        // Use the current viewport width as a hint for word-wrapping delegates.
        const SwRect bounds = rect();
        SwRect viewport = viewportRect(bounds);
        if (viewport.width < 0) viewport.width = 0;
        if (viewport.height < 0) viewport.height = 0;

        SwStyleOptionViewItem opt;
        opt.font = getFont();
        opt.enabled = true;
        opt.selected = false;
        opt.hovered = false;
        opt.hasFocus = getFocus();
        opt.alternate = false;
        opt.rect = SwRect{0, 0, viewport.width, 0};

        m_rowOffsets.push_back(0);
        int y = 0;
        for (int r = 0; r < rows; ++r) {
            const SwModelIndex idx = m_model->index(r, 0);
            SwSize hint = m_delegate->sizeHint(opt, idx);
            int h = hint.height;
            if (h <= 0) {
                h = m_rowHeight;
            }
            h = clampInt(h, 18, 10000);
            y += h;
            m_rowOffsets.push_back(y);
        }

        m_contentHeight = y;
        m_rowMetricsValid = true;
    }
};

