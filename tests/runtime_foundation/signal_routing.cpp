#include "SwRemoteObject.h"
#include "SwThread.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#endif

struct Payload { int value{0}; };
static std::atomic<int> decoded{0};
static const bool serializationRegistered = [] {
    SwAny::registerBinarySerialization<Payload>(
    [](SwAny::BinaryWriter& encoder, const Payload& value) { return encoder.writePOD(value.value); },
    [](SwAny::BinaryReader& decoder, Payload& value) { ++decoded; return decoder.readPOD(value.value); });
    return true;
}();
using namespace sw::ipc;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class Fn> static void waitFor(Fn fn, const char* message) {
    require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return fn(); }, 3000), message);
}

static void direct(Registry& registry) {
    SwIpcSignal<Payload> publisher(registry, "direct", 16, 64, DeliveryMode::Replay);
    SwIpcSignal<Payload> consumer(registry, "direct", 16, 64);
    std::vector<int> first, second;
    auto a = consumer.connect([&](Payload value) { first.push_back(value.value); }, false);
    auto b = consumer.connect([&](Payload value) { second.push_back(value.value); }, false);
    decoded = 0;
    for (int i = 0; i < 100; ++i) {
        require(publisher.publish(Payload{i}), "local cursor failed to release ring space");
        require(first.size() == static_cast<size_t>(i + 1) && second.size() == first.size(),
                "same-thread delivery was deferred or shared subscriber cursor");
    }
    require(first == second && decoded == 0, "local values passed through wire decoder");
    detail::LoopPoller::instance().dispatch();
    require(first.size() == 100 && second.size() == 100, "IPC duplicated native delivery");
    a.stop();
    require(publisher.publish(Payload{100}) && first.size() == 100 && second.size() == 101,
            "disconnect affected another subscriber");

    SwIpcSignal<Payload> nested(registry, "nested", 16, 64, DeliveryMode::Replay);
    std::vector<int> ordered;
    auto reentrant = nested.connect([&](Payload value) {
        ordered.push_back(value.value);
        if (value.value == 1) require(nested.publish(Payload{2}), "recursive publish failed");
    }, false);
    decoded = 0;
    require(nested.publish(Payload{1}), "outer publish failed");
    require(ordered == std::vector<int>({1, 2}) && decoded == 0, "reentrant delivery lost native snapshot/order");
}

static void queued(Registry& registry) {
    SwIpcSignal<Payload> events(registry, "queued", 16, 64, DeliveryMode::Replay);
    SwIpcSignal<Payload> state(registry, "queued_state", 1, 64, DeliveryMode::LatestOnly);
    SwObject receiver;
    SwThread worker;
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    std::atomic_bool blocked{false};
    std::atomic<int> eventCount{0}, stateCount{0}, stateValue{-1};
    std::atomic_bool correctThread{true}, correctOrder{true};
    std::vector<int> received;
    auto a = events.connect(&receiver, [&](Payload value) {
        if (std::this_thread::get_id() != worker.threadId()) correctThread = false;
        if (value.value != eventCount.load()) correctOrder = false;
        ++eventCount;
    }, false);
    auto b = state.connect(&receiver, [&](Payload value) { stateValue = value.value; ++stateCount; }, false);
    require(worker.start(), "worker start failed");
    receiver.moveToThread(&worker);
    require(worker.postTask([&] {
        blocked = true;
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return release; });
    }), "worker blocker rejected");
    waitFor([&] { return blocked.load(); }, "worker not blocked");
    decoded = 0;
    bool publishes = true;
    for (int i = 0; i < 40; ++i) {
        publishes = events.publish(Payload{i}) && publishes;
        publishes = state.publish(Payload{i}) && publishes;
    }
    const bool deferred = eventCount == 0 && stateCount == 0;
    { std::lock_guard<std::mutex> lock(mutex); release = true; }
    cv.notify_all();
    waitFor([&] { return eventCount == 40 && stateCount == 1; }, "queued work lost or LatestOnly not coalesced");
    a.stop(); b.stop(); worker.quit(); worker.wait();
    require(publishes && deferred && correctThread && correctOrder && stateValue == 39 && decoded == 0,
            "queued transport violated affinity/order/native value delivery");
}

