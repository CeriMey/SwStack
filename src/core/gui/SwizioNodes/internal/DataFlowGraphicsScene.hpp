#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/DataFlowGraphicsScene.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by DataFlowGraphicsScene in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the data flow graphics scene interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
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

#include "SwizioNodes/internal/BasicGraphicsScene.hpp"
#include "SwizioNodes/internal/DataFlowGraphModel.hpp"

#include "core/types/SwAny.h"

namespace SwizioNodes {

/**
 * Sw port of the data-flow graphics scene.
 */
class SWIZIO_NODES_PUBLIC DataFlowGraphicsScene : public BasicGraphicsScene
{
    SW_OBJECT(DataFlowGraphicsScene, BasicGraphicsScene)

public:
    /**
     * @brief Constructs a `DataFlowGraphicsScene` instance.
     * @param graphModel Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit DataFlowGraphicsScene(DataFlowGraphModel& graphModel, SwObject* parent = nullptr)
        : BasicGraphicsScene(graphModel, parent) {}

    /**
     * @brief Destroys the `DataFlowGraphicsScene` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~DataFlowGraphicsScene() override = default;

    /**
     * @brief Performs the `graphModel` operation.
     * @return The requested graph Model.
     */
    DataFlowGraphModel& graphModel() { return static_cast<DataFlowGraphModel&>(BasicGraphicsScene::graphModel()); }
    /**
     * @brief Returns the current graph Model.
     * @return The current graph Model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    DataFlowGraphModel const& graphModel() const {
        return static_cast<DataFlowGraphModel const&>(BasicGraphicsScene::graphModel());
    }

    /**
     * @brief Creates the requested node.
     * @param type Value passed to the method.
     * @param pos Position used by the operation.
     * @return The resulting node.
     */
    NodeId createNode(const SwString& type, const SwPointF& pos) {
        NodeId id = graphModel().addNode(type);
        if (id == InvalidNodeId) {
            return id;
        }
        graphModel().setNodeData(id, NodeRole::Position, SwAny::from(pos));
        return id;
    }
};

} // namespace SwizioNodes
