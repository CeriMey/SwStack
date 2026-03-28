#pragma once

/**
 * @file src/core/runtime/SwFiberPool.h
 * @ingroup core_runtime
 * @brief Header-only fiber pool and scheduler used by SwCoreApplication.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "SwMutex.h"
#include "SwDebug.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/win/SwWindows.h"
#else
#include "linux_fiber.h"
#endif

static constexpr const char* kSwLogCategory_SwFiberPool = "sw.core.runtime.swfiberpool";

enum class SwFiberLane {
    Control = 0,
    Input = 1,
    Normal = 2,
    Background = 3
};

struct SwFiberLaneCounters {
    int control;
    int input;
    int normal;
    int background;

    SwFiberLaneCounters()
        : control(0),
          input(0),
          normal(0),
          background(0) {}

    int value(SwFiberLane lane) const {
        switch (lane) {
        case SwFiberLane::Control:
            return control;
        case SwFiberLane::Input:
            return input;
        case SwFiberLane::Normal:
            return normal;
        case SwFiberLane::Background:
        default:
            return background;
        }
    }

    int& ref(SwFiberLane lane) {
        switch (lane) {
        case SwFiberLane::Control:
            return control;
        case SwFiberLane::Input:
            return input;
        case SwFiberLane::Normal:
            return normal;
        case SwFiberLane::Background:
        default:
            return background;
        }
    }
};

struct SwFiberPoolConfig {
    int warmFiberCount;
    int maxFiberCount;
    int emergencySpilloverCount;
    unsigned int stackSizeBytes;

    SwFiberPoolConfig()
        : warmFiberCount(500),
          maxFiberCount(500),
          emergencySpilloverCount(16),
          stackSizeBytes(128u * 1024u) {}
};

struct SwFiberPoolStats {
    int warmCount;
    int spilloverCount;
    int idleCount;
    int runningCount;
    int yieldedCount;
    SwFiberLaneCounters queuedCountByLane;
    SwFiberLaneCounters highWaterMarks;
    long long rejectedCount;
    long long tasksExecuted;
    long long tasksReused;

    SwFiberPoolStats()
        : warmCount(0),
          spilloverCount(0),
          idleCount(0),
          runningCount(0),
          yieldedCount(0),
          rejectedCount(0),
          tasksExecuted(0),
          tasksReused(0) {}
};

class SwFiberPoolObserver {
public:
    virtual ~SwFiberPoolObserver() {}
    virtual void onFiberDispatchEnter(SwFiberLane lane, bool resumed) = 0;
    virtual void onFiberDispatchExit(SwFiberLane lane, bool resumed, long long durationUs) = 0;
};

class SwFiberPool {
public:
    SwFiberPool()
        : mainFiber_(nullptr),
          runningFiberAtomic_(nullptr),
          fiberStartNsAtomic_(nullptr),
          observer_(nullptr),
          watchdogFlag_(nullptr),
          warmInitialized_(false),
          currentSlot_(nullptr),
          rejectedCount_(0),
          tasksExecuted_(0),
          tasksReused_(0),
          inputBudget_(8),
          normalBudget_(4),
          backgroundBudget_(1) {}

    void bindMainFiber(LPVOID mainFiber) {
        SwMutexLocker locker(&mutex_);
        mainFiber_ = mainFiber;
    }

    void bindRuntimeState(std::atomic<LPVOID>* runningFiberAtomic,
                          std::atomic<int64_t>* fiberStartNsAtomic,
                          std::atomic<bool>* watchdogFlag) {
        SwMutexLocker locker(&mutex_);
        runningFiberAtomic_ = runningFiberAtomic;
        fiberStartNsAtomic_ = fiberStartNsAtomic;
        watchdogFlag_ = watchdogFlag;
    }

    void bindObserver(SwFiberPoolObserver* observer) {
        SwMutexLocker locker(&mutex_);
        observer_ = observer;
    }

    void setConfig(const SwFiberPoolConfig& config) {
        SwMutexLocker locker(&mutex_);
        config_ = config;
        if (config_.warmFiberCount < 0) {
            config_.warmFiberCount = 0;
        }
        if (config_.maxFiberCount < config_.warmFiberCount) {
            config_.maxFiberCount = config_.warmFiberCount;
        }
        if (config_.emergencySpilloverCount < 0) {
            config_.emergencySpilloverCount = 0;
        }
        if (config_.stackSizeBytes == 0) {
            config_.stackSizeBytes = 128u * 1024u;
        }
    }

    SwFiberPoolConfig config() const {
        SwMutexLocker locker(&mutex_);
        return config_;
    }

    bool isWarmInitialized() const {
        SwMutexLocker locker(&mutex_);
        return warmInitialized_;
    }

    bool containsYieldId(int id) const {
        SwMutexLocker locker(&mutex_);
        return yieldedFibers_.find(id) != yieldedFibers_.end();
    }

    void ensureWarmInitialized() {
        LPVOID localMainFiber = nullptr;
        int currentWarmCount = 0;
        int targetWarmCount = 0;
        {
            SwMutexLocker locker(&mutex_);
            if (warmInitialized_) {
                return;
            }
            localMainFiber = mainFiber_;
            currentWarmCount = warmCountLocked_();
            targetWarmCount = warmTargetLocked_();
        }

        if (!localMainFiber) {
            return;
        }

        while (currentWarmCount < targetWarmCount) {
            if (!createFiberSlot_(false)) {
                break;
            }
            ++currentWarmCount;
        }

        SwMutexLocker locker(&mutex_);
        warmInitialized_ = true;
    }

    bool enqueueTask(const std::function<void()>& task,
                     SwFiberLane lane,
                     bool* rejectedByBackpressure = nullptr) {
        return enqueueTaskImpl_(task, lane, rejectedByBackpressure);
    }

    bool enqueueTask(std::function<void()>&& task,
                     SwFiberLane lane,
                     bool* rejectedByBackpressure = nullptr) {
        return enqueueTaskImpl_(std::move(task), lane, rejectedByBackpressure);
    }

    bool hasWork() const {
        SwMutexLocker locker(&mutex_);
        return hasControlWorkLocked_() || hasNonControlWorkLocked_();
    }

    bool hasControlWork() const {
        SwMutexLocker locker(&mutex_);
        return hasLaneWorkLocked_(SwFiberLane::Control);
    }

    bool hasNonControlWork() const {
        SwMutexLocker locker(&mutex_);
        return hasNonControlWorkLocked_();
    }

    bool runNextWorkItem() {
        ensureWarmInitialized();

#if defined(__ANDROID__)
        std::function<void()> inlineTask;
        {
            SwMutexLocker locker(&mutex_);
            const SwFiberLane lane = chooseNextLaneLocked_();
            if (!isValidLane_(lane)) {
                return false;
            }
            const int laneIndex = laneIndex_(lane);
            if (pendingTasks_[laneIndex].empty()) {
                return false;
            }
            inlineTask = std::move(pendingTasks_[laneIndex].front());
            pendingTasks_[laneIndex].pop_front();
        }
        executeCallbackSafely_(inlineTask);
        SwMutexLocker androidLocker(&mutex_);
        ++tasksExecuted_;
        return true;
#else
        FiberSlot* slot = nullptr;
        std::function<void()> newTask;
        bool dropDequeuedTask = false;

        {
            SwMutexLocker locker(&mutex_);
            const SwFiberLane lane = chooseNextLaneLocked_();
            if (!isValidLane_(lane)) {
                return false;
            }

            const int laneIndex = laneIndex_(lane);
            if (!readyFibers_[laneIndex].empty()) {
                slot = readyFibers_[laneIndex].front();
                readyFibers_[laneIndex].pop_front();
                if (slot) {
                    slot->queuedReady = false;
                    slot->running = true;
                    slot->readyLane = lane;
                    slot->resumedDispatch = true;
                }
            } else if (!pendingTasks_[laneIndex].empty()) {
                newTask = std::move(pendingTasks_[laneIndex].front());
                pendingTasks_[laneIndex].pop_front();
                slot = acquireDispatchSlotLocked_(lane);
                if (!slot) {
                    dropDequeuedTask = true;
                    ++rejectedCount_;
                } else {
                    if (!slot->newlyCreated) {
                        ++tasksReused_;
                    }
                    slot->taskAssigned = true;
                    slot->running = true;
                    slot->yielded = false;
                    slot->queuedReady = false;
                    slot->idle = false;
                    slot->yieldId = 0;
                    slot->assignedLane = lane;
                    slot->readyLane = lane;
                    slot->resumedDispatch = false;
                    slot->callback = std::move(newTask);
                }
            }
        }

        if (dropDequeuedTask) {
            swCWarning(kSwLogCategory_SwFiberPool)
                << "Dropping queued fiber task because no dispatch slot is available.";
            return true;
        }

        if (!slot) {
            return false;
        }

        switchToSlot_(slot);
        return true;
#endif
    }

    void yieldCurrent(int id) {
#if defined(__ANDROID__)
        (void)id;
        swCWarning(kSwLogCategory_SwFiberPool)
            << "yieldCurrent() is not supported on Android fake fibers.";
#else
        FiberSlot* slot = currentRunningSlot_();
        if (!slot || !mainFiber_) {
            return;
        }

        {
            SwMutexLocker locker(&mutex_);
            FiberSlot* previous = nullptr;
            std::map<int, FiberSlot*>::iterator it = yieldedFibers_.find(id);
            if (it != yieldedFibers_.end()) {
                previous = it->second;
            }
            if (previous && previous != slot) {
                previous->yieldId = 0;
            }
            yieldedFibers_[id] = slot;
            slot->yieldId = id;
            slot->yielded = true;
            slot->running = false;
            slot->idle = false;
        }

        clearRunningState_();
        SwitchToFiber(mainFiber_);
#endif
    }

    bool unYield(int id, SwFiberLane wakeLane) {
        FiberSlot* slot = nullptr;
        {
            SwMutexLocker locker(&mutex_);
            std::map<int, FiberSlot*>::iterator it = yieldedFibers_.find(id);
            if (it == yieldedFibers_.end()) {
                return false;
            }
            slot = it->second;
            yieldedFibers_.erase(it);
            if (!slot || !slot->handle) {
                return false;
            }
            slot->yieldId = 0;
            slot->yielded = false;
            slot->readyLane = wakeLane;
            if (!slot->queuedReady) {
                readyFibers_[laneIndex_(wakeLane)].push_back(slot);
                slot->queuedReady = true;
                updateHighWaterMarkLocked_(wakeLane);
            }
        }
        return true;
    }

    void releaseCurrent() {
#if defined(__ANDROID__)
        swCWarning(kSwLogCategory_SwFiberPool)
            << "releaseCurrent() is not supported on Android fake fibers.";
#else
        FiberSlot* slot = currentRunningSlot_();
        if (!slot || !mainFiber_) {
            return;
        }

        {
            SwMutexLocker locker(&mutex_);
            if (!slot->queuedReady) {
                readyFibers_[laneIndex_(slot->readyLane)].push_back(slot);
                slot->queuedReady = true;
                updateHighWaterMarkLocked_(slot->readyLane);
            }
            slot->running = false;
            slot->idle = false;
        }

        clearRunningState_();
        SwitchToFiber(mainFiber_);
#endif
    }

    SwFiberPoolStats stats() const {
        SwFiberPoolStats stats;
        SwMutexLocker locker(&mutex_);

        stats.warmCount = warmCountLocked_();
        stats.spilloverCount = spilloverCountLocked_();
        stats.idleCount = static_cast<int>(idleWarmFibers_.size() + idleSpillFibers_.size());
        stats.runningCount = runningCountLocked_();
        stats.yieldedCount = static_cast<int>(yieldedFibers_.size());
        stats.rejectedCount = rejectedCount_;
        stats.tasksExecuted = tasksExecuted_;
        stats.tasksReused = tasksReused_;
        stats.highWaterMarks = highWaterMarks_;

        for (int i = 0; i < 4; ++i) {
            const SwFiberLane lane = laneFromIndex_(i);
            stats.queuedCountByLane.ref(lane) =
                static_cast<int>(pendingTasks_[i].size() + readyFibers_[i].size());
        }
        return stats;
    }

    void shutdown() {
#if defined(__ANDROID__)
        SwMutexLocker locker(&mutex_);
        pendingTasks_[0].clear();
        pendingTasks_[1].clear();
        pendingTasks_[2].clear();
        pendingTasks_[3].clear();
        readyFibers_[0].clear();
        readyFibers_[1].clear();
        readyFibers_[2].clear();
        readyFibers_[3].clear();
        yieldedFibers_.clear();
        slots_.clear();
        idleWarmFibers_.clear();
        idleSpillFibers_.clear();
        warmInitialized_ = false;
#else
        std::vector<LPVOID> handlesToDelete;
        {
            SwMutexLocker locker(&mutex_);
            for (size_t i = 0; i < slots_.size(); ++i) {
                FiberSlot* slot = slots_[i].get();
                if (slot && slot->handle && slot->handle != mainFiber_) {
                    handlesToDelete.push_back(slot->handle);
                    slot->handle = nullptr;
                }
            }
            pendingTasks_[0].clear();
            pendingTasks_[1].clear();
            pendingTasks_[2].clear();
            pendingTasks_[3].clear();
            readyFibers_[0].clear();
            readyFibers_[1].clear();
            readyFibers_[2].clear();
            readyFibers_[3].clear();
            yieldedFibers_.clear();
            idleWarmFibers_.clear();
            idleSpillFibers_.clear();
            slots_.clear();
            warmInitialized_ = false;
            currentSlot_ = nullptr;
        }

        for (size_t i = 0; i < handlesToDelete.size(); ++i) {
            DeleteFiber(handlesToDelete[i]);
        }
#endif
    }

private:
    struct FiberSlot {
        FiberSlot()
            : pool(nullptr),
              handle(nullptr),
              assignedLane(SwFiberLane::Normal),
              readyLane(SwFiberLane::Normal),
              yieldId(0),
              warm(false),
              spillover(false),
              idle(true),
              running(false),
              yielded(false),
              queuedReady(false),
              taskAssigned(false),
              newlyCreated(false),
              resumedDispatch(false) {}

        SwFiberPool* pool;
        LPVOID handle;
        std::function<void()> callback;
        SwFiberLane assignedLane;
        SwFiberLane readyLane;
        int yieldId;
        bool warm;
        bool spillover;
        bool idle;
        bool running;
        bool yielded;
        bool queuedReady;
        bool taskAssigned;
        bool newlyCreated;
        bool resumedDispatch;
    };

    template<typename TTask>
    bool enqueueTaskImpl_(TTask&& task,
                          SwFiberLane lane,
                          bool* rejectedByBackpressure) {
        if (rejectedByBackpressure) {
            *rejectedByBackpressure = false;
        }
        ensureWarmInitialized();

        SwMutexLocker locker(&mutex_);
        if (!canAcceptTaskLocked_(lane)) {
            ++rejectedCount_;
            if (rejectedByBackpressure) {
                *rejectedByBackpressure = true;
            }
            return false;
        }

        pendingTasks_[laneIndex_(lane)].push_back(std::forward<TTask>(task));
        updateHighWaterMarkLocked_(lane);
        return true;
    }

    static int laneIndex_(SwFiberLane lane) {
        switch (lane) {
        case SwFiberLane::Control:
            return 0;
        case SwFiberLane::Input:
            return 1;
        case SwFiberLane::Normal:
            return 2;
        case SwFiberLane::Background:
        default:
            return 3;
        }
    }

    static SwFiberLane laneFromIndex_(int index) {
        switch (index) {
        case 0:
            return SwFiberLane::Control;
        case 1:
            return SwFiberLane::Input;
        case 2:
            return SwFiberLane::Normal;
        case 3:
        default:
            return SwFiberLane::Background;
        }
    }

    static bool isValidLane_(SwFiberLane lane) {
        return lane == SwFiberLane::Control ||
               lane == SwFiberLane::Input ||
               lane == SwFiberLane::Normal ||
               lane == SwFiberLane::Background;
    }

    int warmTargetLocked_() const {
        return std::max(0, (std::min)(config_.warmFiberCount, config_.maxFiberCount));
    }

    int warmCountLocked_() const {
        int count = 0;
        for (size_t i = 0; i < slots_.size(); ++i) {
            const FiberSlot* slot = slots_[i].get();
            if (slot && slot->warm && slot->handle) {
                ++count;
            }
        }
        return count;
    }

    int spilloverCountLocked_() const {
        int count = 0;
        for (size_t i = 0; i < slots_.size(); ++i) {
            const FiberSlot* slot = slots_[i].get();
            if (slot && slot->spillover && slot->handle) {
                ++count;
            }
        }
        return count;
    }

    int runningCountLocked_() const {
        int count = 0;
        for (size_t i = 0; i < slots_.size(); ++i) {
            const FiberSlot* slot = slots_[i].get();
            if (slot && slot->handle && slot->running) {
                ++count;
            }
        }
        return count;
    }

    int occupiedSlotCountLocked_() const {
        int count = 0;
        for (size_t i = 0; i < slots_.size(); ++i) {
            const FiberSlot* slot = slots_[i].get();
            if (slot && slot->handle && !slot->idle) {
                ++count;
            }
        }
        return count;
    }

    int pendingTaskCountLocked_() const {
        return static_cast<int>(pendingTasks_[0].size() +
                                pendingTasks_[1].size() +
                                pendingTasks_[2].size() +
                                pendingTasks_[3].size());
    }

    bool canAcceptTaskLocked_(SwFiberLane lane) const {
        const int demand = occupiedSlotCountLocked_() + pendingTaskCountLocked_();
        const int warmCapacity = warmTargetLocked_();
        if (lane == SwFiberLane::Control || lane == SwFiberLane::Input) {
            return demand < (warmCapacity + config_.emergencySpilloverCount);
        }
        return demand < warmCapacity;
    }

    bool hasLaneWorkLocked_(SwFiberLane lane) const {
        const int laneIndex = laneIndex_(lane);
        return !readyFibers_[laneIndex].empty() || !pendingTasks_[laneIndex].empty();
    }

    bool hasControlWorkLocked_() const {
        return hasLaneWorkLocked_(SwFiberLane::Control);
    }

    bool hasNonControlWorkLocked_() const {
        return hasLaneWorkLocked_(SwFiberLane::Input) ||
               hasLaneWorkLocked_(SwFiberLane::Normal) ||
               hasLaneWorkLocked_(SwFiberLane::Background);
    }

    void resetBudgetsLocked_() {
        inputBudget_ = 8;
        normalBudget_ = 4;
        backgroundBudget_ = 1;
    }

    SwFiberLane chooseNextLaneLocked_() {
        if (hasControlWorkLocked_()) {
            return SwFiberLane::Control;
        }

        for (int pass = 0; pass < 2; ++pass) {
            if (inputBudget_ <= 0 && normalBudget_ <= 0 && backgroundBudget_ <= 0) {
                resetBudgetsLocked_();
            }

            if (inputBudget_ > 0 && hasLaneWorkLocked_(SwFiberLane::Input)) {
                --inputBudget_;
                return SwFiberLane::Input;
            }
            if (normalBudget_ > 0 && hasLaneWorkLocked_(SwFiberLane::Normal)) {
                --normalBudget_;
                return SwFiberLane::Normal;
            }
            if (backgroundBudget_ > 0 && hasLaneWorkLocked_(SwFiberLane::Background)) {
                --backgroundBudget_;
                return SwFiberLane::Background;
            }

            if (!hasNonControlWorkLocked_()) {
                break;
            }
            resetBudgetsLocked_();
        }

        if (hasLaneWorkLocked_(SwFiberLane::Input)) {
            return SwFiberLane::Input;
        }
        if (hasLaneWorkLocked_(SwFiberLane::Normal)) {
            return SwFiberLane::Normal;
        }
        if (hasLaneWorkLocked_(SwFiberLane::Background)) {
            return SwFiberLane::Background;
        }
        return static_cast<SwFiberLane>(-1);
    }

    void updateHighWaterMarkLocked_(SwFiberLane lane) {
        const int laneIndex = laneIndex_(lane);
        const int queuedCount = static_cast<int>(pendingTasks_[laneIndex].size() + readyFibers_[laneIndex].size());
        int& highWater = highWaterMarks_.ref(lane);
        if (queuedCount > highWater) {
            highWater = queuedCount;
        }
    }

    FiberSlot* acquireDispatchSlotLocked_(SwFiberLane lane) {
        FiberSlot* slot = nullptr;
        if (!idleWarmFibers_.empty()) {
            slot = idleWarmFibers_.front();
            idleWarmFibers_.pop_front();
        } else if ((lane == SwFiberLane::Control || lane == SwFiberLane::Input) && !idleSpillFibers_.empty()) {
            slot = idleSpillFibers_.front();
            idleSpillFibers_.pop_front();
        } else if ((lane == SwFiberLane::Control || lane == SwFiberLane::Input) &&
                   spilloverCountLocked_() < config_.emergencySpilloverCount) {
            slot = createFiberSlotLocked_(true);
            if (slot && !idleSpillFibers_.empty() && idleSpillFibers_.back() == slot) {
                idleSpillFibers_.pop_back();
            }
        }

        if (slot) {
            slot->idle = false;
        }
        return slot;
    }

    static void WINAPI fiberEntryProc_(LPVOID parameter) {
#if !defined(__ANDROID__)
        FiberSlot* slot = static_cast<FiberSlot*>(parameter);
        if (!slot || !slot->pool) {
            return;
        }
        slot->pool->fiberEntryLoop_(slot);
#else
        (void)parameter;
#endif
    }

#if !defined(__ANDROID__)
    void fiberEntryLoop_(FiberSlot* slot) {
        if (!slot) {
            return;
        }
        while (true) {
            if (slot->taskAssigned && slot->callback) {
                executeCallbackSafely_(slot->callback);
                completeTask_(slot);
            }

            if (!mainFiber_) {
                return;
            }
            SwitchToFiber(mainFiber_);
        }
    }
#endif

    static void executeCallbackSafely_(const std::function<void()>& callback) {
        try {
            callback();
        } catch (const std::exception& e) {
            swCError(kSwLogCategory_SwFiberPool)
                << "Unhandled exception in fiber callback: " << e.what();
        } catch (...) {
            swCError(kSwLogCategory_SwFiberPool)
                << "Unhandled unknown exception in fiber callback";
        }
    }

    bool createFiberSlot_(bool spillover) {
        SwMutexLocker locker(&mutex_);
        return createFiberSlotLocked_(spillover) != nullptr;
    }

    FiberSlot* createFiberSlotLocked_(bool spillover) {
        if (!mainFiber_) {
            return nullptr;
        }

        std::unique_ptr<FiberSlot> slot(new FiberSlot());
        slot->pool = this;
        slot->warm = !spillover;
        slot->spillover = spillover;
        slot->assignedLane = spillover ? SwFiberLane::Input : SwFiberLane::Normal;
        slot->readyLane = slot->assignedLane;
        slot->newlyCreated = true;

        const SIZE_T stackSize = static_cast<SIZE_T>(config_.stackSizeBytes ? config_.stackSizeBytes : 128u * 1024u);
        slot->handle = CreateFiber(stackSize, &SwFiberPool::fiberEntryProc_, slot.get());
        if (!slot->handle) {
#if defined(_WIN32)
            swCError(kSwLogCategory_SwFiberPool) << "Failed to create fiber. Error: " << GetLastError();
#else
            swCError(kSwLogCategory_SwFiberPool) << "Failed to create fiber.";
#endif
            return nullptr;
        }

        FiberSlot* rawSlot = slot.get();
        slots_.push_back(std::move(slot));
        if (spillover) {
            idleSpillFibers_.push_back(rawSlot);
        } else {
            idleWarmFibers_.push_back(rawSlot);
        }
        return rawSlot;
    }

    FiberSlot* currentRunningSlot_() {
        if (!currentSlot_) {
            return nullptr;
        }
        if (currentSlot_->handle != GetCurrentFiber()) {
            return nullptr;
        }
        return currentSlot_;
    }

    void markRunningState_(FiberSlot* slot) {
        currentSlot_ = slot;
        if (runningFiberAtomic_) {
            runningFiberAtomic_->store(slot ? slot->handle : nullptr, std::memory_order_release);
        }
        if (fiberStartNsAtomic_) {
            fiberStartNsAtomic_->store(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count(),
                std::memory_order_release);
        }
    }

    void clearRunningState_() {
        if (runningFiberAtomic_) {
            runningFiberAtomic_->store(nullptr, std::memory_order_release);
        }
        if (fiberStartNsAtomic_) {
            fiberStartNsAtomic_->store(0, std::memory_order_release);
        }
    }

    void completeTask_(FiberSlot* slot) {
        if (!slot) {
            return;
        }

        SwMutexLocker locker(&mutex_);
        slot->callback = std::function<void()>();
        slot->taskAssigned = false;
        slot->running = false;
        slot->yielded = false;
        slot->queuedReady = false;
        slot->yieldId = 0;
        slot->idle = true;
        slot->newlyCreated = false;
        slot->resumedDispatch = false;
        ++tasksExecuted_;

        if (slot->spillover) {
            idleSpillFibers_.push_back(slot);
        } else {
            idleWarmFibers_.push_back(slot);
        }
    }

    void handleWatchdogReturn_(FiberSlot* slot) {
        if (!slot || !watchdogFlag_) {
            return;
        }

        if (!watchdogFlag_->exchange(false, std::memory_order_acq_rel)) {
            return;
        }

#if defined(_WIN32)
        swCWarning(kSwLogCategory_SwFiberPool)
            << "Watchdog preempted a pooled fiber on Windows; retiring the fiber slot.";

        LPVOID oldHandle = nullptr;
        {
            SwMutexLocker locker(&mutex_);
            oldHandle = slot->handle;
            slot->handle = nullptr;
            slot->callback = std::function<void()>();
            slot->taskAssigned = false;
            slot->running = false;
            slot->yielded = false;
            slot->queuedReady = false;
            slot->yieldId = 0;
            slot->idle = true;
            slot->newlyCreated = false;
            slot->resumedDispatch = false;
            removeSlotFromQueuesLocked_(slot);
            eraseYieldedReferenceLocked_(slot);
        }

        if (oldHandle) {
            DeleteFiber(oldHandle);
        }

        ensureWarmReplacement_();
#else
        SwMutexLocker locker(&mutex_);
        if (!slot->queuedReady) {
            readyFibers_[laneIndex_(slot->readyLane)].push_back(slot);
            slot->queuedReady = true;
            slot->running = false;
            slot->idle = false;
            updateHighWaterMarkLocked_(slot->readyLane);
        }
#endif
    }

    void ensureWarmReplacement_() {
        int warmCount = 0;
        int warmTarget = 0;
        {
            SwMutexLocker locker(&mutex_);
            warmCount = warmCountLocked_();
            warmTarget = warmTargetLocked_();
        }

        while (warmCount < warmTarget) {
            if (!createFiberSlot_(false)) {
                break;
            }
            ++warmCount;
        }
    }

    void removeSlotFromQueuesLocked_(FiberSlot* slot) {
        if (!slot) {
            return;
        }

        for (int laneIndex = 0; laneIndex < 4; ++laneIndex) {
            std::deque<FiberSlot*>& queue = readyFibers_[laneIndex];
            for (std::deque<FiberSlot*>::iterator it = queue.begin(); it != queue.end(); ++it) {
                if (*it == slot) {
                    queue.erase(it);
                    break;
                }
            }
        }

        for (std::deque<FiberSlot*>::iterator it = idleWarmFibers_.begin(); it != idleWarmFibers_.end(); ++it) {
            if (*it == slot) {
                idleWarmFibers_.erase(it);
                break;
            }
        }
        for (std::deque<FiberSlot*>::iterator it = idleSpillFibers_.begin(); it != idleSpillFibers_.end(); ++it) {
            if (*it == slot) {
                idleSpillFibers_.erase(it);
                break;
            }
        }
    }

    void eraseYieldedReferenceLocked_(FiberSlot* slot) {
        if (!slot) {
            return;
        }
        for (std::map<int, FiberSlot*>::iterator it = yieldedFibers_.begin(); it != yieldedFibers_.end(); ++it) {
            if (it->second == slot) {
                yieldedFibers_.erase(it);
                break;
            }
        }
    }

    void switchToSlot_(FiberSlot* slot) {
        if (!slot || !slot->handle || !mainFiber_) {
            return;
        }

        SwFiberPoolObserver* observer = nullptr;
        bool resumed = false;
        SwFiberLane lane = SwFiberLane::Normal;
        {
            SwMutexLocker locker(&mutex_);
            observer = observer_;
            resumed = slot->resumedDispatch;
            lane = slot->readyLane;
        }
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (observer) {
            observer->onFiberDispatchEnter(lane, resumed);
        }
        markRunningState_(slot);
        SwitchToFiber(slot->handle);
        clearRunningState_();
        if (observer) {
            const long long durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - start)
                                             .count();
            observer->onFiberDispatchExit(lane, resumed, durationUs);
        }
        currentSlot_ = nullptr;
        handleWatchdogReturn_(slot);
    }

    mutable SwMutex mutex_;
    LPVOID mainFiber_;
    std::atomic<LPVOID>* runningFiberAtomic_;
    std::atomic<int64_t>* fiberStartNsAtomic_;
    SwFiberPoolObserver* observer_;
    std::atomic<bool>* watchdogFlag_;
    SwFiberPoolConfig config_;
    bool warmInitialized_;
    std::vector<std::unique_ptr<FiberSlot> > slots_;
    std::deque<FiberSlot*> idleWarmFibers_;
    std::deque<FiberSlot*> idleSpillFibers_;
    std::deque<FiberSlot*> readyFibers_[4];
    std::deque<std::function<void()> > pendingTasks_[4];
    std::map<int, FiberSlot*> yieldedFibers_;
    FiberSlot* currentSlot_;
    long long rejectedCount_;
    long long tasksExecuted_;
    long long tasksReused_;
    SwFiberLaneCounters highWaterMarks_;
    int inputBudget_;
    int normalBudget_;
    int backgroundBudget_;
};
