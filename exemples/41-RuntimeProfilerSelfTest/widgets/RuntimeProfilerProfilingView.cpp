#include "RuntimeProfilerProfilingView.h"

#include <algorithm>
#include <chrono>

namespace {

static long long steadyNowNs_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void applyProfilerSplitterStyle_(SwSplitter* splitter) {
    if (!splitter) {
        return;
    }

    splitter->setStyleSheet(R"(
        SwSplitter {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            handle-color: rgb(58, 58, 62);
            handle-color-hover: rgb(0, 122, 204);
            handle-color-pressed: rgb(55, 148, 255);
            handle-border-color: rgb(58, 58, 62);
            handle-border-color-hover: rgb(0, 122, 204);
            handle-border-color-pressed: rgb(55, 148, 255);
            grip-color: rgba(0,0,0,0);
            grip-color-hover: rgba(0,0,0,0);
            grip-color-pressed: rgba(0,0,0,0);
            handle-visual-width: 1px;
            handle-border-width: 0px;
            handle-radius: 0px;
        }
    )");
}

static SwString compactLoadSeriesLabel_(const SwString& seriesLabel) {
    if (seriesLabel.isEmpty()) {
        return "runtime";
    }

    const SwList<SwString> parts = seriesLabel.split('|');
    if (parts.size() >= 2) {
        const SwString leftPart = parts[0].trimmed();
        const SwString rightPart = parts[parts.size() - 1].trimmed();
        SwString compactLeft = leftPart;
        if (compactLeft.size() > 18) {
            compactLeft = compactLeft.left(18) + "...";
        }
        if (!compactLeft.isEmpty() && !rightPart.isEmpty()) {
            return compactLeft + " " + rightPart;
        }
        if (!rightPart.isEmpty()) {
            return rightPart;
        }
    }

    if (seriesLabel.size() > 22) {
        return seriesLabel.left(22) + "...";
    }
    return seriesLabel;
}

static SwString loadSummaryText_(const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples) {
    if (!loadSamples || loadSamples->isEmpty()) {
        return "Charge: --";
    }

    SwList<RuntimeProfilerDashboardLoadSample> latestBySeries;
    for (int i = static_cast<int>(loadSamples->size()) - 1; i >= 0; --i) {
        const RuntimeProfilerDashboardLoadSample& sample = (*loadSamples)[static_cast<size_t>(i)];
        bool found = false;
        for (size_t j = 0; j < latestBySeries.size(); ++j) {
            if (latestBySeries[j].applicationId == sample.applicationId &&
                latestBySeries[j].threadId == sample.threadId) {
                found = true;
                break;
            }
        }
        if (!found) {
            latestBySeries.append(sample);
        }
    }

    SwString summary = "Charge: ";
    const size_t visibleCount = std::min<size_t>(latestBySeries.size(), 2);
    for (size_t i = 0; i < visibleCount; ++i) {
        if (i > 0) {
            summary += "  |  ";
        }
        const RuntimeProfilerDashboardLoadSample& sample = latestBySeries[i];
        summary += compactLoadSeriesLabel_(sample.seriesLabel);
        summary += " ";
        summary += SwString::number(sample.loadPercentage, 'f', sample.loadPercentage >= 10.0 ? 1 : 2);
        summary += "%";
    }
    if (latestBySeries.size() > visibleCount) {
        summary += "  |  +";
        summary += SwString::number(static_cast<unsigned long long>(latestBySeries.size() - visibleCount));
    }
    return summary;
}

} // namespace

RuntimeProfilerProfilingView::RuntimeProfilerProfilingView(SwWidget* parent)
    : SwWidget(parent) {
    buildUi_();
}

SwSize RuntimeProfilerProfilingView::minimumSizeHint() const {
    return SwSize{620, 420};
}

void RuntimeProfilerProfilingView::setLaunchTimeNs(long long launchTimeNs) {
    launchTimeNs_ = launchTimeNs;
    if (tableWidget_) {
        tableWidget_->setLaunchTimeNs(launchTimeNs_);
    }
    rebuildChart_();
}

