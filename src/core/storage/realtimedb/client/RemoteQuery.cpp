#include "RemoteQuery.hpp"
#include "../SwRealtimeDbJson.h"
#include <core/remote/SwProxyObject.h>
#include <core/remote/SwJsonRpcTransfer.h>
#include <algorithm>
#include <chrono>
#include <cmath>

SW_PROXY_OBJECT_CLASS_BEGIN(SwRealtimeDbProxy)
    SW_PROXY_OBJECT_RPC(SwString, query, SwString)
    SW_PROXY_OBJECT_RPC(SwString, readResult, SwString, int)
SW_PROXY_OBJECT_CLASS_END()

namespace swRealtimeDbDetail {
struct RemoteQuery::State {
    State(const SwString& domain, const SwString& endpoint, const SwString& actor)
        : proxy(domain, endpoint, actor) {}
    SwRealtimeDbProxy proxy;
};

struct RemoteQuery::Exchange : std::enable_shared_from_this<Exchange> {
    using Clock = std::chrono::steady_clock;
    std::weak_ptr<State> state;
    SwRealtimeDbCompletion complete;
    std::unique_ptr<sw::ipc::JsonRequestUpload> upload;
    Clock::time_point deadline;
    SwString token, contents;
    std::size_t bytes{0};
    std::uint64_t queryId{0}, readId{0};
    bool done{false};

    int remaining() const {
        return static_cast<int>(std::max<std::int64_t>(0,
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()));
    }
    void abortUpload(const std::shared_ptr<State>& owner) {
        if (!upload || upload->abortPacket().isEmpty()) return;
        owner->proxy.queryRpc().callAsyncResult(upload->abortPacket(),
            [](const sw::ipc::RpcResult<SwString>&) {}, 250);
    }
    void finish(SwRealtimeDbReply reply) {
        if (done) return;
        done = true;
        const auto owner = state.lock();
        if (!owner) return;
        if (!reply.ok) abortUpload(owner);
        auto callback = std::move(complete);
        if (callback) callback(std::move(reply));
    }
    void cancel() {
        if (done) return;
        done = true;
        complete = {};
        if (const auto owner = state.lock()) {
            if (queryId) owner->proxy.queryRpc().cancel(queryId);
            if (readId) owner->proxy.readResultRpc().cancel(readId);
            abortUpload(owner);
        }
    }
    void decode(const SwString& encoded, bool allowPaging) {
        const auto response = parseObject(encoded);
        if (!response["ok"].isBool()) throw std::runtime_error("Invalid RtDb response");
        if (!response["ok"].toBool()) { finish({false, {}, response["error"].toString()}); return; }
        if (response["data"].isObject()) { finish({true, response["data"].toObject(), {}}); return; }
        const double length = response["bytes"].toDouble();
        if (!allowPaging || !response["result_id"].isString() ||
            !(response["bytes"].isDouble() || response["bytes"].isInt()) ||
            !std::isfinite(length) || length < 1 || length > 2 * 1024 * 1024 || std::floor(length) != length)
            throw std::runtime_error("Invalid RtDb result descriptor");
        token = response["result_id"].toString();
        if (token.isEmpty()) throw std::runtime_error("Empty RtDb result token");
        bytes = static_cast<std::size_t>(length);
        contents.reserve(bytes);
        fetch();
    }
    void send(const SwString& packet) {
        const auto owner = state.lock();
        if (!owner || done) return;
        if (!remaining()) { finish({false, {}, "RtDb request timed out; outcome may be unknown"}); return; }
        const std::weak_ptr<Exchange> weak = shared_from_this();
        queryId = owner->proxy.queryRpc().callAsyncResult(packet,
            [weak](const sw::ipc::RpcResult<SwString>& reply) {
                const auto self = weak.lock();
                if (!self || self->done || self->state.expired()) return;
                self->queryId = 0;
                if (!reply.ok) { self->finish({false, {}, reply.error}); return; }
                try {
                    SwString next;
                    if (self->upload->advance(reply.value, next)) self->send(next);
                    else self->decode(reply.value, true);
                } catch (const std::exception& error) { self->finish({false, {}, error.what()}); }
            }, remaining());
    }
    void fetch() {
        const auto owner = state.lock();
        if (!owner || done) return;
        if (!remaining()) { finish({false, {}, "RtDb result download timed out"}); return; }
        const std::weak_ptr<Exchange> weak = shared_from_this();
        readId = owner->proxy.readResultRpc().callAsyncResult(token, static_cast<int>(contents.size()),
            [weak](const sw::ipc::RpcResult<SwString>& reply) {
                const auto self = weak.lock();
                if (!self || self->done || self->state.expired()) return;
                self->readId = 0;
                if (!reply.ok) { self->finish({false, {}, reply.error}); return; }
                try {
                    if (reply.value.isEmpty() || reply.value.size() > self->bytes - self->contents.size())
                        throw std::runtime_error("Invalid RtDb result chunk");
                    self->contents += reply.value;
                    if (self->contents.size() == self->bytes) self->decode(self->contents, false);
                    else self->fetch();
                } catch (const std::exception& error) { self->finish({false, {}, error.what()}); }
            }, remaining());
    }
};

RemoteQuery::RemoteQuery(const SwString& domain, const SwString& endpoint, const SwString& actor)
    : state_(std::make_shared<State>(domain, endpoint, actor)) {}
RemoteQuery::~RemoteQuery() = default;
std::function<void()> RemoteQuery::request(const SwJsonObject& request,
                                          SwRealtimeDbCompletion complete, int timeoutMs) {
    const auto exchange = std::make_shared<Exchange>();
    exchange->state = state_;
    exchange->complete = std::move(complete);
    exchange->deadline = Exchange::Clock::now() + std::chrono::milliseconds(timeoutMs);
    try {
        exchange->upload = std::make_unique<sw::ipc::JsonRequestUpload>(json(request));
        exchange->send(exchange->upload->first());
    } catch (const std::exception& error) { exchange->finish({false, {}, error.what()}); }
    return [exchange] { exchange->cancel(); };
}
}
