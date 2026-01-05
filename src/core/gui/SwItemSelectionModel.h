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
 * SwItemSelectionModel - minimal Qt-like selection model (≈ QItemSelectionModel).
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
    explicit SwItemSelectionModel(SwAbstractItemModel* model = nullptr, SwObject* parent = nullptr)
        : SwObject(parent)
        , m_model(model) {}

    SwAbstractItemModel* model() const { return m_model; }

    void setModel(SwAbstractItemModel* model) {
        if (m_model == model) {
            return;
        }
        m_model = model;
        clear();
        modelChanged();
    }

    SwModelIndex currentIndex() const { return m_currentIndex; }

    SwModelIndex anchorIndex() const { return m_anchorIndex; }

    SwList<SwModelIndex> selectedIndexes() const { return m_selected; }

    bool hasSelection() const { return !m_selected.isEmpty(); }

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

    void setAnchorIndex(const SwModelIndex& index) { m_anchorIndex = index; }

    // Backward compatible default: clear + select current.
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

    void clearSelection() {
        if (m_selected.isEmpty()) {
            return;
        }
        m_selected.clear();
        selectionChanged();
    }

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

    void toggle(const SwModelIndex& index) {
        if (!index.isValid()) {
            return;
        }
        select(index, !isSelected(index));
    }

    void setSelectedIndexes(const SwList<SwModelIndex>& indexes) {
        m_selected = indexes;
        selectionChanged();
    }

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
