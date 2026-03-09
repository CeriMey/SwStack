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
 * @file src/core/gui/SwSortFilterProxyModel.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwSortFilterProxyModel in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the sort filter proxy model interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwSortFilterProxyModel.
 *
 * Model-oriented declarations here define the data contract consumed by views, delegates, or
 * algorithms, with an emphasis on stable roles, ownership, and update flow rather than on
 * presentation details.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwSortFilterProxyModel - sort/filter proxy model.
 *
 * Scope (v1):
 * - Wraps a source SwAbstractItemModel.
 * - Provides row filtering (fixed string or regex) and per-parent sorting.
 * - Emits modelReset on invalidation (simple + robust for the current MVC stack).
 **************************************************************************************************/

#include "SwAbstractItemModel.h"

#include "core/types/SwList.h"
#include "core/types/SwRegularExpression.h"
#include "core/types/SwString.h"

#include <algorithm>
#include <unordered_map>

class SwSortFilterProxyModel : public SwAbstractItemModel {
    SW_OBJECT(SwSortFilterProxyModel, SwAbstractItemModel)

public:
    /**
     * @brief Constructs a `SwSortFilterProxyModel` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwSortFilterProxyModel(SwObject* parent = nullptr)
        : SwAbstractItemModel(parent) {}

    /**
     * @brief Sets the source Model.
     * @param source Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSourceModel(SwAbstractItemModel* source) {
        if (m_sourceModel == source) {
            return;
        }
        if (m_sourceModel) {
            SwObject::disconnect(m_sourceModel, this);
        }
        m_sourceModel = source;
        invalidateInternal_(false);
        if (m_sourceModel) {
            SwObject::connect(m_sourceModel, &SwAbstractItemModel::modelReset, this, [this]() { invalidate(); });
            SwObject::connect(m_sourceModel, &SwAbstractItemModel::dataChanged, this, [this](const SwModelIndex&, const SwModelIndex&) { invalidate(); });
        }
        invalidate();
    }

    /**
     * @brief Returns the current source Model.
     * @return The current source Model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwAbstractItemModel* sourceModel() const { return m_sourceModel; }

    /**
     * @brief Sets the filter Fixed String.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilterFixedString(const SwString& text) {
        if (!m_useRegex && m_filterFixedString == text) {
            return;
        }
        m_useRegex = false;
        m_filterFixedString = text;
        invalidate();
    }

    /**
     * @brief Returns the current filter Fixed String.
     * @return The current filter Fixed String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString filterFixedString() const { return m_filterFixedString; }

    /**
     * @brief Sets the filter Regular Expression.
     * @param re Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilterRegularExpression(const SwRegularExpression& re) {
        if (m_useRegex && m_filterRegularExpression == re) {
            return;
        }
        m_useRegex = true;
        m_filterRegularExpression = re;
        invalidate();
    }

    /**
     * @brief Returns the current filter Regular Expression.
     * @return The current filter Regular Expression.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRegularExpression filterRegularExpression() const { return m_filterRegularExpression; }

    /**
     * @brief Sets the filter Case Sensitivity.
     * @param cs Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilterCaseSensitivity(Sw::CaseSensitivity cs) {
        if (m_filterCaseSensitivity == cs) {
            return;
        }
        m_filterCaseSensitivity = cs;
        invalidate();
    }

    /**
     * @brief Returns the current filter Case Sensitivity.
     * @return The current filter Case Sensitivity.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Sw::CaseSensitivity filterCaseSensitivity() const { return m_filterCaseSensitivity; }

    /**
     * @brief Sets the filter Key Column.
     * @param column Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilterKeyColumn(int column) {
        if (m_filterKeyColumn == column) {
            return;
        }
        m_filterKeyColumn = column;
        invalidate();
    }

    /**
     * @brief Returns the current filter Key Column.
     * @return The current filter Key Column.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int filterKeyColumn() const { return m_filterKeyColumn; }

    /**
     * @brief Sets the filter Role.
     * @param role Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilterRole(SwItemDataRole role) {
        if (m_filterRole == role) {
            return;
        }
        m_filterRole = role;
        invalidate();
    }

    /**
     * @brief Returns the current filter Role.
     * @return The current filter Role.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwItemDataRole filterRole() const { return m_filterRole; }

    /**
     * @brief Sets the sort Role.
     * @param role Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSortRole(SwItemDataRole role) {
        if (m_sortRole == role) {
            return;
        }
        m_sortRole = role;
        invalidate();
    }

    /**
     * @brief Returns the current sort Role.
     * @return The current sort Role.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwItemDataRole sortRole() const { return m_sortRole; }

    /**
     * @brief Sets the sort Case Sensitivity.
     * @param cs Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSortCaseSensitivity(Sw::CaseSensitivity cs) {
        if (m_sortCaseSensitivity == cs) {
            return;
        }
        m_sortCaseSensitivity = cs;
        invalidate();
    }

    /**
     * @brief Returns the current sort Case Sensitivity.
     * @return The current sort Case Sensitivity.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Sw::CaseSensitivity sortCaseSensitivity() const { return m_sortCaseSensitivity; }

    /**
     * @brief Performs the `invalidate` operation.
     */
    void invalidate() {
        invalidateInternal_(true);
        modelReset();
    }

