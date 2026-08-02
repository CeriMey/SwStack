#include <core/runtime/SwCoreApplication.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

struct WorkerRuntime {
    std::mutex mutex;
    std::condition_variable changed;
    SwCoreApplication* application = nullptr;
    bool ready = false;
    bool claimSucceeded = false;
    bool probeProcessed = false;
    std::atomic<bool> exited{false};
    std::thread thread;

    void start(bool attemptClaim) {
        thread = std::thread([this, attemptClaim]() {
            SwCoreApplication worker;
            const bool claimed = attemptClaim
                                     ? worker.claimOrderedProcessTermination()
                                     : false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                application = &worker;
                claimSucceeded = claimed;
                ready = true;
            }
            changed.notify_all();

            (void)worker.exec();
            exited.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(mutex);
                application = nullptr;
            }
            changed.notify_all();
        });
    }

    bool waitUntilReady() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2),
                                [this]() { return ready; });
    }

    bool proveStillRunning() {
        std::unique_lock<std::mutex> lock(mutex);
        if (!application || exited.load(std::memory_order_acquire)) {
            return false;
        }
        application->postEvent([this]() {
            {
                std::lock_guard<std::mutex> eventLock(mutex);
                probeProcessed = true;
            }
            changed.notify_all();
        });
        return changed.wait_for(lock, std::chrono::seconds(2),
                                [this]() { return probeProcessed; });
    }

    void requestStop() {
        std::lock_guard<std::mutex> lock(mutex);
        if (application) {
            application->quit();
        }
    }

    bool waitUntilExited() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [this]() {
            return exited.load(std::memory_order_acquire);
        });
    }

    void join() {
        if (thread.joinable()) {
            thread.join();
        }
    }
};

bool orderedTerminationCase(int argc, char** argv) {
    SwCoreApplication owner(argc, argv);
    if (!owner.claimOrderedProcessTermination()) {
        return false;
    }
    if (!owner.ownsOrderedProcessTermination()) {
        return false;
    }

    WorkerRuntime worker;
    worker.start(true);
    if (!worker.waitUntilReady()) {
        worker.requestStop();
        worker.join();
        return false;
    }
    if (worker.claimSucceeded) {
        worker.requestStop();
        worker.join();
        return false;
    }

#if defined(_WIN32)
    if (!SwCoreApplication::requestProcessTermination()) {
        worker.requestStop();
        worker.join();
        return false;
    }
#else
    if (::raise(SIGTERM) != 0) {
        worker.requestStop();
        worker.join();
        return false;
    }
#endif

    const int exitCode = owner.exec();
    const bool workerSurvived = worker.proveStillRunning();
    worker.requestStop();
    worker.join();
    owner.releaseOrderedProcessTermination();
    return exitCode == 0 && workerSurvived &&
           !owner.ownsOrderedProcessTermination();
}

bool legacyQuitAllCase() {
    SwCoreApplication primary;
    WorkerRuntime worker;
    worker.start(false);
    if (!worker.waitUntilReady()) {
        worker.requestStop();
        worker.join();
        return false;
    }

    if (!SwCoreApplication::requestProcessTermination()) {
        worker.requestStop();
        worker.join();
        return false;
    }

    const int exitCode = primary.exec();
    const bool workerExited = worker.waitUntilExited();
    if (!workerExited) {
        worker.requestStop();
    }
    worker.join();
    return exitCode == 0 && workerExited;
}

} // namespace

int main(int argc, char** argv) {
    if (!orderedTerminationCase(argc, argv)) {
        return 1;
    }
    if (!legacyQuitAllCase()) {
        return 1;
    }
    return 0;
}
