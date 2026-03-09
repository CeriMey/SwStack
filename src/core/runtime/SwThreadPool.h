#pragma once

/**
 * @file src/core/runtime/SwThreadPool.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwThreadPool in the CoreSw runtime layer.
 *
 * This header belongs to the CoreSw runtime layer. It coordinates application lifetime, event
 * delivery, timers, threads, crash handling, and other process-level services consumed by the
 * rest of the stack.
 *
 * Within that layer, this file focuses on the thread pool interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwThreadPoolRemoveReference, SwThreadPoolEnableIf,
 * SwThreadPoolIsRunnablePointer, SwDeadlineTimer, SwRunnable, SwRunnableFunction,
 * SwCallableRunnable, and SwThreadPool.
 *
 * Thread-pool declarations here describe how execution resources are pooled, scheduled, and
 * reclaimed without leaking lower-level synchronization details into user code.
 *
 * Runtime declarations in this area define lifecycle and threading contracts that higher-level
 * modules depend on for safe execution and orderly shutdown.
 *
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwObject.h"
#include "SwThread.h"
#include "SwMutex.h"
#include "SwList.h"
#include "SwString.h"
#include "SwVector.h"
#include "SwEventLoop.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/syscall.h>
#endif

template<typename T>
struct SwThreadPoolRemoveReference {
    typedef T Type;
};

template<typename T>
struct SwThreadPoolRemoveReference<T&> {
    typedef T Type;
};

template<typename T>
struct SwThreadPoolRemoveReference<T&&> {
    typedef T Type;
};

template<typename T>
inline T&& swThreadPoolForward_(typename SwThreadPoolRemoveReference<T>::Type& value) {
    return static_cast<T&&>(value);
}

template<typename T>
inline T&& swThreadPoolForward_(typename SwThreadPoolRemoveReference<T>::Type&& value) {
    return static_cast<T&&>(value);
}

template<typename Callable>
class SwCallableRunnable;
class SwRunnable;

template<bool Condition, typename T = void>
struct SwThreadPoolEnableIf {
};

template<typename T>
struct SwThreadPoolEnableIf<true, T> {
    typedef T Type;
};

template<typename T>
struct SwThreadPoolIsRunnablePointer {
private:
    static char test(SwRunnable*);
    static int test(...);

public:
    enum { Value = (sizeof(test(static_cast<T*>(nullptr))) == sizeof(char)) };
};

/**
 * @class SwDeadlineTimer
 * @brief Minimal deadline timer used by SwThreadPool::waitForDone.
 */
class SwDeadlineTimer {
public:
    enum ForeverConstant {
        Forever = -1
    };

    /**
     * @brief Constructs a `SwDeadlineTimer` instance.
     * @param msecs Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwDeadlineTimer(int msecs = Forever)
        : m_forever(msecs < 0),
          m_deadlineMs(m_forever ? -1 : (nowMs() + msecs)) {
    }

    /**
     * @brief Returns whether the object reports forever.
     * @return `true` when the object reports forever; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isForever() const {
        return m_forever;
    }

    /**
     * @brief Returns whether the object reports expired.
     * @return `true` when the object reports expired; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasExpired() const {
        if (m_forever) {
            return false;
        }
        return nowMs() >= m_deadlineMs;
    }

    /**
     * @brief Returns the current remaining Time.
     * @return The current remaining Time.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int remainingTime() const {
        if (m_forever) {
            return -1;
        }

        const long long left = m_deadlineMs - nowMs();
        if (left <= 0) {
            return 0;
        }
        if (left > 2147483647LL) {
            return 2147483647;
        }
        return static_cast<int>(left);
    }

    /**
     * @brief Returns the current now Ms.
     * @return The current now Ms.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static long long nowMs() {
#if defined(_WIN32)
        return static_cast<long long>(GetTickCount64());
#else
        timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            return 0;
        }
        return static_cast<long long>(ts.tv_sec) * 1000LL +
               static_cast<long long>(ts.tv_nsec / 1000000LL);
#endif
    }

private:
    bool m_forever;
    long long m_deadlineMs;
};

/**
 * @class SwRunnable
 * @brief Runnable task for SwThreadPool.
 */
class SwRunnable {
public:
    /**
     * @brief Constructs a `SwRunnable` instance.
     * @param true Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRunnable() : m_autoDelete(true) {}
    /**
     * @brief Destroys the `SwRunnable` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwRunnable() {}

    /**
     * @brief Returns the current run.
     * @return The current run.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void run() = 0;

    /**
     * @brief Returns the current auto Delete.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool autoDelete() const {
        return m_autoDelete;
    }

    /**
     * @brief Sets the auto Delete.
     * @param autoDelete Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAutoDelete(bool autoDelete) {
        m_autoDelete = autoDelete;
    }

    /**
     * @brief Creates the requested create.
     * @return The resulting create.
     */
    static SwRunnable* create(void (*function)());

