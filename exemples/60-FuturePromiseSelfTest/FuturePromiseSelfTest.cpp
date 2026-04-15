#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "SwCoreApplication.h"
#include "SwConcurrent.h"
#include "SwPromise.h"
#include "SwFutureWatcher.h"
#include "SwThread.h"
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
    pool.setExpiryTimeout(-1);

    SwPromise<int> promise;
    SwFuture<int> future = promise.future();
    pool.start([promise]() mutable {
        promise.start();
        promise.setProgressRange(0, 100);
        promise.setProgressValueAndText(25, "boot");
        promise.addResult(7);
        promise.setProgressValueAndText(100, "done");
        promise.finish();
    });

    future.waitForFinished();
    addResult("Future becomes finished", future.isFinished());
    addResult("Future returns first result", future.result() == 7);
    addResult("Future reports progress text", future.progressText() == "done");
    addResult("Future reports contiguous resultCount", future.resultCount() == 1);
    addResult("waitForResult succeeds on ready index", future.waitForResult(0));

    SwPromise<int> indexedPromise;
    SwFuture<int> indexedFuture = indexedPromise.future();
    indexedPromise.start();
    const bool firstInsert = indexedPromise.addResult(42, 1);
    const bool duplicateInsert = indexedPromise.addResult(99, 1);
    indexedPromise.addResult(11, 0);
    indexedPromise.finish();
    addResult("Promise accepts sparse indexed result", firstInsert);
    addResult("Promise rejects duplicate indexed result", !duplicateInsert);
    addResult("Contiguous resultCount follows Qt semantics", indexedFuture.resultCount() == 2);
    addResult("Indexed resultAt returns stored value", indexedFuture.resultAt(1) == 42);

    SwPromise<int> multiPromise;
    SwFuture<int> multiFuture = multiPromise.future();
    multiPromise.start();
    SwVector<int> vectorBatch;
    vectorBatch.push_back(3);
    vectorBatch.push_back(4);
    SwList<int> listBatch;
    listBatch.append(5);
    listBatch.append(6);
    multiPromise.addResults(vectorBatch);
    multiPromise.addResults(listBatch);
    multiPromise.finish();
    const SwVector<int> copiedResults = multiFuture.results();
    const SwVector<int> movedResults = multiFuture.takeResults();
    addResult("Promise addResults accepts SwVector and SwList", copiedResults.size() == 4);
    addResult("Future takeResults drains ready values", movedResults.size() == 4 && multiFuture.resultCount() == 0);

    SwFuture<int> doubledFuture = future.then([](SwFuture<int> previous) {
        return previous.result() * 2;
    });
    doubledFuture.waitForFinished();
    addResult("Future then chains continuation", doubledFuture.result() == 14);

    SwFuture<int> valueFuture = future.then([](int value) {
        return value + 5;
    });
    valueFuture.waitForFinished();
    addResult("Future then accepts value callback", valueFuture.result() == 12);

    SwFuture<int> unwrappedFuture = future.thenOnThreadPool(&pool, [&](SwFuture<int> previous) {
        return SwConcurrent::runOnThreadPool(&pool, [previous]() {
            return previous.result() + 1;
        });
    });
    unwrappedFuture.waitForFinished();
    addResult("Future then unwraps nested SwFuture", unwrappedFuture.result() == 8);

    SwFuture<int> concurrentFuture = SwConcurrent::runOnThreadPool(&pool, []() {
        return 99;
    });
    concurrentFuture.waitForFinished();
    addResult("SwConcurrent::runOnThreadPool produces a future", concurrentFuture.result() == 99);

    SwPromise<void> voidPromise;
    SwFuture<void> voidFuture = voidPromise.future();
    pool.start([voidPromise]() mutable {
        voidPromise.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        voidPromise.finish();
    });
    voidFuture.waitForFinished();
    addResult("Void future completes", voidFuture.isFinished());

    SwFuture<int> voidValueFuture = voidFuture.then([]() {
        return 1234;
    });
    voidValueFuture.waitForFinished();
    addResult("Void future then accepts no-arg callback", voidValueFuture.result() == 1234);

    SwPromise<int> cancelPromise;
    SwFuture<int> cancelFuture = cancelPromise.future();
    std::atomic<bool> workerObservedCancel(false);
    pool.start([cancelPromise, &workerObservedCancel]() mutable {
        cancelPromise.start();
        while (!cancelPromise.isCanceled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        workerObservedCancel.store(true);
        cancelPromise.finish();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    cancelFuture.cancel();
    cancelFuture.waitForFinished();
    addResult("Future propagates cooperative cancel", cancelFuture.isCanceled() && workerObservedCancel.load());

    std::atomic<bool> cancelCallbackObserved(false);
    cancelFuture.onCanceled([&](SwFuture<int>) {
        cancelCallbackObserved.store(true);
    });
    addResult("Future onCanceled callback fires", cancelCallbackObserved.load());

    SwPromise<int> failedPromise;
    SwFuture<int> failedFuture = failedPromise.future();
    std::atomic<bool> failedCallbackObserved(false);
    failedFuture.onFailed([&](SwFuture<int> value) {
        failedCallbackObserved.store(value.failureText() == "boom");
    });
    failedPromise.fail("boom");
    failedFuture.waitForFinished();
    addResult("Future stores failure state", failedFuture.isFailed() && failedFuture.failureText() == "boom");
    addResult("Future onFailed callback fires", failedCallbackObserved.load());

    SwPromise<int> canceledChainPromise;
    SwFuture<int> canceledChainSource = canceledChainPromise.future();
    std::atomic<bool> canceledContinuationCalled(false);
    SwFuture<int> canceledChainFuture = canceledChainSource.thenInline([&](SwFuture<int>) {
        canceledContinuationCalled.store(true);
        return 1;
    });
    canceledChainPromise.cancel();
    canceledChainFuture.waitForFinished();
    addResult("then propagates cancel without running continuation",
              canceledChainFuture.isCanceled() && !canceledContinuationCalled.load());

    SwPromise<int> failedChainPromise;
    SwFuture<int> failedChainSource = failedChainPromise.future();
    std::atomic<bool> failedContinuationCalled(false);
    SwFuture<int> failedChainFuture = failedChainSource.thenInline([&](SwFuture<int>) {
        failedContinuationCalled.store(true);
        return 2;
    });
    failedChainPromise.fail("chain-fail");
    failedChainFuture.waitForFinished();
    addResult("then propagates failure without running continuation",
              failedChainFuture.isFailed() &&
              failedChainFuture.failureText() == "chain-fail" &&
              !failedContinuationCalled.load());

    SwPromise<int> suspendPromise;
    SwFuture<int> suspendFuture = suspendPromise.future();
    std::atomic<bool> enteredSuspend(false);
    std::atomic<bool> resumed(false);
    suspendFuture.suspend();
    pool.start([suspendPromise, &enteredSuspend, &resumed]() mutable {
        suspendPromise.start();
        suspendPromise.addResult(1);
        suspendPromise.suspendIfRequested();
        enteredSuspend.store(true);
        resumed.store(true);
        suspendPromise.addResult(2);
        suspendPromise.finish();
    });

    for (int i = 0; i < 50; ++i) {
        if (suspendFuture.isSuspended()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    suspendFuture.resume();
    suspendFuture.waitForFinished();
    addResult("Pause aliases suspend state", suspendFuture.isPaused() == false);
    addResult("Future enters suspended state on request", enteredSuspend.load());
    addResult("Future resumes and finishes", resumed.load() && suspendFuture.resultCount() == 2);

    SwThread watcherThread("FutureWatcherThread");
    watcherThread.start();
    SwFutureWatcher<int> watcher;
    watcher.moveToThread(&watcherThread);
    std::atomic<bool> watcherStarted(false);
    std::atomic<bool> watcherFinished(false);
    std::atomic<bool> watcherProgress(false);
    SwObject::connect(&watcher, &SwFutureWatcher<int>::started, [&]() {
        watcherStarted.store(true);
    }, DirectConnection);
    SwObject::connect(&watcher, &SwFutureWatcher<int>::finished, [&]() {
        watcherFinished.store(true);
    }, DirectConnection);
    SwObject::connect(&watcher, &SwFutureWatcher<int>::progressValueChanged, [&](int value) {
        if (value == 50) {
            watcherProgress.store(true);
        }
    }, DirectConnection);

    SwPromise<int> watchedPromise;
    SwFuture<int> watchedFuture = watchedPromise.future();
    watcher.setFuture(watchedFuture);
    pool.start([watchedPromise]() mutable {
        watchedPromise.start();
        watchedPromise.setProgressValue(50);
        watchedPromise.addResult(123);
        watchedPromise.finish();
    });
    watchedFuture.waitForFinished();
    for (int i = 0; i < 50 && (!watcherStarted.load() || !watcherFinished.load() || !watcherProgress.load()); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    addResult("FutureWatcher observes lifecycle and progress",
              watcherStarted.load() && watcherFinished.load() && watcherProgress.load());
    watcherThread.quit();
    watcherThread.wait();

    SwFuture<int> appLaneFuture = SwConcurrent::runOnAppLane(&app, SwFiberLane::Background, []() {
        return 321;
    });
    app.postEventOnLane([&app, appLaneFuture]() mutable {
        appLaneFuture.thenOnAppLane(&app, SwFiberLane::Control, [&app](SwFuture<int>) {
            app.exit(0);
            return 0;
        });
    }, SwFiberLane::Control);
    app.exec(300000);
    addResult("SwConcurrent::runOnAppLane produces a future", appLaneFuture.isFinished() && appLaneFuture.result() == 321);

    std::cout << "\n===== SwFuture / SwPromise Summary =====\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const TestResult& result = results[i];
        std::cout << (result.success ? "[PASS] " : "[FAIL] ") << result.name;
        if (!result.detail.empty()) {
            std::cout << " -> " << result.detail;
        }
        std::cout << std::endl;
    }

    return 0;
}
