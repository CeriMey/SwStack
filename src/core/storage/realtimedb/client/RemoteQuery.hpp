#pragma once
#include <core/storage/SwRealtimeDbClient.h>

namespace swRealtimeDbDetail {
// The RTDB wire protocol pages large JSON requests/results. Routing, deadlines
// and cancellation belong to the common RPC client which invokes this adapter.
class RemoteQuery final {
public:
    RemoteQuery(const SwString& domain, const SwString& endpoint, const SwString& actor);
    ~RemoteQuery();
    std::function<void()> request(const SwJsonObject& request,
                                  SwRealtimeDbCompletion complete, int timeoutMs);
private:
    struct State;
    struct Exchange;
    std::shared_ptr<State> state_;
};
}
