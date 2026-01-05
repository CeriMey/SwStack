#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwLabel.h"
#include "SwVideoWidget.h"
#include "SwTimer.h"
#include "SwLayout.h"

#include "media/SwMediaFoundationVideoSource.h"
#include "media/SwMediaFoundationMovieSource.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>

#if !defined(_WIN32)
int main() {
    std::cerr << "Example 16 requires Windows Media Foundation and is only available on Windows." << std::endl;
    return 0;
}
#else

#ifdef _WIN32
namespace {
std::wstring toWide(const char* utf8) {
    if (!utf8) {
        return {};
    }
    int len = static_cast<int>(std::strlen(utf8));
    if (len == 0) {
        return {};
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, len, nullptr, 0);
    if (wlen <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, len, wide.empty() ? nullptr : &wide[0], wlen);
    return wide;
}
} // namespace
#endif

int main(int argc, char** argv) {
    SwGuiApplication app;
    SwMainWindow window(L"Example 16 - Webcam Video Widget", 1280, 760);
    window.show();

    auto* infoLabel = new SwLabel(&window);
    infoLabel->setText("Initializing webcam...");
    infoLabel->setMinimumSize(480, 28);

    auto* videoWidget = new SwVideoWidget(&window);
    videoWidget->show();
    videoWidget->setBackgroundColor({10, 10, 10});
    videoWidget->setScalingMode(SwVideoWidget::ScalingMode::Fit);

    auto* layout = new SwVerticalLayout(&window);
    layout->setMargin(20);
    layout->setSpacing(12);
    window.setLayout(layout);
    layout->addWidget(infoLabel, 0, infoLabel->minimumSizeHint().height);
    layout->addWidget(videoWidget, 1);

    struct VideoStats {
        std::atomic<int> frames{0};
        std::atomic<int> width{0};
        std::atomic<int> height{0};
    };
    auto videoStats = std::make_shared<VideoStats>();
    SwTimer statsTimer(1000, &window);
    statsTimer.setSingleShot(false);

    std::shared_ptr<SwVideoSource> source;
#if defined(_WIN32)
    enum class SourceSelection { Auto, Webcam, File };
    SourceSelection selection = SourceSelection::Auto;
    std::wstring moviePath;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--webcam" || arg == "-w") {
            selection = SourceSelection::Webcam;
        } else if (arg == "--file" || arg == "-f") {
            if (i + 1 < argc) {
                moviePath = toWide(argv[++i]);
                selection = SourceSelection::File;
            }
        } else {
            selection = SourceSelection::File;
            moviePath = toWide(argv[i]);
        }
    }

    if (selection != SourceSelection::Webcam && !moviePath.empty()) {
        auto movieSource = std::make_shared<SwMediaFoundationMovieSource>(moviePath);
        if (movieSource->initialize()) {
            source = movieSource;
            infoLabel->setText(SwString("Playing file source"));
        } else {
            infoLabel->setText("Failed to initialize movie source. Falling back to webcam.");
        }
    }

    if (!source) {
        auto webcamSource = std::make_shared<SwMediaFoundationVideoSource>();
        if (!webcamSource->initialize()) {
            infoLabel->setText("Failed to initialize webcam.");
            std::cerr << "[Example16] Unable to initialize webcam source." << std::endl;
        } else {
            source = webcamSource;
            infoLabel->setText("Using webcam source");
        }
    }
#endif

    if (source) {
        videoWidget->setVideoSource(source);
        videoWidget->setFrameArrivedCallback(
            [videoStats](const SwVideoFrame& frame) {
                videoStats->frames.fetch_add(1, std::memory_order_relaxed);
                videoStats->width.store(frame.width(), std::memory_order_relaxed);
                videoStats->height.store(frame.height(), std::memory_order_relaxed);
            });
        SwObject::connect(&statsTimer, &SwTimer::timeout, [videoStats, infoLabel]() {
            const int fps = videoStats->frames.exchange(0, std::memory_order_relaxed);
            const int width = videoStats->width.load(std::memory_order_relaxed);
            const int height = videoStats->height.load(std::memory_order_relaxed);
            if (width > 0 && height > 0) {
                std::ostringstream oss;
                oss << "Webcam feed: " << width << "x" << height << " @ " << fps << " fps";
                infoLabel->setText(SwString(oss.str().c_str()));
            } else {
                infoLabel->setText("Waiting for frames...");
            }
        });
        statsTimer.start();
        videoWidget->start();
    }

    if (!source) {
        infoLabel->setText("No available source.");
    }

    int exitCode = app.exec();
    if (source) {
        source->stop();
    }
    return exitCode;
}

#endif
