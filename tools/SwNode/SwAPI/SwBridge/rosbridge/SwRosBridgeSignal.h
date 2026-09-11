#pragma once

#include "SwRosBridgeCatalog.h"

namespace swros {
// JSON access to the dynamic signal ring described by the native registry.
// Topic polling intentionally reads the latest value, including throttled subscriptions.
class Signal {
public:
    explicit Signal(const Binding& binding);
    bool read(uint64_t& sequence, SwJsonArray& values);
    void publish(const SwJsonArray& values);
private:
    swapi::wire::RpcQueueInfo info_;
    std::vector<std::string> types_;
    uint32_t maxPayload_{0};
};
}