static void lifetime(Registry& registry) {
    SwIpcSignal<int> signal(registry, "lifetime");
    auto receiver = std::unique_ptr<SwObject>(new SwObject);
    int calls = 0;
    auto connection = signal.connect(receiver.get(), [&](int) { ++calls; }, false);
    receiver.reset();
    require(signal.publish(1) && calls == 0, "destroyed context received callback");
    auto timed = signal.connect([&](int) { ++calls; }, false, 10);
    require(SwEventLoop::waitUntil([] { return false; }, 30) == false, "timeout wait failed");
    signal.publish(2);
    require(calls == 0, "expired native subscription remained active");
}

class UnavailableReceiver : public SwObject {
public:
    std::thread::id affinityThreadId() const override { return {}; }
    bool postToAffinity(std::function<void()>) const override { return false; }
};
static void publicationResult(Registry& registry) {
    SwIpcSignal<int> signal(registry, "publication_result", 2, 64, DeliveryMode::Replay);
    UnavailableReceiver unavailable;
    int committed = 0, observed = 0, unavailableCalls = 0;
    auto remoteThread = signal.connect(&unavailable, [&](int) { ++unavailableCalls; }, false);
    auto local = signal.connect([&](int value) {
        require(committed == value, "observer ran before publication commit");
        ++observed;
    }, false);
    for (int value = 1; value <= 3; ++value) {
        require(signal.publishWithCommit([&] { committed = value; }, value),
                "accepted SHM publication reported failure after a rejected local queue");
        int latest = 0;
        require(signal.readLatest(latest) && latest == value && observed == value,
                "publication result disagrees with committed state or active receiver");
    }
    require(unavailableCalls == 0, "unavailable receiver callback ran on the publisher thread");
}

static void initialAndMode(Registry& registry) {
    SwIpcSignal<int> early(registry, "declared_later", 16, 4096);
    std::vector<int> observed;
    auto connection = early.connect([&](int value) { observed.push_back(value); }, false);
    SwIpcSignal<int> state(registry, "declared_later", 1, 64, DeliveryMode::LatestOnly);
    require(state.publish(1), "late declaration publish failed");
    require(early.defaultMode() == DeliveryMode::LatestOnly && observed == std::vector<int>({1}),
            "subscriber-first mode differs from publisher declaration");

    SwIpcSignal<int> backlog(registry, "initial", 16, 64, DeliveryMode::Replay);
    backlog.publish(10); backlog.publish(20); backlog.publish(30);
    std::vector<int> replay, latest, fresh;
    auto a = backlog.connect([&](int value) { replay.push_back(value); }, true);
    auto b = backlog.connect([&](int value) { latest.push_back(value); }, DeliveryMode::LatestOnly, true);
    auto c = backlog.connect([&](int value) { fresh.push_back(value); }, false);
    waitFor([&] { return replay.size() == 3 && latest.size() == 1; }, "initial backlog missing");
    require(replay == std::vector<int>({10, 20, 30}) && latest[0] == 30 && fresh.empty(),
            "fireInitial semantics changed");
    require(backlog.publish(40) && fresh == std::vector<int>({40}), "fireInitial=false missed next event");
}

class Node : public SwRemoteObject {
public:
    Node(const SwString& domain, const SwString& name) : SwRemoteObject(domain, "test", name) {}
    SW_IPC_SIGNAL(event, int);
};
static void namedConnection(const SwString& domain) {
    Node publisher(domain, "source"), receiver(domain, "receiver");
    std::vector<int> received;
    auto token = receiver.ipcConnectT("test/source#event", [&](int value) { received.push_back(value); }, false);
    require(token != 0 && publisher.event.publish(42) && received == std::vector<int>({42}),
            "ipcConnect did not route directly by name");
    require(receiver.ipcDisconnect(token), "ipcDisconnect failed");
    publisher.event.publish(43);
    require(received.size() == 1, "ipcDisconnect retained native callback");

    bool mismatchRejected = false;
    try {
        receiver.ipcConnectT("test/source#event", [](SwString) {}, false);
    } catch (const std::runtime_error&) { mismatchRejected = true; }
    require(mismatchRejected, "named connection hid an existing signal type mismatch");

    SwString large;
    auto pending = receiver.ipcConnectT("test/late#state", [&](SwString value) { large = value; }, false);
    Registry lateRegistry(domain, "test/late");
    SwIpcSignal<SwString> late(lateRegistry, "state", 1, 8192, DeliveryMode::LatestOnly);
    require(late.capacity() == 1 && late.maxBytes() == 8192, "early named connection guessed channel dimensions");
    const SwString text(std::string(7000, 'x'));
    require(late.publish(text) && large == text, "early named connection missed direct first emission");
    require(receiver.ipcDisconnect(pending), "pending connection could not disconnect");

    std::atomic<int> otherThread{0};
    auto threadToken = receiver.ipcConnectT("test/thread_late#event", [&](int) { ++otherThread; }, false);
    std::atomic_bool produced{false}, accepted{true};
    std::thread producer([&] {
        SwCoreApplication app;
        Registry source(domain, "test/thread_late");
        SwIpcSignal<int> signal(source, "event", 2, 64, DeliveryMode::Replay);
        for (int i = 0; i < 30; ++i) if (!signal.publish(i)) accepted = false;
        produced = true;
    });
    waitFor([&] { return produced.load() && otherThread == 30; }, "early cross-thread named connection lost burst");
    producer.join();
    require(accepted && receiver.ipcDisconnect(threadToken), "cross-thread setup backpressure or disconnect failure");
}

