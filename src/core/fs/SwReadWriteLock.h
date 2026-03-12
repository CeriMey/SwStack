#ifndef SWREADWRITELOCK_H
#define SWREADWRITELOCK_H

/**
 * @file src/core/fs/SwReadWriteLock.h
 * @ingroup core_fs
 * @brief Declares the public interface exposed by SwReadWriteLock in the CoreSw utility layer.
 *
 * This header provides a framework-native read/write locking API with Qt-like naming and RAII
 * helpers while remaining compatible with the project's C++11 baseline.
 *
 * Non-recursive mode uses a native shared lock backend on supported platforms:
 * - Windows: `SRWLOCK`
 * - Linux/POSIX: `pthread_rwlock_t`
 *
 * Recursive mode falls back to a recursive mutex because those native reader/writer primitives
 * are not recursively lockable in a portable way.
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

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <pthread.h>
#endif

class SwReadWriteLock {
public:
    enum RecursionMode {
        NonRecursive,
        Recursive
    };

    explicit SwReadWriteLock(RecursionMode mode = NonRecursive)
        : mode_(mode) {
        initNative_();
    }

    ~SwReadWriteLock() {
        destroyNative_();
    }

    SwReadWriteLock(const SwReadWriteLock&) = delete;
    SwReadWriteLock& operator=(const SwReadWriteLock&) = delete;

    void lockForRead() {
        if (mode_ == Recursive) {
            recursiveMutex_.lock();
            return;
        }
        lockForReadNative_();
        registerReadLock_();
    }

    void lockForWrite() {
        if (mode_ == Recursive) {
            recursiveMutex_.lock();
            return;
        }
        lockForWriteNative_();
        registerWriteLock_();
    }

    bool tryLockForRead() {
        return tryLockForRead(0);
    }

    bool tryLockForRead(int timeoutMs) {
        if (mode_ == Recursive) {
            return tryLockRecursive_(timeoutMs);
        }
        return tryLockNative_(timeoutMs, false);
    }

    bool tryLockForWrite() {
        return tryLockForWrite(0);
    }

    bool tryLockForWrite(int timeoutMs) {
        if (mode_ == Recursive) {
            return tryLockRecursive_(timeoutMs);
        }
        return tryLockNative_(timeoutMs, true);
    }

    template<typename Rep, typename Period>
    bool tryLockForRead(const std::chrono::duration<Rep, Period>& duration) {
        return tryLockForRead(durationToTimeoutMs_(duration));
    }

    template<typename Rep, typename Period>
    bool tryLockForWrite(const std::chrono::duration<Rep, Period>& duration) {
        return tryLockForWrite(durationToTimeoutMs_(duration));
    }

    void unlock() {
        if (mode_ == Recursive) {
            recursiveMutex_.unlock();
            return;
        }

        UnlockKind unlockKind = unregisterLock_();
        if (unlockKind == UnlockWrite) {
            unlockWriteNative_();
        } else if (unlockKind == UnlockRead) {
            unlockReadNative_();
        }
    }

    bool isRecursive() const {
        return mode_ == Recursive;
    }

private:
    enum UnlockKind {
        UnlockNone,
        UnlockRead,
        UnlockWrite
    };

    struct ThreadLockState {
        int readCount;
        int writeCount;

        ThreadLockState()
            : readCount(0)
            , writeCount(0) {}
    };

    void initNative_() {
        if (mode_ == Recursive) {
            return;
        }
#if defined(_WIN32)
        InitializeSRWLock(&nativeLock_);
#else
        pthread_rwlock_init(&nativeLock_, nullptr);
#endif
    }

    void destroyNative_() {
        if (mode_ == Recursive) {
            return;
        }
#if !defined(_WIN32)
        pthread_rwlock_destroy(&nativeLock_);
#endif
    }

    void lockForReadNative_() {
#if defined(_WIN32)
        AcquireSRWLockShared(&nativeLock_);
#else
        pthread_rwlock_rdlock(&nativeLock_);
#endif
    }

    void lockForWriteNative_() {
#if defined(_WIN32)
        AcquireSRWLockExclusive(&nativeLock_);
#else
        pthread_rwlock_wrlock(&nativeLock_);
#endif
    }

    bool tryLockReadImmediateNative_() {
#if defined(_WIN32)
        return TryAcquireSRWLockShared(&nativeLock_) != 0;
#else
        return pthread_rwlock_tryrdlock(&nativeLock_) == 0;
#endif
    }

    bool tryLockWriteImmediateNative_() {
#if defined(_WIN32)
        return TryAcquireSRWLockExclusive(&nativeLock_) != 0;
#else
        return pthread_rwlock_trywrlock(&nativeLock_) == 0;
#endif
    }

    void unlockReadNative_() {
#if defined(_WIN32)
        ReleaseSRWLockShared(&nativeLock_);
#else
        pthread_rwlock_unlock(&nativeLock_);
#endif
    }

    void unlockWriteNative_() {
#if defined(_WIN32)
        ReleaseSRWLockExclusive(&nativeLock_);
#else
        pthread_rwlock_unlock(&nativeLock_);
#endif
    }

    bool tryLockRecursive_(int timeoutMs) {
        if (timeoutMs < 0) {
            recursiveMutex_.lock();
            return true;
        }
        if (timeoutMs == 0) {
            return recursiveMutex_.try_lock();
        }
        return recursiveMutex_.try_lock_for(std::chrono::milliseconds(timeoutMs));
    }

    bool tryLockNative_(int timeoutMs, bool writeLock) {
        if (timeoutMs < 0) {
            if (writeLock) {
                lockForWriteNative_();
                registerWriteLock_();
            } else {
                lockForReadNative_();
                registerReadLock_();
            }
            return true;
        }

        if (tryLockImmediateNative_(writeLock)) {
            return true;
        }

        if (timeoutMs == 0) {
            return false;
        }

        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            pauseBeforeRetry_();
            if (tryLockImmediateNative_(writeLock)) {
                return true;
            }
        }

        return false;
    }

    bool tryLockImmediateNative_(bool writeLock) {
        const bool locked = writeLock ? tryLockWriteImmediateNative_() : tryLockReadImmediateNative_();
        if (!locked) {
            return false;
        }

        if (writeLock) {
            registerWriteLock_();
        } else {
            registerReadLock_();
        }
        return true;
    }

    void registerReadLock_() const {
        ThreadLockState& state = threadLockStates_()[this];
        ++state.readCount;
    }

    void registerWriteLock_() const {
        ThreadLockState& state = threadLockStates_()[this];
        ++state.writeCount;
    }

    UnlockKind unregisterLock_() const {
        std::map<const SwReadWriteLock*, ThreadLockState>& states = threadLockStates_();
        typename std::map<const SwReadWriteLock*, ThreadLockState>::iterator it = states.find(this);
        if (it == states.end()) {
            return UnlockNone;
        }

        if (it->second.writeCount > 0) {
            --it->second.writeCount;
            if (it->second.readCount == 0 && it->second.writeCount == 0) {
                states.erase(it);
            }
            return UnlockWrite;
        }

        if (it->second.readCount > 0) {
            --it->second.readCount;
            if (it->second.readCount == 0 && it->second.writeCount == 0) {
                states.erase(it);
            }
            return UnlockRead;
        }

        states.erase(it);
        return UnlockNone;
    }

    static std::map<const SwReadWriteLock*, ThreadLockState>& threadLockStates_() {
        static thread_local std::map<const SwReadWriteLock*, ThreadLockState> states;
        return states;
    }

    static void pauseBeforeRetry_() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    template<typename Rep, typename Period>
    static int durationToTimeoutMs_(const std::chrono::duration<Rep, Period>& duration) {
        if (duration < std::chrono::duration<Rep, Period>::zero()) {
            return -1;
        }

        const long long timeoutMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        if (timeoutMs > 2147483647LL) {
            return 2147483647;
        }

        return static_cast<int>(timeoutMs);
    }

    RecursionMode mode_;
    std::recursive_timed_mutex recursiveMutex_;
#if defined(_WIN32)
    SRWLOCK nativeLock_;
#else
    pthread_rwlock_t nativeLock_;
#endif
};

class SwReadLocker {
public:
    explicit SwReadLocker(SwReadWriteLock* lock)
        : lock_(lock)
        , locked_(false) {
        if (lock_) {
            lock_->lockForRead();
            locked_ = true;
        }
    }

    explicit SwReadLocker(SwReadWriteLock& lock)
        : SwReadLocker(&lock) {}

    ~SwReadLocker() {
        if (lock_ && locked_) {
            lock_->unlock();
        }
    }

    SwReadLocker(const SwReadLocker&) = delete;
    SwReadLocker& operator=(const SwReadLocker&) = delete;

    void unlock() {
        if (lock_ && locked_) {
            lock_->unlock();
            locked_ = false;
        }
    }

    void relock() {
        if (lock_ && !locked_) {
            lock_->lockForRead();
            locked_ = true;
        }
    }

    SwReadWriteLock* readWriteLock() const {
        return lock_;
    }

    bool isLocked() const {
        return locked_;
    }

private:
    SwReadWriteLock* lock_;
    bool locked_;
};

class SwWriteLocker {
public:
    explicit SwWriteLocker(SwReadWriteLock* lock)
        : lock_(lock)
        , locked_(false) {
        if (lock_) {
            lock_->lockForWrite();
            locked_ = true;
        }
    }

    explicit SwWriteLocker(SwReadWriteLock& lock)
        : SwWriteLocker(&lock) {}

    ~SwWriteLocker() {
        if (lock_ && locked_) {
            lock_->unlock();
        }
    }

    SwWriteLocker(const SwWriteLocker&) = delete;
    SwWriteLocker& operator=(const SwWriteLocker&) = delete;

    void unlock() {
        if (lock_ && locked_) {
            lock_->unlock();
            locked_ = false;
        }
    }

    void relock() {
        if (lock_ && !locked_) {
            lock_->lockForWrite();
            locked_ = true;
        }
    }

    SwReadWriteLock* readWriteLock() const {
        return lock_;
    }

    bool isLocked() const {
        return locked_;
    }

private:
    SwReadWriteLock* lock_;
    bool locked_;
};

#endif
