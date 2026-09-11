#pragma once

/**
 * @file src/core/remote/SwIpcRpc.h
 * @ingroup core_remote
 * @brief Declares the public interface exposed by SwIpcRpc in the CoreSw remote and IPC layer.
 *
 * This header belongs to the CoreSw remote and IPC layer. It provides the abstractions used to
 * expose objects across process boundaries and to transport data or signals between peers.
 *
 * Within that layer, this file focuses on the IPC rpc interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Remote-facing declarations in this area usually coordinate identity, proxying, serialization,
 * and synchronization across runtimes.
 *
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwEventLoop.h"
#include "SwSharedMemorySignal.h"
#include "SwIpcRpcNative.h"
#include "SwString.h"
#include "SwTimer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include "SwMutex.h"
#include <thread>

namespace sw {
namespace ipc {

class RpcSpinMutex_ {
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

inline uint32_t normalizeRpcQueueCapacity(uint32_t requested) {
    if (requested <= 10u) return 10u;
    if (requested <= 25u) return 25u;
    if (requested <= 50u) return 50u;
    if (requested <= 100u) return 100u;
    if (requested <= 200u) return 200u;
    if (requested <= 500u) return 500u;
    return 1000u;
}

inline std::atomic<uint32_t>& rpcQueueCapacityStorage_() {
    static std::atomic<uint32_t> cap(1000u);
    return cap;
}

inline uint32_t rpcQueueCapacity() {
    return rpcQueueCapacityStorage_().load(std::memory_order_acquire);
}

inline void setRpcQueueCapacity(uint32_t capacity) {
    const uint32_t normalized = normalizeRpcQueueCapacity(capacity == 0u ? 1u : capacity);
    rpcQueueCapacityStorage_().store(normalized, std::memory_order_release);
}

inline SwString rpcQueueCapacityTag() {
    return SwString::number(static_cast<int>(rpcQueueCapacity()));
}

inline SwString rpcRequestQueueName(const SwString& methodName) {
    return SwString("__rpc__|") + rpcQueueCapacityTag() + "|" + methodName;
}

inline SwString rpcResponseQueueName(const SwString& methodName, uint32_t clientPid) {
    return SwString("__rpc_ret__|") + rpcQueueCapacityTag() + "|" + methodName + "|" +
           SwString::number(static_cast<int>(clientPid));
}

template <typename Ret, typename... Args>
class RpcMethodClient {
public:
    using Result = RpcResult<Ret>;
    using Completion = std::function<void(const Result&)>;
    using Traits = RpcResultTraits<Ret>;
    using Router = typename Traits::Router;
    using Native = NativeRpcEndpoint<Ret, Args...>;
    using RemoteCancel = std::function<void()>;
    using RemoteInvoker = std::function<RemoteCancel(const Args&..., Completion, int)>;

    class PreparedCall {
    public:
        PreparedCall(PreparedCall&&) = default;
        PreparedCall& operator=(PreparedCall&&) = default;
        PreparedCall(const PreparedCall&) = delete;
        PreparedCall& operator=(const PreparedCall&) = delete;
    private:
        friend class RpcMethodClient;
        PreparedCall(std::shared_ptr<typename Native::Channel> channel, typename Native::Selection endpoint,
                     std::shared_ptr<typename Native::Tuple> values)
            : channel_(std::move(channel)), endpoint_(std::move(endpoint)), values_(std::move(values)) {}
        std::shared_ptr<typename Native::Channel> channel_;
        typename Native::Selection endpoint_;
        std::shared_ptr<typename Native::Tuple> values_;
    };
    // Admission may precede execution in a bounded application queue. Capture
    // the selected service now, so a queued mutation cannot cross a restart.
    PreparedCall prepare(Args... args) const {
        return PreparedCall(native_, Native::select(native_),
                            std::make_shared<typename Native::Tuple>(std::move(args)...));
    }

    RpcMethodClient(const SwString& domain, const SwString& object, const SwString& method,
                    const SwString& clientInfo = SwString())
        : RpcMethodClient(domain, object, method, clientInfo, RemoteInvoker()) {
        router_ = Router::get(domain, object, rpcResponseQueueName(method, pid_), rpcQueueCapacity());
        switch (rpcQueueCapacity()) {
#define SW_RPC_REQUEST_CAP(N) case N: initRequest<N>(); break
            SW_RPC_REQUEST_CAP(10); SW_RPC_REQUEST_CAP(25); SW_RPC_REQUEST_CAP(50);
            SW_RPC_REQUEST_CAP(100); SW_RPC_REQUEST_CAP(200); SW_RPC_REQUEST_CAP(500);
            SW_RPC_REQUEST_CAP(1000);
#undef SW_RPC_REQUEST_CAP
            default: throw std::invalid_argument("unsupported RPC capacity");
        }
    }
    // This overload does not instantiate fixed-packet codecs. The supplied
    // adapter runs only when no native endpoint existed at submission time.
    RpcMethodClient(const SwString& domain, const SwString& object, const SwString& method,
                    const SwString& clientInfo, RemoteInvoker remote)
        : registry_(domain, object), method_(method), clientInfo_(clientInfo),
          state_(std::make_shared<State>()), pid_(detail::currentPid()),
          native_(Native::channel(domain, object, method)), remote_(std::move(remote)) {
        detail::registerNativeSignalThread();
    }
    ~RpcMethodClient() {
        std::vector<RemoteCancel> cancels;
        {
            SwMutexLocker lock(state_->mutex);
            state_->closed.store(true, std::memory_order_release);
            for (const auto& entry : state_->pending) {
                if (router_) router_->remove(entry.first);
                stopTimer(entry.second);
                entry.second->cancelRequested = true;
                if (entry.second->remoteCancel) cancels.push_back(std::move(entry.second->remoteCancel));
            }
            state_->pending.clear();
        }
        for (const auto& cancel : cancels) cancel();
    }
    RpcMethodClient(const RpcMethodClient&) = delete;
    RpcMethodClient& operator=(const RpcMethodClient&) = delete;

    SwString lastError() const {
        SwMutexLocker lock(state_->mutex);
        return state_->lastError;
    }
    size_t pendingCount() const {
        SwMutexLocker lock(state_->mutex);
        return state_->pending.size();
    }
    bool canCallDirect() const { return Native::isDirect(Native::select(native_)); }
    bool tryCallDirect(Result& out, const Args&... args) {
        const auto endpoint = Native::select(native_);
        if (!Native::isDirect(endpoint)) return false;
        const auto state = state_;
        RpcContext context; context.clientPid = pid_; context.clientInfo = clientInfo_;
        out = Native::invoke(endpoint, context, typename Native::Tuple(args...));
        { SwMutexLocker lock(state->mutex); state->lastError = out.error; }
        return true;
    }
    Result callResult(const Args&... args, int timeoutMs = 2000) {
        auto state = state_; auto response = router_;
        auto pending = begin(Completion(), timeoutMs, false, prepare(args...));
        if (!pending->done.load(std::memory_order_acquire)) {
            SwEventLoop::waitUntil([pending]() {
                detail::LoopPoller::instance().dispatch();
                return pending->done.load(std::memory_order_acquire);
            }, timeoutMs);
        }
        if (!pending->done.load(std::memory_order_acquire)) {
            Result result; result.error = "rpc: timeout";
            finish(state, response, pending, result, true);
        }
        return pending->result;
    }
    typename Traits::Return call(const Args&... args, int timeoutMs = 2000) {
        return Traits::value(callResult(args..., timeoutMs));
    }

    // Execution and completion are deferred, including on the same thread.
    // This gives the caller a cancellable ID before a handler can execute.
    uint64_t callAsyncResult(const Args&... args, Completion complete, int timeoutMs = 2000) {
        return callAsyncResult(prepare(args...), std::move(complete), timeoutMs);
    }
    uint64_t callAsyncResult(PreparedCall prepared, Completion complete, int timeoutMs = 2000) {
        auto state = state_;
        auto response = router_;
        auto pending = begin(std::move(complete), timeoutMs, true, std::move(prepared));
        if (timeoutMs > 0 && !pending->done.load(std::memory_order_acquire)) {
            std::weak_ptr<State> weak = state;
            auto timer = detail::startNativeSignalTimer(pending->caller, [weak, response, pending]() {
                if (auto state = weak.lock()) {
                    Result result; result.error = "rpc: timeout";
                    deliver(state, response, pending, result, true);
                }
            }, static_cast<int64_t>(timeoutMs) * 1000);
            if (!timer) {
                Result result; result.error = "rpc: caller timer runtime unavailable";
                finish(state, response, pending, result, true);
            } else {
                SwMutexLocker lock(state->mutex);
                if (state->closed.load() || pending->done.load()) timer();
                else pending->timerCancel = std::move(timer);
            }
        }
        return pending->id;
    }
    template<class R = Ret>
    typename std::enable_if<!std::is_void<R>::value, void>::type
    callAsync(const Args&... args, typename RpcResultTraits<R>::Success onOk, int timeoutMs = 2000) {
        callAsyncResult(args..., [onOk](const Result& r) { if (r.ok && onOk) onOk(r.value); }, timeoutMs);
    }
    template<class R = Ret>
    typename std::enable_if<std::is_void<R>::value, void>::type
    callAsync(const Args&... args, std::function<void(bool)> onDone, int timeoutMs = 2000) {
        callAsyncResult(args..., [onDone](const Result& r) { if (onDone) onDone(r.ok); }, timeoutMs);
    }
    bool cancel(uint64_t id) {
        std::shared_ptr<Pending> pending;
        {
            SwMutexLocker lock(state_->mutex);
            auto it = state_->pending.find(id);
            if (it == state_->pending.end() || it->second->cancelRequested.load()) return false;
            pending = it->second;
            pending->cancelRequested.store(true, std::memory_order_release);
        }
        Result result; result.error = "rpc: cancelled";
        deliver(state_, router_, pending, result, true);
        return true;
    }
    void cancelAll() {
        auto state = state_; auto response = router_;
        std::vector<std::shared_ptr<Pending>> pending;
        { SwMutexLocker lock(state->mutex);
          for (const auto& entry : state->pending) {
              entry.second->cancelRequested.store(true, std::memory_order_release);
              pending.push_back(entry.second);
          } }
        Result result; result.error = "rpc: cancelled";
        for (const auto& call : pending) deliver(state, response, call, result, true);
    }
private:
    using Clock = std::chrono::steady_clock;
    struct Pending {
        uint64_t id{0};
        std::atomic_bool done{false};
        Result result;
        Completion complete;
        RemoteCancel timerCancel;
        bool asynchronous{false};
        std::atomic_bool cancelRequested{false};
        std::thread::id caller;
        Clock::time_point deadline{Clock::time_point::max()};
        RemoteCancel remoteCancel;
    };
    struct State {
        SwMutex mutex;
        std::atomic_bool closed{false};
        SwString lastError;
        std::map<uint64_t, std::shared_ptr<Pending>> pending;
    };
    static void stopTimer(const std::shared_ptr<Pending>& pending) {
        auto cancel = std::move(pending->timerCancel);
        if (cancel) cancel();
    }
    static bool finish(const std::shared_ptr<State>& state, const std::shared_ptr<Router>& response,
                       const std::shared_ptr<Pending>& pending, const Result& result,
                       bool cancelRemote = false, bool suppressCallback = false) {
        Completion complete;
        RemoteCancel cancel;
        Result expired;
        const Result* admitted = &result;
        {
            SwMutexLocker lock(state->mutex);
            if (state->closed.load() || pending->done.load(std::memory_order_acquire) ||
                (pending->cancelRequested.load(std::memory_order_acquire) && !cancelRemote)) return false;
            // A handler can block the caller's timer, or a response can wait in
            // its queue past the deadline. Admission on the caller thread must
            // enforce the deadline independently of timer callback dispatch.
            if (!cancelRemote && Clock::now() >= pending->deadline) {
                expired.error = "rpc: timeout";
                admitted = &expired;
                cancelRemote = true;
            }
            if (response) response->remove(pending->id);
            stopTimer(pending);
            state->pending.erase(pending->id);
            pending->result = *admitted;
            state->lastError = admitted->error;
            complete = std::move(pending->complete);
            pending->cancelRequested = cancelRemote;
            if (cancelRemote) cancel = std::move(pending->remoteCancel);
            else pending->remoteCancel = {};
            pending->done.store(true, std::memory_order_release);
        }
        if (cancel) cancel();
        if (complete && !suppressCallback) complete(*admitted);
        return true;
    }
    static void deliver(const std::shared_ptr<State>& state, const std::shared_ptr<Router>& response,
                        const std::shared_ptr<Pending>& pending, Result result, bool cancelRemote = false) {
        if (state->closed.load() || pending->done.load()) return;
        if (!pending->asynchronous || pending->caller == std::this_thread::get_id()) {
            finish(state, response, pending, result, cancelRemote); return;
        }
        if (!detail::postNativeSignalThread(pending->caller, [state, response, pending, result, cancelRemote] {
            finish(state, response, pending, result, cancelRemote);
        })) {
            // The caller runtime has stopped. Release the call without running
            // arbitrary application code on the server's thread.
            result.ok = false; result.error = "rpc: caller thread unavailable";
            finish(state, response, pending, result, true, true);
        }
    }
    static bool current(const std::shared_ptr<State>& state, const std::shared_ptr<Router>& response,
                        const std::shared_ptr<Pending>& pending) {
        if (state->closed.load() || pending->done.load()) return false;
        if (pending->cancelRequested.load(std::memory_order_acquire)) {
            Result result; result.error = "rpc: cancelled";
            deliver(state, response, pending, result, true);
            return false;
        }
        if (Clock::now() < pending->deadline) return true;
        Result result; result.error = "rpc: timeout";
        deliver(state, response, pending, result, true);
        return false;
    }
    template<size_t... I>
    static RemoteCancel invokeRemote(const RemoteInvoker& remote, const typename Native::Tuple& args,
                                     Completion complete, int timeoutMs, detail::index_sequence<I...>) {
        return remote(std::get<I>(args)..., std::move(complete), timeoutMs);
    }
    template<size_t... I>
    static bool invokeRequest(const std::function<bool(uint64_t, uint32_t, const SwString&, const Args&...)>& request,
                              const typename Native::Tuple& args, uint64_t id, uint32_t pid,
                              const SwString& info, detail::index_sequence<I...>) {
        return request(id, pid, info, std::get<I>(args)...);
    }
    std::shared_ptr<Pending> begin(Completion complete, int timeoutMs, bool asynchronous, PreparedCall prepared) {
        detail::registerNativeSignalThread();
        const auto state = state_;
        const auto response = router_;
        const auto remote = remote_;
        const auto request = request_;
        const bool validPrepared = prepared.channel_ == native_ && static_cast<bool>(prepared.values_);
        const auto endpoint = std::move(prepared.endpoint_);
        RpcContext context; context.clientPid = pid_; context.clientInfo = clientInfo_;
        auto values = std::move(prepared.values_);
        auto pending = std::make_shared<Pending>();
        pending->id = nextRpcCallId();
        pending->complete = std::move(complete);
        pending->asynchronous = asynchronous;
        pending->caller = std::this_thread::get_id();
        if (timeoutMs > 0) pending->deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        {
            SwMutexLocker lock(state->mutex);
            state->lastError.clear();
            state->pending[pending->id] = pending;
        }
        Completion done = [state, response, pending](const Result& result) {
            deliver(state, response, pending, result);
        };
        const auto live = [state, response, pending] { return current(state, response, pending); };
        auto start = [state, response, remote, request, endpoint, context, values, pending, done, live, validPrepared]() {
            if (!live()) return;
            if (!validPrepared) { Result result; result.error = "rpc: invalid prepared call"; done(result); return; }
            if (endpoint) { Native::dispatch(endpoint, context, values, done, live); return; }
            try {
                if (remote) {
                    const int remaining = pending->deadline == Clock::time_point::max() ? 0 :
                        static_cast<int>(std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::milliseconds>(
                            pending->deadline - Clock::now()).count()));
                    auto cancel = invokeRemote(remote, *values, done, remaining, typename detail::make_index_sequence<sizeof...(Args)>::type{});
                    bool cancelNow = false;
                    {
                        SwMutexLocker lock(state->mutex);
                        cancelNow = state->closed.load() || pending->cancelRequested;
                        if (!cancelNow && !pending->done.load()) pending->remoteCancel = std::move(cancel);
                    }
                    if (cancelNow && cancel) cancel();
                    return;
                }
                if (!request || !response) {
                    Result result; result.error = "rpc: no remote adapter"; done(result); return;
                }
                Traits::listen(*response, pending->id, done);
                if (!invokeRequest(request, *values, pending->id, context.clientPid, context.clientInfo,
                                   typename detail::make_index_sequence<sizeof...(Args)>::type{})) {
                    Result result; result.error = "rpc: request queue full (or payload too large)";
                    done(result);
                }
            } catch (const std::exception& error) {
                Result result; result.error = SwString("rpc: transport exception: ") + SwString(error.what()).left(512);
                done(result);
            } catch (...) { Result result; result.error = "rpc: unknown transport exception"; done(result); }
        };
        if (!asynchronous) start();
        else if (!detail::postNativeSignalThread(pending->caller, std::move(start))) {
            Result result; result.error = "rpc: caller event queue unavailable";
            finish(state, response, pending, result);
        }
        return pending;
    }
    template<size_t Capacity> void initRequest() {
        using Queue = RingQueue<Capacity, uint64_t, uint32_t, SwString, Args...>;
        auto queue = std::make_shared<Queue>(registry_, rpcRequestQueueName(method_));
        request_ = [queue](uint64_t id, uint32_t pid, const SwString& info, const Args&... args) {
            return queue->push(id, pid, info, args...);
        };
    }
    Registry registry_;
    SwString method_;
    SwString clientInfo_;
    std::shared_ptr<State> state_;
    uint32_t pid_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<typename Native::Channel> native_;
    RemoteInvoker remote_;
    std::function<bool(uint64_t, uint32_t, const SwString&, const Args&...)> request_;
};

} // namespace ipc
} // namespace sw
