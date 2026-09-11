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
        require(fd >= 0, "missing registry");
        void* memory = ::mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        require(memory != MAP_FAILED, "mmap failed");
        data = static_cast<Layout*>(memory);
    }
    ~Mapping() { ::munmap(data, sizeof(Layout)); }
    Layout* data;
};

template<class Fn> void inChild(Fn fn) {
    const auto child = ::fork();
    require(child >= 0, "fork failed");
    if (!child) {
        try { fn(); ::_exit(0); } catch (...) { ::_exit(1); }
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (::waitpid(child, &status, WNOHANG) == 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(child, SIGKILL); ::waitpid(child, &status, 0);
            throw std::runtime_error("registry operation blocked after mutex owner was killed");
        }
        ::usleep(10000);
    }
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "registry recovery failed");
}

template<class Layout, class Damage> void abandon(const SwString& name, Damage damage) {
    Mapping<Layout> mapping(name);
    int ready[2];
    require(::pipe(ready) == 0, "pipe failed");
    const auto holder = ::fork();
    require(holder >= 0, "fork failed");
    if (!holder) {
        ::close(ready[0]);
        if (::pthread_mutex_lock(&mapping.data->mtx) != 0) ::_exit(2);
        damage(*mapping.data);
        const char byte = 'x';
        if (::write(ready[1], &byte, 1) != 1) ::_exit(3);
        for (;;) ::pause();
    }
    ::close(ready[1]);
    char byte = 0;
    const auto received = ::read(ready[0], &byte, 1);
    ::close(ready[0]);
    ::kill(holder, SIGKILL);
    int status = 0; ::waitpid(holder, &status, 0);
    require(received == 1, "holder did not acquire the mutex");
}

void appsRecovery(const SwString& domain) {
    using Layout = detail::AppLayout<64>;
    const SwString name = "/" + domain + "_apps";
    struct Unlink { SwString name; ~Unlink() { ::shm_unlink(name.c_str()); } } cleanup{name};
    auto* memory = static_cast<Layout*>(detail::openRegistryMemory_<Layout>(name));
    ::munmap(memory, sizeof(Layout));
    Mapping<Layout> mapping(name);
    mapping.data->count = 1;
    mapping.data->apps[0].pidCount = 1;
    mapping.data->apps[0].pids[0] = detail::currentPid();
    abandon<Layout>(name, [](auto& layout) { layout.count = 100; layout.apps[0].pidCount = 100; });
    bool recovered = false;
    require(detail::lockRegistryMemory_(mapping.data, true, [&](auto* layout) {
        recovered = true;
        require(layout->count == 64 && layout->apps[0].pidCount == 16, "interrupted app counters not bounded");
        require(layout->apps[0].pids[0] == detail::currentPid(), "live app lost during mutex recovery");
    }), "try-lock did not recover an abandoned app registry");
    require(recovered, "app recovery callback was skipped");
    inChild([&] {
        require(!detail::lockRegistryMemory_(mapping.data, true, [](auto*) {}), "try-lock acquired a live owner's mutex");
    });
    ::pthread_mutex_unlock(&mapping.data->mtx);
    require(detail::lockRegistryMemory_(mapping.data, true, [](auto*) {
        throw std::runtime_error("already-consistent mutex recovered twice");
    }), "app registry could not be locked after recovery");
    ::pthread_mutex_unlock(&mapping.data->mtx);
}

void concurrentCreation(const SwString& domain) {
    using Layout = detail::SubscribersLayout<512>;
    const SwString name = "/" + domain + "_create";
    struct Unlink { SwString name; ~Unlink() { ::shm_unlink(name.c_str()); } } cleanup{name};
    int gate[2]; require(::pipe(gate) == 0, "creation gate failed");
    std::vector<pid_t> children;
    for (int i = 0; i < 8; ++i) {
        const auto pid = ::fork(); require(pid >= 0, "creator fork failed");
        if (!pid) {
            ::close(gate[1]); char byte;
            if (::read(gate[0], &byte, 1) != 1) ::_exit(2);
            try {
                auto* layout = static_cast<Layout*>(detail::openRegistryMemory_<Layout>(name));
                detail::lockRegistryMemory_(layout, false, [](auto*) {});
                ++layout->entries[0].refCount;
                ::pthread_mutex_unlock(&layout->mtx);
                ::_exit(0);
            } catch (...) { ::_exit(3); }
        }
        children.push_back(pid);
    }
    ::close(gate[0]); require(::write(gate[1], "12345678", 8) == 8, "creation gate write failed"); ::close(gate[1]);
    bool success = true;
    for (const auto pid : children) {
        int status = 0;
        if (::waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) success = false;
    }
    require(success, "concurrent registry creator failed");
    Mapping<Layout> mapping(name);
    require(mapping.data->entries[0].refCount == 8, "concurrent initialization reset published data");
}

