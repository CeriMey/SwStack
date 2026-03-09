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
 * @file src/core/gui/SwListWidget.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwListWidget in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the list widget interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwListWidget.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwListWidget - convenience widget.
 *
 * Wraps a SwStandardItemModel + SwListView.
 **************************************************************************************************/

#include "SwListView.h"
#include "SwStandardItemModel.h"

class SwListWidget : public SwListView {
    SW_OBJECT(SwListWidget, SwListView)

public:
    /**
     * @brief Constructs a `SwListWidget` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwListWidget(SwWidget* parent = nullptr)
        : SwListView(parent) {
        m_model = new SwStandardItemModel(0, 1, this);
        SwListView::setModel(m_model);
    }

    /**
     * @brief Returns the current model.
     * @return The current model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStandardItemModel* model() const { return m_model; }

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const { return m_model ? m_model->rowCount() : 0; }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        if (m_model) {
            m_model->clear();
        }
    }

    /**
     * @brief Adds the specified item.
     * @param text Value passed to the method.
     */
    void addItem(const SwString& text) {
        if (!m_model) {
            return;
        }
        m_model->appendRow(new SwStandardItem(text));
    }

    /**
     * @brief Performs the `item` operation.
     * @return The requested item.
     */
    SwStandardItem* item(int row) const { return m_model ? m_model->item(row, 0) : nullptr; }

private:
    SwStandardItemModel* m_model{nullptr};
};
