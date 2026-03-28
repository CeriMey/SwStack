#pragma once

/**
 * @file src/core/runtime/SwRuntimeProfiler.h
 * @ingroup core_runtime
 * @brief Lightweight runtime profiling primitives for SwCoreApplication-based loops.
 */

#include "SwFiberPool.h"
#include "SwList.h"
#include "SwString.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#elif !defined(__ANDROID__)
#include <execinfo.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

enum class SwRuntimeTimingKind {
    PlatformPump = 0,
    PostedEvent = 1,
    ObjectEvent = 2,
    Timer = 3,
    FiberTask = 4,
    ManualScope = 5
};

struct SwRuntimeProfileConfig {
    long long stallThresholdUs;
    long long monitorPeriodUs;
    int recordCapacity;
    int maxStackFrames;
    long long stallCooldownUs;
    bool enableAutoRuntimeScopes;
    bool enableManualScopes;
    bool enableStackCaptureOnStall;

    SwRuntimeProfileConfig()
        : stallThresholdUs(10000),
          monitorPeriodUs(2000),
          recordCapacity(4096),
          maxStackFrames(64),
          stallCooldownUs(500000),
          enableAutoRuntimeScopes(true),
          enableManualScopes(true),
          enableStackCaptureOnStall(true) {}
};

struct SwRuntimeTimingRecord {
    SwRuntimeTimingKind kind;
    const char* label;
    long long durationUs;
    unsigned long long threadId;
    SwFiberLane lane;

    SwRuntimeTimingRecord()
        : kind(SwRuntimeTimingKind::ManualScope),
          label(""),
          durationUs(0),
          threadId(0),
          lane(SwFiberLane::Normal) {}
};

struct SwRuntimeCountersSnapshot {
    double loadPercentage;
    double lastSecondLoadPercentage;
    unsigned long long busyMicroseconds;
    unsigned long long totalMicroseconds;
    int postedEventCount;
    int priorityPostedEventCount;
    int timerCount;
    SwFiberPoolStats fiberPoolStats;
    long long droppedRecords;

    SwRuntimeCountersSnapshot()
        : loadPercentage(0.0),
          lastSecondLoadPercentage(0.0),
          busyMicroseconds(0),
          totalMicroseconds(0),
          postedEventCount(0),
          priorityPostedEventCount(0),
          timerCount(0),
          droppedRecords(0) {}
};

struct SwRuntimeStallReport {
    unsigned long long threadId;
    SwRuntimeTimingKind kind;
    const char* label;
    long long elapsedUs;
    SwFiberLane lane;
    SwList<unsigned long long> frames;
    SwList<SwString> symbols;

    SwRuntimeStallReport()
        : threadId(0),
          kind(SwRuntimeTimingKind::ManualScope),
          label(""),
          elapsedUs(0),
          lane(SwFiberLane::Normal) {}
};

class SwRuntimeProfileSink {
public:
    virtual ~SwRuntimeProfileSink() {}
    virtual void onRuntimeBatch(const SwList<SwRuntimeTimingRecord>& records,
                                const SwRuntimeCountersSnapshot& counters) = 0;
    virtual void onStall(const SwRuntimeStallReport& report) = 0;
};

class SwRuntimeProfilerSession;

class SwRuntimeScopedSpan {
public:
    SwRuntimeScopedSpan(SwRuntimeProfilerSession* session,
                        SwRuntimeTimingKind kind,
                        const char* label,
                        SwFiberLane lane,
                        bool emitTiming = true);
    ~SwRuntimeScopedSpan();

private:
    SwRuntimeProfilerSession* session_;
};

class SwProfileScope {
public:
    explicit SwProfileScope(const char* label);

private:
    SwRuntimeScopedSpan scope_;
};

class SwRuntimeProfilerService;

