#pragma once
#include "SwBridgeRpcClient.h"
#include <core/remote/SwJsonRpcTransfer.h>
#include "SwPointer.h"

// One bounded asynchronous JSON transfer. Owned by its caller/session; destroying
// it cancels the pending reply and releases an unfinished upload when possible.
class SwBridgeJsonRpcCall final : public SwObject {
public:
    using Callback = std::function<void(bool, const SwJsonValue&)>;
    SwBridgeJsonRpcCall(SwBridgeRpcClient& rpc, SwString target, SwString method,
                        SwString request, int timeoutMs, Callback callback, SwObject* parent);
    ~SwBridgeJsonRpcCall() override;
    void start();
private:
    void send(const SwString& method, const SwJsonArray& args, bool readback = false);
    void received(const SwBridgeRpcClient::Result& result, bool readback);
    void complete(const SwString& text);
    void finish(bool ok, const SwJsonValue& value);
    void readNext();
    SwBridgeRpcClient& rpc_;
    SwString target_, method_;
    sw::ipc::JsonRequestUpload upload_;
    Callback callback_;
    std::chrono::steady_clock::time_point deadline_;
    uint64_t pending_{0};
    bool finished_{false};
    SwString token_, text_;
    size_t bytes_{0};
};
