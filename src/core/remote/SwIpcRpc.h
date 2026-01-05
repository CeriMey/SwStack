#pragma once
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

inline SwString rpcRequestQueueName(const SwString& methodName) {
    return SwString("__rpc__|") + methodName;
}

inline SwString rpcResponseQueueName(const SwString& methodName, uint32_t clientPid) {
    return SwString("__rpc_ret__|") + methodName + "|" + SwString::number(static_cast<int>(clientPid));
}

template <typename Ret, typename... Args>
class RpcMethodClient {
public:
    RpcMethodClient(const SwString& domain,
                    const SwString& object,
                    const SwString& methodName,
                    const SwString& clientInfo = SwString())
        : reg_(domain, object),
          method_(methodName),
          clientInfo_(clientInfo),
          alive_(new std::atomic_bool(true)),
          pid_(detail::currentPid()),
          req_(reg_, rpcRequestQueueName(method_)),
          resp_(reg_, rpcResponseQueueName(method_, pid_)) {
        startResponseListener_();
    }

    ~RpcMethodClient() {
        if (alive_) alive_->store(false, std::memory_order_release);
        stopResponseListener_();
    }

    RpcMethodClient(const RpcMethodClient&) = delete;
    RpcMethodClient& operator=(const RpcMethodClient&) = delete;

    SwString lastError() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return lastError_;
    }

    // Blocking call (fiber-friendly if SwCoreApplication is running).
    Ret call(const Args&... args, int timeoutMs = 2000) {
        clearLastError_();

        const uint64_t callId = nextCallId_++;

        Waiter waiter;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, &waiter);
        }

        if (!req_.push(callId, pid_, clientInfo_, args...)) {
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
                HANDLE h = resp_.wakeEvent();
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
    void callAsync(const Args&... args,
                   std::function<void(const Ret&)> onOk,
                   int timeoutMs = 2000) {
        clearLastError_();
        const uint64_t callId = nextCallId_++;

        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, std::move(onOk));
        }

        if (!req_.push(callId, pid_, clientInfo_, args...)) {
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
        std::function<void(const Ret&)> onOk;

        Pending() {}
        Pending(uint64_t id, Waiter* w) : callId(id), waiter(w) {}
        Pending(uint64_t id, std::function<void(const Ret&)> cb) : callId(id), waiter(nullptr), onOk(std::move(cb)) {}
    };

    void startResponseListener_() {
        loopSub_ = resp_.connect([this](uint64_t callId, bool ok, SwString err, Ret value) {
            onResponse_(callId, ok, err, value);
        }, /*fireInitial=*/true);
    }

    void stopResponseListener_() {
        loopSub_.stop();
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

    RingQueue<10, uint64_t, uint32_t, SwString, Args...> req_;
    RingQueue<10, uint64_t, bool, SwString, Ret> resp_;

    typename RingQueue<10, uint64_t, bool, SwString, Ret>::Subscription loopSub_;

    mutable std::mutex mutex_;
    mutable SwString lastError_;
    std::map<uint64_t, Pending> pending_;
    std::atomic<uint64_t> nextCallId_{1};
};

// void specialization (no return value on success)
template <typename... Args>
class RpcMethodClient<void, Args...> {
public:
    RpcMethodClient(const SwString& domain,
                    const SwString& object,
                    const SwString& methodName,
                    const SwString& clientInfo = SwString())
        : reg_(domain, object),
          method_(methodName),
          clientInfo_(clientInfo),
          alive_(new std::atomic_bool(true)),
          pid_(detail::currentPid()),
          req_(reg_, rpcRequestQueueName(method_)),
          resp_(reg_, rpcResponseQueueName(method_, pid_)) {
        startResponseListener_();
    }

    ~RpcMethodClient() {
        if (alive_) alive_->store(false, std::memory_order_release);
        stopResponseListener_();
    }

    RpcMethodClient(const RpcMethodClient&) = delete;
    RpcMethodClient& operator=(const RpcMethodClient&) = delete;

    SwString lastError() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return lastError_;
    }

    bool call(const Args&... args, int timeoutMs = 2000) {
        clearLastError_();

        const uint64_t callId = nextCallId_++;

        Waiter waiter;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, &waiter);
        }

        if (!req_.push(callId, pid_, clientInfo_, args...)) {
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
                HANDLE h = resp_.wakeEvent();
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

    void callAsync(const Args&... args, std::function<void(bool ok)> onDone, int timeoutMs = 2000) {
        clearLastError_();
        const uint64_t callId = nextCallId_++;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            pending_[callId] = Pending(callId, std::move(onDone));
        }

        if (!req_.push(callId, pid_, clientInfo_, args...)) {
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
        std::function<void(bool ok)> onDone;

        Pending() {}
        Pending(uint64_t id, Waiter* w) : callId(id), waiter(w) {}
        Pending(uint64_t id, std::function<void(bool)> cb) : callId(id), waiter(nullptr), onDone(std::move(cb)) {}
    };

    void startResponseListener_() {
        loopSub_ = resp_.connect([this](uint64_t callId, bool ok, SwString err) {
            onResponse_(callId, ok, err);
        }, /*fireInitial=*/true);
    }

    void stopResponseListener_() {
        loopSub_.stop();
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

    RingQueue<10, uint64_t, uint32_t, SwString, Args...> req_;
    RingQueue<10, uint64_t, bool, SwString> resp_;

    typename RingQueue<10, uint64_t, bool, SwString>::Subscription loopSub_;

    mutable std::mutex mutex_;
    mutable SwString lastError_;
    std::map<uint64_t, Pending> pending_;
    std::atomic<uint64_t> nextCallId_{1};
};

} // namespace ipc
} // namespace sw
