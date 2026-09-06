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
#include "SwIpcRpcRouter.h"
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

struct RpcContext {
    uint32_t clientPid{0};
    SwString clientInfo;
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

    RpcMethodClient(const SwString& domain, const SwString& object, const SwString& method,
                    const SwString& clientInfo = SwString())
        : registry_(domain, object), method_(method), clientInfo_(clientInfo),
          state_(std::make_shared<State>()), pid_(detail::currentPid()) {
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
    ~RpcMethodClient() {
        // Destruction suppresses callbacks. Explicit cancelAll() completes them.
        SwMutexLocker lock(state_->mutex);
        state_->closed = true;
        for (const auto& entry : state_->pending) {
            router_->remove(entry.first);
            stopTimer(entry.second);
        }
        state_->pending.clear();
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
    Result callResult(const Args&... args, int timeoutMs = 2000) {
        auto pending = begin(Completion(), args...);
        SwEventLoop::waitUntil([pending]() {
            detail::LoopPoller::instance().dispatch();
            return pending->done.load(std::memory_order_acquire);
        }, timeoutMs);
        if (!pending->done.load(std::memory_order_acquire)) {
            Result result; result.error = "rpc: timeout";
            finish(state_, router_, pending, result);
        }
        return pending->result;
    }
    typename Traits::Return call(const Args&... args, int timeoutMs = 2000) {
        return Traits::value(callResult(args..., timeoutMs));
    }

    // Always completes once while the client is alive; pending calls can be
    // explicitly cancelled. An event loop is required for asynchronous calls.
    uint64_t callAsyncResult(const Args&... args, Completion complete, int timeoutMs = 2000) {
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            Result result; result.error = "rpc: asynchronous call requires an event loop";
            { SwMutexLocker lock(state_->mutex); state_->lastError = result.error; }
            if (complete) complete(result);
            return 0;
        }
        auto state = state_;
        auto response = router_;
        auto pending = begin(std::move(complete), args...);
        if (timeoutMs > 0 && !pending->done.load(std::memory_order_acquire)) {
            std::weak_ptr<State> weak = state;
            std::weak_ptr<Router> router = response;
            std::weak_ptr<Pending> weakPending = pending;
            const int timer = app->addTimer([weak, router, weakPending]() {
                auto pending = weakPending.lock();
                if (!pending) return;
                auto state = weak.lock(); auto response = router.lock();
                if (!state || !response) return;
                Result result; result.error = "rpc: timeout";
                finish(state, response, pending, result);
            }, static_cast<int64_t>(timeoutMs) * 1000, true);
            SwMutexLocker lock(state->mutex);
            if (state->closed || pending->done.load()) app->removeTimer(timer);
            else pending->timer = timer;
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
            if (it == state_->pending.end()) return false;
            pending = it->second;
        }
        Result result; result.error = "rpc: cancelled";
        return finish(state_, router_, pending, result);
    }
    void cancelAll() {
        auto state = state_; auto router = router_;
        std::vector<std::shared_ptr<Pending>> pending;
        { SwMutexLocker lock(state->mutex);
          for (const auto& entry : state->pending) pending.push_back(entry.second); }
        Result result; result.error = "rpc: cancelled";
        for (const auto& call : pending) finish(state, router, call, result);
    }
private:
    struct Pending {
        uint64_t id{0};
        std::atomic_bool done{false};
        Result result;
        Completion complete;
        int timer{0};
    };
    struct State {
        SwMutex mutex;
        bool closed{false};
        SwString lastError;
        std::map<uint64_t, std::shared_ptr<Pending>> pending;
    };
    static void stopTimer(const std::shared_ptr<Pending>& pending) {
        if (pending->timer) {
            if (auto app = SwCoreApplication::instance(false)) app->removeTimer(pending->timer);
            pending->timer = 0;
        }
    }
    static bool finish(const std::shared_ptr<State>& state, const std::shared_ptr<Router>& router,
                       const std::shared_ptr<Pending>& pending, const Result& result) {
        Completion complete;
        {
            SwMutexLocker lock(state->mutex);
            if (state->closed || pending->done.load(std::memory_order_acquire)) return false;
            router->remove(pending->id);
            stopTimer(pending);
            state->pending.erase(pending->id);
            pending->result = result;
            state->lastError = result.error;
            complete = std::move(pending->complete);
            pending->done.store(true, std::memory_order_release);
        }
        if (complete) complete(result);
        return true;
    }
    std::shared_ptr<Pending> begin(Completion complete, const Args&... args) {
        auto pending = std::make_shared<Pending>();
        pending->id = nextRpcCallId();
        pending->complete = std::move(complete);
        {
            SwMutexLocker lock(state_->mutex);
            state_->lastError.clear();
            state_->pending[pending->id] = pending;
        }
        std::weak_ptr<State> weak = state_;
        std::weak_ptr<Router> router = router_;
        Traits::listen(*router_, pending->id, [weak, router, pending](const Result& result) {
            auto state = weak.lock(); auto response = router.lock();
            if (state && response) finish(state, response, pending, result);
        });
        if (!request_(pending->id, pid_, clientInfo_, args...)) {
            Result result; result.error = "rpc: request queue full (or payload too large)";
            finish(state_, router_, pending, result);
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
    std::function<bool(uint64_t, uint32_t, const SwString&, const Args&...)> request_;
};

} // namespace ipc
} // namespace sw
