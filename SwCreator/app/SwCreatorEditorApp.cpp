#include "editor/SwCreatorEditorPanel.h"

#include "SwDir.h"
#include "SwGuiApplication.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwShortcut.h"
#include "core/runtime/SwCrashHandler.h"

namespace {

SwString initialPath_(int argc, char** argv) {
    if (argc > 1 && argv && argv[1]) {
        return SwString(argv[1]);
    }
    return SwDir::currentPath();
}

void updateWindowTitle_(SwMainWindow* window, SwCreatorEditorPanel* panel) {
    if (!window || !panel) {
        return;
    }
    window->setWindowTitle(panel->windowTitle().toStdWString());
}

} // namespace

int main(int argc, char** argv) {
    SwCrashHandler::install("SwCreatorEditor");
    SwGuiApplication app;

    SwMainWindow mainWindow(L"SwCreatorEditor", 1480, 900);
    SwCreatorEditorPanel* panel = new SwCreatorEditorPanel(mainWindow.centralWidget());

    SwVerticalLayout* layout = new SwVerticalLayout(mainWindow.centralWidget());
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(panel, 1, 0);
    mainWindow.setLayout(layout);

    if (SwMenuBar* bar = mainWindow.menuBar()) {
        SwMenu* fileMenu = bar->addMenu("File");
        if (fileMenu) {
            fileMenu->addAction("Open File...", [&mainWindow, panel]() { panel->openFileDialog(&mainWindow); });
            fileMenu->addAction("Open Folder...", [&mainWindow, panel]() { panel->openFolderDialog(&mainWindow); });
            fileMenu->addSeparator();
            fileMenu->addAction("Save", [panel]() { (void)panel->saveCurrent(); });
            fileMenu->addAction("Save All", [panel]() { (void)panel->saveAll(); });
            fileMenu->addAction("Close Tab", [panel]() { (void)panel->closeCurrentFile(); });
            fileMenu->addSeparator();
            fileMenu->addAction("Reload Explorer", [panel]() { panel->reloadExplorer(); });
        }
    }

    SwShortcut* saveShortcut = new SwShortcut(SwKeySequence::fromString("Ctrl+S"), &mainWindow);
    SwObject::connect(saveShortcut, &SwShortcut::activated, panel, [panel]() { (void)panel->saveCurrent(); });

    SwShortcut* saveAllShortcut = new SwShortcut(SwKeySequence::fromString("Ctrl+Shift+S"), &mainWindow);
    SwObject::connect(saveAllShortcut, &SwShortcut::activated, panel, [panel]() { (void)panel->saveAll(); });

    SwShortcut* closeShortcut = new SwShortcut(SwKeySequence::fromString("Ctrl+W"), &mainWindow);
    SwObject::connect(closeShortcut, &SwShortcut::activated, panel, [panel]() { (void)panel->closeCurrentFile(); });

    SwShortcut* reloadShortcut = new SwShortcut(SwKeySequence::fromString("F5"), &mainWindow);
    SwObject::connect(reloadShortcut, &SwShortcut::activated, panel, [panel]() { panel->reloadExplorer(); });

    SwObject::connect(panel, &SwCreatorEditorPanel::stateChanged, &mainWindow, [&mainWindow, panel]() {
        updateWindowTitle_(&mainWindow, panel);
    });

    panel->openPath(initialPath_(argc, argv));
    updateWindowTitle_(&mainWindow, panel);

    mainWindow.showMaximized();
    return app.exec();
}
