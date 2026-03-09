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
 * @file src/core/gui/SwStandardItemModel.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwStandardItemModel in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the standard item model interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwStandardItem and SwStandardItemModel.
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
 * SwStandardItemModel - standard item model.
 *
 * Minimal implementation to back SwTreeView / SwTableView.
 **************************************************************************************************/

#include "SwAbstractItemModel.h"
#include "graphics/SwImage.h"

#include "SwList.h"
#include "SwString.h"

class SwStandardItemModel;

class SwStandardItem {
public:
    /**
     * @brief Constructs a `SwStandardItem` instance.
     * @param text Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwStandardItem(const SwString& text = SwString())
        : m_text(text) {}

    /**
     * @brief Destroys the `SwStandardItem` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwStandardItem() { clearChildren(); }

    /**
     * @brief Constructs a `SwStandardItem` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwStandardItem(const SwStandardItem&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwStandardItem& operator=(const SwStandardItem&) = delete;

    /**
     * @brief Sets the text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setText(const SwString& text) { m_text = text; }
    /**
     * @brief Returns the current text.
     * @return The current text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString text() const { return m_text; }

    /**
     * @brief Sets the tool Tip.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setToolTip(const SwString& text) { m_toolTip = text; }
    /**
     * @brief Returns the current tool Tip.
     * @return The current tool Tip.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toolTip() const { return m_toolTip; }

    /**
     * @brief Sets the editable.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEditable(bool on) { m_editable = on; }
    /**
     * @brief Returns whether the object reports editable.
     * @return `true` when the object reports editable; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEditable() const { return m_editable; }

    /**
     * @brief Sets the icon.
     * @param icon Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIcon(const SwImage& icon) { m_icon = icon; }
    /**
     * @brief Returns the current icon.
     * @return The current icon.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwImage icon() const { return m_icon; }

    /**
     * @brief Returns the current parent.
     * @return The current parent.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStandardItem* parent() const { return m_parent; }

    /**
     * @brief Returns the current row.
     * @return The current row.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int row() const { return m_rowInParent; }
    /**
     * @brief Returns the current column.
     * @return The current column.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int column() const { return m_columnInParent; }

    /**
     * @brief Performs the `rowCount` operation.
     * @return The requested row Count.
     */
    int rowCount() const { return static_cast<int>(m_children.size()); }

    /**
     * @brief Returns the current column Count.
     * @return The current column Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int columnCount() const {
        int maxCols = 0;
        for (const SwList<SwStandardItem*>& row : m_children) {
            const int cols = static_cast<int>(row.size());
            if (cols > maxCols) {
                maxCols = cols;
            }
        }
        return maxCols;
    }

    /**
     * @brief Performs the `child` operation.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @return The requested child.
     */
    SwStandardItem* child(int row, int column = 0) const {
        if (row < 0 || row >= static_cast<int>(m_children.size())) {
            return nullptr;
        }
        const SwList<SwStandardItem*>& cols = m_children[static_cast<size_t>(row)];
        if (column < 0 || column >= static_cast<int>(cols.size())) {
            return nullptr;
        }
        return cols[static_cast<size_t>(column)];
    }

    /**
     * @brief Performs the `appendRow` operation.
     * @param item Item affected by the operation.
     */
    void appendRow(SwStandardItem* item) {
        SwList<SwStandardItem*> row;
        row.append(item);
        appendRow(row);
    }

    /**
     * @brief Performs the `appendRow` operation.
     * @param items Collection of items affected by the operation.
     */
    void appendRow(const SwList<SwStandardItem*>& items) {
        SwList<SwStandardItem*> row = items;
        const int rowIndex = static_cast<int>(m_children.size());
        for (size_t c = 0; c < row.size(); ++c) {
            if (!row[c]) {
                row[c] = new SwStandardItem();
            }
            row[c]->attachToParent(this, rowIndex, static_cast<int>(c));
        }
        m_children.append(row);
    }

