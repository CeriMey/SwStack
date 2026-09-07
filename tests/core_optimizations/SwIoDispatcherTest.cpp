#include "SwIoDispatcher.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <stdexcept>
#if !defined(_WIN32)
#include <sys/socket.h>
#include <ctime>
#endif

using Clock = std::chrono::steady_clock;
class Queue {
public:
    bool post(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex);
        tasks.push_back(std::move(task));
        changed.notify_one();
        return true;
    }
    std::function<void()> take() {
        std::unique_lock<std::mutex> lock(mutex);
        // GCC 11's TSan does not intercept pthread_cond_clockwait used by
        // steady-clock wait_for. The realtime timed wait is intercepted.
        assert(changed.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(3),
                                  [&] { return !tasks.empty(); }));
        auto task = std::move(tasks.front());
        tasks.pop_front();
        return task;
    }
    size_t size() { std::lock_guard<std::mutex> lock(mutex); return tasks.size(); }
private:
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> tasks;
};
class Event {
public:
#if defined(_WIN32)
    HANDLE fd = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Event() { assert(fd); }
    ~Event() { ::CloseHandle(fd); }
    void signal() { assert(::SetEvent(fd)); }
    void drain() { assert(::ResetEvent(fd)); }
    SwIoDispatcher::Token watch(SwIoDispatcher& io, const SwIoDispatcher::ReliableAffinityPoster& poster,
                               const std::function<void()>& callback) {
        return io.watchHandleReliable(fd, poster, callback);
    }
#else
    int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    Event() { assert(fd >= 0); }
    ~Event() { ::close(fd); }
    void signal() { const uint64_t one = 1; assert(::write(fd, &one, sizeof(one)) == sizeof(one)); }
    void drain() { uint64_t n; while (::read(fd, &n, sizeof(n)) > 0) {} }
    SwIoDispatcher::Token watch(SwIoDispatcher& io, const SwIoDispatcher::ReliableAffinityPoster& poster,
                               const std::function<void()>& callback) {
        return io.watchFdReliable(fd, SwIoDispatcher::Readable, poster, [callback](uint32_t) { callback(); });
    }
#endif
};
#if defined(_WIN32)
static void pendingAndLifetime() {
    Queue queue;
    Event event;
    unsigned callbacks = 0;
    std::function<void()> stale;
    {
        SwIoDispatcher io;
        auto poster = [&](std::function<void()> task) { return queue.post(std::move(task)); };
        auto token = event.watch(io, poster, [&] { ++callbacks; event.drain(); });
        assert(token);
        event.signal();
        stale = queue.take();
        io.remove(token);
        stale();
        assert(callbacks == 0);
        event.drain();
        token = event.watch(io, poster, [&] { ++callbacks; event.drain(); });
        assert(token);
        event.signal();
        stale = queue.take();
    }
    stale();
    assert(callbacks == 0);
}
#else
static void pendingAndLifetime() {
    Queue queue;
    Event event;
    unsigned callbacks = 0;
    std::function<void()> stale;
    {
        SwIoDispatcher io;
        auto token = event.watch(io, [&](std::function<void()> task) { return queue.post(std::move(task)); },
                                [&] { ++callbacks; event.drain(); });
        assert(token);
        event.signal();
        auto task = queue.take();
#if !defined(_WIN32)
        timespec before{}, after{};
        ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &before);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &after);
        const double cpuMs = (after.tv_sec - before.tv_sec) * 1000.0 + (after.tv_nsec - before.tv_nsec) / 1e6;
        std::cout << "pending callback CPU ms / 250 ms=" << cpuMs << '\n';
        assert(cpuMs < 100); // Old level-triggered dispatcher consumes ~250 ms.
        assert(queue.size() == 0);
        assert(io.updateFd(token, SwIoDispatcher::Readable | SwIoDispatcher::Error));
#endif
        task();
#if !defined(_WIN32)
        assert(callbacks == 1);
#else
        assert(callbacks >= 1); // Windows may coalesce several native wait notifications.
#endif
        for (int i = 0; i < 200; ++i) {
            event.signal();
            queue.take()();
        }
#if !defined(_WIN32)
        assert(callbacks == 201);
#endif
        event.signal();
        stale = queue.take();
        io.remove(token);
        const auto saved = callbacks;
        stale();
        assert(callbacks == saved);
        event.drain();
        token = event.watch(io, [&](std::function<void()> task) { return queue.post(std::move(task)); },
                            [&] { ++callbacks; event.drain(); });
        assert(token);
        event.signal();
        stale = queue.take();
    }
    const auto saved = callbacks;
    stale(); // Dispatcher and native backend have been destroyed.
    assert(callbacks == saved);
}
#endif