    template<typename Callable>
    /**
     * @brief Creates the requested create.
     * @param functionToRun Value passed to the method.
     * @return The resulting create.
     */
    static SwRunnable* create(Callable&& functionToRun) {
        typedef typename SwThreadPoolRemoveReference<Callable>::Type CallableType;
        return new SwCallableRunnable<CallableType>(swThreadPoolForward_<Callable>(functionToRun));
    }

private:
    bool m_autoDelete;
};

class SwRunnableFunction final : public SwRunnable {
public:
    /**
     * @brief Performs the `SwRunnableFunction` operation.
     * @param function Value passed to the method.
     * @return The requested sw Runnable Function.
     */
    explicit SwRunnableFunction(void (*function)())
        : m_function(function) {
    }

    /**
     * @brief Performs the `run` operation.
     */
    void run() override {
        if (m_function) {
            m_function();
        }
    }

private:
    void (*m_function)() = nullptr;
};

inline SwRunnable* SwRunnable::create(void (*function)()) {
    return new SwRunnableFunction(function);
}

template<typename Callable>
class SwCallableRunnable final : public SwRunnable {
public:
    template<typename F>
    /**
     * @brief Performs the `SwCallableRunnable` operation.
     * @return The requested sw Callable Runnable.
     */
    explicit SwCallableRunnable(F&& callable)
        : m_callable(swThreadPoolForward_<F>(callable)) {
    }

    /**
     * @brief Performs the `run` operation.
     */
    void run() override {
        m_callable();
    }

private:
    Callable m_callable;
};

/**
 * @class SwThreadPool
 * @brief Thread pool built on top of SwThread workers.
 */
class SwThreadPool : public SwObject {
    SW_OBJECT(SwThreadPool, SwObject)
private:
    struct TaskItem;
    struct WorkerEntry;

public:
    enum ThreadPriority {
        IdlePriority = 0,
        LowestPriority = 1,
        LowPriority = 2,
        NormalPriority = 3,
        HighPriority = 4,
        HighestPriority = 5,
        TimeCriticalPriority = 6,
        InheritPriority = 7
    };

    enum QualityOfService {
        QualityOfServiceAuto = 0,
        QualityOfServiceHigh = 1,
        QualityOfServiceEco = 2
    };

