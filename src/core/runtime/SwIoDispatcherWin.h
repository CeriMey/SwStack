#pragma once

/**
 * @file src/core/runtime/SwIoDispatcherWin.h
 * @ingroup core_runtime
 * @brief Windows backend; applications include SwIoDispatcher.h.
 */

#include "SwDebug.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <winsock2.h>
#include "platform/win/SwWindows.h"

static constexpr const char* kSwLogCategory_SwIoDispatcher = "sw.core.runtime.swiodispatcher";

class SwIoDispatcherMutex_ {
public:
    SwIoDispatcherMutex_() noexcept {
        ::InitializeSRWLock(&lock_);
    }

    SwIoDispatcherMutex_(const SwIoDispatcherMutex_&) = delete;
    SwIoDispatcherMutex_& operator=(const SwIoDispatcherMutex_&) = delete;

    void lock() noexcept {
        ::AcquireSRWLockExclusive(&lock_);
    }

    void unlock() noexcept {
        ::ReleaseSRWLockExclusive(&lock_);
    }

private:
    SRWLOCK lock_{};
};

class SwIoDispatcherLock_ {
public:
    explicit SwIoDispatcherLock_(SwIoDispatcherMutex_& mutex) noexcept
        : mutex_(&mutex) {
        mutex_->lock();
    }

    ~SwIoDispatcherLock_() {
        if (mutex_) {
            mutex_->unlock();
        }
    }

    SwIoDispatcherLock_(const SwIoDispatcherLock_&) = delete;
    SwIoDispatcherLock_& operator=(const SwIoDispatcherLock_&) = delete;

private:
    SwIoDispatcherMutex_* mutex_{nullptr};
};

class SwIoDispatcher {
public:
    enum EventFlag : uint32_t {
        None = 0x0,
        Readable = 0x1,
        Writable = 0x2,
        Error = 0x4,
        Hangup = 0x8
    };

    using EventMask = uint32_t;
    using Token = size_t;
    using EventCallback = std::function<void(EventMask)>;
    using AffinityPoster = std::function<void(std::function<void()>)>;
    using ReliableAffinityPoster = std::function<bool(std::function<void()>)>;

    SwIoDispatcher() {
    }

    ~SwIoDispatcher() {
        shutdown();
    }

    SwIoDispatcher(const SwIoDispatcher&) = delete;
    SwIoDispatcher& operator=(const SwIoDispatcher&) = delete;

    void shutdown() {
        std::vector<std::shared_ptr<Entry_>> entries;
        {
            SwIoDispatcherLock_ lock(m_mutex);
            if (m_shutdown) {
                return;
            }
            m_shutdown = true;
            for (auto& kv : m_entries) {
                entries.push_back(kv.second);
            }
            m_entries.clear();
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            unregisterWindowsEntry_(entries[i]);
        }
    }

    Token watchHandle(HANDLE handle,
                      const AffinityPoster& poster,
                      const std::function<void()>& callback) {
        return watchHandleImpl_(handle,
                                ReliableAffinityPoster(),
                                poster,
                                callback);
    }

    Token watchHandleReliable(HANDLE handle,
                              const ReliableAffinityPoster& poster,
                              const std::function<void()>& callback) {
        return watchHandleImpl_(handle,
                                poster,
                                AffinityPoster(),
                                callback);
    }

