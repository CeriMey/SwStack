#include "SwQtVideoPlayerWidget.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <utility>

#include <QByteArray>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#include "SwGuiApplication.h"
#include "SwDebug.h"
#include "SwLayout.h"
#include "SwMediaOpenOptions.h"
#include "SwMediaSource.h"
#include "SwMediaSourceFactory.h"
#include "SwObject.h"
#include "SwString.h"
#include "SwVideoSink.h"
#include "SwVideoWidget.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"
#include "gui/qtbinding/SwQtBindingEventPump.h"
#include "gui/qtbinding/SwQtBindingWin32WidgetHost.h"
#include "source/SwVtpVideoSource.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

namespace {

SwString toSwString_(const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    return SwString(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

class SharedSwQtRuntime {
public:
    static SharedSwQtRuntime& instance() {
        static SharedSwQtRuntime* runtime = new SharedSwQtRuntime();
        return *runtime;
    }

    void ensure() {
        configureDebugLog_();
        if (!SwGuiApplication::instance(false)) {
            ownedApplication_.reset(new SwGuiApplication());
        }
        pump_.setApplication(SwGuiApplication::instance(false));
    }

    void drain() {
        ensure();
        pump_.drainPostedWork(256, true);
    }

private:
    static void configureDebugLog_() {
        static bool configured = false;
        if (configured) {
            return;
        }
        configured = true;
        const char* logFile = std::getenv("SW_LOG_FILE");
        if (logFile && logFile[0] != '\0') {
            SwDebug::setFilePath(logFile);
            SwDebug::setFileEnabled(true);
        }
    }

    std::unique_ptr<SwGuiApplication> ownedApplication_{};
    SwQtBindingEventPump pump_{};
};

class SwVideoPlayerRoot final : public SwWidget {
    SW_OBJECT(SwVideoPlayerRoot, SwWidget)

public:
    explicit SwVideoPlayerRoot(SwWidget* parent = nullptr)
        : SwWidget(parent)
        , videoSink_(std::make_shared<SwVideoSink>()) {
        setObjectName("SwQtVideoPlayerRoot");
        setStyleSheet("SwWidget#SwQtVideoPlayerRoot { background-color: rgb(8, 8, 8); }");
        video_ = new SwVideoWidget(this);
        video_->setScalingMode(SwVideoWidget::ScalingMode::Fit);
        video_->setBackgroundColor({8, 8, 8});
        video_->setVideoSink(videoSink_);
        videoSink_->setDedicatedDecodeThreadEnabled(true);
        videoSink_->setDecodeQueueLimits(6, 2 * 1024 * 1024);
        videoSink_->setDecoderStallRecoveryEnabled(true);

        SwVerticalLayout* layout = new SwVerticalLayout(this);
        layout->setMargin(0);
        layout->setSpacing(0);
        layout->addWidget(video_, 1);
        setLayout(layout);
        setMinimumSize(320, 180);
        resize(640, 360);
    }

    ~SwVideoPlayerRoot() override {
        stop();
    }

    bool openUrl(const QString& url) {
        return openUrlInternal_(url, autoPlay_, "open");
    }

    void play() {
        swCWarning("sw.exemples.qtstaticvideo")
            << "[SwQtVideoPlayerWidget] Play requested url=" << toSwString_(currentUrl_);
        if (currentUrl_.trimmed().isEmpty()) {
            return;
        }
        openUrlInternal_(currentUrl_, true, "play");
    }

    void stop() {
        swCWarning("sw.exemples.qtstaticvideo")
            << "[SwQtVideoPlayerWidget] Stop requested url=" << toSwString_(currentUrl_);
        playing_ = false;
        clearSourceCallbacks_();
        if (video_) {
            video_->stop();
        } else if (videoSink_) {
            videoSink_->stop();
        } else if (source_) {
            source_->stop();
        }
    }

    void restartIfStale(int noFrameTimeoutMs, int staleFrameTimeoutMs, int minRestartIntervalMs) {
        if (!playing_ || currentUrl_.trimmed().isEmpty() || !videoSink_) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (lastRestartTime_.time_since_epoch().count() != 0) {
            const auto sinceRestartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastRestartTime_).count();
            if (sinceRestartMs < minRestartIntervalMs) {
                return;
            }
        }

        const auto lastFrameTime = videoSink_->lastFrameTime();
        bool stale = false;
        long long ageMs = 0;
        if (lastFrameTime.time_since_epoch().count() == 0) {
            if (lastStartTime_.time_since_epoch().count() == 0) {
                return;
            }
            ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastStartTime_).count();
            stale = ageMs >= noFrameTimeoutMs;
        } else {
            ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastFrameTime).count();
            stale = ageMs >= staleFrameTimeoutMs;
        }
        if (!stale) {
            return;
        }

        lastRestartTime_ = now;
        swCWarning("sw.exemples.qtstaticvideo")
            << "[SwQtVideoPlayerWidget] Watchdog reopening stale stream ageMs=" << ageMs
            << " url=" << toSwString_(currentUrl_);
        openUrlInternal_(currentUrl_, true, "watchdog");
    }

