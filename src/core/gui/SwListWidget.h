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
 * SwListWidget - Qt-like convenience widget (≈ QListWidget).
 *
 * Wraps a SwStandardItemModel + SwListView.
 **************************************************************************************************/

#include "SwListView.h"
#include "SwStandardItemModel.h"

class SwListWidget : public SwListView {
    SW_OBJECT(SwListWidget, SwListView)

public:
    explicit SwListWidget(SwWidget* parent = nullptr)
        : SwListView(parent) {
        m_model = new SwStandardItemModel(0, 1, this);
        SwListView::setModel(m_model);
    }

    SwStandardItemModel* model() const { return m_model; }

    int count() const { return m_model ? m_model->rowCount() : 0; }

    void clear() {
        if (m_model) {
            m_model->clear();
        }
    }

    void addItem(const SwString& text) {
        if (!m_model) {
            return;
        }
        m_model->appendRow(new SwStandardItem(text));
    }

    SwStandardItem* item(int row) const { return m_model ? m_model->item(row, 0) : nullptr; }

private:
    SwStandardItemModel* m_model{nullptr};
};

