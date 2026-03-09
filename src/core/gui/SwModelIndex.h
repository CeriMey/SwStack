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
 * @file src/core/gui/SwModelIndex.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwModelIndex in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the model index interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwModelIndex.
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
 * SwModelIndex - model index.
 *
 * Stores row/column + an internal pointer owned by the model.
 **************************************************************************************************/

class SwAbstractItemModel;

class SwModelIndex {
public:
    /**
     * @brief Constructs a `SwModelIndex` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwModelIndex() = default;

    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const { return m_model != nullptr && m_row >= 0 && m_column >= 0; }

    /**
     * @brief Returns the current row.
     * @return The current row.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int row() const { return m_row; }
    /**
     * @brief Returns the current column.
     * @return The current column.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int column() const { return m_column; }

    /**
     * @brief Returns the current internal Pointer.
     * @return The current internal Pointer.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void* internalPointer() const { return m_internalPointer; }
    /**
     * @brief Returns the current model.
     * @return The current model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwAbstractItemModel* model() const { return m_model; }

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwModelIndex& other) const {
        return m_row == other.m_row &&
               m_column == other.m_column &&
               m_internalPointer == other.m_internalPointer &&
               m_model == other.m_model;
    }

    /**
     * @brief Performs the `operator!=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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
