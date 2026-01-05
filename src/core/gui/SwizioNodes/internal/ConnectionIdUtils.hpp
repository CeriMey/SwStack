#pragma once

#include "Definitions.hpp"

#include "core/types/SwJsonObject.h"

#include <iostream>
#include <string>

namespace SwizioNodes {

inline NodeId getNodeId(PortType portType, ConnectionId connectionId)
{
    NodeId id = InvalidNodeId;

    if (portType == PortType::Out) {
        id = connectionId.outNodeId;
    } else if (portType == PortType::In) {
        id = connectionId.inNodeId;
    }

    return id;
}

inline PortIndex getPortIndex(PortType portType, ConnectionId connectionId)
{
    PortIndex index = InvalidPortIndex;

    if (portType == PortType::Out) {
        index = connectionId.outPortIndex;
    } else if (portType == PortType::In) {
        index = connectionId.inPortIndex;
    }

    return index;
}

inline PortType oppositePort(PortType port)
{
    switch (port) {
    case PortType::In:
        return PortType::Out;
    case PortType::Out:
        return PortType::In;
    case PortType::None:
    default:
        return PortType::None;
    }
}

inline bool isPortIndexValid(PortIndex index) { return index != InvalidPortIndex; }

inline bool isPortTypeValid(PortType portType) { return portType != PortType::None; }

/**
 * Creates a connection Id instance filled just on one side.
 */
inline ConnectionId makeIncompleteConnectionId(NodeId const connectedNodeId,
                                               PortType const connectedPort,
                                               PortIndex const connectedPortIndex)
{
    return (connectedPort == PortType::In)
               ? ConnectionId{InvalidNodeId, InvalidPortIndex, connectedNodeId, connectedPortIndex}
               : ConnectionId{connectedNodeId, connectedPortIndex, InvalidNodeId, InvalidPortIndex};
}

/**
 * Turns a full connection Id into an incomplete one by removing the
 * data on the given side.
 */
inline ConnectionId makeIncompleteConnectionId(ConnectionId connectionId,
                                               PortType const portToDisconnect)
{
    if (portToDisconnect == PortType::Out) {
        connectionId.outNodeId = InvalidNodeId;
        connectionId.outPortIndex = InvalidPortIndex;
    } else {
        connectionId.inNodeId = InvalidNodeId;
        connectionId.inPortIndex = InvalidPortIndex;
    }

    return connectionId;
}

inline ConnectionId makeCompleteConnectionId(ConnectionId incompleteConnectionId,
                                             NodeId const nodeId,
                                             PortIndex const portIndex)
{
    if (incompleteConnectionId.outNodeId == InvalidNodeId) {
        incompleteConnectionId.outNodeId = nodeId;
        incompleteConnectionId.outPortIndex = portIndex;
    } else {
        incompleteConnectionId.inNodeId = nodeId;
        incompleteConnectionId.inPortIndex = portIndex;
    }

    return incompleteConnectionId;
}

inline std::ostream& operator<<(std::ostream& ostr, ConnectionId const connectionId)
{
    ostr << "(" << connectionId.outNodeId << ", "
         << (isPortIndexValid(connectionId.outPortIndex) ? std::to_string(connectionId.outPortIndex)
                                                         : "INVALID")
         << ", " << connectionId.inNodeId << ", "
         << (isPortIndexValid(connectionId.inPortIndex) ? std::to_string(connectionId.inPortIndex)
                                                        : "INVALID")
         << ")" << std::endl;

    return ostr;
}

inline SwJsonObject toJson(ConnectionId const& connId)
{
    SwJsonObject connJson;

    connJson["outNodeId"] = static_cast<long long>(connId.outNodeId);
    connJson["outPortIndex"] = static_cast<long long>(connId.outPortIndex);
    connJson["inNodeId"] = static_cast<long long>(connId.inNodeId);
    connJson["inPortIndex"] = static_cast<long long>(connId.inPortIndex);

    return connJson;
}

inline ConnectionId fromJson(SwJsonObject const& connJson)
{
    auto readInt = [&connJson](const char* key, long long def) -> long long {
        if (!connJson.contains(key)) {
            return def;
        }
        return connJson[key].toLongLong();
    };

    ConnectionId connId{static_cast<NodeId>(readInt("outNodeId", InvalidNodeId)),
                        static_cast<PortIndex>(readInt("outPortIndex", InvalidPortIndex)),
                        static_cast<NodeId>(readInt("inNodeId", InvalidNodeId)),
                        static_cast<PortIndex>(readInt("inPortIndex", InvalidPortIndex))};

    return connId;
}

} // namespace SwizioNodes

