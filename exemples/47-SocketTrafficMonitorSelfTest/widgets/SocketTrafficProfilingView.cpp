#include "SocketTrafficProfilingView.h"

#include <algorithm>
#include <chrono>

namespace {

static long long steadyNowNs_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void applySocketTrafficSplitterStyle_(SwSplitter* splitter) {
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

} // namespace

SocketTrafficProfilingView::SocketTrafficProfilingView(SwWidget* parent)
    : SwWidget(parent) {
    buildUi_();
}

SwSize SocketTrafficProfilingView::minimumSizeHint() const {
    return SwSize{680, 300};
}

void SocketTrafficProfilingView::setLaunchTimeNs(long long launchTimeNs) {
    launchTimeNs_ = launchTimeNs;
    rebuildChart_();
}

void SocketTrafficProfilingView::setSampleIntervalUs(long long intervalUs) {
    sampleIntervalUs_ = intervalUs;
    if (monitorBar_) {
        monitorBar_->setSampleIntervalUs(sampleIntervalUs_);
    }
}

void SocketTrafficProfilingView::rebuild(const SwSocketTrafficTelemetrySample& sample,
                                         const SwList<SocketTrafficDashboardHistoryPoint>& history,
                                         unsigned long long preferredConsumerId,
                                         bool followTop) {
    currentSample_ = &sample;
    history_ = &history;

    if (monitorBar_) {
        monitorBar_->setOpenSocketCount(sample.totals.openSocketCount);
    }
    if (tableWidget_) {
        tableWidget_->rebuild(sample.consumers, preferredConsumerId, followTop);
    }

    selectedConsumerId_ = tableWidget_ ? tableWidget_->currentSelectedConsumerId() : preferredConsumerId;
    rebuildChart_();
    showConsumerForId_(selectedConsumerId_);
}

void SocketTrafficProfilingView::clearEntries() {
    currentSample_ = nullptr;
    history_ = nullptr;
    selectedConsumerId_ = 0;
    if (tableWidget_) {
        tableWidget_->clearEntries();
    }
    resetSelectedConsumerView_();
    rebuildChart_();
}

unsigned long long SocketTrafficProfilingView::currentSelectedConsumerId() const {
    return tableWidget_ ? tableWidget_->currentSelectedConsumerId() : selectedConsumerId_;
}

bool SocketTrafficProfilingView::isTopSelected() const {
    return tableWidget_ ? tableWidget_->isTopSelected() : true;
}

bool SocketTrafficProfilingView::isPinnedToTop() const {
    return tableWidget_ ? tableWidget_->isPinnedToTop() : true;
}

void SocketTrafficProfilingView::setMonitoringActive(bool enabled) {
    if (monitorBar_) {
        monitorBar_->setMonitoringEnabled(enabled);
    }
}

void SocketTrafficProfilingView::setOpenSocketCount(unsigned long long openSocketCount) {
    if (monitorBar_) {
        monitorBar_->setOpenSocketCount(openSocketCount);
    }
}

void SocketTrafficProfilingView::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    ensureInitialSplitterSizes_();
}

SwFrame* SocketTrafficProfilingView::createPanel_(SwWidget* parent) {
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

void SocketTrafficProfilingView::buildUi_() {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    SwFrame* chartPanel = createPanel_(this);
    monitorBar_ = new SocketTrafficMonitoringBarWidget(chartPanel);
    monitorBar_->setSampleIntervalUs(sampleIntervalUs_);
    timelineWidget_ = new SocketTrafficConsumerTimelineWidget(chartPanel);

    SwVerticalLayout* chartLayout = new SwVerticalLayout();
    chartLayout->setMargin(2);
    chartLayout->setSpacing(2);
    chartLayout->addWidget(monitorBar_, 0, 22);
    chartLayout->addWidget(timelineWidget_, 0, 200);
    chartPanel->setLayout(chartLayout);

    detailSplitter_ = new SwSplitter(SwSplitter::Orientation::Horizontal, this);
    detailSplitter_->setHandleWidth(2);
    applySocketTrafficSplitterStyle_(detailSplitter_);

    SwFrame* listPanel = createPanel_(detailSplitter_);
    tableWidget_ = new SocketTrafficConsumerTableWidget(listPanel);
    SwVerticalLayout* listLayout = new SwVerticalLayout();
    listLayout->setMargin(2);
    listLayout->setSpacing(0);
    listLayout->addWidget(tableWidget_, 1, 260);
    listPanel->setLayout(listLayout);

    SwFrame* inspectorPanel = createPanel_(detailSplitter_);
    inspectorWidget_ = new SocketTrafficInspectorWidget(inspectorPanel);
    SwVerticalLayout* inspectorLayout = new SwVerticalLayout();
    inspectorLayout->setMargin(2);
    inspectorLayout->setSpacing(0);
    inspectorLayout->addWidget(inspectorWidget_, 1, 260);
    inspectorPanel->setLayout(inspectorLayout);

    detailSplitter_->addWidget(listPanel);
    detailSplitter_->addWidget(inspectorPanel);

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(0);
    layout->setSpacing(4);
    layout->addWidget(chartPanel, 0, 224);
    layout->addWidget(detailSplitter_, 1, 560);
    setLayout(layout);

    if (tableWidget_) {
        SwObject::connect(tableWidget_,
                          &SocketTrafficConsumerTableWidget::currentConsumerChanged,
                          this,
                          [this](unsigned long long consumerId) {
                              selectedConsumerId_ = consumerId;
                              rebuildChart_();
                              showConsumerForId_(consumerId);
                              emit selectedConsumerChanged(consumerId);
                          });
    }
}

void SocketTrafficProfilingView::ensureInitialSplitterSizes_() {
    if (splittersInitialized_ || !detailSplitter_) {
        return;
    }

    const SwRect bounds = rect();
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }

    SwVector<int> horizontalSizes;
    horizontalSizes.push_back(std::max(360, bounds.width / 2));
    horizontalSizes.push_back(std::max(420, bounds.width - horizontalSizes[0]));
    detailSplitter_->setSizes(horizontalSizes);

    splittersInitialized_ = true;
}

