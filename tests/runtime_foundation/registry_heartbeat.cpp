#include "SwSharedMemorySignal.h"
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>

using namespace sw::ipc;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Layout> class Mapping {
public:
    explicit Mapping(const SwString& name) {
        const int fd = ::shm_open(name.c_str(), O_RDWR, 0);
        require(fd >= 0, "cannot open test registry");
        auto* memory = ::mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        require(memory != MAP_FAILED, "cannot map test registry");
        data = static_cast<Layout*>(memory);
    }
    ~Mapping() { ::munmap(data, sizeof(Layout)); }
    template<class Fn> void locked(Fn fn) {
        require(::pthread_mutex_lock(&data->mtx) == 0, "cannot lock test registry");
        try { fn(*data); } catch (...) { ::pthread_mutex_unlock(&data->mtx); throw; }
        ::pthread_mutex_unlock(&data->mtx);
    }
    Layout* data;
};
using Signals = Mapping<detail::RegistryLayout<256>>;
using Subscribers = Mapping<detail::SubscribersLayout<512>>;
const SwString marker("__config__|video");

bool visible(const SwString& domain) {
    const auto rows = shmRegistrySnapshot(domain);
    for (const auto& value : rows) {
        const auto row = value.toObject();
        if (row["object"].toString() == "video" && row["signal"].toString() == marker)
            return row["pid"].toInt() == int(detail::currentPid());
    }
    return false;
}
void expire(const SwString& domain) {
    Signals table(detail::signalsRegistryNameForDomain_(domain));
    table.locked([](auto& layout) {
        for (uint32_t i = 0; i < layout.count; ++i)
            if (layout.entries[i].pid == detail::currentPid())
                layout.entries[i].lastSeenMs = detail::nowMs() - 16000;
    });
    Subscribers subscribers(detail::subscribersRegistryNameForDomain_(domain));
    subscribers.locked([](auto& layout) {
        for (uint32_t i = 0; i < layout.count; ++i)
            if (layout.entries[i].subPid == detail::currentPid())
                layout.entries[i].lastSeenMs = detail::nowMs() - 16000;
    });
}
int child(const char* executable, const SwString& domain, const char* mode) {
    const auto pid = ::fork();
    require(pid >= 0, "fork failed");
    if (pid == 0) {
        ::execl(executable, executable, mode, domain.c_str(), nullptr);
        ::_exit(127);
    }
    int status = 0;
    require(::waitpid(pid, &status, 0) == pid && WIFEXITED(status), "observer failed");
    return WEXITSTATUS(status);
}
struct Cleanup {
    SwString domain;
    ~Cleanup() {
        try {
            Signals table(detail::signalsRegistryNameForDomain_(domain));
            table.locked([](auto& layout) {
                for (const auto& row : layout.entries)
                    if (row.shmName[0]) ::shm_unlink(row.shmName);
            });
            ::shm_unlink(detail::signalsRegistryNameForDomain_(domain).c_str());
            ::shm_unlink(detail::subscribersRegistryNameForDomain_(domain).c_str());
        } catch (...) {}
    }
};
}
int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        SwString launchDomain;
        for (int i = 1; i < argc; ++i) {
            const SwString argument(argv[i]);
            if (argument.startsWith("--sys=")) launchDomain = argument.mid(6);
        }
        if (!launchDomain.isEmpty()) {
            // SwLaunch fixture: stop only outside IPC locks, so the test
            // isolates a delayed runtime heartbeat from an abandoned mutex.
            Registry registry(launchDomain, "video");
            SwIpcSignal<int> presence(registry, marker);
            SwTimer timer(50);
            SwObject::connect(&timer, &SwTimer::timeout, [] {
                if (::unlink("pause") == 0) {
                    std::cout << "pausing pid=" << detail::currentPid() << std::endl;
                    ::raise(SIGSTOP);
                    std::cout << "resumed pid=" << detail::currentPid() << std::endl;
                }
            }, DirectConnection);
            timer.start();
            std::cout << "ready pid=" << detail::currentPid() << std::endl;
            return app.exec();
        }
        if (argc == 3) {
            const SwString domain(argv[2]);
            if (std::string(argv[1]) == "observe") {
                require(shmRegistrySnapshot(domain).isEmpty(), "expired service still advertised online");
                require(shmSubscribersSnapshot(domain).isEmpty(), "expired subscribers still advertised online");
            } else if (std::string(argv[1]) == "open") {
                Registry registry(domain, "video");
                SwIpcSignal<int> reader(registry, marker);
                Signals table(detail::signalsRegistryNameForDomain_(domain));
                table.locked([](auto& layout) {
                    for (uint32_t i = 0; i < layout.count; ++i) {
                        const auto& row = layout.entries[i];
                        if (SwString(row.signal) == marker)
                            require(row.pid != detail::currentPid(), "reader stole expired live publisher ownership");
                    }
                });
            } else if (std::string(argv[1]) == "populate") {
                for (int i = 0; i < 256; ++i) {
                    const SwString name = "signal_" + SwString::number(i);
                    detail::RegistryTable<>::registerSignal(domain, "departing", name,
                        detail::make_shm_name(domain, "departing", name), detail::type_id<int>(), "int");
                }
                Signals table(detail::signalsRegistryNameForDomain_(domain));
                table.locked([](auto& layout) { require(layout.count == 256, "test registry was not filled"); });
            } else {
                throw std::runtime_error("unknown child mode");
            }
            return 0;
        }
        const SwString domain = "swregistry_" + SwString::number(detail::currentPid()) + "_" +
            SwString::number(detail::nowMs());
        Cleanup cleanup{domain};
        Registry registry(domain, "video");
        SwIpcSignal<int> publisher(registry, marker);
        // Two logical subscriptions must retain their independent lifetimes
        // across a heartbeat gap, including an unsubscribe during that gap.
        detail::SubscribersRegistryTable<>::registerSubscription(domain, "video", marker);
        detail::SubscribersRegistryTable<>::registerSubscription(domain, "video", marker);
        require(visible(domain), "publisher not advertised initially");
        expire(domain);
        require(child(argv[0], domain, "observe") == 0, "remote expiry observation failed");
        require(SwEventLoop::waitUntil([&] { return visible(domain); }, 2500),
                "heartbeat never restored publisher after observer expired it");
        require(SwEventLoop::waitUntil([&] { return !shmSubscribersSnapshot(domain).isEmpty(); }, 2500),
                "heartbeat never restored subscriptions");
        expire(domain);
        require(child(argv[0], domain, "open") == 0, "remote reader took live ownership");
        detail::SubscribersRegistryTable<>::unregisterSubscription(domain, "video", marker);
        std::vector<uint32_t> pids;
        detail::SubscribersRegistryTable<>::listSubscriberPids(domain, "video", marker, pids);
        require(pids.size() == 1 && pids[0] == detail::currentPid(), "expired subscriber cannot receive wakeup");
        require(SwEventLoop::waitUntil([&] { return !shmSubscribersSnapshot(domain).isEmpty(); }, 2500),
                "remaining subscription lost after unsubscribe during expiry");
        const auto rows = shmSubscribersSnapshot(domain);
        require(rows.size() == 1 && rows[0].toObject()["refCount"].toInt() == 1,
                "subscription reference count changed during expiry");
        detail::SubscribersRegistryTable<>::unregisterSubscription(domain, "video", marker);
        require(shmSubscribersSnapshot(domain).isEmpty(), "last subscription not removed");
        require(child(argv[0], domain, "populate") == 0, "dead-publisher fixture failed");
        SwIpcSignal<int> replacement(registry, "after_exit");
        Signals table(detail::signalsRegistryNameForDomain_(domain));
        table.locked([](auto& layout) {
            require(layout.count == 3, "dead publishers not reclaimed before new registration");
            for (uint32_t i = 0; i < layout.count; ++i)
                require(layout.entries[i].pid == detail::currentPid(), "dead publisher remained in registry");
        });
        std::cout << "PASS: expiry, heartbeat recovery, live ownership, subscription lifetime and dead-publisher reclamation\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
