#pragma once

#include "SwizioNodes/NodeDelegateModel"

#include "AnyListData.h"

#include <memory>

namespace swnodeeditor {

class BinaryMathModel : public SwizioNodes::NodeDelegateModel {
public:
    explicit BinaryMathModel(const SwString& context);

    bool portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const override;
    unsigned int nPorts(SwizioNodes::PortType portType) const override;
    SwizioNodes::NodeDataType dataType(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

    void setInData(std::shared_ptr<SwizioNodes::NodeData> data, SwizioNodes::PortIndex const portIndex) override;
    std::shared_ptr<SwizioNodes::NodeData> outData(SwizioNodes::PortIndex const portIndex) override;

protected:
    virtual double op_(double a, double b) const = 0;
    virtual SwString outName_() const = 0;

    SwString portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const override;

private:
    void compute_();

    std::shared_ptr<AnyListData> m_a;
    std::shared_ptr<AnyListData> m_b;
    std::shared_ptr<SwizioNodes::NodeData> m_out{std::make_shared<AnyListData>(AnyListData::makeNumber(0.0, false))};
};

} // namespace swnodeeditor
