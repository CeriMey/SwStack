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
 * SwAbstractItemModel - Qt-like item model base (≈ QAbstractItemModel).
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
    explicit SwAbstractItemModel(SwObject* parent = nullptr)
        : SwObject(parent) {}

    virtual ~SwAbstractItemModel() = default;

    virtual SwModelIndex index(int row,
                               int column,
                               const SwModelIndex& parent = SwModelIndex()) const = 0;
    virtual SwModelIndex parent(const SwModelIndex& child) const = 0;

    virtual int rowCount(const SwModelIndex& parent = SwModelIndex()) const = 0;
    virtual int columnCount(const SwModelIndex& parent = SwModelIndex()) const = 0;

    virtual SwAny data(const SwModelIndex& index,
                       SwItemDataRole role = SwItemDataRole::DisplayRole) const = 0;

    virtual bool setData(const SwModelIndex& index,
                         const SwAny& value,
                         SwItemDataRole role = SwItemDataRole::EditRole) {
        SW_UNUSED(index)
        SW_UNUSED(value)
        SW_UNUSED(role)
        return false;
    }

    virtual SwAny headerData(int section,
                             SwOrientation orientation,
                             SwItemDataRole role = SwItemDataRole::DisplayRole) const {
        SW_UNUSED(section)
        SW_UNUSED(orientation)
        SW_UNUSED(role)
        return SwAny();
    }

    virtual void sort(int column, SwSortOrder order = SwSortOrder::AscendingOrder) {
        SW_UNUSED(column)
        SW_UNUSED(order)
    }

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
    SwModelIndex createIndex(int row, int column, void* internalPointer) const {
        return SwModelIndex(row, column, internalPointer, this);
    }
};
