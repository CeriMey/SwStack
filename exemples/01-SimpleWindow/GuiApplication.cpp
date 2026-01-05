#include <iostream>
#include "SwPushButton.h"
#include "SwGuiApplication.h"
#include "SwTimer.h"
#include "SwEventLoop.h"
#include "SwProcess.h"
#include "SwMainWindow.h"
#include "SwLineEdit.h"
#include "SwLabel.h"
#include "SwString.h"
#include <SwDebug.h>

int main() {
    SwGuiApplication app;
    SwMainWindow mainWindow;
    mainWindow.show();

    SwObject::connect(&mainWindow, SIGNAL(destroyed), std::function<void()>([]() {
        std::cout << "[MainWindow] destroyed" << std::endl;
    }));

    SwObject::connect(&mainWindow, SIGNAL(resized), [&](int width, int height) {
        std::cout << "Fen�tre -------> redimensionn�e: " << width << "x" << height << std::endl;
    });

    SwString buttonStyle = R"(
        SwPushButton {
            background-color: rgb(56, 118, 255);
            color: #FFFFFF;
            border-radius: 12px;
            border-width: 0px;
            padding: 6px 14px;
        }
    )";

    SwString lineEditStyle = R"(
        SwLineEdit {
            background-color: rgb(25, 31, 55);
            color: #E6EDF6;
            border-radius: 10px;
            border-width: 1px;
            border-color: rgb(70, 86, 140);
            padding: 6px 10px;
        }
    )";

    int startX = 50;
    int startY = 100;
    int spacingX = 350;
    int spacingY = 150;
    int labelWidth = 100;
    int labelHeight = 30;
    int buttonWidth = 100;
    int buttonHeight = 30;
    int lineEditWidth = 180;
    int lineEditHeight = 30;

    for (int i = 0; i < 4; ++i) {
        int row = i / 2;
        int col = i % 2;

        int xPos = startX + col * spacingX;
        int yPos = startY + row * spacingY;

        SwLabel* label = new SwLabel(&mainWindow);
        label->setText(SwString("Label %1:").arg(SwString::number(i)));
        label->move(xPos, yPos);
        label->resize(labelWidth, labelHeight);

        SwLineEdit* lineEdit = new SwLineEdit("Entrez votre message ici...", &mainWindow);
        lineEdit->setStyleSheet(lineEditStyle);
        lineEdit->move(xPos, yPos + labelHeight + 10);
        lineEdit->resize(lineEditWidth, lineEditHeight);
        lineEdit->setEchoMode(EchoModeEnum::NormalEcho);

        SwPushButton* button = new SwPushButton(SwString("Button %1").arg(SwString::number(i)), &mainWindow);
        button->setStyleSheet(buttonStyle);
        button->setCursor(CursorType::Hand);
        button->move(xPos + lineEditWidth + 20, yPos + labelHeight + 10);
        button->resize(buttonWidth, buttonHeight);
        swDebug() << button->classHierarchy();

        SwObject::connect(button, &SwPushButton::clicked, []() {
            std::cout << "*********Button Clicked**********" << std::endl;
        });
    }

    int exitCode = app.exec();
    std::cout << "[SwGuiApplication] exec returned " << exitCode << std::endl;
    return exitCode;
}
