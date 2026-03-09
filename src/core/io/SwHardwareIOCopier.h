#pragma once

/**
 * @file src/core/io/SwHardwareIOCopier.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwHardwareIOCopier in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the hardware IO copier interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwHardwareIOCopier.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
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
#include "SwThreadPool.h"
#include "SwEventLoop.h"
#include "SwMutex.h"
#include "SwMap.h"
#include "SwList.h"
#include "SwByteArray.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <thread>

/**
 * @class SwHardwareIOCopier
 * @brief Generic async copier with pluggable backends (CPU/GPU/DMA).
 *
 * This class provides:
 * - async copy jobs (linear/strided/SwByteArray),
 * - cooperative waiting for fibers (`awaitJobFiber`),
 * - backend selection through `CopyPolicy`,
 * - explicit backend registration (`setBackend`) for GPU/DMA integration.
 *
 * Default backend setup:
 * - CPU: implemented and available.
 * - GPU/DMA: "not configured" backends (fail explicitly until replaced).
 */
class SwHardwareIOCopier : public SwObject {
    SW_OBJECT(SwHardwareIOCopier, SwObject)
    DECLARE_SIGNAL(copyFinished, std::uint64_t, std::size_t)
    DECLARE_SIGNAL(copyFailed, std::uint64_t, int, const SwString&)

public:
    enum BackendHint {
        BackendAuto = 0,
        BackendCPU = 1,
        BackendGPU = 2,
        BackendDMA = 3
    };

    enum MemoryHint {
        MemoryAuto = 0,
        MemoryHost = 1,
        MemoryHostPinned = 2,
        MemoryShared = 3,
        MemoryDeviceLocal = 4
    };

    struct CopyPolicy {
        BackendHint backend = BackendAuto;
        MemoryHint sourceMemory = MemoryAuto;
        MemoryHint destinationMemory = MemoryAuto;
        bool allowCpuFallback = false;
        bool requireZeroCopy = false;
    };

    struct StridedCopyRequest {
        void* dst = nullptr;
        const void* src = nullptr;
        std::size_t dstStrideBytes = 0;
        std::size_t srcStrideBytes = 0;
        std::size_t rowBytes = 0;
        std::size_t rowCount = 0;
    };

    struct BackendResult {
        bool ok = false;
        std::size_t bytesCopied = 0u;
        int errorCode = 0;
        SwString errorString;
    };

    class Backend {
    public:
        /**
         * @brief Destroys the `Backend` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        virtual ~Backend() {}
        /**
         * @brief Returns the current hint.
         * @return The current hint.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        virtual BackendHint hint() const = 0;
        /**
         * @brief Returns the current name.
         * @return The current name.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        virtual SwString name() const = 0;
        /**
         * @brief Returns whether the object reports available.
         * @return The current available.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        virtual bool isAvailable() const { return true; }
        /**
         * @brief Performs the `copyLinear` operation.
         * @param dst Value passed to the method.
         * @param src Value passed to the method.
         * @param bytes Value passed to the method.
         * @param policy Policy value applied by the operation.
         * @return The requested copy Linear.
         */
        virtual BackendResult copyLinear(void* dst,
                                         const void* src,
                                         std::size_t bytes,
                                         const CopyPolicy& policy) = 0;
        /**
         * @brief Performs the `copyStrided` operation.
         * @param request Request instance associated with the operation.
         * @param policy Policy value applied by the operation.
         * @return The requested copy Strided.
         */
        virtual BackendResult copyStrided(const StridedCopyRequest& request,
                                          const CopyPolicy& policy) = 0;
    };

    class CpuBackend : public Backend {
    public:
        /**
         * @brief Returns the current hint.
         * @return The current hint.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        BackendHint hint() const override {
            return BackendCPU;
        }

        /**
         * @brief Returns the current name.
         * @return The current name.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        SwString name() const override {
            return "CPU";
        }

        /**
         * @brief Performs the `copyLinear` operation.
         * @param dst Value passed to the method.
         * @param src Value passed to the method.
         * @param bytes Value passed to the method.
         * @param policy Policy value applied by the operation.
         * @return The requested copy Linear.
         */
        BackendResult copyLinear(void* dst,
                                 const void* src,
                                 std::size_t bytes,
                                 const CopyPolicy& policy) override {
            SW_UNUSED(policy);
            BackendResult r;
            if (bytes == 0u) {
                r.ok = true;
                return r;
            }
            if (!dst || !src) {
                r.errorCode = -1;
                r.errorString = "Invalid source or destination pointer";
                return r;
            }
            std::memcpy(dst, src, bytes);
            r.ok = true;
            r.bytesCopied = bytes;
            return r;
        }

