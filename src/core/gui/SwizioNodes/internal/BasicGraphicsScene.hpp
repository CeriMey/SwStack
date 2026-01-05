#pragma once

#include "Export.hpp"

#include "SwizioNodes/internal/AbstractGraphModel.hpp"
#include "SwizioNodes/internal/ConnectionIdHash.hpp"
#include "SwizioNodes/internal/Definitions.hpp"

#include "graphics/SwGraphicsScene.h"

#include <unordered_map>

namespace SwizioNodes {

class NodeGraphicsObject;
class ConnectionGraphicsObject;

/**
 * SwGraphicsScene counterpart of QtNodes::BasicGraphicsScene.
 */
class SWIZIO_NODES_PUBLIC BasicGraphicsScene : public SwGraphicsScene
{
    SW_OBJECT(BasicGraphicsScene, SwGraphicsScene)

public:
    explicit BasicGraphicsScene(AbstractGraphModel& graphModel, SwObject* parent = nullptr)
        : SwGraphicsScene(parent), m_graphModel(graphModel) {}

    ~BasicGraphicsScene() override = default;

public:
    AbstractGraphModel const& graphModel() const { return m_graphModel; }
    AbstractGraphModel& graphModel() { return m_graphModel; }

    NodeGraphicsObject* nodeGraphicsObject(NodeId nodeId) {
        auto it = m_nodes.find(nodeId);
        return (it != m_nodes.end()) ? it->second : nullptr;
    }

    ConnectionGraphicsObject* connectionGraphicsObject(ConnectionId connectionId) {
        auto it = m_connections.find(connectionId);
        return (it != m_connections.end()) ? it->second : nullptr;
    }

    void registerNode(NodeGraphicsObject* node) {
        if (!node) {
            return;
        }
        m_nodes[node->nodeId()] = node;
    }

    void unregisterNode(NodeId nodeId) {
        auto it = m_nodes.find(nodeId);
        if (it != m_nodes.end()) {
            m_nodes.erase(it);
        }
    }

    void registerConnection(ConnectionGraphicsObject* conn) {
        if (!conn) {
            return;
        }
        m_connections[conn->connectionId()] = conn;
    }

    void unregisterConnection(ConnectionId connectionId) {
        auto it = m_connections.find(connectionId);
        if (it != m_connections.end()) {
            m_connections.erase(it);
        }
    }

    void clearRegistry() {
        m_nodes.clear();
        m_connections.clear();
    }

private:
    AbstractGraphModel& m_graphModel;
    std::unordered_map<NodeId, NodeGraphicsObject*> m_nodes;
    std::unordered_map<ConnectionId, ConnectionGraphicsObject*, ConnectionIdHash> m_connections;
};

} // namespace SwizioNodes