    /**
     * @brief Sets the child.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param item Item affected by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setChild(int row, int column, SwStandardItem* item) {
        if (row < 0 || column < 0) {
            return;
        }
        while (row >= static_cast<int>(m_children.size())) {
            m_children.append(SwList<SwStandardItem*>());
        }
        SwList<SwStandardItem*>& cols = m_children[static_cast<size_t>(row)];
        while (column >= static_cast<int>(cols.size())) {
            cols.append(static_cast<SwStandardItem*>(nullptr));
        }
        if (!item) {
            item = new SwStandardItem();
        }
        if (cols[static_cast<size_t>(column)] && cols[static_cast<size_t>(column)] != item) {
            delete cols[static_cast<size_t>(column)];
        }
        cols[static_cast<size_t>(column)] = item;
        item->attachToParent(this, row, column);
    }

private:
    friend class SwStandardItemModel;

    void attachToParent(SwStandardItem* parent, int row, int column) {
        m_parent = parent;
        m_rowInParent = row;
        m_columnInParent = column;
    }

    void clearChildren() {
        for (SwList<SwStandardItem*>& row : m_children) {
            for (size_t c = 0; c < row.size(); ++c) {
                delete row[c];
                row[c] = nullptr;
            }
        }
        m_children.clear();
    }

    SwString m_text;
    SwString m_toolTip;
    SwImage  m_icon;
    bool m_editable{false};
    SwStandardItem* m_parent{nullptr};
    int m_rowInParent{-1};
    int m_columnInParent{-1};
    SwList<SwList<SwStandardItem*>> m_children;
};

class SwStandardItemModel : public SwAbstractItemModel {
    SW_OBJECT(SwStandardItemModel, SwAbstractItemModel)

public:
    /**
     * @brief Constructs a `SwStandardItemModel` instance.
     * @param rows Value passed to the method.
     * @param columns Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwStandardItemModel(int rows = 0,
                                 int columns = 1,
                                 SwObject* parent = nullptr)
        : SwAbstractItemModel(parent)
        , m_root(new SwStandardItem())
        , m_columnCount(columns > 0 ? columns : 1) {
        if (rows > 0) {
            for (int r = 0; r < rows; ++r) {
                SwList<SwStandardItem*> row;
                for (int c = 0; c < m_columnCount; ++c) {
                    row.append(new SwStandardItem());
                }
                m_root->appendRow(row);
            }
        }
    }

    /**
     * @brief Destroys the `SwStandardItemModel` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwStandardItemModel() override {
        delete m_root;
        m_root = nullptr;
    }

    /**
     * @brief Returns the current invisible Root Item.
     * @return The current invisible Root Item.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStandardItem* invisibleRootItem() const { return m_root; }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        delete m_root;
        m_root = new SwStandardItem();
        modelReset();
    }

    /**
     * @brief Sets the column Count.
     * @param columns Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColumnCount(int columns) {
        m_columnCount = (columns > 0) ? columns : 1;
        modelReset();
    }

    /**
     * @brief Performs the `columnCount` operation.
     * @param parent Optional parent object that owns this instance.
     * @return The requested column Count.
     */
    int columnCount(const SwModelIndex& parent = SwModelIndex()) const override {
        SW_UNUSED(parent)
        return m_columnCount;
    }

    /**
     * @brief Performs the `rowCount` operation.
     * @param parent Optional parent object that owns this instance.
     * @return The requested row Count.
     */
    int rowCount(const SwModelIndex& parent = SwModelIndex()) const override {
        SwStandardItem* parentItem = itemFromIndex(parent);
        if (!parentItem) {
            return 0;
        }
        return parentItem->rowCount();
    }

    /**
     * @brief Performs the `index` operation.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The requested index.
     */
    SwModelIndex index(int row,
                       int column,
                       const SwModelIndex& parent = SwModelIndex()) const override {
        if (row < 0 || column < 0 || column >= m_columnCount) {
            return SwModelIndex();
        }
        SwStandardItem* parentItem = itemFromIndex(parent);
        if (!parentItem) {
            return SwModelIndex();
        }
        SwStandardItem* child = parentItem->child(row, column);
        if (!child) {
            return SwModelIndex();
        }
        return createIndex(row, column, child);
    }