void RuntimeProfilerProfilingView::setThresholdUs(long long thresholdUs) {
    thresholdUs_ = thresholdUs;
    if (tableWidget_) {
        tableWidget_->setThresholdUs(thresholdUs_);
    }
    if (monitorBar_) {
        monitorBar_->setThresholdUs(thresholdUs_);
    }
    rebuildChart_();
    showEntryForSequence_(currentSelectedSequence());
}

void RuntimeProfilerProfilingView::rebuild(const SwList<RuntimeProfilerDashboardStallEntry>& stalls,
                                           const SwList<RuntimeProfilerDashboardLoadSample>& loadSamples,
                                           unsigned long long preferredSequence,
                                           bool followLatest) {
    stalls_ = &stalls;
    loadSamples_ = &loadSamples;

    const unsigned long long lastSequence = stalls.isEmpty() ? 0 : stalls[stalls.size() - 1].sequence;
    const bool stallsChanged = (renderedStallCount_ != static_cast<unsigned long long>(stalls.size())) ||
                               (renderedLastSequence_ != lastSequence);

    if (tableWidget_ && stallsChanged) {
        tableWidget_->setLaunchTimeNs(launchTimeNs_);
        tableWidget_->setThresholdUs(thresholdUs_);
        tableWidget_->rebuild(stalls, preferredSequence, followLatest);
    }

    renderedStallCount_ = static_cast<unsigned long long>(stalls.size());
    renderedLastSequence_ = lastSequence;

    rebuildChart_();

    if (!stalls_ || stalls_->isEmpty()) {
        resetSelectedStallView_();
        return;
    }

    showEntryForSequence_(currentSelectedSequence());
}

void RuntimeProfilerProfilingView::clearEntries() {
    stalls_ = nullptr;
    loadSamples_ = nullptr;
    renderedStallCount_ = 0;
    renderedLastSequence_ = 0;
    if (tableWidget_) {
        tableWidget_->clearEntries();
    }
    if (monitorBar_) {
        monitorBar_->setLoadSummary("Charge: --");
    }
    resetSelectedStallView_();
    rebuildChart_();
}

unsigned long long RuntimeProfilerProfilingView::currentSelectedSequence() const {
    return tableWidget_ ? tableWidget_->currentSelectedSequence() : 0;
}

bool RuntimeProfilerProfilingView::isLatestSelected() const {
    const bool listPinned = !tableWidget_ || (tableWidget_->isLatestSelected() && tableWidget_->isPinnedToTop());
    const bool stackPinned = !stackWidget_ || stackWidget_->isPinnedToTop();
    return listPinned && stackPinned;
}

void RuntimeProfilerProfilingView::setMonitoringActive(bool enabled) {
    if (monitorBar_) {
        monitorBar_->setMonitoringEnabled(enabled);
    }
}

void RuntimeProfilerProfilingView::setObservedStallCount(unsigned long long stallCount) {
    if (monitorBar_) {
        monitorBar_->setStallCount(stallCount);
    }
}

void RuntimeProfilerProfilingView::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    ensureInitialSplitterSizes_();
}

SwFrame* RuntimeProfilerProfilingView::createPanel_(SwWidget* parent) {
    SwFrame* panel = new SwFrame(parent);
    panel->setStyleSheet(R"(
        SwFrame {
            background-color: rgb(37, 37, 38);
            border-color: rgb(62, 62, 66);
            border-width: 1px;
            border-radius: 10px;
        }
    )");
    return panel;
}

