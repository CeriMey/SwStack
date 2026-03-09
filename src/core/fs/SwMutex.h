#ifndef SWMUTEX_H
#define SWMUTEX_H

/**
 * @file src/core/fs/SwMutex.h
 * @ingroup core_fs
 * @brief Declares the public interface exposed by SwMutex in the CoreSw filesystem layer.
 *
 * This header belongs to the CoreSw filesystem layer. It wraps platform-specific path, directory,
 * settings, and related utility services behind framework-native types.
 *
 * Within that layer, this file focuses on the mutex interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwMutex and SwMutexLocker.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Filesystem declarations in this area are meant to keep file and path behavior predictable
 * across platforms while staying inside the Sw* type system.
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

#include <mutex>
#include <chrono>

/**
 * @class SwMutex
 * @brief Mutex wrapper with optional recursion support.
 */
class SwMutex {
public:
    enum RecursionMode {
        NonRecursive,
        Recursive
    };

    /**
     * @brief Constructs a `SwMutex` instance.
     * @param mode Mode value that controls the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMutex(RecursionMode mode = NonRecursive)
        : mode_(mode) {}

    /**
     * @brief Constructs a `SwMutex` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMutex(const SwMutex&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwMutex& operator=(const SwMutex&) = delete;

    /**
     * @brief Performs the `lock` operation.
     */
    void lock() {
        if (mode_ == Recursive) {
            recursiveMutex_.lock();
        } else {
            mutex_.lock();
        }
    }

    /**
     * @brief Returns the current try Lock.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool tryLock() {
        return tryLock(0);
    }

    /**
     * @brief Performs the `tryLock` operation.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return `true` on success; otherwise `false`.
     */
    bool tryLock(int timeoutMs) {
        if (timeoutMs < 0) {
            lock();
            return true;
        }
        if (timeoutMs == 0) {
            return tryImmediate();
        }
        return tryFor(std::chrono::milliseconds(timeoutMs));
    }

    template<typename Rep, typename Period>
    /**
     * @brief Performs the `tryLockFor` operation.
     * @param duration Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool tryLockFor(const std::chrono::duration<Rep, Period>& duration) {
        return tryFor(std::chrono::duration_cast<std::chrono::milliseconds>(duration));
    }

    /**
     * @brief Performs the `unlock` operation.
     */
    void unlock() {
        if (mode_ == Recursive) {
            recursiveMutex_.unlock();
        } else {
            mutex_.unlock();
        }
    }

    /**
     * @brief Returns whether the object reports recursive.
     * @return `true` when the object reports recursive; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isRecursive() const { return mode_ == Recursive; }

private:
    bool tryImmediate() {
        return (mode_ == Recursive)
            ? recursiveMutex_.try_lock()
            : mutex_.try_lock();
    }

    bool tryFor(std::chrono::milliseconds timeout) {
        return (mode_ == Recursive)
            ? recursiveMutex_.try_lock_for(timeout)
            : mutex_.try_lock_for(timeout);
    }

    RecursionMode mode_;
    std::timed_mutex mutex_;
    std::recursive_timed_mutex recursiveMutex_;
};

/**
 * @class SwMutexLocker
 * @brief RAII helper for SwMutex locking.
 */
class SwMutexLocker {
public:
    /**
     * @brief Constructs a `SwMutexLocker` instance.
     * @param false Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMutexLocker(SwMutex* mutex)
        : mutex_(mutex)
        , locked_(false) {
        if (mutex_) {
            mutex_->lock();
            locked_ = true;
        }
    }

    /**
     * @brief Constructs a `SwMutexLocker` instance.
     * @param mutex Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMutexLocker(SwMutex& mutex)
        : SwMutexLocker(&mutex) {}

    /**
     * @brief Destroys the `SwMutexLocker` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwMutexLocker() {
        if (mutex_ && locked_) {
            mutex_->unlock();
        }
    }

    /**
     * @brief Constructs a `SwMutexLocker` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMutexLocker(const SwMutexLocker&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwMutexLocker& operator=(const SwMutexLocker&) = delete;

    /**
     * @brief Performs the `unlock` operation.
     */
    void unlock() {
        if (mutex_ && locked_) {
            mutex_->unlock();
            locked_ = false;
        }
    }

    /**
     * @brief Performs the `relock` operation.
     */
    void relock() {
        if (mutex_ && !locked_) {
            mutex_->lock();
            locked_ = true;
        }
    }

    /**
     * @brief Returns the current mutex.
     * @return The current mutex.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMutex* mutex() const { return mutex_; }
    /**
     * @brief Returns whether the object reports locked.
     * @return `true` when the object reports locked; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isLocked() const { return locked_; }

private:
    SwMutex* mutex_;
    bool locked_;
};

#endif // SWMUTEX_H
