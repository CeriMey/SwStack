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
#include "SwString.h"
#include "SwTimer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace sw {
namespace ipc {

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
    return 500u;
}

inline std::atomic<uint32_t>& rpcQueueCapacityStorage_() {
    static std::atomic<uint32_t> cap(100u);
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
    /**
     * @brief Constructs a `RpcMethodClient` instance.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param methodName Value passed to the method.
     * @param clientInfo Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    RpcMethodClient(const SwString& domain,
                    const SwString& object,
                    const SwString& methodName,
                    const SwString& clientInfo = SwString())
        : reg_(domain, object),
          method_(methodName),
          clientInfo_(clientInfo),
          alive_(new std::atomic_bool(true)),
          pid_(detail::currentPid()) {
        initQueues_();
        startResponseListener_();
    }

    /**
     * @brief Destroys the `RpcMethodClient` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~RpcMethodClient() {
        if (alive_) alive_->store(false, std::memory_order_release);
        stopResponseListener_();
    }

    /**
     * @brief Constructs a `RpcMethodClient` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    RpcMethodClient(const RpcMethodClient&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    RpcMethodClient& operator=(const RpcMethodClient&) = delete;

    /**
     * @brief Returns the current last Error.
     * @return The current last Error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString lastError() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return lastError_;
    }

    // Blocking call (fiber-friendly if SwCoreApplication is running).
    /**
     * @brief Performs the `call` operation.
     * @param args Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return The requested call.
     */
    Ret call(const Args&... args, int timeoutMs = 2000) {
        clearLastError_();

        const uint64_t callId = nextCallId_++;

        Waiter waiter;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, &waiter);
        }

        if (!reqPush_ || !reqPush_(callId, pid_, clientInfo_, args...)) {
            erasePending_(callId);
            setLastError_("rpc: request queue full (or payload too large)");
            return Ret();
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app) {
            int timeoutTimerId = -1;
            if (timeoutMs > 0) {
                timeoutTimerId = app->addTimer([&waiter]() { waiter.loop.quit(); },
                                               timeoutMs * 1000,
                                               /*singleShot=*/true);
            }
            waiter.loop.exec(0);
            if (timeoutTimerId != -1) {
                app->removeTimer(timeoutTimerId);
            }
        } else {
            // Threadless mode (no SwCoreApplication): wait on the response queue wake event and dispatch.
            const auto t0 = std::chrono::steady_clock::now();
            const auto deadline = (timeoutMs > 0) ? (t0 + std::chrono::milliseconds(timeoutMs)) : t0;

            sw::ipc::detail::LoopPoller::instance().dispatch();

            while (!waiter.done) {
                if (timeoutMs > 0 && std::chrono::steady_clock::now() >= deadline) break;

#if defined(_WIN32)
                HANDLE h = respWakeEvent_ ? respWakeEvent_() : NULL;
                DWORD waitMs = INFINITE;
                if (timeoutMs > 0) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto rem = (deadline > now) ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                                                             .count()
                                                     : 0;
                    waitMs = (rem > 0) ? static_cast<DWORD>(rem) : 0;
                }
                if (h) {
                    (void)::WaitForSingleObject(h, waitMs);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif

                sw::ipc::detail::LoopPoller::instance().dispatch();
            }
        }

        // If still pending, it timed out (or app quit).
        if (erasePending_(callId)) {
            setLastError_("rpc: timeout");
            return Ret();
        }

        if (!waiter.done) {
            setLastError_("rpc: timeout");
            return Ret();
        }

        if (!waiter.ok) {
            setLastError_(waiter.error);
            return Ret();
        }

        return waiter.value;
    }

    // Async call: onOk is called only on success.
    /**
     * @brief Performs the `callAsync` operation.
     * @param args Value passed to the method.
     * @param onOk Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     */
    void callAsync(const Args&... args,
                   std::function<void(const Ret&)> onOk,
                   int timeoutMs = 2000) {
        clearLastError_();
        const uint64_t callId = nextCallId_++;

        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, std::move(onOk));
        }

        if (!reqPush_ || !reqPush_(callId, pid_, clientInfo_, args...)) {
            erasePending_(callId);
            setLastError_("rpc: request queue full (or payload too large)");
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (timeoutMs > 0 && app) {
            const std::shared_ptr<std::atomic_bool> alive = alive_;
            app->addTimer([this, alive, callId]() {
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                (void)erasePending_(callId);
            }, timeoutMs * 1000, /*singleShot=*/true);
        }
    }

