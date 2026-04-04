#include "SwByteArray.h"
#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwScrollArea.h"
#include "SwWidgetSnapshot.h"
#include "core/io/SwSocketTrafficTelemetry.h"
#include "core/runtime/SwRuntimeApplicationMonitor.h"
#include "../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerProfilingView.h"
#include "../41-RuntimeProfilerSelfTest/widgets/RuntimeProfilerViewTypes.h"
#include "widgets/SocketTrafficProfilingView.h"
#include "widgets/SocketTrafficRtspPlayerWidget.h"
#include "widgets/SocketTrafficViewTypes.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace {

static const int kUiRefreshPeriodUs_ = 60000;
static const int kHistoryLimit_ = 180;
static const int kProfilerChartHistoryLimit_ = 96;
static const int kProfilerLoadHistoryLimit_ = 480;

static long long steadyNowNs_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static SwString humanBytes_(unsigned long long bytes) {
    const double value = static_cast<double>(bytes);
    if (value >= 1024.0 * 1024.0 * 1024.0) {
        return SwString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
    if (value >= 1024.0 * 1024.0) {
        return SwString::number(value / (1024.0 * 1024.0), 'f', 2) + " MB";
    }
    if (value >= 1024.0) {
        return SwString::number(value / 1024.0, 'f', 1) + " KB";
    }
    return SwString::number(bytes) + " B";
}

static SwString humanRate_(unsigned long long bytesPerSecond) {
    return humanBytes_(bytesPerSecond) + "/s";
}

static SwString percentString_(double value) {
    return SwString::number(value, 'f', value >= 10.0 ? 1 : 2) + " %";
}

static SwString runtimeSeriesLabel_(const SwRuntimeApplicationDescriptor& descriptor) {
    SwString label = descriptor.label;
    if (label.isEmpty()) {
        label = descriptor.runtimeKind.isEmpty()
                    ? ("runtime #" + SwString::number(descriptor.applicationId))
                    : (descriptor.runtimeKind + " runtime #" + SwString::number(descriptor.applicationId));
    }
    label += "  |  T" + SwString::number(descriptor.threadId);
    return label;
}

static SwRuntimeApplicationMonitorConfig runtimeMonitorConfigFromProfilerConfig_(
    const SwRuntimeProfilerConfig& profilerConfig) {
    SwRuntimeApplicationMonitorConfig config;
    config.captureIterationSamples = true;
    config.captureTimingBatches = true;
    config.captureStalls = true;
    config.profilerCaptureEnabledOnStart = false;
    config.profilerConfig = profilerConfig;
    return config;
}

static bool hasArg_(int argc, char** argv, const char* flag) {
    if (!flag) {
        return false;
    }
    const SwString flagText(flag);
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && SwString(argv[i]) == flagText) {
            return true;
        }
    }
    return false;
}

class SocketTrafficDashboardSink : public SwSocketTrafficTelemetrySink {
public:
    struct Snapshot {
        bool hasSample{false};
        SwSocketTrafficTelemetrySample sample;
    };

    void onSocketTrafficSample(const SwSocketTrafficTelemetrySample& sample) override {
        std::lock_guard<std::mutex> lock(mutex_);
        latestSample_ = sample;
        hasPending_ = true;
    }

    Snapshot consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        Snapshot snapshot;
        snapshot.hasSample = hasPending_;
        snapshot.sample = latestSample_;
        hasPending_ = false;
        return snapshot;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        hasPending_ = false;
        latestSample_ = SwSocketTrafficTelemetrySample();
    }

private:
    std::mutex mutex_;
    bool hasPending_{false};
    SwSocketTrafficTelemetrySample latestSample_;
};

class SocketTrafficRuntimeMonitorSink : public SwRuntimeApplicationMonitorSink {
public:
    struct RuntimeState {
        SwRuntimeApplicationDescriptor application{};
        bool hasIteration{false};
        SwRuntimeApplicationIterationSample iteration{};
        bool hasCounters{false};
        SwRuntimeCountersSnapshot counters{};
        unsigned long long stallCount{0};
    };

    struct Snapshot {
        SwList<RuntimeState> runtimes{};
        bool hasSelectedIteration{false};
        SwRuntimeApplicationIterationSample selectedIteration{};
        SwList<RuntimeProfilerDashboardStallEntry> newStalls{};
        unsigned long long totalStallCount{0};
    };

    void setSelectedApplicationId(unsigned long long applicationId) {
        SwMutexLocker locker(mutex_);
        selectedApplicationId_ = applicationId;
    }

    void onRuntimeApplicationAttached(const SwRuntimeApplicationDescriptor& descriptor) override {
        SwMutexLocker locker(mutex_);
        ensureRuntimeStateLocked_(descriptor);
    }

