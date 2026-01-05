#include "NodeDelegateModel.h"

namespace SwizioNodes {

NodeDelegateModel::NodeDelegateModel(const SwString& context)
    : SwObject(nullptr)
    , m_nodeContext(context)
    , m_nodeStyle(StyleCollection::nodeStyle())
{
}

SwJsonObject NodeDelegateModel::save() const
{
    SwJsonObject modelJson;
    modelJson["model-name"] = name().toStdString();
    return modelJson;
}

void NodeDelegateModel::load(SwJsonObject const&)
{
    // Default: no-op.
}

ConnectionPolicy NodeDelegateModel::portConnectionPolicy(PortType portType, PortIndex) const
{
    switch (portType) {
    case PortType::In:
        return ConnectionPolicy::Many;
    case PortType::Out:
        return ConnectionPolicy::Many;
    case PortType::None:
    default:
        return ConnectionPolicy::Many;
    }
}

NodeStyle const& NodeDelegateModel::nodeStyle() const { return m_nodeStyle; }

void NodeDelegateModel::setNodeStyle(NodeStyle const& style) { m_nodeStyle = style; }

} // namespace SwizioNodes

