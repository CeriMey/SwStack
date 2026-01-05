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

#include "SwCoreApplication.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

class SwObject;

namespace sw {
namespace atomic {

/**
 * @class Thread
 * @brief Lightweight worker thread hosting its own SwCoreApplication event loop.
 *
 * This class mirrors the behavior of Qt's QThread by providing an isolated event loop
 * for objects that need to live outside of the main thread. Each Thread owns a
 * dedicated SwCoreApplication instance, allowing SwObject::moveToThread() to safely
 * migrate objects and execute their queued events in the appropriate context.
 */
class Thread {
public:
    explicit Thread(const SwString& name = "Thread")
        : m_name(name) {
    }

    ~Thread() {
        if (!m_isAdopted) {
            quit();
            wait();
        }
        detachAllObjects();
    }

    /**
     * @brief Starts the worker thread and initializes its internal SwCoreApplication.
     * @return true if the thread successfully started, false otherwise.
     */
    bool start() {
        if (m_isAdopted) {
            // Adopted threads already run their event loop.
            return true;
        }
        if (m_running) {
            return true;
        }

        m_shouldQuit = false;
        m_thread = std::thread([this]() { threadEntry(); });
        std::unique_lock<std::mutex> lock(m_startMutex);
        m_startCv.wait(lock, [this]() { return m_appReady; });
        return m_running;
    }

    /**
     * @brief Requests the internal event loop to quit.
     *
     * The quit request is posted inside the thread's event queue to avoid
     * blocking the caller.
     */
    void quit() {
        if (!m_running || m_isAdopted) {
            return;
        }
        m_shouldQuit = true;
        auto app = application();
        if (app) {
            app->postEvent([app]() {
                app->quit();
            });
        }
    }

    /**
     * @brief Waits until the worker thread finishes execution.
     */
    void wait() {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    /**
     * @brief Indicates whether the thread is currently running.
     */
    bool isRunning() const {
        return m_running;
    }

    /**
     * @brief Retrieves the SwCoreApplication associated with this thread.
     *
     * @return Pointer to the internal SwCoreApplication, or nullptr if the
     *         event loop is not ready yet.
     */
    SwCoreApplication* application() const {
        std::lock_guard<std::mutex> lock(m_appMutex);
        return m_app;
    }

    /**
     * @brief Retrieves the native std::thread::id.
     */
    std::thread::id threadId() const {
        return m_threadId;
    }

    /**
     * @brief Posts a task to the internal event loop.
     *
     * @param task Functor to execute inside the thread's SwCoreApplication event loop.
     * @return true if the task was delivered immediately, false if it was queued
     *         for delivery once the event loop becomes available.
     */
    bool postTask(std::function<void()> task) {
        auto app = application();
        if (app) {
            app->postEvent(std::move(task));
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingTasks.push_back(std::move(task));
        }
        return false;
    }

    /**
     * @brief Returns the Thread representing the current thread if registered.
     *
     * If the current thread is not running under a Thread context but owns a
     * SwCoreApplication, it is automatically adopted.
     */
    static Thread* currentThread() {
        Thread*& local = localThreadInstance();
        if (local) {
            return local;
        }

        const auto id = std::this_thread::get_id();
        {
            std::lock_guard<std::mutex> lock(registryMutex());
            auto it = registry().find(id);
            if (it != registry().end()) {
                local = it->second;
                return local;
            }
        }

        return adoptCurrentThread("MainThread");
    }

    /**
     * @brief Adopts the current thread if it already owns a SwCoreApplication.
     *
     * @param name Optional friendly name used for logging/debugging.
     * @return A Thread wrapper for the current thread, or nullptr if
     *         no SwCoreApplication is bound to it.
     */
    static Thread* adoptCurrentThread(const SwString& name = "MainThread") {
        Thread*& local = localThreadInstance();
        if (local) {
            return local;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return nullptr;
        }

        auto adopted = new Thread(app, name, true);
        adopted->m_threadId = std::this_thread::get_id();
        adopted->m_running = true;
        adopted->m_appReady = true;
        registerThreadInstance(adopted);
        flushPendingTasksFor(adopted);
        local = adopted;
        return adopted;
    }

    /**
     * @brief Internal helper used by SwObject to track residency.
     */
    void attachObject(SwObject* object) {
        if (!object) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_objectsMutex);
        m_objects.insert(object);
    }

