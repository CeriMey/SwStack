#pragma once

#include "SwizioNodes/NodeDelegateModel"

#include "AnyListData.h"

#include <memory>

namespace swnodeeditor {

class OutputNodeModel final : public SwizioNodes::NodeDelegateModel {
public:
    explicit OutputNodeModel(const SwString& context);

    static SwString Name();

    SwString caption() const override;
    SwString name() const override;

    bool portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const override;
    SwString portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    unsigned int nPorts(SwizioNodes::PortType portType) const override;
    SwizioNodes::NodeDataType dataType(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    void setInData(std::shared_ptr<SwizioNodes::NodeData> data, SwizioNodes::PortIndex const portIndex) override;

    std::shared_ptr<AnyListData> input() const;

private:
    std::shared_ptr<AnyListData> m_in;
};

} // namespace swnodeeditor
