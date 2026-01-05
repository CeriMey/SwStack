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
 * SwStandardItemModel - Qt-like standard item model (≈ QStandardItemModel).
 *
 * Minimal implementation to back SwTreeView / SwTableView.
 **************************************************************************************************/

#include "SwAbstractItemModel.h"

#include "SwList.h"
#include "SwString.h"

class SwStandardItemModel;

class SwStandardItem {
public:
    explicit SwStandardItem(const SwString& text = SwString())
        : m_text(text) {}

    ~SwStandardItem() { clearChildren(); }

    SwStandardItem(const SwStandardItem&) = delete;
    SwStandardItem& operator=(const SwStandardItem&) = delete;

    void setText(const SwString& text) { m_text = text; }
    SwString text() const { return m_text; }

    void setToolTip(const SwString& text) { m_toolTip = text; }
    SwString toolTip() const { return m_toolTip; }

    void setEditable(bool on) { m_editable = on; }
    bool isEditable() const { return m_editable; }

    SwStandardItem* parent() const { return m_parent; }

    int row() const { return m_rowInParent; }
    int column() const { return m_columnInParent; }

    int rowCount() const { return static_cast<int>(m_children.size()); }

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

    void appendRow(SwStandardItem* item) {
        SwList<SwStandardItem*> row;
        row.append(item);
        appendRow(row);
    }

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
    bool m_editable{false};
    SwStandardItem* m_parent{nullptr};
    int m_rowInParent{-1};
    int m_columnInParent{-1};
    SwList<SwList<SwStandardItem*>> m_children;
};

class SwStandardItemModel : public SwAbstractItemModel {
    SW_OBJECT(SwStandardItemModel, SwAbstractItemModel)

public:
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

    ~SwStandardItemModel() override {
        delete m_root;
        m_root = nullptr;
    }

    SwStandardItem* invisibleRootItem() const { return m_root; }

    void clear() {
        delete m_root;
        m_root = new SwStandardItem();
        modelReset();
    }

    void setColumnCount(int columns) {
        m_columnCount = (columns > 0) ? columns : 1;
        modelReset();
    }

    int columnCount(const SwModelIndex& parent = SwModelIndex()) const override {
        SW_UNUSED(parent)
        return m_columnCount;
    }

    int rowCount(const SwModelIndex& parent = SwModelIndex()) const override {
        SwStandardItem* parentItem = itemFromIndex(parent);
        if (!parentItem) {
            return 0;
        }
        return parentItem->rowCount();
    }

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
        if (role == SwItemDataRole::DisplayRole || role == SwItemDataRole::EditRole) {
            return SwAny(item->text());
        }
        return SwAny();
    }

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

    void setHorizontalHeaderLabels(const SwList<SwString>& labels) {
        m_horizontalHeaders = labels;
        if (static_cast<int>(m_horizontalHeaders.size()) > m_columnCount) {
            m_columnCount = static_cast<int>(m_horizontalHeaders.size());
        }
        modelReset();
    }

    SwStandardItem* item(int row, int column = 0) const {
        if (row < 0 || column < 0) {
            return nullptr;
        }
        return m_root ? m_root->child(row, column) : nullptr;
    }

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