#ifndef _WIN32
static int child(const char* domain, int ready, int result) {
    Registry registry(domain, "channels");
    SwIpcSignal<int> signal(registry, "external", 16, 64, DeliveryMode::Replay);
    std::vector<int> values;
    auto connection = signal.connect([&](int value) { values.push_back(value); }, false);
    char byte = 'R';
    require(::write(ready, &byte, 1) == 1, "child readiness failed");
    waitFor([&] { return values.size() >= 3; }, "external receiver missed events");
    byte = values == std::vector<int>({10, 20, 30}) ? 'Y' : 'N';
    require(::write(result, &byte, 1) == 1, "child result failed");
    return byte == 'Y' ? 0 : 1;
}
static void external(const SwString& domain, const char* executable, Registry& registry) {
    SwIpcSignal<int> signal(registry, "external", 16, 64, DeliveryMode::Replay);
    std::vector<int> local;
    auto connection = signal.connect([&](int value) { local.push_back(value); }, false);
    int ready[2], result[2];
    require(::pipe(ready) == 0 && ::pipe(result) == 0, "pipe failed");
    const auto readyFd = std::to_string(ready[1]), resultFd = std::to_string(result[1]);
    pid_t pid = ::fork();
    require(pid >= 0, "fork failed");
    if (!pid) {
        ::execl(executable, executable, "--child", domain.toStdString().c_str(), readyFd.c_str(), resultFd.c_str(), nullptr);
        ::_exit(127);
    }
    ::close(ready[1]); ::close(result[1]);
    char byte = 0;
    require(::read(ready[0], &byte, 1) == 1, "external receiver not ready");
    for (int value : {10, 20, 30}) require(signal.publish(value), "mixed local/IPC publish failed");
    require(local == std::vector<int>({10, 20, 30}), "local delivery waited for IPC");
    require(::read(result[0], &byte, 1) == 1 && byte == 'Y', "remote delivery failed");
    int status = 0;
    require(::waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0, "child failed");
    ::close(ready[0]); ::close(result[0]);
    detail::LoopPoller::instance().dispatch();
    require(local.size() == 3, "mixed IPC duplicated native values");
}
#endif

int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
#ifndef _WIN32
        if (argc == 5 && std::string(argv[1]) == "--child") return child(argv[2], std::stoi(argv[3]), std::stoi(argv[4]));
#endif
        const char* configured = std::getenv("VISIONMAX_TEST_DOMAIN");
        const SwString domain = configured ? configured : ("routing_" + SwString::number(detail::currentPid()));
        const auto work = std::filesystem::temp_directory_path() / domain.toStdString();
        std::filesystem::create_directories(work);
        SwRemoteObject::ConfigRootScope config(SwString((work / "config").string()));
        Registry registry(domain, "channels");
        direct(registry); queued(registry); lifetime(registry); publicationResult(registry);
        initialAndMode(registry); namedConnection(domain);
#ifndef _WIN32
        external(domain, std::filesystem::absolute(argv[0]).c_str(), registry);
#endif
        std::cout << "PASS native direct/queued/IPC routing, ordering, lifetime and replay\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
