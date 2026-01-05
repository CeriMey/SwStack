#pragma once

#include "Export.hpp"

#include "SwizioNodes/internal/BasicGraphicsScene.hpp"
#include "SwizioNodes/internal/DataFlowGraphModel.hpp"

#include "core/types/SwAny.h"

namespace SwizioNodes {

/**
 * Sw port of QtNodes::DataFlowGraphicsScene.
 */
class SWIZIO_NODES_PUBLIC DataFlowGraphicsScene : public BasicGraphicsScene
{
    SW_OBJECT(DataFlowGraphicsScene, BasicGraphicsScene)

public:
    explicit DataFlowGraphicsScene(DataFlowGraphModel& graphModel, SwObject* parent = nullptr)
        : BasicGraphicsScene(graphModel, parent) {}

    ~DataFlowGraphicsScene() override = default;

    DataFlowGraphModel& graphModel() { return static_cast<DataFlowGraphModel&>(BasicGraphicsScene::graphModel()); }
    DataFlowGraphModel const& graphModel() const {
        return static_cast<DataFlowGraphModel const&>(BasicGraphicsScene::graphModel());
    }

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