        /**
         * @brief Performs the `copyStrided` operation.
         * @param request Request instance associated with the operation.
         * @param policy Policy value applied by the operation.
         * @return The requested copy Strided.
         */
        BackendResult copyStrided(const StridedCopyRequest& request,
                                  const CopyPolicy& policy) override {
            SW_UNUSED(policy);
            BackendResult r;
            if (request.rowCount == 0u || request.rowBytes == 0u) {
                r.ok = true;
                return r;
            }
            if (!request.dst || !request.src) {
                r.errorCode = -1;
                r.errorString = "Invalid source or destination pointer";
                return r;
            }
            if (request.dstStrideBytes < request.rowBytes ||
                request.srcStrideBytes < request.rowBytes) {
                r.errorCode = -4;
                r.errorString = "Stride smaller than rowBytes";
                return r;
            }

            const char* srcBytes = static_cast<const char*>(request.src);
            char* dstBytes = static_cast<char*>(request.dst);
            for (std::size_t y = 0; y < request.rowCount; ++y) {
                const char* srcRow = srcBytes + (y * request.srcStrideBytes);
                char* dstRow = dstBytes + (y * request.dstStrideBytes);
                std::memcpy(dstRow, srcRow, request.rowBytes);
            }

            std::size_t total = 0u;
            if (!safeMultiply_(request.rowBytes, request.rowCount, total)) {
                r.errorCode = -3;
                r.errorString = "Requested copy size overflows size_t";
                return r;
            }
            r.ok = true;
            r.bytesCopied = total;
            return r;
        }

    private:
        static bool safeMultiply_(std::size_t a, std::size_t b, std::size_t& out) {
            if (a == 0u || b == 0u) {
                out = 0u;
                return true;
            }
            if (a > (std::numeric_limits<std::size_t>::max)() / b) {
                out = 0u;
                return false;
            }
            out = a * b;
            return true;
        }
    };

    class UnavailableBackend : public Backend {
    public:
        /**
         * @brief Constructs a `UnavailableBackend` instance.
         * @param hint Value passed to the method.
         * @param name Value passed to the method.
         * @param reason Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        UnavailableBackend(BackendHint hint, const SwString& name, const SwString& reason)
            : m_hint(hint), m_name(name), m_reason(reason) {
        }

        /**
         * @brief Returns the current hint.
         * @return The current hint.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        BackendHint hint() const override {
            return m_hint;
        }

        /**
         * @brief Returns the current name.
         * @return The current name.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        SwString name() const override {
            return m_name;
        }

        /**
         * @brief Returns whether the object reports available.
         * @return `true` when the object reports available; otherwise `false`.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        bool isAvailable() const override {
            return false;
        }

        /**
         * @brief Performs the `copyLinear` operation.
         * @param size_t Value passed to the method.
         * @return The requested copy Linear.
         */
        BackendResult copyLinear(void*,
                                 const void*,
                                 std::size_t,
                                 const CopyPolicy&) override {
            BackendResult r;
            r.errorCode = -20;
            r.errorString = m_reason;
            return r;
        }

        /**
         * @brief Performs the `copyStrided` operation.
         * @return The requested copy Strided.
         */
        BackendResult copyStrided(const StridedCopyRequest&,
                                  const CopyPolicy&) override {
            BackendResult r;
            r.errorCode = -20;
            r.errorString = m_reason;
            return r;
        }

    private:
        BackendHint m_hint = BackendAuto;
        SwString m_name;
        SwString m_reason;
    };

