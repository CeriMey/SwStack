#pragma once

/**
 * @file src/core/runtime/SwFuture.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwFuture in the CoreSw runtime layer.
 */

#include "SwCoreApplication.h"
#include "SwMutex.h"
#include "SwString.h"
#include "SwList.h"
#include "SwVector.h"

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>

template<typename T>
class SwPromise;

template<typename T>
class SwFutureWatcher;

class SwThreadPool;

namespace sw {
namespace detail {

template<typename T>
struct SwFutureTypeTraits;

template<typename F, typename Signature>
struct SwFutureIsInvocable;

template<typename F, typename Arg>
struct SwFutureIsInvocable {
private:
    template<typename U>
    static char test(typename std::result_of<U(Arg)>::type*);

    template<typename U>
    static long test(...);

public:
    enum { value = sizeof(test<F>(nullptr)) == sizeof(char) };
};

template<typename F>
struct SwFutureIsInvocable<F, void> {
private:
    template<typename U>
    static char test(typename std::result_of<U()>::type*);

    template<typename U>
    static long test(...);

public:
    enum { value = sizeof(test<F>(nullptr)) == sizeof(char) };
};

enum class SwFutureNotificationType {
    Started,
    Finished,
    Failed,
    Canceled,
    Suspended,
    Resumed,
    ResultReadyAt,
    ResultsReadyAt,
    ProgressRangeChanged,
    ProgressValueChanged,
    ProgressTextChanged
};

struct SwFutureNotification {
    SwFutureNotificationType type;
    int firstIndex;
    int lastIndex;
    int progressMinimum;
    int progressMaximum;
    int progressValue;
    SwString progressText;
    SwString failureText;

    SwFutureNotification()
        : type(SwFutureNotificationType::Finished),
          firstIndex(-1),
          lastIndex(-1),
          progressMinimum(0),
          progressMaximum(0),
          progressValue(0) {
    }
};

enum class SwFutureYieldWaitKind {
    Finished,
    ResultReady,
    ResumeRequested
};

struct SwFutureYieldWaiter {
    int yieldId;
    SwFutureYieldWaitKind kind;
    int index;

    SwFutureYieldWaiter()
        : yieldId(0),
          kind(SwFutureYieldWaitKind::Finished),
          index(-1) {
    }
};

template<typename T>
struct SwFutureStorage {
    typedef T ValueType;

    SwMutex mutex;
    std::condition_variable_any condition;
    bool started;
    bool finished;
    bool failed;
    bool canceled;
    bool suspensionRequested;
    bool suspended;
    int progressMinimum;
    int progressMaximum;
    int progressValue;
    SwString progressText;
    SwString failureText;
    SwVector<ValueType> results;
    SwVector<char> readyFlags;
    SwVector<SwFutureYieldWaiter> yieldWaiters;
    std::map<int, std::function<void(const SwFutureNotification&)> > observers;
    int nextObserverId;
    std::function<void(std::function<void()>)> continuationScheduler;

    SwFutureStorage()
        : mutex(SwMutex::NonRecursive),
          started(false),
          finished(false),
          failed(false),
          canceled(false),
          suspensionRequested(false),
          suspended(false),
          progressMinimum(0),
          progressMaximum(0),
          progressValue(0),
          nextObserverId(1) {
    }
};

template<>
struct SwFutureStorage<void> {
    SwMutex mutex;
    std::condition_variable_any condition;
    bool started;
    bool finished;
    bool failed;
    bool canceled;
    bool suspensionRequested;
    bool suspended;
    int progressMinimum;
    int progressMaximum;
    int progressValue;
    SwString progressText;
    SwString failureText;
    SwVector<SwFutureYieldWaiter> yieldWaiters;
    std::map<int, std::function<void(const SwFutureNotification&)> > observers;
    int nextObserverId;
    std::function<void(std::function<void()>)> continuationScheduler;

