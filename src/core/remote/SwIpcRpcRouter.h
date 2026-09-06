#pragma once

#include "SwSharedMemorySignal.h"
#include "SwIpcRpcResources.h"
#include <atomic>
#include "SwMap.h"
#include <memory>
#include "SwMutex.h"

namespace sw { namespace ipc {

inline uint64_t nextRpcCallId() {
    static std::atomic<uint64_t> sequence{1};
    return sequence.fetch_add(1, std::memory_order_relaxed);
}

// One consuming subscription per response queue in this process. A separate
// RingQueue listener per proxy would steal responses from the other proxies.
template<class... Values>
class RpcResponseRouter : public std::enable_shared_from_this<RpcResponseRouter<Values...>> {
public:
    using Callback = std::function<void(bool, const SwString&, const Values&...)>;
    static std::shared_ptr<RpcResponseRouter> get(const SwString& domain, const SwString& object,
                                                 const SwString& signal, uint32_t capacity) {
        static SwMutex poolMutex;
        static SwMap<SwString, std::weak_ptr<RpcResponseRouter>> pool;
        const SwString key = detail::make_shm_name(domain, object, signal);
        SwMutexLocker lock(poolMutex);
        for (auto it = pool.begin(); it != pool.end(); ) {
            if (it.value().expired()) it = pool.erase(it); else ++it;
        }
        auto shared = pool[key].lock();
        if (!shared) {
            shared.reset(new RpcResponseRouter(domain, object));
            shared->start(signal, capacity);
            pool[key] = shared;
        }
        return shared;
    }
    void add(uint64_t id, Callback callback) {
        SwMutexLocker lock(mutex_);
        callbacks_[id] = std::move(callback);
    }
    void remove(uint64_t id) {
        SwMutexLocker lock(mutex_);
        callbacks_.erase(id);
    }
private:
    explicit RpcResponseRouter(const SwString& domain, const SwString& object) : registry_(domain, object) {}
    template<size_t Capacity> void startQueue(const SwString& signal) {
        using Queue = RingQueue<Capacity, uint64_t, bool, SwString, Values...>;
        auto queue = std::make_shared<Queue>(registry_, signal);
        detail::RpcResponseResources::own(queue->shmName());
        std::weak_ptr<RpcResponseRouter> weak = this->shared_from_this();
        auto subscription = queue->connect([weak](uint64_t id, bool ok, SwString error, Values... values) {
            auto self = weak.lock();
            if (!self) return;
            Callback callback;
            {
                SwMutexLocker lock(self->mutex_);
                auto it = self->callbacks_.find(id);
                if (it == self->callbacks_.end()) return;
                callback = std::move(it.value());
                self->callbacks_.erase(it);
            }
            callback(ok, error, values...);
        }, true);
        queue_ = queue;
        subscription_ = std::make_shared<typename Queue::Subscription>(std::move(subscription));
    }
    void start(const SwString& signal, uint32_t capacity) {
        switch (capacity) {
#define SW_RPC_ROUTER_CAP(N) case N: startQueue<N>(signal); break
            SW_RPC_ROUTER_CAP(10); SW_RPC_ROUTER_CAP(25); SW_RPC_ROUTER_CAP(50);
            SW_RPC_ROUTER_CAP(100); SW_RPC_ROUTER_CAP(200); SW_RPC_ROUTER_CAP(500);
            SW_RPC_ROUTER_CAP(1000);
#undef SW_RPC_ROUTER_CAP
            default: throw std::invalid_argument("unsupported RPC capacity");
        }
    }
    Registry registry_;
    SwMutex mutex_;
    SwMap<uint64_t, Callback> callbacks_;
    std::shared_ptr<void> queue_;
    std::shared_ptr<void> subscription_;
};

// Completion has a single shape on success, timeout, cancellation and remote error.
template<class T> struct RpcResult {
    bool ok{false};
    T value{};
    SwString error;
    explicit operator bool() const { return ok; }
};
template<> struct RpcResult<void> {
    bool ok{false};
    SwString error;
    explicit operator bool() const { return ok; }
};

template<class T> struct RpcResultTraits {
    using Router = RpcResponseRouter<T>;
    using Return = T;
    using Success = std::function<void(const T&)>;
    static T value(const RpcResult<T>& result) { return result.value; }
    template<class Fn> static void listen(Router& router, uint64_t id, Fn fn) {
        router.add(id, [fn](bool ok, const SwString& error, const T& value) {
            RpcResult<T> result; result.ok = ok; result.error = error; result.value = value;
            fn(result);
        });
    }
};
template<> struct RpcResultTraits<void> {
    using Router = RpcResponseRouter<>;
    using Return = bool;
    static bool value(const RpcResult<void>& result) { return result.ok; }
    template<class Fn> static void listen(Router& router, uint64_t id, Fn fn) {
        router.add(id, [fn](bool ok, const SwString& error) {
            RpcResult<void> result; result.ok = ok; result.error = error; fn(result);
        });
    }
};
}}
