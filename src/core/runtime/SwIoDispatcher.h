#pragma once

/**
 * @file src/core/runtime/SwIoDispatcher.h
 * @ingroup core_runtime
 * @brief Central native IO dispatcher used by the runtime and transport layers.
 */

#include "SwDebug.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include "platform/win/SwWindows.h"
#else
#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

static constexpr const char* kSwLogCategory_SwIoDispatcher = "sw.core.runtime.swiodispatcher";

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

    SwIoDispatcher() {
#if !defined(_WIN32)
        startLinuxBackend_();
#endif
    }

    ~SwIoDispatcher() {
        shutdown();
    }

    SwIoDispatcher(const SwIoDispatcher&) = delete;
    SwIoDispatcher& operator=(const SwIoDispatcher&) = delete;

    void shutdown() {
#if defined(_WIN32)
        std::vector<std::shared_ptr<Entry_>> entries;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
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
#else
        bool expected = false;
        if (!m_shutdown.compare_exchange_strong(expected, true)) {
            return;
        }

        signalLinuxWake_();
        if (m_linuxThread.joinable()) {
            m_linuxThread.join();
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();

        if (m_linuxWakeFd >= 0) {
            ::close(m_linuxWakeFd);
            m_linuxWakeFd = -1;
        }
        if (m_linuxEpollFd >= 0) {
            ::close(m_linuxEpollFd);
            m_linuxEpollFd = -1;
        }
#endif
    }

#if defined(_WIN32)
    Token watchHandle(HANDLE handle,
                      const AffinityPoster& poster,
                      const std::function<void()>& callback) {
        if (!handle || !callback) {
            return 0;
        }

        std::shared_ptr<Entry_> entry(new Entry_());
        entry->token = nextToken_();
        entry->poster = poster;
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

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown) {
            unregisterWindowsEntry_(entry);
            return 0;
        }
        m_entries[entry->token] = entry;
        return entry->token;
    }
#else
    Token watchFd(int fd,
                  EventMask events,
                  const AffinityPoster& poster,
                  const EventCallback& callback) {
        if (fd < 0 || !callback || m_linuxEpollFd < 0) {
            return 0;
        }

        std::shared_ptr<Entry_> entry(new Entry_());
        entry->token = nextToken_();
        entry->poster = poster;
        entry->callback = callback;
        entry->fd = fd;
        entry->events = events;

        struct epoll_event ev{};
        ev.events = toLinuxEvents_(events);
        ev.data.u64 = static_cast<uint64_t>(entry->token);
        if (::epoll_ctl(m_linuxEpollFd, EPOLL_CTL_ADD, fd, &ev) != 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] epoll_ctl add failed fd=" << fd;
            return 0;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_shutdown.load()) {
                ::epoll_ctl(m_linuxEpollFd, EPOLL_CTL_DEL, fd, nullptr);
                return 0;
            }
            m_entries[entry->token] = entry;
        }

        signalLinuxWake_();
        return entry->token;
    }

    bool updateFd(Token token, EventMask events) {
        std::shared_ptr<Entry_> entry;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_entries.find(token);
            if (it == m_entries.end()) {
                return false;
            }
            entry = it->second;
            entry->events = events;
        }

        if (!entry || entry->fd < 0 || m_linuxEpollFd < 0) {
            return false;
        }

        struct epoll_event ev{};
        ev.events = toLinuxEvents_(events);
        ev.data.u64 = static_cast<uint64_t>(token);
        if (::epoll_ctl(m_linuxEpollFd, EPOLL_CTL_MOD, entry->fd, &ev) != 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] epoll_ctl mod failed fd=" << entry->fd;
            return false;
        }
        signalLinuxWake_();
        return true;
    }
#endif

    void remove(Token token) {
        if (!token) {
            return;
        }

        std::shared_ptr<Entry_> entry;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
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

#if defined(_WIN32)
        unregisterWindowsEntry_(entry);
#else
        if (m_linuxEpollFd >= 0 && entry->fd >= 0) {
            ::epoll_ctl(m_linuxEpollFd, EPOLL_CTL_DEL, entry->fd, nullptr);
        }
        signalLinuxWake_();
#endif
    }