    SwFutureStorage()
        : mutex(SwMutex::NonRecursive),
          started(false),
          finished(false),
          failed(false),
          canceled(false),
          suspensionRequested(false),
          suspended(false),
          progressMinimum(0),
          progressMaximum(0),
          progressValue(0),
          nextObserverId(1) {
    }
};

template<typename T>
inline int swFutureContiguousResultCountLocked_(const SwFutureStorage<T>& state) {
    int count = 0;
    const int size = static_cast<int>(state.readyFlags.size());
    while (count < size && state.readyFlags[static_cast<SwVector<char>::size_type>(count)]) {
        ++count;
    }
    return count;
}

template<typename T>
inline bool swFutureResultReadyLocked_(const SwFutureStorage<T>& state, int index) {
    return index >= 0 &&
           index < static_cast<int>(state.readyFlags.size()) &&
           state.readyFlags[static_cast<SwVector<char>::size_type>(index)] != 0;
}

template<typename T>
inline void swFutureMarkStartedLocked_(SwFutureStorage<T>& state) {
    if (!state.started) {
        state.started = true;
    }
}

inline void swFutureMarkStartedLocked_(SwFutureStorage<void>& state) {
    if (!state.started) {
        state.started = true;
    }
}

template<typename T>
inline void swFutureResizeResultsLocked_(SwFutureStorage<T>& state, int size) {
    while (static_cast<int>(state.results.size()) < size) {
        state.results.push_back(T());
    }
    while (static_cast<int>(state.readyFlags.size()) < size) {
        state.readyFlags.push_back(0);
    }
}

template<typename TState>
inline bool swFutureWaitConditionSatisfiedLocked_(const TState& state,
                                                  SwFutureYieldWaitKind kind,
                                                  int index) {
    switch (kind) {
    case SwFutureYieldWaitKind::Finished:
        return state.finished;
    case SwFutureYieldWaitKind::ResumeRequested:
        return !state.suspensionRequested || state.finished || state.canceled;
    case SwFutureYieldWaitKind::ResultReady:
        return state.finished || swFutureResultReadyLocked_(state, index);
    }
    return state.finished;
}

template<>
inline bool swFutureWaitConditionSatisfiedLocked_(const SwFutureStorage<void>& state,
                                                  SwFutureYieldWaitKind kind,
                                                  int index) {
    (void)index;
    switch (kind) {
    case SwFutureYieldWaitKind::Finished:
        return state.finished;
    case SwFutureYieldWaitKind::ResumeRequested:
        return !state.suspensionRequested || state.finished || state.canceled;
    case SwFutureYieldWaitKind::ResultReady:
        return state.finished;
    }
    return state.finished;
}

template<typename TState>
inline void swFutureCollectWakeIdsLocked_(TState& state, SwVector<int>& wakeIds) {
    for (int i = static_cast<int>(state.yieldWaiters.size()) - 1; i >= 0; --i) {
        const SwFutureYieldWaiter& waiter = state.yieldWaiters[static_cast<SwVector<SwFutureYieldWaiter>::size_type>(i)];
        if (!swFutureWaitConditionSatisfiedLocked_(state, waiter.kind, waiter.index)) {
            continue;
        }
        wakeIds.push_back(waiter.yieldId);
        state.yieldWaiters.removeAt(i);
    }
}

template<typename TState>
inline void swFutureRemoveYieldWaiterLocked_(TState& state, int yieldId) {
    for (int i = static_cast<int>(state.yieldWaiters.size()) - 1; i >= 0; --i) {
        if (state.yieldWaiters[static_cast<SwVector<SwFutureYieldWaiter>::size_type>(i)].yieldId == yieldId) {
            state.yieldWaiters.removeAt(i);
        }
    }
}

template<typename TState>
inline void swFutureDispatchNotificationLocked_(TState& state,
                                                const SwFutureNotification& notification,
                                                std::vector<std::function<void(const SwFutureNotification&)> >& callbacks,
                                                SwVector<int>& wakeIds) {
    for (typename std::map<int, std::function<void(const SwFutureNotification&)> >::const_iterator it = state.observers.begin();
         it != state.observers.end();
         ++it) {
        callbacks.push_back(it->second);
    }
    swFutureCollectWakeIdsLocked_(state, wakeIds);
    state.condition.notify_all();
    (void)notification;
}

inline void swFutureDeliverCallbacks_(const std::vector<std::function<void(const SwFutureNotification&)> >& callbacks,
                                      const SwFutureNotification& notification) {
    for (size_t i = 0; i < callbacks.size(); ++i) {
        callbacks[i](notification);
    }
}

inline void swFutureWakeYielders_(const SwVector<int>& wakeIds) {
    for (int i = 0; i < static_cast<int>(wakeIds.size()); ++i) {
        SwCoreApplication::unYieldFiberHighPriority(wakeIds[static_cast<SwVector<int>::size_type>(i)]);
    }
}

template<typename TState>
inline int swFutureAddObserverLocked_(TState& state,
                                      const std::function<void(const SwFutureNotification&)>& callback) {
    const int id = state.nextObserverId++;
    state.observers[id] = callback;
    return id;
}

template<typename TState>
inline void swFutureRemoveObserverLocked_(TState& state, int id) {
    typename std::map<int, std::function<void(const SwFutureNotification&)> >::iterator it = state.observers.find(id);
    if (it != state.observers.end()) {
        state.observers.erase(it);
    }
}

template<typename T>
inline void swFutureBuildSnapshotLocked_(const SwFutureStorage<T>& state,
                                         SwVector<SwFutureNotification>& notifications) {
    if (state.started) {
        SwFutureNotification started;
        started.type = SwFutureNotificationType::Started;
        notifications.push_back(started);
    }

    SwFutureNotification range;
    range.type = SwFutureNotificationType::ProgressRangeChanged;
    range.progressMinimum = state.progressMinimum;
    range.progressMaximum = state.progressMaximum;
    notifications.push_back(range);

    SwFutureNotification value;
    value.type = SwFutureNotificationType::ProgressValueChanged;
    value.progressValue = state.progressValue;
    notifications.push_back(value);

    if (!state.progressText.isEmpty()) {
        SwFutureNotification text;
        text.type = SwFutureNotificationType::ProgressTextChanged;
        text.progressText = state.progressText;
        text.progressValue = state.progressValue;
        notifications.push_back(text);
    }

    for (int i = 0; i < static_cast<int>(state.readyFlags.size()); ++i) {
        if (!state.readyFlags[static_cast<SwVector<char>::size_type>(i)]) {
            continue;
        }
        SwFutureNotification one;
        one.type = SwFutureNotificationType::ResultReadyAt;
        one.firstIndex = i;
        one.lastIndex = i;
        notifications.push_back(one);

        SwFutureNotification all;
        all.type = SwFutureNotificationType::ResultsReadyAt;
        all.firstIndex = i;
        all.lastIndex = i;
        notifications.push_back(all);
    }

    if (state.canceled) {
        SwFutureNotification canceled;
        canceled.type = SwFutureNotificationType::Canceled;
        notifications.push_back(canceled);
    }

    if (state.failed) {
        SwFutureNotification failed;
        failed.type = SwFutureNotificationType::Failed;
        failed.progressText = state.failureText;
        notifications.push_back(failed);
    }

    if (state.suspended) {
        SwFutureNotification suspended;
        suspended.type = SwFutureNotificationType::Suspended;
        notifications.push_back(suspended);
    }

    if (state.finished) {
        SwFutureNotification finished;
        finished.type = SwFutureNotificationType::Finished;
        notifications.push_back(finished);
    }
}

inline void swFutureBuildSnapshotLocked_(const SwFutureStorage<void>& state,
                                         SwVector<SwFutureNotification>& notifications) {
    if (state.started) {
        SwFutureNotification started;
        started.type = SwFutureNotificationType::Started;
        notifications.push_back(started);
    }

    SwFutureNotification range;
    range.type = SwFutureNotificationType::ProgressRangeChanged;
    range.progressMinimum = state.progressMinimum;
    range.progressMaximum = state.progressMaximum;
    notifications.push_back(range);

    SwFutureNotification value;
    value.type = SwFutureNotificationType::ProgressValueChanged;
    value.progressValue = state.progressValue;
    notifications.push_back(value);

    if (!state.progressText.isEmpty()) {
        SwFutureNotification text;
        text.type = SwFutureNotificationType::ProgressTextChanged;
        text.progressText = state.progressText;
        text.progressValue = state.progressValue;
        notifications.push_back(text);
    }

    if (state.canceled) {
        SwFutureNotification canceled;
        canceled.type = SwFutureNotificationType::Canceled;
        notifications.push_back(canceled);
    }

    if (state.failed) {
        SwFutureNotification failed;
        failed.type = SwFutureNotificationType::Failed;
        failed.progressText = state.failureText;
        notifications.push_back(failed);
    }

    if (state.suspended) {
        SwFutureNotification suspended;
        suspended.type = SwFutureNotificationType::Suspended;
        notifications.push_back(suspended);
    }

    if (state.finished) {
        SwFutureNotification finished;
        finished.type = SwFutureNotificationType::Finished;
        notifications.push_back(finished);
    }
}

inline bool swFutureCanYieldCurrentFiber_() {
    SwCoreApplication* app = SwCoreApplication::instance(false);
    if (!app) {
        return false;
    }
    LPVOID current = GetCurrentFiber();
    return current && app->fiberPool().isWarmInitialized();
}

} // namespace detail
} // namespace sw

