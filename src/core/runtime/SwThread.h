#pragma once

/**
 * @file src/core/runtime/SwThread.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwThread in the CoreSw runtime layer.
 *
 * This header belongs to the CoreSw runtime layer. It coordinates application lifetime, event
 * delivery, timers, threads, crash handling, and other process-level services consumed by the
 * rest of the stack.
 *
 * Within that layer, this file focuses on the thread interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwThread.
 *
 * Thread-oriented declarations here define execution-context ownership, affinity, or
 * synchronization boundaries that the rest of the framework relies on.
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
#include "atomic/thread.h"

#include "SwMutex.h"
#include "SwMap.h"
#include <functional>
#include <memory>
#include <thread>
#include <utility>

/**
 * @class SwThread
 * @brief High-level wrapper around atomic::Thread with a familiar API.
 */
class SwThread : public SwObject {
    SW_OBJECT(SwThread, SwObject)
    DECLARE_SIGNAL_VOID(started)
    DECLARE_SIGNAL_VOID(finished)
    DECLARE_SIGNAL_VOID(terminated)

public:
    /**
     * @brief Constructs a `SwThread` instance.
     * @param name Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param name Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwThread(const SwString& name = "SwThread", SwObject* parent = nullptr)
        : SwObject(parent), m_name(name) {
        m_ownedThread.reset(new sw::atomic::Thread(name));
        m_threadHandle = m_ownedThread.get();
        registerWrapper(m_threadHandle, this);
        initializeCallbacks();
    }

    /**
     * @brief Constructs a `SwThread` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwThread(SwObject* parent)
        : SwThread("SwThread", parent) {
    }

    /**
     * @brief Destroys the `SwThread` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwThread() {
        cleanup();
    }

    /**
     * @brief Starts the underlying thread.
     */
    virtual bool start() {
        if (!m_threadHandle) {
            return false;
        }
        bool started = m_threadHandle->start();
        if (started) {
            auto* self = this;
            m_threadHandle->postTask([self]() {
                if (!SwObject::isLive(self)) {
                    return;
                }
                self->run();
            });
        }
        return started;
    }

    /**
     * @brief Requests the underlying event loop to quit.
     */
    void quit() {
        if (m_threadHandle) {
            m_threadHandle->quit();
        }
    }

    /**
     * @brief Waits until the thread finishes.
     */
    void wait() {
        if (m_threadHandle && m_ownedThread) {
            m_threadHandle->wait();
        }
    }

    /**
     * @brief Returns whether the object reports running.
     * @return `true` when the object reports running; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isRunning() const {
        return m_threadHandle ? m_threadHandle->isRunning() : false;
    }

    /**
     * @brief Performs the `postTask` operation.
     * @param task Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool postTask(std::function<void()> task) {
        return m_threadHandle ? m_threadHandle->postTask(std::move(task)) : false;
    }

    /**
     * @brief Returns the current application.
     * @return The current application.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwCoreApplication* application() const {
        return m_threadHandle ? m_threadHandle->application() : nullptr;
    }

    /**
     * @brief Returns the current thread Id.
     * @return The current thread Id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::thread::id threadId() const {
        return m_threadHandle ? m_threadHandle->threadId() : std::thread::id{};
    }

    /**
     * @brief Returns the current handle.
     * @return The current handle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    sw::atomic::Thread* handle() const {
        return m_threadHandle;
    }

    /**
     * @brief Returns the SwThread wrapper for the current thread, adopting it if necessary.
     */
    static SwThread* currentThread() {
        if (localWrapper()) {
            return localWrapper();
        }
        sw::atomic::Thread* handle = sw::atomic::Thread::currentThread();
        if (!handle) {
            return nullptr;
        }
        if (auto wrapper = wrapperFor(handle)) {
            localWrapper() = wrapper;
            return wrapper;
        }
        auto adopted = new SwThread(handle, "AdoptedThread", true, nullptr);
        localWrapper() = adopted;
        return adopted;
    }

    /**
     * @brief Explicitly adopts the current thread.
     */
    static SwThread* adoptCurrentThread(const SwString& name = "MainThread") {
        sw::atomic::Thread* handle = sw::atomic::Thread::adoptCurrentThread(name);
        if (!handle) {
            return nullptr;
        }
        if (auto wrapper = wrapperFor(handle)) {
            localWrapper() = wrapper;
            return wrapper;
        }
        auto adopted = new SwThread(handle, name, true, nullptr);
        localWrapper() = adopted;
        return adopted;
    }

    /**
     * @brief Performs the `fromHandle` operation.
     * @param handle Value passed to the method.
     * @return The requested from Handle.
     */
    static SwThread* fromHandle(sw::atomic::Thread* handle) {
        return wrapperFor(handle);
    }

protected:
    /**
     * @brief Override to implement custom thread logic.
     *
     * The default implementation does nothing. Reimplementations can run local loops or
     * perform long operations; typically, you will invoke quit() to terminate the thread.
     */
    virtual void run() {
        // Default implementation: do nothing.
    }

private:
    SwThread(sw::atomic::Thread* adoptedHandle, const SwString& name, bool adopted, SwObject* parent)
        : SwObject(parent),
          m_threadHandle(adoptedHandle),
          m_isAdopted(adopted),
          m_name(name) {
        registerWrapper(m_threadHandle, this);
        localWrapper() = this;
        // Adopted threads are already running; no callbacks required.
    }

    void initializeCallbacks() {
        if (!m_threadHandle) {
            return;
        }
        m_threadHandle->setStartedCallback([this]() {
            localWrapper() = this;
            emit started();
        });
        m_threadHandle->setFinishedCallback([this]() {
            emit finished();
        });
    }

    void cleanup() {
        unregisterWrapper(m_threadHandle);
        if (!m_isAdopted) {
            quit();
            wait();
        }
        if (m_threadHandle) {
            m_threadHandle->detachObject(this);
        }
        m_threadHandle = nullptr;
        m_ownedThread.reset();
    }

    static void registerWrapper(sw::atomic::Thread* handle, SwThread* wrapper) {
        if (!handle || !wrapper) {
            return;
        }
        SwMutexLocker lock(wrapperMutex());
        wrapperMap()[handle] = wrapper;
    }

    static void unregisterWrapper(sw::atomic::Thread* handle) {
        if (!handle) {
            return;
        }
        SwMutexLocker lock(wrapperMutex());
        wrapperMap().remove(handle);
    }

    static SwThread* wrapperFor(sw::atomic::Thread* handle) {
        if (!handle) {
            return nullptr;
        }
        SwMutexLocker lock(wrapperMutex());
        auto it = wrapperMap().find(handle);
        if (it != wrapperMap().end()) {
            return it->second;
        }
        return nullptr;
    }

    static SwMutex& wrapperMutex() {
        static SwMutex s_mutex;
        return s_mutex;
    }

    static SwMap<sw::atomic::Thread*, SwThread*>& wrapperMap() {
        static SwMap<sw::atomic::Thread*, SwThread*> s_map;
        return s_map;
    }

    static SwThread*& localWrapper() {
        static thread_local SwThread* wrapper = nullptr;
        return wrapper;
    }

    std::unique_ptr<sw::atomic::Thread> m_ownedThread;
    sw::atomic::Thread* m_threadHandle = nullptr;
    bool m_isAdopted = false;
    SwString m_name;
};

inline void SwObject::moveToThread(SwThread* targetThread) {
    moveToThread(targetThread ? targetThread->handle() : nullptr);
}

inline SwThread* SwObject::thread() const {
    return SwThread::fromHandle(m_threadAffinity);
}
