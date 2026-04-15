#include "SocketTrafficRtspPlayerWidget.h"

#include "media/SwMediaOpenOptions.h"
#include "media/SwMediaSourceFactory.h"
#include "media/SwPlatformVideoDecoderIds.h"
#include "runtime/SwTimer.h"

#include <cstdlib>

namespace {

static SwString playbackStateLabel_(SwMediaPlayer::PlaybackState state) {
    switch (state) {
    case SwMediaPlayer::PlaybackState::PlayingState:
        return "Playing";
    case SwMediaPlayer::PlaybackState::PausedState:
        return "Paused";
    case SwMediaPlayer::PlaybackState::StoppedState:
    default:
        return "Stopped";
    }
}

static SwString actionButtonStyle_(const SwString& color) {
    return "SwPushButton { background-color: rgb(30, 30, 30); color: " + color +
           "; border-color: rgb(62, 62, 66); border-width: 1px; border-radius: 10px; padding: 10px 12px; font-size: 12px; }";
}

static SwString threadLoadText_(double loadPercentage) {
    return "Runtime " + SwString::number(loadPercentage, 'f', loadPercentage >= 10.0 ? 1 : 2) + " %";
}

static bool shouldAutoOpenRtsp_() {
    std::string enabled;
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, "SW_RTSP_AUTOSTART") != 0 || !value) {
        return false;
    }
    enabled.assign(value, valueSize > 0 ? valueSize - 1 : 0);
    std::free(value);
#else
    const char* value = std::getenv("SW_RTSP_AUTOSTART");
    if (!value) {
        return false;
    }
    enabled = value;
#endif
    return enabled == "1" || enabled == "true" || enabled == "TRUE" || enabled == "yes" || enabled == "YES";
}

static SwString configuredRtspUrl_() {
    std::string url;
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, "SW_RTSP_URL") != 0 || !value) {
        return "rtsp://127.0.0.1:8554/test";
    }
    url.assign(value, valueSize > 0 ? valueSize - 1 : 0);
    std::free(value);
#else
    const char* value = std::getenv("SW_RTSP_URL");
    if (!value || !*value) {
        return "rtsp://127.0.0.1:8554/test";
    }
    url = value;
#endif
    return url.empty() ? SwString("rtsp://127.0.0.1:8554/test") : SwString(url);
}

static SwString configuredDecoderId_() {
    std::string decoderId;
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, "SW_RTSP_DECODER_ID") != 0 || !value) {
        return SwString();
    }
    decoderId.assign(value, valueSize > 0 ? valueSize - 1 : 0);
    std::free(value);
#else
    const char* value = std::getenv("SW_RTSP_DECODER_ID");
    if (!value || !*value) {
        return SwString();
    }
    decoderId = value;
#endif
    return decoderId.empty() ? SwString() : SwString(decoderId);
}

} // namespace

SocketTrafficRtspPlayerWidget::SocketTrafficRtspPlayerWidget(SwWidget* parent)
    : SwFrame(parent) {
    buildUi_();
    rebuildPlayer_();
    refreshStatusText_();
    if (shouldAutoOpenRtsp_()) {
        SwTimer::singleShot(0, [this]() { openCurrentUrl(); });
    }
}

SwSize SocketTrafficRtspPlayerWidget::minimumSizeHint() const {
    return {1040, 404};
}

bool SocketTrafficRtspPlayerWidget::monitoringEnabled() const {
    return !monitoringCheck_ || monitoringCheck_->isChecked();
}

bool SocketTrafficRtspPlayerWidget::stallProfilingEnabled() const {
    return stallProfilingCheck_ && stallProfilingCheck_->isChecked();
}

void SocketTrafficRtspPlayerWidget::setMonitoringEnabled(bool enabled) {
    if (!monitoringCheck_ || monitoringCheck_->isChecked() == enabled) {
        return;
    }
    monitoringCheck_->setChecked(enabled);
}

void SocketTrafficRtspPlayerWidget::setStallProfilingEnabled(bool enabled) {
    if (!stallProfilingCheck_ || stallProfilingCheck_->isChecked() == enabled) {
        return;
    }
    stallProfilingCheck_->setChecked(enabled);
}

void SocketTrafficRtspPlayerWidget::setThreadLoadPercentage(double loadPercentage) {
    const SwString nextText = threadLoadText_(loadPercentage);
    if (lastThreadLoadText_ == nextText) {
        return;
    }

    lastThreadLoadText_ = nextText;
    if (threadLoadLabel_) {
        threadLoadLabel_->setText(lastThreadLoadText_);
    }
}

void SocketTrafficRtspPlayerWidget::setMonitoringUiEnabled(bool enabled) {
    if (threadLoadLabel_) {
        if (!enabled) {
            lastThreadLoadText_.clear();
            threadLoadLabel_->setText("Runtime --");
            threadLoadLabel_->hide();
        } else {
            if (lastThreadLoadText_.isEmpty()) {
                threadLoadLabel_->setText("Runtime --");
            }
            threadLoadLabel_->show();
        }
    }
}