class SwRuntimeProfilerSession : public SwFiberPoolObserver,
                                 public std::enable_shared_from_this<SwRuntimeProfilerSession> {
public:
    explicit SwRuntimeProfilerSession(SwRuntimeProfileSink* sink,
                                      const SwRuntimeProfileConfig& config = SwRuntimeProfileConfig());
    ~SwRuntimeProfilerSession();

    const SwRuntimeProfileConfig& config() const;
    bool autoRuntimeScopesEnabled() const;
    bool manualScopesEnabled() const;
    void setStallThresholdUs(long long thresholdUs);
    long long stallThresholdUs() const;

    void bindToCurrentThread();
    void clearThreadCurrent();
    static SwRuntimeProfilerSession* current();

    void updateCounters(const SwRuntimeCountersSnapshot& snapshot);
    SwRuntimeCountersSnapshot countersSnapshot() const;

    void recordTiming(SwRuntimeTimingKind kind,
                      const char* label,
                      long long durationUs,
                      SwFiberLane lane);
    SwList<SwRuntimeTimingRecord> drainRecords();

    void onFiberDispatchEnter(SwFiberLane lane, bool resumed) override;
    void onFiberDispatchExit(SwFiberLane lane, bool resumed, long long durationUs) override;

    void beginScope(SwRuntimeTimingKind kind,
                    const char* label,
                    SwFiberLane lane,
                    bool emitTiming);
    void endScope();

    bool maybeEmitStall();
    bool emitBatchIfNeeded();

private:
    struct ScopeFrame_ {
        SwRuntimeTimingKind kind;
        const char* label;
        SwFiberLane lane;
        bool emitTiming;
        unsigned long long spanId;
        long long accumulatedRunningNs;
        long long runningSegmentStartNs;

        ScopeFrame_()
            : kind(SwRuntimeTimingKind::ManualScope),
              label(""),
              lane(SwFiberLane::Normal),
              emitTiming(true),
              spanId(0),
              accumulatedRunningNs(0),
              runningSegmentStartNs(0) {}
    };

    static long long nowNs_();
    static unsigned long long currentThreadKey_();
    std::size_t nextIndex_(std::size_t index) const;
    bool tryPushRecord_(const SwRuntimeTimingRecord& record);
    void publishCurrentActiveScope_();
    void loadCurrentActiveSpan_(unsigned long long& spanIdOut,
                                long long& startNsOut,
                                SwRuntimeTimingKind& kindOut,
                                SwFiberLane& laneOut,
                                const char*& labelOut) const;
    void callSinkBatch_(const SwList<SwRuntimeTimingRecord>& batch,
                        const SwRuntimeCountersSnapshot& snapshot);
    void callSinkStall_(const SwRuntimeStallReport& report);
    void captureStack_(SwList<unsigned long long>& framesOut,
                       SwList<SwString>& symbolsOut);

#if defined(_WIN32)
    void captureWindowsStack_(SwList<unsigned long long>& framesOut,
                              SwList<SwString>& symbolsOut);
#elif !defined(__ANDROID__)
    static int linuxSignalNumber_();
    static void installLinuxSignalHandlerOnce_();
    static void linuxSignalHandler_(int signalNumber, siginfo_t* info, void* uctx);
    void captureLinuxStack_(SwList<unsigned long long>& framesOut,
                            SwList<SwString>& symbolsOut);
    static const int kLinuxMaxSampleFrames_ = 128;
#endif

    static SwRuntimeProfilerSession*& currentSessionTls_();

    SwRuntimeProfileSink* sink_;
    SwRuntimeProfileConfig config_;
    std::atomic<long long> stallThresholdUsLive_;
    std::vector<SwRuntimeTimingRecord> records_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
    std::atomic<long long> droppedRecords_;

    mutable std::mutex countersMutex_;
    SwRuntimeCountersSnapshot countersSnapshot_;

    std::atomic<unsigned long long> nextSpanId_;
    unsigned long long currentThreadIdKey_;
    std::atomic<bool> ownerBound_;

#if defined(_WIN32)
    DWORD nativeThreadId_;
#elif !defined(__ANDROID__)
    pthread_t nativePthread_;
    std::atomic<unsigned long long> sampleRequestSeq_;
    std::atomic<unsigned long long> sampleResponseSeq_;
    std::atomic<int> sampleFrameCount_;
    void* sampleFrames_[128];
#endif

    std::vector<ScopeFrame_> frames_;
    std::atomic<unsigned long long> activeScopeSpanId_;
    std::atomic<long long> activeScopeStartNs_;
    std::atomic<int> activeScopeKind_;
    std::atomic<int> activeScopeLane_;
    std::atomic<const char*> activeScopeLabel_;

    std::atomic<unsigned long long> dispatchSpanId_;
    std::atomic<long long> dispatchStartNs_;
    std::atomic<int> dispatchLane_;
    std::atomic<const char*> dispatchLabel_;
    std::atomic<bool> dispatchResumed_;

    std::atomic<unsigned long long> lastReportedSpanId_;
    std::atomic<long long> lastReportedNs_;
    std::atomic<bool> executing_;
};