    /**
     * @brief Performs the `mapToSource` operation.
     * @param proxyIndex Value passed to the method.
     * @return The requested map To Source.
     */
    SwModelIndex mapToSource(const SwModelIndex& proxyIndex) const {
        if (!m_sourceModel || !proxyIndex.isValid() || proxyIndex.model() != this) {
            return SwModelIndex();
        }
        ensureCaches_();
        auto it = m_sourceIndexByPtr.find(proxyIndex.internalPointer());
        if (it == m_sourceIndexByPtr.end()) {
            return SwModelIndex();
        }
        return it->second;
    }

    /**
     * @brief Performs the `mapFromSource` operation.
     * @param sourceIndex Value passed to the method.
     * @return The requested map From Source.
     */
    SwModelIndex mapFromSource(const SwModelIndex& sourceIndex) const {
        if (!m_sourceModel || !sourceIndex.isValid() || sourceIndex.model() != m_sourceModel) {
            return SwModelIndex();
        }
        ensureCaches_();
        return mapFromSourceInternal_(sourceIndex);
    }

    // SwAbstractItemModel overrides
    /**
     * @brief Performs the `index` operation.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The requested index.
     */
    SwModelIndex index(int row, int column, const SwModelIndex& parent = SwModelIndex()) const override {
        if (!m_sourceModel) {
            return SwModelIndex();
        }
        if (row < 0 || column < 0) {
            return SwModelIndex();
        }

        ensureCaches_();
        const SwModelIndex sourceParent = mapToSource(parent);
        Node& node = nodeForSourceParent_(sourceParent);
        buildNode_(node);

        if (row >= static_cast<int>(node.sourceRows.size())) {
            return SwModelIndex();
        }

        const int sourceRow = node.sourceRows[static_cast<size_t>(row)];
        const SwModelIndex sourceIndex = m_sourceModel->index(sourceRow, column, sourceParent);
        if (!sourceIndex.isValid()) {
            return SwModelIndex();
        }
        return createIndex(row, column, sourceIndex.internalPointer());
    }

    /**
     * @brief Performs the `parent` operation.
     * @param child Value passed to the method.
     * @return The requested parent.
     */
    SwModelIndex parent(const SwModelIndex& child) const override {
        if (!m_sourceModel || !child.isValid() || child.model() != this) {
            return SwModelIndex();
        }
        ensureCaches_();
        const SwModelIndex sourceChild = mapToSource(child);
        if (!sourceChild.isValid()) {
            return SwModelIndex();
        }
        const SwModelIndex sourceParent = m_sourceModel->parent(sourceChild);
        if (!sourceParent.isValid()) {
            return SwModelIndex();
        }
        return mapFromSourceInternal_(sourceParent);
    }

    /**
     * @brief Performs the `rowCount` operation.
     * @param parent Optional parent object that owns this instance.
     * @return The requested row Count.
     */
    int rowCount(const SwModelIndex& parent = SwModelIndex()) const override {
        if (!m_sourceModel) {
            return 0;
        }
        ensureCaches_();
        const SwModelIndex sourceParent = mapToSource(parent);
        Node& node = nodeForSourceParent_(sourceParent);
        buildNode_(node);
        return static_cast<int>(node.sourceRows.size());
    }

    /**
     * @brief Performs the `columnCount` operation.
     * @param parent Optional parent object that owns this instance.
     * @return The requested column Count.
     */
    int columnCount(const SwModelIndex& parent = SwModelIndex()) const override {
        if (!m_sourceModel) {
            return 0;
        }
        const SwModelIndex sourceParent = mapToSource(parent);
        return std::max(0, m_sourceModel->columnCount(sourceParent));
    }

    /**
     * @brief Performs the `data` operation.
     * @param index Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested data.
     */
    SwAny data(const SwModelIndex& index, SwItemDataRole role = SwItemDataRole::DisplayRole) const override {
        if (!m_sourceModel) {
            return SwAny();
        }
        const SwModelIndex src = mapToSource(index);
        if (!src.isValid()) {
            return SwAny();
        }
        return m_sourceModel->data(src, role);
    }