void SocketTrafficProfilingView::showConsumerForId_(unsigned long long consumerId) {
    if (!currentSample_ || consumerId == 0) {
        resetSelectedConsumerView_();
        return;
    }

    for (size_t i = 0; i < currentSample_->consumers.size(); ++i) {
        const SwSocketTrafficTelemetryConsumerSnapshot& consumer = currentSample_->consumers[i];
        if (consumer.consumerId != consumerId) {
            continue;
        }

        if (!inspectorWidget_) {
            return;
        }

        SocketTrafficInspectorData data;
        data.sampleTimeNs = currentSample_->sampleTimeNs;
        data.launchTimeNs = launchTimeNs_;
        data.consumer = consumer;
        inspectorWidget_->showConsumer(data);
        return;
    }

    resetSelectedConsumerView_();
}

void SocketTrafficProfilingView::resetSelectedConsumerView_() {
    if (inspectorWidget_) {
        inspectorWidget_->clearEntry();
    }
}

void SocketTrafficProfilingView::rebuildChart_() {
    if (!timelineWidget_) {
        return;
    }

    double xMin = 0.0;
    double xMax = 12.0;
    computeTimeRange_(xMin, xMax);

    double maxRate = 1024.0;
    if (history_) {
        for (size_t i = 0; i < history_->size(); ++i) {
            const SocketTrafficDashboardHistoryPoint& point = (*history_)[i];
            maxRate = std::max(maxRate, static_cast<double>(point.totalRxRateBytesPerSecond));
            maxRate = std::max(maxRate, static_cast<double>(point.totalTxRateBytesPerSecond));
            maxRate = std::max(maxRate, static_cast<double>(point.selectedRxRateBytesPerSecond));
            maxRate = std::max(maxRate, static_cast<double>(point.selectedTxRateBytesPerSecond));
        }
    }

    SwString selectedLabel;
    if (currentSample_) {
        for (size_t i = 0; i < currentSample_->consumers.size(); ++i) {
            if (currentSample_->consumers[i].consumerId == selectedConsumerId_) {
                selectedLabel = currentSample_->consumers[i].consumerLabel;
                break;
            }
        }
    }

    timelineWidget_->setLaunchTimeNs(launchTimeNs_);
    timelineWidget_->setHistory(history_);
    timelineWidget_->setSelectedConsumerLabel(selectedLabel);
    timelineWidget_->setXRange(xMin, xMax);
    timelineWidget_->setYRange(std::max(1024.0, maxRate * 1.18));
}

double SocketTrafficProfilingView::secondsSinceLaunch_(long long sampleTimeNs) const {
    if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
        return 0.0;
    }
    return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
}

void SocketTrafficProfilingView::computeTimeRange_(double& xMinOut, double& xMaxOut) const {
    const bool hasHistory = history_ && !history_->isEmpty();
    const double minSeconds = hasHistory ? secondsSinceLaunch_((*history_)[0].sampleTimeNs) : 0.0;

    double maxSeconds = secondsSinceLaunch_(steadyNowNs_());
    if (hasHistory) {
        maxSeconds = std::max(maxSeconds, secondsSinceLaunch_((*history_)[history_->size() - 1].sampleTimeNs));
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
