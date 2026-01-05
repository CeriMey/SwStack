#include "OutputNodeModel.h"

namespace swnodeeditor {

OutputNodeModel::OutputNodeModel(const SwString& context)
    : SwizioNodes::NodeDelegateModel(context)
{
}

SwString OutputNodeModel::Name() { return "Output"; }

SwString OutputNodeModel::caption() const { return "Output"; }

SwString OutputNodeModel::name() const { return Name(); }

bool OutputNodeModel::portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const { return true; }

SwString OutputNodeModel::portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const
{
    if (portType == SwizioNodes::PortType::In && portIndex == 0) {
        return "in";
    }
    return SwString();
}

unsigned int OutputNodeModel::nPorts(SwizioNodes::PortType portType) const
{
    return (portType == SwizioNodes::PortType::In) ? 1u : 0u;
}

SwizioNodes::NodeDataType OutputNodeModel::dataType(SwizioNodes::PortType, SwizioNodes::PortIndex) const
{
    return AnyListData::Type();
}

void OutputNodeModel::setInData(std::shared_ptr<SwizioNodes::NodeData> data, SwizioNodes::PortIndex const)
{
    m_in = std::dynamic_pointer_cast<AnyListData>(data);
}

std::shared_ptr<AnyListData> OutputNodeModel::input() const { return m_in; }

} // namespace swnodeeditor
