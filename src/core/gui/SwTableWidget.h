#pragma once

/**
 * @file
 * @ingroup core_gui
 * @brief Declares `SwTableWidget`, a convenience table view backed by a standard model.
 *
 * The widget packages `SwTableView` together with an internal `SwStandardItemModel` so
 * callers can populate cells without manually wiring a model first. It is the higher-level
 * table API for editable grids, simple forms, and utility dialogs.
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
#include "SwTableView.h"

#include <algorithm>

class SwTableWidget : public SwTableView {
    SW_OBJECT(SwTableWidget, SwTableView)

public:
    /**
     * @brief Constructs a `SwTableWidget` instance.
     * @param rows Value passed to the method.
     * @param columns Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwTableWidget(int rows = 0, int columns = 0, SwWidget* parent = nullptr)
        : SwTableView(parent) {
        m_model = new SwStandardItemModel(rows, std::max(1, columns), this);
        SwTableView::setModel(m_model);
    }

    /**
     * @brief Returns the current model.
     * @return The current model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStandardItemModel* model() const { return m_model; }

    /**
     * @brief Performs the `rowCount` operation.
     * @return The requested row Count.
     */
    int rowCount() const { return m_model ? m_model->rowCount() : 0; }
    /**
     * @brief Performs the `columnCount` operation.
     * @return The requested column Count.
     */
    int columnCount() const { return m_model ? m_model->columnCount() : 0; }

    /**
     * @brief Sets the cell Widget.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param widget Widget associated with the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCellWidget(int row, int column, SwWidget* widget) {
        if (!m_model) {
            if (widget) {
                delete widget;
            }
            return;
        }
        SwModelIndex idx = m_model->index(row, column);
        SwTableView::setIndexWidget(idx, widget);
    }

    /**
     * @brief Sets the row Count.
     * @param rows Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRowCount(int rows) {
        if (!m_model) {
            return;
        }
        const int cols = std::max(1, m_model->columnCount());
        m_model->clear();
        m_model->setColumnCount(cols);
        for (int r = 0; r < rows; ++r) {
            SwList<SwStandardItem*> row;
            for (int c = 0; c < cols; ++c) {
                row.append(new SwStandardItem());
            }
            m_model->appendRow(row);
        }
    }

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
        columns = std::max(1, columns);
        const int rows = std::max(0, m_model->rowCount());
        m_model->clear();
        m_model->setColumnCount(columns);
        for (int r = 0; r < rows; ++r) {
            SwList<SwStandardItem*> row;
            for (int c = 0; c < columns; ++c) {
                row.append(new SwStandardItem());
            }
            m_model->appendRow(row);
        }
    }

    /**
     * @brief Sets the horizontal Header Labels.
     * @param labels Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHorizontalHeaderLabels(const SwList<SwString>& labels) {
        if (!m_model) {
            return;
        }
        m_model->setHorizontalHeaderLabels(labels);
    }

    /**
     * @brief Performs the `item` operation.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @return The requested item.
     */
    SwStandardItem* item(int row, int column) const { return m_model ? m_model->item(row, column) : nullptr; }
    /**
     * @brief Sets the item.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param item Item affected by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setItem(int row, int column, SwStandardItem* item) {
        if (!m_model) {
            return;
        }
        m_model->setItem(row, column, item);
    }

private:
    SwStandardItemModel* m_model{nullptr};
};
