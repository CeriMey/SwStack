#pragma once

/**
 * @file src/core/gui/SwTreeWidget.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwTreeWidget in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the tree widget interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwTreeWidget.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
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

#include "SwStandardItemModel.h"
#include "SwTreeView.h"

#include <algorithm>

class SwTreeWidget : public SwTreeView {
    SW_OBJECT(SwTreeWidget, SwTreeView)

public:
    /**
     * @brief Constructs a `SwTreeWidget` instance.
     * @param columns Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwTreeWidget(int columns = 1, SwWidget* parent = nullptr)
        : SwTreeView(parent) {
        m_model = new SwStandardItemModel(0, std::max(1, columns), this);
        SwTreeView::setModel(m_model);
        SwObject::connect(this, &SwTreeView::clicked, this, [this](const SwModelIndex& idx) {
            onClicked_(idx);
        });
        if (selectionModel()) {
            SwObject::connect(selectionModel(), &SwItemSelectionModel::currentChanged, this,
                              [this](const SwModelIndex& cur, const SwModelIndex& prev) {
                                  onCurrentChanged_(cur, prev);
                              });
        }
    }

    /**
     * @brief Returns the current model.
     * @return The current model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStandardItemModel* model() const { return m_model; }

    /**
     * @brief Performs the `invisibleRootItem` operation.
     * @return The requested invisible Root Item.
     */
    SwStandardItem* invisibleRootItem() const { return m_model ? m_model->invisibleRootItem() : nullptr; }

    /**
     * @brief Sets the column Count.
     * @param columns Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColumnCount(int columns) {
        if (!m_model) {
            return;
        }
        m_model->setColumnCount(std::max(1, columns));
    }

    /**
     * @brief Sets the header Labels.
     * @param labels Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHeaderLabels(const SwList<SwString>& labels) {
        if (!m_model) {
            return;
        }
        m_model->setHorizontalHeaderLabels(labels);
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        if (m_model) {
            m_model->clear();
        }
    }

    // Convenience helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    /**
     * @brief Adds the specified top Level Item.
     * @param item Item affected by the operation.
     */
    void addTopLevelItem(SwStandardItem* item) {
        if (!m_model || !item) return;
        m_model->appendRow(item);  // goes through model so modelReset() is emitted
    }

    /**
     * @brief Performs the `topLevelItem` operation.
     * @param index Value passed to the method.
     * @return The requested top Level Item.
     */
    SwStandardItem* topLevelItem(int index) const {
        if (!m_model) return nullptr;
        return m_model->invisibleRootItem()->child(index, 0);
    }

    /**
     * @brief Returns the current top Level Item Count.
     * @return The current top Level Item Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int topLevelItemCount() const {
        if (!m_model) return 0;
        return m_model->invisibleRootItem()->rowCount();
    }

    /**
     * @brief Returns the current current Item.
     * @return The current current Item.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStandardItem* currentItem() const {
        if (!m_model || !selectionModel()) return nullptr;
        const SwModelIndex idx = selectionModel()->currentIndex();
        if (!idx.isValid()) return nullptr;
        return static_cast<SwStandardItem*>(idx.internalPointer());
    }

    /**
     * @brief Sets the root Is Decorated.
     * @param show Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRootIsDecorated(bool show) {
        SwTreeView::setRootIsDecorated(show);
    }

    using SwTreeView::collapse;
    using SwTreeView::expand;

    // Expand/collapse a specific item
    /**
     * @brief Performs the `expand` operation.
     * @param item Item affected by the operation.
     */
    void expand(SwStandardItem* item) {
        if (!item || !m_model) return;
        const SwModelIndex idx = indexForItem(item);
        if (idx.isValid()) SwTreeView::expand(idx);
    }

    /**
     * @brief Performs the `collapse` operation.
     * @param item Item affected by the operation.
     */
    void collapse(SwStandardItem* item) {
        if (!item || !m_model) return;
        const SwModelIndex idx = indexForItem(item);
        if (idx.isValid()) SwTreeView::collapse(idx);
    }

    // Find a top-level or nested item by path stored in toolTip
    /**
     * @brief Performs the `findItemByPath` operation.
     * @param path Path used by the operation.
     * @return The requested find Item By Path.
     */
    SwStandardItem* findItemByPath(const SwString& path) const {
        if (!m_model) return nullptr;
        return findItemByPathRecursive(m_model->invisibleRootItem(), path);
    }

    // Select the item matching the given path (stored as toolTip)
    /**
     * @brief Sets the active Path.
     * @param path Path used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setActivePath(const SwString& path) {
        if (!m_model || !selectionModel()) return;
        SwStandardItem* found = findItemByPath(path);
        if (!found) {
            selectionModel()->clearSelection();
            return;
        }
        const SwModelIndex idx = indexForItem(found);
        if (idx.isValid()) {
            selectionModel()->setCurrentIndex(idx, true, false);
        }
    }

    // Signals â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    DECLARE_SIGNAL(itemClicked, SwStandardItem*);
    DECLARE_SIGNAL(currentItemChanged, SwStandardItem*, SwStandardItem*);

private:
    // Find the model index for a SwStandardItem
    SwModelIndex indexForItem(SwStandardItem* item) const {
        if (!item || !m_model) return SwModelIndex();
        SwStandardItem* parent = item->parent();
        if (!parent) parent = m_model->invisibleRootItem();
        const int r = item->row();
        const int c = item->column();
        if (r < 0 || c < 0) return SwModelIndex();
        const SwModelIndex parentIdx = (parent == m_model->invisibleRootItem())
            ? SwModelIndex()
            : indexForItem(parent);
        return m_model->index(r, c, parentIdx);
    }

    SwStandardItem* findItemByPathRecursive(SwStandardItem* parent, const SwString& path) const {
        if (!parent) return nullptr;
        for (int r = 0; r < parent->rowCount(); ++r) {
            SwStandardItem* child = parent->child(r, 0);
            if (!child) continue;
            if (child->toolTip() == path) return child;
            SwStandardItem* found = findItemByPathRecursive(child, path);
            if (found) return found;
        }
        return nullptr;
    }

    void onClicked_(const SwModelIndex& index) {
        if (!index.isValid()) return;
        auto* item = static_cast<SwStandardItem*>(index.internalPointer());
        emit itemClicked(item);
    }

    void onCurrentChanged_(const SwModelIndex& current, const SwModelIndex& previous) {
        auto* cur  = current.isValid()  ? static_cast<SwStandardItem*>(current.internalPointer())  : nullptr;
        auto* prev = previous.isValid() ? static_cast<SwStandardItem*>(previous.internalPointer()) : nullptr;
        emit currentItemChanged(cur, prev);
    }

    SwStandardItemModel* m_model{nullptr};
};