class SwRuntimeProfilerService {
public:
    static void registerSession(const std::shared_ptr<SwRuntimeProfilerSession>& session);
    static void unregisterSession(SwRuntimeProfilerSession* session);

private:
    struct State_ {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::shared_ptr<SwRuntimeProfilerSession> > sessions;
        std::thread thread;
        bool stopRequested;

        State_()
            : stopRequested(false) {}

        ~State_();
    };

    static State_& instance_();
    static void threadEntry_();
};

inline long long SwRuntimeProfilerSession::nowNs_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline unsigned long long SwRuntimeProfilerSession::currentThreadKey_() {
    return static_cast<unsigned long long>(std::hash<std::thread::id>()(std::this_thread::get_id()));
}

inline std::size_t SwRuntimeProfilerSession::nextIndex_(std::size_t index) const {
    ++index;
    if (index >= records_.size()) {
        index = 0;
    }
    return index;
}

inline bool SwRuntimeProfilerSession::tryPushRecord_(const SwRuntimeTimingRecord& record) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t nextHead = nextIndex_(head);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (nextHead == tail) {
        return false;
    }
    records_[head] = record;
    head_.store(nextHead, std::memory_order_release);
    return true;
}

inline SwRuntimeProfilerSession::SwRuntimeProfilerSession(SwRuntimeProfileSink* sink,
                                                          const SwRuntimeProfileConfig& config)
    : sink_(sink),
      config_(config),
      stallThresholdUsLive_(config_.stallThresholdUs),
      head_(0),
      tail_(0),
      droppedRecords_(0),
      nextSpanId_(1),
      currentThreadIdKey_(0),
      ownerBound_(false),
      activeScopeSpanId_(0),
      activeScopeStartNs_(0),
      activeScopeKind_(static_cast<int>(SwRuntimeTimingKind::ManualScope)),
      activeScopeLane_(static_cast<int>(SwFiberLane::Normal)),
      activeScopeLabel_(""),
      dispatchSpanId_(0),
      dispatchStartNs_(0),
      dispatchLane_(static_cast<int>(SwFiberLane::Normal)),
      dispatchLabel_("fiber_resume"),
      dispatchResumed_(false),
      lastReportedSpanId_(0),
      lastReportedNs_(0),
      executing_(false) {
    const int normalizedCapacity = config_.recordCapacity > 0 ? config_.recordCapacity : 1;
    records_.resize(static_cast<std::size_t>(normalizedCapacity + 1));
    frames_.reserve(32);
#if defined(_WIN32)
    nativeThreadId_ = 0;
#elif !defined(__ANDROID__)
    nativePthread_ = pthread_t();
    sampleRequestSeq_.store(0, std::memory_order_relaxed);
    sampleResponseSeq_.store(0, std::memory_order_relaxed);
    sampleFrameCount_.store(0, std::memory_order_relaxed);
    installLinuxSignalHandlerOnce_();
#endif
}

inline SwRuntimeProfilerSession::~SwRuntimeProfilerSession() {
    clearThreadCurrent();
}

inline const SwRuntimeProfileConfig& SwRuntimeProfilerSession::config() const {
    return config_;
}

inline bool SwRuntimeProfilerSession::autoRuntimeScopesEnabled() const {
    return config_.enableAutoRuntimeScopes;
}

inline bool SwRuntimeProfilerSession::manualScopesEnabled() const {
    return config_.enableManualScopes;
}

inline void SwRuntimeProfilerSession::setStallThresholdUs(long long thresholdUs) {
    stallThresholdUsLive_.store(std::max(1000LL, thresholdUs), std::memory_order_release);
}

inline long long SwRuntimeProfilerSession::stallThresholdUs() const {
    return stallThresholdUsLive_.load(std::memory_order_acquire);
}

inline void SwRuntimeProfilerSession::bindToCurrentThread() {
    currentSessionTls_() = this;
    if (ownerBound_.load(std::memory_order_acquire)) {
        return;
    }
    currentThreadIdKey_ = currentThreadKey_();
#if defined(_WIN32)
    nativeThreadId_ = ::GetCurrentThreadId();
#elif !defined(__ANDROID__)
    nativePthread_ = pthread_self();
#endif
    ownerBound_.store(true, std::memory_order_release);
}