    void pulseVideoPaint() {
        if (!playing_) {
            return;
        }
        update();
        if (video_) {
            video_->pumpFramePresentation();
            video_->update();
        }
    }

    void setAutoPlay(bool enabled) {
        autoPlay_ = enabled;
    }

    QString currentUrl() const {
        return currentUrl_;
    }

private:
    bool openUrlInternal_(const QString& url, bool startAfterOpen, const char* reason) {
        const QString trimmed = url.trimmed();
        if (trimmed.isEmpty()) {
            return false;
        }
        currentUrl_ = trimmed;
        const SwMediaOpenOptions openOptions = SwMediaOpenOptions::fromUrl(toSwString_(trimmed));
        std::shared_ptr<SwMediaSource> source =
            SwMediaSourceFactory::createMediaSource(openOptions);
        if (!source) {
            return false;
        }
        swCWarning("sw.exemples.qtstaticvideo")
            << "[SwQtVideoPlayerWidget] Opening source reason=" << (reason ? reason : "unknown")
            << " url=" << toSwString_(trimmed);
        stop();
        source_ = source;
        auto videoSource = std::dynamic_pointer_cast<SwVideoSource>(source_);
        if (!videoSource) {
            source_.reset();
            return false;
        }
        if (!openOptions.decoderId.isEmpty()) {
            videoSink_->setPreferredVideoDecoder(SwVideoPacket::Codec::H264, openOptions.decoderId);
            videoSink_->setPreferredVideoDecoder(SwVideoPacket::Codec::H265, openOptions.decoderId);
            videoSink_->setPreferredVideoDecoder(SwVideoPacket::Codec::AV1, openOptions.decoderId);
        }
        videoSink_->setVideoSource(videoSource);
        bindSwVtpMetrics_(source_);
        if (startAfterOpen) {
            startCurrentSource_();
        }
        return true;
    }

    void startCurrentSource_() {
        if (!source_ || !videoSink_) {
            return;
        }
        bindSourceCallbacks_();
        playing_ = true;
        lastStartTime_ = std::chrono::steady_clock::now();
        if (video_) {
            video_->start();
        } else {
            videoSink_->start();
        }
    }

    void bindSourceCallbacks_() {
        if (!source_ || callbacksBound_) {
            return;
        }
        std::weak_ptr<int> weakGuard = callbackGuard_;
        source_->setRecoveryCallback([this, weakGuard](const SwMediaSource::RecoveryEvent& event) {
            if (weakGuard.expired() || !videoSink_) {
                return;
            }
            videoSink_->recoverLiveEdge(event);
        });
        callbacksBound_ = true;
    }