    /**
     * @brief Constructs a `SwHardwareIOCopier` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwHardwareIOCopier(SwObject* parent = nullptr)
        : SwObject(parent),
          m_threadPool(SwThreadPool::globalInstance()) {
        installDefaultBackends_();
    }

    /**
     * @brief Sets the thread Pool.
     * @param threadPool Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setThreadPool(SwThreadPool* threadPool) {
        SwMutexLocker locker(&m_mutex);
        m_threadPool = threadPool ? threadPool : SwThreadPool::globalInstance();
    }

    /**
     * @brief Returns the current thread Pool.
     * @return The current thread Pool.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwThreadPool* threadPool() const {
        SwMutexLocker locker(&m_mutex);
        return m_threadPool;
    }

    /**
     * @brief Sets the default Policy.
     * @param policy Policy value applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDefaultPolicy(const CopyPolicy& policy) {
        SwMutexLocker locker(&m_mutex);
        m_defaultPolicy = policy;
    }

    /**
     * @brief Returns the current default Policy.
     * @return The current default Policy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    CopyPolicy defaultPolicy() const {
        SwMutexLocker locker(&m_mutex);
        return m_defaultPolicy;
    }

    /**
     * @brief Installs/replaces a backend implementation.
     */
    void setBackend(BackendHint hint, const std::shared_ptr<Backend>& backend) {
        if (hint == BackendAuto) {
            return;
        }
        SwMutexLocker locker(&m_mutex);
        if (backend) {
            m_backends.insert(static_cast<int>(hint), backend);
        } else {
            m_backends.remove(static_cast<int>(hint));
        }
    }

    /**
     * @brief Performs the `backend` operation.
     * @param hint Value passed to the method.
     * @return The requested backend.
     */
    std::shared_ptr<Backend> backend(BackendHint hint) const {
        if (hint == BackendAuto) {
            return std::shared_ptr<Backend>();
        }
        SwMutexLocker locker(&m_mutex);
        auto it = m_backends.find(static_cast<int>(hint));
        if (it == m_backends.end()) {
            return std::shared_ptr<Backend>();
        }
        return it.value();
    }

    /**
     * @brief Returns whether the object reports backend Supported.
     * @param hint Value passed to the method.
     * @return `true` when the object reports backend Supported; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isBackendSupported(BackendHint hint) const {
        std::shared_ptr<Backend> b = backend(hint);
        return b && b->isAvailable();
    }

    /**
     * @brief Performs the `copyAsync` operation.
     * @param dst Value passed to the method.
     * @param src Value passed to the method.
     * @param bytes Value passed to the method.
     * @param priority Value passed to the method.
     * @return The requested copy Async.
     */
    std::uint64_t copyAsync(void* dst,
                            const void* src,
                            std::size_t bytes,
                            int priority = 0) {
        return copyAsync(dst, src, bytes, defaultPolicy(), priority);
    }

    /**
     * @brief Performs the `copyAsync` operation.
     * @param dst Value passed to the method.
     * @param src Value passed to the method.
     * @param bytes Value passed to the method.
     * @param policy Policy value applied by the operation.
     * @param priority Value passed to the method.
     * @return The requested copy Async.
     */
    std::uint64_t copyAsync(void* dst,
                            const void* src,
                            std::size_t bytes,
                            const CopyPolicy& policy,
                            int priority = 0) {
        const std::uint64_t jobId = createJob_(bytes);
        if (bytes == 0u) {
            finishSuccess_(jobId, 0u);
            return jobId;
        }
        if (!dst || !src) {
            finishFailure_(jobId, -1, "Invalid source or destination pointer");
            return jobId;
        }

        std::shared_ptr<Backend> selectedBackend;
        if (!selectBackend_(policy, selectedBackend, jobId)) {
            return jobId;
        }

        std::function<void()> task = [this, jobId, selectedBackend, dst, src, bytes, policy]() {
            markRunning_(jobId);
            BackendResult result = selectedBackend->copyLinear(dst, src, bytes, policy);
            if (result.ok) {
                finishSuccess_(jobId, result.bytesCopied);
            } else {
                const int code = result.errorCode != 0 ? result.errorCode : -30;
                const SwString msg = result.errorString.isEmpty() ? "Backend linear copy failed" : result.errorString;
                finishFailure_(jobId, code, msg);
            }
        };

        if (!submitTask_(task, priority)) {
            finishFailure_(jobId, -2, "Unable to submit copy task to SwThreadPool");
        }
        return jobId;
    }