inline void SwRuntimeProfilerSession::clearThreadCurrent() {
    if (currentSessionTls_() == this) {
        currentSessionTls_() = nullptr;
    }
}

inline SwRuntimeProfilerSession* SwRuntimeProfilerSession::current() {
    return currentSessionTls_();
}

inline void SwRuntimeProfilerSession::updateCounters(const SwRuntimeCountersSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(countersMutex_);
    countersSnapshot_ = snapshot;
}

inline SwRuntimeCountersSnapshot SwRuntimeProfilerSession::countersSnapshot() const {
    std::lock_guard<std::mutex> lock(countersMutex_);
    SwRuntimeCountersSnapshot snapshot = countersSnapshot_;
    snapshot.droppedRecords = droppedRecords_.load(std::memory_order_acquire);
    return snapshot;
}

inline void SwRuntimeProfilerSession::recordTiming(SwRuntimeTimingKind kind,
                                                   const char* label,
                                                   long long durationUs,
                                                   SwFiberLane lane) {
    SwRuntimeTimingRecord record;
    record.kind = kind;
    record.label = label ? label : "";
    record.durationUs = durationUs;
    record.threadId = currentThreadIdKey_;
    record.lane = lane;
    if (!tryPushRecord_(record)) {
        droppedRecords_.fetch_add(1, std::memory_order_relaxed);
    }
}

inline SwList<SwRuntimeTimingRecord> SwRuntimeProfilerSession::drainRecords() {
    SwList<SwRuntimeTimingRecord> out;
    while (true) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            break;
        }
        out.append(records_[tail]);
        tail_.store(nextIndex_(tail), std::memory_order_release);
    }
    return out;
}

inline void SwRuntimeProfilerSession::publishCurrentActiveScope_() {
    if (!executing_.load(std::memory_order_acquire) || frames_.empty()) {
        activeScopeSpanId_.store(0, std::memory_order_release);
        activeScopeStartNs_.store(0, std::memory_order_release);
        activeScopeLabel_.store("", std::memory_order_release);
        activeScopeKind_.store(static_cast<int>(SwRuntimeTimingKind::ManualScope),
                               std::memory_order_release);
        activeScopeLane_.store(static_cast<int>(SwFiberLane::Normal), std::memory_order_release);
        return;
    }

    const ScopeFrame_& top = frames_.back();
    if (top.runningSegmentStartNs == 0) {
        activeScopeSpanId_.store(0, std::memory_order_release);
        activeScopeStartNs_.store(0, std::memory_order_release);
        activeScopeLabel_.store("", std::memory_order_release);
        activeScopeKind_.store(static_cast<int>(SwRuntimeTimingKind::ManualScope),
                               std::memory_order_release);
        activeScopeLane_.store(static_cast<int>(SwFiberLane::Normal), std::memory_order_release);
        return;
    }

    activeScopeKind_.store(static_cast<int>(top.kind), std::memory_order_release);
    activeScopeLane_.store(static_cast<int>(top.lane), std::memory_order_release);
    activeScopeLabel_.store(top.label ? top.label : "", std::memory_order_release);
    activeScopeStartNs_.store(top.runningSegmentStartNs, std::memory_order_release);
    activeScopeSpanId_.store(top.spanId, std::memory_order_release);
}

