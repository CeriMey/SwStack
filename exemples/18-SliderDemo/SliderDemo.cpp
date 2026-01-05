#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwLayout.h"
#include "SwMediaControlWidget.h"

int main() {
    SwGuiApplication app;
    SwMainWindow window(L"Slider Demo", 820, 260);
    window.setStyleSheet("SwMainWindow { background-color: white; }");
    window.show();

    auto* layout = new SwVerticalLayout(&window);
    layout->setMargin(20);
    layout->setSpacing(10);

    auto* media = new SwMediaControlWidget(&window);
    media->setDurationSeconds(245.0);
    media->setBufferedSeconds(180.0);
    media->setPositionSeconds(42.0);

    layout->addWidget(media, 1, 200);
    window.setLayout(layout);

    return app.exec();
}