    void onRuntimeApplicationDetached(const SwRuntimeApplicationDescriptor& descriptor) override {
        SwMutexLocker locker(mutex_);
        const int index = runtimeStateIndexLocked_(descriptor.applicationId);
        if (index < 0) {
            return;
        }
        runtimeStates_.removeAt(index);
    }

    void onRuntimeApplicationIterationSample(const SwRuntimeApplicationIterationSample& sample) override {
        SwMutexLocker locker(mutex_);
        RuntimeState* state = ensureRuntimeStateLocked_(sample.application);
        state->hasIteration = true;
        state->iteration = sample;
    }

    void onRuntimeApplicationTimingBatch(const SwRuntimeApplicationTimingBatch& batch) override {
        SwMutexLocker locker(mutex_);
        RuntimeState* state = ensureRuntimeStateLocked_(batch.application);
        state->hasCounters = true;
        state->counters = batch.counters;
    }

    void onRuntimeApplicationStall(const SwRuntimeApplicationStallEvent& event) override {
        RuntimeProfilerDashboardStallEntry entry;
        entry.applicationId = event.application.applicationId;
        entry.applicationLabel = runtimeSeriesLabel_(event.application);
        entry.kind = event.stall.kind;
        entry.label = event.stall.label ? SwString(event.stall.label) : SwString();
        entry.elapsedUs = event.stall.elapsedUs;
        entry.sampleTimeNs = event.sampleTimeNs != 0 ? event.sampleTimeNs : steadyNowNs_();
        entry.lane = event.stall.lane;
        entry.threadId = event.stall.threadId;
        entry.frames = event.stall.frames;
        entry.resolvedFrames = event.stall.resolvedFrames;
        entry.symbols = event.stall.symbols;
        entry.symbolBackend = event.stall.symbolBackend;
        entry.symbolSearchPath = event.stall.symbolSearchPath;

        SwMutexLocker locker(mutex_);
        RuntimeState* state = ensureRuntimeStateLocked_(event.application);
        ++state->stallCount;
        entry.sequence = ++totalStallCount_;
        pendingStalls_.append(entry);
    }

    Snapshot consume() {
        Snapshot snapshot;
        SwMutexLocker locker(mutex_);
        snapshot.runtimes = runtimeStates_;
        snapshot.newStalls = pendingStalls_;
        snapshot.totalStallCount = totalStallCount_;
        for (size_t i = 0; i < runtimeStates_.size(); ++i) {
            if (runtimeStates_[i].application.applicationId != selectedApplicationId_) {
                continue;
            }
            if (!runtimeStates_[i].hasIteration) {
                break;
            }
            snapshot.hasSelectedIteration = true;
            snapshot.selectedIteration = runtimeStates_[i].iteration;
            break;
        }
        pendingStalls_.clear();
        return snapshot;
    }

    void reset() {
        SwMutexLocker locker(mutex_);
        pendingStalls_.clear();
        totalStallCount_ = 0;
        for (size_t i = 0; i < runtimeStates_.size(); ++i) {
            runtimeStates_[i].hasIteration = false;
            runtimeStates_[i].iteration = SwRuntimeApplicationIterationSample();
            runtimeStates_[i].hasCounters = false;
            runtimeStates_[i].counters = SwRuntimeCountersSnapshot();
            runtimeStates_[i].stallCount = 0;
        }
    }

private:
    int runtimeStateIndexLocked_(unsigned long long applicationId) const {
        for (int i = 0; i < static_cast<int>(runtimeStates_.size()); ++i) {
            if (runtimeStates_[static_cast<size_t>(i)].application.applicationId == applicationId) {
                return i;
            }
        }
        return -1;
    }

    RuntimeState* ensureRuntimeStateLocked_(const SwRuntimeApplicationDescriptor& descriptor) {
        const int existingIndex = runtimeStateIndexLocked_(descriptor.applicationId);
        if (existingIndex >= 0) {
            runtimeStates_[static_cast<size_t>(existingIndex)].application = descriptor;
            return &runtimeStates_[static_cast<size_t>(existingIndex)];
        }

        RuntimeState state;
        state.application = descriptor;
        runtimeStates_.append(state);
        return &runtimeStates_[runtimeStates_.size() - 1];
    }

    SwMutex mutex_;
    unsigned long long selectedApplicationId_{0};
    SwList<RuntimeState> runtimeStates_{};
    SwList<RuntimeProfilerDashboardStallEntry> pendingStalls_{};
    unsigned long long totalStallCount_{0};
};

