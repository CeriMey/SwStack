#pragma once

#include "../SwApiRpcWire.h"
#include "SwObject.h"
#include <chrono>
#include <functional>
#include <map>

class SwTimer;

// One consumer per response queue, shared by HTTP and rosbridge in this process.
// All methods and callbacks run on the bridge event loop.
class SwBridgeRpcClient : public SwObject {
public:
    struct Result {
        bool ok{false};
        bool hasReturn{false};
        SwString error, returnType, method;
        SwJsonValue value;
        uint64_t callId{0};
        int transportStatus{0}; // HTTP status for local transport errors; remote errors remain RPC results.
    };
    using Callback = std::function<void(const Result&)>;
    explicit SwBridgeRpcClient(SwObject* parent = nullptr);
    ~SwBridgeRpcClient() override;
    uint64_t call(const SwString& target, const SwString& method, const SwJsonArray& args,
                  int timeoutMs, const SwString& clientInfo, Callback callback);
    void cancel(uint64_t id);

private:
    struct Pending {
        Callback callback;
        std::chrono::steady_clock::time_point deadline;
    };
    struct Channel {
        swapi::wire::RpcQueueAccess response;
        SwString domain, object, method, responseSignal;
        std::vector<std::string> responseTypes;
        std::map<uint64_t, Pending> pending;
    };
    void poll();
    SwTimer* timer_;
    std::map<SwString, Channel> channels_;
};
