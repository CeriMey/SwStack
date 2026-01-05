#include "TimesNodeModel.h"

namespace swnodeeditor {

TimesNodeModel::TimesNodeModel(const SwString& context)
    : BinaryMathModel(context)
{
}

SwString TimesNodeModel::Name() { return "Times"; }

SwString TimesNodeModel::caption() const { return "Times"; }

SwString TimesNodeModel::name() const { return Name(); }

double TimesNodeModel::op_(double a, double b) const { return a * b; }

SwString TimesNodeModel::outName_() const { return "product"; }

} // namespace swnodeeditor
