#include "SwRemoteObject.h"
#include "SwThread.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

struct SharedPayload {
    int value{0};
    std::vector<unsigned char> bytes;
    static std::atomic<int> copies, decoded;
    SharedPayload() = default;
    explicit SharedPayload(int id, size_t size = 256) : value(id), bytes(size, 7) {}
    SharedPayload(const SharedPayload& other) : value(other.value), bytes(other.bytes) { ++copies; }
    SharedPayload& operator=(const SharedPayload& other) {
        value = other.value; bytes = other.bytes; ++copies; return *this;
    }
    SharedPayload(SharedPayload&&) = default;
    SharedPayload& operator=(SharedPayload&&) = default;
};
std::atomic<int> SharedPayload::copies{0}, SharedPayload::decoded{0};
static const bool serializationRegistered = [] {
    SwAny::registerBinarySerialization<SharedPayload>(
    [](SwAny::BinaryWriter& encoder, const SharedPayload& value) {
        const uint32_t size = static_cast<uint32_t>(value.bytes.size());
        return encoder.writePOD(value.value) && encoder.writePOD(size) &&
               encoder.writeBytes(value.bytes.data(), size);
    },
    [](SwAny::BinaryReader& decoder, SharedPayload& value) {
        ++SharedPayload::decoded;
        uint32_t size = 0;
        if (!decoder.readPOD(value.value) || !decoder.readPOD(size) || size > decoder.cap - decoder.pos) return false;
        value.bytes.resize(size);
        return decoder.readBytes(value.bytes.data(), size);
    });
    return true;
}();
using namespace sw::ipc;
using Signal = SwIpcSignal<SharedPayload>;
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class Fn> static void waitFor(Fn ready, const char* message) {
    require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return ready(); }, 3000), message);
}
static Signal::SharedValues owned(int value, size_t size = 256) {
    return std::make_shared<const Signal::Values>(SharedPayload(value, size));
}
class Node : public SwRemoteObject {
public:
    Node(const SwString& domain, const SwString& name) : SwRemoteObject(domain, "sharing", name) {}
    SW_IPC_SIGNAL_SIZED(data, 4096, SharedPayload);
    bool send(const SharedPayload& value) { return emit data(value); }
    bool sendOwned(const Signal::SharedValues& values) { return emit data(values); }
};
static void directAndNamed(const SwString& domain) {
    Node publisher(domain, "source"), receiver(domain, "receiver");
    SwObject context;
    std::vector<const SharedPayload*> addresses;
    auto callback = [&](const SharedPayload& value) {
        require(value.value == 42 && value.bytes[0] == 7, "shared payload was mutated");
        addresses.push_back(&value);
    };
    auto first = publisher.data.connect(callback, false);
    auto second = publisher.data.connect(callback, false);
    const auto token = receiver.ipcConnectT("sharing/source#data", callback, false);
    auto scoped = receiver.ipcConnectScopedT("sharing/source", "data", &context, callback, false);
    require(token && scoped, "named/scoped connection failed");
    SharedPayload input(42);
    SharedPayload::copies = SharedPayload::decoded = 0;
    require(publisher.send(input), "ordinary SwRemoteObject emit failed");
    require(addresses.size() == 4 && SharedPayload::copies == 1 && SharedPayload::decoded == 0,
            "ordinary publication must capture once with no per-receiver copies");
    for (auto address : addresses)
        require(address == addresses[0] && address != &input, "receivers did not share the protected snapshot");
    addresses.clear();
    const auto shared = owned(42);
    SharedPayload::copies = 0;
    require(publisher.sendOwned(shared), "shared SwRemoteObject emit failed");
    require(addresses.size() == 4 && SharedPayload::copies == 0, "shared publication copied its payload");
    for (auto address : addresses)
        require(address == &std::get<0>(*shared), "shared publication did not preserve the original address");
    addresses.clear();
    auto mutableOwner = std::make_shared<Signal::Values>(SharedPayload(42));
    SharedPayload::copies = 0;
    require((emit publisher.data(mutableOwner)) && addresses.size() == 4 &&
            SharedPayload::copies == 0, "mutable tuple owner was not recognized");
    for (auto address : addresses)
        require(address == &std::get<0>(*mutableOwner), "mutable owner payload was copied");
    addresses.clear();
    auto movedOwner = owned(42);
    const auto originalAddress = &std::get<0>(*movedOwner);
    require((emit publisher.data(std::move(movedOwner))) && !movedOwner &&
            addresses.size() == 4 && SharedPayload::copies == 0, "rvalue owner was not moved");
    for (auto address : addresses)
        require(address == originalAddress, "rvalue owner lost its payload address");
    const auto acceptedSequence = publisher.data.raw().sequence();
    Signal::SharedValues nullOwner;
    require(!(emit publisher.data(nullOwner)) &&
            publisher.data.raw().sequence() == acceptedSequence, "empty emit owner was accepted");
    // A by-value slot still owns a private copy and can safely modify it.
    auto mutableSlot = publisher.data.connect([](SharedPayload value) { value.bytes[0] = 99; }, false);
    SharedPayload::copies = 0;
    require(publisher.data.publishShared(shared) && SharedPayload::copies == 1 &&
            std::get<0>(*shared).bytes[0] == 7, "by-value callback isolation changed");
    require(!publisher.data.publishShared({}), "empty owner was accepted");
    const auto sequence = publisher.data.raw().sequence();
    require(!publisher.data.publishShared(owned(42, 8192)) && publisher.data.raw().sequence() == sequence,
            "oversized shared publication changed the retained state");
    SharedPayload wire;
    require(publisher.data.readLatest(wire) && wire.value == 42 && wire.bytes[0] == 7,
            "shared publication changed the wire format");
    receiver.ipcDisconnect(token);
}
struct GatedWorker {
    SwThread thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool released{false};
    std::atomic_bool blocked{false};
    GatedWorker() {
        require(thread.start(), "worker start failed");
        require(thread.postTask([this] {
            std::unique_lock<std::mutex> lock(mutex);
            blocked = true;
            cv.wait(lock, [this] { return released; });
        }), "worker gate failed");
        waitFor([this] { return blocked.load(); }, "worker did not reach gate");
    }
    void release() {
        { std::lock_guard<std::mutex> lock(mutex); released = true; }
        cv.notify_all();
    }
    ~GatedWorker() { release(); thread.quit(); thread.wait(); }
};
static void queuedOwnership(Registry& registry) {
    Signal replay(registry, "queued_shared", 2, 4096, DeliveryMode::Replay);
    Signal latest(registry, "latest_shared", 1, 4096, DeliveryMode::LatestOnly);
    SwObject receiver;
    GatedWorker worker;
    receiver.moveToThread(&worker.thread);
    std::vector<const SharedPayload*> expected;
    std::atomic<int> events{0}, states{0};
    std::atomic_bool correct{true};
    auto first = replay.connect(&receiver, [&](const SharedPayload& value) {
        const int i = events.load();
        if (i >= 8 || value.value != i || &value != expected[i] ||
            std::this_thread::get_id() != worker.thread.threadId()) correct = false;
        ++events;
    }, false);
    auto second = latest.connect(&receiver, [&](const SharedPayload& value) {
        if (value.value != 7 || &value != expected[7]) correct = false;
        ++states;
    }, false);
    std::weak_ptr<const Signal::Values> oldest;
    SharedPayload::copies = SharedPayload::decoded = 0;
    for (int i = 0; i < 8; ++i) {
        auto value = owned(i);
        expected.push_back(&std::get<0>(*value));
        if (i == 0) oldest = value;
        require((emit replay(value)) && (emit latest(value)), "queued shared emit failed");
    }
    require(events == 0 && states == 0 && !oldest.expired(), "queued publication owner was released early");
    worker.release();
    waitFor([&] { return events == 8 && states == 1; }, "queued/replay/latest callbacks missing");
    // The batch owner may be released immediately after the last callback returns.
    waitFor([&] { return oldest.expired(); }, "completed queue retained an obsolete publication");
    require(correct && SharedPayload::copies == 0 && SharedPayload::decoded == 0,
            "queued path copied/decoded values or violated affinity/order");
}
static void yieldAndReentrancy(Registry& registry) {
    Signal signal(registry, "yield_shared", 1, 4096, DeliveryMode::LatestOnly);
    auto input = owned(1);
    const auto original = &std::get<0>(*input);
    std::weak_ptr<const Signal::Values> life = input;
    bool otherTask = false;
    std::vector<int> received;
    auto connection = signal.connect([&](const SharedPayload& value) {
        received.push_back(value.value);
        if (value.value != 1) return;
        SwTimer::singleShot(0, [&] {
            input.reset();
            require(signal.publishShared(owned(2)), "publication during yield failed");
            otherTask = true;
        });
        waitFor([&] { return otherTask; }, "slot did not yield to another Sw task");
        require(!life.expired() && &value == original && value.value == 1 && value.bytes[0] == 7,
                "yield/reentrant publication invalidated the active reference");
    }, false);
    SharedPayload::copies = SharedPayload::decoded = 0;
    require((emit signal(input)), "yielding shared emit failed");
    require(received == std::vector<int>({1, 2}) && life.expired() &&
            SharedPayload::copies == 0 && SharedPayload::decoded == 0,
            "yield/reentrancy copied, retained or reordered publications");
}
static void cancellation(Registry& registry) {
    Signal signal(registry, "cancel_shared", 1, 4096, DeliveryMode::LatestOnly);
    auto receiver = std::unique_ptr<SwObject>(new SwObject);
    GatedWorker worker;
    receiver->moveToThread(&worker.thread);
    std::atomic<int> calls{0};
    auto connection = signal.connect(receiver.get(), [&](const SharedPayload&) { ++calls; }, false);
    auto value = owned(1);
    std::weak_ptr<const Signal::Values> life = value;
    require(signal.publishShared(value), "cancel fixture failed");
    value.reset();
    require(signal.publishShared(owned(2)), "cancel replacement failed");
    // LatestOnly releases the replaced owner before the target thread resumes.
    require(life.expired(), "LatestOnly retained the replaced payload");
    receiver.reset();
    connection.stop();
    worker.release();
    worker.thread.quit();
    worker.thread.wait();
    require(calls == 0, "destroyed receiver received queued data");
}