template<typename T>
class SwFuture {
    friend class SwPromise<T>;
    friend class SwFutureWatcher<T>;

public:
    typedef T result_type;

    SwFuture() {
    }

    bool isValid() const {
        return static_cast<bool>(m_state);
    }

    bool isStarted() const {
        return withLockBool_([](const State& state) { return state.started; });
    }

    bool isFinished() const {
        return withLockBool_([](const State& state) { return state.finished; });
    }

    bool isRunning() const {
        return withLockBool_([](const State& state) { return state.started && !state.finished; });
    }

    bool isCanceled() const {
        return withLockBool_([](const State& state) { return state.canceled; });
    }

    bool isFailed() const {
        return withLockBool_([](const State& state) { return state.failed; });
    }

    bool isSuspending() const {
        return withLockBool_([](const State& state) {
            return state.suspensionRequested && !state.suspended && !state.finished;
        });
    }

    bool isSuspended() const {
        return withLockBool_([](const State& state) {
            return state.suspended && !state.finished;
        });
    }

    bool isPaused() const {
        return isSuspended();
    }

    void waitForFinished() const {
        waitForCondition_(sw::detail::SwFutureYieldWaitKind::Finished, -1);
    }

    bool waitForResult(int index) const {
        return waitForCondition_(sw::detail::SwFutureYieldWaitKind::ResultReady, index);
    }

