#pragma once
#include "../ChangeNotice.hpp"
#include "../JsonSize.hpp"
#include "../SwRealtimeDbJson.h"
#include <initializer_list>

namespace swRealtimeDbDetail {
// Select the complete value batch or its compact wakeup before serializing.
// Native receivers retain the complete packet in either case.
inline SwString notificationWire(const SwJsonObject& packet) {
    if (packet.contains("snapshots")) {
        try {
            JsonSize(changeNoticeCapacity - 64).object(packet);
            return json(packet);
        } catch (const std::runtime_error&) {}
    }
    SwJsonObject compact;
    for (const auto* key : {"epoch", "revision", "from_revision", "tables", "catalog_changed"}) {
        const auto found = packet.dataRef().find(key);
        if (found != packet.dataRef().end()) compact[key] = found->second;
    }
    return json(compact);
}
}
