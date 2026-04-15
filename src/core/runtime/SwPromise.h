#pragma once

/**
 * @file src/core/runtime/SwPromise.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwPromise in the CoreSw runtime layer.
 */

#include "SwFuture.h"
#include "SwThreadPool.h"

#include <memory>
#include <utility>

template<typename T>
class SwPromise {
public:
    typedef T result_type;

    SwPromise()
        : m_state(new State()) {
    }

    SwFuture<T> future() const {
        return SwFuture<T>(m_state);
    }

    void setContinuationScheduler(const std::function<void(std::function<void()>)>& scheduler) {
        if (!m_state) {
            return;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        m_state->continuationScheduler = scheduler;
    }

    void start() {
        if (!m_state) {
            return;
        }

        dispatchNotification_([&](State& state) {
            sw::detail::swFutureMarkStartedLocked_(state);
        }, sw::detail::SwFutureNotificationType::Started);
    }

    void finish() {
        if (!m_state) {
            return;
        }

        dispatchNotification_([&](State& state) {
            sw::detail::swFutureMarkStartedLocked_(state);
            state.finished = true;
            state.suspended = false;
            state.suspensionRequested = false;
        }, sw::detail::SwFutureNotificationType::Finished);
    }

    void cancel() {
        if (!m_state) {
            return;
        }

        dispatchNotification_([&](State& state) {
            state.canceled = true;
            state.finished = true;
            state.suspended = false;
            state.suspensionRequested = false;
        }, sw::detail::SwFutureNotificationType::Canceled);
    }

    void fail(const SwString& errorText) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification notification;
        notification.type = sw::detail::SwFutureNotificationType::Failed;
        notification.progressText = errorText;
        dispatchCustomNotification_([&](State& state) {
            sw::detail::swFutureMarkStartedLocked_(state);
            state.failed = true;
            state.failureText = errorText;
            state.finished = true;
            state.suspended = false;
            state.suspensionRequested = false;
        }, notification);
    }

    bool isCanceled() const {
        if (!m_state) {
            return false;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->canceled;
    }

    bool isSuspensionRequested() const {
        if (!m_state) {
            return false;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->suspensionRequested;
    }

    void suspendIfRequested() {
        if (!m_state) {
            return;
        }

        if (!sw::detail::swFutureCanYieldCurrentFiber_()) {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            while (m_state->suspensionRequested && !m_state->finished && !m_state->canceled) {
                m_state->suspended = true;
                m_state->condition.notify_all();
                m_state->condition.wait(lock);
            }
            m_state->suspended = false;
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return;
        }

        for (;;) {
            int yieldId = 0;
            {
                std::unique_lock<SwMutex> lock(m_state->mutex);
                if (!m_state->suspensionRequested || m_state->finished || m_state->canceled) {
                    m_state->suspended = false;
                    return;
                }
                m_state->suspended = true;
                yieldId = SwCoreApplication::generateYieldId();
                sw::detail::SwFutureYieldWaiter waiter;
                waiter.yieldId = yieldId;
                waiter.kind = sw::detail::SwFutureYieldWaitKind::ResumeRequested;
                waiter.index = -1;
                m_state->yieldWaiters.push_back(waiter);
                m_state->condition.notify_all();
            }

            const int timerId = app->addTimer([yieldId]() {
                SwCoreApplication::unYieldFiberHighPriority(yieldId);
            }, 1000, true, SwFiberLane::Control);
            SwCoreApplication::yieldFiber(yieldId);
            app->removeTimer(timerId);

            std::unique_lock<SwMutex> lock(m_state->mutex);
            sw::detail::swFutureRemoveYieldWaiterLocked_(*m_state, yieldId);
            if (!m_state->suspensionRequested || m_state->finished || m_state->canceled) {
                m_state->suspended = false;
                return;
            }
        }
    }

    bool addResult(const T& value, int index = -1) {
        return storeResult_(value, index);
    }

    bool addResult(T&& value, int index = -1) {
        return storeResult_(std::move(value), index);
    }

    bool addResults(const SwVector<T>& values, int index = -1) {
        return addResultsImpl_(values.begin(), values.end(), index);
    }

    bool addResults(const SwList<T>& values, int index = -1) {
        return addResultsImpl_(values.begin(), values.end(), index);
    }

    template<typename... Args>
    bool emplaceResult(int index, Args&&... args) {
        T value(std::forward<Args>(args)...);
        return storeResult_(std::move(value), index);
    }

    void setProgressRange(int minimum, int maximum) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification notification;
        notification.type = sw::detail::SwFutureNotificationType::ProgressRangeChanged;
        notification.progressMinimum = minimum;
        notification.progressMaximum = maximum;
        dispatchCustomNotification_([&](State& state) {
            state.progressMinimum = minimum;
            state.progressMaximum = maximum;
            if (state.progressValue < minimum) {
                state.progressValue = minimum;
            }
            if (state.progressValue > maximum) {
                state.progressValue = maximum;
            }
        }, notification);
    }

    void setProgressValue(int value) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification notification;
        notification.type = sw::detail::SwFutureNotificationType::ProgressValueChanged;
        notification.progressValue = value;
        dispatchCustomNotification_([&](State& state) {
            state.progressValue = value;
        }, notification);
    }