class SocketTrafficDashboardWindow : public SwMainWindow {
    SW_OBJECT(SocketTrafficDashboardWindow, SwMainWindow)

public:
    SocketTrafficDashboardWindow(SwGuiApplication* app,
                                 const SwSocketTrafficTelemetryConfig& config)
        : SwMainWindow(L"Socket Traffic Monitor", 1480, 930),
          app_(app),
          telemetrySession_(new SwSocketTrafficTelemetrySession(&sink_, config)),
          monitorPeriodUs_(config.monitorPeriodUs),
          launchTimeNs_(steadyNowNs_()),
          runtimeMonitor_(&runtimeMonitorSink_, runtimeMonitorConfigFromProfilerConfig_(stallProfilerConfig_)),
          selectedRuntimeApplicationId_(app ? app->runtimeApplicationId() : 0) {
        resize(1480, 1020);
        runtimeMonitorSink_.setSelectedApplicationId(selectedRuntimeApplicationId_);
        setStyleSheet("SwMainWindow { background-color: rgb(30, 30, 30); }");
        buildUi_();
        setSampleIntervalUs_(monitorPeriodUs_);
    }

    ~SocketTrafficDashboardWindow() override {
        uninstallStallProfiling_();
        uninstallMonitoring_();
    }

    std::shared_ptr<SwSocketTrafficTelemetrySession> telemetrySession() const {
        return telemetrySession_;
    }

    bool start() {
        applyMonitoringState_(rtspPlayerWidget_ ? rtspPlayerWidget_->monitoringEnabled() : true);
        applyStallProfilingState_(rtspPlayerWidget_ ? rtspPlayerWidget_->stallProfilingEnabled() : false);
        return true;
    }

    bool saveSnapshot(const SwString& filePath,
                      bool includeMonitoring,
                      bool includeStallProfiling) {
        if (rtspPlayerWidget_) {
            rtspPlayerWidget_->setMonitoringEnabled(includeMonitoring);
            rtspPlayerWidget_->setStallProfilingEnabled(includeStallProfiling);
        }
        applyMonitoringState_(includeMonitoring);
        applyStallProfilingState_(includeStallProfiling);
        int targetHeight = 430;
        if (includeMonitoring && includeStallProfiling) {
            targetHeight = 980;
        } else if (includeMonitoring) {
            targetHeight = 820;
        } else if (includeStallProfiling) {
            targetHeight = 700;
        }
        resize(width(), targetHeight);
        if (centralWidget()) {
            centralWidget()->resize(width(), height());
        }
        refreshMonitoringLayout_();
        if (monitoringScrollArea_) {
            monitoringScrollArea_->refreshLayout();
        }
        return SwWidgetSnapshot::savePng(this, filePath);
    }

private:
    struct RuntimeLoadCursor_ {
        unsigned long long applicationId{0};
        long long lastSampleTimeNs{0};
    };

