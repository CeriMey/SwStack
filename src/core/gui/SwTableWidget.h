#pragma once
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
    explicit SwTableWidget(int rows = 0, int columns = 0, SwWidget* parent = nullptr)
        : SwTableView(parent) {
        m_model = new SwStandardItemModel(rows, std::max(1, columns), this);
        SwTableView::setModel(m_model);
    }

    SwStandardItemModel* model() const { return m_model; }

    int rowCount() const { return m_model ? m_model->rowCount() : 0; }
    int columnCount() const { return m_model ? m_model->columnCount() : 0; }

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

    void setHorizontalHeaderLabels(const SwList<SwString>& labels) {
        if (!m_model) {
            return;
        }
        m_model->setHorizontalHeaderLabels(labels);
    }

    SwStandardItem* item(int row, int column) const { return m_model ? m_model->item(row, column) : nullptr; }
    void setItem(int row, int column, SwStandardItem* item) {
        if (!m_model) {
            return;
        }
        m_model->setItem(row, column, item);
    }

private:
    SwStandardItemModel* m_model{nullptr};
};
