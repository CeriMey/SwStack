#include <core/runtime/SwCoreApplication.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <thread>

#if defined(SW_TEST_WRAP_POLL)
// Test-only injection into the exact check-to-wait window. No production hook
// or signal-handler allocation is needed to reproduce a watchdog-style reentry.
namespace {
thread_local std::function<void()> beforePoll;
void injectBeforePoll() {
    if (beforePoll) {
        auto callback=std::move(beforePoll); beforePoll={}; callback();
    }
}
}
extern "C" int __real_poll(struct pollfd*, nfds_t, int);
extern "C" int __wrap_poll(struct pollfd* descriptors, nfds_t count, int timeout) {
    injectBeforePoll();
    return __real_poll(descriptors, count, timeout);
}
#if defined(__GLIBC__)
// _FORTIFY_SOURCE may redirect poll to the checked glibc entry point. Keep
// its descriptor-buffer size intact, and exercise the same injection exactly once.
extern "C" int __real___poll_chk(struct pollfd*, nfds_t, int, std::size_t);
extern "C" int __wrap___poll_chk(struct pollfd* descriptors, nfds_t count,
                                  int timeout, std::size_t descriptorBytes) {
    injectBeforePoll();
    return __real___poll_chk(descriptors, count, timeout, descriptorBytes);
}
#endif
#endif

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
class Application : public SwCoreApplication {
public:
    Application() {
        SwFiberPoolConfig config;
        config.warmFiberCount=2; config.maxFiberCount=2; config.emergencySpilloverCount=0;
        fiberPool_.setConfig(config);
    }
    void wait(long long us) { waitForWork(us); }
#if defined(_WIN32)
    void guiWait(long long us) { waitForWorkGui(us); }
#endif
    void drain() {
        for (unsigned i=0; i<1000; ++i) if (processEvent()!=0) return;
        throw std::runtime_error("event queue did not drain");
    }
};
void ownerWake(Application& app, bool gui) {
    app.drain(); app.wait(0);
    require(app.processEvent()<0, "fixture has a pending deadline");
    int completed=0;
    require(app.tryPostEvent([&] {
        ++completed;
        app.postEvent([&] {++completed;});
    }), "owner post rejected");
    const auto start=std::chrono::steady_clock::now();
#if defined(_WIN32)
    if (gui) app.guiWait(500000); else
#else
    (void)gui;
#endif
    app.wait(500000);
    require(std::chrono::steady_clock::now()-start < std::chrono::milliseconds(250),
            "owner post after idle calculation did not cancel the stale wait");
    require(completed==0, "wait dispatched a callback inline");
    app.processEvent();
    require(completed==1, "first owner callback was lost or duplicated");
    const auto nestedStart=std::chrono::steady_clock::now();
    app.wait(500000);
    require(std::chrono::steady_clock::now()-nestedStart < std::chrono::milliseconds(250),
            "nested owner post did not wake a subsequent wait");
    app.drain();
    require(completed==2, "nested callback was lost or duplicated");
}
#if defined(SW_TEST_WRAP_POLL)
void postAfterWaitCheck(Application& app) {
    app.drain(); app.wait(0);
    bool injected=false, accepted=false;
    int completed=0;
    beforePoll=[&] {
        injected=true;
        accepted=app.tryPostEvent([&] {++completed;});
    };
    const auto start=std::chrono::steady_clock::now();
    app.wait(500000);
    require(injected && accepted, "check-to-wait injection was not exercised");
    require(std::chrono::steady_clock::now()-start < std::chrono::milliseconds(250),
            "owner post between final check and OS wait lost its wakeup");
    require(completed==0, "check-to-wait post ran its callback inline");
    app.drain();
    require(completed==1, "check-to-wait callback was lost or duplicated");
}
#endif

void reliableRejection(Application& app) {
    app.drain();
    int completed=0, rejected=0;
    require(app.tryPostEvent([&] {++completed;}) && app.tryPostEvent([&] {++completed;}),
            "fixture did not fill its normal lane");
    require(!app.tryPostEvent([&] {++rejected;}), "saturated normal lane accepted an event");
    require(app.postEventOnLaneReliable([&] {++completed;}, SwFiberLane::Normal),
            "reliable event was lost under normal-lane saturation");
    require(completed==0 && rejected==0, "reliable post dispatched synchronously");
    app.drain();
    require(completed==3 && rejected==0, "reliable resume or rejected callback executed incorrectly");
}
void foreignWake(Application& app) {
    app.drain(); app.wait(0);
    std::atomic<bool> stop(false), ready(false), failed(false);
    std::atomic<int> requested(0);
    int completed=0;
    std::thread producer([&] {
        Application ownApplication; // Its TLS instance differs from the destination.
        ready.store(true);
        for (int round=1; round<=64 && !stop.load(); ++round) {
            while (!stop.load() && requested.load()<round) std::this_thread::yield();
            if (stop.load()) break;
            if (round==1) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!app.tryPostEvent([&] {++completed;})) {failed.store(true); break;}
        }
    });
    try {
        while (!ready.load()) std::this_thread::yield();
        requested.store(1);
        app.wait(500000); // Exercise a genuinely idle destination first.
        app.drain();
        require(completed==1, "foreign runtime did not wake an idle destination");
        for (int round=2; round<=64; ++round) {
            // Race the owner's coalesced wake consumption with an OS wake from
            // another runtime. Queue synchronization owns callback visibility.
            require(app.tryPostEvent([] {}), "owner stress post rejected");
            requested.store(round);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while (completed<round && !failed.load()) {
                const auto delay=app.processEvent();
                if (delay!=0) app.wait(20000);
                require(std::chrono::steady_clock::now()<deadline, "concurrent owner/foreign wake was lost");
            }
            require(!failed.load(), "foreign stress post rejected");
        }
        stop.store(true); producer.join();
        app.drain();
        require(completed==64, "foreign callback was lost or duplicated");
    } catch (...) {stop.store(true); producer.join(); throw;}
}
}
int main() {
    try {
        Application app;
        ownerWake(app, false);
#if defined(_WIN32)
        ownerWake(app, true);
#endif
#if defined(SW_TEST_WRAP_POLL)
        postAfterWaitCheck(app);
#endif
        reliableRejection(app);
        foreignWake(app);
        std::cout << "PASS: owner/nested/GUI wake, foreign runtime races and reliable normal-lane saturation\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
