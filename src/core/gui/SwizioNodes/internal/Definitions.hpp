#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/Definitions.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by Definitions in the CoreSw node-editor layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the definitions interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
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


#include "core/types/Sw.h"

#include <algorithm>
#include <limits>

// X11 defines `None` as a macro, which breaks scoped enums such as `PortType::None`.
#ifdef None
#undef None
#endif

/**
 * @file
 * Important definitions used throughout the Node Editor library.
 */

namespace SwizioNodes {

/**
 * Constants used for fetching SwAny data from GraphModel.
 */
enum class NodeRole {
    Type = 0,           ///< Type of the current node, usually a string.
    Position = 1,       ///< `SwPointF` position of the node on the scene.
    Size = 2,           ///< `SwSize` for resizable nodes.
    CaptionVisible = 3, ///< `bool` for caption visibility.
    Caption = 4,        ///< `SwString` for node caption.
    Style = 5,          ///< Custom NodeStyle as SwJsonDocument (stored in SwAny).
    InternalData = 6,   ///< Node-specific user data as SwJsonObject.
    InPortCount = 7,    ///< `unsigned int`
    OutPortCount = 9,   ///< `unsigned int`
    Widget = 10,        ///< Optional `SwWidget*` or `nullptr`
};

/**
 * Specific flags regulating node features and appearance.
 */
enum class NodeFlag {
    NoFlags = 0x0,   ///< Default NodeFlag
    Resizable = 0x1, ///< Lets the node be resizable
    Locked = 0x2,
};

using NodeFlags = SwFlagSet<NodeFlag>;

/**
 * Constants for fetching port-related information from the GraphModel.
 */
enum class PortRole {
    Data = 0,                 ///< `std::shared_ptr<NodeData>`.
    DataType = 1,             ///< `SwString` describing the port data type.
    ConnectionPolicyRole = 2, ///< `enum` ConnectionPolicyRole
    CaptionVisible = 3,       ///< `bool` for caption visibility.
    Caption = 4,              ///< `SwString` for port caption.
    DataPropagation = 5,      ///< `bool` Enable data propagation on connection.
};

/**
 * Defines how many connections are possible to attach to ports. The
 * values are fetched using PortRole::ConnectionPolicyRole.
 */
enum class ConnectionPolicy {
    One,  ///< Just one connection for each port.
    Many, ///< Any number of connections possible for the port.
};

/**
 * Used for distinguishing input and output node ports.
 */
enum class PortType {
    In = 0,  ///< Input node port (from the left).
    Out = 1, ///< Output node port (from the right).
    None = 2
};

using PortCount = unsigned int;

/// Ports are consecutively numbered starting from zero.
using PortIndex = unsigned int;

static constexpr PortIndex InvalidPortIndex = std::numeric_limits<PortIndex>::max();

/// Unique Id associated with each node in the GraphModel.
using NodeId = unsigned int;

static constexpr NodeId InvalidNodeId = std::numeric_limits<NodeId>::max();

/**
 * A unique connection identificator that stores
 * out `NodeId`, out `PortIndex`, in `NodeId`, in `PortIndex`.
 */
struct ConnectionId
{
    NodeId outNodeId;
    PortIndex outPortIndex;
    NodeId inNodeId;
    PortIndex inPortIndex;
};

inline bool operator==(ConnectionId const& a, ConnectionId const& b)
{
    return a.outNodeId == b.outNodeId && a.outPortIndex == b.outPortIndex
           && a.inNodeId == b.inNodeId && a.inPortIndex == b.inPortIndex;
}

inline bool operator!=(ConnectionId const& a, ConnectionId const& b) { return !(a == b); }

inline void invertConnection(ConnectionId& id)
{
    std::swap(id.outNodeId, id.inNodeId);
    std::swap(id.outPortIndex, id.inPortIndex);
}

} // namespace SwizioNodes