private:
    struct Entry_ : public std::enable_shared_from_this<Entry_> {
        Token token{0};
        AffinityPoster poster;
        EventCallback callback;
        std::atomic<bool> active{true};
        std::atomic<bool> dispatchQueued{false};
        std::atomic<uint32_t> pendingEvents{None};

#if defined(_WIN32)
        HANDLE waitHandle{NULL};
        HANDLE registeredWait{NULL};
#else
        int fd{-1};
        EventMask events{None};
#endif
    };

    std::mutex m_mutex;
    std::unordered_map<Token, std::shared_ptr<Entry_>> m_entries;
    std::atomic<bool> m_shutdown{false};
    std::atomic<Token> m_nextToken{1};

#if !defined(_WIN32)
    int m_linuxEpollFd{-1};
    int m_linuxWakeFd{-1};
    std::thread m_linuxThread;
#endif

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

        if (entry->poster) {
            entry->poster(std::move(invoke));
            return;
        }
        invoke();
    }

#if defined(_WIN32)
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
#else
    static uint32_t toLinuxEvents_(EventMask events) {
        uint32_t nativeEvents = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
        if (events & Readable) {
            nativeEvents |= EPOLLIN;
        }
        if (events & Writable) {
            nativeEvents |= EPOLLOUT;
        }
        return nativeEvents;
    }

    static EventMask fromLinuxEvents_(uint32_t nativeEvents) {
        EventMask events = None;
        if (nativeEvents & EPOLLIN) {
            events |= Readable;
        }
        if (nativeEvents & EPOLLOUT) {
            events |= Writable;
        }
        if (nativeEvents & EPOLLERR) {
            events |= Error;
        }
        if (nativeEvents & (EPOLLHUP | EPOLLRDHUP)) {
            events |= Hangup;
        }
        return events;
    }

    void startLinuxBackend_() {
        m_linuxEpollFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (m_linuxEpollFd < 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] epoll_create1 failed";
            return;
        }

        m_linuxWakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (m_linuxWakeFd < 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] eventfd failed";
            ::close(m_linuxEpollFd);
            m_linuxEpollFd = -1;
            return;
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u64 = 0;
        if (::epoll_ctl(m_linuxEpollFd, EPOLL_CTL_ADD, m_linuxWakeFd, &ev) != 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] epoll_ctl add wake fd failed";
            ::close(m_linuxWakeFd);
            ::close(m_linuxEpollFd);
            m_linuxWakeFd = -1;
            m_linuxEpollFd = -1;
            return;
        }

        m_linuxThread = std::thread([this]() { linuxLoop_(); });
    }

    void signalLinuxWake_() {
        if (m_linuxWakeFd < 0) {
            return;
        }
        const uint64_t one = 1;
        const ssize_t written = ::write(m_linuxWakeFd, &one, sizeof(one));
        (void)written;
    }

    void linuxLoop_() {
        std::vector<struct epoll_event> events(64);
        while (!m_shutdown.load()) {
            const int ready = ::epoll_wait(m_linuxEpollFd,
                                           events.data(),
                                           static_cast<int>(events.size()),
                                           -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                swCError(kSwLogCategory_SwIoDispatcher) << "[SwIoDispatcher] epoll_wait failed";
                return;
            }

            for (int i = 0; i < ready; ++i) {
                if (events[static_cast<size_t>(i)].data.u64 == 0) {
                    uint64_t value = 0;
                    while (::read(m_linuxWakeFd, &value, sizeof(value)) == sizeof(value)) {
                    }
                    continue;
                }

                std::shared_ptr<Entry_> entry;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto it = m_entries.find(static_cast<Token>(events[static_cast<size_t>(i)].data.u64));
                    if (it == m_entries.end()) {
                        continue;
                    }
                    entry = it->second;
                }

                dispatchToAffinity_(entry, fromLinuxEvents_(events[static_cast<size_t>(i)].events));
            }
        }
    }
#endif
};
