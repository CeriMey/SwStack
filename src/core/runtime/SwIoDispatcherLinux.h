#pragma once

// Implementation detail: applications include SwIoDispatcher.h.
#include "SwDebug.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static constexpr const char* kSwLogCategory_SwIoDispatcher = "sw.core.runtime.swiodispatcher";

class SwIoDispatcher {
public:
    enum EventFlag : uint32_t { None = 0, Readable = 1, Writable = 2, Error = 4, Hangup = 8 };
    using EventMask = uint32_t;
    using Token = size_t;
    using EventCallback = std::function<void(EventMask)>;
    using AffinityPoster = std::function<void(std::function<void()>)>;
    using ReliableAffinityPoster = std::function<bool(std::function<void()>)>;

    SwIoDispatcher() : state_(new State) {
        state_->epollFd = ::epoll_create1(EPOLL_CLOEXEC);
        state_->wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (state_->epollFd < 0 || state_->wakeFd < 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "Cannot create epoll/eventfd backend errno=" << errno;
            return;
        }
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u64 = 0;
        if (::epoll_ctl(state_->epollFd, EPOLL_CTL_ADD, state_->wakeFd, &ev) != 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "Cannot register epoll wake fd errno=" << errno;
            return;
        }
        state_->stopped = false;
        const auto state = state_;
        thread_ = std::thread([state]() { run_(state); });
    }
    ~SwIoDispatcher() { shutdown(); }
    SwIoDispatcher(const SwIoDispatcher&) = delete;
    SwIoDispatcher& operator=(const SwIoDispatcher&) = delete;

