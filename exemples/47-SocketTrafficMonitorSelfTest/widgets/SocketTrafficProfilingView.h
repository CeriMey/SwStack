#pragma once

#include "SwFrame.h"
#include "SwLayout.h"
#include "SwSplitter.h"
#include "SwWidget.h"
#include "SocketTrafficMonitoringBarWidget.h"
#include "SocketTrafficInspectorWidget.h"
#include "SocketTrafficConsumerTableWidget.h"
#include "SocketTrafficConsumerTimelineWidget.h"

class SocketTrafficProfilingView : public SwWidget {
    SW_OBJECT(SocketTrafficProfilingView, SwWidget)

public:
    explicit SocketTrafficProfilingView(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    void setLaunchTimeNs(long long launchTimeNs);
    void setSampleIntervalUs(long long intervalUs);
    void rebuild(const SwSocketTrafficTelemetrySample& sample,
                 const SwList<SocketTrafficDashboardHistoryPoint>& history,
                 unsigned long long preferredConsumerId,
                 bool followTop);
    void clearEntries();
    unsigned long long currentSelectedConsumerId() const;
    bool isTopSelected() const;
    bool isPinnedToTop() const;
    void setMonitoringActive(bool enabled);
    void setOpenSocketCount(unsigned long long openSocketCount);

signals:
    DECLARE_SIGNAL(monitoringToggleRequested, bool)
    DECLARE_SIGNAL(sampleIntervalChangedUs, long long)
    DECLARE_SIGNAL(selectedConsumerChanged, unsigned long long)

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    SwFrame* createPanel_(SwWidget* parent);
    void buildUi_();
    void ensureInitialSplitterSizes_();
    void showConsumerForId_(unsigned long long consumerId);
    void resetSelectedConsumerView_();
    void rebuildChart_();
    double secondsSinceLaunch_(long long sampleTimeNs) const;
    void computeTimeRange_(double& xMinOut, double& xMaxOut) const;

    long long launchTimeNs_{0};
    long long sampleIntervalUs_{200000};
    bool splittersInitialized_{false};

    const SwSocketTrafficTelemetrySample* currentSample_{nullptr};
    const SwList<SocketTrafficDashboardHistoryPoint>* history_{nullptr};

    SwSplitter* detailSplitter_{nullptr};
    SocketTrafficMonitoringBarWidget* monitorBar_{nullptr};
    SocketTrafficConsumerTimelineWidget* timelineWidget_{nullptr};
    SocketTrafficConsumerTableWidget* tableWidget_{nullptr};
    SocketTrafficInspectorWidget* inspectorWidget_{nullptr};
    unsigned long long selectedConsumerId_{0};
};
