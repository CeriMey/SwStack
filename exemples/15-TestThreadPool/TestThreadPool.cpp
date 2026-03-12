#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SwCoreApplication.h"
#include "SwThreadPool.h"

struct TestResult {
    std::string name;
    bool success;
    std::string detail;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);
    (void)app;

    std::vector<TestResult> results;
    auto addResult = [&](const std::string& name, bool success, const std::string& detail = std::string()) {
        results.push_back({name, success, detail});
        std::cout << "[TEST] " << name << " -> " << (success ? "PASS" : "FAIL");
        if (!detail.empty()) {
            std::cout << " (" << detail << ")";
        }
        std::cout << std::endl;
    };

    SwThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.setMaxQueuedTaskCount(1);
    pool.setExpiryTimeout(-1);

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool firstStarted = false;
    bool releaseFirst = false;
    std::atomic<int> executedCount(0);

    pool.start([&]() {
        std::unique_lock<std::mutex> lock(gateMutex);
        firstStarted = true;
        gateCv.notify_all();
        gateCv.wait(lock, [&]() { return releaseFirst; });
        ++executedCount;
    });

    bool firstStartedInTime = false;
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        firstStartedInTime = gateCv.wait_for(lock, std::chrono::seconds(2), [&]() { return firstStarted; });
    }
    addResult("Worker starts first task", firstStartedInTime, firstStartedInTime ? "" : "first task did not start");
    if (!firstStartedInTime) {
        return 1;
    }

    pool.start([&]() {
        ++executedCount;
    });

    bool queuedReached = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (pool.queuedTaskCount() == 1) {
            queuedReached = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.start([&]() {
        ++executedCount;
    });

    bool rejectionRecorded = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (pool.rejectedTaskCount() == 1) {
            rejectionRecorded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    addResult("ThreadPool enforces queued-task limit", queuedReached);
    addResult("ThreadPool counts saturated rejections",
              rejectionRecorded && pool.rejectedTaskCount() == 1,
              rejectionRecorded ? "" : "rejection metric did not move");

    {
        std::lock_guard<std::mutex> lock(gateMutex);
        releaseFirst = true;
    }
    gateCv.notify_all();

    const bool drained = pool.waitForDone(2000);
    addResult("ThreadPool drains after release", drained, drained ? "" : "waitForDone timed out");
    addResult("Only accepted tasks execute", executedCount.load() == 2);
    addResult("Queue is empty after drain", pool.queuedTaskCount() == 0);

    std::cout << "\n===== SwThreadPool Test Summary =====\n";
    for (const auto& result : results) {
        std::cout << (result.success ? "[PASS] " : "[FAIL] ") << result.name;
        if (!result.detail.empty()) {
            std::cout << " -> " << result.detail;
        }
        std::cout << std::endl;
    }

    return 0;
}
