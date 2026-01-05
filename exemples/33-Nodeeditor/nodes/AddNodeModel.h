#pragma once

#include "BinaryMathModel.h"

namespace swnodeeditor {

class AddNodeModel final : public BinaryMathModel {
public:
    explicit AddNodeModel(const SwString& context);

    static SwString Name();

    SwString caption() const override;
    SwString name() const override;

protected:
    double op_(double a, double b) const override;
    SwString outName_() const override;
};

} // namespace swnodeeditor