    void clearSourceCallbacks_() {
        if (!source_) {
            callbacksBound_ = false;
            return;
        }
        source_->setMediaPacketCallback(SwMediaSource::MediaPacketCallback());
        source_->setRecoveryCallback(SwMediaSource::RecoveryCallback());
        if (auto swvtpSource = std::dynamic_pointer_cast<SwVtpVideoSource>(source_)) {
            swvtpSource->setMetricsCallback(SwVtpVideoSource::MetricsCallback());
        }
        callbacksBound_ = false;
    }

    void bindSwVtpMetrics_(const std::shared_ptr<SwMediaSource>& source) {
        auto swvtpSource = std::dynamic_pointer_cast<SwVtpVideoSource>(source);
        if (!swvtpSource) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(swVtpMetricsLogMutex_);
            lastSwVtpMetricsLogTime_ = std::chrono::steady_clock::time_point{};
            lastSwVtpDatagrams_ = 0;
            lastSwVtpFramesCompleted_ = 0;
            lastSwVtpKlvPacketsCompleted_ = 0;
            lastSwVtpAccepted_ = false;
            loggedSwVtpMetrics_ = false;
        }

        std::weak_ptr<int> weakGuard = callbackGuard_;
        swvtpSource->setMetricsCallback(
            [this, weakGuard](const SwVtpVideoSourceMetrics& metrics) {
                if (weakGuard.expired()) {
                    return;
                }
                logSwVtpMetrics_(metrics);
            });

        swCWarning("sw.exemples.qtstaticvideo")
            << "[SwQtVideoPlayerWidget] SwVTP metrics callback bound";
    }

    void logSwVtpMetrics_(const SwVtpVideoSourceMetrics& metrics) {
        const auto now = std::chrono::steady_clock::now();
        uint64_t presentedFrames = 0;
        SwVideoSource::StreamStatus status;
        if (videoSink_) {
            presentedFrames = videoSink_->presentedFrameCount();
            status = videoSink_->streamStatus();
        }

        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(swVtpMetricsLogMutex_);
            const bool acceptedChanged = metrics.accepted != lastSwVtpAccepted_;
            const bool datagramsChanged = metrics.datagramsReceived != lastSwVtpDatagrams_;
            const bool framesChanged = metrics.framesCompleted != lastSwVtpFramesCompleted_;
            const bool klvChanged = metrics.klvPacketsCompleted != lastSwVtpKlvPacketsCompleted_;
            const auto elapsedMs =
                lastSwVtpMetricsLogTime_.time_since_epoch().count() == 0
                    ? 0
                    : std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - lastSwVtpMetricsLogTime_).count();

            shouldLog =
                !loggedSwVtpMetrics_ ||
                acceptedChanged ||
                (framesChanged && elapsedMs >= 500) ||
                (klvChanged && elapsedMs >= 1000) ||
                (datagramsChanged && metrics.framesCompleted == 0 && elapsedMs >= 1000);

            if (!shouldLog) {
                return;
            }

            loggedSwVtpMetrics_ = true;
            lastSwVtpMetricsLogTime_ = now;
            lastSwVtpDatagrams_ = metrics.datagramsReceived;
            lastSwVtpFramesCompleted_ = metrics.framesCompleted;
            lastSwVtpKlvPacketsCompleted_ = metrics.klvPacketsCompleted;
            lastSwVtpAccepted_ = metrics.accepted;
        }

        swCWarning("sw.exemples.qtstaticvideo")
            << "[SwQtVideoPlayerWidget] SwVTP metrics"
            << " accepted=" << (metrics.accepted ? 1 : 0)
            << " clockSynced=" << (metrics.clockSynced ? 1 : 0)
            << " localPort=" << metrics.localPort
            << " announced=" << metrics.announcedAddress
            << " datagrams=" << metrics.datagramsReceived
            << " udpBytes=" << metrics.datagramBytesReceived
            << " framesCompleted=" << metrics.framesCompleted
            << " videoBytes=" << metrics.videoBytesCompleted
            << " klvPackets=" << metrics.klvPacketsCompleted
            << " droppedFrames=" << metrics.droppedFrames
            << " duplicateFragments=" << metrics.duplicateFragments
            << " staleFragments=" << metrics.staleFragments
            << " liveVideoKbps=" << metrics.liveVideoKbps
            << " presentedFrames=" << presentedFrames
            << " sinkState=" << static_cast<int>(status.state);
    }

    SwVideoWidget* video_{nullptr};
    std::shared_ptr<SwVideoSink> videoSink_{};
    std::shared_ptr<SwMediaSource> source_{};
    std::shared_ptr<int> callbackGuard_{std::make_shared<int>(0)};
    QString currentUrl_{};
    bool autoPlay_{true};
    bool callbacksBound_{false};
    bool playing_{false};
    std::chrono::steady_clock::time_point lastStartTime_{};
    std::chrono::steady_clock::time_point lastRestartTime_{};
    std::mutex swVtpMetricsLogMutex_{};
    std::chrono::steady_clock::time_point lastSwVtpMetricsLogTime_{};
    uint64_t lastSwVtpDatagrams_{0};
    uint64_t lastSwVtpFramesCompleted_{0};
    uint64_t lastSwVtpKlvPacketsCompleted_{0};
    bool lastSwVtpAccepted_{false};
    bool loggedSwVtpMetrics_{false};
};

} // namespace

