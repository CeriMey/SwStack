#pragma once
#include <core/types/SwJsonObject.h>
#include <core/types/SwString.h>
#include <functional>
#include <memory>
#include <optional>

namespace swRealtimeDbDetail { class ChangeBatch; }

struct SwRealtimeDbReply {
    bool ok{false};
    SwJsonObject data;
    SwString error;
};
using SwRealtimeDbCompletion = std::function<void(SwRealtimeDbReply)>;

// Use on one SwCoreApplication thread. Callbacks run on that same thread;
// destruction cancels delivery to the former owner. No blocking RPC API.
class SwRealtimeDbClient final {
public:
    explicit SwRealtimeDbClient(const SwString& domain, const SwString& actor,
                        const SwString& endpoint = "rtdb");
    ~SwRealtimeDbClient();
    SwRealtimeDbClient(const SwRealtimeDbClient&) = delete;
    SwRealtimeDbClient& operator=(const SwRealtimeDbClient&) = delete;
    void request(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs = 2000);
    // Execute immediately when this client has no earlier pending operation and
    // the service belongs to this thread. Otherwise use native asynchronous RPC.
    // Completion can run before return; nested completions yield after a bound.
    void requestPreferDirect(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs = 2000);
    void cancelAll();
    // Notifications use the common signal routing and run on this client's loop.
    void setChangedHandler(std::function<void(const SwJsonObject&)> handler);
    bool canRequestDirect() const;
    // A nonblocking immediate request through the common RPC client. Returns
    // nullopt when the receiver requires queued or interprocess delivery.
    std::optional<SwRealtimeDbReply> requestImmediate(const SwJsonObject& request);
private:
    friend class SwRealtimeDbComponent;
    void setNativeChangedHandler(std::function<void(const swRealtimeDbDetail::ChangeBatch&)> handler);
    // Component builds each request from scalar fields or deep-copied public
    // inputs. This private transfer cannot admit caller-owned mutable aliases.
    void requestOwned(SwJsonObject&& request, SwRealtimeDbCompletion complete, int timeoutMs = 2000);
    void requestOwnedPreferDirect(SwJsonObject&& request, SwRealtimeDbCompletion complete, int timeoutMs = 2000);
    void requestPreferredImpl(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs,
                              SwJsonObject* owned);
    void requestImpl(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs,
                     SwJsonObject* owned);
    struct State;
    struct Operation;
    std::shared_ptr<State> state_;
};
