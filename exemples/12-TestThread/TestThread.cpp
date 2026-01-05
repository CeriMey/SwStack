#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "SwCoreApplication.h"
#include "SwThread.h"
#include "SwTimer.h"

struct TestResult {
    std::string name;
    bool success;
    std::string detail;
};

class ThreadAwareWorker : public SwObject {
    DECLARE_SIGNAL(jobFinished, const std::string&, std::thread::id);

public:
    ThreadAwareWorker() = default;

    void performJob(const std::string& label, int delayMs) {
        auto schedule = [this, label, delayMs]() {
            SwTimer::singleShot(delayMs, [this, label]() {
                m_lastExecutionThread = std::this_thread::get_id();
                jobFinished(label, m_lastExecutionThread);
            });
        };

        SwThread* targetThread = thread();
        if (targetThread) {
            targetThread->postTask(schedule);
        } else {
            SwCoreApplication::instance()->postEvent(schedule);
        }
    }

    std::thread::id lastExecutionThread() const {
        return m_lastExecutionThread;
    }

private:
    std::thread::id m_lastExecutionThread;
};

class ThreadLogger : public SwObject {
public:
    struct Entry {
        std::string label;
        std::thread::id receiverThread;
        std::thread::id originThread;
    };

    void recordJob(const std::string& label, std::thread::id originThread) {
        entries.push_back({label, std::this_thread::get_id(), originThread});
    }

    const Entry* findEntry(const std::string& label) const {
        for (const auto& entry : entries) {
            if (entry.label == label) {
                return &entry;
            }
        }
        return nullptr;
    }

private:
    std::vector<Entry> entries;
};

class PingPongNode : public SwObject {
    DECLARE_SIGNAL(message, const std::string&, int);

public:
    explicit PingPongNode(const std::string& name)
        : m_name(name) {}

    void setOnReceived(std::function<void(PingPongNode*, const std::string&, int)> callback) {
        m_onReceived = std::move(callback);
    }

    void startExchange(const std::string& label, int hops) {
        SwThread* host = thread();
        auto starter = [this, label, hops]() {
            message(label, hops);
        };
        if (host) {
            host->postTask(starter);
        } else {
            SwCoreApplication::instance()->postEvent(starter);
        }
    }

    void onMessage(const std::string& label, int remaining) {
        ++m_receiveCount;
        if (thread()) {
            auto expectedId = thread()->threadId();
            if (std::this_thread::get_id() != expectedId) {
                m_threadContextValid = false;
            }
        }
        if (m_onReceived) {
            m_onReceived(this, label, remaining);
        }
    }

    void relayToPeer(const std::string& label, int remaining) {
        message(label, remaining);
    }

    bool threadValid() const { return m_threadContextValid; }
    int receiveCount() const { return m_receiveCount; }
    const std::string& name() const { return m_name; }

private:
    std::string m_name;
    std::function<void(PingPongNode*, const std::string&, int)> m_onReceived;
    bool m_threadContextValid = true;
    int m_receiveCount = 0;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    std::vector<TestResult> results;
    auto addResult = [&](const std::string& name, bool success, const std::string& detail = std::string()) {
        results.push_back({name, success, detail});
        std::cout << "[TEST] " << name << " -> " << (success ? "PASS" : "FAIL");
        if (!detail.empty()) {
            std::cout << " (" << detail << ")";
        }
        std::cout << std::endl;
    };

    std::vector<std::string> logs;
    auto logMessage = [&](const std::string& entry) {
        logs.push_back(entry);
        std::cout << "[LOG] " << entry << std::endl;
    };

    auto logThreadAffinity = [&](const std::string& tag, SwThread* target, SwObject& object) {
        std::ostringstream oss;
        oss << tag
            << " targetThreadId=" << (target ? target->threadId() : std::thread::id())
            << " objectThreadId=";
        if (auto objThread = object.thread()) {
            oss << objThread->threadId();
        } else {
            oss << std::thread::id();
        }
        logMessage(oss.str());
    };