void RuntimeProfilerProfilingView::buildUi_() {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    SwFrame* chartPanel = createPanel_(this);
    chartPanel->setMinimumSize(0, 246);
    monitorBar_ = new RuntimeProfilerMonitoringBarWidget(chartPanel);
    monitorBar_->setThresholdUs(thresholdUs_);
    timelineWidget_ = new RuntimeProfilerStallTimelineWidget(chartPanel);
    timelineWidget_->setMinimumSize(0, 200);

    SwVerticalLayout* chartLayout = new SwVerticalLayout();
    chartLayout->setMargin(2);
    chartLayout->setSpacing(4);
    chartLayout->addWidget(monitorBar_, 0, 34);
    chartLayout->addWidget(timelineWidget_, 0, 200);
    chartPanel->setLayout(chartLayout);

    detailSplitter_ = new SwSplitter(SwSplitter::Orientation::Horizontal, this);
    detailSplitter_->setHandleWidth(2);
    applyProfilerSplitterStyle_(detailSplitter_);

    SwFrame* listPanel = createPanel_(detailSplitter_);
    tableWidget_ = new RuntimeProfilerStallTableWidget(listPanel);

    SwVerticalLayout* listLayout = new SwVerticalLayout();
    listLayout->setMargin(2);
    listLayout->setSpacing(0);
    listLayout->addWidget(tableWidget_, 1, 240);
    listPanel->setLayout(listLayout);

    SwFrame* stackPanel = createPanel_(detailSplitter_);
    stackWidget_ = new RuntimeProfilerStackInspectorWidget(stackPanel);

    SwVerticalLayout* stackLayout = new SwVerticalLayout();
    stackLayout->setMargin(2);
    stackLayout->setSpacing(0);
    stackLayout->addWidget(stackWidget_, 1, 240);
    stackPanel->setLayout(stackLayout);

    detailSplitter_->addWidget(listPanel);
    detailSplitter_->addWidget(stackPanel);

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(0);
    layout->setSpacing(8);
    layout->addWidget(chartPanel, 0, 246);
    layout->addWidget(detailSplitter_, 1, 540);
    setLayout(layout);

    if (tableWidget_) {
        SwObject::connect(tableWidget_, &RuntimeProfilerStallTableWidget::currentSequenceChanged, this,
                          [this](unsigned long long sequence) {
                              showEntryForSequence_(sequence);
                          });
    }
    if (monitorBar_) {
        SwObject::connect(monitorBar_, &RuntimeProfilerMonitoringBarWidget::monitoringToggleRequested, this,
                          [this](bool enabled) {
                              emit monitoringToggleRequested(enabled);
                          });
        SwObject::connect(monitorBar_, &RuntimeProfilerMonitoringBarWidget::thresholdChangedUs, this,
                          [this](long long thresholdUs) {
                              emit thresholdChangedUs(thresholdUs);
                          });
    }
}

void RuntimeProfilerProfilingView::ensureInitialSplitterSizes_() {
    if (splittersInitialized_ || !detailSplitter_) {
        return;
    }

    const SwRect bounds = rect();
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }

    SwVector<int> horizontalSizes;
    horizontalSizes.push_back(std::max(340, bounds.width / 3));
    horizontalSizes.push_back(std::max(440, bounds.width - horizontalSizes[0]));
    detailSplitter_->setSizes(horizontalSizes);

    splittersInitialized_ = true;
}

void RuntimeProfilerProfilingView::showEntryForSequence_(unsigned long long sequence) {
    if (sequence == 0 || !stalls_) {
        resetSelectedStallView_();
        return;
    }

    for (size_t i = 0; i < stalls_->size(); ++i) {
        const RuntimeProfilerDashboardStallEntry& entry = (*stalls_)[i];
        if (entry.sequence != sequence) {
            continue;
        }

        if (!stackWidget_) {
            return;
        }

        RuntimeProfilerStackInspectorData data;
        data.sequence = entry.sequence;
        data.applicationId = entry.applicationId;
        data.applicationLabel = entry.applicationLabel;
        data.kind = entry.kind;
        data.label = entry.label;
        data.elapsedUs = entry.elapsedUs;
        data.thresholdUs = thresholdUs_;
        data.sampleTimeNs = entry.sampleTimeNs;
        data.launchTimeNs = launchTimeNs_;
        data.lane = entry.lane;
        data.threadId = entry.threadId;
        data.frames = entry.frames;
        data.resolvedFrames = entry.resolvedFrames;
        data.symbols = entry.symbols;
        data.symbolBackend = entry.symbolBackend;
        data.symbolSearchPath = entry.symbolSearchPath;
        stackWidget_->showEntry(data);
        return;
    }

    resetSelectedStallView_();
}