inline void SwRuntimeProfilerSession::loadCurrentActiveSpan_(unsigned long long& spanIdOut,
                                                             long long& startNsOut,
                                                             SwRuntimeTimingKind& kindOut,
                                                             SwFiberLane& laneOut,
                                                             const char*& labelOut) const {
    const unsigned long long scopeSpanId = activeScopeSpanId_.load(std::memory_order_acquire);
    const long long scopeStartNs = activeScopeStartNs_.load(std::memory_order_acquire);
    if (scopeSpanId != 0 && scopeStartNs != 0) {
        spanIdOut = scopeSpanId;
        startNsOut = scopeStartNs;
        kindOut = static_cast<SwRuntimeTimingKind>(activeScopeKind_.load(std::memory_order_acquire));
        laneOut = static_cast<SwFiberLane>(activeScopeLane_.load(std::memory_order_acquire));
        labelOut = activeScopeLabel_.load(std::memory_order_acquire);
        return;
    }

    if (!executing_.load(std::memory_order_acquire)) {
        spanIdOut = 0;
        startNsOut = 0;
        kindOut = SwRuntimeTimingKind::FiberTask;
        laneOut = SwFiberLane::Normal;
        labelOut = "";
        return;
    }

    if (!dispatchResumed_.load(std::memory_order_acquire)) {
        spanIdOut = 0;
        startNsOut = 0;
        kindOut = SwRuntimeTimingKind::FiberTask;
        laneOut = SwFiberLane::Normal;
        labelOut = "";
        return;
    }

    spanIdOut = dispatchSpanId_.load(std::memory_order_acquire);
    startNsOut = dispatchStartNs_.load(std::memory_order_acquire);
    kindOut = SwRuntimeTimingKind::FiberTask;
    laneOut = static_cast<SwFiberLane>(dispatchLane_.load(std::memory_order_acquire));
    labelOut = dispatchLabel_.load(std::memory_order_acquire);
}

inline void SwRuntimeProfilerSession::onFiberDispatchEnter(SwFiberLane lane, bool resumed) {
    bindToCurrentThread();
    const long long nowNs = nowNs_();
    executing_.store(true, std::memory_order_release);
    if (!frames_.empty()) {
        ScopeFrame_& top = frames_.back();
        if (top.runningSegmentStartNs == 0) {
            top.runningSegmentStartNs = nowNs;
        }
    }
    if (resumed) {
        dispatchResumed_.store(true, std::memory_order_release);
        dispatchLane_.store(static_cast<int>(lane), std::memory_order_release);
        dispatchStartNs_.store(nowNs, std::memory_order_release);
        dispatchSpanId_.store(nextSpanId_.fetch_add(1, std::memory_order_acq_rel),
                              std::memory_order_release);
    } else {
        dispatchResumed_.store(false, std::memory_order_release);
        dispatchStartNs_.store(0, std::memory_order_release);
        dispatchSpanId_.store(0, std::memory_order_release);
    }
    publishCurrentActiveScope_();
}

inline void SwRuntimeProfilerSession::onFiberDispatchExit(SwFiberLane lane,
                                                          bool resumed,
                                                          long long durationUs) {
    const long long nowNs = nowNs_();
    if (!frames_.empty()) {
        ScopeFrame_& top = frames_.back();
        if (top.runningSegmentStartNs != 0) {
            top.accumulatedRunningNs += (nowNs - top.runningSegmentStartNs);
            top.runningSegmentStartNs = 0;
        }
    }
    executing_.store(false, std::memory_order_release);
    publishCurrentActiveScope_();
    if (resumed && durationUs >= 0) {
        recordTiming(SwRuntimeTimingKind::FiberTask, "fiber_resume", durationUs, lane);
    }
    dispatchResumed_.store(false, std::memory_order_release);
    dispatchStartNs_.store(0, std::memory_order_release);
    dispatchSpanId_.store(0, std::memory_order_release);
}

inline void SwRuntimeProfilerSession::beginScope(SwRuntimeTimingKind kind,
                                                 const char* label,
                                                 SwFiberLane lane,
                                                 bool emitTiming) {
    bindToCurrentThread();
    ScopeFrame_ frame;
    frame.kind = kind;
    frame.label = label ? label : "";
    frame.lane = lane;
    frame.emitTiming = emitTiming;
    frame.accumulatedRunningNs = 0;
    frame.spanId = nextSpanId_.fetch_add(1, std::memory_order_acq_rel);
    frame.runningSegmentStartNs = executing_.load(std::memory_order_acquire) ? nowNs_() : 0;
    frames_.push_back(frame);
    publishCurrentActiveScope_();
}

inline void SwRuntimeProfilerSession::endScope() {
    if (frames_.empty()) {
        return;
    }
    const long long nowNs = nowNs_();
    ScopeFrame_ frame = frames_.back();
    frames_.pop_back();
    if (frame.runningSegmentStartNs != 0) {
        frame.accumulatedRunningNs += (nowNs - frame.runningSegmentStartNs);
    }
    publishCurrentActiveScope_();
    if (frame.emitTiming) {
        const long long durationUs = frame.accumulatedRunningNs / 1000;
        recordTiming(frame.kind, frame.label, durationUs, frame.lane);
    }
}

