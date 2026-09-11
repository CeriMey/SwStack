#include <core/storage/SwRealtimeDbClient.h>
#include <core/storage/realtimedb/ChangeNotice.hpp>
#include <core/remote/SwIpcRpc.h>
#include "RemoteQuery.hpp"
#include "../JsonSize.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <cstdlib>
#include <map>
#include <thread>

namespace {
using Transport = sw::ipc::RpcMethodClient<SwRealtimeDbReply, SwJsonObject>;
void removePriority(SwJsonObject& request, bool* high = nullptr) {
    if (!request.contains("priority")) return;
    const auto& value = static_cast<const SwJsonObject&>(request)["priority"];
    if (!value.isString() || (value.toString() != "normal" && value.toString() != "high"))
        throw std::invalid_argument("priority must be normal or high");
    if (high) *high = value.toString() == "high";
    request.remove("priority");
}
}

struct SwRealtimeDbClient::State : std::enable_shared_from_this<State> {
    State(const SwString& domain, const SwString& endpoint, const SwString& actor)
        : domain(domain), endpoint(endpoint), actor(actor) {}
    SwString domain, endpoint, actor;
    std::unique_ptr<Transport> transport;
    std::unique_ptr<swRealtimeDbDetail::RemoteQuery> remote;
    std::unique_ptr<sw::ipc::Registry> registry;
    std::unique_ptr<sw::ipc::SwIpcSignal<swRealtimeDbDetail::ChangeNotice>> signal;
    sw::ipc::SwIpcSignal<swRealtimeDbDetail::ChangeNotice>::Subscription subscription;
    std::function<void(const SwJsonObject&)> changed;
    std::function<void(const swRealtimeDbDetail::ChangeBatch&)> nativeChanged;
    bool profile{std::getenv("SW_RTDB_PROFILE") != nullptr};
    bool closed{false}, cancelling{false}, pumping{false}, largeActive{false};
    std::size_t active{0};
    unsigned highRun{0};
    unsigned directDepth{0};
    bool directExecuting{false};
    std::uint64_t nextId{0};
    std::map<std::uint64_t, std::shared_ptr<Operation>> calls;
    std::deque<std::shared_ptr<Operation>> waiting;

    void initialize() {
        const std::weak_ptr<State> weak = shared_from_this();
        transport = std::make_unique<Transport>(domain, endpoint, "query", actor,
            [weak](const SwJsonObject& request, Transport::Completion complete, int timeoutMs)
                -> Transport::RemoteCancel {
                const auto owner = weak.lock();
                if (!owner || owner->closed) return {};
                if (!owner->remote) owner->remote = std::make_unique<swRealtimeDbDetail::RemoteQuery>(
                    owner->domain, owner->endpoint, owner->actor);
                return owner->remote->request(request, [complete](SwRealtimeDbReply reply) {
                    Transport::Result result; result.ok = true; result.value = std::move(reply);
                    complete(result);
                }, timeoutMs);
            });
    }
    void connectChanged() {
        if (closed || (!changed && !nativeChanged)) {
            subscription.stop(); signal.reset(); registry.reset();
            return;
        }
        if (signal) return;
        registry = std::make_unique<sw::ipc::Registry>(domain, endpoint);
        signal = std::make_unique<sw::ipc::SwIpcSignal<swRealtimeDbDetail::ChangeNotice>>(
            *registry, "changed", 1u, swRealtimeDbDetail::changeNoticeCapacity, sw::ipc::DeliveryMode::LatestOnly);
        const std::weak_ptr<State> weak = shared_from_this();
        subscription = signal->connect([weak](swRealtimeDbDetail::ChangeNotice notice) {
            const auto state = weak.lock();
            if (!state || state->closed || !notice.batch()) return;
            const auto nativeCallback = state->nativeChanged;
            const auto callback = state->changed;
            try {
                if (nativeCallback) nativeCallback(*notice.batch());
                else if (callback) callback(notice.batch()->detachPacket());
            } catch (const std::exception& error) {
                swCWarning("sw.core.storage.realtimedb.client") << "Database notification callback: " << error.what();
            } catch (...) {
                swCWarning("sw.core.storage.realtimedb.client") << "Database notification callback failed";
            }
        }, false);
    }
    void pump();
    void cancel();
};

