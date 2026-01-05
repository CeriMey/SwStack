#include "AddNodeModel.h"

namespace swnodeeditor {

AddNodeModel::AddNodeModel(const SwString& context)
    : BinaryMathModel(context)
{
}

SwString AddNodeModel::Name() { return "Add"; }

SwString AddNodeModel::caption() const { return "Add"; }

SwString AddNodeModel::name() const { return Name(); }

double AddNodeModel::op_(double a, double b) const { return a + b; }

SwString AddNodeModel::outName_() const { return "sum"; }

} // namespace swnodeeditor