    SwThread* mainThreadWrapper = SwThread::currentThread();
    const std::thread::id mainThreadId = std::this_thread::get_id();
    auto ensureMainThreadThread = [&]() -> SwThread* {
        SwThread* current = SwThread::currentThread();
        return current ? current : mainThreadWrapper;
    };
    if (!mainThreadWrapper) {
        addResult("Main thread adoption", false, "SwThread::currentThread() returned nullptr");
        return 1;
    }

    SwThread workerA("WorkerA");
    SwThread workerB("WorkerB");
    SwThread delayedThread("DelayedStart");

    bool workerAStartedSignal = false;
    bool workerAFinishedSignal = false;
    bool workerBStartedSignal = false;
    bool workerBFinishedSignal = false;
    bool delayedStartedSignal = false;
    bool delayedFinishedSignal = false;

    auto attachThreadMonitors = [&](SwThread& thread, const std::string& label, bool& startedFlag, bool& finishedFlag) {
        SwObject::connect(&thread, &SwThread::started, [&, label]() {
            startedFlag = true;
            std::ostringstream oss;
            oss << label << " started (id=" << thread.threadId() << ")";
            logMessage(oss.str());
        }, DirectConnection);
        SwObject::connect(&thread, &SwThread::finished, [&, label]() {
            finishedFlag = true;
            std::ostringstream oss;
            oss << label << " finished (id=" << thread.threadId() << ")";
            logMessage(oss.str());
        }, DirectConnection);
    };

    attachThreadMonitors(workerA, "WorkerA", workerAStartedSignal, workerAFinishedSignal);
    attachThreadMonitors(workerB, "WorkerB", workerBStartedSignal, workerBFinishedSignal);
    attachThreadMonitors(delayedThread, "DelayedThread", delayedStartedSignal, delayedFinishedSignal);

    bool delayedTaskExecuted = false;
    std::thread::id delayedTaskThreadId;
    delayedThread.postTask([&]() {
        delayedTaskExecuted = true;
        delayedTaskThreadId = std::this_thread::get_id();
    });

    workerA.start();
    workerB.start();
    delayedThread.start();

    ThreadAwareWorker worker;
    ThreadLogger logger;
    logThreadAffinity("worker initial thread", worker.thread(), worker);
    logThreadAffinity("logger initial thread", logger.thread(), logger);
    SwObject::connect(&worker, &ThreadAwareWorker::jobFinished, &logger, &ThreadLogger::recordJob, QueuedConnection);

    worker.moveToThread(&workerA);
    logThreadAffinity("worker moved to WorkerA", &workerA, worker);
    addResult("moveToThread -> WorkerA", worker.thread() == &workerA);

    auto evaluateDelayedThread = [&]() {
        bool ready = delayedTaskExecuted && delayedTaskThreadId == delayedThread.threadId();
        addResult("postTask before start executes inside worker", ready);
    };

    worker.performJob("workerA-first", 20);

