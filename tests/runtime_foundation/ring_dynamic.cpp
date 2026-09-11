#include "SwSharedMemorySignal.h"
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class Fn> void rejects(Fn fn, const char* message) {
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, message);
}
using namespace sw::ipc;
using Ring = RingQueueDynamic<int>;

void replay(Registry& registry) {
    Ring queue(registry, "replay", 2);
    std::vector<int> first, second;
    auto a = queue.connect([&](int value) { first.push_back(value); }, false);
    auto b = queue.connect([&](int value) { second.push_back(value); }, false);
    require(queue.push(10) && queue.push(20) && !queue.push(30), "Replay does not protect unread slots");
    a.drain();
    require(first == std::vector<int>({10, 20}) && second.empty(), "subscription drain crossed cursors");
    require(!queue.push(30), "slow second subscription lost its cursor");
    a.stop(); b.drain();
    require(second == first && queue.push(30), "stopping sibling removed active cursor");
    b.drain();
    require(second == std::vector<int>({10, 20, 30}), "Replay omitted a message");
}
void latest(Registry& registry) {
    Ring queue(registry, "latest", 1, 16, false, DeliveryMode::LatestOnly);
    std::vector<int> received;
    auto subscription = queue.connect([&](int value) { received.push_back(value); }, false, 0,
                                       DeliveryMode::LatestOnly);
    for (int value = 1; value <= 100; ++value) require(queue.push(value), "LatestOnly stalled producer");
    subscription.drain();
    require(received == std::vector<int>({100}), "LatestOnly did not collapse backlog");
    int value = 0;
    require(queue.readLatest(value) && value == 100, "readLatest failed");
    Ring discovered(registry, "latest", 32, 4096, true);
    require(discovered.capacity() == 1 && discovered.maxPayload() == 16 &&
            discovered.defaultMode() == DeliveryMode::LatestOnly, "existing dimensions/mode not discovered");
    rejects([&] { Ring mismatch(registry, "latest", 32); }, "strict dimensions accepted mismatch");
    rejects([&] { RingQueueDynamic<SwString> mismatch(registry, "latest", 1, 16, true); }, "type mismatch accepted");
    uint32_t capacity = 0, payload = 0;
    DeliveryMode mode = DeliveryMode::Replay;
    require(Ring::inspect(queue.shmName(), detail::type_id<int>(), capacity, payload, mode) &&
            capacity == 1 && payload == 16 && mode == DeliveryMode::LatestOnly, "raw inspect failed");
    std::vector<uint8_t> bytes;
    uint64_t sequence = 0, origin = 0;
    require(Ring::readLatestBytes(queue.shmName(), detail::type_id<int>(), bytes, sequence, origin) &&
            sequence == 100 && origin == 0, "raw latest failed");
    require(Ring::publishBytes(queue.shmName(), detail::type_id<int>(), bytes.data(), bytes.size()), "raw publish failed");
    require(!Ring::publishBytes(queue.shmName(), detail::type_id<int>(), bytes.data(), 17), "oversized bytes accepted");
    require(queue.sequence() == 101 && queue.readLatest(value) && value == 100, "failed publish corrupted latest");
    auto initial = queue.connect([&](int v) { received.push_back(v); }, true, 0, DeliveryMode::LatestOnly);
    initial.drain();
    require(received == std::vector<int>({100, 100}), "LatestOnly initial delivery failed");
}
void publisherDeclaration(Registry& registry) {
    Ring provisional(registry, "first_reader", 1);
    std::vector<int> received;
    auto subscription = provisional.connect([&](int value) { received.push_back(value); }, false, 0,
                                            DeliveryMode::FollowPublisher);
    require(subscription.deliveryMode() == DeliveryMode::Replay, "provisional mode wrong");
    Ring publisher(registry, "first_reader", 1, 4096, true, DeliveryMode::LatestOnly);
    for (int value = 1; value <= 100; ++value)
        require(publisher.pushAs(DeliveryMode::LatestOnly, value), "early generic reader blocked state publisher");
    require(subscription.deliveryMode() == DeliveryMode::LatestOnly &&
            publisher.defaultMode() == DeliveryMode::LatestOnly, "producer declaration not visible");
    subscription.drain();
    require(received == std::vector<int>({100}), "generic reader did not follow publisher");
    auto replay = provisional.connect([](int) {}, false, 0, DeliveryMode::Replay);
    require(publisher.pushAs(DeliveryMode::LatestOnly, 101) && !publisher.pushAs(DeliveryMode::LatestOnly, 102),
            "explicit Replay cursor ignored by state publisher");
    replay.stop();
    require(publisher.pushAs(DeliveryMode::LatestOnly, 102), "stopped explicit cursor retained backpressure");
}
void mixed(Registry& registry) {
    Ring queue(registry, "mixed", 2);
    std::vector<int> replay, latest;
    auto a = queue.connect([&](int value) { replay.push_back(value); }, false);
    auto b = queue.connect([&](int value) { latest.push_back(value); }, false, 0, DeliveryMode::LatestOnly);
    require(queue.push(1) && queue.push(2) && !queue.push(3), "LatestOnly bypassed Replay backpressure");
    b.drain();
    require(latest == std::vector<int>({2}) && !queue.push(3), "LatestOnly discarded another cursor's messages");
    a.drain();
    require(replay == std::vector<int>({1, 2}) && queue.push(3), "Replay not preserved with mixed readers");
}
void initialAndNative(Registry& registry) {
    Ring queue(registry, "initial", 3);
    for (int value = 1; value <= 5; ++value) require(queue.push(value), "no-subscriber ring stalled");
    std::vector<int> received;
    auto backlog = queue.connect([&](int value) { received.push_back(value); });
    backlog.drain();
    require(received == std::vector<int>({3, 4, 5}), "late Replay backlog wrong");
    backlog.stop(); received.clear();
    auto dispatchMutex = std::make_shared<std::recursive_mutex>();
    int nativeCount = 0, decodedCount = 0;
    uint64_t cachedSequence = 0;
    auto native = queue.connectStamped([&](uint64_t, uint64_t, int value) {
        ++decodedCount; received.push_back(value);
    }, false, 0, DeliveryMode::Replay, dispatchMutex,
    [&](uint64_t sequence, uint64_t origin) -> std::function<void()> {
        if (sequence != cachedSequence || origin != 42) return {};
        return [&] { ++nativeCount; received.push_back(7); };
    });
    require(queue.push(6), "IPC preceding native failed");
    require(queue.pushStamped(42, cachedSequence, 7), "stamped publish failed");
    native.drain(); native.drain();
    require(received == std::vector<int>({6, 7}) && nativeCount == 1 && decodedCount == 1,
            "native drain lost order or duplicated delivery");
    int value = 0; uint64_t sequence = 0, origin = 0;
    require(queue.readLatestStamped(sequence, origin, value) && sequence == cachedSequence &&
            origin == 42 && value == 7, "origin/sequence not preserved");
}
void reentrantAndStop(Registry& registry) {
    Ring queue(registry, "reentrant", 2);
    std::vector<int> received;
    Ring::Subscription subscription;
    subscription = queue.connect([&](int value) {
        received.push_back(value);
        if (value == 1) { require(queue.push(2), "reentrant push failed"); subscription.drain(); }
        if (value == 2) subscription.stop();
    }, false);
    require(queue.push(1), "reentrant start failed");
    subscription.drain();
    require(received == std::vector<int>({1, 2}) && queue.push(3), "reentrant drain or self-stop failed");
}
void crossChannelCallbacks(Registry& registry) {
    Ring first(registry, "cross_a", 4), second(registry, "cross_b", 4);
    auto firstMutex = std::make_shared<std::recursive_mutex>();
    auto secondMutex = std::make_shared<std::recursive_mutex>();
    std::atomic<int> entered{0}, firstCount{0}, secondCount{0};
    std::atomic_bool succeeded{true};
    Ring::Subscription a, b;
    auto barrier = [&] {
        ++entered;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (entered.load() < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        require(entered.load() == 2, "cross-channel callbacks did not run concurrently");
    };
    a = first.connectStamped([&](uint64_t, uint64_t, int value) {
        ++firstCount;
        if (value == 1) { barrier(); require(second.push(2), "cross B push failed"); b.drain(); }
    }, false, 0, DeliveryMode::Replay, firstMutex);
    b = second.connectStamped([&](uint64_t, uint64_t, int value) {
        ++secondCount;
        if (value == 1) { barrier(); require(first.push(2), "cross A push failed"); a.drain(); }
    }, false, 0, DeliveryMode::Replay, secondMutex);
    require(first.push(1) && second.push(1), "cross-channel setup failed");
    std::thread one([&] { try { a.drain(); } catch (...) { succeeded = false; } });
    std::thread two([&] { try { b.drain(); } catch (...) { succeeded = false; } });
    one.join(); two.join();
    a.drain(); b.drain();
    require(succeeded && firstCount == 2 && secondCount == 2, "cross-channel delivery failed");
}
void concurrentCreation(Registry& registry) {
    std::vector<std::thread> threads;
    std::atomic_bool succeeded{true};
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&, i] {
            try { Ring queue(registry, "concurrent", 2); if (!queue.push(i)) succeeded = false; }
            catch (...) { succeeded = false; }
        });
    }
    for (auto& thread : threads) thread.join();
    Ring queue(registry, "concurrent", 2);
    require(succeeded && queue.sequence() == 8, "concurrent creation lost initialization or publications");
}
#ifndef _WIN32
void processes(Registry& registry) {
    Ring queue(registry, "processes", 3);
    std::vector<int> received;
    auto subscription = queue.connect([&](int value) { received.push_back(value); }, false);
    const pid_t writer = ::fork();
    require(writer >= 0, "fork failed");
    if (writer == 0) {
        Ring remote(registry, "processes", 3);
        const bool ok = remote.push(11) && remote.push(12);
        ::_exit(ok ? 0 : 1);
    }
    int status = 0;
    require(::waitpid(writer, &status, 0) == writer && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "remote publication failed");
    subscription.drain();
    require(received == std::vector<int>({11, 12}), "cross-process Replay failed");

    Ring abandoned(registry, "abandoned", 1);
    const pid_t reader = ::fork();
    require(reader >= 0, "fork reader failed");
    if (reader == 0) {
        auto leaked = abandoned.connect([](int) {}, false);
        ::_exit(abandoned.push(1) ? 0 : 1);
    }
    require(::waitpid(reader, &status, 0) == reader && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "dead-reader setup failed");
    require(abandoned.push(2), "dead subscriber permanently blocked publisher");
}
void pendingMappings(Registry& registry) {
    for (bool reserved : {false, true}) {
        const SwString signal = reserved ? "pending_reserved" : "pending_empty";
        const SwString name = detail::make_shm_name(registry.domain(), registry.object(), signal) + "_r3";
        const int descriptor = ::shm_open(name.toStdString().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        require(descriptor >= 0, "cannot create pending mapping fixture");
        if (reserved) require(::ftruncate(descriptor, 16384) == 0, "cannot reserve pending mapping fixture");
        ::close(descriptor);
        uint32_t capacity = 0, bytes = 0;
        DeliveryMode mode = DeliveryMode::Replay;
        require(!Ring::inspectExisting(name, detail::type_id<int>(), capacity, bytes, mode),
                "uninitialized publisher mapping was rejected instead of deferred");
        Ring publisher(registry, signal, 2, 16);
        require(Ring::inspectExisting(name, detail::type_id<int>(), capacity, bytes, mode) &&
                capacity == 2 && bytes == 16, "publisher did not initialize pending mapping");
        int latest = 0;
        require(publisher.push(42) && publisher.readLatest(latest) && latest == 42,
                "initialized pending mapping cannot publish/read");
    }
}
void invalidMappings(Registry& registry) {
    const SwString name = detail::make_shm_name(registry.domain(), registry.object(), "bad") + "_r3";
    const int descriptor = ::shm_open(name.toStdString().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    require(descriptor >= 0, "cannot create malformed mapping");
    require(::ftruncate(descriptor, 8) == 0, "truncate fixture failed");
    ::close(descriptor);
    rejects([&] { Ring invalid(registry, "bad", 1); }, "truncated mapping accepted");
    uint32_t capacity = 0, bytes = 0;
    DeliveryMode mode = DeliveryMode::Replay;
    rejects([&] { Ring::inspectExisting(name, detail::type_id<int>(), capacity, bytes, mode); },
            "truncated nonempty mapping treated as pending");
    ::shm_unlink(name.toStdString().c_str());
    const int legacy = ::shm_open(name.toStdString().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    require(legacy >= 0 && ::ftruncate(legacy, 8192) == 0, "legacy mapping fixture failed");
    const uint32_t oldVersion[] = {0x51554431u, 2u};
    require(::write(legacy, oldVersion, sizeof oldVersion) == sizeof oldVersion, "legacy header write failed");
    ::close(legacy);
    rejects([&] { Ring invalid(registry, "bad", 1); }, "legacy ring layout accepted");
    ::shm_unlink(name.toStdString().c_str());
}
#endif
}
int main(int argc, char** argv) {
    const SwString domain = "ring_dynamic_" + SwString::number(sw::ipc::detail::currentPid()) + "_" +
        SwString::number(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        SwCoreApplication app(argc, argv);
        sw::ipc::Registry registry(domain, "test");
        replay(registry); latest(registry); publisherDeclaration(registry); mixed(registry); initialAndNative(registry); reentrantAndStop(registry);
        crossChannelCallbacks(registry); concurrentCreation(registry);
#ifndef _WIN32
        processes(registry); pendingMappings(registry); invalidMappings(registry);
#endif
        std::cout << "ring_dynamic: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ring_dynamic: " << error.what() << '\n';
        return 1;
    }
}