    /**
     * @brief Performs the `copyStridedAsync` operation.
     * @param request Request instance associated with the operation.
     * @param priority Value passed to the method.
     * @return The requested copy Strided Async.
     */
    std::uint64_t copyStridedAsync(const StridedCopyRequest& request,
                                   int priority = 0) {
        return copyStridedAsync(request, defaultPolicy(), priority);
    }

    /**
     * @brief Performs the `copyStridedAsync` operation.
     * @param request Request instance associated with the operation.
     * @param policy Policy value applied by the operation.
     * @param priority Value passed to the method.
     * @return The requested copy Strided Async.
     */
    std::uint64_t copyStridedAsync(const StridedCopyRequest& request,
                                   const CopyPolicy& policy,
                                   int priority = 0) {
        std::size_t requestedBytes = 0u;
        if (!safeMultiply_(request.rowBytes, request.rowCount, requestedBytes)) {
            const std::uint64_t jobId = createJob_(0u);
            finishFailure_(jobId, -3, "Requested copy size overflows size_t");
            return jobId;
        }

        const std::uint64_t jobId = createJob_(requestedBytes);
        if (request.rowCount == 0u || request.rowBytes == 0u) {
            finishSuccess_(jobId, 0u);
            return jobId;
        }
        if (!request.dst || !request.src) {
            finishFailure_(jobId, -1, "Invalid source or destination pointer");
            return jobId;
        }

        std::shared_ptr<Backend> selectedBackend;
        if (!selectBackend_(policy, selectedBackend, jobId)) {
            return jobId;
        }

        std::function<void()> task = [this, jobId, selectedBackend, request, policy]() {
            markRunning_(jobId);
            BackendResult result = selectedBackend->copyStrided(request, policy);
            if (result.ok) {
                finishSuccess_(jobId, result.bytesCopied);
            } else {
                const int code = result.errorCode != 0 ? result.errorCode : -31;
                const SwString msg = result.errorString.isEmpty() ? "Backend strided copy failed" : result.errorString;
                finishFailure_(jobId, code, msg);
            }
        };

        if (!submitTask_(task, priority)) {
            finishFailure_(jobId, -2, "Unable to submit strided copy task to SwThreadPool");
        }
        return jobId;
    }

    /**
     * @brief Performs the `copyByteArrayAsync` operation.
     * @param dst Value passed to the method.
     * @param src Value passed to the method.
     * @param priority Value passed to the method.
     * @return The requested copy Byte Array Async.
     */
    std::uint64_t copyByteArrayAsync(SwByteArray* dst,
                                     const SwByteArray& src,
                                     int priority = 0) {
        return copyByteArrayAsync(dst, src, defaultPolicy(), priority);
    }

    /**
     * @brief Performs the `copyByteArrayAsync` operation.
     * @param dst Value passed to the method.
     * @param src Value passed to the method.
     * @param policy Policy value applied by the operation.
     * @param priority Value passed to the method.
     * @return The requested copy Byte Array Async.
     */
    std::uint64_t copyByteArrayAsync(SwByteArray* dst,
                                     const SwByteArray& src,
                                     const CopyPolicy& policy,
                                     int priority = 0) {
        const std::size_t bytes = src.size();
        const std::uint64_t jobId = createJob_(bytes);
        if (!dst) {
            finishFailure_(jobId, -1, "Invalid destination SwByteArray");
            return jobId;
        }

        std::shared_ptr<Backend> selectedBackend;
        if (!selectBackend_(policy, selectedBackend, jobId)) {
            return jobId;
        }

        // Keep source immutable during async operation.
        const SwByteArray srcCopy = src;
        std::function<void()> task = [this, jobId, selectedBackend, dst, srcCopy, bytes, policy]() {
            markRunning_(jobId);
            dst->resize(bytes);
            BackendResult result = selectedBackend->copyLinear(dst->data(),
                                                               srcCopy.constData(),
                                                               bytes,
                                                               policy);
            if (result.ok) {
                finishSuccess_(jobId, result.bytesCopied);
            } else {
                const int code = result.errorCode != 0 ? result.errorCode : -32;
                const SwString msg = result.errorString.isEmpty() ? "Backend byte-array copy failed" : result.errorString;
                finishFailure_(jobId, code, msg);
            }
        };

        if (!submitTask_(task, priority)) {
            finishFailure_(jobId, -2, "Unable to submit SwByteArray copy task to SwThreadPool");
        }
        return jobId;
    }