class SwQtVideoPlayerWidget::Impl {
public:
    explicit Impl(SwQtVideoPlayerWidget* owner)
        : owner_(owner) {
        SharedSwQtRuntime::instance().ensure();
        root_ = new SwVideoPlayerRoot();
        hostBinding_.setRootWidget(root_);

        eventPumpTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&eventPumpTimer_, &QTimer::timeout, owner_, [this]() {
            drainRuntimeAndPaint_(false);
        });
        eventPumpTimer_.start(4);

        paintPulseTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&paintPulseTimer_, &QTimer::timeout, owner_, [this]() {
            drainRuntimeAndPaint_(true);
        });
        paintPulseTimer_.start(16);

        watchdogTimer_.setTimerType(Qt::CoarseTimer);
        QObject::connect(&watchdogTimer_, &QTimer::timeout, owner_, [this]() {
            if (root_) {
                root_->restartIfStale(4000, 2000, 3000);
            }
        });
        watchdogTimer_.start(500);
    }

    ~Impl() {
        shutdown();
    }

    bool openUrl(const QString& url) {
        return root_ ? root_->openUrl(url) : false;
    }

    void play() {
        if (root_) {
            root_->play();
        }
    }

    void stop() {
        if (root_) {
            root_->stop();
        }
    }

    void setAutoPlay(bool enabled) {
        if (root_) {
            root_->setAutoPlay(enabled);
        }
    }

    QString currentUrl() const {
        return root_ ? root_->currentUrl() : QString();
    }

    QSize minimumSizeHint() const {
        return QSize(320, 180);
    }

    QSize sizeHint() const {
        return QSize(960, 540);
    }

    void showEvent(QShowEvent*) {
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        syncRootGeometry_();
        if (root_) {
            root_->update();
        }
    }

    void resizeEvent(QResizeEvent*) {
        syncRootGeometry_();
    }

#if defined(_WIN32)
    bool handleNativeMessage(MSG* msg, intptr_t* result) {
        if (!root_ || !msg) {
            return false;
        }
        if (msg->message == WM_SIZE) {
            syncRootGeometry_();
        }
        return hostBinding_.handleMessage(msg, result);
    }
#endif

    void shutdown() {
        eventPumpTimer_.stop();
        paintPulseTimer_.stop();
        watchdogTimer_.stop();
        hostBinding_.shutdown();
        if (root_) {
            delete root_;
            root_ = nullptr;
        }
#if defined(_WIN32)
        cachedHostHwnd_ = nullptr;
#endif
    }

