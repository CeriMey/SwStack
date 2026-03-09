#include "SwCreatorDocument.h"
#include "SwCreatorController.h"
#include "editor/SwCreatorEditorPanel.h"
#include "ui/SwCreatorShell.h"
#include "SwDir.h"
#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "core/runtime/SwCrashHandler.h"

namespace {

SwString initialEditorPath_(int argc, char** argv) {
    if (argc > 1 && argv && argv[1]) {
        return SwString(argv[1]);
    }
    return SwDir::currentPath();
}

} // namespace

int main(int argc, char** argv) {
    SwCrashHandler::install("SwCreator");
    SwGuiApplication app;

    SwMainWindow mainWindow(L"SwCreator", 1480, 880);
    auto* shell = new SwCreatorShell(mainWindow.centralWidget());
    {
        auto* layout = new SwVerticalLayout(mainWindow.centralWidget());
        layout->setMargin(0);
        layout->setSpacing(0);
        layout->addWidget(shell, 1, 0);
        mainWindow.setLayout(layout);
    }

    SwCreatorDocument doc;
    SwCreatorController ctrl(&mainWindow, shell, &doc);
    ctrl.setup();
    if (shell && shell->editorPanel()) {
        shell->editorPanel()->openPath(initialEditorPath_(argc, argv));
        if (argc > 1 && argv && argv[1]) {
            shell->showEditorPage();
        }
    }

    mainWindow.showMaximized();
    return app.exec();
}
