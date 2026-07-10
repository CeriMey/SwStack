#include "SwCoreApplication.h"
#include "SwTimer.h"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    const int timerCount = 10000;
    const int expected = timerCount / 2;
    std::shared_ptr<int> fired(new int(0));
    std::shared_ptr<bool> failed(new bool(false));
    std::shared_ptr<bool> shortTimersComplete(new bool(false));
    std::shared_ptr<int> reliableEventsFired(new int(0));
    std::shared_ptr<bool> cancelledDispatchFired(new bool(false));
    std::vector<int> ids;
    ids.reserve(timerCount);

    // Put one callback ahead of an already-expired single-shot timer. A concurrent removal then
    // exercises cancellation after timer dispatch but before callback execution.
    app.postEventOnLaneReliable([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }, SwFiberLane::Control);
    const int dispatchedThenCancelled = app.addTimer(
        [cancelledDispatchFired]() { *cancelledDispatchFired = true; },
        1,
        true,
        SwFiberLane::Control);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    for (int i = 0; i < timerCount; ++i) {
        const int id = app.addTimer([&app, fired, failed, shortTimersComplete, i, expected]() {
            if ((i % 2) == 0) {
                *failed = true;
                app.exit(2);
                return;
            }
            ++(*fired);
            if (*fired == expected) {
                *shortTimersComplete = true;
            }
        }, 1000 + (i % 100) * 1000, true, SwFiberLane::Control);
        ids.push_back(id);
    }

    for (int i = 0; i < timerCount; i += 2) {
        app.removeTimer(ids[static_cast<std::size_t>(i)]);
    }

    std::thread canceller([&app, dispatchedThenCancelled]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        app.removeTimer(dispatchedThenCancelled);
    });
    std::thread reliableProducer([&app, reliableEventsFired]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        for (int i = 0; i < 1000; ++i) {
            if (!app.postEventOnLaneReliable([reliableEventsFired]() {
                    ++(*reliableEventsFired);
                }, SwFiberLane::Control)) {
                break;
            }
        }
    });

    const auto started = std::chrono::steady_clock::now();
    SwTimer longDeadline(2500);
    longDeadline.setSingleShot(true);
    SwObject::connect(&longDeadline, &SwTimer::timeout,
                      [&app, fired, failed, shortTimersComplete, reliableEventsFired,
                       cancelledDispatchFired, expected, started]() {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (elapsed < 2400 || !*shortTimersComplete || *fired != expected ||
            *reliableEventsFired != 1000 || *cancelledDispatchFired) {
            *failed = true;
            app.exit(5);
            return;
        }
        app.exit(*failed ? 2 : 0);
    });
    longDeadline.start();

    SwTimer watchdog(5000);
    watchdog.setSingleShot(true);
    SwObject::connect(&watchdog, &SwTimer::timeout, [&app]() { app.exit(3); });
    watchdog.start();

    const int result = app.exec();
    canceller.join();
    reliableProducer.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (result != 0 || *fired != expected || *reliableEventsFired != 1000 ||
        *cancelledDispatchFired || elapsed < 2400 || elapsed > 5000) {
        return result == 0 ? 4 : result;
    }
    return 0;
}