private:
    void syncRootGeometry_() {
        if (!root_) {
            return;
        }
        syncBridgeToNativeHost_();
        hostBinding_.syncRootGeometryToHostClientRect(
            std::max(1, owner_->width()),
            std::max(1, owner_->height()));
        SwWidgetPlatformAdapter::flushDamage(true);
        updateNativeHostNow_();
    }

#if defined(_WIN32)
    void syncBridgeToNativeHost_() {
        cacheHostHwnd_();
        const HWND hwnd = cachedHostHwnd_;
        if (hwnd && hostBinding_.hostWindowHandle() != hwnd) {
            hostBinding_.setHostWindowHandle(hwnd);
        }
    }

    void cacheHostHwnd_() {
        if (!cachedHostHwnd_) {
            cachedHostHwnd_ =
                reinterpret_cast<HWND>(static_cast<std::uintptr_t>(owner_->winId()));
        }
    }

    void updateNativeHostNow_() {
        cacheHostHwnd_();
        if (cachedHostHwnd_ && ::IsWindow(cachedHostHwnd_)) {
            ::UpdateWindow(cachedHostHwnd_);
        }
    }
#else
    void syncBridgeToNativeHost_() {}
    void updateNativeHostNow_() {}
#endif

    void drainRuntimeAndPaint_(bool forceVideoPaint) {
        SharedSwQtRuntime::instance().drain();
        if (forceVideoPaint && root_) {
            root_->pulseVideoPaint();
            SwWidgetPlatformAdapter::flushDamage(true);
        }
        updateNativeHostNow_();
    }

    SwQtVideoPlayerWidget* owner_{nullptr};
    SwVideoPlayerRoot* root_{nullptr};
    SwQtBindingWin32WidgetHost hostBinding_{};
    QTimer eventPumpTimer_{};
    QTimer paintPulseTimer_{};
    QTimer watchdogTimer_{};
#if defined(_WIN32)
    HWND cachedHostHwnd_{nullptr};
#endif
};

SwQtVideoPlayerWidget::SwQtVideoPlayerWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(new Impl(this)) {
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);
}

SwQtVideoPlayerWidget::~SwQtVideoPlayerWidget() = default;

bool SwQtVideoPlayerWidget::openUrl(const QString& url) {
    return impl_->openUrl(url);
}

void SwQtVideoPlayerWidget::play() {
    impl_->play();
}

void SwQtVideoPlayerWidget::stop() {
    impl_->stop();
}

void SwQtVideoPlayerWidget::setAutoPlay(bool enabled) {
    impl_->setAutoPlay(enabled);
}

QString SwQtVideoPlayerWidget::currentUrl() const {
    return impl_->currentUrl();
}

QSize SwQtVideoPlayerWidget::minimumSizeHint() const {
    return impl_->minimumSizeHint();
}

QSize SwQtVideoPlayerWidget::sizeHint() const {
    return impl_->sizeHint();
}

QPaintEngine* SwQtVideoPlayerWidget::paintEngine() const {
    return nullptr;
}

void SwQtVideoPlayerWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void SwQtVideoPlayerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    impl_->showEvent(event);
}

void SwQtVideoPlayerWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    impl_->resizeEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool SwQtVideoPlayerWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#else
bool SwQtVideoPlayerWidget::nativeEvent(const QByteArray& eventType, void* message, long* result) {
#endif
    Q_UNUSED(eventType);
#if defined(_WIN32)
    intptr_t nativeResult = 0;
    const bool handled = impl_->handleNativeMessage(static_cast<MSG*>(message), &nativeResult);
    if (handled && result) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        *result = static_cast<qintptr>(nativeResult);
#else
        *result = static_cast<long>(nativeResult);
#endif
    }
    return handled;
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}