inline void SwRuntimeProfilerSession::callSinkBatch_(const SwList<SwRuntimeTimingRecord>& batch,
                                                     const SwRuntimeCountersSnapshot& snapshot) {
    try {
        sink_->onRuntimeBatch(batch, snapshot);
    } catch (...) {
    }
}

inline void SwRuntimeProfilerSession::callSinkStall_(const SwRuntimeStallReport& report) {
    try {
        sink_->onStall(report);
    } catch (...) {
    }
}

inline bool SwRuntimeProfilerSession::emitBatchIfNeeded() {
    if (!sink_) {
        return false;
    }
    SwList<SwRuntimeTimingRecord> batch = drainRecords();
    if (batch.isEmpty()) {
        return false;
    }
    const SwRuntimeCountersSnapshot snapshot = countersSnapshot();
    callSinkBatch_(batch, snapshot);
    return true;
}

inline bool SwRuntimeProfilerSession::maybeEmitStall() {
    if (!sink_ || !config_.enableStackCaptureOnStall) {
        return false;
    }

    unsigned long long spanId = 0;
    long long startNs = 0;
    SwRuntimeTimingKind kind = SwRuntimeTimingKind::ManualScope;
    SwFiberLane lane = SwFiberLane::Normal;
    const char* label = "";

    loadCurrentActiveSpan_(spanId, startNs, kind, lane, label);
    if (spanId == 0 || startNs == 0) {
        return false;
    }

    const long long nowNs = nowNs_();
    const long long elapsedUs = (nowNs - startNs) / 1000;
    const long long thresholdUs = stallThresholdUs();
    if (elapsedUs < thresholdUs) {
        return false;
    }

    if (lastReportedSpanId_.load(std::memory_order_acquire) == spanId) {
        return false;
    }
    const long long lastReportedNs = lastReportedNs_.load(std::memory_order_acquire);
    if (lastReportedNs != 0 && ((nowNs - lastReportedNs) / 1000) < config_.stallCooldownUs) {
        return false;
    }

    SwRuntimeStallReport report;
    report.threadId = currentThreadIdKey_;
    report.kind = kind;
    report.label = label;
    report.elapsedUs = elapsedUs;
    report.lane = lane;
    captureStack_(report.frames, report.symbols);

    lastReportedSpanId_.store(spanId, std::memory_order_release);
    lastReportedNs_.store(nowNs, std::memory_order_release);
    callSinkStall_(report);
    return true;
}

inline void SwRuntimeProfilerSession::captureStack_(SwList<unsigned long long>& framesOut,
                                                    SwList<SwString>& symbolsOut) {
#if defined(_WIN32)
    captureWindowsStack_(framesOut, symbolsOut);
#elif !defined(__ANDROID__)
    captureLinuxStack_(framesOut, symbolsOut);
#else
    (void)framesOut;
    (void)symbolsOut;
#endif
}

#if defined(_WIN32)
inline void SwRuntimeProfilerSession::captureWindowsStack_(SwList<unsigned long long>& framesOut,
                                                           SwList<SwString>& symbolsOut) {
    if (!ownerBound_.load(std::memory_order_acquire) || nativeThreadId_ == 0) {
        return;
    }

    HANDLE threadHandle = ::OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                                       FALSE,
                                       nativeThreadId_);
    if (!threadHandle) {
        return;
    }

    const DWORD suspendResult = ::SuspendThread(threadHandle);
    if (suspendResult == static_cast<DWORD>(-1)) {
        ::CloseHandle(threadHandle);
        return;
    }

    CONTEXT ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;

    if (!::GetThreadContext(threadHandle, &ctx)) {
        ::ResumeThread(threadHandle);
        ::CloseHandle(threadHandle);
        return;
    }

    static std::mutex s_dbghelpMutex;
    std::lock_guard<std::mutex> dbghelpLock(s_dbghelpMutex);

    HANDLE process = ::GetCurrentProcess();
    ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    const BOOL symReady = ::SymInitialize(process, nullptr, TRUE);

    STACKFRAME64 frame;
    std::memset(&frame, 0, sizeof(frame));
    DWORD machine = 0;

