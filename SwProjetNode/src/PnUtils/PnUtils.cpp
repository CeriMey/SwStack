#include "PnUtils.h"

#include "PnCore.h"

int PnUtils::magic() {
    return PnCore::add(40, 2);
}

SwString PnUtils::banner(const SwString& nodeName) {
    return SwString("[") + nodeName + SwString("] ") + PnCore::version();
}
