#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/DataFlowGraphModel.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by DataFlowGraphModel in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the data flow graph model interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * Model-oriented declarations here define the data contract consumed by views, delegates, or
 * algorithms, with an emphasis on stable roles, ownership, and update flow rather than on
 * presentation details.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


#include "AbstractGraphModel.hpp"
#include "ConnectionIdUtils.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "Serializable.hpp"
#include "StyleCollection.hpp"

#include "core/types/SwJsonObject.h"
#include "core/types/SwJsonArray.h"

#include "graphics/SwGraphicsTypes.h"

#include "core/runtime/SwCoreApplication.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

#include <memory>
#include <unordered_map>
#include <unordered_set>

class SwWidget;

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC DataFlowGraphModel : public AbstractGraphModel, public Serializable
{
    SW_OBJECT(DataFlowGraphModel, AbstractGraphModel)

public:
    struct NodeGeometryData
    {
        SwSize size{};
        SwPointF pos{};
    };

public:
    /**
     * @brief Constructs a `DataFlowGraphModel` instance.
     * @param context Value passed to the method.
     * @param registry Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    DataFlowGraphModel(const SwString& context, std::shared_ptr<NodeDelegateModelRegistry> registry);

    /**
     * @brief Destroys the `DataFlowGraphModel` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~DataFlowGraphModel() override = default;

    /**
     * @brief Returns the current data Model Registry.
     * @return The current data Model Registry.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::shared_ptr<NodeDelegateModelRegistry> dataModelRegistry() { return m_registry; }

public:
    /**
     * @brief Returns the current all Node Ids.
     * @return The current all Node Ids.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::unordered_set<NodeId> allNodeIds() const override;

    /**
     * @brief Performs the `allConnectionIds` operation.
     * @param nodeId Value passed to the method.
     * @return The requested all Connection Ids.
     */
    std::unordered_set<ConnectionId> allConnectionIds(NodeId const nodeId) const override;

    /**
     * @brief Performs the `connections` operation.
     * @param nodeId Value passed to the method.
     * @param portType Value passed to the method.
     * @param portIndex Value passed to the method.
     * @return The requested connections.
     */
    std::unordered_set<ConnectionId> connections(NodeId nodeId,
                                                 PortType portType,
                                                 PortIndex portIndex) const override;

    /**
     * @brief Performs the `connectionExists` operation.
     * @param connectionId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool connectionExists(ConnectionId const connectionId) const override;

    /**
     * @brief Adds the specified node.
     * @param nodeType Value passed to the method.
     * @return The requested node.
     */
    NodeId addNode(SwString const nodeType) override;

    /**
     * @brief Performs the `connectionPossible` operation.
     * @param connectionId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool connectionPossible(ConnectionId const connectionId) const override;

    /**
     * @brief Adds the specified connection.
     * @param connectionId Value passed to the method.
     */
    void addConnection(ConnectionId const connectionId) override;

    /**
     * @brief Performs the `nodeExists` operation.
     * @param nodeId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool nodeExists(NodeId const nodeId) const override;

    /**
     * @brief Performs the `nodeData` operation.
     * @param nodeId Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested node Data.
     */
    SwAny nodeData(NodeId nodeId, NodeRole role) const override;

    /**
     * @brief Performs the `nodeFlags` operation.
     * @param nodeId Value passed to the method.
     * @return The requested node Flags.
     */
    NodeFlags nodeFlags(NodeId nodeId) const override;

    /**
     * @brief Sets the node Data.
     * @param nodeId Value passed to the method.
     * @param role Value passed to the method.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setNodeData(NodeId nodeId, NodeRole role, SwAny value) override;

    /**
     * @brief Performs the `portData` operation.
     * @param nodeId Value passed to the method.
     * @param portType Value passed to the method.
     * @param portIndex Value passed to the method.
     * @param role Value passed to the method.
     * @return The requested port Data.
     */
    SwAny portData(NodeId nodeId,
                   PortType portType,
                   PortIndex portIndex,
                   PortRole role) const override;

    /**
     * @brief Sets the port Data.
     * @param nodeId Value passed to the method.
     * @param portType Value passed to the method.
     * @param portIndex Value passed to the method.
     * @param value Value passed to the method.
     * @param role Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setPortData(NodeId nodeId,
                     PortType portType,
                     PortIndex portIndex,
                     SwAny const& value,
                     PortRole role = PortRole::Data) override;

    /**
     * @brief Performs the `deleteConnection` operation.
     * @param connectionId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool deleteConnection(ConnectionId const connectionId) override;

    /**
     * @brief Performs the `deleteNode` operation.
     * @param nodeId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool deleteNode(NodeId const nodeId) override;

    /**
     * @brief Performs the `saveNode` operation on the associated resource.
     * @return The requested node.
     */
    SwJsonObject saveNode(NodeId const) const override;

    /**
     * @brief Returns the current save.
     * @return The current save.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwJsonObject save() const override;

    /**
     * @brief Performs the `loadNode` operation on the associated resource.
     * @param nodeJson Value passed to the method.
     */
    void loadNode(SwJsonObject const& nodeJson) override;

    /**
     * @brief Performs the `load` operation on the associated resource.
     * @param json Value passed to the method.
     */
    void load(SwJsonObject const& json) override;

    /**
     * Fetches the NodeDelegateModel for the given `nodeId` and tries to cast the
     * stored pointer to the given type.
     */
    template<typename NodeDelegateModelType>
    /**
     * @brief Performs the `delegateModel` operation.
     * @param nodeId Value passed to the method.
     * @return The requested delegate Model.
     */
    NodeDelegateModelType* delegateModel(NodeId const nodeId)
    {
        auto it = m_models.find(nodeId);
        if (it == m_models.end()) {
            return nullptr;
        }
        return dynamic_cast<NodeDelegateModelType*>(it->second.get());
    }

    /**
     * @brief Performs the `delegateModel` operation.
     * @param nodeId Value passed to the method.
     * @return The requested delegate Model.
     */
    NodeDelegateModel* delegateModel(NodeId const nodeId)
    {
        auto it = m_models.find(nodeId);
        if (it == m_models.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    /**
     * @brief Performs the `delegateModel` operation.
     * @param nodeId Value passed to the method.
     * @return The requested delegate Model.
     */
    NodeDelegateModel const* delegateModel(NodeId const nodeId) const
    {
        auto it = m_models.find(nodeId);
        if (it == m_models.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    /**
     * @brief Sets the registry.
     * @param newRegistry Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRegistry(const std::shared_ptr<NodeDelegateModelRegistry>& newRegistry);

signals:
    DECLARE_SIGNAL(inPortDataWasSet, NodeId const, PortType const, PortIndex const);
    DECLARE_SIGNAL(outPortDataUpdated, NodeId const, PortIndex const);

private:
    NodeId newNodeId() override { return m_nextNodeId++; }

    void sendConnectionCreation(ConnectionId const connectionId);
    void sendConnectionDeletion(ConnectionId const connectionId);

private:
    void onOutPortDataUpdated_(NodeId const nodeId, PortIndex const portIndex);

    /// Function is called after detaching a connection.
    void propagateEmptyDataTo_(NodeId const nodeId, PortIndex const portIndex);

private:
    std::shared_ptr<NodeDelegateModelRegistry> m_registry;

    NodeId m_nextNodeId{1};

    std::unordered_map<NodeId, std::unique_ptr<NodeDelegateModel>> m_models;

    std::unordered_set<ConnectionId> m_connectivity;

    mutable std::unordered_map<NodeId, NodeGeometryData> m_nodeGeometryData;

    SwString m_nodesContext;

    std::shared_ptr<int> m_lifeToken{std::make_shared<int>(0)};
};

inline void registerDataFlowGraphModelAnyTypesOnce_()
{
    static bool once = false;
    if (once) {
        return;
    }
    once = true;

    // Types that we store inside SwAny throughout the graph model.
    SwAny::registerMetaType<SwPointF>();
    SwAny::registerMetaType<SwSize>();
    SwAny::registerMetaType<NodeDataType>();
    SwAny::registerMetaType<ConnectionPolicy>();
    SwAny::registerMetaType<std::shared_ptr<NodeData>>();
    SwAny::registerMetaType<SwWidget*>();
}

inline DataFlowGraphModel::DataFlowGraphModel(const SwString& context, std::shared_ptr<NodeDelegateModelRegistry> registry)
    : AbstractGraphModel(nullptr),
      m_registry(std::move(registry)),
      m_nextNodeId(1),
      m_nodesContext(context)
{
    registerDataFlowGraphModelAnyTypesOnce_();
}

inline std::unordered_set<NodeId> DataFlowGraphModel::allNodeIds() const
{
    std::unordered_set<NodeId> nodeIds;
    nodeIds.reserve(m_models.size());
    for (auto const& p : m_models) {
        nodeIds.insert(p.first);
    }
    return nodeIds;
}

inline std::unordered_set<ConnectionId> DataFlowGraphModel::allConnectionIds(NodeId const nodeId) const
{
    std::unordered_set<ConnectionId> result;

    std::copy_if(m_connectivity.begin(),
                 m_connectivity.end(),
                 std::inserter(result, std::end(result)),
                 [&nodeId](ConnectionId const& cid) { return cid.inNodeId == nodeId || cid.outNodeId == nodeId; });

    return result;
}

inline std::unordered_set<ConnectionId> DataFlowGraphModel::connections(NodeId nodeId,
                                                                        PortType portType,
                                                                        PortIndex portIndex) const
{
    std::unordered_set<ConnectionId> result;

    std::copy_if(m_connectivity.begin(),
                 m_connectivity.end(),
                 std::inserter(result, std::end(result)),
                 [&portType, &portIndex, &nodeId](ConnectionId const& cid) {
                     return (getNodeId(portType, cid) == nodeId && getPortIndex(portType, cid) == portIndex);
                 });

    return result;
}

inline bool DataFlowGraphModel::connectionExists(ConnectionId const connectionId) const
{
    return (m_connectivity.find(connectionId) != m_connectivity.end());
}

inline NodeId DataFlowGraphModel::addNode(SwString const nodeType)
{
    if (!m_registry) {
        return InvalidNodeId;
    }

    std::unique_ptr<NodeDelegateModel> model = m_registry->create(nodeType, m_nodesContext);

    if (!model) {
        return InvalidNodeId;
    }

    NodeId newId = newNodeId();

    SwObject::connect(model.get(), &NodeDelegateModel::dataUpdated, this, [newId, this](PortIndex portIndex) {
        onOutPortDataUpdated_(newId, portIndex);
    });

    SwObject::connect(model.get(),
                      &NodeDelegateModel::portsAboutToBeDeleted,
                      this,
                      [newId, this](PortType portType, PortIndex first, PortIndex last) {
                          portsAboutToBeDeleted(newId, portType, first, last);
                      });
    SwObject::connect(model.get(), &NodeDelegateModel::portsDeleted, this, [this]() { portsDeleted(); });

    SwObject::connect(model.get(),
                      &NodeDelegateModel::portsAboutToBeInserted,
                      this,
                      [newId, this](PortType portType, PortIndex first, PortIndex last) {
                          portsAboutToBeInserted(newId, portType, first, last);
                      });
    SwObject::connect(model.get(), &NodeDelegateModel::portsInserted, this, [this]() { portsInserted(); });

    m_models[newId] = std::move(model);

    nodeCreated(newId);
    return newId;
}

inline bool DataFlowGraphModel::connectionPossible(ConnectionId const connectionId) const
{
    auto getDataType = [&](PortType const portType) -> NodeDataType {
        SwAny dtAny = portData(getNodeId(portType, connectionId),
                               portType,
                               getPortIndex(portType, connectionId),
                               PortRole::DataType);
        if (dtAny.typeName().empty()) {
            return NodeDataType{};
        }
        try {
            return dtAny.get<NodeDataType>();
        } catch (...) {
            return NodeDataType{};
        }
    };

    auto portVacant = [&](PortType const portType) -> bool {
        NodeId const nodeId = getNodeId(portType, connectionId);
        PortIndex const portIndex = getPortIndex(portType, connectionId);
        auto const connected = connections(nodeId, portType, portIndex);

        SwAny policyAny = portData(nodeId, portType, portIndex, PortRole::ConnectionPolicyRole);
        ConnectionPolicy policy = ConnectionPolicy::Many;
        if (!policyAny.typeName().empty()) {
            try {
                policy = policyAny.get<ConnectionPolicy>();
            } catch (...) {
            }
        }

        return connected.empty() || (policy == ConnectionPolicy::Many);
    };

    NodeDataType outType = getDataType(PortType::Out);
    NodeDataType inType = getDataType(PortType::In);

    SwString outId = outType.id;
    SwString inId = inType.id;
    outId.replace("_debug", "");
    inId.replace("_debug", "");

    if (outId.isEmpty() || inId.isEmpty()) {
        return false;
    }
    return (outId == inId) && portVacant(PortType::Out) && portVacant(PortType::In);
}

inline void DataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    m_connectivity.insert(connectionId);

    sendConnectionCreation(connectionId);

    const bool isPropagationEnabled = portData(connectionId.outNodeId,
                                               PortType::Out,
                                               connectionId.outPortIndex,
                                               PortRole::DataPropagation)
                                          .toBool();
    if (!isPropagationEnabled) {
        return;
    }

    SwAny const portDataToPropagate = portData(connectionId.outNodeId,
                                               PortType::Out,
                                               connectionId.outPortIndex,
                                               PortRole::Data);
    std::weak_ptr<int> weakLife = m_lifeToken;
    DataFlowGraphModel* self = this;
    SwCoreApplication::instance()->postEvent([weakLife, self, connectionId, portDataToPropagate]() {
        if (weakLife.expired()) {
            return;
        }
        self->setPortData(connectionId.inNodeId, PortType::In, connectionId.inPortIndex, portDataToPropagate, PortRole::Data);
    });
}

inline void DataFlowGraphModel::sendConnectionCreation(ConnectionId const connectionId)
{
    connectionCreated(connectionId);

    auto iti = m_models.find(connectionId.inNodeId);
    auto ito = m_models.find(connectionId.outNodeId);
    if (iti != m_models.end() && ito != m_models.end()) {
        auto& inModel = iti->second;
        auto& outModel = ito->second;
        inModel->inputConnectionCreated(connectionId);
        outModel->outputConnectionCreated(connectionId);
    }
}

inline void DataFlowGraphModel::sendConnectionDeletion(ConnectionId const connectionId)
{
    connectionDeleted(connectionId);

    auto iti = m_models.find(connectionId.inNodeId);
    auto ito = m_models.find(connectionId.outNodeId);
    if (iti != m_models.end() && ito != m_models.end()) {
        auto& inModel = iti->second;
        auto& outModel = ito->second;
        inModel->inputConnectionDeleted(connectionId);
        outModel->outputConnectionDeleted(connectionId);
    }
}

inline bool DataFlowGraphModel::nodeExists(NodeId const nodeId) const
{
    return (m_models.find(nodeId) != m_models.end());
}

inline SwAny DataFlowGraphModel::nodeData(NodeId nodeId, NodeRole role) const
{
    auto it = m_models.find(nodeId);
    if (it == m_models.end()) {
        return SwAny();
    }

    auto& model = it->second;

    switch (role) {
    case NodeRole::Type:
        return SwAny(model->name());

    case NodeRole::Position:
        return SwAny::from(m_nodeGeometryData[nodeId].pos);

    case NodeRole::Size:
        return SwAny::from(m_nodeGeometryData[nodeId].size);

    case NodeRole::CaptionVisible:
        return SwAny(model->captionVisible());

    case NodeRole::Caption:
        return SwAny(model->caption());

    case NodeRole::Style: {
        auto style = StyleCollection::nodeStyle();
        return SwAny::from(style.toJson());
    }

    case NodeRole::InternalData: {
        SwJsonObject nodeJson;
        nodeJson["internal-data"] = m_models.at(nodeId)->save();
        return SwAny::from(nodeJson);
    }

    case NodeRole::InPortCount:
        return SwAny(model->nPorts(PortType::In));

    case NodeRole::OutPortCount:
        return SwAny(model->nPorts(PortType::Out));

    case NodeRole::Widget:
        return SwAny::from(model->embeddedWidget());
    }

    return SwAny();
}

inline NodeFlags DataFlowGraphModel::nodeFlags(NodeId nodeId) const
{
    auto it = m_models.find(nodeId);
    if (it != m_models.end() && it->second && it->second->resizable()) {
        return NodeFlag::Resizable;
    }
    return NodeFlag::NoFlags;
}

inline bool DataFlowGraphModel::setNodeData(NodeId nodeId, NodeRole role, SwAny value)
{
    bool result = false;

    switch (role) {
    case NodeRole::Position: {
        m_nodeGeometryData[nodeId].pos = value.get<SwPointF>();
        nodePositionUpdated(nodeId);
        result = true;
    } break;

    case NodeRole::Size: {
        m_nodeGeometryData[nodeId].size = value.get<SwSize>();
        result = true;
    } break;

    default:
        break;
    }

    return result;
}

inline SwAny DataFlowGraphModel::portData(NodeId nodeId,
                                         PortType portType,
                                         PortIndex portIndex,
                                         PortRole role) const
{
    auto it = m_models.find(nodeId);
    if (it == m_models.end()) {
        return SwAny();
    }

    auto& model = it->second;

    switch (role) {
    case PortRole::Data:
        if (portType == PortType::Out) {
            return SwAny::from(model->outData(portIndex));
        }
        return SwAny();

    case PortRole::DataType:
        return SwAny::from(model->dataType(portType, portIndex));

    case PortRole::ConnectionPolicyRole:
        return SwAny::from(model->portConnectionPolicy(portType, portIndex));

    case PortRole::CaptionVisible:
        return SwAny(model->portCaptionVisible(portType, portIndex));

    case PortRole::Caption:
        return SwAny(model->portCaption(portType, portIndex));

    case PortRole::DataPropagation:
        return SwAny(model->dataPropagationOnConnection(portIndex));
    }

    return SwAny();
}

inline bool DataFlowGraphModel::setPortData(NodeId nodeId,
                                            PortType portType,
                                            PortIndex portIndex,
                                            SwAny const& value,
                                            PortRole role)
{
    auto it = m_models.find(nodeId);
    if (it == m_models.end()) {
        return false;
    }

    auto& model = it->second;

    switch (role) {
    case PortRole::Data:
        if (portType == PortType::In) {
            std::shared_ptr<NodeData> inData;
            if (!value.typeName().empty()) {
                // The expected type is `std::shared_ptr<NodeData>`.
                try {
                    inData = value.get<std::shared_ptr<NodeData>>();
                } catch (...) {
                    inData.reset();
                }
            }

            model->setInData(inData, portIndex);

            // Triggers repainting on the scene.
            inPortDataWasSet(nodeId, portType, portIndex);
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

inline bool DataFlowGraphModel::deleteConnection(ConnectionId const connectionId)
{
    bool disconnected = false;

    auto it = m_connectivity.find(connectionId);
    if (it != m_connectivity.end()) {
        disconnected = true;
        m_connectivity.erase(it);
    }

    if (disconnected) {
        sendConnectionDeletion(connectionId);
        propagateEmptyDataTo_(getNodeId(PortType::In, connectionId), getPortIndex(PortType::In, connectionId));
    }

    return disconnected;
}

inline bool DataFlowGraphModel::deleteNode(NodeId const nodeId)
{
    // Delete connections to this node first.
    auto connectionIds = allConnectionIds(nodeId);
    for (auto const& cId : connectionIds) {
        deleteConnection(cId);
    }

    m_nodeGeometryData.erase(nodeId);
    m_models.erase(nodeId);

    nodeDeleted(nodeId);
    return true;
}

inline SwJsonObject DataFlowGraphModel::saveNode(NodeId const nodeId) const
{
    SwJsonObject nodeJson;
    nodeJson["id"] = static_cast<long long>(nodeId);
    nodeJson["internal-data"] = m_models.at(nodeId)->save();

    SwPointF pos{};
    auto it = m_nodeGeometryData.find(nodeId);
    if (it != m_nodeGeometryData.end()) {
        pos = it->second.pos;
    }
    SwJsonObject posJson;
    posJson["x"] = pos.x;
    posJson["y"] = pos.y;
    nodeJson["position"] = posJson;

    return nodeJson;
}

inline SwJsonObject DataFlowGraphModel::save() const
{
    SwJsonObject sceneJson;

    SwJsonArray nodesJsonArray;
    for (auto const nodeId : allNodeIds()) {
        nodesJsonArray.append(saveNode(nodeId));
    }
    sceneJson["nodes"] = nodesJsonArray;

    SwJsonArray connJsonArray;
    for (auto const& cid : m_connectivity) {
        connJsonArray.append(toJson(cid));
    }
    sceneJson["connections"] = connJsonArray;

    return sceneJson;
}

inline void DataFlowGraphModel::loadNode(SwJsonObject const& nodeJson)
{
    if (!nodeJson.contains("id")) {
        throw std::logic_error("loadNode: missing 'id'");
    }

    NodeId restoredNodeId = static_cast<NodeId>(nodeJson["id"].toLongLong());
    m_nextNodeId = std::max(m_nextNodeId, restoredNodeId + 1);

    SwJsonObject internalDataJson;
    if (nodeJson.contains("internal-data") && nodeJson["internal-data"].isObject()) {
        internalDataJson = SwJsonObject(nodeJson["internal-data"].toObject());
    }

    SwString delegateModelName;
    if (internalDataJson.contains("model-name")) {
        delegateModelName = internalDataJson["model-name"].toString();
    }

    if (!m_registry) {
        throw std::logic_error("No registry set on DataFlowGraphModel");
    }

    std::unique_ptr<NodeDelegateModel> model = m_registry->create(delegateModelName, m_nodesContext);

    if (!model) {
        throw std::logic_error(std::string("No registered model with name ") + delegateModelName.toStdString());
    }

    SwObject::connect(model.get(), &NodeDelegateModel::dataUpdated, this, [restoredNodeId, this](PortIndex portIndex) {
        onOutPortDataUpdated_(restoredNodeId, portIndex);
    });

    SwObject::connect(model.get(),
                      &NodeDelegateModel::portsAboutToBeDeleted,
                      this,
                      [restoredNodeId, this](PortType portType, PortIndex first, PortIndex last) {
                          portsAboutToBeDeleted(restoredNodeId, portType, first, last);
                      });
    SwObject::connect(model.get(), &NodeDelegateModel::portsDeleted, this, [this]() { portsDeleted(); });

    SwObject::connect(model.get(),
                      &NodeDelegateModel::portsAboutToBeInserted,
                      this,
                      [restoredNodeId, this](PortType portType, PortIndex first, PortIndex last) {
                          portsAboutToBeInserted(restoredNodeId, portType, first, last);
                      });
    SwObject::connect(model.get(), &NodeDelegateModel::portsInserted, this, [this]() { portsInserted(); });

    m_models[restoredNodeId] = std::move(model);

    nodeCreated(restoredNodeId);

    if (nodeJson.contains("position") && nodeJson["position"].isObject()) {
        SwJsonObject posJson(nodeJson["position"].toObject());
        SwPointF pos;
        pos.x = posJson.contains("x") ? posJson["x"].toDouble() : 0.0;
        pos.y = posJson.contains("y") ? posJson["y"].toDouble() : 0.0;
        setNodeData(restoredNodeId, NodeRole::Position, SwAny::from(pos));
    }

    m_models[restoredNodeId]->load(internalDataJson);
}

inline void DataFlowGraphModel::load(SwJsonObject const& jsonDocument)
{
    if (jsonDocument.contains("nodes") && jsonDocument["nodes"].isArray()) {
        const SwJsonArray nodesJsonArray = jsonDocument["nodes"].toArray();
        for (auto const& nodeVal : nodesJsonArray.data()) {
            if (nodeVal.isObject()) {
                loadNode(SwJsonObject(nodeVal.toObject()));
            }
        }
    }

    if (jsonDocument.contains("connections") && jsonDocument["connections"].isArray()) {
        const SwJsonArray connJsonArray = jsonDocument["connections"].toArray();
        for (auto const& connVal : connJsonArray.data()) {
            if (connVal.isObject()) {
                ConnectionId connId = fromJson(SwJsonObject(connVal.toObject()));
                addConnection(connId);
            }
        }
    }
}

inline void DataFlowGraphModel::onOutPortDataUpdated_(NodeId const nodeId, PortIndex const portIndex)
{
    outPortDataUpdated(nodeId, portIndex);

    const bool isPropagationEnabled = portData(nodeId, PortType::Out, portIndex, PortRole::DataPropagation).toBool();
    if (!isPropagationEnabled) {
        return;
    }

    std::unordered_set<ConnectionId> const connected = connections(nodeId, PortType::Out, portIndex);
    SwAny const portDataToPropagate = portData(nodeId, PortType::Out, portIndex, PortRole::Data);

    std::weak_ptr<int> weakLife = m_lifeToken;
    DataFlowGraphModel* self = this;
    for (auto const& cn : connected) {
        SwCoreApplication::instance()->postEvent([weakLife, self, cn, portDataToPropagate]() {
            if (weakLife.expired()) {
                return;
            }
            self->setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
        });
    }
}

inline void DataFlowGraphModel::propagateEmptyDataTo_(NodeId const nodeId, PortIndex const portIndex)
{
    SwAny emptyData{};
    setPortData(nodeId, PortType::In, portIndex, emptyData, PortRole::Data);
}

inline void DataFlowGraphModel::setRegistry(const std::shared_ptr<NodeDelegateModelRegistry>& newRegistry)
{
    m_registry = newRegistry;
}

} // namespace SwizioNodes