    void setProgressValueAndText(int value, const SwString& text) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification valueNotification;
        valueNotification.type = sw::detail::SwFutureNotificationType::ProgressValueChanged;
        valueNotification.progressValue = value;
        dispatchCustomNotification_([&](State& state) {
            state.progressValue = value;
            state.progressText = text;
        }, valueNotification);

        sw::detail::SwFutureNotification textNotification;
        textNotification.type = sw::detail::SwFutureNotificationType::ProgressTextChanged;
        textNotification.progressValue = value;
        textNotification.progressText = text;
        dispatchCustomNotification_([&](State& state) {
            state.progressValue = value;
            state.progressText = text;
        }, textNotification);
    }

    void swap(SwPromise& other) {
        m_state.swap(other.m_state);
    }

private:
    typedef sw::detail::SwFutureStorage<T> State;

    template<typename Mutator>
    void dispatchNotification_(Mutator mutator, sw::detail::SwFutureNotificationType type) {
        sw::detail::SwFutureNotification notification;
        notification.type = type;
        dispatchCustomNotification_(mutator, notification);
    }

    template<typename Mutator>
    void dispatchCustomNotification_(Mutator mutator,
                                     const sw::detail::SwFutureNotification& notification) {
        if (!m_state) {
            return;
        }

        std::vector<std::function<void(const sw::detail::SwFutureNotification&)> > callbacks;
        SwVector<int> wakeIds;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            mutator(*m_state);
            sw::detail::swFutureDispatchNotificationLocked_(*m_state, notification, callbacks, wakeIds);
        }
        sw::detail::swFutureDeliverCallbacks_(callbacks, notification);
        sw::detail::swFutureWakeYielders_(wakeIds);
    }

    template<typename Iter>
    bool addResultsImpl_(Iter begin, Iter end, int index) {
        if (!m_state) {
            return false;
        }

        bool inserted = false;
        int currentIndex = index;
        for (Iter it = begin; it != end; ++it) {
            if (storeResult_(*it, currentIndex)) {
                inserted = true;
            }
            if (currentIndex >= 0) {
                ++currentIndex;
            }
        }
        return inserted;
    }

    template<typename ValueLike>
    bool storeResult_(ValueLike&& value, int index) {
        if (!m_state) {
            return false;
        }

        bool stored = false;
        sw::detail::SwFutureNotification one;
        one.type = sw::detail::SwFutureNotificationType::ResultReadyAt;
        sw::detail::SwFutureNotification range;
        range.type = sw::detail::SwFutureNotificationType::ResultsReadyAt;

        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            if (m_state->finished) {
                return false;
            }

            sw::detail::swFutureMarkStartedLocked_(*m_state);

            if (index < 0) {
                index = static_cast<int>(m_state->results.size());
            }

            sw::detail::swFutureResizeResultsLocked_(*m_state, index + 1);
            const SwVector<char>::size_type readyIndex = static_cast<SwVector<char>::size_type>(index);
            if (m_state->readyFlags[readyIndex]) {
                return false;
            }

            m_state->results[static_cast<typename SwVector<T>::size_type>(index)] = std::forward<ValueLike>(value);
            m_state->readyFlags[readyIndex] = 1;
            stored = true;
            one.firstIndex = index;
            one.lastIndex = index;
            range.firstIndex = index;
            range.lastIndex = index;
        }

        if (stored) {
            dispatchCustomNotification_([](State&) {}, one);
            dispatchCustomNotification_([](State&) {}, range);
        }
        return stored;
    }

    std::shared_ptr<State> m_state;
};

