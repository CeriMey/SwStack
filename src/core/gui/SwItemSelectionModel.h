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
 * @file src/core/gui/SwItemSelectionModel.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwItemSelectionModel in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the item selection model interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwItemSelectionModel.
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
 * SwItemSelectionModel - minimal selection model.
 *
 * For now this is single-selection + current index.
 **************************************************************************************************/

#include "SwObject.h"

#include "SwModelIndex.h"
#include "SwList.h"

class SwAbstractItemModel;

class SwItemSelectionModel : public SwObject {
    SW_OBJECT(SwItemSelectionModel, SwObject)

public:
    /**
     * @brief Constructs a `SwItemSelectionModel` instance.
     * @param model Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param model Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwItemSelectionModel(SwAbstractItemModel* model = nullptr, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_model(model) {}

    /**
     * @brief Returns the current model.
     * @return The current model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwAbstractItemModel* model() const { return m_model; }

    /**
     * @brief Sets the model.
     * @param model Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModel(SwAbstractItemModel* model) {
        if (m_model == model) {
            return;
        }
        m_model = model;
        clear();
        modelChanged();
    }

    /**
     * @brief Returns the current current Index.
     * @return The current current Index.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwModelIndex currentIndex() const { return m_currentIndex; }

    /**
     * @brief Returns the current anchor Index.
     * @return The current anchor Index.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwModelIndex anchorIndex() const { return m_anchorIndex; }

    /**
     * @brief Returns the current selected Indexes.
     * @return The current selected Indexes.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<SwModelIndex> selectedIndexes() const { return m_selected; }

    /**
     * @brief Returns whether the object reports selection.
     * @return `true` when the object reports selection; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool hasSelection() const { return !m_selected.isEmpty(); }

    /**
     * @brief Returns whether the object reports selected.
     * @param index Value passed to the method.
     * @return `true` when the object reports selected; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isSelected(const SwModelIndex& index) const {
        if (!index.isValid()) {
            return false;
        }
        for (size_t i = 0; i < m_selected.size(); ++i) {
            if (m_selected[i] == index) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Sets the anchor Index.
     * @param index Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAnchorIndex(const SwModelIndex& index) { m_anchorIndex = index; }

    // Backward compatible default: clear + select current.
    /**
     * @brief Sets the current Index.
     * @param index Value passed to the method.
     * @param clearAndSelect Value passed to the method.
     * @param updateAnchor Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCurrentIndex(const SwModelIndex& index, bool clearAndSelect = true, bool updateAnchor = true) {
        if (m_currentIndex == index && (!clearAndSelect || (m_selected.size() == 1 && isSelected(index)))) {
            return;
        }

        const SwModelIndex previous = m_currentIndex;
        m_currentIndex = index;
        currentChanged(m_currentIndex, previous);

        bool selectionTouched = false;
        if (clearAndSelect) {
            if (!m_selected.isEmpty()) {
                m_selected.clear();
                selectionTouched = true;
            }
            if (index.isValid()) {
                m_selected.append(index);
                selectionTouched = true;
            }
        }
        if (updateAnchor) {
            m_anchorIndex = index;
        }
        if (selectionTouched) {
            selectionChanged();
        }
    }

    /**
     * @brief Clears the current object state.
     */
    void clearSelection() {
        if (m_selected.isEmpty()) {
            return;
        }
        m_selected.clear();
        selectionChanged();
    }

    /**
     * @brief Performs the `select` operation.
     * @param index Value passed to the method.
     * @param on Value passed to the method.
     */
    void select(const SwModelIndex& index, bool on = true) {
        if (!index.isValid()) {
            return;
        }

        int existing = -1;
        for (size_t i = 0; i < m_selected.size(); ++i) {
            if (m_selected[i] == index) {
                existing = static_cast<int>(i);
                break;
            }
        }

        if (on) {
            if (existing >= 0) {
                return;
            }
            m_selected.append(index);
            selectionChanged();
            return;
        }

        if (existing < 0) {
            return;
        }
        m_selected.removeAt(static_cast<size_t>(existing));
        selectionChanged();
    }

    /**
     * @brief Performs the `toggle` operation.
     * @param index Value passed to the method.
     */
    void toggle(const SwModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        select(index, !isSelected(index));
    }

    /**
     * @brief Sets the selected Indexes.
     * @param indexes Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSelectedIndexes(const SwList<SwModelIndex>& indexes) {
        m_selected = indexes;
        selectionChanged();
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        const bool hadCurrent = m_currentIndex.isValid();
        const bool hadSelection = !m_selected.isEmpty();

        const SwModelIndex previous = m_currentIndex;
        m_currentIndex = SwModelIndex();
        m_anchorIndex = SwModelIndex();
        m_selected.clear();

        if (hadCurrent) {
            currentChanged(m_currentIndex, previous);
        }
        if (hadSelection) {
            selectionChanged();
        }
    }

    DECLARE_SIGNAL_VOID(modelChanged);
    DECLARE_SIGNAL(currentChanged, const SwModelIndex&, const SwModelIndex&);
    DECLARE_SIGNAL_VOID(selectionChanged);

private:
    SwAbstractItemModel* m_model{nullptr};
    SwModelIndex m_currentIndex;
    SwModelIndex m_anchorIndex;
    SwList<SwModelIndex> m_selected;
};