static void backpressure() {
    Queue queue;
    Event event;
    SwIoDispatcher io;
    std::atomic<int> attempts{0};
    unsigned callbacks = 0;
    auto token = event.watch(io, [&](std::function<void()> task) {
        const int attempt = ++attempts;
        if (attempt < 5) return false;
        return queue.post(std::move(task));
    }, [&] { event.drain(); ++callbacks; });
    assert(token);
    event.signal();
    queue.take()();
    assert(attempts >= 5 && callbacks >= 1);
#if !defined(_WIN32)
    assert(callbacks == 1);
#endif
    io.remove(token);
}
#if !defined(_WIN32)
static void exceptionsAndReuse() {
    Event event;
    Queue queue;
    SwIoDispatcher io;
    int attempts = 0;
    auto token = event.watch(io, [&](std::function<void()> task) {
        if (++attempts == 1) throw std::runtime_error("poster");
        return queue.post(std::move(task));
    }, [&] { event.drain(); throw std::runtime_error("callback"); });
    event.signal();
    queue.take()(); // Callback exception is contained, watch disabled.
    io.remove(token);
    unsigned delivered = 0;
    for (int i = 0; i < 100; ++i) {
        token = event.watch(io, [&](std::function<void()> task) { return queue.post(std::move(task)); },
                            [&] { event.drain(); ++delivered; });
        event.signal();
        auto old = queue.take();
        io.remove(token);
        event.drain();
        token = event.watch(io, [&](std::function<void()> task) { return queue.post(std::move(task)); },
                            [&] { event.drain(); ++delivered; });
        old(); // Same fd, new registration: must not rearm/call the removed watch.
        event.signal();
        queue.take()();
        assert(delivered == static_cast<unsigned>(i + 1));
        io.remove(token);
    }
}
static void maskDuringCallback() {
    int sockets[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) == 0);
    SwIoDispatcher io;
    Queue queue;
    unsigned callbacks = 0;
    SwIoDispatcher::Token token = 0;
    token = io.watchFdReliable(sockets[0], SwIoDispatcher::Readable,
        [&](std::function<void()> task) { return queue.post(std::move(task)); },
        [&](uint32_t events) {
            if (++callbacks == 1) {
                assert(events & SwIoDispatcher::Readable);
                char c;
                assert(::read(sockets[0], &c, 1) == 1);
                assert(io.updateFd(token, SwIoDispatcher::Writable));
            } else {
                assert(events & SwIoDispatcher::Writable);
                io.remove(token); // Removal inside the callback cannot deadlock.
            }
        });
    assert(::write(sockets[1], "a", 1) == 1);
    queue.take()();
    queue.take()();
    assert(callbacks == 2);
    ::close(sockets[0]); ::close(sockets[1]);
}
static void concurrentUpdatesAndDelivery() {
    Event event;
    Queue queue;
    SwIoDispatcher io;
    std::atomic<uint64_t> received{0};
    std::atomic<bool> done{false};
    const auto token = event.watch(io, [&](std::function<void()> task) { return queue.post(std::move(task)); },
        [&] {
            uint64_t count = 0;
            while (::read(event.fd, &count, sizeof(count)) == sizeof(count)) received.fetch_add(count);
        });
    assert(token);
    std::thread receiver([&] { while (!done.load()) queue.take()(); });
    std::thread updater([&] {
        for (int i = 0; i < 10000; ++i) {
            assert(io.updateFd(token, SwIoDispatcher::Readable | ((i % 2) ? SwIoDispatcher::Error : 0)));
            if (i % 16 == 0) std::this_thread::yield();
        }
    });
    std::thread sender([&] {
        for (int i = 0; i < 10000; ++i) {
            event.signal();
            if (i % 16 == 0) std::this_thread::yield();
        }
    });
    sender.join(); updater.join();
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (received.load() < 10000 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(received == 10000);
    io.remove(token);
    done = true;
    queue.post([] {});
    receiver.join();
}
static void reentrantDestruction() {
    Event first, second;
    SwIoDispatcher io;
    auto other = second.watch(io, {}, [] {});
    auto owned = std::shared_ptr<int>(new int(1), [&](int* value) { io.remove(other); delete value; });
    auto token = first.watch(io, {}, [owned] {});
    owned.reset();
    io.remove(token); // Destroying captured state may remove another watch.
    assert(!io.updateFd(other, SwIoDispatcher::Readable));

    std::atomic<bool> destroyed{false};
    auto* inlineDispatcher = new SwIoDispatcher;
    assert(first.watch(*inlineDispatcher, {}, [&, inlineDispatcher] {
        first.drain();
        delete inlineDispatcher; // Destruction on its own reactor thread.
        destroyed = true;
    }));
    first.signal();
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!destroyed.load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(destroyed);
}
#endif
int main() {
    pendingAndLifetime();
    backpressure();
#if !defined(_WIN32)
    exceptionsAndReuse();
    maskDuringCallback();
    concurrentUpdatesAndDelivery();
    reentrantDestruction();
#endif
    std::cout << "dispatcher regression checks passed\n";
}