    void shutdown() {
        std::unordered_map<Token, std::shared_ptr<Entry>> removed;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->stopped = true;
            for (auto& item : state_->entries) item.second->active.store(false);
            removed.swap(state_->entries);
            wake_(*state_);
        }
        if (thread_.joinable()) {
            // An inline callback may destroy its dispatcher. The reactor owns State
            // until it exits, so even this case cannot close/reuse its fds early.
            if (thread_.get_id() == std::this_thread::get_id()) thread_.detach();
            else thread_.join();
        }
    }

    Token watchFd(int fd, EventMask events, const AffinityPoster& poster, const EventCallback& callback) {
        ReliableAffinityPoster reliable;
        if (poster) reliable = [poster](std::function<void()> task) { poster(std::move(task)); return true; };
        return watchFdReliable(fd, events, reliable, callback);
    }
    Token watchFdReliable(int fd, EventMask events, const ReliableAffinityPoster& poster,
                          const EventCallback& callback) {
        if (fd < 0 || !callback) return 0;
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->stopped) return 0;
        std::shared_ptr<Entry> entry(new Entry);
        entry->token = ++state_->nextToken;
        entry->fd = fd;
        entry->events = events;
        entry->poster = poster;
        entry->callback = callback;
        epoll_event ev{};
        ev.events = nativeEvents_(events);
        ev.data.u64 = entry->token;
        if (::epoll_ctl(state_->epollFd, EPOLL_CTL_ADD, fd, &ev) != 0) {
            swCError(kSwLogCategory_SwIoDispatcher) << "Cannot watch fd=" << fd << " errno=" << errno;
            return 0;
        }
        // Publication and registration share the lock used by delivery/removal.
        state_->entries.emplace(entry->token, entry);
        return entry->token;
    }
    bool updateFd(Token token, EventMask events) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->entries.find(token);
        if (state_->stopped || it == state_->entries.end() || !it->second->active.load()) return false;
        if (it->second->events == events) return true;
        it->second->events = events;
        it->second->dirty = true;
        wake_(*state_);
        return true;
    }
    void remove(Token token) {
        std::shared_ptr<Entry> removed;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto it = state_->entries.find(token);
            if (it == state_->entries.end()) return;
            removed = it->second;
            removed->active.store(false);
            ::epoll_ctl(state_->epollFd, EPOLL_CTL_DEL, removed->fd, nullptr);
            state_->entries.erase(it);
        }
        // Captured user objects may remove other watches from their destructors.
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        Token token{0};
        int fd{-1};
        EventMask events{None}, delivered{None};
        ReliableAffinityPoster poster;
        EventCallback callback;
        std::atomic<bool> active{true};
        // All remaining fields are protected by State::mutex.
        bool busy{false}, dirty{false}, retry{false};
        unsigned retryDelayMs{2};
        Clock::time_point retryAt;
    };
    struct State {
        std::mutex mutex;
        std::unordered_map<Token, std::shared_ptr<Entry>> entries;
        Token nextToken{0};
        bool stopped{true};
        int epollFd{-1}, wakeFd{-1};
        ~State() {
            if (wakeFd >= 0) ::close(wakeFd);
            if (epollFd >= 0) ::close(epollFd);
        }
    };
    std::shared_ptr<State> state_;
    std::thread thread_;

    static void wake_(State& state) {
        const uint64_t one = 1;
        if (state.wakeFd >= 0) {
            ssize_t written;
            do { written = ::write(state.wakeFd, &one, sizeof(one)); } while (written < 0 && errno == EINTR);
            // EAGAIN means a wake is already pending.
        }
    }
    static uint32_t nativeEvents_(EventMask events) {
        uint32_t result = EPOLLONESHOT | EPOLLERR | EPOLLHUP;
        if (events & Readable) result |= EPOLLIN;
        if (events & Writable) result |= EPOLLOUT;
        if (events & Hangup) result |= EPOLLRDHUP;
        return result;
    }
    static EventMask eventMask_(uint32_t events) {
        EventMask result = None;
        if (events & EPOLLIN) result |= Readable;
        if (events & EPOLLOUT) result |= Writable;
        if (events & EPOLLERR) result |= Error;
        if (events & (EPOLLHUP | EPOLLRDHUP)) result |= Hangup;
        return result;
    }
    static void dispatch_(const std::shared_ptr<State>& state, const std::shared_ptr<Entry>& entry) {
        if (!entry->active.load()) return;
        // A queued callback must not retain a dispatcher, nor keep native fds open.
        const std::weak_ptr<State> weakState(state);
        auto invoke = [weakState, entry]() {
            if (!entry->active.load()) return;
            try {
                entry->callback(entry->delivered);
            } catch (...) {
                swCError(kSwLogCategory_SwIoDispatcher) << "IO callback threw; disabling watch token=" << entry->token;
                entry->active.store(false);
            }
            if (auto state = weakState.lock()) {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->stopped || !entry->active.load()) return;
                entry->busy = false;
                entry->dirty = true;
                entry->retryDelayMs = 2;
                wake_(*state);
            }
        };
        try {
            if (!entry->poster) { invoke(); return; }
            if (entry->poster(std::move(invoke))) return;
        } catch (...) {
            swCError(kSwLogCategory_SwIoDispatcher) << "IO affinity poster threw; retrying token=" << entry->token;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->stopped || !entry->active.load()) return;
        // Keep the fd disabled under backpressure. Retry the retained notification
        // after 2/4/8/16/32 ms; no epoll spin and no lost edge if the fd was drained.
        entry->retry = true;
        entry->retryAt = Clock::now() + std::chrono::milliseconds(entry->retryDelayMs);
        if (entry->retryDelayMs < 32) entry->retryDelayMs *= 2;
    }
    static void run_(const std::shared_ptr<State>& state) {
        epoll_event events[64];
        for (;;) {
            std::vector<std::shared_ptr<Entry>> retries;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->stopped) return;
                const auto now = Clock::now();
                for (auto& item : state->entries) {
                    auto& entry = item.second;
                    if (!entry->active.load()) continue;
                    if (entry->retry && entry->retryAt <= now) {
                        entry->retry = false;
                        retries.push_back(entry);
                    }
                    if (entry->busy || !entry->dirty) continue;
                    epoll_event ev{};
                    ev.events = nativeEvents_(entry->events);
                    ev.data.u64 = entry->token;
                    if (::epoll_ctl(state->epollFd, EPOLL_CTL_MOD, entry->fd, &ev) != 0) {
                        entry->active.store(false);
                        swCError(kSwLogCategory_SwIoDispatcher) << "epoll rearm failed fd=" << entry->fd;
                    }
                    entry->dirty = false;
                }
            }
            for (const auto& entry : retries) dispatch_(state, entry);
            int timeout = -1;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->stopped) return;
                const auto now = Clock::now();
                for (const auto& item : state->entries) {
                    const auto& entry = item.second;
                    if (!entry->active.load() || !entry->retry) continue;
                    const auto delta = entry->retryAt - now;
                    const int ms = delta <= Clock::duration::zero() ? 0 :
                        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()) + 1;
                    if (timeout < 0 || ms < timeout) timeout = ms;
                }
            }
            const int ready = ::epoll_wait(state->epollFd, events, 64, timeout);
            if (ready < 0) {
                if (errno == EINTR) continue;
                swCError(kSwLogCategory_SwIoDispatcher) << "epoll_wait failed";
                return;
            }
            for (int i = 0; i < ready; ++i) {
                if (!events[i].data.u64) {
                    uint64_t value;
                    while (::read(state->wakeFd, &value, sizeof(value)) > 0) {}
                    continue;
                }
                std::shared_ptr<Entry> entry;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    auto it = state->entries.find(static_cast<Token>(events[i].data.u64));
                    if (state->stopped || it == state->entries.end()) continue;
                    entry = it->second;
                    if (!entry->active.load() || entry->busy) continue;
                    entry->busy = true;
                    entry->delivered = eventMask_(events[i].events);
                }
                dispatch_(state, entry);
            }
            // Only this thread issues MOD, after consuming the entire epoll batch.
            // updateFd during a callback changes the desired mask without rearming
            // its still-readable fd. Completion applies the latest mask next turn.
        }
    }
};