    /**
     * @brief Internal helper used by SwObject to detach residency.
     */
    void detachObject(SwObject* object) {
        if (!object) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_objectsMutex);
        m_objects.erase(object);
    }

    void setStartedCallback(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onStarted = std::move(cb);
    }

    void setFinishedCallback(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onFinished = std::move(cb);
    }

private:
    Thread(SwCoreApplication* existingApp, const SwString& name, bool adopted)
        : m_name(name),
          m_threadId(std::this_thread::get_id()),
          m_running(adopted),
          m_isAdopted(adopted),
          m_app(existingApp) {
        if (adopted) {
            m_appReady = true;
        }
    }

    void threadEntry() {
        localThreadInstance() = this;
        m_threadId = std::this_thread::get_id();
        registerThreadInstance(this);

        m_ownedApp.reset(new SwCoreApplication());
        {
            std::lock_guard<std::mutex> lock(m_appMutex);
            m_app = m_ownedApp.get();
        }

        {
            std::lock_guard<std::mutex> lock(m_startMutex);
            m_running = true;
            m_appReady = true;
        }
        m_startCv.notify_all();

        flushPendingTasks();
        invokeStartedCallback();

        if (!m_shouldQuit) {
            m_app->exec();
        }

        {
            std::lock_guard<std::mutex> lock(m_appMutex);
            m_app = nullptr;
        }
        m_ownedApp.reset();

        m_running = false;
        detachAllObjects();
        invokeFinishedCallback();
        localThreadInstance() = nullptr;
        unregisterThreadInstance(m_threadId);
    }

    void flushPendingTasks() {
        std::vector<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            pending.swap(m_pendingTasks);
        }
        for (auto& task : pending) {
            auto app = application();
            if (app) {
                app->postEvent(task);
            }
        }
    }

    static void flushPendingTasksFor(Thread* thread) {
        if (!thread) {
            return;
        }
        thread->flushPendingTasks();
    }

    void detachAllObjects() {
        std::lock_guard<std::mutex> lock(m_objectsMutex);
        m_objects.clear();
    }

    static void registerThreadInstance(Thread* thread) {
        if (!thread) {
            return;
        }
        localThreadInstance() = thread;
        std::lock_guard<std::mutex> lock(registryMutex());
        registry()[thread->m_threadId] = thread;
    }

    static void unregisterThreadInstance(const std::thread::id& id) {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().erase(id);
    }

    static Thread*& localThreadInstance() {
        static thread_local Thread* s_thread = nullptr;
        return s_thread;
    }

    static std::mutex& registryMutex() {
        static std::mutex s_mutex;
        return s_mutex;
    }

    static std::map<std::thread::id, Thread*>& registry() {
        static std::map<std::thread::id, Thread*> s_registry;
        return s_registry;
    }

    void invokeStartedCallback() {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            cb = m_onStarted;
        }
        if (cb) {
            cb();
        }
    }

    void invokeFinishedCallback() {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            cb = m_onFinished;
        }
        if (cb) {
            cb();
        }
    }

    SwString m_name;
    std::thread m_thread;
    std::thread::id m_threadId;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldQuit{false};
    bool m_isAdopted = false;

    SwCoreApplication* m_app = nullptr;
    std::unique_ptr<SwCoreApplication> m_ownedApp;
    mutable std::mutex m_appMutex;
    std::condition_variable m_startCv;
    std::mutex m_startMutex;
    bool m_appReady = false;

    std::mutex m_objectsMutex;
    std::set<SwObject*> m_objects;

    std::mutex m_pendingMutex;
    std::vector<std::function<void()>> m_pendingTasks;

    std::mutex m_callbackMutex;
    std::function<void()> m_onStarted;
    std::function<void()> m_onFinished;
};

} // namespace atomic
} // namespace sw

