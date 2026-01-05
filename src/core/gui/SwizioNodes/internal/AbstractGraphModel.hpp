#pragma once

#include "Export.hpp"

#include "core/object/SwObject.h"
#include "core/types/SwAny.h"
#include "core/types/SwJsonObject.h"
#include "core/types/SwString.h"

#include "ConnectionIdHash.hpp"
#include "ConnectionIdUtils.hpp"
#include "Definitions.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SwizioNodes {

/**
 * The central class in the Model-View approach. It delivers all kinds
 * of information from the backing user data structures that represent
 * the graph. The class allows to modify the graph structure: create
 * and remove nodes and connections.
 *
 * We use two types of the unique ids for graph manipulations:
 *   - NodeId
 *   - ConnectionId
 */
class SWIZIO_NODES_PUBLIC AbstractGraphModel : public SwObject
{
    SW_OBJECT(AbstractGraphModel, SwObject)

public:
    explicit AbstractGraphModel(SwObject* parent = nullptr)
        : SwObject(parent) {}

    ~AbstractGraphModel() override = default;

public:
    /// Generates a new unique NodeId.
    virtual NodeId newNodeId() = 0;

    /// @brief Returns the full set of unique Node Ids.
    /**
     * Model creator is responsible for generating unique `unsigned int`
     * Ids for all the nodes in the graph. From an Id it should be
     * possible to trace back to the model's internal representation of
     * the node.
     */
    virtual std::unordered_set<NodeId> allNodeIds() const = 0;

    /**
     * A collection of all input and output connections for the given `nodeId`.
     */
    virtual std::unordered_set<ConnectionId> allConnectionIds(NodeId const nodeId) const = 0;

    /// @brief Returns all connected Node Ids for given port.
    /**
     * The returned set of nodes and port indices correspond to the type
     * opposite to the given `portType`.
     */
    virtual std::unordered_set<ConnectionId> connections(NodeId nodeId,
                                                         PortType portType,
                                                         PortIndex index) const
        = 0;

    /// Checks if two nodes with the given `connectionId` are connected.
    virtual bool connectionExists(ConnectionId const connectionId) const = 0;

    /// Creates a new node instance in the derived class.
    /**
     * The model is responsible for generating a unique `NodeId`.
     * @param[in] nodeType is free to be used and interpreted by the
     * model on its own, it helps to distinguish between possible node
     * types and create a correct instance inside.
     */
    virtual NodeId addNode(SwString const nodeType = SwString()) = 0;

    /// Model decides if a connection with a given connection Id possible.
    /**
     * The default implementation compares corresponding data types.
     *
     * It is possible to override the function and connect non-equal
     * data types.
     */
    virtual bool connectionPossible(ConnectionId const connectionId) const = 0;

    /// Defines if detaching the connection is possible.
    virtual bool detachPossible(ConnectionId const) const { return true; }

    /// Creates a new connection between two nodes.
    /**
     * Default implementation emits signal `connectionCreated(connectionId)`.
     *
     * In the derived classes user must emit the signal to notify the
     * scene about the changes.
     */
    virtual void addConnection(ConnectionId const connectionId) = 0;

    /**
     * @returns `true` if there is data in the model associated with the
     * given `nodeId`.
     */
    virtual bool nodeExists(NodeId const nodeId) const = 0;

    /// @brief Returns node-related data for requested NodeRole.
    /**
     * @returns Node Caption, Node Caption Visibility, Node Position etc.
     */
    virtual SwAny nodeData(NodeId nodeId, NodeRole role) const = 0;

    /**
     * A utility function that unwraps the `SwAny` value returned from the
     * standard `SwAny AbstractGraphModel::nodeData(NodeId, NodeRole)` function.
     */
    template<typename T>
    T nodeData(NodeId nodeId, NodeRole role) const
    {
        return nodeData(nodeId, role).get<T>();
    }

    virtual NodeFlags nodeFlags(NodeId nodeId) const
    {
        SW_UNUSED(nodeId);
        return NodeFlag::NoFlags;
    }

    /// @brief Sets node properties.
    /**
     * Sets: Node Caption, Node Caption Visibility,
     * Style, State, Node Position etc.
     * @see NodeRole.
     */
    virtual bool setNodeData(NodeId nodeId, NodeRole role, SwAny value) = 0;

    /// @brief Returns port-related data for requested PortRole.
    /**
     * @returns Port Data Type, Port Data, Connection Policy, Port Caption.
     */
    virtual SwAny portData(NodeId nodeId, PortType portType, PortIndex index, PortRole role) const = 0;

    /**
     * A utility function that unwraps the `SwAny` value returned from the
     * standard `SwAny AbstractGraphModel::portData(...)` function.
     */
    template<typename T>
    T portData(NodeId nodeId, PortType portType, PortIndex index, PortRole role) const
    {
        return portData(nodeId, portType, index, role).get<T>();
    }

    virtual bool setPortData(NodeId nodeId,
                             PortType portType,
                             PortIndex index,
                             SwAny const& value,
                             PortRole role = PortRole::Data)
        = 0;

    virtual bool deleteConnection(ConnectionId const connectionId) = 0;

    virtual bool deleteNode(NodeId const nodeId) = 0;