private:
    struct Waiter {
        SwEventLoop loop;
        bool done{false};
        bool ok{false};
        SwString error;
        Ret value{};

        /**
         * @brief Performs the `wake` operation.
         * @param okIn Value passed to the method.
         * @param errIn Value passed to the method.
         * @param vIn Value passed to the method.
         */
        void wake(bool okIn, const SwString& errIn, const Ret& vIn) {
            done = true;
            ok = okIn;
            error = errIn;
            value = vIn;
            loop.quit();
        }
    };

    struct Pending {
        uint64_t callId{0};
        Waiter* waiter{nullptr};
        /**
         * @brief Performs the `function<void` operation.
         * @return The requested function<void.
         */
        std::function<void(const Ret&)> onOk;

        /**
         * @brief Constructs a `Pending` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Pending() {}
        /**
         * @brief Constructs a `Pending` instance.
         * @param id Value passed to the method.
         * @param w Width value.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Pending(uint64_t id, Waiter* w) : callId(id), waiter(w) {}
        /**
         * @brief Constructs a `Pending` instance.
         * @param id Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Pending(uint64_t id, std::function<void(const Ret&)> cb) : callId(id), waiter(nullptr), onOk(std::move(cb)) {}
    };

    template <size_t Capacity>
    void initQueuesCap_() {
        typedef RingQueue<Capacity, uint64_t, uint32_t, SwString, Args...> ReqQueue;
        typedef RingQueue<Capacity, uint64_t, bool, SwString, Ret> RespQueue;

        std::shared_ptr<ReqQueue> req(new ReqQueue(reg_, rpcRequestQueueName(method_)));
        std::shared_ptr<RespQueue> resp(new RespQueue(reg_, rpcResponseQueueName(method_, pid_)));

        reqHolder_ = req;
        respHolder_ = resp;

        reqPush_ = [req](uint64_t callId, uint32_t pid, const SwString& clientInfo, const Args&... argsIn) -> bool {
            return req->push(callId, pid, clientInfo, argsIn...);
        };

#if defined(_WIN32)
        respWakeEvent_ = [resp]() -> HANDLE { return resp->wakeEvent(); };
#endif

        startResponseListenerFn_ = [this, resp]() {
            typedef typename RespQueue::Subscription SubT;
            std::shared_ptr<SubT> sub(new SubT(resp->connect(
                [this](uint64_t callId, bool ok, SwString err, Ret value) {
                    onResponse_(callId, ok, err, value);
                },
                /*fireInitial=*/true)));
            loopSubHolder_ = sub;
            stopResponseListenerFn_ = [sub]() { sub->stop(); };
        };
    }

    void initQueues_() {
        const uint32_t cap = rpcQueueCapacity();
        switch (cap) {
            case 10u:  initQueuesCap_<10>(); break;
            case 25u:  initQueuesCap_<25>(); break;
            case 50u:  initQueuesCap_<50>(); break;
            case 100u: initQueuesCap_<100>(); break;
            case 200u: initQueuesCap_<200>(); break;
            case 500u: initQueuesCap_<500>(); break;
            default:   initQueuesCap_<100>(); break;
        }
    }

    void startResponseListener_() {
        if (startResponseListenerFn_) {
            startResponseListenerFn_();
        }
    }

    void stopResponseListener_() {
        if (stopResponseListenerFn_) {
            stopResponseListenerFn_();
            stopResponseListenerFn_ = std::function<void()>();
        }
        loopSubHolder_.reset();
    }

    void onResponse_(uint64_t callId, bool ok, const SwString& err, const Ret& value) {
        Pending p;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            typename std::map<uint64_t, Pending>::iterator it = pending_.find(callId);
            if (it == pending_.end()) return;
            p = it->second;
            pending_.erase(it);
        }

        if (p.waiter) {
            p.waiter->wake(ok, err, value);
        } else if (p.onOk) {
            if (ok) p.onOk(value);
        }
    }

    bool erasePending_(uint64_t callId) {
        std::lock_guard<std::mutex> lk(mutex_);
        typename std::map<uint64_t, Pending>::iterator it = pending_.find(callId);
        if (it == pending_.end()) return false;
        pending_.erase(it);
        return true;
    }

    void setLastError_(const SwString& e) const {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_ = e;
    }

    void clearLastError_() const {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_.clear();
    }

    Registry reg_;
    SwString method_;
    SwString clientInfo_;
    std::shared_ptr<std::atomic_bool> alive_;
    uint32_t pid_{0};

    std::shared_ptr<void> reqHolder_;
    std::shared_ptr<void> respHolder_;
    std::shared_ptr<void> loopSubHolder_;
    std::function<bool(uint64_t, uint32_t, const SwString&, const Args&...)> reqPush_;
    std::function<void()> startResponseListenerFn_;
    std::function<void()> stopResponseListenerFn_;
#if defined(_WIN32)
    std::function<HANDLE()> respWakeEvent_;