    /**
     * @brief Performs the `parent` operation.
     * @param child Value passed to the method.
     * @return The requested parent.
     */
    SwModelIndex parent(const SwModelIndex& child) const override {
        if (!child.isValid()) {
            return SwModelIndex();
        }
        auto* item = static_cast<SwStandardItem*>(child.internalPointer());
        if (!item) {
            return SwModelIndex();
        }
        SwStandardItem* parentItem = item->parent();
        if (!parentItem || parentItem == m_root) {
            return SwModelIndex();
        }
        return createIndex(parentItem->row(), 0, parentItem);
    }

    /**
     * @brief Performs the `data` operation.
     * @param index Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested data.
     */
    SwAny data(const SwModelIndex& index, SwItemDataRole role = SwItemDataRole::DisplayRole) const override {
        if (!index.isValid()) {
            return SwAny();
        }
        auto* item = static_cast<SwStandardItem*>(index.internalPointer());
        if (!item) {
            return SwAny();
        }
        if (role == SwItemDataRole::ToolTipRole) {
            return SwAny(item->toolTip());
        }
        if (role == SwItemDataRole::DecorationRole) {
            return SwAny::from<SwImage>(item->icon());
        }
        if (role == SwItemDataRole::DisplayRole || role == SwItemDataRole::EditRole) {
            return SwAny(item->text());
        }
        return SwAny();
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
        if (!index.isValid()) {
            return false;
        }
        if (role != SwItemDataRole::EditRole && role != SwItemDataRole::DisplayRole && role != SwItemDataRole::ToolTipRole) {
            return false;
        }
        auto* item = static_cast<SwStandardItem*>(index.internalPointer());
        if (!item) {
            return false;
        }
        if (role == SwItemDataRole::ToolTipRole) {
            SwAny converted = value.canConvert<SwString>() ? value.convert<SwString>() : SwAny();
            if (!converted.canConvert<SwString>()) {
                return false;
            }
            SwString text;
            try {
                text = converted.get<SwString>();
            } catch (...) {
                return false;
            }
            item->setToolTip(text);
            dataChanged(index, index);
            return true;
        }
        if (!item->isEditable() && role == SwItemDataRole::EditRole) {
            return false;
        }
        SwString text;
        SwAny converted = value.canConvert<SwString>() ? value.convert<SwString>() : SwAny();
        if (!converted.canConvert<SwString>()) {
            return false;
        }
        try {
            text = converted.get<SwString>();
        } catch (...) {
            return false;
        }
        item->setText(text);
        dataChanged(index, index);
        return true;
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
        if (role != SwItemDataRole::DisplayRole) {
            return SwAny();
        }
        if (orientation == SwOrientation::Horizontal) {
            if (section < 0 || section >= static_cast<int>(m_horizontalHeaders.size())) {
                return SwAny();
            }
            return SwAny(m_horizontalHeaders[static_cast<size_t>(section)]);
        }
        return SwAny();
    }

    /**
     * @brief Performs the `sort` operation.
     * @param column Value passed to the method.
     * @param order Value passed to the method.
     */
    void sort(int column, SwSortOrder order = SwSortOrder::AscendingOrder) override {
        if (!m_root) {
            return;
        }
        if (column < 0 || column >= m_columnCount) {
            return;
        }
        sortChildrenRecursive(m_root, column, order);
        modelReset();
    }

    /**
     * @brief Sets the horizontal Header Labels.
     * @param labels Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHorizontalHeaderLabels(const SwList<SwString>& labels) {
        m_horizontalHeaders = labels;
        if (static_cast<int>(m_horizontalHeaders.size()) > m_columnCount) {
            m_columnCount = static_cast<int>(m_horizontalHeaders.size());
        }
        modelReset();
    }

    /**
     * @brief Performs the `item` operation.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @return The requested item.
     */
    SwStandardItem* item(int row, int column = 0) const {
        if (row < 0 || column < 0) {
            return nullptr;
        }
        return m_root ? m_root->child(row, column) : nullptr;
    }

