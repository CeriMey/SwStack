#pragma once

#include "SwCheckBox.h"
#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwLineEdit.h"
#include "SwPushButton.h"
#include "SwVideoWidget.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwMediaPlayer.h"

class SocketTrafficRtspPlayerWidget : public SwFrame {
    SW_OBJECT(SocketTrafficRtspPlayerWidget, SwFrame)

public:
    explicit SocketTrafficRtspPlayerWidget(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    bool monitoringEnabled() const;
    bool stallProfilingEnabled() const;
    void setMonitoringEnabled(bool enabled);
    void setStallProfilingEnabled(bool enabled);
    void setThreadLoadPercentage(double loadPercentage);
    void setMonitoringUiEnabled(bool enabled);
    void openCurrentUrl();
    void stopPlayback();

signals:
    DECLARE_SIGNAL(monitoringEnabledChanged, bool)
    DECLARE_SIGNAL(stallProfilingEnabledChanged, bool)

private:
    static SwString defaultRtspUrl_();

    void buildUi_();
    void rebuildPlayer_();
    void configureLiveVideoPipeline_(const SwMediaOpenOptions& openOptions);
    void refreshStatusText_();
    SwString statusText_() const;
    void applyAudioPreference_(bool enabled);

    SwLineEdit* urlEdit_{nullptr};
    SwCheckBox* monitoringCheck_{nullptr};
    SwCheckBox* stallProfilingCheck_{nullptr};
    SwCheckBox* audioCheck_{nullptr};
    SwPushButton* openButton_{nullptr};
    SwPushButton* stopButton_{nullptr};
    SwLabel* statusLabel_{nullptr};
    SwLabel* threadLoadLabel_{nullptr};
    SwVideoWidget* videoWidget_{nullptr};
    std::shared_ptr<SwMediaPlayer> player_;
    SwString openedUrl_;
    SwString lastThreadLoadText_;
};
