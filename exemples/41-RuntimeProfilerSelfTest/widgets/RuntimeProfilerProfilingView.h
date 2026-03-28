#pragma once

#include "SwFrame.h"
#include "SwLayout.h"
#include "SwSplitter.h"
#include "SwWidget.h"
#include "RuntimeProfilerMonitoringBarWidget.h"
#include "RuntimeProfilerStackInspectorWidget.h"
#include "RuntimeProfilerStallTableWidget.h"
#include "RuntimeProfilerStallTimelineWidget.h"

class RuntimeProfilerProfilingView : public SwWidget {
    SW_OBJECT(RuntimeProfilerProfilingView, SwWidget)

public:
    explicit RuntimeProfilerProfilingView(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    void setLaunchTimeNs(long long launchTimeNs);
    void setThresholdUs(long long thresholdUs);
    void rebuild(const SwList<RuntimeProfilerDashboardStallEntry>& stalls,
                 const SwList<RuntimeProfilerDashboardLoadSample>& loadSamples,
                 unsigned long long preferredSequence,
                 bool followLatest);
    void clearEntries();
    unsigned long long currentSelectedSequence() const;
    bool isLatestSelected() const;
    void setMonitoringActive(bool enabled);
    void setObservedStallCount(unsigned long long stallCount);

signals:
    DECLARE_SIGNAL(monitoringToggleRequested, bool)
    DECLARE_SIGNAL(thresholdChangedUs, long long)

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    SwFrame* createPanel_(SwWidget* parent);
    void buildUi_();
    void ensureInitialSplitterSizes_();
    void showEntryForSequence_(unsigned long long sequence);
    void resetSelectedStallView_();
    void rebuildChart_();
    double secondsSinceLaunch_(long long sampleTimeNs) const;
    void computeTimeRange_(double& xMinOut, double& xMaxOut) const;

    long long launchTimeNs_{0};
    long long thresholdUs_{10000};
    bool splittersInitialized_{false};

    const SwList<RuntimeProfilerDashboardStallEntry>* stalls_{nullptr};
    const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples_{nullptr};

    SwSplitter* mainSplitter_{nullptr};
    SwSplitter* detailSplitter_{nullptr};
    RuntimeProfilerMonitoringBarWidget* monitorBar_{nullptr};
    RuntimeProfilerStallTimelineWidget* timelineWidget_{nullptr};
    RuntimeProfilerStallTableWidget* tableWidget_{nullptr};
    RuntimeProfilerStackInspectorWidget* stackWidget_{nullptr};
    unsigned long long renderedStallCount_{0};
    unsigned long long renderedLastSequence_{0};
};