    /**
     * @brief Sets the item.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param item Item affected by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setItem(int row, int column, SwStandardItem* item) {
        if (!m_root) {
            return;
        }
        if (column >= m_columnCount) {
            setColumnCount(column + 1);
        }
        m_root->setChild(row, column, item);
        modelReset();
    }

    /**
     * @brief Performs the `appendRow` operation.
     * @param item Item affected by the operation.
     */
    void appendRow(SwStandardItem* item) {
        if (!m_root) {
            return;
        }
        if (m_columnCount < 1) {
            m_columnCount = 1;
        }
        SwList<SwStandardItem*> row;
        row.append(item);
        for (int c = 1; c < m_columnCount; ++c) {
            row.append(new SwStandardItem());
        }
        m_root->appendRow(row);
        modelReset();
    }

    /**
     * @brief Performs the `appendRow` operation.
     * @param items Collection of items affected by the operation.
     */
    void appendRow(const SwList<SwStandardItem*>& items) {
        if (!m_root) {
            return;
        }
        SwList<SwStandardItem*> row = items;
        if (static_cast<int>(row.size()) < m_columnCount) {
            for (int c = static_cast<int>(row.size()); c < m_columnCount; ++c) {
                row.append(new SwStandardItem());
            }
        } else if (static_cast<int>(row.size()) > m_columnCount) {
            m_columnCount = static_cast<int>(row.size());
        }
        m_root->appendRow(row);
        modelReset();
    }

    /**
     * @brief Performs the `flags` operation.
     * @param index Value passed to the method.
     * @return The requested flags.
     */
    SwItemFlags flags(const SwModelIndex& index) const override {
        SwItemFlags base = SwAbstractItemModel::flags(index);
        if (!index.isValid()) {
            return base;
        }
        auto* item = static_cast<SwStandardItem*>(index.internalPointer());
        if (item && item->isEditable()) {
            base.setFlag(SwItemFlag::ItemIsEditable, true);
        }
        return base;
    }

private:
    void sortChildrenRecursive(SwStandardItem* parent, int column, SwSortOrder order) {
        if (!parent) {
            return;
        }

        SwList<SwList<SwStandardItem*>>& rows = parent->m_children;
        if (rows.size() > 1) {
            auto keyForRow = [column](const SwList<SwStandardItem*>& row) -> SwString {
                if (column < 0) {
                    return SwString();
                }
                const size_t idx = static_cast<size_t>(column);
                if (idx >= row.size()) {
                    return SwString();
                }
                SwStandardItem* item = row[idx];
                return item ? item->text() : SwString();
            };

            std::stable_sort(rows.begin(), rows.end(), [order, &keyForRow](const SwList<SwStandardItem*>& a, const SwList<SwStandardItem*>& b) {
                const SwString ka = keyForRow(a);
                const SwString kb = keyForRow(b);
                const int cmp = ka.compare(kb, Sw::CaseInsensitive);
                if (cmp == 0) {
                    return false;
                }
                return (order == SwSortOrder::AscendingOrder) ? (cmp < 0) : (cmp > 0);
            });

            for (size_t r = 0; r < rows.size(); ++r) {
                SwList<SwStandardItem*>& row = rows[r];
                for (size_t c = 0; c < row.size(); ++c) {
                    if (row[c]) {
                        row[c]->attachToParent(parent, static_cast<int>(r), static_cast<int>(c));
                    }
                }
            }
        }

        for (SwList<SwStandardItem*>& row : rows) {
            for (SwStandardItem* item : row) {
                if (item && item->rowCount() > 0) {
                    sortChildrenRecursive(item, column, order);
                }
            }
        }
    }

    SwStandardItem* itemFromIndex(const SwModelIndex& index) const {
        if (!index.isValid()) {
            return m_root;
        }
        if (index.model() != this) {
            return nullptr;
        }
        return static_cast<SwStandardItem*>(index.internalPointer());
    }

    SwStandardItem* m_root{nullptr};
    int m_columnCount{1};
    SwList<SwString> m_horizontalHeaders;
};
