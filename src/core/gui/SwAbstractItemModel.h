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
 * @file src/core/gui/SwAbstractItemModel.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwAbstractItemModel in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the abstract item model interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwOrientation, SwSortOrder, SwItemDataRole,
 * SwItemFlag, and SwAbstractItemModel.
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
 * SwAbstractItemModel - item model base.
 **************************************************************************************************/

#include "SwAny.h"
#include "SwObject.h"
#include "Sw.h"

#include "SwModelIndex.h"

enum class SwOrientation {
    Horizontal,
    Vertical
};

enum class SwSortOrder {
    AscendingOrder,
    DescendingOrder
};

enum class SwItemDataRole {
    DisplayRole = 0,
    DecorationRole = 1,
    EditRole = 2,
    ToolTipRole = 3
};

enum class SwItemFlag {
    NoItemFlags = 0x0,
    ItemIsSelectable = 0x1,
    ItemIsEditable = 0x2,
    ItemIsEnabled = 0x4
};

using SwItemFlags = SwFlagSet<SwItemFlag>;

class SwAbstractItemModel : public SwObject {
    SW_OBJECT(SwAbstractItemModel, SwObject)

public:
    /**
     * @brief Constructs a `SwAbstractItemModel` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwAbstractItemModel(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Destroys the `SwAbstractItemModel` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwAbstractItemModel() = default;

    /**
     * @brief Performs the `index` operation.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The requested index.
     */
    virtual SwModelIndex index(int row,
                               int column,
                               const SwModelIndex& parent = SwModelIndex()) const = 0;
    /**
     * @brief Performs the `parent` operation.
     * @param child Value passed to the method.
     * @return The requested parent.
     */
    virtual SwModelIndex parent(const SwModelIndex& child) const = 0;

    /**
     * @brief Performs the `rowCount` operation.
     * @param parent Optional parent object that owns this instance.
     * @return The requested row Count.
     */
    virtual int rowCount(const SwModelIndex& parent = SwModelIndex()) const = 0;
    /**
     * @brief Performs the `columnCount` operation.
     * @param parent Optional parent object that owns this instance.
     * @return The requested column Count.
     */
    virtual int columnCount(const SwModelIndex& parent = SwModelIndex()) const = 0;

    /**
     * @brief Performs the `data` operation.
     * @param index Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested data.
     */
    virtual SwAny data(const SwModelIndex& index,
                       SwItemDataRole role = SwItemDataRole::DisplayRole) const = 0;

    /**
     * @brief Sets the data.
     * @param index Value passed to the method.
     * @param value Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested data.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual bool setData(const SwModelIndex& index,
                         const SwAny& value,
                         SwItemDataRole role = SwItemDataRole::EditRole) {
        SW_UNUSED(index)
        SW_UNUSED(value)
        SW_UNUSED(role)
        return false;
    }

    /**
     * @brief Performs the `headerData` operation.
     * @param section Value passed to the method.
     * @param orientation Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested header Data.
     */
    virtual SwAny headerData(int section,
                             SwOrientation orientation,
                             SwItemDataRole role = SwItemDataRole::DisplayRole) const {
        SW_UNUSED(section)
        SW_UNUSED(orientation)
        SW_UNUSED(role)
        return SwAny();
    }

    /**
     * @brief Performs the `sort` operation.
     * @param column Value passed to the method.
     * @param order Value passed to the method.
     * @return The requested sort.
     */
    virtual void sort(int column, SwSortOrder order = SwSortOrder::AscendingOrder) {
        SW_UNUSED(column)
        SW_UNUSED(order)
    }

    /**
     * @brief Performs the `flags` operation.
     * @param index Value passed to the method.
     * @return The requested flags.
     */
    virtual SwItemFlags flags(const SwModelIndex& index) const {
        if (!index.isValid()) {
            return SwItemFlags();
        }
        SwItemFlags f;
        f.setFlag(SwItemFlag::ItemIsEnabled, true);
        f.setFlag(SwItemFlag::ItemIsSelectable, true);
        return f;
    }

    DECLARE_SIGNAL_VOID(modelReset);
    DECLARE_SIGNAL(dataChanged, const SwModelIndex&, const SwModelIndex&);

protected:
    /**
     * @brief Creates the requested index.
     * @param row Value passed to the method.
     * @param column Value passed to the method.
     * @param internalPointer Value passed to the method.
     * @return The resulting index.
     */
    SwModelIndex createIndex(int row, int column, void* internalPointer) const {
        return SwModelIndex(row, column, internalPointer, this);
    }
};
