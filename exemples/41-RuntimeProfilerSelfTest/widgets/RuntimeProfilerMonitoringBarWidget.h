#pragma once

#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwPushButton.h"
#include "SwSpinBox.h"

class RuntimeProfilerMonitoringBarWidget : public SwFrame {
    SW_OBJECT(RuntimeProfilerMonitoringBarWidget, SwFrame)

public:
    explicit RuntimeProfilerMonitoringBarWidget(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    void setMonitoringEnabled(bool enabled);
    void setThresholdUs(long long thresholdUs);
    void setStallCount(unsigned long long stallCount);

signals:
    DECLARE_SIGNAL(monitoringToggleRequested, bool)
    DECLARE_SIGNAL(thresholdChangedUs, long long)

private:
    void updateVisualState_();
    static int thresholdMsFromUs_(long long durationUs);
    static SwString badgeText_(unsigned long long stallCount);

    SwPushButton* toggleButton_{nullptr};
    SwSpinBox* thresholdSpinBox_{nullptr};
    SwLabel* stackCaptureLabel_{nullptr};
    SwLabel* countCaptionLabel_{nullptr};
    SwLabel* countLabel_{nullptr};
    long long thresholdUs_{10000};
    unsigned long long stallCount_{0};
    bool monitoringEnabled_{false};
    bool syncingState_{false};
    bool syncingThreshold_{false};
};