struct SwRealtimeDbClient::Operation : std::enable_shared_from_this<Operation> {
    using Clock = std::chrono::steady_clock;
    std::weak_ptr<State> state;
    SwRealtimeDbCompletion complete;
    std::optional<Transport::PreparedCall> prepared;
    Clock::time_point deadline, queuedAt{Clock::now()}, sentAt{};
    SwString description;
    std::uint64_t id{0}, rpcId{0};
    bool done{false}, active{false}, large{false}, high{false};
    std::function<void()> timerCancel;

    int remaining() const {
        return static_cast<int>(std::max<std::int64_t>(0,
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()));
    }
    void finish(SwRealtimeDbReply reply) {
        if (done) return;
        done = true;
        const auto owner = state.lock();
        if (!owner) return;
        auto stopTimer = std::move(timerCancel);
        if (stopTimer) stopTimer();
        owner->calls.erase(id);
        owner->waiting.erase(std::remove(owner->waiting.begin(), owner->waiting.end(), shared_from_this()),
                             owner->waiting.end());
        if (active) { --owner->active; if (large) owner->largeActive = false; }
        if (rpcId) owner->transport->cancel(rpcId);
        if (owner->profile) {
            const auto total = std::chrono::duration<double, std::milli>(Clock::now() - queuedAt).count();
            if (total > 10) swCDebug("sw.core.storage.realtimedb.client.profile") << owner->actor << " " << description
                << " total_ms=" << total << " queued_ms="
                << (active ? std::chrono::duration<double, std::milli>(sentAt - queuedAt).count() : total);
        }
        auto callback = std::move(complete);
        if (!owner->closed && callback) {
            try { callback(std::move(reply)); }
            catch (const std::exception& error) {
                swCWarning("sw.core.storage.realtimedb.client") << "Request completion callback: " << error.what();
            } catch (...) {
                swCWarning("sw.core.storage.realtimedb.client") << "Request completion callback failed";
            }
        }
        owner->pump();
    }
};

void SwRealtimeDbClient::State::pump() {
    if (closed || pumping || cancelling) return;
    pumping = true;
    // Bound whole requests, including remote paged downloads. One large request
    // leaves capacity for small control calls; priorities cannot starve peers.
    while (!closed && active < 2 && !waiting.empty()) {
        const auto eligible = [this](const auto& call) { return !call->large || !largeActive; };
        const bool preferHigh = highRun < 4;
        auto next = std::find_if(waiting.begin(), waiting.end(), [&](const auto& call) {
            return eligible(call) && call->high == preferHigh;
        });
        if (next == waiting.end()) next = std::find_if(waiting.begin(), waiting.end(), eligible);
        if (next == waiting.end()) break;
        const auto call = *next;
        waiting.erase(next);
        if (call->done) continue;
        const int timeout = call->remaining();
        if (!timeout) { call->finish({false, {}, "RtDb request timed out before execution"}); continue; }
        highRun = call->high ? std::min(highRun + 1, 4u) : 0;
        call->active = true; call->sentAt = Operation::Clock::now(); ++active;
        if (call->large) largeActive = true;
        const std::weak_ptr<Operation> weak = call;
        call->rpcId = transport->callAsyncResult(std::move(*call->prepared), [weak](const Transport::Result& result) {
            const auto call = weak.lock();
            if (!call || call->done) return;
            call->rpcId = 0;
            call->finish(result.ok ? result.value : SwRealtimeDbReply{false, {}, result.error});
        }, timeout);
    }
    pumping = false;
}
void SwRealtimeDbClient::State::cancel() {
    if (cancelling) return;
    cancelling = true;
    const auto snapshot = calls;
    for (const auto& entry : snapshot) entry.second->finish({false, {}, "RtDb request cancelled"});
    cancelling = false;
}

SwRealtimeDbClient::SwRealtimeDbClient(const SwString& domain, const SwString& actor, const SwString& endpoint)
    : state_(std::make_shared<State>(domain, endpoint, actor)) { state_->initialize(); }