    /**
     * Reimplement the function if you want to store/restore the node's
     * inner state during undo/redo node deletion operations.
     */
    virtual SwJsonObject saveNode(NodeId const) const { return {}; }

    /**
     * Reimplement the function if you want to support:
     *   - graph save/restore operations,
     *   - undo/redo operations after deleting the node.
     */
    virtual void loadNode(SwJsonObject const&) {}

public:
    /**
     * Function clears connections attached to the ports that are scheduled to be
     * deleted. It must be called right before the model removes its old port data.
     */
    void portsAboutToBeDeleted(NodeId const nodeId,
                               PortType const portType,
                               PortIndex const first,
                               PortIndex const last);

    /// Signal emitted when model no longer has the old data associated with the given port indices.
    void portsDeleted();

    /**
     * Signal emitted when model is about to create new ports on the given node.
     * Function caches existing connections that are located after the `last` port.
     */
    void portsAboutToBeInserted(NodeId const nodeId,
                                PortType const portType,
                                PortIndex const first,
                                PortIndex const last);

    /// Function re-creates the connections that were shifted during the port insertion.
    void portsInserted();

signals:
    DECLARE_SIGNAL(connectionCreated, ConnectionId const);
    DECLARE_SIGNAL(connectionDeleted, ConnectionId const);
    DECLARE_SIGNAL(nodeCreated, NodeId const);
    DECLARE_SIGNAL(nodeDeleted, NodeId const);
    DECLARE_SIGNAL(nodeUpdated, NodeId const);
    DECLARE_SIGNAL(nodeFlagsUpdated, NodeId const);
    DECLARE_SIGNAL(nodePositionUpdated, NodeId const);
    DECLARE_SIGNAL_VOID(modelReset);

    DECLARE_SIGNAL(nodeWaiting, NodeId const);
    DECLARE_SIGNAL(nodeResumed, NodeId const);

private:
    std::vector<ConnectionId> m_shiftedByDynamicPortsConnections;
};

inline void AbstractGraphModel::portsAboutToBeDeleted(NodeId const nodeId,
                                                      PortType const portType,
                                                      PortIndex const first,
                                                      PortIndex const last)
{
    m_shiftedByDynamicPortsConnections.clear();

    NodeRole portCountRole = (portType == PortType::In) ? NodeRole::InPortCount : NodeRole::OutPortCount;

    unsigned int portCount = nodeData(nodeId, portCountRole).toUInt();

    if (portCount == 0) {
        return;
    }
    if (first > portCount - 1) {
        return;
    }
    if (last < first) {
        return;
    }

    PortIndex const clampedLast = std::min(last, portCount - 1);

    for (PortIndex portIndex = first; portIndex <= clampedLast; ++portIndex) {
        std::unordered_set<ConnectionId> conns = connections(nodeId, portType, portIndex);
        for (auto connectionId : conns) {
            deleteConnection(connectionId);
        }
    }

    std::size_t const nRemovedPorts = clampedLast - first + 1;

    for (PortIndex portIndex = clampedLast + 1; portIndex < portCount; ++portIndex) {
        std::unordered_set<ConnectionId> conns = connections(nodeId, portType, portIndex);

        for (auto connectionId : conns) {
            // Erases the information about the port on one side.
            auto c = makeIncompleteConnectionId(connectionId, portType);

            c = makeCompleteConnectionId(c, nodeId, portIndex - static_cast<PortIndex>(nRemovedPorts));

            m_shiftedByDynamicPortsConnections.push_back(c);

            deleteConnection(connectionId);
        }
    }
}

inline void AbstractGraphModel::portsDeleted()
{
    for (auto const connectionId : m_shiftedByDynamicPortsConnections) {
        addConnection(connectionId);
    }
    m_shiftedByDynamicPortsConnections.clear();
}

inline void AbstractGraphModel::portsAboutToBeInserted(NodeId const nodeId,
                                                       PortType const portType,
                                                       PortIndex const first,
                                                       PortIndex const last)
{
    m_shiftedByDynamicPortsConnections.clear();

    NodeRole portCountRole = (portType == PortType::In) ? NodeRole::InPortCount : NodeRole::OutPortCount;

    unsigned int portCount = nodeData(nodeId, portCountRole).toUInt();

    if (first > portCount) {
        return;
    }
    if (last < first) {
        return;
    }

    std::size_t const nNewPorts = last - first + 1;

    for (PortIndex portIndex = first; portIndex < portCount; ++portIndex) {
        std::unordered_set<ConnectionId> conns = connections(nodeId, portType, portIndex);

        for (auto connectionId : conns) {
            // Erases the information about the port on one side.
            auto c = makeIncompleteConnectionId(connectionId, portType);

            c = makeCompleteConnectionId(c, nodeId, portIndex + static_cast<PortIndex>(nNewPorts));

            m_shiftedByDynamicPortsConnections.push_back(c);

            deleteConnection(connectionId);
        }
    }
}

inline void AbstractGraphModel::portsInserted()
{
    for (auto const connectionId : m_shiftedByDynamicPortsConnections) {
        addConnection(connectionId);
    }
    m_shiftedByDynamicPortsConnections.clear();
}

} // namespace SwizioNodes

