#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <numeric>

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

    bool innerLoopTimerTriggered = false;
    bool innerLoopFinished = false;

    auto now = []() { return std::chrono::steady_clock::now(); };

    // =====================================================================
    // 1. Basic event ordering — simple sequential events
    // =====================================================================
    app.postEvent([&]() { record("event:A"); });

    // =====================================================================
    // 2. Yield / unYield — basic cooperation
    // =====================================================================
    app.postEvent([&]() {
        record("yield:start");
        SwCoreApplication::yieldFiber(1);
        record("yield:resumed");
        addResult("yield/unYield resume", true);
    });
    app.postEvent([&]() {
        record("unyield:trigger");
        SwCoreApplication::unYieldFiber(1);
    });

    // =====================================================================
    // 3. release() — immediate requeue
    // =====================================================================
    app.postEvent([&]() {
        record("release:start");
        SwCoreApplication::release();
        record("release:resumed");
        addResult("release() resumes automatically", true);
    });

    // =====================================================================
    // 4. Fiber chain — 3 fibers yield, a 4th resumes them in reverse
    //    Verifies FIFO ordering of the ready-queue
    // =====================================================================
    std::vector<std::string> chainOrder;

    app.postEvent([&]() {
        record("chain:A:start");
        SwCoreApplication::yieldFiber(10);
        record("chain:A:resumed");
        chainOrder.push_back("A");
    });
    app.postEvent([&]() {
        record("chain:B:start");
        SwCoreApplication::yieldFiber(11);
        record("chain:B:resumed");
        chainOrder.push_back("B");
    });
    app.postEvent([&]() {
        record("chain:C:start");
        SwCoreApplication::yieldFiber(12);
        record("chain:C:resumed");
        chainOrder.push_back("C");
    });
    app.postEvent([&]() {
        record("chain:D:unyield-reverse");
        SwCoreApplication::unYieldFiber(12);
        SwCoreApplication::unYieldFiber(11);
        SwCoreApplication::unYieldFiber(10);
    });

    // =====================================================================
    // 5. Ping-pong — two fibers alternate via yield/unYield, 2 full rounds
    // =====================================================================
    int pingPongCount = 0;

    app.postEvent([&]() {
        record("pp:P:start");
        SwCoreApplication::yieldFiber(20);
        record("pp:P:r1");
        pingPongCount++;
        SwCoreApplication::unYieldFiber(21);
        SwCoreApplication::yieldFiber(20);
        record("pp:P:r2");
        pingPongCount++;
        SwCoreApplication::unYieldFiber(21);
    });
    app.postEvent([&]() {
        record("pp:Q:start");
        SwCoreApplication::unYieldFiber(20);
        SwCoreApplication::yieldFiber(21);
        record("pp:Q:r1");
        pingPongCount++;
        SwCoreApplication::unYieldFiber(20);
        SwCoreApplication::yieldFiber(21);
        record("pp:Q:r2");
        pingPongCount++;
    });

    // =====================================================================
    // 6. Yield + release interleave
    //    X yields, Y does release() then unYields X
    // =====================================================================
    bool interleaveXOk = false;
    bool interleaveYOk = false;

    app.postEvent([&]() {
        record("mix:X:start");
        SwCoreApplication::yieldFiber(30);
        interleaveXOk = true;
        record("mix:X:resumed");
    });
    app.postEvent([&]() {
        record("mix:Y:start");
        SwCoreApplication::release();
        interleaveYOk = true;
        record("mix:Y:afterRelease");
        SwCoreApplication::unYieldFiber(30);
    });

    // =====================================================================
    // 7. Double yield same ID — second fiber overwrites the first
    // =====================================================================
    bool dy_F1 = false;
    bool dy_F2 = false;

    app.postEvent([&]() {
        record("dy:F1:start");
        SwCoreApplication::yieldFiber(40);
        dy_F1 = true;
        record("dy:F1:resumed");
    });
    app.postEvent([&]() {
        record("dy:F2:start");
        SwCoreApplication::yieldFiber(40);
        dy_F2 = true;
        record("dy:F2:resumed");
    });
    app.postEvent([&]() {
        record("dy:F3:unyield");
        SwCoreApplication::unYieldFiber(40);
    });

    // =====================================================================
    // 8. Cascade wake-up — each fiber, when resumed, wakes the next one
    //    W1 yields(50), W2 yields(51), W3 yields(52)
    //    Trigger unYields W1, W1 resumes and unYields W2, W2 unYields W3
    // =====================================================================
    std::vector<std::string> cascadeOrder;

    app.postEvent([&]() {
        record("cascade:W1:start");
        SwCoreApplication::yieldFiber(50);
        record("cascade:W1:resumed");
        cascadeOrder.push_back("W1");
        SwCoreApplication::unYieldFiber(51); // wake W2
    });
    app.postEvent([&]() {
        record("cascade:W2:start");
        SwCoreApplication::yieldFiber(51);
        record("cascade:W2:resumed");
        cascadeOrder.push_back("W2");
        SwCoreApplication::unYieldFiber(52); // wake W3
    });
    app.postEvent([&]() {
        record("cascade:W3:start");
        SwCoreApplication::yieldFiber(52);
        record("cascade:W3:resumed");
        cascadeOrder.push_back("W3");
    });
    app.postEvent([&]() {
        record("cascade:trigger");
        SwCoreApplication::unYieldFiber(50); // kick off the chain
    });

    // =====================================================================
    // 9. Triple release — fiber calls release() 3 times in a row
    //    Each time it should be re-queued and resumed
    // =====================================================================
    int tripleReleaseCount = 0;

    app.postEvent([&]() {
        record("3rel:start");
        SwCoreApplication::release();
        tripleReleaseCount++;
        record("3rel:after1");
        SwCoreApplication::release();
        tripleReleaseCount++;
        record("3rel:after2");
        SwCoreApplication::release();
        tripleReleaseCount++;
        record("3rel:after3");
    });

    // =====================================================================
    // 10. Re-yield — fiber yields with id=60, gets resumed, then yields
    //     again with id=61. A later event resumes it via 61.
    // =====================================================================
    int reyieldPhase = 0;

    app.postEvent([&]() {
        record("reyield:start");
        SwCoreApplication::yieldFiber(60);
        reyieldPhase = 1;
        record("reyield:phase1");
        SwCoreApplication::yieldFiber(61);
        reyieldPhase = 2;
        record("reyield:phase2");
    });
    app.postEvent([&]() {
        record("reyield:wake60");
        SwCoreApplication::unYieldFiber(60);
    });
    app.postEvent([&]() {
        record("reyield:wake61");
        SwCoreApplication::unYieldFiber(61);
    });

    // =====================================================================
    // 11. unYield on non-existent ID — should be a no-op
    // =====================================================================
    app.postEvent([&]() {
        record("noop:unyield999");
        SwCoreApplication::unYieldFiber(999); // no fiber with this ID
        record("noop:survived");
    });

    // =====================================================================
    // 12. High-priority unYield — verify it resumes before normal ready
    //     Fiber H1 yields(70), Fiber H2 yields(71)
    //     Dispatcher does normal unYield(71) then highPriority unYield(70)
    //     H1 should resume first (high-priority queue)
    // =====================================================================
    std::vector<std::string> hiPriOrder;

    app.postEvent([&]() {
        record("hipri:H1:start");
        SwCoreApplication::yieldFiber(70);
        record("hipri:H1:resumed");
        hiPriOrder.push_back("H1");
    });
    app.postEvent([&]() {
        record("hipri:H2:start");
        SwCoreApplication::yieldFiber(71);
        record("hipri:H2:resumed");
        hiPriOrder.push_back("H2");
    });
    app.postEvent([&]() {
        record("hipri:dispatch");
        SwCoreApplication::unYieldFiber(71);                // normal priority
        SwCoreApplication::unYieldFiberHighPriority(70);    // high priority
    });

    // =====================================================================
    // 13. Priority event queue — postEventPriority cuts ahead of postEvent
    // =====================================================================
    bool priEvtSeen = false;
    bool normalEvtSeen = false;
    std::vector<std::string> priOrder;

    app.postEvent([&]() {
        record("prievt:setup");
        // Post a normal event, then a priority event
        // The priority event should execute first
        app.postEvent([&]() {
            record("prievt:normal");
            normalEvtSeen = true;
            priOrder.push_back("normal");
        });
        app.postEventPriority([&]() {
            record("prievt:priority");
            priEvtSeen = true;
            priOrder.push_back("priority");
        });
    });

    // =====================================================================
    // 14. Rapid-fire events — 10 events posted, all must execute in order
    // =====================================================================
    std::vector<int> rapidResults;
    for (int i = 0; i < 10; ++i) {
        app.postEvent([&, i]() {
            record("rapid:" + std::to_string(i));
            rapidResults.push_back(i);
        });
    }

    // =====================================================================
    // 15. Nested SwEventLoop with timer
    // =====================================================================
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
        addTimingResult("nested SwEventLoop timer", elapsed, 97, 103);
        addResult("nested SwEventLoop completed", innerLoopTimerTriggered && innerLoopFinished);
    });

    // =====================================================================
    // 16. SwEventLoop::swsleep
    // =====================================================================
    app.postEvent([&]() {
        record("swsleep:start");
        auto start = now();
        SwEventLoop::swsleep(100);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - start).count();
        record("swsleep:end");
        addTimingResult("SwEventLoop::swsleep(100ms)", elapsed, 97, 103);
    });

    // =====================================================================
    // 17. SwTimer::singleShot timing — started from inside the event loop
    //     to avoid including app startup overhead in the measurement
    // =====================================================================
    auto singleShotStartPtr = std::make_shared<std::chrono::steady_clock::time_point>();
    app.postEvent([&, singleShotStartPtr]() {
        *singleShotStartPtr = now();
        SwTimer::singleShot(200, [&, singleShotStartPtr]() {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *singleShotStartPtr).count();
            record("singleShot:200ms");
            addTimingResult("SwTimer::singleShot 200ms", elapsed, 197, 203);
        });
    });

    // =====================================================================
    // 18. Core application timer via addTimer/removeTimer — started from
    //     inside the event loop to avoid app startup overhead
    // =====================================================================
    auto coreTimerStartPtr = std::make_shared<std::chrono::steady_clock::time_point>();
    auto coreTimerId = std::make_shared<int>(0);
    app.postEvent([&, coreTimerStartPtr, coreTimerId]() {
        *coreTimerStartPtr = now();
        *coreTimerId = app.addTimer([&, coreTimerId, coreTimerStartPtr]() {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *coreTimerStartPtr).count();
            record("coreTimer:callback");
            SwCoreApplication::instance()->removeTimer(*coreTimerId);
            addTimingResult("SwCoreApplication::addTimer 150ms", elapsed, 147, 153);
        }, 150000);
    });

    // =====================================================================
    // 19. Watchdog exercise
    // =====================================================================
    app.postEvent([&]() {
        record("watchdog:activate");
        app.activeWatchDog();
        SwEventLoop::swsleep(5);
        app.desactiveWatchDog();
        record("watchdog:deactivate");
        addResult("watchdog activate/deactivate", true);
    });

    // =====================================================================
    // 20. Short-duration timer precision (swsleep)
    //     Tests: 1ms, 5ms, 10ms, 20ms — ±3ms tolerance
    //     These do NOT call record() to avoid disturbing the log ordering test
    // =====================================================================
    app.postEvent([&]() {
        {
            auto s = now();
            SwEventLoop::swsleep(1);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(1ms)", e, 1, 4);
        }
        {
            auto s = now();
            SwEventLoop::swsleep(5);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(5ms)", e, 2, 8);
        }
        {
            auto s = now();
            SwEventLoop::swsleep(10);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(10ms)", e, 7, 13);
        }
        {
            auto s = now();
            SwEventLoop::swsleep(20);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(20ms)", e, 17, 23);
        }
    });

    // =====================================================================
    // 21. Short-duration singleShot precision
    //     Tests: 10ms, 25ms, 50ms — ±3ms tolerance
    // =====================================================================
    app.postEvent([&]() {
        auto t10 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(10, [&, t10]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t10).count();
            addTimingResult("singleShot(10ms)", e, 7, 13);
        });
        auto t25 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(25, [&, t25]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t25).count();
            addTimingResult("singleShot(25ms)", e, 22, 28);
        });
        auto t50 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(50, [&, t50]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t50).count();
            addTimingResult("singleShot(50ms)", e, 47, 53);
        });
    });

    // =====================================================================
    // 22. Long-duration timer precision (singleShot)
    //     Tests: 500ms, 1000ms, 2000ms, 3000ms — ±3ms tolerance
    // =====================================================================
    app.postEvent([&]() {
        auto t500 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(500, [&, t500]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t500).count();
            addTimingResult("singleShot(500ms)", e, 497, 503);
        });
        auto t1000 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(1000, [&, t1000]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t1000).count();
            addTimingResult("singleShot(1000ms)", e, 997, 1003);
        });
        auto t2000 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(2000, [&, t2000]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t2000).count();
            addTimingResult("singleShot(2000ms)", e, 1997, 2003);
        });
        auto t3000 = std::make_shared<std::chrono::steady_clock::time_point>(now());
        SwTimer::singleShot(3000, [&, t3000]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *t3000).count();
            addTimingResult("singleShot(3000ms)", e, 2997, 3003);
        });
    });

    // =====================================================================
    // 23. Long-duration swsleep precision
    //     Tests: 500ms, 1000ms, 3000ms — ±3ms tolerance
    // =====================================================================
    app.postEvent([&]() {
        {
            auto s = now();
            SwEventLoop::swsleep(500);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(500ms)", e, 497, 503);
        }
        {
            auto s = now();
            SwEventLoop::swsleep(1000);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(1000ms)", e, 997, 1003);
        }
        {
            auto s = now();
            SwEventLoop::swsleep(3000);
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - s).count();
            addTimingResult("swsleep(3000ms)", e, 2997, 3003);
        }
    });

    // =====================================================================
    // 24. Short-duration addTimer precision
    //     Tests: 10ms, 50ms — ±3ms tolerance
    // =====================================================================
    app.postEvent([&]() {
        auto at10Start = std::make_shared<std::chrono::steady_clock::time_point>(now());
        auto at10Id = std::make_shared<int>(0);
        *at10Id = app.addTimer([&, at10Id, at10Start]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *at10Start).count();
            SwCoreApplication::instance()->removeTimer(*at10Id);
            addTimingResult("addTimer(10ms)", e, 7, 13);
        }, 10000);

        auto at50Start = std::make_shared<std::chrono::steady_clock::time_point>(now());
        auto at50Id = std::make_shared<int>(0);
        *at50Id = app.addTimer([&, at50Id, at50Start]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *at50Start).count();
            SwCoreApplication::instance()->removeTimer(*at50Id);
            addTimingResult("addTimer(50ms)", e, 47, 53);
        }, 50000);
    });

    // =====================================================================
    // 25. Long-duration addTimer precision
    //     Tests: 1000ms, 3000ms — ±3ms tolerance
    // =====================================================================
    app.postEvent([&]() {
        auto at1000Start = std::make_shared<std::chrono::steady_clock::time_point>(now());
        auto at1000Id = std::make_shared<int>(0);
        *at1000Id = app.addTimer([&, at1000Id, at1000Start]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *at1000Start).count();
            SwCoreApplication::instance()->removeTimer(*at1000Id);
            addTimingResult("addTimer(1000ms)", e, 997, 1003);
        }, 1000000);

        auto at3000Start = std::make_shared<std::chrono::steady_clock::time_point>(now());
        auto at3000Id = std::make_shared<int>(0);
        *at3000Id = app.addTimer([&, at3000Id, at3000Start]() {
            auto e = std::chrono::duration_cast<std::chrono::milliseconds>(now() - *at3000Start).count();
            SwCoreApplication::instance()->removeTimer(*at3000Id);
            addTimingResult("addTimer(3000ms)", e, 2997, 3003);
        }, 3000000);
    });

    // =====================================================================
    // Final summary — 8s delay to let all timers fire
    // (test 23 has sequential swsleep 500+1000+3000 = 4.5s,
    //  test 22/25 have 3s singleShot/addTimer — total ~8s max)
    // =====================================================================
    SwTimer::singleShot(8000, [&]() {
        record("tests:completed");

        // --- Fiber chain reverse resume ---
        {
            std::vector<std::string> expected = {"C", "B", "A"};
            std::ostringstream oss;
            for (size_t i = 0; i < chainOrder.size(); ++i) { if (i) oss << ","; oss << chainOrder[i]; }
            addResult("fiber chain reverse resume", chainOrder == expected, oss.str());
        }

        // --- Ping-pong ---
        addResult("ping-pong 4 round-trips", pingPongCount == 4, "count=" + std::to_string(pingPongCount));

        // --- Yield+release interleave ---
        addResult("yield+release interleave X", interleaveXOk);
        addResult("yield+release interleave Y", interleaveYOk);

        // --- Double yield ---
        {
            std::ostringstream oss;
            oss << "F1=" << (dy_F1 ? "resumed" : "lost") << " F2=" << (dy_F2 ? "resumed" : "lost");
            addResult("double yield same ID (F2 wins)", dy_F2 && !dy_F1, oss.str());
        }

        // --- Cascade wake-up ---
        {
            std::vector<std::string> expected = {"W1", "W2", "W3"};
            std::ostringstream oss;
            for (size_t i = 0; i < cascadeOrder.size(); ++i) { if (i) oss << ","; oss << cascadeOrder[i]; }
            addResult("cascade wake-up W1->W2->W3", cascadeOrder == expected, oss.str());
        }

        // --- Triple release ---
        addResult("triple release() count", tripleReleaseCount == 3, "count=" + std::to_string(tripleReleaseCount));

        // --- Re-yield ---
        addResult("re-yield (2 phases)", reyieldPhase == 2, "phase=" + std::to_string(reyieldPhase));

        // --- High-priority unYield ---
        {
            std::vector<std::string> expected = {"H1", "H2"};
            std::ostringstream oss;
            for (size_t i = 0; i < hiPriOrder.size(); ++i) { if (i) oss << ","; oss << hiPriOrder[i]; }
            addResult("high-priority unYield (H1 before H2)", hiPriOrder == expected, oss.str());
        }

        // --- Priority event queue ---
        {
            std::vector<std::string> expected = {"priority", "normal"};
            std::ostringstream oss;
            for (size_t i = 0; i < priOrder.size(); ++i) { if (i) oss << ","; oss << priOrder[i]; }
            addResult("priority event queue ordering", priOrder == expected, oss.str());
        }

        // --- Rapid-fire ---
        {
            std::vector<int> expected(10);
            std::iota(expected.begin(), expected.end(), 0);
            addResult("rapid-fire 10 events in order", rapidResults == expected,
                      "count=" + std::to_string(rapidResults.size()));
        }

        // --- Full event log ordering ---
        std::vector<std::string> expectedOrder = {
            "event:A",
            // 2. yield/unYield
            "yield:start",
            "unyield:trigger",
            "yield:resumed",
            // 3. release
            "release:start",
            "release:resumed",
            // 4. fiber chain
            "chain:A:start",
            "chain:B:start",
            "chain:C:start",
            "chain:D:unyield-reverse",
            "chain:C:resumed",
            "chain:B:resumed",
            "chain:A:resumed",
            // 5. ping-pong (mix:X sneaks in between rounds — see test 6 comment)
            "pp:P:start",
            "pp:Q:start",
            "pp:P:r1",
            "pp:Q:r1",
            "mix:X:start",          // X yields(30), inserted here by event queue
            "pp:P:r2",
            "pp:Q:r2",
            // 6. yield+release interleave
            "mix:Y:start",
            "mix:Y:afterRelease",
            "mix:X:resumed",
            // 7. double yield
            "dy:F1:start",
            "dy:F2:start",
            "dy:F3:unyield",
            "dy:F2:resumed",
            // 8. cascade wake-up
            "cascade:W1:start",
            "cascade:W2:start",
            "cascade:W3:start",
            "cascade:trigger",
            "cascade:W1:resumed",
            "cascade:W2:resumed",
            "cascade:W3:resumed",
            // 9. triple release — each release() re-queues the fiber,
            // so other pending events interleave between resumes
            "3rel:start",
            "3rel:after1",
            "reyield:start",       // reyield was next in event queue
            "3rel:after2",
            "reyield:wake60",      // next event runs before 3rel resumes
            "3rel:after3",
            // 10. re-yield
            "reyield:phase1",
            "reyield:wake61",
            "reyield:phase2",
            // 11. noop unYield
            "noop:unyield999",
            "noop:survived",
            // 12. high-priority unYield
            "hipri:H1:start",
            "hipri:H2:start",
            "hipri:dispatch",
            "hipri:H1:resumed",
            "hipri:H2:resumed",
            // 13. priority event — priority fires immediately, normal is deferred
            "prievt:setup",
            "prievt:priority",
            // 14. rapid-fire
            "rapid:0", "rapid:1", "rapid:2", "rapid:3", "rapid:4",
            "rapid:5", "rapid:6", "rapid:7", "rapid:8", "rapid:9",
            // 15-19. timed events — prievt:normal lands here because
            // watchdog's swsleep gives the nested loop a chance to drain it
            "swloop:start",
            "swsleep:start",
            "watchdog:activate",
            "prievt:normal",
            "watchdog:deactivate",
            "swloop:timer",
            "swloop:end:timer-triggered",
            "swsleep:end",
            "coreTimer:callback",
            "singleShot:200ms",
            "tests:completed"
        };

        bool orderOk = log == expectedOrder;
        std::ostringstream oss;
        if (!orderOk)
        {
            oss << "expected=" << expectedOrder.size() << " got=" << log.size();
            size_t maxLen = std::max(expectedOrder.size(), log.size());
            for (size_t i = 0; i < maxLen; ++i) {
                std::string exp = i < expectedOrder.size() ? expectedOrder[i] : "(none)";
                std::string got = i < log.size() ? log[i] : "(none)";
                std::string marker = (exp != got) ? " <-- MISMATCH" : "";
                std::cout << "  [" << i << "] expected=\"" << exp << "\" got=\"" << got << "\"" << marker << std::endl;
            }
        }
        addResult("event log ordering (" + std::to_string(expectedOrder.size()) + " entries)", orderOk, oss.str());

        std::cout << "\n===== Test Summary =====\n";
        int passed = 0, failed = 0;
        for (const auto &result : results)
        {
            std::cout << (result.success ? "[PASS] " : "[FAIL] ") << result.name;
            if (!result.detail.empty())
                std::cout << " -> " << result.detail;
            std::cout << std::endl;
            result.success ? ++passed : ++failed;
        }
        std::cout << "\n" << passed << " passed, " << failed << " failed, "
                  << (passed + failed) << " total\n";

        app.quit();
    });

    return app.exec();
}