static void affinityDuringBatch(Registry& registry) {
    Signal signal(registry, "moving_shared", 2, 4096, DeliveryMode::Replay);
    SwObject receiver;
    SwThread next;
    require(next.start(), "second worker start failed");
    {
        GatedWorker first;
        receiver.moveToThread(&first.thread);
        std::atomic<int> calls{0};
        std::atomic_bool correct{true};
        std::vector<const SharedPayload*> expected;
        auto connection = signal.connect(&receiver, [&](const SharedPayload& value) {
            const auto target = value.value == 0 ? first.thread.threadId() : next.threadId();
            if (value.value != calls.load() || &value != expected[value.value] ||
                std::this_thread::get_id() != target) correct = false;
            if (value.value == 0) receiver.moveToThread(&next);
            ++calls;
        }, false);
        SharedPayload::copies = SharedPayload::decoded = 0;
        for (int i = 0; i < 4; ++i) {
            auto value = owned(i);
            expected.push_back(&std::get<0>(*value));
            require(signal.publishShared(value), "moving publication failed");
        }
        first.release();
        waitFor([&] { return calls == 4; }, "batch did not follow receiver affinity");
        require(correct && SharedPayload::copies == 0 && SharedPayload::decoded == 0,
                "moving a queued batch copied values or lost ownership/order");
    }
    next.quit();
    next.wait();
}

