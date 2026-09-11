#include "SwSharedMemorySignal.h"
#include "SwCoreApplication.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reentrantMirror(sw::ipc::Registry& registry) {
    using Property = sw::ipc::SwIpcProperty<int>;
    auto alive = std::make_shared<std::atomic_bool>(true);
    std::vector<int> ownerNotifications, mirrorNotifications;
    Property owner(registry, "reentrant", 0, 1, alive,
        [&](const int& value) { ownerNotifications.push_back(value); });
    Property mirror(registry, "reentrant", 0, 2, alive, [&](const int& value) {
        mirrorNotifications.push_back(value);
        require(owner.get() == value, "mirror callback sees stale owner state");
        if (value == 1) require(owner.set(2), "nested owner update failed");
    });
    require(owner.set(1), "owner update failed");
    require(owner.get() == 2 && mirror.get() == 2, "outer update overwrote nested state");
    require(ownerNotifications == std::vector<int>({2}), "stale outer notification followed nested update");
    require(mirrorNotifications == std::vector<int>({1, 2}), "mirror lost ordered changes");
    require(owner.set(2) && ownerNotifications.size() == 1, "unchanged write emitted again");
}
void rejectedPayload(sw::ipc::Registry& registry) {
    using Property = sw::ipc::SwIpcProperty<SwString>;
    auto alive = std::make_shared<std::atomic_bool>(true);
    unsigned notifications = 0;
    Property owner(registry, "bounded", "default", 3, alive,
        [&](const SwString&) { ++notifications; }, 64);
    require(owner.set("valid"), "bounded property write failed");
    require(!owner.set(SwString(100, 'x')), "oversized property accepted");
    require(owner.get() == "valid" && notifications == 1, "rejected property changed cache or notified");
    sw::ipc::SwIpcSignal<uint64_t, SwString> wire(registry, "bounded", 16, 64,
                                                sw::ipc::DeliveryMode::LatestOnly);
    uint64_t writer = 0;
    SwString value;
    require(wire.readLatest(writer, value) && writer == 3 && value == "valid",
            "rejected property corrupted retained value");
}
void concurrentWriters(sw::ipc::Registry& registry) {
    auto alive = std::make_shared<std::atomic_bool>(true);
    sw::ipc::SwIpcProperty<int> owner(registry, "concurrent", 0, 4, alive, [](const int&) {});
    std::atomic_bool accepted{true};
    auto write = [&](int parity) {
        for (int i = 1; i <= 200; ++i)
            if (!owner.set(2 * i + parity)) accepted.store(false);
    };
    std::thread first(write, 0), second(write, 1);
    first.join();
    second.join();
    require(accepted.load(), "concurrent state publisher stalled");
    sw::ipc::SwIpcSignal<uint64_t, int> wire(registry, "concurrent", 16, 0,
                                           sw::ipc::DeliveryMode::LatestOnly);
    uint64_t writer = 0;
    int value = 0;
    require(wire.readLatest(writer, value) && writer == 4 && owner.get() == value,
            "concurrent cache commit diverged from transport order");
}
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    try {
        const SwString domain = "signal-property-" + SwString::number(sw::ipc::detail::currentPid())
            + "-" + SwString::number(sw::ipc::detail::nowMs());
        sw::ipc::Registry registry(domain, "owner");
        reentrantMirror(registry);
        rejectedPayload(registry);
        concurrentWriters(registry);
        std::cout << "PASS property direct mirror read, nested writes, stale notification suppression, payload rejection, concurrent writes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