template<>
class SwPromise<void> {
public:
    typedef void result_type;

    SwPromise()
        : m_state(new State()) {
    }

    SwFuture<void> future() const {
        return SwFuture<void>(m_state);
    }

    void setContinuationScheduler(const std::function<void(std::function<void()>)>& scheduler) {
        if (!m_state) {
            return;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        m_state->continuationScheduler = scheduler;
    }

    void start() {
        if (!m_state) {
            return;
        }

        dispatchNotification_([&](State& state) {
            sw::detail::swFutureMarkStartedLocked_(state);
        }, sw::detail::SwFutureNotificationType::Started);
    }

    void finish() {
        if (!m_state) {
            return;
        }

        dispatchNotification_([&](State& state) {
            sw::detail::swFutureMarkStartedLocked_(state);
            state.finished = true;
            state.suspended = false;
            state.suspensionRequested = false;
        }, sw::detail::SwFutureNotificationType::Finished);
    }

    void cancel() {
        if (!m_state) {
            return;
        }

        dispatchNotification_([&](State& state) {
            state.canceled = true;
            state.finished = true;
            state.suspended = false;
            state.suspensionRequested = false;
        }, sw::detail::SwFutureNotificationType::Canceled);
    }

    void fail(const SwString& errorText) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification notification;
        notification.type = sw::detail::SwFutureNotificationType::Failed;
        notification.progressText = errorText;
        dispatchCustomNotification_([&](State& state) {
            sw::detail::swFutureMarkStartedLocked_(state);
            state.failed = true;
            state.failureText = errorText;
            state.finished = true;
            state.suspended = false;
            state.suspensionRequested = false;
        }, notification);
    }

    bool isCanceled() const {
        if (!m_state) {
            return false;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->canceled;
    }

    bool isSuspensionRequested() const {
        if (!m_state) {
            return false;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->suspensionRequested;
    }

    void suspendIfRequested() {
        if (!m_state) {
            return;
        }

        if (!sw::detail::swFutureCanYieldCurrentFiber_()) {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            while (m_state->suspensionRequested && !m_state->finished && !m_state->canceled) {
                m_state->suspended = true;
                m_state->condition.notify_all();
                m_state->condition.wait(lock);
            }
            m_state->suspended = false;
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return;
        }

        for (;;) {
            int yieldId = 0;
            {
                std::unique_lock<SwMutex> lock(m_state->mutex);
                if (!m_state->suspensionRequested || m_state->finished || m_state->canceled) {
                    m_state->suspended = false;
                    return;
                }
                m_state->suspended = true;
                yieldId = SwCoreApplication::generateYieldId();
                sw::detail::SwFutureYieldWaiter waiter;
                waiter.yieldId = yieldId;
                waiter.kind = sw::detail::SwFutureYieldWaitKind::ResumeRequested;
                waiter.index = -1;
                m_state->yieldWaiters.push_back(waiter);
                m_state->condition.notify_all();
            }

            const int timerId = app->addTimer([yieldId]() {
                SwCoreApplication::unYieldFiberHighPriority(yieldId);
            }, 1000, true, SwFiberLane::Control);
            SwCoreApplication::yieldFiber(yieldId);
            app->removeTimer(timerId);

            std::unique_lock<SwMutex> lock(m_state->mutex);
            sw::detail::swFutureRemoveYieldWaiterLocked_(*m_state, yieldId);
            if (!m_state->suspensionRequested || m_state->finished || m_state->canceled) {
                m_state->suspended = false;
                return;
            }
        }
    }

    void setProgressRange(int minimum, int maximum) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification notification;
        notification.type = sw::detail::SwFutureNotificationType::ProgressRangeChanged;
        notification.progressMinimum = minimum;
        notification.progressMaximum = maximum;
        dispatchCustomNotification_([&](State& state) {
            state.progressMinimum = minimum;
            state.progressMaximum = maximum;
            if (state.progressValue < minimum) {
                state.progressValue = minimum;
            }
            if (state.progressValue > maximum) {
                state.progressValue = maximum;
            }
        }, notification);
    }

    void setProgressValue(int value) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification notification;
        notification.type = sw::detail::SwFutureNotificationType::ProgressValueChanged;
        notification.progressValue = value;
        dispatchCustomNotification_([&](State& state) {
            state.progressValue = value;
        }, notification);
    }