static void tupleAndEmpty(Registry& registry) {
    SwIpcSignal<int, SwString> pair(registry, "pair", 2, 4096);
    using Pair = SwIpcSignal<int, SwString>;
    auto value = std::make_shared<const Pair::Values>(42, SwString("shared"));
    bool called = false;
    auto connection = pair.connect([&](const int& number, const SwString& text) {
        called = &number == &std::get<0>(*value) && &text == &std::get<1>(*value);
    }, false);
    require((emit pair(value)) && called, "multiple argument references were not preserved");
    SwIpcSignal<> empty(registry, "empty");
    int calls = 0;
    auto noArgs = empty.connect([&] { ++calls; }, false);
    require((emit empty(std::make_shared<const SwIpcSignal<>::Values>())) &&
            (emit empty()) && calls == 2, "zero argument signal changed");
    SwIpcSignal<int> scalar(registry, "scalar_braces");
    int observed = -1;
    auto integer = scalar.connect([&](int number) { observed = number; }, false);
    require((emit scalar({})) && observed == 0, "ordinary braced emit became ambiguous");
    require((emit scalar(42)) && observed == 42, "ordinary scalar emit changed");
    static_assert(!std::is_invocable<Signal&, std::shared_ptr<const std::tuple<SwString>>>::value,
                  "an unrelated shared owner must not select the native publication overload");
}
static void fanoutSnapshot(Registry& registry) {
    for (int count : {1, 4, 5, 9}) {
        Signal signal(registry, "fanout" + SwString::number(count), 16, 4096);
        std::vector<Signal::Subscription> subscriptions;
        std::vector<int> seen;
        const auto owner = owned(42);
        for (int index = 0; index < count; ++index) {
            subscriptions.push_back(signal.connect([&, index](const SharedPayload& payload) {
                require(&payload == &std::get<0>(*owner), "fanout lost shared owner");
                seen.push_back(index);
                if (index == 0 && count > 1) subscriptions.back().stop();
            }, false));
        }
        SharedPayload::copies = SharedPayload::decoded = 0;
        require(signal.publishShared(owner), "fanout publish");
        require(seen.size() == static_cast<size_t>(count == 1 ? 1 : count - 1),
                "snapshot did not respect callback cancellation");
        for (size_t index = 0; index < seen.size(); ++index)
            require(seen[index] == static_cast<int>(index), "fanout order changed");
        require(SharedPayload::copies == 0 && SharedPayload::decoded == 0,
                "fanout copied/decoded native payload");
    }
}
int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        const SwString domain = "signal_sharing_" + SwString::number(detail::currentPid());
        const auto configPath = std::filesystem::temp_directory_path() / domain.toStdString();
        SwRemoteObject::ConfigRootScope config(SwString(configPath.string()));
        Registry registry(domain, "sharing");
        directAndNamed(domain);
        queuedOwnership(registry);
        yieldAndReentrancy(registry);
        cancellation(registry);
        affinityDuringBatch(registry);
        tupleAndEmpty(registry);
        fanoutSnapshot(registry);
        std::cout << "PASS signal shared addresses/copy counts, named/scoped, queued ownership, yield, replay, latest, cancellation and wire\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