#endif

    mutable std::mutex mutex_;
    mutable SwString lastError_;
    std::map<uint64_t, Pending> pending_;
    std::atomic<uint64_t> nextCallId_{1};
};

// void specialization (no return value on success)
template <typename... Args>
class RpcMethodClient<void, Args...> {
public:
    /**
     * @brief Performs the `RpcMethodClient` operation.
     * @param domain Value passed to the method.
     * @param object Value passed to the method.
     * @param methodName Value passed to the method.
     * @param clientInfo Value passed to the method.
     */
    RpcMethodClient(const SwString& domain,
                    const SwString& object,
                    const SwString& methodName,
                    const SwString& clientInfo = SwString())
        : reg_(domain, object),
          method_(methodName),
          clientInfo_(clientInfo),
          alive_(new std::atomic_bool(true)),
          pid_(detail::currentPid()) {
        initQueues_();
        startResponseListener_();
    }

    /**
     * @brief Destroys the `Args` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~RpcMethodClient() {
        if (alive_) alive_->store(false, std::memory_order_release);
        stopResponseListener_();
    }

    /**
     * @brief Performs the `RpcMethodClient` operation.
     */
    RpcMethodClient(const RpcMethodClient&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    RpcMethodClient& operator=(const RpcMethodClient&) = delete;

    /**
     * @brief Returns the current last Error.
     * @return The current last Error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString lastError() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return lastError_;
    }

    /**
     * @brief Performs the `call` operation.
     * @param args Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return `true` on success; otherwise `false`.
     */
    bool call(const Args&... args, int timeoutMs = 2000) {
        clearLastError_();

        const uint64_t callId = nextCallId_++;

        Waiter waiter;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, &waiter);
        }

        if (!reqPush_ || !reqPush_(callId, pid_, clientInfo_, args...)) {
            erasePending_(callId);
            setLastError_("rpc: request queue full (or payload too large)");
            return false;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app) {
            int timeoutTimerId = -1;
            if (timeoutMs > 0) {
                timeoutTimerId = app->addTimer([&waiter]() { waiter.loop.quit(); },
                                               timeoutMs * 1000,
                                               /*singleShot=*/true);
            }
            waiter.loop.exec(0);
            if (timeoutTimerId != -1) {
                app->removeTimer(timeoutTimerId);
            }
        } else {
            // Threadless mode (no SwCoreApplication): wait on the response queue wake event and dispatch.
            const auto t0 = std::chrono::steady_clock::now();
            const auto deadline = (timeoutMs > 0) ? (t0 + std::chrono::milliseconds(timeoutMs)) : t0;

            sw::ipc::detail::LoopPoller::instance().dispatch();

            while (!waiter.done) {
                if (timeoutMs > 0 && std::chrono::steady_clock::now() >= deadline) break;

#if defined(_WIN32)
                HANDLE h = respWakeEvent_ ? respWakeEvent_() : NULL;
                DWORD waitMs = INFINITE;
                if (timeoutMs > 0) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto rem = (deadline > now) ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                                                             .count()
                                                     : 0;
                    waitMs = (rem > 0) ? static_cast<DWORD>(rem) : 0;
                }
                if (h) {
                    (void)::WaitForSingleObject(h, waitMs);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif

                sw::ipc::detail::LoopPoller::instance().dispatch();
            }
        }

        if (erasePending_(callId)) {
            setLastError_("rpc: timeout");
            return false;
        }

        if (!waiter.done) {
            setLastError_("rpc: timeout");
            return false;
        }
        if (!waiter.ok) {
            setLastError_(waiter.error);
            return false;
        }
        return true;
    }

    /**
     * @brief Performs the `callAsync` operation.
     * @param args Value passed to the method.
     * @param onDone Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     */
    void callAsync(const Args&... args, std::function<void(bool ok)> onDone, int timeoutMs = 2000) {
        clearLastError_();
        const uint64_t callId = nextCallId_++;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, std::move(onDone));
        }

        if (!reqPush_ || !reqPush_(callId, pid_, clientInfo_, args...)) {
            erasePending_(callId);
            setLastError_("rpc: request queue full (or payload too large)");
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (timeoutMs > 0 && app) {
            const std::shared_ptr<std::atomic_bool> alive = alive_;
            app->addTimer([this, alive, callId]() {
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                (void)erasePending_(callId);
            }, timeoutMs * 1000, /*singleShot=*/true);
        }
    }

