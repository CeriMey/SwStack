#include "SwCreatorDocument.h"
#include "SwCreatorController.h"
#include "designer/SwCreatorMainPanel.h"
#include "editor/SwCreatorEditorPanel.h"
#include "theme/SwCreatorTheme.h"
#include "ui/SwCreatorShell.h"
#include "SwDir.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwMainWindow.h"
#include "SwStatusBar.h"
#include "core/runtime/SwCrashHandler.h"

#include <cstdlib>
#include <fstream>

namespace {

SwString initialEditorPath_(int argc, char** argv) {
    if (argc > 1 && argv && argv[1]) {
        return SwString(argv[1]);
    }
    return SwDir::currentPath();
}

bool shouldDumpMinHints_() {
    char* env = nullptr;
    size_t envLen = 0;
    const errno_t err = _dupenv_s(&env, &envLen, "SW_CREATOR_DUMP_MINHINTS");
    const bool enabled = (err == 0) && env && env[0] != '\0' && env[0] != '0';
    free(env);
    return enabled;
}

void dumpMinHints_(const SwMainWindow& mainWindow, const SwCreatorShell* shell) {
    if (!shouldDumpMinHints_() || !shell) {
        return;
    }

    std::ofstream out("swcreator_minhint_dump.txt", std::ios::trunc);
    if (!out) {
        return;
    }

    const SwWidget* central = mainWindow.centralWidget();
    const SwCreatorMainPanel* creator = shell->creatorPanel();
    const SwCreatorEditorPanel* editor = shell->editorPanel();

    auto dumpSize = [&out](const char* label, const SwSize& size) {
        out << label << '=' << size.width << 'x' << size.height << '\n';
    };

    dumpSize("mainWindow.minimumSizeHint", mainWindow.minimumSizeHint());
    dumpSize("central.minimumSizeHint", central ? central->minimumSizeHint() : SwSize{0, 0});
    dumpSize("shell.minimumSizeHint", shell->minimumSizeHint());
    dumpSize("creator.minimumSizeHint", creator ? creator->minimumSizeHint() : SwSize{0, 0});
    dumpSize("editor.minimumSizeHint", editor ? editor->minimumSizeHint() : SwSize{0, 0});
    out << "shell.isCreatorPageActive=" << (shell->isCreatorPageActive() ? 1 : 0) << '\n';
    out << "shell.isEditorPageActive=" << (shell->isEditorPageActive() ? 1 : 0) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    SwCrashHandler::install("SwCreator");
    SwGuiApplication app;

    const auto& th = SwCreatorTheme::current();

    SwMainWindow mainWindow(L"SwCreator", 1480, 880);

    auto* shell = new SwCreatorShell(mainWindow.centralWidget());

    auto* statusBar = mainWindow.statusBar();
    statusBar->setStyleSheet(
        "SwStatusBar {"
        " background-color: " + SwCreatorTheme::rgb(th.surface0)
        + "; border-color: " + SwCreatorTheme::rgb(th.borderLight)
        + "; border-width: 0px; border-radius: 0px;"
        " color: " + SwCreatorTheme::rgb(th.textOnSurface0) + ";"
        " padding: 0px 8px; }");
    auto* statusLeft = new SwLabel("SwCreator", statusBar);
    statusLeft->setFont(th.uiCaption);
    statusLeft->setStyleSheet(
        "SwLabel { color: " + SwCreatorTheme::rgb(th.textMuted) + "; border-width: 0px; }");
    statusBar->addPermanentWidget(statusLeft);

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

    // Update status bar when the shell page or editor state changes
    auto updateStatus = [&]() {
        SwString text = "SwCreator";
        if (shell->isEditorPageActive() && shell->editorPanel()) {
            const SwString file = shell->editorPanel()->currentFilePath();
            if (!file.isEmpty()) {
                text = file;
            }
        } else if (shell->isCreatorPageActive()) {
            text = "Designer";
        }
        statusBar->showMessage(text);
    };

    SwObject::connect(shell, &SwCreatorShell::currentPageChanged, statusBar, updateStatus);
    if (shell->editorPanel()) {
        SwObject::connect(shell->editorPanel(), &SwCreatorEditorPanel::stateChanged, statusBar, updateStatus);
    }

    if (shell && shell->editorPanel()) {
        shell->editorPanel()->openPath(initialEditorPath_(argc, argv));
        if (argc > 1 && argv && argv[1]) {
            shell->showEditorPage();
        }
    }

    updateStatus();
    dumpMinHints_(mainWindow, shell);
    mainWindow.showMaximized();
    return app.exec();
}
