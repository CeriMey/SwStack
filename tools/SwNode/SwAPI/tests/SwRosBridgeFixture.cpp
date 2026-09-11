#include "SwCoreApplication.h"
#include "SwRemoteObject.h"
#include "SwTimer.h"
#include "SwEventLoop.h"
#include <iostream>

class BridgeFixture : public SwRemoteObject {
    SW_OBJECT(BridgeFixture, SwRemoteObject)
public:
    explicit BridgeFixture(const SwString& domain) : SwRemoteObject(domain, "demo", "counter") {
        ipcRegisterConfig(int, limit_, "limit", 10);
        ipcRegisterConfig(SwString, label_, "label", SwString("initial"));
        ipcRegisterConfig(double, gain_, "tracking/gain", 1.5);
        ipcExposeRpc(add, [](int a, int b) { return a + b; });
        ipcExposeRpc(echo, [](SwString value) { return value; });
        ipcExposeRpc(reset, [this]() { seen_ = 0; });
        ipcExposeRpc(seen, [this]() { return seen_; });
        ipcExposeRpc(limit, [this]() { return limit_; });
        ipcExposeRpc(label, [this]() { return label_; });
        ipcExposeRpc(fail, []() -> int { throw std::runtime_error("fixture failure"); });
        ipcExposeRpc(slow, [](int value) { SwEventLoop::swsleep(250); return value; });
        ipcConnect("counter#input", [this](int value) { seen_ = value; }, false);
        input(0);
        dynamic(42);
        largeState(SwString(std::string(5000, 'x')));
        connect(&timer_, &SwTimer::timeout, this, [this] { telemetry(++tick_); });
        timer_.start(25);
    }
    SW_IPC_LATCH(telemetry, int);
    SW_IPC_SIGNAL(input, int);
    SW_IPC_LATCH(dynamic, int);
    SW_IPC_LATCH_SIZED(largeState, 8192, SwString);
private:
    int limit_{0}, seen_{0}, tick_{0};
    SwString label_;
    double gain_{0};
    SwTimer timer_;
};

int main(int argc, char** argv) {
    if (argc == 3 && SwString(argv[1]) == "--cleanup" && SwString(argv[2]).startsWith("swros_test_")) {
#ifndef _WIN32
        const SwString domain(argv[2]);
        for (const auto& value : sw::ipc::shmRegistrySnapshot(domain))
            ::shm_unlink(value.toObject()["shmName"].toString().c_str());
        ::shm_unlink(sw::ipc::shmRegistrySegmentName(domain).c_str());
        ::shm_unlink(sw::ipc::detail::subscribersRegistryNameForDomain_(domain).c_str());
#endif
        return 0;
    }
    if (argc != 2) return 2;
    SwCoreApplication app(argc, argv);
    sw::ipc::setRpcQueueCapacity(100);
    BridgeFixture fixture(argv[1]);
    std::cout << "READY" << std::endl;
    return app.exec();
}