private:
    struct Waiter {
        SwEventLoop loop;
        bool done{false};
        bool ok{false};
        SwString error;

        /**
         * @brief Performs the `wake` operation.
         * @param okIn Value passed to the method.
         * @param errIn Value passed to the method.
         */
        void wake(bool okIn, const SwString& errIn) {
            done = true;
            ok = okIn;
            error = errIn;
            loop.quit();
        }
    };

    struct Pending {
        uint64_t callId{0};
        Waiter* waiter{nullptr};
        /**
         * @brief Performs the `function<void` operation.
         * @param ok Optional flag updated to report success.
         * @return The requested function<void.
         */
        std::function<void(bool ok)> onDone;

        /**
         * @brief Constructs a `Pending` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Pending() {}
        /**
         * @brief Constructs a `Pending` instance.
         * @param id Value passed to the method.
         * @param w Width value.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Pending(uint64_t id, Waiter* w) : callId(id), waiter(w) {}
        /**
         * @brief Constructs a `Pending` instance.
         * @param id Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Pending(uint64_t id, std::function<void(bool)> cb) : callId(id), waiter(nullptr), onDone(std::move(cb)) {}
    };

    template <size_t Capacity>
    void initQueuesCap_() {
        typedef RingQueue<Capacity, uint64_t, uint32_t, SwString, Args...> ReqQueue;
        typedef RingQueue<Capacity, uint64_t, bool, SwString> RespQueue;

        std::shared_ptr<ReqQueue> req(new ReqQueue(reg_, rpcRequestQueueName(method_)));
        std::shared_ptr<RespQueue> resp(new RespQueue(reg_, rpcResponseQueueName(method_, pid_)));

        reqHolder_ = req;
        respHolder_ = resp;

        reqPush_ = [req](uint64_t callId, uint32_t pid, const SwString& clientInfo, const Args&... argsIn) -> bool {
            return req->push(callId, pid, clientInfo, argsIn...);
        };

#if defined(_WIN32)
        respWakeEvent_ = [resp]() -> HANDLE { return resp->wakeEvent(); };
#endif

        startResponseListenerFn_ = [this, resp]() {
            typedef typename RespQueue::Subscription SubT;
            std::shared_ptr<SubT> sub(new SubT(resp->connect(
                [this](uint64_t callId, bool ok, SwString err) {
                    onResponse_(callId, ok, err);
                },
                /*fireInitial=*/true)));
            loopSubHolder_ = sub;
            stopResponseListenerFn_ = [sub]() { sub->stop(); };
        };
    }

    void initQueues_() {
        const uint32_t cap = rpcQueueCapacity();
        switch (cap) {
            case 10u:  initQueuesCap_<10>(); break;
            case 25u:  initQueuesCap_<25>(); break;
            case 50u:  initQueuesCap_<50>(); break;
            case 100u: initQueuesCap_<100>(); break;
            case 200u: initQueuesCap_<200>(); break;
            case 500u: initQueuesCap_<500>(); break;
            default:   initQueuesCap_<100>(); break;
        }
    }

    void startResponseListener_() {
        if (startResponseListenerFn_) {
            startResponseListenerFn_();
        }
    }

    void stopResponseListener_() {
        if (stopResponseListenerFn_) {
            stopResponseListenerFn_();
            stopResponseListenerFn_ = std::function<void()>();
        }
        loopSubHolder_.reset();
    }

    void onResponse_(uint64_t callId, bool ok, const SwString& err) {
        Pending p;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            typename std::map<uint64_t, Pending>::iterator it = pending_.find(callId);
            if (it == pending_.end()) return;
            p = it->second;
            pending_.erase(it);
        }

        if (p.waiter) {
            p.waiter->wake(ok, err);
        } else if (p.onDone) {
            p.onDone(ok);
        }
    }

    bool erasePending_(uint64_t callId) {
        std::lock_guard<std::mutex> lk(mutex_);
        typename std::map<uint64_t, Pending>::iterator it = pending_.find(callId);
        if (it == pending_.end()) return false;
        pending_.erase(it);
        return true;
    }

    void setLastError_(const SwString& e) const {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_ = e;
    }

    void clearLastError_() const {
        std::lock_guard<std::mutex> lk(mutex_);
        lastError_.clear();
    }

    Registry reg_;
    SwString method_;
    SwString clientInfo_;
    std::shared_ptr<std::atomic_bool> alive_;
    uint32_t pid_{0};

    std::shared_ptr<void> reqHolder_;
    std::shared_ptr<void> respHolder_;
    std::shared_ptr<void> loopSubHolder_;
    std::function<bool(uint64_t, uint32_t, const SwString&, const Args&...)> reqPush_;
    std::function<void()> startResponseListenerFn_;
    std::function<void()> stopResponseListenerFn_;
#if defined(_WIN32)
    std::function<HANDLE()> respWakeEvent_;
#endif

    mutable std::mutex mutex_;
    mutable SwString lastError_;
    std::map<uint64_t, Pending> pending_;
    std::atomic<uint64_t> nextCallId_{1};
};

} // namespace ipc
} // namespace sw
