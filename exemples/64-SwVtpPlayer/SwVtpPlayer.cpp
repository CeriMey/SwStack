#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwTimer.h"
#include "SwVideoWidget.h"

#include "media/SwMediaPlayer.h"
#include "media/SwMediaSourceFactory.h"
#include "media/SwVtpVideoSource.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace {

struct AppOptions {
    std::string url{"swvtp://127.0.0.1:55245?announce=127.0.0.1&localport=0"};
    std::string decoderId{};
    uint64_t autoExitFrames{0};
    int timeoutMs{15000};
};

bool startsWith(const std::string& value, const char* prefix) {
    const std::string p(prefix);
    return value.size() >= p.size() && value.compare(0, p.size(), p) == 0;
}

uint64_t parseU64(const std::string& text, uint64_t fallback) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return fallback;
    }
    return static_cast<uint64_t>(value);
}

int parseInt(const std::string& text, int fallback) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(value);
}

void printDecoders() {
    std::cout << "[SwVtpPlayer] Available AV1 decoders:\n";
    std::cout << "  - id=auto name=Automatic\n";
    const SwList<SwVideoDecoderDescriptor> decoders =
        SwVideoWidget::availableVideoDecoders(SwVideoPacket::Codec::AV1);
    for (const auto& decoder : decoders) {
        std::cout << "  - id=" << decoder.id
                  << " name=" << decoder.displayName
                  << " priority=" << decoder.priority
                  << " shareable=" << (decoder.shareable ? 1 : 0)
                  << "\n";
    }
}

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  SwVtpPlayer [swvtp://host:port?announce=clientIp&localport=0] "
           "[--decoder=id] [--auto-exit-frames=N] [--timeout-ms=N]\n"
        << "  SwVtpPlayer --list-decoders\n";
}

AppOptions parseOptions(int argc, char** argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        }
        if (arg == "--list-decoders") {
            printDecoders();
            std::exit(0);
        }
        if (startsWith(arg, "--decoder=")) {
            options.decoderId = arg.substr(std::string("--decoder=").size());
            continue;
        }
        if (startsWith(arg, "--auto-exit-frames=")) {
            options.autoExitFrames =
                parseU64(arg.substr(std::string("--auto-exit-frames=").size()), 0);
            continue;
        }
        if (startsWith(arg, "--timeout-ms=")) {
            options.timeoutMs = parseInt(arg.substr(std::string("--timeout-ms=").size()),
                                         options.timeoutMs);
            continue;
        }
        if (!arg.empty() && arg[0] != '-') {
            options.url = arg;
        }
    }
    return options;
}

SwString formatMetrics(const SwVtpVideoSourceMetrics& metrics,
                       uint64_t presentedFrames,
                       double presentedFps) {
    char buffer[1024];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "rx frames=%llu presented=%llu fps=%.1f datagrams=%llu "
                  "bitrate video=%.0fkbps udp=%.0fkbps "
                  "videoBytes=%llu udpBytes=%llu clock=%s rtt=%.3fms uncertainty=%.3fms "
                  "transfer avg/min/max=%.3f/%.3f/%.3fms capture avg/min/max=%.3f/%.3f/%.3fms "
                  "dup=%llu stale=%llu drop=%llu",
                  static_cast<unsigned long long>(metrics.framesCompleted),
                  static_cast<unsigned long long>(presentedFrames),
                  presentedFps,
                  static_cast<unsigned long long>(metrics.datagramsReceived),
                  metrics.liveVideoKbps,
                  metrics.liveUdpKbps,
                  static_cast<unsigned long long>(metrics.videoBytesCompleted),
                  static_cast<unsigned long long>(metrics.datagramBytesReceived),
                  metrics.clockSynced ? "ok" : "wait",
                  static_cast<double>(metrics.clockRttUs) / 1000.0,
                  static_cast<double>(metrics.clockUncertaintyUs) / 1000.0,
                  metrics.averageTransferLatencyMs(),
                  metrics.minTransferLatencyMs(),
                  metrics.maxTransferLatencyMs(),
                  metrics.averageCaptureLatencyMs(),
                  metrics.minCaptureLatencyMs(),
                  metrics.maxCaptureLatencyMs(),
                  static_cast<unsigned long long>(metrics.duplicateFragments),
                  static_cast<unsigned long long>(metrics.staleFragments),
                  static_cast<unsigned long long>(metrics.droppedFrames));
    return SwString(buffer);
}

