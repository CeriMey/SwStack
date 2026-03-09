#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/BasicGraphicsScene.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by BasicGraphicsScene in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the basic graphics scene interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
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

#include "SwizioNodes/internal/AbstractGraphModel.hpp"
#include "SwizioNodes/internal/ConnectionGraphicsObject.hpp"
#include "SwizioNodes/internal/ConnectionIdHash.hpp"
#include "SwizioNodes/internal/Definitions.hpp"
#include "SwizioNodes/internal/NodeGraphicsObject.hpp"

#include "graphics/SwGraphicsScene.h"

#include <unordered_map>

namespace SwizioNodes {

/**
 * SwGraphicsScene counterpart of the basic graphics scene.
 */
class SWIZIO_NODES_PUBLIC BasicGraphicsScene : public SwGraphicsScene
{
    SW_OBJECT(BasicGraphicsScene, SwGraphicsScene)

public:
    /**
     * @brief Constructs a `BasicGraphicsScene` instance.
     * @param graphModel Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param graphModel Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit BasicGraphicsScene(AbstractGraphModel& graphModel, SwObject* parent = nullptr)
        : SwGraphicsScene(parent), m_graphModel(graphModel) {}

    /**
     * @brief Destroys the `BasicGraphicsScene` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~BasicGraphicsScene() override = default;

public:
    /**
     * @brief Returns the current graph Model.
     * @return The current graph Model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    AbstractGraphModel const& graphModel() const { return m_graphModel; }
    /**
     * @brief Returns the current graph Model.
     * @return The current graph Model.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    AbstractGraphModel& graphModel() { return m_graphModel; }

    /**
     * @brief Performs the `nodeGraphicsObject` operation.
     * @param nodeId Value passed to the method.
     * @return The requested node Graphics Object.
     */
    NodeGraphicsObject* nodeGraphicsObject(NodeId nodeId) {
        auto it = m_nodes.find(nodeId);
        return (it != m_nodes.end()) ? it->second : nullptr;
    }

    /**
     * @brief Performs the `connectionGraphicsObject` operation.
     * @param connectionId Value passed to the method.
     * @return The requested connection Graphics Object.
     */
    ConnectionGraphicsObject* connectionGraphicsObject(ConnectionId connectionId) {
        auto it = m_connections.find(connectionId);
        return (it != m_connections.end()) ? it->second : nullptr;
    }

    /**
     * @brief Performs the `registerNode` operation.
     * @param node Value passed to the method.
     */
    void registerNode(NodeGraphicsObject* node) {
        if (!node) {
            return;
        }
        m_nodes[node->nodeId()] = node;
    }

    /**
     * @brief Performs the `unregisterNode` operation.
     * @param nodeId Value passed to the method.
     */
    void unregisterNode(NodeId nodeId) {
        auto it = m_nodes.find(nodeId);
        if (it != m_nodes.end()) {
            m_nodes.erase(it);
        }
    }

    /**
     * @brief Performs the `registerConnection` operation.
     * @param conn Value passed to the method.
     */
    void registerConnection(ConnectionGraphicsObject* conn) {
        if (!conn) {
            return;
        }
        m_connections[conn->connectionId()] = conn;
    }

    /**
     * @brief Performs the `unregisterConnection` operation.
     * @param connectionId Value passed to the method.
     */
    void unregisterConnection(ConnectionId connectionId) {
        auto it = m_connections.find(connectionId);
        if (it != m_connections.end()) {
            m_connections.erase(it);
        }
    }

    /**
     * @brief Clears the current object state.
     */
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
