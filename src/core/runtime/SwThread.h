#pragma once
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

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

/**
 * @class SwThread
 * @brief High-level wrapper around the atomic::Thread, offering a Qt-like API.
 */
class SwThread : public SwObject {
    DECLARE_SIGNAL_VOID(started)
    DECLARE_SIGNAL_VOID(finished)
    DECLARE_SIGNAL_VOID(terminated)

public:
    explicit SwThread(const SwString& name = "SwThread", SwObject* parent = nullptr)
        : SwObject(parent), m_name(name) {
        m_ownedThread.reset(new sw::atomic::Thread(name));
        m_threadHandle = m_ownedThread.get();
        registerWrapper(m_threadHandle, this);
        initializeCallbacks();
    }

    explicit SwThread(SwObject* parent)
        : SwThread("SwThread", parent) {
    }

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
            m_threadHandle->postTask([this]() {
                run();
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

    bool isRunning() const {
        return m_threadHandle ? m_threadHandle->isRunning() : false;
    }

    bool postTask(std::function<void()> task) {
        return m_threadHandle ? m_threadHandle->postTask(std::move(task)) : false;
    }

    SwCoreApplication* application() const {
        return m_threadHandle ? m_threadHandle->application() : nullptr;
    }

    std::thread::id threadId() const {
        return m_threadHandle ? m_threadHandle->threadId() : std::thread::id{};
    }

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
        std::lock_guard<std::mutex> lock(wrapperMutex());
        wrapperMap()[handle] = wrapper;
    }

    static void unregisterWrapper(sw::atomic::Thread* handle) {
        if (!handle) {
            return;
        }
        std::lock_guard<std::mutex> lock(wrapperMutex());
        wrapperMap().erase(handle);
    }

    static SwThread* wrapperFor(sw::atomic::Thread* handle) {
        if (!handle) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(wrapperMutex());
        auto it = wrapperMap().find(handle);
        if (it != wrapperMap().end()) {
            return it->second;
        }
        return nullptr;
    }

    static std::mutex& wrapperMutex() {
        static std::mutex s_mutex;
        return s_mutex;
    }

    static std::map<sw::atomic::Thread*, SwThread*>& wrapperMap() {
        static std::map<sw::atomic::Thread*, SwThread*> s_map;
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