    /**
     * @brief Performs the `awaitJobFiber` operation.
     * @param jobId Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     * @return `true` on success; otherwise `false`.
     */
    bool awaitJobFiber(std::uint64_t jobId, int timeoutMs = -1) {
        if (jobId == 0u) {
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        while (true) {
            JobState snapshot;
            bool exists = false;
            {
                SwMutexLocker locker(&m_mutex);
                auto it = m_jobs.find(jobId);
                if (it != m_jobs.end()) {
                    snapshot = it.value();
                    exists = true;
                }
            }

            if (!exists) {
                return false;
            }
            if (snapshot.status == StatusSucceeded) {
                return true;
            }
            if (snapshot.status == StatusFailed) {
                return false;
            }
            if (timeoutMs == 0) {
                return false;
            }

            SwCoreApplication* app = SwCoreApplication::instance(false);
            if (!app) {
                if (timeoutMs > 0) {
                    const long long elapsed = static_cast<long long>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count());
                    if (elapsed >= timeoutMs) {
                        return false;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const int waiterId = SwCoreApplication::generateYieldId();
            {
                SwMutexLocker locker(&m_mutex);
                auto it = m_jobs.find(jobId);
                if (it == m_jobs.end()) {
                    return false;
                }
                if (isTerminal_(it.value().status)) {
                    continue;
                }
                it.value().waiterYieldIds.append(waiterId);
            }

            int timerId = -1;
            if (timeoutMs > 0) {
                const long long elapsed = static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count());
                const int remainingMs = timeoutMs - static_cast<int>(elapsed);
                if (remainingMs <= 0) {
                    removeWaiter_(jobId, waiterId);
                    return false;
                }
                timerId = app->addTimer([waiterId]() {
                    SwCoreApplication::unYieldFiberHighPriority(waiterId);
                }, remainingMs * 1000, true);
            }

            SwCoreApplication::yieldFiber(waiterId);

            if (timerId >= 0) {
                app->removeTimer(timerId);
            }

            {
                SwMutexLocker locker(&m_mutex);
                auto it = m_jobs.find(jobId);
                if (it == m_jobs.end()) {
                    return false;
                }

                if (it.value().status == StatusSucceeded) {
                    return true;
                }
                if (it.value().status == StatusFailed) {
                    return false;
                }

                removeWaiterFromState_(it.value(), waiterId);
            }

            if (timeoutMs > 0) {
                const long long elapsed = static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count());
                if (elapsed >= timeoutMs) {
                    return false;
                }
            }

            SwEventLoop::swsleep(1);
        }
    }

    /**
     * @brief Returns whether the object reports job Done.
     * @param jobId Value passed to the method.
     * @return `true` when the object reports job Done; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isJobDone(std::uint64_t jobId) const {
        SwMutexLocker locker(&m_mutex);
        auto it = m_jobs.find(jobId);
        if (it == m_jobs.end()) {
            return false;
        }
        return isTerminal_(it.value().status);
    }

    /**
     * @brief Performs the `jobSucceeded` operation.
     * @param jobId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool jobSucceeded(std::uint64_t jobId) const {
        SwMutexLocker locker(&m_mutex);
        auto it = m_jobs.find(jobId);
        return (it != m_jobs.end()) && (it.value().status == StatusSucceeded);
    }

    /**
     * @brief Performs the `jobBytesCopied` operation.
     * @param jobId Value passed to the method.
     * @return The requested job Bytes Copied.
     */
    std::size_t jobBytesCopied(std::uint64_t jobId) const {
        SwMutexLocker locker(&m_mutex);
        auto it = m_jobs.find(jobId);
        if (it == m_jobs.end()) {
            return 0u;
        }
        return it.value().copiedBytes;
    }

    /**
     * @brief Performs the `jobErrorString` operation.
     * @param jobId Value passed to the method.
     * @return The requested job Error String.
     */
    SwString jobErrorString(std::uint64_t jobId) const {
        SwMutexLocker locker(&m_mutex);
        auto it = m_jobs.find(jobId);
        if (it == m_jobs.end()) {
            return "Unknown job";
        }
        return it.value().errorString;
    }

    /**
     * @brief Clears the current object state.
     */
    void clearFinishedJobs() {
        SwList<std::uint64_t> finished;
        {
            SwMutexLocker locker(&m_mutex);
            for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
                if (isTerminal_(it.value().status)) {
                    finished.append(it.key());
                }
            }
            for (std::size_t i = 0; i < finished.size(); ++i) {
                m_jobs.remove(finished[i]);
            }
        }
    }

private:
    enum JobStatus {
        StatusPending = 0,
        StatusRunning = 1,
        StatusSucceeded = 2,
        StatusFailed = 3
    };

