#pragma once

#include "Export.hpp"

#include "core/types/SwString.h"

#include <memory>

namespace SwizioNodes {

/**
 * `id` represents an internal unique data type for the given port.
 * `name` is a normal text description.
 */
struct SWIZIO_NODES_PUBLIC NodeDataType
{
    SwString id;
    SwString name;
};

/**
 * Class represents data transferred between nodes.
 * @param type is used for comparing the types
 * The actual data is stored in subtypes.
 */
class SWIZIO_NODES_PUBLIC NodeData
{
public:
    virtual ~NodeData() = default;

    virtual bool sameType(NodeData const& nodeData) const
    {
        return (this->type().id == nodeData.type().id);
    }

    /// Type for inner use.
    virtual NodeDataType type() const = 0;
};

} // namespace SwizioNodes

