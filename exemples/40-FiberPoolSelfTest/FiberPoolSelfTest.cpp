#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "SwCoreApplication.h"

struct FiberPoolCheckResult {
    std::string name;
    bool success;
    std::string detail;
};

static void printResult(const FiberPoolCheckResult& result) {
    std::cout << "[TEST] " << result.name << " -> " << (result.success ? "PASS" : "FAIL");
    if (!result.detail.empty()) {
        std::cout << " (" << result.detail << ")";
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    const int warmOccupancy = 500;
    const int inputBurst = 4;
    const int rejectedBackgroundCount = 8;
    const int warmYieldBase = 10000;
    const int inputYieldBase = 20000;

    std::atomic<int> warmEntered(0);
    std::atomic<int> warmResumed(0);
    std::atomic<int> inputEntered(0);
    std::atomic<int> inputResumed(0);
    std::atomic<int> controlExecuted(0);
    std::atomic<int> backgroundExecuted(0);

    std::vector<FiberPoolCheckResult> results;

    auto addResult = [&](const std::string& name, bool success, const std::string& detail) {
        FiberPoolCheckResult result{name, success, detail};
        results.push_back(result);
        printResult(result);
    };

    auto summarizeAndQuit = [&]() {
        bool overallSuccess = true;
        for (size_t i = 0; i < results.size(); ++i) {
            if (!results[i].success) {
                overallSuccess = false;
                break;
            }
        }
        std::cout << "[SUMMARY] " << (overallSuccess ? "PASS" : "FAIL") << std::endl;
        return overallSuccess;
    };

    auto waitUntil = [](const std::function<bool()>& predicate, int timeoutMs) {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return predicate();
    };

    for (int i = 0; i < warmOccupancy; ++i) {
        app.postEvent([&, i]() {
            ++warmEntered;
            SwCoreApplication::yieldFiber(warmYieldBase + i);
            ++warmResumed;
        });
    }

    std::thread verifier([&]() {
        const bool saturated = waitUntil([&]() {
            return warmEntered.load() == warmOccupancy;
        }, 2000);

        const SwFiberPoolStats saturatedStats = app.fiberPoolStats();
        {
            std::ostringstream oss;
            oss << "warmEntered=" << warmEntered.load()
                << " yieldedCount=" << saturatedStats.yieldedCount;
            addResult("saturation reached before timeout",
                      saturated &&
                      warmEntered.load() == warmOccupancy &&
                      saturatedStats.yieldedCount == warmOccupancy,
                      oss.str());
        }

        {
            std::ostringstream oss;
            oss << "warmCount=" << saturatedStats.warmCount;
            addResult("warm pool preheated to 500", saturatedStats.warmCount == warmOccupancy, oss.str());
        }

        for (int i = 0; i < inputBurst; ++i) {
            app.postEventOnLane([&, i]() {
                ++inputEntered;
                SwCoreApplication::yieldFiber(inputYieldBase + i);
                ++inputResumed;
            }, SwFiberLane::Input);
        }

        for (int i = 0; i < rejectedBackgroundCount; ++i) {
            app.postEventOnLane([&]() {
                ++backgroundExecuted;
            }, SwFiberLane::Background);
        }

        app.postEventPriority([&]() {
            ++controlExecuted;
        });

        const bool spilloverObserved = waitUntil([&]() {
            return inputEntered.load() == inputBurst && controlExecuted.load() == 1;
        }, 2000);
        const SwFiberPoolStats spilloverStats = app.fiberPoolStats();

        {
            std::ostringstream oss;
            oss << "spilloverCount=" << spilloverStats.spilloverCount
                << " inputEntered=" << inputEntered.load()
                << " controlExecuted=" << controlExecuted.load();
            addResult("spillover activated for input",
                      spilloverObserved &&
                      spilloverStats.spilloverCount >= inputBurst,
                      oss.str());
        }

        {
            std::ostringstream oss;
            oss << "rejectedCount=" << spilloverStats.rejectedCount << " expected>=" << rejectedBackgroundCount;
            addResult("background overflow rejected", spilloverStats.rejectedCount >= rejectedBackgroundCount, oss.str());
        }

        {
            std::ostringstream oss;
            oss << "backgroundExecuted=" << backgroundExecuted.load();
            addResult("rejected background tasks did not run", backgroundExecuted.load() == 0, oss.str());
        }

        for (int i = 0; i < warmOccupancy; ++i) {
            SwCoreApplication::unYieldFiber(warmYieldBase + i);
        }
        for (int i = 0; i < inputBurst; ++i) {
            SwCoreApplication::unYieldFiberHighPriority(inputYieldBase + i);
        }

        const bool resumed = waitUntil([&]() {
            return warmResumed.load() == warmOccupancy &&
                   inputResumed.load() == inputBurst;
        }, 2000);

        const SwFiberPoolStats resumedStats = app.fiberPoolStats();
        {
            std::ostringstream oss;
            oss << "warmResumed=" << warmResumed.load()
                << " expected=" << warmOccupancy
                << " resumed=" << (resumed ? "true" : "false");
            addResult("warm fibers resumed", resumed && warmResumed.load() == warmOccupancy, oss.str());
        }

        {
            std::ostringstream oss;
            oss << "inputResumed=" << inputResumed.load() << " expected=" << inputBurst;
            addResult("input spillover resumed", resumed && inputResumed.load() == inputBurst, oss.str());
        }

        {
            std::ostringstream oss;
            oss << "warmCount=" << resumedStats.warmCount;
            addResult("warm pool stayed at 500", resumedStats.warmCount == warmOccupancy, oss.str());
        }

        {
            std::ostringstream oss;
            oss << "spilloverCount=" << resumedStats.spilloverCount << " max=16";
            addResult("spillover stayed bounded", resumedStats.spilloverCount <= 16, oss.str());
        }

        app.exit(summarizeAndQuit() ? 0 : 1);
    });

    const int exitCode = app.exec();
    verifier.join();
    return exitCode;
}
