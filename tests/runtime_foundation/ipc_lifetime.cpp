#include "SwIpcRpc.h"
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>

using namespace sw::ipc;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool exists(const SwString& name) {
    const int fd = ::shm_open(name.c_str(), O_RDONLY, 0);
    if (fd >= 0) ::close(fd);
    return fd >= 0;
}
using Queue = RingQueue<100, int>;
void noWireForNative(const SwString& domain) {
    require(rpcQueueCapacity() == 100, "default RPC capacity is not 100");
    const auto request = detail::make_shm_name(domain, "native", rpcRequestQueueName("echo"));
    const auto response = detail::make_shm_name(domain, "native", rpcResponseQueueName("echo", detail::currentPid()));
    RpcMethodClient<int, int> client(domain, "native", "echo");
    require(!exists(request) && !exists(response), "unused client allocated RPC queues");
    auto server = NativeRpcEndpoint<int, int>::expose(domain, "native", "echo", nullptr,
        [](RpcContext, int value) { return value + 1; });
    auto result = client.callResult(40, 200);
    require(result.ok && result.value == 41, "native synchronous call failed");
    bool done = false;
    client.callAsyncResult(41, [&](const auto& r) { done = r.ok && r.value == 42; }, 200);
    require(SwEventLoop::waitUntil([&] { return done; }, 500), "native async call failed");
    require(!exists(request) && !exists(response), "native call allocated RPC queues");
    server.stop();
    auto id = client.callAsyncResult(42, [](const auto&) {}, 500);
    require(exists(request) && exists(response), "remote fallback did not allocate queues lazily");
    require(client.cancel(id) && client.pendingCount() == 0, "lazy transport cancellation failed");
}
void lastClose(const SwString& domain) {
    Registry registry(domain, "lifetime");
    auto first = std::make_shared<Queue>(registry, "shared");
    const auto name = first->shmName();
    auto second = std::make_shared<Queue>(registry, "shared");
    require(sizeof(Queue::Layout) < 420000, "100-message queue exceeds budget");
    first.reset();
    detail::SharedMemoryLease::recoverAbandoned();
    require(exists(name) && second->push(42), "collector removed a live queue");
    second.reset();
    require(!exists(name), "last mapping did not unlink its queue");
    SwString dynamicName;
    {
        RingQueueDynamic<int> dynamic(registry, "dynamic", 2, 16);
        dynamicName = dynamic.shmName();
        auto subscription = dynamic.connect([](int) {}, false);
        require(dynamic.push(42), "dynamic queue cannot publish");
    }
    require(!exists(dynamicName), "dynamic queue survived last close");
}
void boundedRestarts(const SwString& domain) {
    Registry registry(domain, "restarts");
    for (int i = 0; i < 20; ++i) {
        const auto signal = "reply_" + SwString::number(i);
        const auto name = detail::make_shm_name(domain, "restarts", signal);
        const auto child = ::fork(); require(child >= 0, "restart fork failed");
        if (!child) {
            try { Queue queue(registry, signal); ::_exit(0); }
            catch (...) { ::_exit(1); }
        }
        int status;
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "restart child failed");
        detail::SharedMemoryLease::recoverAbandoned();
        require(!exists(name), "repeated crashes accumulated a queue");
    }
}
void crashedOwner(const SwString& domain) {
    Registry registry(domain, "crash");
    auto live = std::make_shared<Queue>(registry, "live");
    const auto liveName = live->shmName();
    const auto crashedName = detail::make_shm_name(domain, "crash", "abandoned");
    int ready[2]; require(::pipe(ready) == 0, "pipe failed");
    const auto child = ::fork(); require(child >= 0, "fork failed");
    if (!child) {
        ::close(ready[0]);
        try {
            Queue abandoned(registry, "abandoned");
            if (::write(ready[1], "x", 1) != 1) ::_exit(2);
            for (;;) ::pause();
        } catch (...) { ::_exit(3); }
    }
    ::close(ready[1]); char byte;
    const bool readyOk = ::read(ready[0], &byte, 1) == 1; ::close(ready[0]);
    // Even a fork-inherited mapping must retain the queue after its parent
    // releases its own copy of the lease.
    live.reset();
    detail::SharedMemoryLease::recoverAbandoned();
    const bool protectedLive = exists(liveName) && exists(crashedName);
    ::kill(child, SIGKILL); int status; ::waitpid(child, &status, 0);
    require(readyOk && protectedLive, "collector removed a child's live mapping");
    detail::SharedMemoryLease::recoverAbandoned();
    require(!exists(liveName) && !exists(crashedName), "crashed process left allocated queues");
    Queue replacement(registry, "abandoned");
    require(replacement.push(73), "queue did not recover after crash");
}
}
int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        const SwString domain = "swlease_" + SwString::number(detail::currentPid());
        noWireForNative(domain);
        lastClose(domain);
        crashedOwner(domain);
        boundedRestarts(domain);
        std::cout << "PASS: lazy native/remote RPC, capacity, last-close, live/forked leases, crash recovery\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