    void setProgressValueAndText(int value, const SwString& text) {
        if (!m_state) {
            return;
        }

        sw::detail::SwFutureNotification valueNotification;
        valueNotification.type = sw::detail::SwFutureNotificationType::ProgressValueChanged;
        valueNotification.progressValue = value;
        dispatchCustomNotification_([&](State& state) {
            state.progressValue = value;
            state.progressText = text;
        }, valueNotification);

        sw::detail::SwFutureNotification textNotification;
        textNotification.type = sw::detail::SwFutureNotificationType::ProgressTextChanged;
        textNotification.progressValue = value;
        textNotification.progressText = text;
        dispatchCustomNotification_([&](State& state) {
            state.progressValue = value;
            state.progressText = text;
        }, textNotification);
    }

    void swap(SwPromise& other) {
        m_state.swap(other.m_state);
    }

private:
    typedef sw::detail::SwFutureStorage<void> State;

    template<typename Mutator>
    void dispatchNotification_(Mutator mutator, sw::detail::SwFutureNotificationType type) {
        sw::detail::SwFutureNotification notification;
        notification.type = type;
        dispatchCustomNotification_(mutator, notification);
    }

    template<typename Mutator>
    void dispatchCustomNotification_(Mutator mutator,
                                     const sw::detail::SwFutureNotification& notification) {
        if (!m_state) {
            return;
        }

        std::vector<std::function<void(const sw::detail::SwFutureNotification&)> > callbacks;
        SwVector<int> wakeIds;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            mutator(*m_state);
            sw::detail::swFutureDispatchNotificationLocked_(*m_state, notification, callbacks, wakeIds);
        }
        sw::detail::swFutureDeliverCallbacks_(callbacks, notification);
        sw::detail::swFutureWakeYielders_(wakeIds);
    }

    std::shared_ptr<State> m_state;
};