    /**
     * @brief Sets the data.
     * @param index Value passed to the method.
     * @param value Value passed to the method.
     * @param role Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setData(const SwModelIndex& index, const SwAny& value, SwItemDataRole role = SwItemDataRole::EditRole) override {
        if (!m_sourceModel) {
            return false;
        }
        const SwModelIndex src = mapToSource(index);
        if (!src.isValid()) {
            return false;
        }
        const bool ok = m_sourceModel->setData(src, value, role);
        if (ok) {
            invalidate();
        }
        return ok;
    }

    /**
     * @brief Performs the `headerData` operation.
     * @param section Value passed to the method.
     * @param orientation Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested header Data.
     */
    SwAny headerData(int section,
                     SwOrientation orientation,
                     SwItemDataRole role = SwItemDataRole::DisplayRole) const override {
        if (!m_sourceModel) {
            return SwAny();
        }
        return m_sourceModel->headerData(section, orientation, role);
    }

    /**
     * @brief Performs the `flags` operation.
     * @param index Value passed to the method.
     * @return The requested flags.
     */
    SwItemFlags flags(const SwModelIndex& index) const override {
        if (!m_sourceModel) {
            return SwItemFlags();
        }
        const SwModelIndex src = mapToSource(index);
        return src.isValid() ? m_sourceModel->flags(src) : SwItemFlags();
    }

    /**
     * @brief Performs the `sort` operation.
     * @param column Value passed to the method.
     * @param order Value passed to the method.
     */
    void sort(int column, SwSortOrder order = SwSortOrder::AscendingOrder) override {
        m_sortColumn = column;
        m_sortOrder = order;
        invalidate();
    }

private:
    struct Node {
        void* parentKey{nullptr};
        SwModelIndex sourceParent;
        SwList<int> sourceRows;
        bool built{false};
    };

    void invalidateInternal_(bool clearNodeCache) {
        m_cacheValid = false;
        m_sourceIndexByPtr.clear();
        if (clearNodeCache) {
            m_nodes.clear();
        } else {
            for (Node& n : m_nodes) {
                n.sourceRows.clear();
                n.built = false;
            }
        }
    }

    void ensureCaches_() const {
        if (m_cacheValid) {
            return;
        }
        m_sourceIndexByPtr.clear();
        m_nodes.clear();
        if (m_sourceModel) {
            buildSourceIndexMapRecursive_(SwModelIndex());
        }
        m_cacheValid = true;
    }

    void buildSourceIndexMapRecursive_(const SwModelIndex& parent) const {
        if (!m_sourceModel) {
            return;
        }

        const int rows = std::max(0, m_sourceModel->rowCount(parent));
        const int cols = std::max(0, m_sourceModel->columnCount(parent));
        if (rows <= 0 || cols <= 0) {
            return;
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const SwModelIndex idx = m_sourceModel->index(r, c, parent);
                if (idx.isValid() && idx.internalPointer()) {
                    m_sourceIndexByPtr[idx.internalPointer()] = idx;
                }
            }
        }