void SocketTrafficRtspPlayerWidget::openCurrentUrl() {
    const SwString url = urlEdit_ ? urlEdit_->getText().trimmed() : SwString();
    if (url.isEmpty()) {
        if (statusLabel_) {
            statusLabel_->setText("RTSP URL vide.");
        }
        return;
    }

    SwMediaOpenOptions openOptions = SwMediaOpenOptions::fromUrl(url);
    openOptions.enableAudio = audioCheck_ ? audioCheck_->isChecked() : openOptions.enableAudio;

    const std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(openOptions);
    if (!source) {
        if (statusLabel_) {
            statusLabel_->setText("Source RTSP non supportee. Lecture courante conservee.");
        }
        return;
    }

    rebuildPlayer_();
    player_->setSource(source);
    configureLiveVideoPipeline_(openOptions);
    player_->setAudioEnabled(openOptions.enableAudio);
    player_->setMetadataEnabled(openOptions.enableMetadata);
    player_->play();

    openedUrl_ = url;
    refreshStatusText_();
}

void SocketTrafficRtspPlayerWidget::stopPlayback() {
    openedUrl_.clear();
    rebuildPlayer_();
    refreshStatusText_();
}

SwString SocketTrafficRtspPlayerWidget::defaultRtspUrl_() {
    return configuredRtspUrl_();
}

void SocketTrafficRtspPlayerWidget::buildUi_() {
    setStyleSheet(
        "SocketTrafficRtspPlayerWidget { background-color: rgb(37, 37, 38); border-color: rgb(62, 62, 66); border-width: 1px; border-radius: 16px; }");

    SwLabel* urlLabel = new SwLabel("Flux", this);
    urlLabel->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(210, 210, 210); font-size: 12px; }");

    urlEdit_ = new SwLineEdit(this);
    urlEdit_->setText(defaultRtspUrl_());
    urlEdit_->setMinimumSize(620, 38);
    urlEdit_->setStyleSheet(
        "SwLineEdit { background-color: rgb(30, 30, 30); border-color: rgb(62, 62, 66); border-width: 1px; border-radius: 10px; padding: 8px 12px; color: rgb(228, 228, 228); }");

    monitoringCheck_ = new SwCheckBox("Monitoring", this);
    monitoringCheck_->setChecked(true);
    monitoringCheck_->setAccentColor({97, 218, 251});
    monitoringCheck_->setStyleSheet(
        "SwCheckBox { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(210, 210, 210); font-size: 12px; }");

    stallProfilingCheck_ = new SwCheckBox("Stall profiling", this);
    stallProfilingCheck_->setChecked(false);
    stallProfilingCheck_->setAccentColor({206, 145, 120});
    stallProfilingCheck_->setStyleSheet(
        "SwCheckBox { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(210, 210, 210); font-size: 12px; }");

    audioCheck_ = new SwCheckBox("Audio", this);
    audioCheck_->setChecked(SwMediaOpenOptions::fromUrl(defaultRtspUrl_()).enableAudio);
    audioCheck_->setAccentColor({78, 201, 176});
    audioCheck_->setStyleSheet(
        "SwCheckBox { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(210, 210, 210); font-size: 12px; }");

    openButton_ = new SwPushButton("Open", this);
    openButton_->setStyleSheet(actionButtonStyle_("rgb(97, 218, 251)"));
    openButton_->setMinimumSize(100, 38);

    stopButton_ = new SwPushButton("Stop", this);
    stopButton_->setStyleSheet(actionButtonStyle_("rgb(204, 204, 204)"));
    stopButton_->setMinimumSize(100, 38);

    statusLabel_ = new SwLabel(this);
    statusLabel_->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

    threadLoadLabel_ = new SwLabel("Runtime --", this);
    threadLoadLabel_->setMinimumSize(128, 18);
    threadLoadLabel_->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(78, 201, 176); font-size: 12px; }");

    videoWidget_ = new SwVideoWidget(this);
    videoWidget_->setScalingMode(SwVideoWidget::ScalingMode::Fit);
    videoWidget_->setBackgroundColor({8, 8, 8});
    videoWidget_->setMinimumSize(0, 340);

    SwWidget* controlsRow = new SwWidget(this);
    controlsRow->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    SwWidget* statusRow = new SwWidget(this);
    statusRow->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    SwHorizontalLayout* controlsLayout = new SwHorizontalLayout();
    controlsLayout->setMargin(0);
    controlsLayout->setSpacing(10);
    controlsLayout->addWidget(urlLabel, 0, 34);
    controlsLayout->addWidget(urlEdit_, 1, 0);
    controlsLayout->addWidget(monitoringCheck_, 0, 118);
    controlsLayout->addWidget(stallProfilingCheck_, 0, 140);
    controlsLayout->addWidget(audioCheck_, 0, 90);
    controlsLayout->addWidget(openButton_, 0, 100);
    controlsLayout->addWidget(stopButton_, 0, 100);
    controlsRow->setLayout(controlsLayout);

    SwHorizontalLayout* statusLayout = new SwHorizontalLayout();
    statusLayout->setMargin(0);
    statusLayout->setSpacing(10);
    statusLayout->addWidget(statusLabel_, 1, 0);
    statusLayout->addWidget(threadLoadLabel_, 0, 136);
    statusRow->setLayout(statusLayout);

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(12);
    layout->setSpacing(6);
    layout->addWidget(controlsRow, 0, 38);
    layout->addWidget(statusRow, 0, 18);
    layout->addWidget(videoWidget_, 1, 0);
    setLayout(layout);

    SwObject::connect(openButton_, &SwPushButton::clicked, this, &SocketTrafficRtspPlayerWidget::openCurrentUrl);
    SwObject::connect(stopButton_, &SwPushButton::clicked, this, &SocketTrafficRtspPlayerWidget::stopPlayback);
    SwObject::connect(audioCheck_, &SwCheckBox::toggled, this, [this](bool enabled) { applyAudioPreference_(enabled); });
    SwObject::connect(monitoringCheck_, &SwCheckBox::toggled, this, [this](bool enabled) {
        setMonitoringUiEnabled(enabled);
        emit monitoringEnabledChanged(enabled);
    });
    SwObject::connect(stallProfilingCheck_, &SwCheckBox::toggled, this, [this](bool enabled) {
        emit stallProfilingEnabledChanged(enabled);
    });

    setThreadLoadPercentage(0.0);
    setMonitoringUiEnabled(monitoringEnabled());
}