namespace sw {
namespace detail {

template<typename T>
struct SwFutureTypeTraits {
    typedef T ContinuationType;
    typedef T ValueType;
    enum { ReturnsFuture = 0 };
};

template<typename T>
struct SwFutureTypeTraits<SwFuture<T> > {
    typedef SwFuture<T> ContinuationType;
    typedef T ValueType;
    enum { ReturnsFuture = 1 };
};

template<typename Out, typename FutureType, typename F, bool ReturnsFuture>
struct SwFutureThenInvokerImpl;

template<typename Out, typename FutureType, typename F>
struct SwFutureThenInvokerImpl<Out, FutureType, F, false> {
    static void invoke(const FutureType& future, SwPromise<Out> promise, F fn) {
        promise.start();
        promise.addResult(fn(future));
        promise.finish();
    }
};

template<typename FutureType, typename F>
struct SwFutureThenInvokerImpl<void, FutureType, F, false> {
    static void invoke(const FutureType& future, SwPromise<void> promise, F fn) {
        promise.start();
        fn(future);
        promise.finish();
    }
};

template<typename Out, typename FutureType, typename F>
struct SwFutureThenInvokerImpl<Out, FutureType, F, true> {
    static void invoke(const FutureType& future, SwPromise<Out> promise, F fn) {
        SwFuture<Out> chained = fn(future);
        chained.onCanceled([promise](SwFuture<Out>) mutable {
            promise.cancel();
        });
        chained.onFailed([promise](SwFuture<Out> failedFuture) mutable {
            promise.fail(failedFuture.failureText());
        });
        chained.thenInline([promise](SwFuture<Out> resolvedFuture) mutable -> Out {
            promise.start();
            Out value = resolvedFuture.result();
            promise.addResult(value);
            promise.finish();
            return value;
        });
    }
};

template<typename FutureType, typename F>
struct SwFutureThenInvokerImpl<void, FutureType, F, true> {
    static void invoke(const FutureType& future, SwPromise<void> promise, F fn) {
        SwFuture<void> chained = fn(future);
        chained.onCanceled([promise](SwFuture<void>) mutable {
            promise.cancel();
        });
        chained.onFailed([promise](SwFuture<void> failedFuture) mutable {
            promise.fail(failedFuture.failureText());
        });
        chained.thenInline([promise](SwFuture<void>) mutable {
            promise.start();
            promise.finish();
        });
    }
};

template<typename Out, typename FutureType, typename F>
struct SwFutureThenInvoker {
    typedef typename std::result_of<F(FutureType)>::type RawType;
    static void invoke(const FutureType& future, SwPromise<Out> promise, F fn) {
        SwFutureThenInvokerImpl<Out, FutureType, F, SwFutureTypeTraits<RawType>::ReturnsFuture != 0>::invoke(future, promise, fn);
    }
};

inline std::function<void(std::function<void()>)> swFutureInlineScheduler_() {
    return std::function<void(std::function<void()>)>();
}

inline std::function<void(std::function<void()>)> swFutureThreadPoolScheduler_(SwThreadPool* pool) {
    SwThreadPool* actualPool = pool ? pool : SwThreadPool::globalInstance();
    return [actualPool](std::function<void()> task) {
        actualPool->start(task);
    };
}

inline std::function<void(std::function<void()>)> swFutureAppLaneScheduler_(SwCoreApplication* app, SwFiberLane lane) {
    return [app, lane](std::function<void()> task) {
        if (app) {
            app->postEventOnLane(task, lane);
        } else {
            task();
        }
    };
}

template<typename Out, typename T, typename F, bool ReturnsFuture>
struct SwFutureValueThenInvokerImpl;

template<typename Out, typename T, typename F>
struct SwFutureValueThenInvokerImpl<Out, T, F, false> {
    static void invoke(const SwFuture<T>& future, SwPromise<Out> promise, F fn) {
        promise.start();
        promise.addResult(fn(future.result()));
        promise.finish();
    }
};

template<typename T, typename F>
struct SwFutureValueThenInvokerImpl<void, T, F, false> {
    static void invoke(const SwFuture<T>& future, SwPromise<void> promise, F fn) {
        promise.start();
        fn(future.result());
        promise.finish();
    }
};

template<typename Out, typename T, typename F>
struct SwFutureValueThenInvokerImpl<Out, T, F, true> {
    static void invoke(const SwFuture<T>& future, SwPromise<Out> promise, F fn) {
        SwFuture<Out> chained = fn(future.result());
        chained.onCanceled([promise](SwFuture<Out>) mutable {
            promise.cancel();
        });
        chained.onFailed([promise](SwFuture<Out> failedFuture) mutable {
            promise.fail(failedFuture.failureText());
        });
        chained.thenInline([promise](SwFuture<Out> resolvedFuture) mutable -> Out {
            promise.start();
            Out value = resolvedFuture.result();
            promise.addResult(value);
            promise.finish();
            return value;
        });
    }
};

template<typename T, typename F>
struct SwFutureValueThenInvokerImpl<void, T, F, true> {
    static void invoke(const SwFuture<T>& future, SwPromise<void> promise, F fn) {
        SwFuture<void> chained = fn(future.result());
        chained.onCanceled([promise](SwFuture<void>) mutable {
            promise.cancel();
        });
        chained.onFailed([promise](SwFuture<void> failedFuture) mutable {
            promise.fail(failedFuture.failureText());
        });
        chained.thenInline([promise](SwFuture<void>) mutable {
            promise.start();
            promise.finish();
        });
    }
};

template<typename Out, typename T, typename F>
struct SwFutureValueThenInvoker {
    typedef typename std::result_of<F(T)>::type RawType;
    static void invoke(const SwFuture<T>& future, SwPromise<Out> promise, F fn) {
        SwFutureValueThenInvokerImpl<Out, T, F, SwFutureTypeTraits<RawType>::ReturnsFuture != 0>::invoke(future, promise, fn);
    }
};

template<typename Out, typename F, bool ReturnsFuture>
struct SwFutureVoidThenInvokerImpl;

template<typename Out, typename F>
struct SwFutureVoidThenInvokerImpl<Out, F, false> {
    static void invoke(const SwFuture<void>& future, SwPromise<Out> promise, F fn) {
        (void)future;
        promise.start();
        promise.addResult(fn());
        promise.finish();
    }
};

template<typename F>
struct SwFutureVoidThenInvokerImpl<void, F, false> {
    static void invoke(const SwFuture<void>& future, SwPromise<void> promise, F fn) {
        (void)future;
        promise.start();
        fn();
        promise.finish();
    }
};

template<typename Out, typename F>
struct SwFutureVoidThenInvokerImpl<Out, F, true> {
    static void invoke(const SwFuture<void>& future, SwPromise<Out> promise, F fn) {
        (void)future;
        SwFuture<Out> chained = fn();
        chained.onCanceled([promise](SwFuture<Out>) mutable {
            promise.cancel();
        });
        chained.onFailed([promise](SwFuture<Out> failedFuture) mutable {
            promise.fail(failedFuture.failureText());
        });
        chained.thenInline([promise](SwFuture<Out> resolvedFuture) mutable -> Out {
            promise.start();
            Out value = resolvedFuture.result();
            promise.addResult(value);
            promise.finish();
            return value;
        });
    }
};

template<typename F>
struct SwFutureVoidThenInvokerImpl<void, F, true> {
    static void invoke(const SwFuture<void>& future, SwPromise<void> promise, F fn) {
        (void)future;
        SwFuture<void> chained = fn();
        chained.onCanceled([promise](SwFuture<void>) mutable {
            promise.cancel();
        });
        chained.onFailed([promise](SwFuture<void> failedFuture) mutable {
            promise.fail(failedFuture.failureText());
        });
        chained.thenInline([promise](SwFuture<void>) mutable {
            promise.start();
            promise.finish();
        });
    }
};

template<typename Out, typename F>
struct SwFutureVoidThenInvoker {
    typedef typename std::result_of<F()>::type RawType;
    static void invoke(const SwFuture<void>& future, SwPromise<Out> promise, F fn) {
        SwFutureVoidThenInvokerImpl<Out, F, SwFutureTypeTraits<RawType>::ReturnsFuture != 0>::invoke(future, promise, fn);
    }
};

} // namespace detail
} // namespace sw

