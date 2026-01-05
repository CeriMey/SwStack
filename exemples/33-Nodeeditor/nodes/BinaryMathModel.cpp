#include "BinaryMathModel.h"

#include <algorithm>
#include <cmath>

namespace swnodeeditor {

BinaryMathModel::BinaryMathModel(const SwString& context)
    : SwizioNodes::NodeDelegateModel(context)
{
}

bool BinaryMathModel::portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const { return true; }

unsigned int BinaryMathModel::nPorts(SwizioNodes::PortType portType) const
{
    if (portType == SwizioNodes::PortType::In) {
        return 2u;
    }
    if (portType == SwizioNodes::PortType::Out) {
        return 1u;
    }
    return 0u;
}

SwizioNodes::NodeDataType BinaryMathModel::dataType(SwizioNodes::PortType, SwizioNodes::PortIndex) const
{
    return AnyListData::Type();
}

void BinaryMathModel::setInData(std::shared_ptr<SwizioNodes::NodeData> data, SwizioNodes::PortIndex const portIndex)
{
    auto num = std::dynamic_pointer_cast<AnyListData>(data);
    if (portIndex == 0) {
        m_a = num;
    } else if (portIndex == 1) {
        m_b = num;
    }
    compute_();
}

std::shared_ptr<SwizioNodes::NodeData> BinaryMathModel::outData(SwizioNodes::PortIndex const portIndex)
{
    if (portIndex != 0) {
        return nullptr;
    }
    return m_out;
}

SwString BinaryMathModel::portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const
{
    if (portType == SwizioNodes::PortType::In) {
        return portIndex == 0 ? "a" : "b";
    }
    if (portType == SwizioNodes::PortType::Out && portIndex == 0) {
        return outName_();
    }
    return SwString();
}

void BinaryMathModel::compute_()
{
    const auto a = m_a ? m_a->readNumber() : AnyListData::NumberPayload{};
    const auto b = m_b ? m_b->readNumber() : AnyListData::NumberPayload{};
    const bool valid = a.valid && b.valid;
    if (!valid) {
        m_out = std::make_shared<AnyListData>(AnyListData::makeNumber(0.0, false));
        dataUpdated(0);
        return;
    }

    const double v = op_(a.value, b.value);
    if (m_out) {
        auto* prev = dynamic_cast<AnyListData*>(m_out.get());
        if (prev) {
            const auto prevNum = prev->readNumber();
            if (prevNum.valid) {
                const double diff = std::abs(prevNum.value - v);
                const double scale = std::max(1.0, std::max(std::abs(prevNum.value), std::abs(v)));
                if (diff <= 1e-12 * scale) {
                    return;
                }
            }
        }
    }
    m_out = std::make_shared<AnyListData>(AnyListData::makeNumber(v, true));
    dataUpdated(0);
}

} // namespace swnodeeditor
