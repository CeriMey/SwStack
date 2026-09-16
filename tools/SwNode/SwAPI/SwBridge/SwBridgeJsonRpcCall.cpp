#include "SwBridgeJsonRpcCall.h"

namespace {
SwJsonArray argument(const SwString& text) { SwJsonArray args; args.append(text); return args; }
SwJsonObject envelope(const SwString& text) {
    SwJsonDocument doc; SwString error;
    if (!doc.loadFromJson(text.toStdString(), error) || !doc.isObject() || !doc.object()["ok"].isBool())
        throw std::runtime_error("Invalid JSON RPC response envelope");
    const auto result = doc.object();
    if (!result["ok"].toBool()) throw std::runtime_error(result["error"].toString().toStdString());
    return result;
}
}

SwBridgeJsonRpcCall::SwBridgeJsonRpcCall(SwBridgeRpcClient& rpc, SwString target, SwString method,
                                       SwString request, int timeoutMs, Callback callback, SwObject* parent)
    : SwObject(parent), rpc_(rpc), target_(std::move(target)), method_(std::move(method)),
      upload_(std::move(request)), callback_(std::move(callback)),
      deadline_(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs)) {}

SwBridgeJsonRpcCall::~SwBridgeJsonRpcCall() {
    if (pending_) rpc_.cancel(pending_);
    if (!finished_) {
        const auto abort = upload_.abortPacket();
        if (!abort.isEmpty()) rpc_.call(target_, method_, argument(abort), 1000, "SwBridge/json-abort",
            [](const SwBridgeRpcClient::Result&) {});
    }
}
void SwBridgeJsonRpcCall::start() { send(method_, argument(upload_.first())); }
void SwBridgeJsonRpcCall::send(const SwString& method, const SwJsonArray& args, bool readback) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) { finish(false, "rpc: timeout"); return; }
    SwPointer<SwBridgeJsonRpcCall> weak(this);
    pending_ = rpc_.call(target_, method, args, static_cast<int>(remaining), "SwBridge/json",
        [weak, readback](const SwBridgeRpcClient::Result& result) {
            if (!weak) return;
            weak->pending_ = 0;
            try { weak->received(result, readback); }
            catch (const std::exception& error) { weak->finish(false, SwString(error.what())); }
        });
}
void SwBridgeJsonRpcCall::received(const SwBridgeRpcClient::Result& result, bool readback) {
    if (!result.ok) { finish(false, result.error); return; }
    if (!result.hasReturn || !result.value.isString()) throw std::runtime_error("JSON RPC must return a string");
    const auto value = result.value.toString();
    if (readback) {
        if (value.isEmpty() || value.size() > bytes_ - text_.size()) throw std::runtime_error("Invalid JSON RPC result chunk");
        text_ += value;
        if (text_.size() == bytes_) complete(text_);
        else readNext();
        return;
    }
    SwString next;
    if (upload_.advance(value, next)) { send(method_, argument(next)); return; }
    const auto response = envelope(value);
    if (!response.contains("result_id")) { complete(value); return; }
    token_ = response["result_id"].toString();
    const auto count = response["bytes"].toLongLong();
    if (token_.isEmpty() || !response["bytes"].isInt() || count <= 0 ||
        count > static_cast<long long>(sw::ipc::JsonRequestAssembler::maxBytes))
        throw std::runtime_error("Invalid JSON RPC result size/token");
    bytes_ = static_cast<size_t>(count);
    readNext();
}
void SwBridgeJsonRpcCall::readNext() {
    SwJsonArray args; args.append(token_); args.append(static_cast<int>(text_.size()));
    send("readResult", args, true);
}
void SwBridgeJsonRpcCall::complete(const SwString& text) {
    const auto response = envelope(text);
    if (!response["data"].isObject()) throw std::runtime_error("Missing JSON RPC result object");
    finish(true, response["data"]);
}
void SwBridgeJsonRpcCall::finish(bool ok, const SwJsonValue& value) {
    if (finished_) return;
    if (!ok) {
        const auto abort = upload_.abortPacket();
        if (!abort.isEmpty()) rpc_.call(target_, method_, argument(abort), 1000, "SwBridge/json-abort",
            [](const SwBridgeRpcClient::Result&) {});
    }
    finished_ = true;
    callback_(ok, value);
    deleteLater();
}