template<typename T>
template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value,
                        SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F(SwFuture<T>)>::type>::ValueType> >::type
SwFuture<T>::then(F fn) const {
    typedef typename std::result_of<F(SwFuture<T>)>::type RawType;
    typedef typename sw::detail::SwFutureTypeTraits<RawType>::ValueType OutType;
    SwPromise<OutType> nextPromise;
    nextPromise.setContinuationScheduler(continuationScheduler());
    SwFuture<OutType> nextFuture = nextPromise.future();
    const SwFuture<T> self = *this;

    addObserver_([self, nextPromise, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Canceled) {
            nextPromise.cancel();
            return;
        }
        if (notification.type == sw::detail::SwFutureNotificationType::Failed) {
            nextPromise.fail(self.failureText());
            return;
        }
        if (notification.type != sw::detail::SwFutureNotificationType::Finished) {
            return;
        }
        if (self.isCanceled()) {
            nextPromise.cancel();
            return;
        }
        if (self.isFailed()) {
            nextPromise.fail(self.failureText());
            return;
        }
        self.scheduleContinuation_([self, nextPromise, fn]() mutable {
            sw::detail::SwFutureThenInvoker<OutType, SwFuture<T>, F>::invoke(self, nextPromise, fn);
        });
    }, true);

    return nextFuture;
}

