#pragma once

#include "SwizioNodes/NodeDelegateModel"

#include "AnyListData.h"

#include <memory>

namespace swnodeeditor {

class GenericNodeModel final : public SwizioNodes::NodeDelegateModel {
public:
    explicit GenericNodeModel(const SwString& context);

    static SwString Name();

    SwString caption() const override;
    SwString name() const override;

    bool portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const override;
    SwString portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    unsigned int nPorts(SwizioNodes::PortType portType) const override;
    SwizioNodes::NodeDataType dataType(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    void setInData(std::shared_ptr<SwizioNodes::NodeData> data, SwizioNodes::PortIndex const portIndex) override;
    std::shared_ptr<SwizioNodes::NodeData> outData(SwizioNodes::PortIndex const portIndex) override;

private:
    std::shared_ptr<AnyListData> m_in;
    std::shared_ptr<SwizioNodes::NodeData> m_out{std::make_shared<AnyListData>(AnyListData::makeNumber(0.0, false))};
};

} // namespace swnodeeditor
