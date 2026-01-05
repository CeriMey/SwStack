#pragma once

#include "SwizioNodes/NodeDelegateModel"

#include "AnyListData.h"

#include <memory>

class SwLineEdit;

namespace swnodeeditor {

class InputNodeModel final : public SwizioNodes::NodeDelegateModel {
public:
    explicit InputNodeModel(const SwString& context);
    ~InputNodeModel() override;

    static SwString Name();

    SwString caption() const override;
    SwString name() const override;

    bool portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const override;
    SwString portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    unsigned int nPorts(SwizioNodes::PortType portType) const override;
    SwizioNodes::NodeDataType dataType(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    std::shared_ptr<SwizioNodes::NodeData> outData(SwizioNodes::PortIndex const port) override;

    SwWidget* embeddedWidget() override;

    void setValue(double v, bool valid);

private:
    std::shared_ptr<AnyListData> m_current{std::make_shared<AnyListData>(AnyListData::makeNumber(0.0, false))};
    std::unique_ptr<SwLineEdit> m_widget;
};

} // namespace swnodeeditor
