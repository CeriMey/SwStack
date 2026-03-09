#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/NodeData.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by NodeData in the CoreSw node-editor layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the node data interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


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
    /**
     * @brief Destroys the `NodeData` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~NodeData() = default;

    /**
     * @brief Performs the `sameType` operation.
     * @param nodeData Value passed to the method.
     * @return The requested same Type.
     */
    virtual bool sameType(NodeData const& nodeData) const
    {
        return (this->type().id == nodeData.type().id);
    }

    /// Type for inner use.
    virtual NodeDataType type() const = 0;
};

} // namespace SwizioNodes