SwRealtimeDbClient::~SwRealtimeDbClient() {
    const auto state = std::move(state_);
    state->closed = true;
    state->subscription.stop();
    state->cancel();
}
void SwRealtimeDbClient::cancelAll() { const auto state = state_; state->cancel(); }
void SwRealtimeDbClient::request(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs) {
    requestImpl(request, std::move(complete), timeoutMs, nullptr);
}
void SwRealtimeDbClient::requestPreferDirect(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs) {
    requestPreferredImpl(request,std::move(complete),timeoutMs,nullptr);
}
void SwRealtimeDbClient::requestOwnedPreferDirect(SwJsonObject&& request, SwRealtimeDbCompletion complete, int timeoutMs) {
    requestPreferredImpl(request,std::move(complete),timeoutMs,&request);
}
void SwRealtimeDbClient::requestPreferredImpl(const SwJsonObject& request, SwRealtimeDbCompletion complete,
                                             int timeoutMs, SwJsonObject* owned) {
    const auto state=state_;
    if(timeoutMs>0 && state->directDepth<16 && !state->pumping && state->calls.empty() &&
       !state->directExecuting && state->transport->canCallDirect()) {
        ++state->directDepth;
        auto reply=requestImmediate(request);
        if(reply) {
            try {if(!state->closed && complete)complete(std::move(*reply));}
            catch(const std::exception& error){swCWarning("sw.core.storage.realtimedb.client")<<"Request completion callback: "<<error.what();}
            catch(...){swCWarning("sw.core.storage.realtimedb.client")<<"Request completion callback failed";}
            --state->directDepth;return;
        }
        --state->directDepth;
    }
    requestImpl(request,std::move(complete),timeoutMs,owned);
}
void SwRealtimeDbClient::requestOwned(SwJsonObject&& request, SwRealtimeDbCompletion complete, int timeoutMs) {
    requestImpl(request, std::move(complete), timeoutMs, &request);
}
void SwRealtimeDbClient::requestImpl(const SwJsonObject& request, SwRealtimeDbCompletion complete, int timeoutMs,
                                   SwJsonObject* owned) {
    const auto state = state_;
    if (state->closed || state->cancelling) {
        if (complete) complete({false, {}, "RtDb request cancelled"});
        return;
    }
    if (timeoutMs <= 0 || state->calls.size() >= 64) {
        if (complete) complete({false, {}, "Invalid timeout or RtDb request queue full"});
        return;
    }
    const auto call = std::make_shared<Operation>();
    call->state = state; call->id = ++state->nextId;
    call->complete = std::move(complete);
    call->deadline = Operation::Clock::now() + std::chrono::milliseconds(timeoutMs);
    state->calls.emplace(call->id, call);
    try {
        call->large = swRealtimeDbDetail::JsonSize(1024 * 1024, 32).object(request) > 3000;
        if (state->profile) call->description = request["op"].toString() + " " + request["table"].toString();
        auto payload = owned ? SwJsonObject(std::move(*owned)) : SwJsonObject(request);
        removePriority(payload, &call->high);
        // Pin the common route at admission, including for requests waiting
        // behind the concurrency limit. A restart must not retarget a mutation.
        call->prepared.emplace(state->transport->prepare(std::move(payload)));
        const std::weak_ptr<Operation> weak = call;
        call->timerCancel = sw::ipc::detail::startNativeSignalTimer(std::this_thread::get_id(), [weak] {
            if (const auto call = weak.lock()) call->finish({false, {}, "RtDb request timed out"});
        }, static_cast<int64_t>(timeoutMs) * 1000);
        if (!call->timerCancel) throw std::runtime_error("RtDb caller timer runtime unavailable");
        state->waiting.push_back(call);
        state->pump();
    } catch (const std::exception& error) { call->finish({false, {}, error.what()}); }
}
void SwRealtimeDbClient::setChangedHandler(std::function<void(const SwJsonObject&)> handler) {
    state_->nativeChanged = {}; state_->changed = std::move(handler); state_->connectChanged();
}
void SwRealtimeDbClient::setNativeChangedHandler(std::function<void(const swRealtimeDbDetail::ChangeBatch&)> handler) {
    state_->changed = {}; state_->nativeChanged = std::move(handler); state_->connectChanged();
}
bool SwRealtimeDbClient::canRequestDirect() const { return state_->transport->canCallDirect(); }
std::optional<SwRealtimeDbReply> SwRealtimeDbClient::requestImmediate(const SwJsonObject& request) {
    const auto state = state_;
    if (state->closed || state->cancelling) return SwRealtimeDbReply{false, {}, "RtDb request cancelled"};
    if (state->directExecuting) return std::nullopt;
    struct Guard {bool& busy;Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(state->directExecuting);
    try {
        swRealtimeDbDetail::JsonSize(1024 * 1024, 32).object(request);
        auto payload = request;
        removePriority(payload);
        Transport::Result result;
        if (!state->transport->tryCallDirect(result, payload)) return std::nullopt;
        return result.ok ? result.value : SwRealtimeDbReply{false, {}, result.error};
    } catch (const std::exception& error) { return SwRealtimeDbReply{false, {}, error.what()}; }
}
