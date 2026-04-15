#pragma once

/**
 * @file src/core/runtime/SwConcurrent.h
 * @ingroup core_runtime
 * @brief Lightweight QtConcurrent-like helpers for SwFuture / SwPromise.
 */

#include "SwFuture.h"
#include "SwPromise.h"
#include "SwThreadPool.h"

namespace SwConcurrent {

template<typename R, typename F>
struct SwConcurrentRunner {
    static SwFuture<R> runOnThreadPool(SwThreadPool* pool, F fn) {
        SwPromise<R> promise;
        if (!pool) {
            pool = SwThreadPool::globalInstance();
        }
        promise.setContinuationScheduler([pool](std::function<void()> task) {
            pool->start(task);
        });
        SwFuture<R> future = promise.future();
        pool->start([promise, fn]() mutable {
            promise.start();
            promise.addResult(fn());
            promise.finish();
        });
        return future;
    }

    static SwFuture<R> runOnAppLane(SwCoreApplication* app, SwFiberLane lane, F fn) {
        SwPromise<R> promise;
        if (!app) {
            return runOnThreadPool(SwThreadPool::globalInstance(), fn);
        }
        promise.setContinuationScheduler([app, lane](std::function<void()> task) {
            app->postEventOnLane(task, lane);
        });
        SwFuture<R> future = promise.future();
        app->postEventOnLane([promise, fn]() mutable {
            promise.start();
            promise.addResult(fn());
            promise.finish();
        }, lane);
        return future;
    }
};

template<typename F>
struct SwConcurrentRunner<void, F> {
    static SwFuture<void> runOnThreadPool(SwThreadPool* pool, F fn) {
        SwPromise<void> promise;
        if (!pool) {
            pool = SwThreadPool::globalInstance();
        }
        promise.setContinuationScheduler([pool](std::function<void()> task) {
            pool->start(task);
        });
        SwFuture<void> future = promise.future();
        pool->start([promise, fn]() mutable {
            promise.start();
            fn();
            promise.finish();
        });
        return future;
    }

    static SwFuture<void> runOnAppLane(SwCoreApplication* app, SwFiberLane lane, F fn) {
        SwPromise<void> promise;
        if (!app) {
            return runOnThreadPool(SwThreadPool::globalInstance(), fn);
        }
        promise.setContinuationScheduler([app, lane](std::function<void()> task) {
            app->postEventOnLane(task, lane);
        });
        SwFuture<void> future = promise.future();
        app->postEventOnLane([promise, fn]() mutable {
            promise.start();
            fn();
            promise.finish();
        }, lane);
        return future;
    }
};

template<typename F>
SwFuture<typename std::result_of<F()>::type> run(F fn) {
    typedef typename std::result_of<F()>::type ResultType;
    SwCoreApplication* app = SwCoreApplication::instance(false);
    if (app) {
        return SwConcurrentRunner<ResultType, F>::runOnAppLane(app, SwFiberLane::Background, fn);
    }
    return SwConcurrentRunner<ResultType, F>::runOnThreadPool(SwThreadPool::globalInstance(), fn);
}

template<typename F>
SwFuture<typename std::result_of<F()>::type> runOnThreadPool(SwThreadPool* pool, F fn) {
    typedef typename std::result_of<F()>::type ResultType;
    return SwConcurrentRunner<ResultType, F>::runOnThreadPool(pool, fn);
}

template<typename F>
SwFuture<typename std::result_of<F()>::type> runOnAppLane(SwCoreApplication* app,
                                                          SwFiberLane lane,
                                                          F fn) {
    typedef typename std::result_of<F()>::type ResultType;
    return SwConcurrentRunner<ResultType, F>::runOnAppLane(app, lane, fn);
}

} // namespace SwConcurrent