void SocketTrafficRtspPlayerWidget::rebuildPlayer_() {
    if (videoWidget_) {
        videoWidget_->stop();
        videoWidget_->setVideoSink(std::shared_ptr<SwVideoSink>());
    }

    if (player_) {
        player_->stop();
        player_->setSource(std::shared_ptr<SwMediaSource>());
    }

    player_.reset(new SwMediaPlayer(this));
    player_->setTracksChangedCallback([this](const SwList<SwMediaTrack>&) { refreshStatusText_(); });
    player_->setAudioEnabled(audioCheck_ ? audioCheck_->isChecked() : true);
    player_->setMetadataEnabled(urlEdit_ ? SwMediaOpenOptions::fromUrl(urlEdit_->getText().trimmed()).enableMetadata : false);
    if (videoWidget_) {
        videoWidget_->setVideoSink(player_->videoSink());
    }
}

void SocketTrafficRtspPlayerWidget::configureLiveVideoPipeline_(const SwMediaOpenOptions& openOptions) {
    if (!player_ || !player_->videoSink()) {
        return;
    }

    std::shared_ptr<SwVideoSink> sink = player_->videoSink();
    sink->setDedicatedDecodeThreadEnabled(true);
    sink->setRuntimeDecoderRerouteEnabled(false);
    sink->setDecoderStallRecoveryEnabled(true);
    sink->setDecodeQueueLimits(48, 1024 * 1024);

    SwString decoderId = openOptions.decoderId.trimmed();
    if (decoderId.isEmpty()) {
        decoderId = configuredDecoderId_().trimmed();
    }
    if (decoderId.isEmpty()) {
        sink->setPreferredVideoDecoder(SwVideoPacket::Codec::H264,
                                       swPlatformVideoDecoderId());
        sink->setPreferredVideoDecoder(SwVideoPacket::Codec::H265,
                                       swPlatformVideoDecoderId());
        return;
    }
    sink->setPreferredVideoDecoder(SwVideoPacket::Codec::H264, decoderId);
    sink->setPreferredVideoDecoder(SwVideoPacket::Codec::H265, decoderId);
}

void SocketTrafficRtspPlayerWidget::refreshStatusText_() {
    if (statusLabel_) {
        statusLabel_->setText(statusText_());
    }
}

SwString SocketTrafficRtspPlayerWidget::statusText_() const {
    if (!player_) {
        return "Lecteur indisponible.";
    }

    SwString url = openedUrl_.isEmpty() && urlEdit_ ? urlEdit_->getText().trimmed() : openedUrl_;
    if (url.isEmpty()) {
        url = defaultRtspUrl_();
    }

    SwString text = playbackStateLabel_(player_->playbackState()) + " | " + url;
    text += player_->isAudioEnabled() ? " | audio ON" : " | audio OFF";

    const SwMediaPlayer::MediaInfo info = player_->mediaInfo();
    if (info.trackCount > 0) {
        text += " | tracks " + SwString::number(info.trackCount);
    }
    if (info.videoWidth > 0 && info.videoHeight > 0) {
        text += " | " + SwString::number(info.videoWidth) + "x" + SwString::number(info.videoHeight);
    }
    if (info.videoMeasuredFps > 0.0) {
        text += " | " + SwString::number(info.videoMeasuredFps, 'f', 1) + " fps";
    }
    if (videoWidget_) {
        text += " | renderer actif";
    }
    if (info.hasAudio && !info.audioCodec.isEmpty()) {
        text += " | " + info.audioCodec.toUpper();
    }

    if (!player_->source()) {
        text += " | URL RTSP par defaut prechargee";
    }

    return text;
}

void SocketTrafficRtspPlayerWidget::applyAudioPreference_(bool enabled) {
    if (player_) {
        player_->setAudioEnabled(enabled);
    }
    refreshStatusText_();
}