#if defined(_M_X64) || defined(_WIN64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rsp;
    frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif

    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    const int maxFrames = std::max(1, config_.maxStackFrames);
    for (int i = 0; i < maxFrames; ++i) {
        if (!::StackWalk64(machine,
                           process,
                           threadHandle,
                           &frame,
                           &ctx,
                           nullptr,
                           ::SymFunctionTableAccess64,
                           ::SymGetModuleBase64,
                           nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }

        const unsigned long long addr = static_cast<unsigned long long>(frame.AddrPC.Offset);
        framesOut.append(addr);

        if (!symReady) {
            continue;
        }

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        std::memset(symbolBuffer, 0, sizeof(symbolBuffer));
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line;
        std::memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisp = 0;

        const BOOL haveSymbol = ::SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol);
        const BOOL haveLine = ::SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line);

        if (haveSymbol && haveLine) {
            char buffer[1024];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%s + 0x%llx (%s:%lu)",
                          symbol->Name,
                          static_cast<unsigned long long>(displacement),
                          line.FileName ? line.FileName : "?",
                          static_cast<unsigned long>(line.LineNumber));
            symbolsOut.append(SwString(buffer));
        } else if (haveSymbol) {
            char buffer[512];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%s + 0x%llx",
                          symbol->Name,
                          static_cast<unsigned long long>(displacement));
            symbolsOut.append(SwString(buffer));
        }
    }

    if (symReady) {
        ::SymCleanup(process);
    }
    ::ResumeThread(threadHandle);
    ::CloseHandle(threadHandle);
}
#elif !defined(__ANDROID__)
inline int SwRuntimeProfilerSession::linuxSignalNumber_() {
    return SIGUSR1;
}

inline void SwRuntimeProfilerSession::installLinuxSignalHandlerOnce_() {
    static std::once_flag s_once;
    std::call_once(s_once, []() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = &SwRuntimeProfilerSession::linuxSignalHandler_;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        (void)::sigaction(linuxSignalNumber_(), &sa, nullptr);
    });
}

inline void SwRuntimeProfilerSession::linuxSignalHandler_(int /*signalNumber*/,
                                                          siginfo_t* /*info*/,
                                                          void* /*uctx*/) {
    SwRuntimeProfilerSession* session = currentSessionTls_();
    if (!session) {
        return;
    }
    const unsigned long long request = session->sampleRequestSeq_.load(std::memory_order_acquire);
    const unsigned long long response = session->sampleResponseSeq_.load(std::memory_order_acquire);
    if (request == 0 || request == response) {
        return;
    }

    const int frameLimit = std::max(1, std::min(session->config_.maxStackFrames, kLinuxMaxSampleFrames_));
    const int captured = ::backtrace(session->sampleFrames_, frameLimit);
    session->sampleFrameCount_.store(captured, std::memory_order_release);
    session->sampleResponseSeq_.store(request, std::memory_order_release);
}

inline void SwRuntimeProfilerSession::captureLinuxStack_(SwList<unsigned long long>& framesOut,
                                                         SwList<SwString>& symbolsOut) {
    if (!ownerBound_.load(std::memory_order_acquire)) {
        return;
    }

    const unsigned long long request = sampleRequestSeq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    sampleFrameCount_.store(0, std::memory_order_release);
    (void)::pthread_kill(nativePthread_, linuxSignalNumber_());

    const long long deadlineNs = nowNs_() + 20000000LL;
    while (sampleResponseSeq_.load(std::memory_order_acquire) != request) {
        if (nowNs_() >= deadlineNs) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    const int frameCount = sampleFrameCount_.load(std::memory_order_acquire);
    if (frameCount <= 0) {
        return;
    }

    std::vector<void*> localFrames;
    localFrames.reserve(static_cast<std::size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i) {
        localFrames.push_back(sampleFrames_[i]);
        framesOut.append(static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(sampleFrames_[i])));
    }

    char** symbols = ::backtrace_symbols(localFrames.data(), frameCount);
    if (symbols) {
        for (int i = 0; i < frameCount; ++i) {
            symbolsOut.append(SwString(symbols[i]));
        }
        std::free(symbols);
    } else {
        for (int i = 0; i < frameCount; ++i) {
            Dl_info info;
            std::memset(&info, 0, sizeof(info));
            if (::dladdr(localFrames[static_cast<std::size_t>(i)], &info) && info.dli_sname) {
                symbolsOut.append(SwString(info.dli_sname));
            }
        }
    }
}
#endif