void RuntimeProfilerProfilingView::resetSelectedStallView_() {
    if (stackWidget_) {
        stackWidget_->clearEntry();
    }
}

void RuntimeProfilerProfilingView::rebuildChart_() {
    if (!timelineWidget_) {
        return;
    }

    if (monitorBar_) {
        monitorBar_->setLoadSummary(loadSummaryText_(loadSamples_));
    }

    double xMin = 0.0;
    double xMax = 12.0;
    computeTimeRange_(xMin, xMax);

    const double thresholdMs = static_cast<double>(thresholdUs_) / 1000.0;
    double maxDurationMs = std::max(1.0, thresholdMs * 1.3);
    if (stalls_) {
        for (size_t i = 0; i < stalls_->size(); ++i) {
            const double durationMs = static_cast<double>((*stalls_)[i].elapsedUs) / 1000.0;
            maxDurationMs = std::max(maxDurationMs, durationMs);
        }
    }

    double maxLoad = 20.0;
    if (loadSamples_) {
        for (size_t i = 0; i < loadSamples_->size(); ++i) {
            const double load = std::max(0.0, (*loadSamples_)[i].loadPercentage);
            maxLoad = std::max(maxLoad, load);
        }
    }

    timelineWidget_->setLaunchTimeNs(launchTimeNs_);
    timelineWidget_->setStallThresholdMs(thresholdMs);
    timelineWidget_->setXRange(xMin, xMax);
    timelineWidget_->setYRange(std::max(maxDurationMs * 1.18, thresholdMs * 1.45));
    timelineWidget_->setStalls(stalls_);
    timelineWidget_->setLoadSamples(loadSamples_);
    timelineWidget_->setLoadRange(std::min(100.0, std::max(25.0, maxLoad * 1.18)));
}

double RuntimeProfilerProfilingView::secondsSinceLaunch_(long long sampleTimeNs) const {
    if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
        return 0.0;
    }
    return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
}

void RuntimeProfilerProfilingView::computeTimeRange_(double& xMinOut, double& xMaxOut) const {
    const bool hasStalls = stalls_ && !stalls_->isEmpty();
    const bool hasLoad = loadSamples_ && !loadSamples_->isEmpty();

    const double oldestStallSeconds = hasStalls
                                          ? secondsSinceLaunch_((*stalls_)[0].sampleTimeNs)
                                          : 0.0;
    const double oldestLoadSeconds = hasLoad
                                         ? secondsSinceLaunch_((*loadSamples_)[0].sampleTimeNs)
                                         : 0.0;

    double minSeconds = 0.0;
    if (hasStalls && hasLoad) {
        minSeconds = std::max(oldestStallSeconds, oldestLoadSeconds);
    } else if (hasStalls) {
        minSeconds = oldestStallSeconds;
    } else if (hasLoad) {
        minSeconds = oldestLoadSeconds;
    }

    double maxSeconds = secondsSinceLaunch_(steadyNowNs_());
    if (hasStalls) {
        maxSeconds = std::max(maxSeconds,
                              secondsSinceLaunch_((*stalls_)[stalls_->size() - 1].sampleTimeNs));
    }
    if (hasLoad) {
        maxSeconds = std::max(maxSeconds,
                              secondsSinceLaunch_((*loadSamples_)[loadSamples_->size() - 1].sampleTimeNs));
    }

    if (maxSeconds < minSeconds) {
        maxSeconds = minSeconds;
    }

    if ((maxSeconds - minSeconds) < 10.0) {
        const double targetSpan = 10.0;
        xMinOut = std::max(0.0, maxSeconds - targetSpan);
        xMaxOut = std::max(targetSpan, maxSeconds);
        return;
    }

    xMinOut = minSeconds;
    xMaxOut = maxSeconds;
}