template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value,
                        SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F(SwFuture<void>)>::type>::ValueType> >::type
SwFuture<void>::then(F fn) const {
    typedef typename std::result_of<F(SwFuture<void>)>::type RawType;
    typedef typename sw::detail::SwFutureTypeTraits<RawType>::ValueType OutType;
    SwPromise<OutType> nextPromise;
    nextPromise.setContinuationScheduler(continuationScheduler());
    SwFuture<OutType> nextFuture = nextPromise.future();
    const SwFuture<void> self = *this;

    addObserver_([self, nextPromise, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Canceled) {
            nextPromise.cancel();
            return;
        }
        if (notification.type == sw::detail::SwFutureNotificationType::Failed) {
            nextPromise.fail(self.failureText());
            return;
        }
        if (notification.type != sw::detail::SwFutureNotificationType::Finished) {
            return;
        }
        if (self.isCanceled()) {
            nextPromise.cancel();
            return;
        }
        if (self.isFailed()) {
            nextPromise.fail(self.failureText());
            return;
        }
        self.scheduleContinuation_([self, nextPromise, fn]() mutable {
            sw::detail::SwFutureThenInvoker<OutType, SwFuture<void>, F>::invoke(self, nextPromise, fn);
        });
    }, true);

    return nextFuture;
}

template<typename T>
template<typename F>
typename std::enable_if<!sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value &&
                        sw::detail::SwFutureIsInvocable<F, T>::value,
                        SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F(T)>::type>::ValueType> >::type
SwFuture<T>::then(F fn) const {
    typedef typename std::result_of<F(T)>::type RawType;
    typedef typename sw::detail::SwFutureTypeTraits<RawType>::ValueType OutType;
    SwPromise<OutType> nextPromise;
    nextPromise.setContinuationScheduler(continuationScheduler());
    SwFuture<OutType> nextFuture = nextPromise.future();
    const SwFuture<T> self = *this;

    addObserver_([self, nextPromise, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Canceled) {
            nextPromise.cancel();
            return;
        }
        if (notification.type == sw::detail::SwFutureNotificationType::Failed) {
            nextPromise.fail(self.failureText());
            return;
        }
        if (notification.type != sw::detail::SwFutureNotificationType::Finished) {
            return;
        }
        if (self.isCanceled()) {
            nextPromise.cancel();
            return;
        }
        if (self.isFailed()) {
            nextPromise.fail(self.failureText());
            return;
        }
        self.scheduleContinuation_([self, nextPromise, fn]() mutable {
            sw::detail::SwFutureValueThenInvoker<OutType, T, F>::invoke(self, nextPromise, fn);
        });
    }, true);

    return nextFuture;
}

template<typename F>
typename std::enable_if<!sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value &&
                        sw::detail::SwFutureIsInvocable<F, void>::value,
                        SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F()>::type>::ValueType> >::type
SwFuture<void>::then(F fn) const {
    typedef typename std::result_of<F()>::type RawType;
    typedef typename sw::detail::SwFutureTypeTraits<RawType>::ValueType OutType;
    SwPromise<OutType> nextPromise;
    nextPromise.setContinuationScheduler(continuationScheduler());
    SwFuture<OutType> nextFuture = nextPromise.future();
    const SwFuture<void> self = *this;

    addObserver_([self, nextPromise, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Canceled) {
            nextPromise.cancel();
            return;
        }
        if (notification.type == sw::detail::SwFutureNotificationType::Failed) {
            nextPromise.fail(self.failureText());
            return;
        }
        if (notification.type != sw::detail::SwFutureNotificationType::Finished) {
            return;
        }
        if (self.isCanceled()) {
            nextPromise.cancel();
            return;
        }
        if (self.isFailed()) {
            nextPromise.fail(self.failureText());
            return;
        }
        self.scheduleContinuation_([self, nextPromise, fn]() mutable {
            sw::detail::SwFutureVoidThenInvoker<OutType, F>::invoke(self, nextPromise, fn);
        });
    }, true);

    return nextFuture;
}

