#include <core/storage/realtimedb/ChangeNotice.hpp>
#include <core/runtime/SwCoreApplication.h>
#include <core/runtime/SwThread.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace sw::ipc;
using swRealtimeDbDetail::ChangeBatch;
using swRealtimeDbDetail::ChangeNotice;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class Fn> static void waitFor(Fn fn, const char* message) {
    require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return fn(); }, 3000), message);
}
static ChangeNotice makeNotice() {
    auto packet = swRealtimeDbDetail::parseObject(
        "{\"epoch\":\"notice-test\",\"revision\":\"1\",\"from_revision\":\"0\","
        "\"events\":[{\"revision\":\"1\",\"table\":\"readings\",\"changed\":true,"
        "\"payload\":{\"value\":10}}],\"tables\":[\"readings\"]}");
    auto batch = std::make_shared<const ChangeBatch>(std::move(packet));
    return ChangeNotice(std::move(batch),
        "{\"epoch\":\"notice-test\",\"revision\":\"1\",\"tables\":[\"readings\"]}");
}
static void codec() {
    const auto notice = makeNotice();
    require(detail::type_id<ChangeNotice>() == detail::type_id<SwString>() &&
            detail::type_name<ChangeNotice>() == detail::type_name<SwString>(),
            "notification wire identity changed");
    uint8_t encoded[4096] = {}, ordinary[4096] = {};
    detail::Encoder typed(encoded, sizeof(encoded)), text(ordinary, sizeof(ordinary));
    require(detail::Codec<ChangeNotice>::write(typed, notice) &&
            detail::Codec<SwString>::write(text, notice.wire()) && typed.size() == text.size() &&
            std::memcmp(encoded, ordinary, typed.size()) == 0, "notification is not a SwString wire payload");
    detail::Decoder decoder(encoded, typed.size());
    ChangeNotice decoded;
    require(detail::Codec<ChangeNotice>::read(decoder, decoded) && decoded.batch() &&
            decoded.batch()->revision() == 1 && !decoded.batch()->complete() &&
            decoded.batch().get() != notice.batch().get(), "wire decoder did not create the compact wakeup batch");
    for (const SwString malformed : {SwString("not-json"), SwString("{\"revision\":\"invalid\"}"), SwString("[]")}) {
        detail::Encoder invalid(encoded, sizeof(encoded));
        require(detail::Codec<SwString>::write(invalid, malformed), "invalid fixture encode failed");
        detail::Decoder input(encoded, invalid.size());
        require(!detail::Codec<ChangeNotice>::read(input, decoded), "malformed wakeup was accepted");
    }
    const uint32_t impossible = UINT32_MAX;
    std::memcpy(encoded, &impossible, sizeof(impossible));
    detail::Decoder invalidLength(encoded, sizeof(impossible));
    require(!detail::Codec<ChangeNotice>::read(invalidLength, decoded), "invalid wire length was accepted");
}
static void direct(Registry& registry) {
    SwIpcSignal<ChangeNotice> signal(registry, "direct_notice", 1, 4096, DeliveryMode::LatestOnly);
    SwIpcSignal<ChangeNotice> receiver(registry, "direct_notice", 1, 4096);
    SwIpcSignal<SwString> textReceiver(registry, "direct_notice", 1, 4096);
    const auto notice = makeNotice();
    int calls = 0;
    auto first = receiver.connect([&](ChangeNotice received) {
        ++calls;
        require(received.batch() == notice.batch() && &received.wire() == &notice.wire(),
                "same-thread notification copied or decoded its native envelope");
        auto detached = received.batch()->detachEvent(0);
        (*detached["payload"].toObjectPtr())["value"] = 99;
        require(received.batch()->detachEvent(0)["payload"].toObject()["value"].toInt() == 10,
                "mutable event escaped into the shared immutable batch");
    }, false);
    auto second = receiver.connect([&](ChangeNotice received) {
        ++calls;
        require(received.batch() == notice.batch() && received.batch()->complete() &&
                received.batch()->detachEvent(0)["payload"].toObject()["value"].toInt() == 10,
                "one receiver corrupted another receiver's native batch");
    }, false);
    require(signal.publish(notice) && calls == 2, "notification was not delivered directly");
    SwString wire;
    require(textReceiver.readLatest(wire) && wire == notice.wire(), "SwString reader lost notification compatibility");
    detail::LoopPoller::instance().dispatch();
    require(calls == 2, "SHM duplicated a native notification");
}
static void queued(Registry& registry) {
    SwIpcSignal<ChangeNotice> signal(registry, "queued_notice", 1, 4096, DeliveryMode::LatestOnly);
    SwObject receiver;
    SwThread worker;
    require(worker.start(), "worker start failed");
    receiver.moveToThread(&worker);
    const auto notice = makeNotice();
    std::atomic_bool observed{false}, correct{false};
    auto subscription = signal.connect(&receiver, [&](ChangeNotice received) {
        auto detached = received.batch()->detachPacket();
        (*(*detached["events"].toArrayPtr())[0].toObjectPtr())["table"] = "changed";
        correct = std::this_thread::get_id() == worker.threadId() &&
                  received.batch() == notice.batch() && &received.wire() == &notice.wire() &&
                  received.batch()->eventsFor("readings") && !received.batch()->eventsFor("changed") &&
                  received.batch()->detachEvent(0)["table"].toString() == "readings";
        observed = true;
    }, false);
    require(signal.publish(notice), "queued notification publish failed");
    waitFor([&] { return observed.load(); }, "queued notification was not delivered");
    subscription.stop(); worker.quit(); worker.wait();
    require(correct, "queued notification copied/decoded its batch or violated affinity/isolation");
}
#ifndef _WIN32
static int child(const char* domain, int ready, int result) {
    Registry registry(domain, "notifications");
    SwIpcSignal<SwString> signal(registry, "external_notice", 1, 4096);
    SwString wire;
    bool received = false;
    auto subscription = signal.connect([&](SwString value) { wire = std::move(value); received = true; }, false);
    char status = 'R';
    require(::write(ready, &status, 1) == 1, "child readiness failed");
    waitFor([&] { return received; }, "external SwString reader missed notification");
    SwIpcSignal<ChangeNotice> typed(registry, "external_notice", 1, 4096);
    ChangeNotice decoded;
    status = wire == makeNotice().wire() && typed.readLatest(decoded) && decoded.batch() &&
             decoded.batch()->revision() == 1 && !decoded.batch()->complete() ? 'Y' : 'N';
    require(::write(result, &status, 1) == 1, "child result failed");
    return status == 'Y' ? 0 : 1;
}
static void external(const SwString& domain, const char* executable, Registry& registry) {
    SwIpcSignal<ChangeNotice> signal(registry, "external_notice", 1, 4096, DeliveryMode::LatestOnly);
    const auto notice = makeNotice();
    bool native = false;
    auto subscription = signal.connect([&](ChangeNotice received) { native = received.batch() == notice.batch(); }, false);
    int ready[2], result[2];
    require(::pipe(ready) == 0 && ::pipe(result) == 0, "pipe failed");
    const auto readyFd = std::to_string(ready[1]), resultFd = std::to_string(result[1]);
    const auto pid = ::fork();
    require(pid >= 0, "fork failed");
    if (!pid) {
        ::execl(executable, executable, "--child", domain.toStdString().c_str(), readyFd.c_str(), resultFd.c_str(), nullptr);
        ::_exit(127);
    }
    ::close(ready[1]); ::close(result[1]);
    char status = 0;
    require(::read(ready[0], &status, 1) == 1, "external receiver not ready");
    require(signal.publish(notice) && native, "mixed native/external notification publish failed");
    require(::read(result[0], &status, 1) == 1 && status == 'Y', "external notification wire changed");
    int exitStatus = 0;
    require(::waitpid(pid, &exitStatus, 0) == pid && WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0,
            "external receiver failed");
    ::close(ready[0]); ::close(result[0]);
}
#endif
int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
#ifndef _WIN32
        if (argc == 5 && std::string(argv[1]) == "--child") return child(argv[2], std::stoi(argv[3]), std::stoi(argv[4]));
#endif
        const SwString domain = "rtdb_notice_" + SwString::number(detail::currentPid());
        Registry registry(domain, "notifications");
        codec(); direct(registry); queued(registry);
#ifndef _WIN32
        external(domain, std::filesystem::absolute(argv[0]).c_str(), registry);
#endif
        std::cout << "PASS RTDB native shared batches, queued affinity and SwString IPC wire\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