    struct JobState {
        JobStatus status = StatusPending;
        std::size_t requestedBytes = 0u;
        std::size_t copiedBytes = 0u;
        int errorCode = 0;
        SwString errorString;
        SwList<int> waiterYieldIds;
    };

    static bool safeMultiply_(std::size_t a, std::size_t b, std::size_t& out) {
        if (a == 0u || b == 0u) {
            out = 0u;
            return true;
        }
        if (a > (std::numeric_limits<std::size_t>::max)() / b) {
            out = 0u;
            return false;
        }
        out = a * b;
        return true;
    }

    static bool isTerminal_(JobStatus status) {
        return status == StatusSucceeded || status == StatusFailed;
    }

    void installDefaultBackends_() {
        m_backends.insert(static_cast<int>(BackendCPU), std::make_shared<CpuBackend>());
        m_backends.insert(static_cast<int>(BackendGPU),
                          std::make_shared<UnavailableBackend>(BackendGPU, "GPU", "GPU backend not configured"));
        m_backends.insert(static_cast<int>(BackendDMA),
                          std::make_shared<UnavailableBackend>(BackendDMA, "DMA", "DMA backend not configured"));
    }

    bool isDeviceLocal_(MemoryHint mem) const {
        return mem == MemoryDeviceLocal;
    }

    bool selectBackend_(const CopyPolicy& policy,
                        std::shared_ptr<Backend>& selectedBackend,
                        std::uint64_t jobId) {
        selectedBackend.reset();

        if (policy.backend == BackendAuto) {
            // Auto path: if zero-copy/device-local is requested, try HW backends first.
            const bool needsHardware = policy.requireZeroCopy ||
                                       isDeviceLocal_(policy.sourceMemory) ||
                                       isDeviceLocal_(policy.destinationMemory);
            if (needsHardware) {
                auto gpu = backend(BackendGPU);
                if (gpu && gpu->isAvailable()) {
                    selectedBackend = gpu;
                    return true;
                }
                auto dma = backend(BackendDMA);
                if (dma && dma->isAvailable()) {
                    selectedBackend = dma;
                    return true;
                }

                if (policy.requireZeroCopy) {
                    finishFailure_(jobId, -8, "Zero-copy requested but no hardware backend is available");
                    return false;
                }
                if (isDeviceLocal_(policy.sourceMemory) || isDeviceLocal_(policy.destinationMemory)) {
                    finishFailure_(jobId, -10, "No hardware backend available for device-local memory");
                    return false;
                }
            }

            auto cpu = backend(BackendCPU);
            if (!cpu || !cpu->isAvailable()) {
                finishFailure_(jobId, -21, "No available backend (CPU unavailable)");
                return false;
            }
            selectedBackend = cpu;
            return true;
        }

        auto requested = backend(policy.backend);
        if (!requested || !requested->isAvailable()) {
            if (!policy.allowCpuFallback ||
                policy.backend == BackendCPU ||
                policy.requireZeroCopy ||
                isDeviceLocal_(policy.sourceMemory) ||
                isDeviceLocal_(policy.destinationMemory)) {
                finishFailure_(jobId, -22, "Requested backend is not available");
                return false;
            }

            auto cpu = backend(BackendCPU);
            if (!cpu || !cpu->isAvailable()) {
                finishFailure_(jobId, -21, "Requested backend unavailable and CPU fallback unavailable");
                return false;
            }
            selectedBackend = cpu;
            return true;
        }

        if (policy.requireZeroCopy && policy.backend == BackendCPU) {
            finishFailure_(jobId, -8, "Zero-copy requested but CPU backend selected");
            return false;
        }
        if ((isDeviceLocal_(policy.sourceMemory) || isDeviceLocal_(policy.destinationMemory)) &&
            policy.backend == BackendCPU) {
            finishFailure_(jobId, -10, "CPU backend cannot access device-local memory");
            return false;
        }

        selectedBackend = requested;
        return true;
    }