        // Recurse using column 0 as the parent index for child rows.
        for (int r = 0; r < rows; ++r) {
            const SwModelIndex childParent = m_sourceModel->index(r, 0, parent);
            if (!childParent.isValid()) {
                continue;
            }
            if (m_sourceModel->rowCount(childParent) <= 0) {
                continue;
            }
            buildSourceIndexMapRecursive_(childParent);
        }
    }

    Node& nodeForSourceParent_(const SwModelIndex& sourceParent) const {
        const void* key = sourceParent.isValid() ? sourceParent.internalPointer() : nullptr;
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            if (m_nodes[i].parentKey == key) {
                return m_nodes[i];
            }
        }
        Node n;
        n.parentKey = const_cast<void*>(key);
        n.sourceParent = sourceParent;
        m_nodes.append(n);
        return m_nodes.lastRef();
    }

    bool filterAcceptsRow_(int sourceRow, const SwModelIndex& sourceParent) const {
        if (!m_sourceModel) {
            return true;
        }

        if (!m_useRegex) {
            if (m_filterFixedString.isEmpty()) {
                return true;
            }
        } else {
            if (!m_filterRegularExpression.isValid() || m_filterRegularExpression.pattern().isEmpty()) {
                return true;
            }
        }

        const int cols = std::max(0, m_sourceModel->columnCount(sourceParent));
        if (cols <= 0) {
            return true;
        }

        const int firstCol = (m_filterKeyColumn >= 0) ? m_filterKeyColumn : 0;
        const int lastCol = (m_filterKeyColumn >= 0) ? m_filterKeyColumn : (cols - 1);
        if (firstCol < 0 || lastCol < 0 || firstCol >= cols) {
            return true;
        }

        for (int c = firstCol; c <= lastCol && c < cols; ++c) {
            const SwModelIndex idx = m_sourceModel->index(sourceRow, c, sourceParent);
            if (!idx.isValid()) {
                continue;
            }
            const SwString text = m_sourceModel->data(idx, m_filterRole).toString();
            if (!m_useRegex) {
                if (text.contains(m_filterFixedString, m_filterCaseSensitivity)) {
                    return true;
                }
            } else {
                if (m_filterRegularExpression.match(text).hasMatch()) {
                    return true;
                }
            }
        }
        return false;
    }

    SwString sortKeyForRow_(int sourceRow, const SwModelIndex& sourceParent) const {
        if (!m_sourceModel || m_sortColumn < 0) {
            return SwString();
        }
        const SwModelIndex idx = m_sourceModel->index(sourceRow, m_sortColumn, sourceParent);
        return idx.isValid() ? m_sourceModel->data(idx, m_sortRole).toString() : SwString();
    }

    void buildNode_(Node& node) const {
        if (node.built || !m_sourceModel) {
            return;
        }
        node.sourceRows.clear();

        const int rows = std::max(0, m_sourceModel->rowCount(node.sourceParent));
        for (int r = 0; r < rows; ++r) {
            if (filterAcceptsRow_(r, node.sourceParent)) {
                node.sourceRows.append(r);
            }
        }

        if (m_sortColumn >= 0 && node.sourceRows.size() > 1) {
            const int col = m_sortColumn;
            const SwSortOrder order = m_sortOrder;
            const Sw::CaseSensitivity cs = m_sortCaseSensitivity;
            std::stable_sort(node.sourceRows.begin(),
                             node.sourceRows.end(),
                             [this, col, order, cs, &node](int a, int b) {
                                 SW_UNUSED(col)
                                 const SwString ka = sortKeyForRow_(a, node.sourceParent);
                                 const SwString kb = sortKeyForRow_(b, node.sourceParent);
                                 const int cmp = ka.compare(kb, cs);
                                 if (cmp == 0) {
                                     return false;
                                 }
                                 return (order == SwSortOrder::AscendingOrder) ? (cmp < 0) : (cmp > 0);
                             });
        }

        node.built = true;
    }

    SwModelIndex mapFromSourceInternal_(const SwModelIndex& sourceIndex) const {
        if (!m_sourceModel || !sourceIndex.isValid()) {
            return SwModelIndex();
        }

        const SwModelIndex sourceParent = m_sourceModel->parent(sourceIndex);
        if (sourceParent.isValid()) {
            // Ensure parent is reachable in the proxy (filtered in).
            const SwModelIndex proxyParent = mapFromSourceInternal_(sourceParent);
            if (!proxyParent.isValid()) {
                return SwModelIndex();
            }
        }

        Node& node = nodeForSourceParent_(sourceParent);
        buildNode_(node);

        const int sourceRow = sourceIndex.row();
        int proxyRow = -1;
        for (size_t i = 0; i < node.sourceRows.size(); ++i) {
            if (node.sourceRows[i] == sourceRow) {
                proxyRow = static_cast<int>(i);
                break;
            }
        }
        if (proxyRow < 0) {
            return SwModelIndex();
        }

        return createIndex(proxyRow, sourceIndex.column(), sourceIndex.internalPointer());
    }

    SwAbstractItemModel* m_sourceModel{nullptr};

    bool m_useRegex{false};
    SwString m_filterFixedString;
    SwRegularExpression m_filterRegularExpression;
    Sw::CaseSensitivity m_filterCaseSensitivity{Sw::CaseInsensitive};
    int m_filterKeyColumn{-1};
    SwItemDataRole m_filterRole{SwItemDataRole::DisplayRole};

    int m_sortColumn{-1};
    SwSortOrder m_sortOrder{SwSortOrder::AscendingOrder};
    SwItemDataRole m_sortRole{SwItemDataRole::DisplayRole};
    Sw::CaseSensitivity m_sortCaseSensitivity{Sw::CaseInsensitive};

    mutable bool m_cacheValid{false};
    mutable std::unordered_map<void*, SwModelIndex> m_sourceIndexByPtr;
    mutable SwList<Node> m_nodes;
};
