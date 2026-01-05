#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwVideoWidget.h"

#include "media/SwRtspUdpSource.h"

#include <memory>
#include <string>

int main(int argc, char** argv) {
    SwGuiApplication app;
    SwMainWindow window(L"SwCore - RTSP (UDP) Video", 960, 640);
    window.show();

    auto* info = new SwLabel(&window);
    info->setText("Connecting to RTSP...");
    info->setMinimumSize(480, 28);

    auto* video = new SwVideoWidget(&window);
    video->setScalingMode(SwVideoWidget::ScalingMode::Fit);
    video->setBackgroundColor({8, 8, 8});

    auto* layout = new SwVerticalLayout(&window);
    layout->setMargin(12);
    layout->setSpacing(8);
    window.setLayout(layout);
    layout->addWidget(info, 0, info->minimumSizeHint().height);
    layout->addWidget(video, 1);

    std::string url = "rtsp://172.16.40.81:5004/video";
    std::string localBind;
    uint16_t rtpPort = 0;
    uint16_t rtcpPort = 0;
    if (argc > 1 && argv[1]) {
        url = argv[1];
    }
    if (argc > 2 && argv[2]) {
        localBind = argv[2];
    }
    if (argc > 3 && argv[3]) {
        rtpPort = static_cast<uint16_t>(std::atoi(argv[3]));
        rtcpPort = static_cast<uint16_t>(rtpPort + 1);
    }

    auto source = std::make_shared<SwRtspUdpSource>(SwString(url.c_str()), &window);
    if (!localBind.empty()) {
        source->setLocalAddress(SwString(localBind.c_str()));
    }
    if (rtpPort != 0) {
        source->forceLocalBind(SwString(localBind.empty() ? "0.0.0.0" : localBind.c_str()), rtpPort, rtcpPort);
    }
    video->setVideoSource(source);

    video->start();
    info->setText(SwString("Streaming from: ") + SwString(url.c_str()));

    int code = app.exec();
    video->stop();
    return code;
}