inline SwRuntimeProfilerSession*& SwRuntimeProfilerSession::currentSessionTls_() {
    static thread_local SwRuntimeProfilerSession* s_session = nullptr;
    return s_session;
}

inline SwRuntimeProfilerService::State_::~State_() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopRequested = true;
    }
    cv.notify_all();
    if (thread.joinable()) {
        thread.join();
    }
}

inline SwRuntimeProfilerService::State_& SwRuntimeProfilerService::instance_() {
    static State_ s_state;
    return s_state;
}

inline void SwRuntimeProfilerService::registerSession(const std::shared_ptr<SwRuntimeProfilerSession>& session) {
    State_& state = instance_();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.sessions.push_back(session);
        if (!state.thread.joinable()) {
            state.stopRequested = false;
            state.thread = std::thread(&SwRuntimeProfilerService::threadEntry_);
        }
    }
    state.cv.notify_all();
}

inline void SwRuntimeProfilerService::unregisterSession(SwRuntimeProfilerSession* session) {
    if (!session) {
        return;
    }
    State_& state = instance_();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        for (std::vector<std::shared_ptr<SwRuntimeProfilerSession> >::iterator it = state.sessions.begin();
             it != state.sessions.end();) {
            if (!(*it) || it->get() == session) {
                it = state.sessions.erase(it);
            } else {
                ++it;
            }
        }
    }
    state.cv.notify_all();
}

inline void SwRuntimeProfilerService::threadEntry_() {
    State_& state = instance_();
    while (true) {
        std::vector<std::shared_ptr<SwRuntimeProfilerSession> > sessionsCopy;
        long long sleepUs = 2000;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            if (state.sessions.empty() && !state.stopRequested) {
                state.cv.wait(lock, [&state]() { return state.stopRequested || !state.sessions.empty(); });
            }
            if (state.stopRequested && state.sessions.empty()) {
                break;
            }
            sessionsCopy = state.sessions;
            for (std::size_t i = 0; i < sessionsCopy.size(); ++i) {
                if (!sessionsCopy[i]) {
                    continue;
                }
                const long long configured = sessionsCopy[i]->config().monitorPeriodUs;
                if (configured > 0 && configured < sleepUs) {
                    sleepUs = configured;
                }
            }
        }

        for (std::size_t i = 0; i < sessionsCopy.size(); ++i) {
            if (!sessionsCopy[i]) {
                continue;
            }
            (void)sessionsCopy[i]->emitBatchIfNeeded();
            (void)sessionsCopy[i]->maybeEmitStall();
        }

        if (sleepUs <= 0) {
            sleepUs = 2000;
        }
        std::unique_lock<std::mutex> lock(state.mutex);
        state.cv.wait_for(lock, std::chrono::microseconds(sleepUs));
    }
}

inline SwRuntimeScopedSpan::SwRuntimeScopedSpan(SwRuntimeProfilerSession* session,
                                                SwRuntimeTimingKind kind,
                                                const char* label,
                                                SwFiberLane lane,
                                                bool emitTiming)
    : session_(session) {
    if (session_) {
        session_->beginScope(kind, label, lane, emitTiming);
    }
}

inline SwRuntimeScopedSpan::~SwRuntimeScopedSpan() {
    if (session_) {
        session_->endScope();
    }
}

inline SwProfileScope::SwProfileScope(const char* label)
    : scope_(SwRuntimeProfilerSession::current() &&
                 SwRuntimeProfilerSession::current()->manualScopesEnabled()
                 ? SwRuntimeProfilerSession::current()
                 : nullptr,
             SwRuntimeTimingKind::ManualScope,
             label ? label : "",
             SwFiberLane::Normal,
             true) {}

#ifndef SW_RUNTIME_PROFILE_CONCAT_INNER_
#define SW_RUNTIME_PROFILE_CONCAT_INNER_(a, b) a##b
#define SW_RUNTIME_PROFILE_CONCAT_(a, b) SW_RUNTIME_PROFILE_CONCAT_INNER_(a, b)
#endif

#define swProfileScope(label) \
    SwProfileScope SW_RUNTIME_PROFILE_CONCAT_(_swProfileScope_, __LINE__)(label)