    void cancel() {
        if (!m_state) {
            return;
        }

        std::vector<std::function<void(const Notification&)> > callbacks;
        SwVector<int> wakeIds;
        Notification notification;
        notification.type = sw::detail::SwFutureNotificationType::Canceled;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            if (m_state->canceled) {
                return;
            }
            m_state->canceled = true;
            sw::detail::swFutureDispatchNotificationLocked_(*m_state, notification, callbacks, wakeIds);
        }
        sw::detail::swFutureDeliverCallbacks_(callbacks, notification);
        sw::detail::swFutureWakeYielders_(wakeIds);
    }

    void suspend() {
        setSuspended(true);
    }

    void pause() {
        setSuspended(true);
    }

    void resume() {
        setSuspended(false);
    }

    void setSuspended(bool suspend) {
        if (!m_state) {
            return;
        }

        std::vector<std::function<void(const Notification&)> > callbacks;
        SwVector<int> wakeIds;
        Notification notification;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            if (m_state->finished || m_state->suspensionRequested == suspend) {
                return;
            }
            m_state->suspensionRequested = suspend;
            if (!suspend) {
                m_state->suspended = false;
                notification.type = sw::detail::SwFutureNotificationType::Resumed;
            } else {
                notification.type = sw::detail::SwFutureNotificationType::Suspended;
            }
            sw::detail::swFutureDispatchNotificationLocked_(*m_state, notification, callbacks, wakeIds);
        }
        sw::detail::swFutureDeliverCallbacks_(callbacks, notification);
        sw::detail::swFutureWakeYielders_(wakeIds);
    }

    void toggleSuspended() {
        setSuspended(!isSuspended() && !isSuspending());
    }

    void setPaused(bool paused) {
        setSuspended(paused);
    }

    void togglePaused() {
        toggleSuspended();
    }

    bool isResultReadyAt(int index) const {
        return withLockBool_([index](const State& state) {
            return sw::detail::swFutureResultReadyLocked_(state, index);
        });
    }

    int resultCount() const {
        return withLockInt_([](const State& state) {
            return sw::detail::swFutureContiguousResultCountLocked_(state);
        });
    }

    int progressMinimum() const {
        return withLockInt_([](const State& state) { return state.progressMinimum; });
    }

    int progressMaximum() const {
        return withLockInt_([](const State& state) { return state.progressMaximum; });
    }

    int progressValue() const {
        return withLockInt_([](const State& state) { return state.progressValue; });
    }

    SwString progressText() const {
        if (!m_state) {
            return SwString();
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->progressText;
    }

    SwString failureText() const {
        if (!m_state) {
            return SwString();
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->failureText;
    }

    T result() const {
        return resultAt(0);
    }

    T resultAt(int index) const {
        if (!m_state || index < 0) {
            return T();
        }

        std::unique_lock<SwMutex> lock(m_state->mutex);
        while (!sw::detail::swFutureResultReadyLocked_(*m_state, index) && !m_state->finished) {
            m_state->condition.wait(lock);
        }

        if (!sw::detail::swFutureResultReadyLocked_(*m_state, index)) {
            return T();
        }

        return m_state->results[static_cast<typename SwVector<T>::size_type>(index)];
    }

    T takeResult() {
        if (!m_state) {
            return T();
        }

        waitForFinished();

        std::unique_lock<SwMutex> lock(m_state->mutex);
        const int count = static_cast<int>(m_state->results.size());
        for (int i = 0; i < count; ++i) {
            const SwVector<char>::size_type readyIndex = static_cast<SwVector<char>::size_type>(i);
            if (!m_state->readyFlags[readyIndex]) {
                continue;
            }

            T value = std::move(m_state->results[static_cast<typename SwVector<T>::size_type>(i)]);
            m_state->readyFlags[readyIndex] = 0;
            return value;
        }

        return T();
    }

    SwVector<T> takeResults() {
        SwVector<T> values;
        if (!m_state) {
            return values;
        }

        waitForFinished();

        std::unique_lock<SwMutex> lock(m_state->mutex);
        const int count = static_cast<int>(m_state->results.size());
        for (int i = 0; i < count; ++i) {
            const SwVector<char>::size_type readyIndex = static_cast<SwVector<char>::size_type>(i);
            if (!m_state->readyFlags[readyIndex]) {
                continue;
            }

            values.push_back(std::move(m_state->results[static_cast<typename SwVector<T>::size_type>(i)]));
            m_state->readyFlags[readyIndex] = 0;
        }

        return values;
    }

    SwVector<T> results() const {
        SwVector<T> values;
        if (!m_state) {
            return values;
        }

        waitForFinished();

        std::unique_lock<SwMutex> lock(m_state->mutex);
        const int count = static_cast<int>(m_state->results.size());
        for (int i = 0; i < count; ++i) {
            if (m_state->readyFlags[static_cast<SwVector<char>::size_type>(i)]) {
                values.push_back(m_state->results[static_cast<typename SwVector<T>::size_type>(i)]);
            }
        }
        return values;
    }

    SwList<T> resultsAsList() const {
        const SwVector<T> values = results();
        return SwList<T>(values.begin(), values.end());
    }

    void swap(SwFuture& other) {
        m_state.swap(other.m_state);
    }

    void setContinuationScheduler(const std::function<void(std::function<void()>)>& scheduler) {
        if (!m_state) {
            return;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        m_state->continuationScheduler = scheduler;
    }

    std::function<void(std::function<void()>)> continuationScheduler() const {
        if (!m_state) {
            return std::function<void(std::function<void()>)>();
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->continuationScheduler;
    }

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value,
                            SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F(SwFuture<T>)>::type>::ValueType> >::type
    then(F fn) const;

    template<typename F>
    typename std::enable_if<!sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value &&
                            sw::detail::SwFutureIsInvocable<F, T>::value,
                            SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F(T)>::type>::ValueType> >::type
    then(F fn) const;

    template<typename F>
    SwFuture<T> onCanceled(F fn) const;

    template<typename F>
    SwFuture<T> onFailed(F fn) const;

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value ||
                            sw::detail::SwFutureIsInvocable<F, T>::value,
                            decltype(std::declval<SwFuture<T> >().then(std::declval<F>()))>::type
    thenInline(F fn) const;

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value ||
                            sw::detail::SwFutureIsInvocable<F, T>::value,
                            decltype(std::declval<SwFuture<T> >().then(std::declval<F>()))>::type
    thenOnThreadPool(SwThreadPool* pool, F fn) const;

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<T> >::value ||
                            sw::detail::SwFutureIsInvocable<F, T>::value,
                            decltype(std::declval<SwFuture<T> >().then(std::declval<F>()))>::type
    thenOnAppLane(SwCoreApplication* app, SwFiberLane lane, F fn) const;

private:
    typedef sw::detail::SwFutureStorage<T> State;
    typedef sw::detail::SwFutureNotification Notification;

    explicit SwFuture(const std::shared_ptr<State>& state)
        : m_state(state) {
    }

    bool waitForCondition_(sw::detail::SwFutureYieldWaitKind kind, int index) const {
        if (!m_state) {
            return false;
        }

        if (!sw::detail::swFutureCanYieldCurrentFiber_()) {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            while (!sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index)) {
                m_state->condition.wait(lock);
            }
            return sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index);
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return false;
        }

        for (;;) {
            const int yieldId = SwCoreApplication::generateYieldId();
            {
                std::unique_lock<SwMutex> lock(m_state->mutex);
                if (sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index)) {
                    return true;
                }
                sw::detail::SwFutureYieldWaiter waiter;
                waiter.yieldId = yieldId;
                waiter.kind = kind;
                waiter.index = index;
                m_state->yieldWaiters.push_back(waiter);
            }

            const int timerId = app->addTimer([yieldId]() {
                SwCoreApplication::unYieldFiberHighPriority(yieldId);
            }, 1000, true, SwFiberLane::Control);

            SwCoreApplication::yieldFiber(yieldId);
            app->removeTimer(timerId);

            std::unique_lock<SwMutex> lock(m_state->mutex);
            sw::detail::swFutureRemoveYieldWaiterLocked_(*m_state, yieldId);
            if (sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index)) {
                return true;
            }
        }
    }

    int addObserver_(const std::function<void(const Notification&)>& callback, bool replayCurrent) const {
        if (!m_state) {
            return 0;
        }

        int id = 0;
        SwVector<Notification> snapshot;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            id = sw::detail::swFutureAddObserverLocked_(*m_state, callback);
            if (replayCurrent) {
                sw::detail::swFutureBuildSnapshotLocked_(*m_state, snapshot);
            }
        }

        if (replayCurrent) {
            for (int i = 0; i < static_cast<int>(snapshot.size()); ++i) {
                callback(snapshot[static_cast<SwVector<Notification>::size_type>(i)]);
            }
        }
        return id;
    }

    void removeObserver_(int id) const {
        if (!m_state || id == 0) {
            return;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        sw::detail::swFutureRemoveObserverLocked_(*m_state, id);
    }

    void scheduleContinuation_(std::function<void()> task) const {
        std::function<void(std::function<void()>)> scheduler = continuationScheduler();
        if (scheduler) {
            scheduler(task);
            return;
        }
        task();
    }

    template<typename Fn>
    bool withLockBool_(Fn fn) const {
        if (!m_state) {
            return false;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return fn(*m_state);
    }

    template<typename Fn>
    int withLockInt_(Fn fn) const {
        if (!m_state) {
            return 0;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return fn(*m_state);
    }

    std::shared_ptr<State> m_state;
};

template<>
class SwFuture<void> {
    friend class SwPromise<void>;
    friend class SwFutureWatcher<void>;

public:
    typedef void result_type;

    SwFuture() {
    }

    bool isValid() const {
        return static_cast<bool>(m_state);
    }

    bool isStarted() const {
        return withLockBool_([](const State& state) { return state.started; });
    }

    bool isFinished() const {
        return withLockBool_([](const State& state) { return state.finished; });
    }

    bool isRunning() const {
        return withLockBool_([](const State& state) { return state.started && !state.finished; });
    }

    bool isCanceled() const {
        return withLockBool_([](const State& state) { return state.canceled; });
    }

    bool isFailed() const {
        return withLockBool_([](const State& state) { return state.failed; });
    }

    bool isSuspending() const {
        return withLockBool_([](const State& state) {
            return state.suspensionRequested && !state.suspended && !state.finished;
        });
    }

    bool isSuspended() const {
        return withLockBool_([](const State& state) {
            return state.suspended && !state.finished;
        });
    }

    bool isPaused() const {
        return isSuspended();
    }

    void waitForFinished() const {
        waitForCondition_(sw::detail::SwFutureYieldWaitKind::Finished, -1);
    }

    bool waitForResult(int index) const {
        (void)index;
        return waitForCondition_(sw::detail::SwFutureYieldWaitKind::Finished, -1);
    }

    void cancel() {
        if (!m_state) {
            return;
        }

        std::vector<std::function<void(const Notification&)> > callbacks;
        SwVector<int> wakeIds;
        Notification notification;
        notification.type = sw::detail::SwFutureNotificationType::Canceled;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            if (m_state->canceled) {
                return;
            }
            m_state->canceled = true;
            sw::detail::swFutureDispatchNotificationLocked_(*m_state, notification, callbacks, wakeIds);
        }
        sw::detail::swFutureDeliverCallbacks_(callbacks, notification);
        sw::detail::swFutureWakeYielders_(wakeIds);
    }

    void suspend() {
        setSuspended(true);
    }

    void pause() {
        setSuspended(true);
    }

    void resume() {
        setSuspended(false);
    }

    void setSuspended(bool suspend) {
        if (!m_state) {
            return;
        }

        std::vector<std::function<void(const Notification&)> > callbacks;
        SwVector<int> wakeIds;
        Notification notification;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            if (m_state->finished || m_state->suspensionRequested == suspend) {
                return;
            }
            m_state->suspensionRequested = suspend;
            if (!suspend) {
                m_state->suspended = false;
                notification.type = sw::detail::SwFutureNotificationType::Resumed;
            } else {
                notification.type = sw::detail::SwFutureNotificationType::Suspended;
            }
            sw::detail::swFutureDispatchNotificationLocked_(*m_state, notification, callbacks, wakeIds);
        }
        sw::detail::swFutureDeliverCallbacks_(callbacks, notification);
        sw::detail::swFutureWakeYielders_(wakeIds);
    }

    void toggleSuspended() {
        setSuspended(!isSuspended() && !isSuspending());
    }

    void setPaused(bool paused) {
        setSuspended(paused);
    }

    void togglePaused() {
        toggleSuspended();
    }

    int resultCount() const {
        return 0;
    }

    int progressMinimum() const {
        return withLockInt_([](const State& state) { return state.progressMinimum; });
    }

    int progressMaximum() const {
        return withLockInt_([](const State& state) { return state.progressMaximum; });
    }

    int progressValue() const {
        return withLockInt_([](const State& state) { return state.progressValue; });
    }

    SwString progressText() const {
        if (!m_state) {
            return SwString();
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->progressText;
    }

    SwString failureText() const {
        if (!m_state) {
            return SwString();
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->failureText;
    }

    void swap(SwFuture& other) {
        m_state.swap(other.m_state);
    }

    void setContinuationScheduler(const std::function<void(std::function<void()>)>& scheduler) {
        if (!m_state) {
            return;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        m_state->continuationScheduler = scheduler;
    }

    std::function<void(std::function<void()>)> continuationScheduler() const {
        if (!m_state) {
            return std::function<void(std::function<void()>)>();
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return m_state->continuationScheduler;
    }

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value,
                            SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F(SwFuture<void>)>::type>::ValueType> >::type
    then(F fn) const;

    template<typename F>
    typename std::enable_if<!sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value &&
                            sw::detail::SwFutureIsInvocable<F, void>::value,
                            SwFuture<typename sw::detail::SwFutureTypeTraits<typename std::result_of<F()>::type>::ValueType> >::type
    then(F fn) const;

    template<typename F>
    SwFuture<void> onCanceled(F fn) const;

    template<typename F>
    SwFuture<void> onFailed(F fn) const;

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value ||
                            sw::detail::SwFutureIsInvocable<F, void>::value,
                            decltype(std::declval<SwFuture<void> >().then(std::declval<F>()))>::type
    thenInline(F fn) const;

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value ||
                            sw::detail::SwFutureIsInvocable<F, void>::value,
                            decltype(std::declval<SwFuture<void> >().then(std::declval<F>()))>::type
    thenOnThreadPool(SwThreadPool* pool, F fn) const;

    template<typename F>
    typename std::enable_if<sw::detail::SwFutureIsInvocable<F, SwFuture<void> >::value ||
                            sw::detail::SwFutureIsInvocable<F, void>::value,
                            decltype(std::declval<SwFuture<void> >().then(std::declval<F>()))>::type
    thenOnAppLane(SwCoreApplication* app, SwFiberLane lane, F fn) const;

private:
    typedef sw::detail::SwFutureStorage<void> State;
    typedef sw::detail::SwFutureNotification Notification;

    explicit SwFuture(const std::shared_ptr<State>& state)
        : m_state(state) {
    }

    bool waitForCondition_(sw::detail::SwFutureYieldWaitKind kind, int index) const {
        if (!m_state) {
            return false;
        }

        if (!sw::detail::swFutureCanYieldCurrentFiber_()) {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            while (!sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index)) {
                m_state->condition.wait(lock);
            }
            return sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index);
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return false;
        }

        for (;;) {
            const int yieldId = SwCoreApplication::generateYieldId();
            {
                std::unique_lock<SwMutex> lock(m_state->mutex);
                if (sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index)) {
                    return true;
                }
                sw::detail::SwFutureYieldWaiter waiter;
                waiter.yieldId = yieldId;
                waiter.kind = kind;
                waiter.index = index;
                m_state->yieldWaiters.push_back(waiter);
            }

            const int timerId = app->addTimer([yieldId]() {
                SwCoreApplication::unYieldFiberHighPriority(yieldId);
            }, 1000, true, SwFiberLane::Control);

            SwCoreApplication::yieldFiber(yieldId);
            app->removeTimer(timerId);

            std::unique_lock<SwMutex> lock(m_state->mutex);
            sw::detail::swFutureRemoveYieldWaiterLocked_(*m_state, yieldId);
            if (sw::detail::swFutureWaitConditionSatisfiedLocked_(*m_state, kind, index)) {
                return true;
            }
        }
    }

    int addObserver_(const std::function<void(const Notification&)>& callback, bool replayCurrent) const {
        if (!m_state) {
            return 0;
        }

        int id = 0;
        SwVector<Notification> snapshot;
        {
            std::unique_lock<SwMutex> lock(m_state->mutex);
            id = sw::detail::swFutureAddObserverLocked_(*m_state, callback);
            if (replayCurrent) {
                sw::detail::swFutureBuildSnapshotLocked_(*m_state, snapshot);
            }
        }

        if (replayCurrent) {
            for (int i = 0; i < static_cast<int>(snapshot.size()); ++i) {
                callback(snapshot[static_cast<SwVector<Notification>::size_type>(i)]);
            }
        }
        return id;
    }

    void removeObserver_(int id) const {
        if (!m_state || id == 0) {
            return;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        sw::detail::swFutureRemoveObserverLocked_(*m_state, id);
    }

    void scheduleContinuation_(std::function<void()> task) const {
        std::function<void(std::function<void()>)> scheduler = continuationScheduler();
        if (scheduler) {
            scheduler(task);
            return;
        }
        task();
    }

    template<typename Fn>
    bool withLockBool_(Fn fn) const {
        if (!m_state) {
            return false;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return fn(*m_state);
    }

    template<typename Fn>
    int withLockInt_(Fn fn) const {
        if (!m_state) {
            return 0;
        }
        std::unique_lock<SwMutex> lock(m_state->mutex);
        return fn(*m_state);
    }

    std::shared_ptr<State> m_state;
};
