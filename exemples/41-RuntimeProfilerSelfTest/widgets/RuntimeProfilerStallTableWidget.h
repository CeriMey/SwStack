#pragma once

#include "SwStandardItemModel.h"
#include "SwTableView.h"
#include "SwWidget.h"
#include "RuntimeProfilerViewTypes.h"

class RuntimeProfilerStallTableWidget : public SwWidget {
    SW_OBJECT(RuntimeProfilerStallTableWidget, SwWidget)

public:
    explicit RuntimeProfilerStallTableWidget(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    void setLaunchTimeNs(long long launchTimeNs);
    void setThresholdUs(long long thresholdUs);
    void rebuild(const SwList<RuntimeProfilerDashboardStallEntry>& entries,
                 unsigned long long preferredSequence,
                 bool followLatest);
    void clearEntries();
    unsigned long long currentSelectedSequence() const;
    bool isLatestSelected() const;
    bool isPinnedToTop() const;

signals:
    DECLARE_SIGNAL(currentSequenceChanged, unsigned long long)

private:
    void resetModelColumns_();
    int rowForSequence_(unsigned long long sequence) const;
    void selectRow_(int row);
    void restoreViewport_(unsigned long long anchorSequence, int rowOffsetWithinAnchor, bool followLatest);
    double secondsSinceLaunch_(long long sampleTimeNs) const;

    long long launchTimeNs_{0};
    long long thresholdUs_{0};
    SwTableView* tableView_{nullptr};
    SwStandardItemModel* tableModel_{nullptr};
    SwList<unsigned long long> sequences_;
};
