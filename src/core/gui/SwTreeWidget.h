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
#include "SwTreeView.h"

#include <algorithm>

class SwTreeWidget : public SwTreeView {
    SW_OBJECT(SwTreeWidget, SwTreeView)

public:
    explicit SwTreeWidget(int columns = 1, SwWidget* parent = nullptr)
        : SwTreeView(parent) {
        m_model = new SwStandardItemModel(0, std::max(1, columns), this);
        SwTreeView::setModel(m_model);
    }

    SwStandardItemModel* model() const { return m_model; }

    SwStandardItem* invisibleRootItem() const { return m_model ? m_model->invisibleRootItem() : nullptr; }

    void setColumnCount(int columns) {
        if (!m_model) {
            return;
        }
        m_model->setColumnCount(std::max(1, columns));
    }

    void setHeaderLabels(const SwList<SwString>& labels) {
        if (!m_model) {
            return;
        }
        m_model->setHorizontalHeaderLabels(labels);
    }

    void clear() {
        if (m_model) {
            m_model->clear();
        }
    }

private:
    SwStandardItemModel* m_model{nullptr};
};
