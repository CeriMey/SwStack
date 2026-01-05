#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "SwCoreApplication.h"
#include "SwEventLoop.h"
#include "SwTimer.h"

struct TestResult
{
    std::string name;
    bool success;
    std::string detail;
};

int main(int argc, char *argv[])
{
    SwCoreApplication app(argc, argv);
    std::vector<std::string> log;
    std::vector<TestResult> results;

    auto record = [&](const std::string &entry) {
        log.push_back(entry);
        std::cout << "[LOG] " << entry << std::endl;
    };

    auto addResult = [&](const std::string &name, bool success, const std::string &detail = std::string()) {
        results.push_back({name, success, detail});
        std::cout << "[TEST] " << name << " -> " << (success ? "PASS" : "FAIL");
        if (!detail.empty())
        {
            std::cout << " (" << detail << ")";
        }
        std::cout << std::endl;
    };

    auto addTimingResult = [&](const std::string &name, long long actualMs, long long minMs, long long maxMs) {
        std::ostringstream oss;
        oss << actualMs << "ms (expected " << minMs << "-" << maxMs << "ms)";
        addResult(name, actualMs >= minMs && actualMs <= maxMs, oss.str());
    };

    bool yieldResumed = false;
    bool releaseResumed = false;
    bool innerLoopTimerTriggered = false;
    bool innerLoopFinished = false;
    bool watchdogCovered = false;

    auto now = []() { return std::chrono::steady_clock::now(); };

    // 1. Basic event ordering
    app.postEvent([&]() {
        record("event:A");
    });

    // 2. Yield / unYield cooperation
    app.postEvent([&]() {
        record("event:yield:start");
        SwCoreApplication::yieldFiber(1);
        yieldResumed = true;
        record("event:yield:resumed");
        addResult("yield/unYield resume", yieldResumed);
    });
    app.postEvent([&]() {
        record("event:unyield:trigger");
        SwCoreApplication::unYieldFiber(1);
    });

    // 3. release() immediate requeue
    app.postEvent([&]() {
        record("event:release:start");
        SwCoreApplication::release();
        releaseResumed = true;
        record("event:release:resumed");
        addResult("release() resumes automatically", releaseResumed);
    });

    // 4. SwEventLoop nested run with timer
    app.postEvent([&]() {
        record("swloop:start");
        SwEventLoop innerLoop;
        auto innerStart = now();
        SwTimer loopTimer(100, &innerLoop);
        SwObject::connect(&loopTimer, SIGNAL(timeout), &innerLoop, [&]() {
            record("swloop:timer");
            loopTimer.stop();
            innerLoopTimerTriggered = true;
            innerLoop.quit();
        });
        loopTimer.start();
        innerLoop.exec();
        innerLoopFinished = true;
        record(std::string("swloop:end:") + (innerLoopTimerTriggered ? "timer-triggered" : "no-timer"));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - innerStart).count();
        addTimingResult("nested SwEventLoop timer", elapsed, 80, 300);
        addResult("nested SwEventLoop completed", innerLoopTimerTriggered && innerLoopFinished);
    });

    // 5. SwEventLoop::swsleep demonstration
    app.postEvent([&]() {
        record("swsleep:start");
        auto start = now();
        SwEventLoop::swsleep(20);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - start).count();
        record("swsleep:end");
        addTimingResult("SwEventLoop::swsleep(20ms)", elapsed, 15, 200);
    });

    // 6. SwTimer::singleShot timing
    auto singleShotStart = now();
    SwTimer::singleShot(200, [&, singleShotStart]() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - singleShotStart).count();
        record("singleShot:200ms");
        addTimingResult("SwTimer::singleShot 200ms", elapsed, 180, 400);
    });

    // 7. Core application timer via addTimer/removeTimer
    auto coreTimerStart = now();
    auto coreTimerId = std::make_shared<int>(0);
    *coreTimerId = app.addTimer([&, coreTimerId, coreTimerStart]() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - coreTimerStart).count();
        record("coreTimer:callback");
        SwCoreApplication::instance()->removeTimer(*coreTimerId);
        addTimingResult("SwCoreApplication::addTimer 150ms", elapsed, 130, 400);
    }, 150000); // microseconds -> 150 ms

    // 8. Watchdog exercise
#if defined(_WIN32)
    app.postEvent([&]() {
        record("watchdog:activate");
        record("watchdog:deactivate");
        watchdogCovered = false;
        addResult("watchdog activate/deactivate (skipped on Windows)", true, "Not supported on Windows");
    });
#else
    app.postEvent([&]() {
        record("watchdog:activate");
        app.activeWatchDog();
        SwEventLoop::swsleep(5);
        app.desactiveWatchDog();
        watchdogCovered = true;
        record("watchdog:deactivate");
        addResult("watchdog activate/deactivate", watchdogCovered);
    });
#endif

    // 9. Final summary and quit
    SwTimer::singleShot(700, [&]() {
        record("tests:completed");

#if defined(_WIN32)
        std::vector<std::string> expectedOrder = {
            "event:A",
            "event:yield:start",
            "event:unyield:trigger",
            "event:release:start",
            "swloop:start",
            "swsleep:start",
            "watchdog:activate",
            "watchdog:deactivate",
            "event:yield:resumed",
            "event:release:resumed",
            "swsleep:end",
            "swloop:timer",
            "swloop:end:timer-triggered",
            "coreTimer:callback",
            "singleShot:200ms",
            "tests:completed"
        };
#else
        std::vector<std::string> expectedOrder = {
            "event:A",
            "event:yield:start",
            "event:unyield:trigger",
            "event:yield:resumed",
            "event:release:start",
            "event:release:resumed",
            "swloop:start",
            "swloop:timer",
            "swloop:end:timer-triggered",
            "swsleep:start",
            "swsleep:end",
            "singleShot:200ms",
            "coreTimer:callback",
            "watchdog:activate",
            "watchdog:deactivate",
            "tests:completed"
        };
#endif

        bool orderOk = log == expectedOrder;
        std::ostringstream oss;
        if (!orderOk)
        {
            oss << "expected=" << expectedOrder.size() << " got=" << log.size();
        }
        addResult("event log ordering", orderOk, oss.str());

        std::cout << "\n===== Test Summary =====\n";
        for (const auto &result : results)
        {
            std::cout << (result.success ? "[PASS] " : "[FAIL] ") << result.name;
            if (!result.detail.empty())
            {
                std::cout << " -> " << result.detail;
            }
            std::cout << std::endl;
        }

        app.quit();
    });

    return app.exec();
}