struct Cleanup {
    SwString domain;
    ~Cleanup() {
        // Every child has been reaped. Never acquire the deliberately abandoned
        // mutex during cleanup, including when running against the broken code.
        try {
            Mapping<detail::RegistryLayout<256>> mapping(detail::signalsRegistryNameForDomain_(domain));
            for (const auto& row : mapping.data->entries)
                if (row.shmName[0]) ::shm_unlink(row.shmName);
        } catch (...) {}
        ::shm_unlink(detail::signalsRegistryNameForDomain_(domain).c_str());
        ::shm_unlink(detail::subscribersRegistryNameForDomain_(domain).c_str());
    }
};

struct OldRegistry {
    SwString name;
    explicit OldRegistry(const SwString& domain)
      : name("/sw_ipc_subs_" + detail::sanitizeRegistrySuffix_(domain)) {
        using Layout = detail::SubscribersLayout<512>;
        const int fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        require(fd >= 0, "cannot create old registry fixture");
        const int allocated = detail::reserveSharedMemory_(fd, sizeof(Layout));
        ::close(fd); require(allocated == 0, "old registry allocation failed");
        Mapping<Layout> mapping(name);
        std::memset(mapping.data, 0, sizeof(Layout));
        mapping.data->magic = Layout::kMagic;
        mapping.data->version = 1;
        mapping.data->reserved = Layout::kTimeBaseTag;
        pthread_mutexattr_t attributes;
        ::pthread_mutexattr_init(&attributes);
        ::pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
        // The old protocol used a non-robust shared mutex.
        const int initialized = ::pthread_mutex_init(&mapping.data->mtx, &attributes);
        ::pthread_mutexattr_destroy(&attributes);
        require(initialized == 0, "old registry mutex init failed");
        abandon<Layout>(name, [](auto&) {});
    }
    ~OldRegistry() { ::shm_unlink(name.c_str()); }
};
}

int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        const SwString domain = "swcrash_" + SwString::number(detail::currentPid()) + "_" + SwString::number(detail::nowMs());
        Cleanup cleanup{domain};
        OldRegistry old(domain);
        inChild([&] {
            Registry startup(domain, "startup");
            SwIpcSignal<int> presence(startup, "presence");
            require(!shmRegistrySnapshot(domain).isEmpty(), "startup reused the abandoned old registry");
        });
        Registry registry(domain, "service");
        SwIpcSignal<int> signal(registry, "status");
        detail::SubscribersRegistryTable<>::registerSubscription(domain, "service", "status");

        abandon<detail::SubscribersLayout<512>>(detail::subscribersRegistryNameForDomain_(domain),
            [](auto& layout) { layout.count = 600; });
        inChild([&] {
            std::vector<uint32_t> pids;
            detail::SubscribersRegistryTable<>::listSubscriberPids(domain, "service", "status", pids);
            require(pids.size() == 1 && pids[0] == static_cast<uint32_t>(::getppid()), "live subscriber lost during recovery");
            require(!shmSubscribersSnapshot(domain).isEmpty(), "recovered subscriber registry unusable");
        });
        require(signal.publish(42), "publication failed after subscriber registry recovery");

        abandon<detail::RegistryLayout<256>>(detail::signalsRegistryNameForDomain_(domain),
            [](auto& layout) { layout.count = 300; });
        inChild([&] { require(!shmRegistrySnapshot(domain).isEmpty(), "live publisher lost during recovery"); });
        require(!shmRegistrySnapshot(domain).isEmpty(), "mutex not made consistent for later readers");
        appsRecovery(domain);
        concurrentCreation(domain);
        std::cout << "PASS: abandoned registry mutexes recover, interrupted counters are bounded, live peers survive, initialization is serialized\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
