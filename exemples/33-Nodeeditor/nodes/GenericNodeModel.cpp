#include "GenericNodeModel.h"

namespace swnodeeditor {

GenericNodeModel::GenericNodeModel(const SwString& context)
    : SwizioNodes::NodeDelegateModel(context)
{
}

SwString GenericNodeModel::Name() { return "Generic"; }

SwString GenericNodeModel::caption() const { return "Generic"; }

SwString GenericNodeModel::name() const { return Name(); }

bool GenericNodeModel::portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const { return true; }

SwString GenericNodeModel::portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const
{
    if (portType == SwizioNodes::PortType::In && portIndex == 0) {
        return "in";
    }
    if (portType == SwizioNodes::PortType::Out && portIndex == 0) {
        return "out";
    }
    return SwString();
}

unsigned int GenericNodeModel::nPorts(SwizioNodes::PortType portType) const
{
    if (portType == SwizioNodes::PortType::In) {
        return 1u;
    }
    if (portType == SwizioNodes::PortType::Out) {
        return 1u;
    }
    return 0u;
}

SwizioNodes::NodeDataType GenericNodeModel::dataType(SwizioNodes::PortType, SwizioNodes::PortIndex) const
{
    return AnyListData::Type();
}

void GenericNodeModel::setInData(std::shared_ptr<SwizioNodes::NodeData> data, SwizioNodes::PortIndex const)
{
    m_in = std::dynamic_pointer_cast<AnyListData>(data);
    m_out = m_in ? std::make_shared<AnyListData>(m_in->values()) : std::make_shared<AnyListData>(AnyListData::makeNumber(0.0, false));
    dataUpdated(0);
}

std::shared_ptr<SwizioNodes::NodeData> GenericNodeModel::outData(SwizioNodes::PortIndex const portIndex)
{
    if (portIndex != 0) {
        return nullptr;
    }
    return m_out;
}

} // namespace swnodeeditor