    /**
     * @brief Constructs a `SwThreadPool` instance.
     * @param parent Optional parent object that owns this instance.
     * @param false Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwThreadPool(SwObject* parent = nullptr)
        : SwObject(parent),
          m_maxThreadCount(detectIdealThreadCount_()),
          m_expiryTimeout(30000),
          m_stackSize(0),
          m_threadPriority(InheritPriority),
          m_serviceLevel(QualityOfServiceAuto),
          m_reservedThreads(0),
          m_runningTasks(0),
          m_nextWorkerId(0),
          m_waitingForDone(false),
          m_stopping(false) {
        if (m_maxThreadCount < 1) {
            m_maxThreadCount = 1;
        }
    }

    /**
     * @brief Destroys the `SwThreadPool` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwThreadPool() override {
        shutdown_();
    }

    /**
     * @brief Returns the current global Instance.
     * @return The current global Instance.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwThreadPool* globalInstance() {
        static SwThreadPool* s_pool = new SwThreadPool(nullptr);
        return s_pool;
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     * @param runnable Value passed to the method.
     * @param priority Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start(SwRunnable* runnable, int priority = 0) {
        if (!runnable) {
            return;
        }

        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        if (m_stopping || m_waitingForDone) {
            deleteIfAutoDelete_(runnable);
            return;
        }

        if (!tryStartLocked_(runnable, priority)) {
            enqueueLocked_(runnable, priority);
        }
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     * @param priority Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start(void (*function)(), int priority = 0) {
        start(SwRunnable::create(function), priority);
    }

    template<typename TRunnable>
    typename SwThreadPoolEnableIf<SwThreadPoolIsRunnablePointer<TRunnable>::Value, void>::Type
    /**
     * @brief Starts the underlying activity managed by the object.
     * @param runnable Value passed to the method.
     * @param priority Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    start(TRunnable* runnable, int priority = 0) {
        start(static_cast<SwRunnable*>(runnable), priority);
    }

    template<typename Callable>
    /**
     * @brief Starts the underlying activity managed by the object.
     * @param functionToRun Value passed to the method.
     * @param priority Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start(Callable&& functionToRun, int priority = 0) {
        typedef typename SwThreadPoolRemoveReference<Callable>::Type CallableType;
        start(new SwCallableRunnable<CallableType>(swThreadPoolForward_<Callable>(functionToRun)), priority);
    }

    /**
     * @brief Performs the `tryStart` operation.
     * @param runnable Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool tryStart(SwRunnable* runnable) {
        if (!runnable) {
            return false;
        }

        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        if (m_stopping || m_waitingForDone) {
            return false;
        }
        return tryStartLocked_(runnable, 0);
    }

    /**
     * @brief Performs the `tryStart` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool tryStart(void (*function)()) {
        SwRunnable* runnable = SwRunnable::create(function);
        if (!runnable) {
            return false;
        }

        const bool started = tryStart(runnable);
        if (!started && runnable->autoDelete()) {
            delete runnable;
        }
        return started;
    }

    template<typename TRunnable>
    typename SwThreadPoolEnableIf<SwThreadPoolIsRunnablePointer<TRunnable>::Value, bool>::Type
    /**
     * @brief Performs the `tryStart` operation.
     * @param runnable Value passed to the method.
     */
    tryStart(TRunnable* runnable) {
        return tryStart(static_cast<SwRunnable*>(runnable));
    }

