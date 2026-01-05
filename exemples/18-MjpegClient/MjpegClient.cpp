#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwVideoWidget.h"

#include "media/SwHttpMjpegSource.h"

#include <memory>
#include <string>

int main(int argc, char** argv) {
    SwGuiApplication app;
    SwMainWindow window(L"SwCore - MJPEG Client", 960, 640);
    window.show();

    auto* info = new SwLabel(&window);
    info->setText("Connecting to MJPEG stream...");
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

    std::string url = "http://172.16.40.81:8081/video";
    if (argc > 1 && argv[1]) {
        url = argv[1];
    }

    auto source = std::make_shared<SwHttpMjpegSource>(SwString(url.c_str()), &window);
    if (source->initialize()) {
        video->setVideoSource(source);
        source->start();
        video->start();
        info->setText(SwString("Streaming from: ") + SwString(url.c_str()));
    } else {
        info->setText("Failed to initialize MJPEG source.");
    }

    int code = app.exec();
    source->stop();
    return code;
}