    void buildUi_() {
        SwWidget* central = centralWidget();
        central->setStyleSheet("SwWidget { background-color: rgb(30, 30, 30); border-width: 0px; }");

        rtspPlayerWidget_ = new SocketTrafficRtspPlayerWidget(central);

        monitoringScrollArea_ = new SwScrollArea(central);
        monitoringScrollArea_->setWidgetResizable(true);
        monitoringScrollArea_->setStyleSheet(
            "SwScrollArea { background-color: rgba(0,0,0,0); border-width: 0px; }");

        monitoringContent_ = new SwWidget();
        monitoringContent_->setStyleSheet(
            "SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        monitoringScrollArea_->setWidget(monitoringContent_);

        profilingView_ = new SocketTrafficProfilingView(monitoringContent_);
        profilingView_->setLaunchTimeNs(launchTimeNs_);
        stallProfilingView_ = new RuntimeProfilerProfilingView(monitoringContent_);
        stallProfilingView_->setLaunchTimeNs(launchTimeNs_);
        stallProfilingView_->setThresholdUs(stallProfilerConfig_.stallThresholdUs);
        stallProfilingView_->setMonitoringActive(false);

        summaryLabel_ = new SwLabel("Waiting for traffic samples...", monitoringContent_);
        summaryLabel_->setStyleSheet(
            "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

        monitoringLayout_ = new SwGridLayout();
        monitoringLayout_->setMargin(0);
        monitoringLayout_->setHorizontalSpacing(0);
        monitoringLayout_->setVerticalSpacing(8);
        monitoringLayout_->setColumnStretch(0, 1);
        monitoringContent_->setLayout(monitoringLayout_);

        SwVerticalLayout* rootLayout = new SwVerticalLayout();
        rootLayout->setMargin(14);
        rootLayout->setSpacing(8);
        rootLayout->addWidget(rtspPlayerWidget_, 0, 404);
        rootLayout->addWidget(monitoringScrollArea_, 1, 0);
        central->setLayout(rootLayout);
        rootLayout_ = rootLayout;

        SwObject::connect(profilingView_,
                          &SocketTrafficProfilingView::selectedConsumerChanged,
                          this,
                          [this](unsigned long long consumerId) {
                              if (currentConsumerId_ == consumerId) {
                                  return;
                              }
                              currentConsumerId_ = consumerId;
                              rebuildHistory_();
                              if (hasSample_) {
                                  profilingView_->rebuild(lastSample_, history_, currentConsumerId_, false);
                              }
                          });
        SwObject::connect(rtspPlayerWidget_,
                          &SocketTrafficRtspPlayerWidget::monitoringEnabledChanged,
                          this,
                          [this](bool enabled) {
                              applyMonitoringState_(enabled);
                          });
        SwObject::connect(rtspPlayerWidget_,
                          &SocketTrafficRtspPlayerWidget::stallProfilingEnabledChanged,
                          this,
                          [this](bool enabled) {
                              applyStallProfilingState_(enabled);
                          });
        SwObject::connect(stallProfilingView_,
                          &RuntimeProfilerProfilingView::monitoringToggleRequested,
                          this,
                          [this](bool enabled) {
                              setStallProfilerCaptureEnabled_(enabled);
                          });
        SwObject::connect(stallProfilingView_,
                          &RuntimeProfilerProfilingView::thresholdChangedUs,
                          this,
                          [this](long long thresholdUs) {
                              setStallProfilerThresholdUs_(thresholdUs);
                          });

        clearUi_();
        setMonitoringUiVisible_(rtspPlayerWidget_ ? rtspPlayerWidget_->monitoringEnabled() : true);
        clearStallProfilingUi_();
        setStallProfilingUiVisible_(rtspPlayerWidget_ ? rtspPlayerWidget_->stallProfilingEnabled() : false);
    }

    void refreshMonitoringLayout_() {
        if (!monitoringLayout_) {
            return;
        }

        if (profilingView_) {
            monitoringLayout_->removeWidget(profilingView_);
        }
        if (summaryLabel_) {
            monitoringLayout_->removeWidget(summaryLabel_);
        }
        if (stallProfilingView_) {
            monitoringLayout_->removeWidget(stallProfilingView_);
        }

        for (int row = 0; row < 4; ++row) {
            monitoringLayout_->setRowStretch(row, 0);
        }

        int row = 0;
        if (monitoringUiVisible_) {
            if (profilingView_) {
                monitoringLayout_->addWidget(profilingView_, row++, 0);
            }
            if (summaryLabel_) {
                monitoringLayout_->addWidget(summaryLabel_, row++, 0);
            }
        }
        if (stallProfilingUiVisible_ && stallProfilingView_) {
            monitoringLayout_->addWidget(stallProfilingView_, row++, 0);
        }

        monitoringLayout_->setRowStretch(row, 1);
        if (monitoringContent_ && monitoringContent_->layout()) {
            monitoringContent_->layout()->updateGeometry();
        }
    }

    void refreshMonitoringScrollVisibility_() {
        const bool anyMonitoringPanelVisible = monitoringUiVisible_ || stallProfilingUiVisible_;
        if (monitoringScrollArea_) {
            anyMonitoringPanelVisible ? monitoringScrollArea_->show() : monitoringScrollArea_->hide();
            monitoringScrollArea_->refreshLayout();
        }
        if (rootLayout_) {
            rootLayout_->setStretchFactor(rtspPlayerWidget_, anyMonitoringPanelVisible ? 0 : 1);
            rootLayout_->setStretchFactor(monitoringScrollArea_, anyMonitoringPanelVisible ? 1 : 0);
        }
    }

    void setStallProfilingUiVisible_(bool visible) {
        stallProfilingUiVisible_ = visible;

        if (stallProfilingView_) {
            visible ? stallProfilingView_->show() : stallProfilingView_->hide();
        }
        refreshMonitoringLayout_();
        refreshMonitoringScrollVisibility_();

        if (visible && stallProfilingView_) {
            const unsigned long long preferredSequence = stallProfilingView_->currentSelectedSequence();
            const bool followLatest =
                (preferredSequence == 0ULL) || stallProfilingView_->isLatestSelected();
            stallProfilingView_->setLaunchTimeNs(launchTimeNs_);
            stallProfilingView_->setThresholdUs(stallProfilerConfig_.stallThresholdUs);
            stallProfilingView_->rebuild(stallHistory_,
                                         stallLoadHistory_,
                                         preferredSequence,
                                         followLatest);
        }
        if (monitoringScrollArea_) {
            monitoringScrollArea_->refreshLayout();
        }
    }

private slots:
    void setSampleIntervalUs_(long long intervalUs) {
        monitorPeriodUs_ = intervalUs;
        if (!telemetrySession_) {
            return;
        }
        telemetrySession_->setMonitorPeriodUs(intervalUs);
        if (profilingView_) {
            profilingView_->setSampleIntervalUs(intervalUs);
        }
    }

    void setStallProfilerCaptureEnabled_(bool enabled) {
        if (enabled && !stallProfilingInstalled_) {
            if (!installStallProfiling_()) {
                if (stallProfilingView_) {
                    stallProfilingView_->setMonitoringActive(false);
                }
                return;
            }
        }
        if (enabled && stallProfilingInstalled_) {
            runtimeMonitor_.setProfilerCaptureEnabledForApplication(selectedRuntimeApplicationId_, true);
        } else if (!enabled && stallProfilingInstalled_) {
            runtimeMonitor_.setProfilerCaptureEnabledForApplication(selectedRuntimeApplicationId_, false);
        }
        if (stallProfilingView_) {
            stallProfilingView_->setMonitoringActive(enabled && runtimeMonitor_.running());
        }
    }

    void setStallProfilerThresholdUs_(long long thresholdUs) {
        const long long clampedThresholdUs = std::max(1000LL, thresholdUs);
        stallProfilerConfig_.stallThresholdUs = clampedThresholdUs;
        if (stallProfilingView_) {
            stallProfilingView_->setThresholdUs(clampedThresholdUs);
        }
        if (runtimeMonitor_.running()) {
            runtimeMonitor_.setProfilerStallThresholdUsForApplication(selectedRuntimeApplicationId_,
                                                                      clampedThresholdUs);
        }
    }

    void pullMonitorState_() {
        if (!monitoringInstalled_ && !stallProfilingInstalled_) {
            return;
        }

        const SocketTrafficRuntimeMonitorSink::Snapshot runtimeSnapshot = runtimeMonitorSink_.consume();
        updateRuntimeState_(runtimeSnapshot);

        if (!monitoringInstalled_) {
            return;
        }

        SocketTrafficDashboardSink::Snapshot snapshot = sink_.consume();
        if (!snapshot.hasSample) {
            return;
        }

        lastSample_ = snapshot.sample;
        hasSample_ = true;
        sampleArchive_.append(lastSample_);
        while (sampleArchive_.size() > kHistoryLimit_) {
            sampleArchive_.removeAt(0);
        }

        if (currentConsumerId_ == 0 && !lastSample_.consumers.isEmpty()) {
            currentConsumerId_ = lastSample_.consumers[0].consumerId;
        }

        if (monitoringUiVisible_) {
            rebuildHistory_();

            const bool followTop = profilingView_->isPinnedToTop() && profilingView_->isTopSelected();
            profilingView_->setOpenSocketCount(lastSample_.totals.openSocketCount);
            profilingView_->rebuild(lastSample_, history_, currentConsumerId_, followTop);
            currentConsumerId_ = profilingView_->currentSelectedConsumerId();
            updateSummary_();
        }
    }

    void updateStallProfilerState_(const SocketTrafficRuntimeMonitorSink::Snapshot& snapshot) {
        if (!stallProfilingInstalled_) {
            return;
        }

        for (size_t i = 0; i < snapshot.runtimes.size(); ++i) {
            const SocketTrafficRuntimeMonitorSink::RuntimeState& runtime = snapshot.runtimes[i];
            if (!runtime.hasIteration) {
                continue;
            }

            RuntimeProfilerDashboardLoadSample loadSample;
            loadSample.sampleTimeNs =
                runtime.iteration.sampleTimeNs != 0 ? runtime.iteration.sampleTimeNs : steadyNowNs_();
            if (!rememberRuntimeLoadSample_(runtime.application.applicationId, loadSample.sampleTimeNs)) {
                continue;
            }

            const SwRuntimeIterationSnapshot iterationSnapshot = runtime.iteration.snapshot;
            loadSample.applicationId = runtime.application.applicationId;
            loadSample.threadId = runtime.application.threadId;
            loadSample.seriesLabel = runtimeSeriesLabel_(runtime.application);
            loadSample.loadPercentage = iterationSnapshot.lastSecondLoadPercentage > 0.0
                                            ? iterationSnapshot.lastSecondLoadPercentage
                                            : iterationSnapshot.loadPercentage;
            stallLoadHistory_.append(loadSample);
        }
        while (static_cast<int>(stallLoadHistory_.size()) > kProfilerLoadHistoryLimit_) {
            stallLoadHistory_.removeAt(0);
        }

        if (!snapshot.newStalls.isEmpty()) {
            for (size_t i = 0; i < snapshot.newStalls.size(); ++i) {
                stallHistory_.append(snapshot.newStalls[i]);
            }
            while (static_cast<int>(stallHistory_.size()) > kProfilerChartHistoryLimit_) {
                stallHistory_.removeAt(0);
            }
        }

        if (!stallProfilingUiVisible_ || !stallProfilingView_) {
            return;
        }

        const unsigned long long preferredSequence = stallProfilingView_->currentSelectedSequence();
        const bool followLatest =
            (preferredSequence == 0ULL) || stallProfilingView_->isLatestSelected();
        stallProfilingView_->setLaunchTimeNs(launchTimeNs_);
        stallProfilingView_->setThresholdUs(stallProfilerConfig_.stallThresholdUs);
        stallProfilingView_->setObservedStallCount(snapshot.totalStallCount);
        stallProfilingView_->rebuild(stallHistory_,
                                     stallLoadHistory_,
                                     preferredSequence,
                                     followLatest);
    }

    void clearUi_() {
        sampleArchive_.clear();
        history_.clear();
        lastSample_ = SwSocketTrafficTelemetrySample();
        hasSample_ = false;
        currentConsumerId_ = 0;
        if (telemetrySession_) {
            telemetrySession_->resetBaselines();
        }
        if (profilingView_) {
            profilingView_->clearEntries();
            profilingView_->setSampleIntervalUs(monitorPeriodUs_);
            profilingView_->setMonitoringActive(monitoringInstalled_ &&
                                                telemetrySession_ &&
                                                telemetrySession_->enabled());
        }
        summaryLabel_->setText("Waiting for traffic samples...");
    }

    void clearStallProfilingUi_() {
        stallHistory_.clear();
        stallLoadHistory_.clear();
        runtimeLoadCursors_.clear();
        runtimeMonitorSink_.reset();
        if (stallProfilingView_) {
            stallProfilingView_->clearEntries();
            stallProfilingView_->setThresholdUs(stallProfilerConfig_.stallThresholdUs);
            stallProfilingView_->setObservedStallCount(0);
            stallProfilingView_->setMonitoringActive(stallProfilingInstalled_ && runtimeMonitor_.running());
        }
    }

private:
    bool rememberRuntimeLoadSample_(unsigned long long applicationId, long long sampleTimeNs) {
        if (applicationId == 0ULL || sampleTimeNs <= 0LL) {
            return false;
        }

        for (size_t i = 0; i < runtimeLoadCursors_.size(); ++i) {
            if (runtimeLoadCursors_[i].applicationId != applicationId) {
                continue;
            }
            if (sampleTimeNs <= runtimeLoadCursors_[i].lastSampleTimeNs) {
                return false;
            }
            runtimeLoadCursors_[i].lastSampleTimeNs = sampleTimeNs;
            return true;
        }

        RuntimeLoadCursor_ cursor;
        cursor.applicationId = applicationId;
        cursor.lastSampleTimeNs = sampleTimeNs;
        runtimeLoadCursors_.append(cursor);
        return true;
    }

    bool ensureRuntimeMonitorStarted_() {
        if (runtimeMonitor_.running()) {
            runtimeMonitorSink_.setSelectedApplicationId(selectedRuntimeApplicationId_);
            runtimeMonitor_.setProfilerStallThresholdUsForApplication(selectedRuntimeApplicationId_,
                                                                      stallProfilerConfig_.stallThresholdUs);
            return true;
        }
        runtimeMonitorSink_.setSelectedApplicationId(selectedRuntimeApplicationId_);
        if (!runtimeMonitor_.start()) {
            return false;
        }
        runtimeMonitor_.setProfilerStallThresholdUsForApplication(selectedRuntimeApplicationId_,
                                                                  stallProfilerConfig_.stallThresholdUs);
        return true;
    }

    void stopRuntimeMonitorIfUnused_() {
        if (monitoringInstalled_ || stallProfilingInstalled_) {
            return;
        }
        runtimeMonitor_.stop();
        runtimeMonitorSink_.reset();
    }

    bool installMonitoring_() {
        if (monitoringInstalled_) {
            return true;
        }
        if (!app_ || !telemetrySession_) {
            return false;
        }
        if (!ensureRuntimeMonitorStarted_()) {
            return false;
        }
        if (!app_->attachTelemetrySession(telemetrySession_)) {
            return false;
        }
        monitoringInstalled_ = true;
        telemetrySession_->setEnabled(true);
        telemetrySession_->setMonitorPeriodUs(monitorPeriodUs_);
        if (refreshTimerId_ == 0) {
            refreshTimerId_ = app_->addTimer([this]() { pullMonitorState_(); }, kUiRefreshPeriodUs_, false);
        }
        return true;
    }

    void uninstallMonitoring_() {
        if (app_ && refreshTimerId_ != 0) {
            app_->removeTimer(refreshTimerId_);
            refreshTimerId_ = 0;
        }
        if (app_ && monitoringInstalled_) {
            app_->detachTelemetrySession(telemetrySession_.get());
        }
        monitoringInstalled_ = false;
        stopRuntimeMonitorIfUnused_();
    }

    bool installStallProfiling_() {
        if (stallProfilingInstalled_) {
            return true;
        }
        if (!app_ || !ensureRuntimeMonitorStarted_()) {
            return false;
        }
        runtimeMonitor_.setProfilerStallThresholdUsForApplication(selectedRuntimeApplicationId_,
                                                                  stallProfilerConfig_.stallThresholdUs);
        runtimeMonitor_.setProfilerCaptureEnabledForApplication(selectedRuntimeApplicationId_, true);
        stallProfilingInstalled_ = true;
        if (refreshTimerId_ == 0) {
            refreshTimerId_ = app_->addTimer([this]() { pullMonitorState_(); }, kUiRefreshPeriodUs_, false);
        }
        return true;
    }

    void uninstallStallProfiling_() {
        if (stallProfilingInstalled_) {
            runtimeMonitor_.setProfilerCaptureEnabledForApplication(selectedRuntimeApplicationId_, false);
        }
        stallProfilingInstalled_ = false;
        if (app_ && refreshTimerId_ != 0 && !monitoringInstalled_) {
            app_->removeTimer(refreshTimerId_);
            refreshTimerId_ = 0;
        }
        stopRuntimeMonitorIfUnused_();
    }

    void applyMonitoringState_(bool enabled) {
        if (enabled) {
            if (!installMonitoring_()) {
                setMonitoringUiVisible_(false);
                clearUi_();
                return;
            }
            sink_.reset();
            clearUi_();
            setMonitoringUiVisible_(true);
            if (profilingView_) {
                profilingView_->setMonitoringActive(true);
            }
            if (rtspPlayerWidget_) {
                rtspPlayerWidget_->setMonitoringUiEnabled(true);
            }
            return;
        }

        if (profilingView_) {
            profilingView_->setMonitoringActive(false);
        }
        if (telemetrySession_) {
            telemetrySession_->setEnabled(false);
        }
        if (rtspPlayerWidget_) {
            rtspPlayerWidget_->setMonitoringUiEnabled(false);
        }
        sink_.reset();
        clearUi_();
        setMonitoringUiVisible_(false);
        uninstallMonitoring_();
    }

    void applyStallProfilingState_(bool enabled) {
        if (enabled) {
            if (!installStallProfiling_()) {
                clearStallProfilingUi_();
                setStallProfilingUiVisible_(false);
                return;
            }
            clearStallProfilingUi_();
            setStallProfilingUiVisible_(true);
            if (stallProfilingView_) {
                stallProfilingView_->setMonitoringActive(runtimeMonitor_.running());
            }
            return;
        }

        if (stallProfilingView_) {
            stallProfilingView_->setMonitoringActive(false);
        }
        uninstallStallProfiling_();
        clearStallProfilingUi_();
        setStallProfilingUiVisible_(false);
    }

    void rebuildHistory_() {
        history_.clear();
        const unsigned long long effectiveConsumerId =
            currentConsumerId_ != 0
                ? currentConsumerId_
                : (!sampleArchive_.isEmpty() && !sampleArchive_.last().consumers.isEmpty()
                       ? sampleArchive_.last().consumers[0].consumerId
                       : 0);

        for (size_t i = 0; i < sampleArchive_.size(); ++i) {
            const SwSocketTrafficTelemetrySample& sample = sampleArchive_[i];
            SocketTrafficDashboardHistoryPoint point;
            point.sampleTimeNs = sample.sampleTimeNs;
            point.totalRxRateBytesPerSecond = sample.totals.rxRateBytesPerSecond;
            point.totalTxRateBytesPerSecond = sample.totals.txRateBytesPerSecond;
            point.selectedConsumerId = effectiveConsumerId;
            for (size_t j = 0; j < sample.consumers.size(); ++j) {
                const SwSocketTrafficTelemetryConsumerSnapshot& consumer = sample.consumers[j];
                if (consumer.consumerId != effectiveConsumerId) {
                    continue;
                }
                point.selectedRxRateBytesPerSecond = consumer.rxRateBytesPerSecond;
                point.selectedTxRateBytesPerSecond = consumer.txRateBytesPerSecond;
                break;
            }
            history_.append(point);
        }
    }

    void updateSummary_() {
        if (!hasSample_) {
            return;
        }

        summaryLabel_->setText("RX " + humanRate_(lastSample_.totals.rxRateBytesPerSecond) +
                               " | runtime " +
                               (app_ ? app_->runtimeApplicationDescriptor().label : SwString("n/a")) +
                               " | TX " + humanRate_(lastSample_.totals.txRateBytesPerSecond) +
                               " | total " + humanRate_(lastSample_.totals.totalRateBytesPerSecond) +
                               " | consumers " + SwString::number(lastSample_.totals.activeConsumerCount) +
                               " | open sockets " + SwString::number(lastSample_.totals.openSocketCount) +
                               " | top consumer " + lastSample_.totals.topConsumerLabel +
                               " (" + percentString_(lastSample_.totals.topConsumerSharePercent) + ")");
    }

    void updateRuntimeState_(const SocketTrafficRuntimeMonitorSink::Snapshot& snapshot) {
        if (!rtspPlayerWidget_) {
            return;
        }

        if (snapshot.hasSelectedIteration) {
            const SwRuntimeIterationSnapshot iteration = snapshot.selectedIteration.snapshot;
            const double loadPercentage = iteration.lastSecondLoadPercentage > 0.0
                                              ? iteration.lastSecondLoadPercentage
                                              : iteration.loadPercentage;
            rtspPlayerWidget_->setThreadLoadPercentage(loadPercentage);
        }

        updateStallProfilerState_(snapshot);
    }

    void setMonitoringUiVisible_(bool visible) {
        monitoringUiVisible_ = visible;

        if (profilingView_) {
            visible ? profilingView_->show() : profilingView_->hide();
        }
        if (summaryLabel_) {
            visible ? summaryLabel_->show() : summaryLabel_->hide();
        }
        refreshMonitoringLayout_();
        refreshMonitoringScrollVisibility_();
        if (rtspPlayerWidget_) {
            rtspPlayerWidget_->setMonitoringUiEnabled(visible);
        }

        if (visible && hasSample_ && profilingView_) {
            rebuildHistory_();
            const bool followTop = profilingView_->isPinnedToTop() && profilingView_->isTopSelected();
            profilingView_->setOpenSocketCount(lastSample_.totals.openSocketCount);
            profilingView_->rebuild(lastSample_, history_, currentConsumerId_, followTop);
            currentConsumerId_ = profilingView_->currentSelectedConsumerId();
            updateSummary_();
        }
        if (monitoringScrollArea_) {
            monitoringScrollArea_->refreshLayout();
        }
    }

    SwGuiApplication* app_{nullptr};
    SocketTrafficDashboardSink sink_{};
    std::shared_ptr<SwSocketTrafficTelemetrySession> telemetrySession_;
    SocketTrafficRuntimeMonitorSink runtimeMonitorSink_{};
    long long monitorPeriodUs_{200000};
    long long launchTimeNs_{0};
    SwRuntimeProfilerConfig stallProfilerConfig_{};
    SwRuntimeApplicationMonitor runtimeMonitor_;
    unsigned long long selectedRuntimeApplicationId_{0};
    int refreshTimerId_{0};
    bool monitoringInstalled_{false};
    bool stallProfilingInstalled_{false};
    bool hasSample_{false};
    unsigned long long currentConsumerId_{0};
    SwSocketTrafficTelemetrySample lastSample_;
    SwList<SwSocketTrafficTelemetrySample> sampleArchive_;
    SwList<SocketTrafficDashboardHistoryPoint> history_;
    SwList<RuntimeProfilerDashboardStallEntry> stallHistory_;
    SwList<RuntimeProfilerDashboardLoadSample> stallLoadHistory_;
    SwList<RuntimeLoadCursor_> runtimeLoadCursors_;
    bool monitoringUiVisible_{true};
    bool stallProfilingUiVisible_{false};

    SocketTrafficRtspPlayerWidget* rtspPlayerWidget_{nullptr};
    SocketTrafficProfilingView* profilingView_{nullptr};
    RuntimeProfilerProfilingView* stallProfilingView_{nullptr};
    SwLabel* summaryLabel_{nullptr};
    SwScrollArea* monitoringScrollArea_{nullptr};
    SwWidget* monitoringContent_{nullptr};
    SwGridLayout* monitoringLayout_{nullptr};
    SwVerticalLayout* rootLayout_{nullptr};
};

} // namespace

int main(int argc, char* argv[]) {
    SwGuiApplication app;
    app.setRuntimeApplicationLabel("Socket Traffic Monitor Self-Test");

    SwSocketTrafficTelemetryConfig config;
    config.monitorPeriodUs = 200000;
    config.includeTcp = true;
    config.includeUdp = true;
    config.includeTlsAppBytes = true;

    SocketTrafficDashboardWindow window(&app, config);
    if (!window.start()) {
        return 11;
    }

    if (argc >= 3 && argv[1] && argv[2]) {
        const SwString flag(argv[1]);
        if (flag == "--snapshot" || flag == "--snapshot-stall") {
            const bool includeStallProfiling = (flag == "--snapshot-stall") || hasArg_(argc, argv, "--stall");
            const bool includeMonitoring = !hasArg_(argc, argv, "--no-network");
            return window.saveSnapshot(SwString(argv[2]),
                                       includeMonitoring,
                                       includeStallProfiling)
                       ? 0
                       : 1;
        }
    }

    window.show();

    return app.exec();
}