    template<typename Callable>
    /**
     * @brief Performs the `tryStart` operation.
     * @param functionToRun Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool tryStart(Callable&& functionToRun) {
        typedef typename SwThreadPoolRemoveReference<Callable>::Type CallableType;
        SwRunnable* runnable = new SwCallableRunnable<CallableType>(swThreadPoolForward_<Callable>(functionToRun));
        const bool started = tryStart(runnable);
        if (!started && runnable->autoDelete()) {
            delete runnable;
        }
        return started;
    }

    /**
     * @brief Starts the on Reserved Thread managed by the object.
     * @param runnable Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void startOnReservedThread(SwRunnable* runnable) {
        if (!runnable) {
            return;
        }

        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        if (m_stopping || m_waitingForDone) {
            deleteIfAutoDelete_(runnable);
            return;
        }

        --m_reservedThreads;

        if (!tryStartLocked_(runnable, 0, true)) {
            ++m_reservedThreads;
            enqueueLocked_(runnable, 0);
        }
    }

    /**
     * @brief Starts the on Reserved Thread managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void startOnReservedThread(void (*function)()) {
        startOnReservedThread(SwRunnable::create(function));
    }

    template<typename TRunnable>
    typename SwThreadPoolEnableIf<SwThreadPoolIsRunnablePointer<TRunnable>::Value, void>::Type
    /**
     * @brief Starts the on Reserved Thread managed by the object.
     * @param runnable Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    startOnReservedThread(TRunnable* runnable) {
        startOnReservedThread(static_cast<SwRunnable*>(runnable));
    }

    template<typename Callable>
    /**
     * @brief Starts the on Reserved Thread managed by the object.
     * @param functionToRun Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void startOnReservedThread(Callable&& functionToRun) {
        typedef typename SwThreadPoolRemoveReference<Callable>::Type CallableType;
        startOnReservedThread(new SwCallableRunnable<CallableType>(swThreadPoolForward_<Callable>(functionToRun)));
    }

    /**
     * @brief Performs the `tryTake` operation.
     * @param runnable Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool tryTake(SwRunnable* runnable) {
        if (!runnable) {
            return false;
        }

        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        const int count = static_cast<int>(m_pendingTasks.size());
        for (int i = 0; i < count; ++i) {
            if (m_pendingTasks[static_cast<size_t>(i)].runnable == runnable) {
                m_pendingTasks.removeAt(static_cast<size_t>(i));
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        clearPendingLocked_();
    }

    /**
     * @brief Performs the `waitForDone` operation.
     * @param deadline Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool waitForDone(const SwDeadlineTimer& deadline = SwDeadlineTimer(SwDeadlineTimer::Forever)) {
        {
            SwMutexLocker locker(&m_mutex);
            cleanupRetiredWorkersLocked_(locker);
            if (m_waitingForDone) {
                return false;
            }
            m_waitingForDone = true;
        }

        bool ok = awaitQuiescent_(deadline);
        if (ok) {
            ok = retireAllWorkers_(deadline);
        }

        {
            SwMutexLocker locker(&m_mutex);
            m_waitingForDone = false;
        }
        return ok;
    }

    /**
     * @brief Performs the `waitForDone` operation.
     * @param msecs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool waitForDone(int msecs) {
        return waitForDone(SwDeadlineTimer(msecs));
    }

    /**
     * @brief Returns the current expiry Timeout.
     * @return The current expiry Timeout.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int expiryTimeout() const {
        SwMutexLocker locker(&m_mutex);
        return m_expiryTimeout;
    }

    /**
     * @brief Sets the expiry Timeout.
     * @param expiryTimeout Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setExpiryTimeout(int expiryTimeout) {
        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        m_expiryTimeout = expiryTimeout;
    }

    /**
     * @brief Returns the current max Thread Count.
     * @return The current max Thread Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maxThreadCount() const {
        SwMutexLocker locker(&m_mutex);
        return m_maxThreadCount;
    }

    /**
     * @brief Sets the max Thread Count.
     * @param maxThreadCount Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaxThreadCount(int maxThreadCount) {
        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        m_maxThreadCount = (maxThreadCount < 1) ? 1 : maxThreadCount;
        dispatchPendingLocked_();
    }

    /**
     * @brief Returns the current active Thread Count.
     * @return The current active Thread Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int activeThreadCount() const {
        SwMutexLocker locker(&m_mutex);
        return m_runningTasks + m_reservedThreads;
    }

    /**
     * @brief Performs the `reserveThread` operation.
     */
    void reserveThread() {
        SwMutexLocker locker(&m_mutex);
        ++m_reservedThreads;
    }

    /**
     * @brief Performs the `releaseThread` operation.
     */
    void releaseThread() {
        SwMutexLocker locker(&m_mutex);
        cleanupRetiredWorkersLocked_(locker);
        --m_reservedThreads;
        dispatchPendingLocked_();
    }