    SwTimer::singleShot(200, [&]() {
        auto entryA = logger.findEntry("workerA-first");
        bool entryPresent = entryA != nullptr;
        bool receiverMain = entryPresent && entryA->receiverThread == mainThreadId;
        bool originMatch = entryPresent && entryA->originThread == workerA.threadId();
        bool jobOnWorkerA = worker.lastExecutionThread() == workerA.threadId();

        addResult("workerA signal marshalled to main", entryPresent && receiverMain && originMatch);
        addResult("workerA job executed on worker thread", jobOnWorkerA);

        worker.moveToThread(&workerB);
        logThreadAffinity("worker moved to WorkerB", &workerB, worker);
        addResult("moveToThread -> WorkerB", worker.thread() == &workerB);

        worker.performJob("workerB-second", 30);

        SwTimer::singleShot(250, [&]() {
            auto entryB = logger.findEntry("workerB-second");
            bool entryPresentB = entryB != nullptr;
            bool receiverMainB = entryPresentB && entryB->receiverThread == mainThreadId;
            bool originMatchB = entryPresentB && entryB->originThread == workerB.threadId();
            bool jobOnWorkerB = worker.lastExecutionThread() == workerB.threadId();

            addResult("workerB signal marshalled to main", entryPresentB && receiverMainB && originMatchB);
            addResult("workerB job executed on worker thread", jobOnWorkerB);

            evaluateDelayedThread();

            auto startQueuedPingPongTest = [&](std::function<void()> done) {
                auto pingNodeA = std::make_shared<PingPongNode>("SignalNodeA");
                auto pingNodeB = std::make_shared<PingPongNode>("SignalNodeB");

                pingNodeA->moveToThread(&workerA);
                logThreadAffinity("pingNodeA moved to WorkerA", &workerA, *pingNodeA);
                pingNodeB->moveToThread(&workerB);
                logThreadAffinity("pingNodeB moved to WorkerB", &workerB, *pingNodeB);

                SwObject::connect(pingNodeA.get(), &PingPongNode::message, pingNodeB.get(), &PingPongNode::onMessage, QueuedConnection);
                SwObject::connect(pingNodeB.get(), &PingPongNode::message, pingNodeA.get(), &PingPongNode::onMessage, QueuedConnection);

                auto pingCompletion = std::make_shared<std::atomic<bool>>(false);

                auto finalizePingTest = [&, pingNodeA, pingNodeB, done]() {
                    addResult("Ping nodeA handled on workerA thread",
                              pingNodeA->thread() == &workerA && pingNodeA->threadValid());
                    addResult("Ping nodeB handled on workerB thread",
                              pingNodeB->thread() == &workerB && pingNodeB->threadValid());
                    addResult("Ping pong exchanged signals", pingNodeA->receiveCount() > 0 && pingNodeB->receiveCount() > 0);
                    done();
                };

                auto pingHandler = [&, pingCompletion, finalizePingTest](PingPongNode* receiver, const std::string& label, int remaining) {
                    (void)label;
                    if (remaining > 0) {
                        receiver->relayToPeer(label, remaining - 1);
                    } else {
                        if (!pingCompletion->exchange(true)) {
                            app.postEvent(finalizePingTest);
                        }
                    }
                };

                pingNodeA->setOnReceived(pingHandler);
                pingNodeB->setOnReceived(pingHandler);

                pingNodeA->startExchange("ping-test", 8);
            };

            startQueuedPingPongTest([&, mainThreadWrapper, mainThreadId]() mutable {
                SwThread* resolvedMainThread = ensureMainThreadThread();
                worker.moveToThread(resolvedMainThread);
                logThreadAffinity("worker moved to MainThread", resolvedMainThread, worker);
                addResult("moveToThread -> Main thread", worker.thread() == resolvedMainThread);

                worker.performJob("main-thread", 15);
                SwTimer::singleShot(200, [&, mainThreadId]() {
                    auto entryMain = logger.findEntry("main-thread");
                    bool entryPresentMain = entryMain != nullptr;
                    bool receiverMainMain = entryPresentMain && entryMain->receiverThread == mainThreadId;
                    bool originMain = entryPresentMain && entryMain->originThread == mainThreadId;
                    bool jobOnMain = worker.lastExecutionThread() == mainThreadId;

                    addResult("main thread job stays local", entryPresentMain && receiverMainMain && originMain && jobOnMain);

                    delayedThread.quit();
                    delayedThread.wait();
                    workerA.quit();
                    workerA.wait();
                    workerB.quit();
                    workerB.wait();

                    addResult("WorkerA started signal fired", workerAStartedSignal);
                    addResult("WorkerA finished signal fired", workerAFinishedSignal);
                    addResult("WorkerB started signal fired", workerBStartedSignal);
                    addResult("WorkerB finished signal fired", workerBFinishedSignal);
                    addResult("Delayed thread started signal fired", delayedStartedSignal);
                    addResult("Delayed thread finished signal fired", delayedFinishedSignal);

                    std::cout << "\n===== SwThread Test Summary =====\n";
                    for (const auto& result : results) {
                        std::cout << (result.success ? "[PASS] " : "[FAIL] ") << result.name;
                        if (!result.detail.empty()) {
                            std::cout << " -> " << result.detail;
                        }
                        std::cout << std::endl;
                    }

                    app.quit();
                });
            });
        });
    });

    return app.exec();
}
