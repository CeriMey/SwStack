#pragma once

#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLayout.h"

class SocketTrafficMonitoringBarWidget : public SwFrame {
    SW_OBJECT(SocketTrafficMonitoringBarWidget, SwFrame)

public:
    explicit SocketTrafficMonitoringBarWidget(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    void setMonitoringEnabled(bool enabled);
    void setSampleIntervalUs(long long intervalUs);
    void setOpenSocketCount(unsigned long long socketCount);

signals:
    DECLARE_SIGNAL(monitoringToggleRequested, bool)
    DECLARE_SIGNAL(sampleIntervalChangedUs, long long)

private:
    void updateVisualState_();
    static SwString socketLabelText_(unsigned long long socketCount);

    SwWidget* statusLed_{nullptr};
    SwLabel* socketLabel_{nullptr};
    long long sampleIntervalUs_{200000};
    unsigned long long openSocketCount_{0};
    bool monitoringEnabled_{false};
};
