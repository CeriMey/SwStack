#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwPushButton.h"
#include "SwObject.h"
#include "SwLayout.h"

#include "ScoreBanner.h"
#include "PingPongGameWidget.h"

int main() {
    SwGuiApplication app;

    SwMainWindow mainWindow;
    mainWindow.resize(1080, 920);
    mainWindow.show();

    SwWidget* headerWidget = new SwWidget(&mainWindow);
    headerWidget->resize(100, 120);
    ScoreBanner* scoreBanner = new ScoreBanner(headerWidget);
    scoreBanner->resize(680, 90);
    SwPushButton* replayButton = new SwPushButton("Rejouer", headerWidget);
    replayButton->resize(150, 50);
    SwHorizontalLayout* headerLayout = new SwHorizontalLayout(headerWidget);
    headerLayout->setMargin(0);
    headerLayout->setSpacing(20);
    headerLayout->addWidget(scoreBanner, 1);
    headerLayout->addWidget(replayButton, 0, replayButton->getRect().width);
    headerWidget->setLayout(headerLayout);

    PingPongGameWidget* gameWidget = new PingPongGameWidget(&mainWindow);
    gameWidget->resize(860, 460);
    gameWidget->setScoreBanner(scoreBanner);
    gameWidget->replayRound();

    SwVerticalLayout* mainLayout = new SwVerticalLayout(&mainWindow);
    mainLayout->setMargin(20);
    mainLayout->setSpacing(20);
    mainLayout->addWidget(headerWidget, 0, headerWidget->sizeHint().height);
    mainLayout->addWidget(gameWidget, 1, 240);
    mainWindow.setLayout(mainLayout);

    SwObject::connect(replayButton, &SwPushButton::clicked, [gameWidget]() {
        gameWidget->replayRound();
    });

    return app.exec();
}