void printFinalMetrics(const SwVtpVideoSourceMetrics& metrics,
                       uint64_t presentedFrames,
                       double presentedFps) {
    std::cout << "[SwVTP player metrics] framesCompleted=" << metrics.framesCompleted
              << " presentedFrames=" << presentedFrames
              << " presentedFps=" << presentedFps
              << " datagramsReceived=" << metrics.datagramsReceived
              << " liveVideoKbps=" << metrics.liveVideoKbps
              << " liveUdpKbps=" << metrics.liveUdpKbps
              << " videoBytes=" << metrics.videoBytesCompleted
              << " udpBytes=" << metrics.datagramBytesReceived
              << " clockRttUs=" << metrics.clockRttUs
              << " clockUncertaintyUs=" << metrics.clockUncertaintyUs
              << " transferLatencyMs(avg/min/max)="
              << metrics.averageTransferLatencyMs() << "/"
              << metrics.minTransferLatencyMs() << "/"
              << metrics.maxTransferLatencyMs()
              << " captureLatencyMs(avg/min/max)="
              << metrics.averageCaptureLatencyMs() << "/"
              << metrics.minCaptureLatencyMs() << "/"
              << metrics.maxCaptureLatencyMs()
              << " duplicates=" << metrics.duplicateFragments
              << " stale=" << metrics.staleFragments
              << " dropped=" << metrics.droppedFrames
              << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const AppOptions options = parseOptions(argc, argv);

    SwGuiApplication app;
    SwMainWindow window(L"SwVTP Low Latency Player", 1180, 720);

    auto* title = new SwLabel(&window);
    title->setText(SwString("SwVTP AV1 Player | ") + SwString(options.url.c_str()));
    title->setMinimumSize(760, 28);

    auto* state = new SwLabel(&window);
    state->setText("Opening SwVTP source...");
    state->setMinimumSize(760, 28);

    auto* metricsLabel = new SwLabel(&window);
    metricsLabel->setText("Waiting for metrics...");
    metricsLabel->setMinimumSize(760, 44);

    auto* video = new SwVideoWidget(&window);
    video->setScalingMode(SwVideoWidget::ScalingMode::Fit);
    video->setBackgroundColor({4, 6, 8});

    auto* layout = new SwVerticalLayout(&window);
    layout->setMargin(14);
    layout->setSpacing(8);
    window.setLayout(layout);
    layout->addWidget(title, 0, 30);
    layout->addWidget(state, 0, 30);
    layout->addWidget(metricsLabel, 0, 52);
    layout->addWidget(video, 1);

    std::shared_ptr<SwMediaSource> source =
        SwMediaSourceFactory::createMediaSource(SwString(options.url.c_str()));
    std::shared_ptr<SwVtpVideoSource> vtpSource =
        std::dynamic_pointer_cast<SwVtpVideoSource>(source);
    if (!vtpSource) {
        std::cerr << "[SwVtpPlayer] Unsupported URL: " << options.url << "\n";
        return 2;
    }

    auto player = std::make_shared<SwMediaPlayer>();
    player->setSource(source);
    player->setAudioEnabled(false);
    player->setMetadataEnabled(false);
    player->videoSink()->setDedicatedDecodeThreadEnabled(true);
    player->videoSink()->setDecodeQueueLimits(4, 1024 * 1024);
    player->videoSink()->setDecoderStallRecoveryEnabled(true);
    video->setVideoSink(player->videoSink());

    if (!options.decoderId.empty() && options.decoderId != "auto") {
        if (!video->setPreferredVideoDecoder(SwVideoPacket::Codec::AV1,
                                             SwString(options.decoderId.c_str()))) {
            std::cerr << "[SwVtpPlayer] Unknown AV1 decoder id: "
                      << options.decoderId << "\n";
            printDecoders();
            return 2;
        }
    }

    SwVtpVideoSourceMetrics latestMetrics;
    std::mutex latestMetricsMutex;
    vtpSource->setMetricsCallback([&latestMetrics, &latestMetricsMutex](
                                      const SwVtpVideoSourceMetrics& metrics) {
        std::lock_guard<std::mutex> lock(latestMetricsMutex);
        latestMetrics = metrics;
    });

    SwTimer uiTimer(200, &window);
    SwObject::connect(&uiTimer, &SwTimer::timeout, &window, [&]() {
        const SwVideoSource::StreamStatus status = vtpSource->streamStatus();
        SwString statusText = "Status: ";
        switch (status.state) {
        case SwVideoSource::StreamState::Connecting:
            statusText += "connecting";
            break;
        case SwVideoSource::StreamState::Recovering:
            statusText += "recovering";
            break;
        case SwVideoSource::StreamState::Streaming:
            statusText += "streaming";
            break;
        case SwVideoSource::StreamState::Stopped:
        default:
            statusText += "stopped";
            break;
        }
        if (!status.reason.isEmpty()) {
            statusText += SwString(" | ") + status.reason;
        }
        state->setText(statusText);

        const uint64_t presented = player->presentedVideoFrameCount();
        SwVtpVideoSourceMetrics metrics;
        {
            std::lock_guard<std::mutex> lock(latestMetricsMutex);
            metrics = latestMetrics;
        }
        metricsLabel->setText(formatMetrics(metrics,
                                            presented,
                                            player->videoMeasuredFps()));

        if (options.autoExitFrames > 0U &&
            presented >= options.autoExitFrames) {
            printFinalMetrics(metrics, presented, player->videoMeasuredFps());
            app.exit(0);
        }
    });
    uiTimer.start();

    SwTimer timeoutTimer(options.timeoutMs, &window);
    timeoutTimer.setSingleShot(true);
    SwObject::connect(&timeoutTimer, &SwTimer::timeout, &window, [&]() {
        if (options.autoExitFrames == 0U) {
            return;
        }
        SwVtpVideoSourceMetrics metrics;
        {
            std::lock_guard<std::mutex> lock(latestMetricsMutex);
            metrics = latestMetrics;
        }
        printFinalMetrics(metrics,
                          player->presentedVideoFrameCount(),
                          player->videoMeasuredFps());
        app.exit(player->presentedVideoFrameCount() >= options.autoExitFrames ? 0 : 3);
    });
    timeoutTimer.start();

    player->play();
    window.show();

    const int code = app.exec();
    player->stop();
    if (options.autoExitFrames == 0U) {
        SwVtpVideoSourceMetrics metrics;
        {
            std::lock_guard<std::mutex> lock(latestMetricsMutex);
            metrics = latestMetrics;
        }
        printFinalMetrics(metrics,
                          player->presentedVideoFrameCount(),
                          player->videoMeasuredFps());
    }
    return code;
}
