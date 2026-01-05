#ifndef SWMUTEX_H
#define SWMUTEX_H
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
 * @brief Qt-like mutex wrapper with optional recursion support.
 */
class SwMutex {
public:
    enum RecursionMode {
        NonRecursive,
        Recursive
    };

    explicit SwMutex(RecursionMode mode = NonRecursive)
        : mode_(mode) {}

    SwMutex(const SwMutex&) = delete;
    SwMutex& operator=(const SwMutex&) = delete;

    void lock() {
        if (mode_ == Recursive) {
            recursiveMutex_.lock();
        } else {
            mutex_.lock();
        }
    }

    bool tryLock() {
        return tryLock(0);
    }

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
    bool tryLockFor(const std::chrono::duration<Rep, Period>& duration) {
        return tryFor(std::chrono::duration_cast<std::chrono::milliseconds>(duration));
    }

    void unlock() {
        if (mode_ == Recursive) {
            recursiveMutex_.unlock();
        } else {
            mutex_.unlock();
        }
    }

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
 * @brief RAII helper mirroring Qt's QMutexLocker.
 */
class SwMutexLocker {
public:
    explicit SwMutexLocker(SwMutex* mutex)
        : mutex_(mutex)
        , locked_(false) {
        if (mutex_) {
            mutex_->lock();
            locked_ = true;
        }
    }

    explicit SwMutexLocker(SwMutex& mutex)
        : SwMutexLocker(&mutex) {}

    ~SwMutexLocker() {
        if (mutex_ && locked_) {
            mutex_->unlock();
        }
    }

    SwMutexLocker(const SwMutexLocker&) = delete;
    SwMutexLocker& operator=(const SwMutexLocker&) = delete;

    void unlock() {
        if (mutex_ && locked_) {
            mutex_->unlock();
            locked_ = false;
        }
    }

    void relock() {
        if (mutex_ && !locked_) {
            mutex_->lock();
            locked_ = true;
        }
    }

    SwMutex* mutex() const { return mutex_; }
    bool isLocked() const { return locked_; }

private:
    SwMutex* mutex_;
    bool locked_;
};

#endif // SWMUTEX_H
