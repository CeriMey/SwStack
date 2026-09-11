#include "SwBridgeRpcClient.h"
#include "SwIpcRpcRouter.h"
#include "SwTimer.h"

using namespace swapi::wire;

SwBridgeRpcClient::SwBridgeRpcClient(SwObject* parent) : SwObject(parent), timer_(new SwTimer(5, this)) {
    connect(timer_, &SwTimer::timeout, this, [this] { poll(); });
}

SwBridgeRpcClient::~SwBridgeRpcClient() { timer_->stop(); }

uint64_t SwBridgeRpcClient::call(const SwString& target, const SwString& requestedMethod,
                                const SwJsonArray& args, int timeoutMs,
                                const SwString& clientInfo, Callback callback) {
    Result failure;
    failure.transportStatus = 400;
    failure.method = requestedMethod;
    try {
        size_t count = 0;
        for (const auto& item : channels_) count += item.second.pending.size();
        if (count >= 256) { failure.transportStatus = 503; throw std::runtime_error("rpc: too many pending calls"); }
        const int slash = target.indexOf('/');
        if (slash <= 0 || slash + 1 >= static_cast<int>(target.size()))
            throw std::runtime_error("rpc: expected domain/object target");
        const SwString domain = target.left(slash), object = target.mid(slash + 1);
        RpcQueueInfo requestInfo;
        SwString method;
        if (!findRpcRequestQueueForMethod(domain, object, requestedMethod, requestInfo, method)) {
            failure.transportStatus = 404;
            throw std::runtime_error("rpc: method not found");
        }
        auto types = parseArgTypesFromTypeName(requestInfo.typeName.toStdString());
        if (types.size() < 3 || args.size() != types.size() - 3)
            throw std::runtime_error("rpc: argument count does not match the IPC signature");
        const uint64_t id = sw::ipc::nextRpcCallId();
        const uint32_t pid = sw::ipc::detail::currentPid();
        std::array<uint8_t, RpcQueueAccess::kMaxPayload> bytes;
        sw::ipc::detail::Encoder enc(bytes.data(), bytes.size());
        if (!sw::ipc::detail::Codec<uint64_t>::write(enc, id) ||
            !sw::ipc::detail::Codec<uint32_t>::write(enc, pid) ||
            !sw::ipc::detail::Codec<SwString>::write(enc, clientInfo))
            throw std::runtime_error("rpc: request header exceeds IPC capacity");
        SwString error;
        for (size_t i = 0; i < args.size(); ++i)
            if (!encodeJsonArg(enc, types[i + 3], args[i], error)) throw std::runtime_error(error.toStdString());
        RpcQueueAccess request;
        failure.transportStatus = 500;
        if (!openRpcQueueAccess(requestInfo, rpcQueueCapacityFromQueueMethod(method), request, error))
            throw std::runtime_error(error.toStdString());
        const SwString key = target + "#" + method;
        auto& channel = channels_[key];
        channel.domain = domain; channel.object = object; channel.method = method;
        channel.responseSignal = "__rpc_ret__|" + method + "|" + SwString::number(pid);
        channel.pending.emplace(id, Pending{callback, std::chrono::steady_clock::now() +
            std::chrono::milliseconds(std::max(1, std::min(timeoutMs, 60000)))});
        if (!rpcQueuePushRaw(request, bytes.data(), enc.size(), error)) {
            channel.pending.erase(id);
            if (channel.pending.empty()) channels_.erase(key);
            throw std::runtime_error(error.toStdString());
        }
        sw::ipc::detail::RpcResponseResources::own(sw::ipc::detail::make_shm_name(domain, object, channel.responseSignal));
        if (!timer_->isActive()) timer_->start();
        return id;
    } catch (const std::exception& error) {
        failure.error = error.what();
    }
    callback(failure);
    return 0;
}

void SwBridgeRpcClient::cancel(uint64_t id) {
    for (auto& item : channels_) item.second.pending.erase(id);
}

void SwBridgeRpcClient::poll() {
    // Defer callbacks until after iteration: callbacks may submit/cancel calls.
    std::vector<std::pair<Callback, Result>> completed;
    const auto now = std::chrono::steady_clock::now();
    for (auto it = channels_.begin(); it != channels_.end();) {
        auto& channel = it->second;
        SwString channelError;
        try {
            if (!channel.response.map && !channel.pending.empty()) {
                RpcQueueInfo info;
                if (findSignalInRegistryForTarget(channel.domain, channel.object, channel.responseSignal, info)) {
                    if (openRpcQueueAccess(info, rpcQueueCapacityFromQueueMethod(channel.method), channel.response, channelError)) {
                        channel.responseTypes = parseArgTypesFromTypeName(info.typeName.toStdString());
                        sw::ipc::detail::RpcResponseResources::own(info.shmName);
                    }
                }
            }
            std::vector<uint8_t> bytes;
            for (int budget = 0; budget < 256 && channel.response.map && rpcQueuePopOneRaw(channel.response, bytes); ++budget) {
                sw::ipc::detail::Decoder dec(bytes.data(), bytes.size());
                Result result;
                if (!sw::ipc::detail::Codec<uint64_t>::read(dec, result.callId) ||
                    !sw::ipc::detail::Codec<bool>::read(dec, result.ok) ||
                    !sw::ipc::detail::Codec<SwString>::read(dec, result.error)) continue;
                auto pending = channel.pending.find(result.callId);
                if (pending == channel.pending.end()) continue; // Late reply to an expired/cancelled call.
                result.method = channel.method;
                result.hasReturn = channel.responseTypes.size() == 4;
                if (result.hasReturn) result.returnType = channel.responseTypes[3];
                if (result.ok && (channel.responseTypes.size() < 3 || channel.responseTypes.size() > 4)) {
                    result.ok = false; result.error = "rpc: invalid response signature";
                }
                if (result.ok && result.hasReturn &&
                    !decodeJsonValueByType(dec, result.returnType.toStdString(), result.value, result.error)) {
                    result.ok = false; result.transportStatus = 500;
                }
                completed.emplace_back(std::move(pending->second.callback), std::move(result));
                channel.pending.erase(pending);
            }
        } catch (const std::exception& error) { channelError = error.what(); }
        for (auto pending = channel.pending.begin(); pending != channel.pending.end();) {
            if (!channelError.isEmpty() || now >= pending->second.deadline) {
                Result result;
                result.callId = pending->first; result.method = channel.method;
                result.transportStatus = channelError.isEmpty() ? 504 : 500;
                result.error = channelError.isEmpty() ? SwString("rpc: timeout") : channelError;
                completed.emplace_back(std::move(pending->second.callback), std::move(result));
                pending = channel.pending.erase(pending);
            } else ++pending;
        }
        if (channel.pending.empty()) it = channels_.erase(it); else ++it;
    }
    if (channels_.empty()) timer_->stop();
    for (const auto& item : completed) item.first(item.second);
}
