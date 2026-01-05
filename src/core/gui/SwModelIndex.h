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
 * SwModelIndex - Qt-like model index (≈ QModelIndex).
 *
 * Stores row/column + an internal pointer owned by the model.
 **************************************************************************************************/

class SwAbstractItemModel;

class SwModelIndex {
public:
    SwModelIndex() = default;

    bool isValid() const { return m_model != nullptr && m_row >= 0 && m_column >= 0; }

    int row() const { return m_row; }
    int column() const { return m_column; }

    void* internalPointer() const { return m_internalPointer; }
    const SwAbstractItemModel* model() const { return m_model; }

    bool operator==(const SwModelIndex& other) const {
        return m_row == other.m_row &&
               m_column == other.m_column &&
               m_internalPointer == other.m_internalPointer &&
               m_model == other.m_model;
    }

    bool operator!=(const SwModelIndex& other) const { return !(*this == other); }

private:
    SwModelIndex(int row, int column, void* internalPointer, const SwAbstractItemModel* model)
        : m_row(row)
        , m_column(column)
        , m_internalPointer(internalPointer)
        , m_model(model) {}

    int m_row{-1};
    int m_column{-1};
    void* m_internalPointer{nullptr};
    const SwAbstractItemModel* m_model{nullptr};

    friend class SwAbstractItemModel;
};

