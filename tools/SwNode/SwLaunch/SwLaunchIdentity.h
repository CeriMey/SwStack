#pragma once
#include "SwString.h"

// SwRemoteObject uses the bare object name for the root namespace. Keep the
// controller, process supervisor and heartbeat lookup on that same identity.
inline SwString swLaunchRuntimeId_(const SwString& nameSpace, const SwString& name) {
    return nameSpace.isEmpty() ? name : nameSpace + "/" + name;
}

inline bool swLaunchIdentityValid_(const SwString& nameSpace, const SwString& name,
                                   bool allowRootNamespace) {
    return !name.isEmpty() && (allowRootNamespace || !nameSpace.isEmpty());
}