template<typename T>
template<typename F>
SwFuture<T> SwFuture<T>::onCanceled(F fn) const {
    const SwFuture<T> self = *this;
    addObserver_([self, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Canceled) {
            fn(self);
        }
    }, true);
    return self;
}

template<typename T>
template<typename F>
SwFuture<T> SwFuture<T>::onFailed(F fn) const {
    const SwFuture<T> self = *this;
    addObserver_([self, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Failed) {
            fn(self);
        }
    }, true);
    return self;
}

template<typename T>
template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value ||
                        sw::detail::SwFutureIsInvocable<F, T>::value,
                        decltype(std::declval<SwFuture<T> >().then(std::declval<F>()))>::type
SwFuture<T>::thenInline(F fn) const {
    SwFuture<T> copy = *this;
    copy.setContinuationScheduler(sw::detail::swFutureInlineScheduler_());
    return copy.then(fn);
}

template<typename T>
template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value ||
                        sw::detail::SwFutureIsInvocable<F, T>::value,
                        decltype(std::declval<SwFuture<T> >().then(std::declval<F>()))>::type
SwFuture<T>::thenOnThreadPool(SwThreadPool* pool, F fn) const {
    SwFuture<T> copy = *this;
    copy.setContinuationScheduler(sw::detail::swFutureThreadPoolScheduler_(pool));
    return copy.then(fn);
}

template<typename T>
template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value ||
                        sw::detail::SwFutureIsInvocable<F, T>::value,
                        decltype(std::declval<SwFuture<T> >().then(std::declval<F>()))>::type
SwFuture<T>::thenOnAppLane(SwCoreApplication* app, SwFiberLane lane, F fn) const {
    SwFuture<T> copy = *this;
    copy.setContinuationScheduler(sw::detail::swFutureAppLaneScheduler_(app, lane));
    return copy.then(fn);
}

template<typename F>
SwFuture<void> SwFuture<void>::onCanceled(F fn) const {
    const SwFuture<void> self = *this;
    addObserver_([self, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Canceled) {
            fn(self);
        }
    }, true);
    return self;
}

template<typename F>
SwFuture<void> SwFuture<void>::onFailed(F fn) const {
    const SwFuture<void> self = *this;
    addObserver_([self, fn](const Notification& notification) mutable {
        if (notification.type == sw::detail::SwFutureNotificationType::Failed) {
            fn(self);
        }
    }, true);
    return self;
}

template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value ||
                        sw::detail::SwFutureIsInvocable<F, void>::value,
                        decltype(std::declval<SwFuture<void> >().then(std::declval<F>()))>::type
SwFuture<void>::thenInline(F fn) const {
    SwFuture<void> copy = *this;
    copy.setContinuationScheduler(sw::detail::swFutureInlineScheduler_());
    return copy.then(fn);
}

template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value ||
                        sw::detail::SwFutureIsInvocable<F, void>::value,
                        decltype(std::declval<SwFuture<void> >().then(std::declval<F>()))>::type
SwFuture<void>::thenOnThreadPool(SwThreadPool* pool, F fn) const {
    SwFuture<void> copy = *this;
    copy.setContinuationScheduler(sw::detail::swFutureThreadPoolScheduler_(pool));
    return copy.then(fn);
}

template<typename F>
typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value ||
                        sw::detail::SwFutureIsInvocable<F, void>::value,
                        decltype(std::declval<SwFuture<void> >().then(std::declval<F>()))>::type
SwFuture<void>::thenOnAppLane(SwCoreApplication* app, SwFiberLane lane, F fn) const {
    SwFuture<void> copy = *this;
    copy.setContinuationScheduler(sw::detail::swFutureAppLaneScheduler_(app, lane));
    return copy.then(fn);
}