    std::uint64_t createJob_(std::size_t requestedBytes) {
        const std::uint64_t jobId = m_nextJobId.fetch_add(1, std::memory_order_relaxed);
        JobState state;
        state.requestedBytes = requestedBytes;
        SwMutexLocker locker(&m_mutex);
        m_jobs.insert(jobId, state);
        return jobId;
    }

    void markRunning_(std::uint64_t jobId) {
        SwMutexLocker locker(&m_mutex);
        auto it = m_jobs.find(jobId);
        if (it == m_jobs.end()) {
            return;
        }
        if (it.value().status == StatusPending) {
            it.value().status = StatusRunning;
        }
    }

    void finishSuccess_(std::uint64_t jobId, std::size_t copiedBytes) {
        SwList<int> waiters;
        {
            SwMutexLocker locker(&m_mutex);
            auto it = m_jobs.find(jobId);
            if (it == m_jobs.end() || isTerminal_(it.value().status)) {
                return;
            }
            it.value().status = StatusSucceeded;
            it.value().copiedBytes = copiedBytes;
            waiters = it.value().waiterYieldIds;
            it.value().waiterYieldIds.clear();
        }

        wakeWaiters_(waiters);
        postResult_([this, jobId, copiedBytes]() {
            emit copyFinished(jobId, copiedBytes);
        });
    }

    void finishFailure_(std::uint64_t jobId, int errorCode, const SwString& message) {
        SwList<int> waiters;
        {
            SwMutexLocker locker(&m_mutex);
            auto it = m_jobs.find(jobId);
            if (it == m_jobs.end() || isTerminal_(it.value().status)) {
                return;
            }
            it.value().status = StatusFailed;
            it.value().errorCode = errorCode;
            it.value().errorString = message;
            waiters = it.value().waiterYieldIds;
            it.value().waiterYieldIds.clear();
        }

        wakeWaiters_(waiters);
        postResult_([this, jobId, errorCode, message]() {
            emit copyFailed(jobId, errorCode, message);
        });
    }

    void postResult_(std::function<void()> task) {
        if (!task) {
            return;
        }
        ThreadHandle* affinity = threadHandle();
        if (affinity && ThreadHandle::currentThread() != affinity) {
            affinity->postTask(std::move(task));
            return;
        }
        task();
    }

    static void wakeWaiters_(const SwList<int>& waiters) {
        for (std::size_t i = 0; i < waiters.size(); ++i) {
            SwCoreApplication::unYieldFiberHighPriority(waiters[i]);
        }
    }

    static void removeWaiterFromState_(JobState& state, int waiterId) {
        for (std::size_t i = 0; i < state.waiterYieldIds.size(); ++i) {
            if (state.waiterYieldIds[i] == waiterId) {
                state.waiterYieldIds.removeAt(i);
                break;
            }
        }
    }

    void removeWaiter_(std::uint64_t jobId, int waiterId) {
        SwMutexLocker locker(&m_mutex);
        auto it = m_jobs.find(jobId);
        if (it == m_jobs.end()) {
            return;
        }
        removeWaiterFromState_(it.value(), waiterId);
    }

    bool submitTask_(const std::function<void()>& task, int priority) {
        SwThreadPool* pool = nullptr;
        {
            SwMutexLocker locker(&m_mutex);
            pool = m_threadPool;
        }
        if (!pool) {
            pool = SwThreadPool::globalInstance();
        }
        if (!pool || !task) {
            return false;
        }
        pool->start(task, priority);
        return true;
    }

    mutable SwMutex m_mutex;
    SwMap<std::uint64_t, JobState> m_jobs;
    SwMap<int, std::shared_ptr<Backend>> m_backends;
    std::atomic<std::uint64_t> m_nextJobId{1u};
    SwThreadPool* m_threadPool = nullptr;
    CopyPolicy m_defaultPolicy;
};
