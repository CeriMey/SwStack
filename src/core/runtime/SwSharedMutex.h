#pragma once

#include "SwEventLoop.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

// SharedLockable for both scheduled tasks and native threads. Ownership belongs
// to the caller, not the OS thread: separate fibers may share an OS thread.
class SwSharedMutex {
public:
    SwSharedMutex() = default;
    SwSharedMutex(const SwSharedMutex&) = delete;
    SwSharedMutex& operator=(const SwSharedMutex&) = delete;

    bool try_lock() { return tryAcquire(false); }
    bool try_lock_shared() { return tryAcquire(true); }
    void lock() { acquire(false); }
    void lock_shared() { acquire(true); }
    void unlock() { release(false); }
    void unlock_shared() { release(true); }

private:
    struct Waiter {
        SwCoreApplication* app;
        SwEventLoop* loop;
        std::mutex mutex;
        Waiter(SwCoreApplication* application, SwEventLoop* eventLoop)
            : app(application), loop(eventLoop) {}
    };

    bool available(bool shared) const { return !writer_ && (shared || readers_ == 0); }
    void take(bool shared) {
        if (shared) ++readers_;
        else writer_ = true;
    }
    bool tryAcquire(bool shared) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!available(shared)) return false;
        take(shared);
        return true;
    }
    void acquire(bool shared) {
        if (!SwEventLoop::canYieldCurrentTask()) {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [&] { return available(shared); });
            take(shared);
            return;
        }
        if (tryAcquire(shared)) return;
        for (;;) {
            SwEventLoop loop;
            auto waiter = std::make_shared<Waiter>(SwCoreApplication::instance(false), &loop);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // Registration and availability share a lock: no lost unlock.
                if (available(shared)) {
                    take(shared);
                    return;
                }
                waiters_.push_back(waiter);
            }
            loop.exec();
            {
                // A late notification cannot access a destroyed event loop.
                std::lock_guard<std::mutex> lock(waiter->mutex);
                waiter->loop = nullptr;
            }
        }
    }
    void release(bool shared) {
        std::vector<std::shared_ptr<Waiter>> waiting;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shared) --readers_;
            else writer_ = false;
            if (writer_ || readers_ != 0) return;
            waiting.swap(waiters_);
        }
        changed_.notify_all();
        for (const auto& waiter : waiting) {
            waiter->app->postEventOnLaneReliable([waiter] {
                std::lock_guard<std::mutex> lock(waiter->mutex);
                if (waiter->loop) waiter->loop->quit();
            });
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    size_t readers_{0};
    bool writer_{false};
    std::vector<std::shared_ptr<Waiter>> waiters_;
};