    void remove(Token token) {
        if (!token) {
            return;
        }

        std::shared_ptr<Entry_> entry;
        {
            SwIoDispatcherLock_ lock(m_mutex);
            auto it = m_entries.find(token);
            if (it == m_entries.end()) {
                return;
            }
            entry = it->second;
            m_entries.erase(it);
        }

        if (!entry) {
            return;
        }
        entry->active.store(false);

        unregisterWindowsEntry_(entry);
    }

private:
    Token watchHandleImpl_(HANDLE handle,
                           const ReliableAffinityPoster& reliablePoster,
                           const AffinityPoster& legacyPoster,
                           const std::function<void()>& callback) {
        if (!handle || !callback) {
            return 0;
        }

        std::shared_ptr<Entry_> entry(new Entry_());
        entry->token = nextToken_();
        entry->reliablePoster = reliablePoster;
        entry->legacyPoster = legacyPoster;
        entry->callback = [callback](EventMask) { callback(); };
        entry->waitHandle = handle;

        if (!::RegisterWaitForSingleObject(&entry->registeredWait,
                                           handle,
                                           &SwIoDispatcher::windowsWaitCallback_,
                                           entry.get(),
                                           INFINITE,
                                           WT_EXECUTEINWAITTHREAD | WT_EXECUTEDEFAULT)) {
            swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] RegisterWaitForSingleObject failed";
            return 0;
        }

        SwIoDispatcherLock_ lock(m_mutex);
        if (m_shutdown) {
            unregisterWindowsEntry_(entry);
            return 0;
        }
        m_entries[entry->token] = entry;
        return entry->token;
    }
    struct Entry_ : public std::enable_shared_from_this<Entry_> {
        Token token{0};
        ReliableAffinityPoster reliablePoster;
        AffinityPoster legacyPoster;
        EventCallback callback;
        std::atomic<bool> active{true};
        std::atomic<bool> dispatchQueued{false};
        std::atomic<uint32_t> pendingEvents{None};

        HANDLE waitHandle{NULL};
        HANDLE registeredWait{NULL};
    };

    SwIoDispatcherMutex_ m_mutex;
    std::unordered_map<Token, std::shared_ptr<Entry_>> m_entries;
    std::atomic<bool> m_shutdown{false};
    std::atomic<Token> m_nextToken{1};


    Token nextToken_() {
        return m_nextToken.fetch_add(1, std::memory_order_relaxed);
    }

    static void dispatchToAffinity_(const std::shared_ptr<Entry_>& entry, EventMask events) {
        if (!entry || !entry->active.load() || !entry->callback) {
            return;
        }

        entry->pendingEvents.fetch_or(events, std::memory_order_relaxed);
        bool expected = false;
        if (!entry->dispatchQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        auto invoke = [entry]() {
            while (entry->active.load(std::memory_order_acquire) && entry->callback) {
                const EventMask pending = entry->pendingEvents.exchange(None, std::memory_order_acq_rel);
                if (pending != None) {
                    entry->callback(pending);
                }

                entry->dispatchQueued.store(false, std::memory_order_release);
                if (entry->pendingEvents.load(std::memory_order_acquire) == None) {
                    break;
                }

                bool requeue = false;
                if (!entry->dispatchQueued.compare_exchange_strong(requeue,
                                                                   true,
                                                                   std::memory_order_acq_rel)) {
                    break;
                }
            }
        };

        if (entry->reliablePoster) {
            if (entry->reliablePoster(std::move(invoke))) {
                return;
            }

            // The event bits remain in pendingEvents.  Release the coalescing lease so a
            // level-triggered native notification can retry once the affinity queue has room.
            // In particular, never leave dispatchQueued stuck after runtime backpressure.
            entry->dispatchQueued.store(false, std::memory_order_release);
            return;
        }
        if (entry->legacyPoster) {
            entry->legacyPoster(std::move(invoke));
            return;
        }
        invoke();
    }

    static VOID CALLBACK windowsWaitCallback_(PVOID context, BOOLEAN timedOut) {
        (void)timedOut;
        Entry_* entry = static_cast<Entry_*>(context);
        if (!entry || !entry->active.load()) {
            return;
        }
        dispatchToAffinity_(entry->shared_from_this(), Readable);
    }

    void unregisterWindowsEntry_(const std::shared_ptr<Entry_>& entry) {
        if (!entry) {
            return;
        }
        entry->active.store(false);
        if (entry->registeredWait) {
            HANDLE waitHandle = entry->registeredWait;
            entry->registeredWait = NULL;
            ::UnregisterWaitEx(waitHandle, INVALID_HANDLE_VALUE);
        }
    }
};