    /**
     * @brief Returns the current stack Size.
     * @return The current stack Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    unsigned int stackSize() const {
        SwMutexLocker locker(&m_mutex);
        return m_stackSize;
    }

    /**
     * @brief Sets the stack Size.
     * @param stackSize Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStackSize(unsigned int stackSize) {
        SwMutexLocker locker(&m_mutex);
        m_stackSize = stackSize;
    }

    /**
     * @brief Returns the current thread Priority.
     * @return The current thread Priority.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ThreadPriority threadPriority() const {
        SwMutexLocker locker(&m_mutex);
        return m_threadPriority;
    }

    /**
     * @brief Sets the thread Priority.
     * @param priority Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setThreadPriority(ThreadPriority priority) {
        SwMutexLocker locker(&m_mutex);
        m_threadPriority = priority;
    }

    /**
     * @brief Returns the current service Level.
     * @return The current service Level.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    QualityOfService serviceLevel() const {
        SwMutexLocker locker(&m_mutex);
        return m_serviceLevel;
    }

    /**
     * @brief Sets the service Level.
     * @param serviceLevel Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setServiceLevel(QualityOfService serviceLevel) {
        SwMutexLocker locker(&m_mutex);
        m_serviceLevel = serviceLevel;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param thread Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwThread* thread) const {
        if (!thread) {
            return false;
        }

        SwMutexLocker locker(&m_mutex);
        const int count = m_workers.size();
        for (int i = 0; i < count; ++i) {
            const WorkerEntry* worker = m_workers[static_cast<size_t>(i)];
            if (worker && worker->thread == thread) {
                return true;
            }
        }
        return false;
    }

private:
    struct TaskItem {
        SwRunnable* runnable = nullptr;
        int priority = 0;
    };

    struct WorkerEntry {
        int id = 0;
        SwThread* thread = nullptr;
        bool busy = false;
        unsigned int idleToken = 0;
        int expiryTimeoutMs = 30000;
    };

    static int detectIdealThreadCount_() {
#if defined(_WIN32)
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const int count = static_cast<int>(info.dwNumberOfProcessors);
        return (count > 0) ? count : 1;
#else
        const long count = sysconf(_SC_NPROCESSORS_ONLN);
        return (count > 0) ? static_cast<int>(count) : 1;
#endif
    }

    static void sleepMs_(int ms) {
        if (ms <= 0) {
            return;
        }
#if defined(_WIN32)
        Sleep(static_cast<DWORD>(ms));
#else
        usleep(static_cast<useconds_t>(ms * 1000));
#endif
    }

    static void deleteIfAutoDelete_(SwRunnable* runnable) {
        if (runnable && runnable->autoDelete()) {
            delete runnable;
        }
    }

    static void applyCurrentThreadScheduling_(ThreadPriority priority, QualityOfService qos) {
#if defined(_WIN32)
        int nativePriority = THREAD_PRIORITY_NORMAL;
        bool hasExplicitPriority = (priority != InheritPriority);

        if (hasExplicitPriority) {
            switch (priority) {
            case IdlePriority: nativePriority = THREAD_PRIORITY_IDLE; break;
            case LowestPriority: nativePriority = THREAD_PRIORITY_LOWEST; break;
            case LowPriority: nativePriority = THREAD_PRIORITY_BELOW_NORMAL; break;
            case NormalPriority: nativePriority = THREAD_PRIORITY_NORMAL; break;
            case HighPriority: nativePriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
            case HighestPriority: nativePriority = THREAD_PRIORITY_HIGHEST; break;
            case TimeCriticalPriority: nativePriority = THREAD_PRIORITY_TIME_CRITICAL; break;
            case InheritPriority: break;
            }
        } else {
            switch (qos) {
            case QualityOfServiceHigh: nativePriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
            case QualityOfServiceEco: nativePriority = THREAD_PRIORITY_BELOW_NORMAL; break;
            case QualityOfServiceAuto: default: nativePriority = THREAD_PRIORITY_NORMAL; break;
            }
        }

        SetThreadPriority(GetCurrentThread(), nativePriority);
#else
        int desiredNice = 0;
        bool hasDesiredNice = false;

        if (priority != InheritPriority) {
            hasDesiredNice = true;
            switch (priority) {
            case IdlePriority: desiredNice = 19; break;
            case LowestPriority: desiredNice = 10; break;
            case LowPriority: desiredNice = 5; break;
            case NormalPriority: desiredNice = 0; break;
            case HighPriority: desiredNice = -2; break;
            case HighestPriority: desiredNice = -5; break;
            case TimeCriticalPriority: desiredNice = -10; break;
            case InheritPriority: hasDesiredNice = false; break;
            }
        } else {
            switch (qos) {
            case QualityOfServiceHigh: desiredNice = -5; hasDesiredNice = true; break;
            case QualityOfServiceEco: desiredNice = 5; hasDesiredNice = true; break;
            case QualityOfServiceAuto: default: break;
            }
        }

        if (hasDesiredNice) {
#if defined(SYS_gettid)
            const long tid = static_cast<long>(syscall(SYS_gettid));
            setpriority(PRIO_PROCESS, static_cast<id_t>(tid), desiredNice);
#else
            setpriority(PRIO_PROCESS, static_cast<id_t>(0), desiredNice);
#endif
        }
#endif
    }

    static void applyCurrentThreadSettings_(unsigned int taskStackSize,
                                            ThreadPriority priority,
                                            QualityOfService qos) {
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app) {
            app->setEventFiberStackSize(taskStackSize);
        }
        applyCurrentThreadScheduling_(priority, qos);
    }

    int effectiveCapacityLocked_(bool ignoreReserved = false) const {
        int capacity = m_maxThreadCount;
        if (!ignoreReserved && !m_stopping) {
            capacity -= m_reservedThreads;
        }
        if (capacity < 1) {
            capacity = 1;
        }
        return capacity;
    }

    WorkerEntry* findIdleWorkerLocked_() const {
        const int count = m_workers.size();
        for (int i = 0; i < count; ++i) {
            WorkerEntry* worker = m_workers[static_cast<size_t>(i)];
            if (worker && !worker->busy) {
                return worker;
            }
        }
        return nullptr;
    }

    WorkerEntry* findWorkerByIdLocked_(int workerId) const {
        const int count = m_workers.size();
        for (int i = 0; i < count; ++i) {
            WorkerEntry* worker = m_workers[static_cast<size_t>(i)];
            if (worker && worker->id == workerId) {
                return worker;
            }
        }
        return nullptr;
    }

    void removeWorkerLocked_(WorkerEntry* worker) {
        if (!worker) {
            return;
        }
        const int count = m_workers.size();
        for (int i = 0; i < count; ++i) {
            if (m_workers[static_cast<size_t>(i)] == worker) {
                m_workers.removeAt(i);
                return;
            }
        }
    }

    static void stopSingleWorker_(WorkerEntry* worker) {
        if (!worker) {
            return;
        }
        if (worker->thread) {
            worker->thread->quit();
            worker->thread->wait();
            delete worker->thread;
            worker->thread = nullptr;
        }
        delete worker;
    }

    void cleanupRetiredWorkersLocked_(SwMutexLocker& locker) {
        SwVector<WorkerEntry*> toDelete;

        for (int i = static_cast<int>(m_retiredWorkers.size()) - 1; i >= 0; --i) {
            WorkerEntry* worker = m_retiredWorkers[static_cast<size_t>(i)];
            if (!worker || !worker->thread || !worker->thread->isRunning()) {
                toDelete.push_back(worker);
                m_retiredWorkers.removeAt(static_cast<size_t>(i));
            }
        }

        if (toDelete.isEmpty()) {
            return;
        }

        locker.unlock();
        const int count = toDelete.size();
        for (int i = 0; i < count; ++i) {
            stopSingleWorker_(toDelete[static_cast<size_t>(i)]);
        }
        locker.relock();
    }

    void scheduleWorkerExpiryCheckLocked_(WorkerEntry* worker) {
        if (!worker || !worker->thread) {
            return;
        }

        const int timeout = worker->expiryTimeoutMs;
        if (timeout < 0) {
            return;
        }

        const int workerId = worker->id;
        const unsigned int token = worker->idleToken;
        SwThreadPool* pool = this;
        worker->thread->postTask([pool, workerId, token, timeout]() {
            if (timeout > 0) {
                SwEventLoop::swsleep(timeout);
            }
            pool->onWorkerExpiryTimeout_(workerId, token);
        });
    }

    void onWorkerExpiryTimeout_(int workerId, unsigned int token) {
        WorkerEntry* workerToStop = nullptr;
        {
            SwMutexLocker locker(&m_mutex);
            if (m_stopping || m_waitingForDone) {
                return;
            }

            WorkerEntry* worker = findWorkerByIdLocked_(workerId);
            if (!worker) {
                return;
            }
            if (worker->busy) {
                return;
            }
            if (worker->idleToken != token) {
                return;
            }
            if (!m_pendingTasks.isEmpty()) {
                return;
            }

            removeWorkerLocked_(worker);

            SwThread* currentWrapper = SwThread::currentThread();
            const bool onSameWorkerThread =
                currentWrapper && worker->thread &&
                (currentWrapper->handle() == worker->thread->handle());

            if (onSameWorkerThread) {
                worker->thread->quit();
                m_retiredWorkers.append(worker);
                return;
            }

            workerToStop = worker;
        }

        stopSingleWorker_(workerToStop);
    }

    WorkerEntry* createWorkerLocked_() {
        WorkerEntry* worker = new WorkerEntry();
        worker->id = ++m_nextWorkerId;
        worker->thread = new SwThread("SwThreadPoolWorker_" + SwString::number(worker->id), nullptr);
        worker->busy = false;
        worker->idleToken = 0;
        worker->expiryTimeoutMs = m_expiryTimeout;

        const unsigned int workerStackSize = m_stackSize;
        const ThreadPriority workerPriority = m_threadPriority;
        const QualityOfService workerQos = m_serviceLevel;

        const bool started = worker->thread->start();
        if (!started) {
            delete worker->thread;
            worker->thread = nullptr;
            delete worker;
            return nullptr;
        }

        worker->thread->postTask([workerStackSize, workerPriority, workerQos]() {
            applyCurrentThreadSettings_(workerStackSize, workerPriority, workerQos);
        });

        m_workers.push_back(worker);
        return worker;
    }

    bool tryStartLocked_(SwRunnable* runnable, int priority, bool ignoreReserved = false) {
        SW_UNUSED(priority);

        const int capacity = effectiveCapacityLocked_(ignoreReserved);
        if (m_runningTasks >= capacity) {
            return false;
        }

        WorkerEntry* worker = findIdleWorkerLocked_();
        if (!worker) {
            if (m_workers.size() >= capacity) {
                return false;
            }
            worker = createWorkerLocked_();
            if (!worker) {
                return false;
            }
        }

        dispatchRunnableLocked_(worker, runnable);
        return true;
    }

    void dispatchRunnableLocked_(WorkerEntry* worker, SwRunnable* runnable) {
        if (!worker || !worker->thread || !runnable) {
            return;
        }

        worker->busy = true;
        ++m_runningTasks;

        SwThreadPool* pool = this;
        worker->thread->postTask([pool, worker, runnable]() {
            runnable->run();
            if (runnable->autoDelete()) {
                delete runnable;
            }
            pool->onRunnableFinished_(worker);
        });
    }

    void enqueueLocked_(SwRunnable* runnable, int priority) {
        TaskItem item;
        item.runnable = runnable;
        item.priority = priority;

        const int count = static_cast<int>(m_pendingTasks.size());
        int insertAt = count;
        for (int i = 0; i < count; ++i) {
            const TaskItem& current = m_pendingTasks[static_cast<size_t>(i)];
            if (item.priority > current.priority) {
                insertAt = i;
                break;
            }
        }

        if (insertAt >= count) {
            m_pendingTasks.append(item);
        } else {
            m_pendingTasks.insert(static_cast<size_t>(insertAt), item);
        }
    }

    void dispatchPendingLocked_() {
        while (!m_pendingTasks.isEmpty()) {
            const int capacity = effectiveCapacityLocked_(false);
            if (m_runningTasks >= capacity) {
                return;
            }

            WorkerEntry* worker = findIdleWorkerLocked_();
            if (!worker) {
                if (m_workers.size() >= capacity) {
                    return;
                }
                worker = createWorkerLocked_();
                if (!worker) {
                    return;
                }
            }

            TaskItem item = m_pendingTasks.first();
            m_pendingTasks.removeAt(0);
            dispatchRunnableLocked_(worker, item.runnable);
        }
    }

    void clearPendingLocked_() {
        const int count = static_cast<int>(m_pendingTasks.size());
        for (int i = 0; i < count; ++i) {
            deleteIfAutoDelete_(m_pendingTasks[static_cast<size_t>(i)].runnable);
        }
        m_pendingTasks.clear();
    }

    void onRunnableFinished_(WorkerEntry* worker) {
        SwMutexLocker locker(&m_mutex);
        if (m_runningTasks > 0) {
            --m_runningTasks;
        }
        if (worker) {
            worker->busy = false;
            ++worker->idleToken;
            scheduleWorkerExpiryCheckLocked_(worker);
        }
        dispatchPendingLocked_();
    }

    void shutdown_() {
        {
            SwMutexLocker locker(&m_mutex);
            if (m_stopping) {
                return;
            }
            m_stopping = true;
        }

        // Destruction waits for all queued/running tasks to finish.
        waitForDone(-1);
    }

    bool awaitQuiescent_(const SwDeadlineTimer& deadline) {
        while (true) {
            {
                SwMutexLocker locker(&m_mutex);
                cleanupRetiredWorkersLocked_(locker);
                dispatchPendingLocked_();
                if (m_runningTasks == 0 && m_pendingTasks.isEmpty()) {
                    return true;
                }
            }

            if (deadline.hasExpired()) {
                return false;
            }

            sleepMs_(1);
        }
    }

    bool retireAllWorkers_(const SwDeadlineTimer& deadline) {
        SwVector<WorkerEntry*> workersToStop;
        {
            SwMutexLocker locker(&m_mutex);
            cleanupRetiredWorkersLocked_(locker);
            workersToStop = m_workers;
            const int retiredCount = static_cast<int>(m_retiredWorkers.size());
            for (int i = 0; i < retiredCount; ++i) {
                workersToStop.push_back(m_retiredWorkers[static_cast<size_t>(i)]);
            }
            m_workers.clear();
            m_retiredWorkers.clear();
        }

        const int count = workersToStop.size();
        for (int i = 0; i < count; ++i) {
            WorkerEntry* worker = workersToStop[static_cast<size_t>(i)];
            if (worker && worker->thread) {
                worker->thread->quit();
            }
        }

        while (true) {
            bool allStopped = true;
            for (int i = 0; i < count; ++i) {
                WorkerEntry* worker = workersToStop[static_cast<size_t>(i)];
                if (worker && worker->thread && worker->thread->isRunning()) {
                    allStopped = false;
                    break;
                }
            }

            if (allStopped) {
                break;
            }

            if (deadline.hasExpired()) {
                return false;
            }

            sleepMs_(1);
        }

        for (int i = 0; i < count; ++i) {
            WorkerEntry* worker = workersToStop[static_cast<size_t>(i)];
            stopSingleWorker_(worker);
        }
        return true;
    }

    mutable SwMutex m_mutex;
    SwList<TaskItem> m_pendingTasks;
    SwVector<WorkerEntry*> m_workers;
    SwList<WorkerEntry*> m_retiredWorkers;

    int m_maxThreadCount;
    int m_expiryTimeout;
    unsigned int m_stackSize;
    ThreadPriority m_threadPriority;
    QualityOfService m_serviceLevel;
    int m_reservedThreads;
    int m_runningTasks;
    int m_nextWorkerId;
    bool m_waitingForDone;
    bool m_stopping;
};
