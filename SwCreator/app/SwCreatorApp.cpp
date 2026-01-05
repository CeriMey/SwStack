#include "ui/SwCreatorMainPanel.h"
#include "ui/SwCreatorFormCanvas.h"
#include "serialization/SwCreatorSwuiSerializer.h"

#include "SwGuiApplication.h"
#include "SwFileDialog.h"
#include "SwMainWindow.h"

#include "core/runtime/SwCrashHandler.h"
#include "core/io/SwFile.h"
#include "SwShortcut.h"
#include "SwMessageBox.h"

#include <unordered_set>
#include <vector>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    SwCrashHandler::install("SwCreator");

    SwGuiApplication app;

    SwMainWindow mainWindow(L"SwCreator", 1480, 880);

    auto* panel = new SwCreatorMainPanel(mainWindow.centralWidget());
    {
        auto* layout = new SwVerticalLayout(mainWindow.centralWidget());
        layout->setMargin(0);
        layout->setSpacing(0);
        layout->addWidget(panel, 1, 0);
        mainWindow.setLayout(layout);
    }

    SwString currentPath;
    bool dirty = false;
    bool suppressDirty = false;

    auto updateTitle = [&]() {
        SwString title("SwCreator");
        if (!currentPath.isEmpty()) {
            SwFile f(currentPath);
            title = "SwCreator - " + f.fileName();
        }
        if (dirty) {
            title += "*";
        }
        mainWindow.setWindowTitle(title.toStdWString());
    };

    auto markDirty = [&]() {
        if (suppressDirty || dirty) {
            return;
        }
        dirty = true;
        updateTitle();
    };

    std::unordered_set<SwWidget*> hookedWidgets;
    auto hookMoveResize = [&](SwWidget* w) {
        if (!w) {
            return;
        }
        if (hookedWidgets.find(w) != hookedWidgets.end()) {
            return;
        }
        hookedWidgets.insert(w);

        SwObject::connect(w, &SwWidget::moved, &mainWindow, [&](int, int) { markDirty(); });
        SwObject::connect(w, &SwWidget::resized, &mainWindow, [&](int, int) { markDirty(); });
    };

    auto clearCanvas = [&]() {
        auto* c = panel ? panel->canvas() : nullptr;
        if (!c) {
            return;
        }
        hookedWidgets.clear();
        std::vector<SwWidget*> copy;
        const auto& widgets = c->designWidgets();
        copy.reserve(widgets.size());
        for (SwWidget* w : widgets) {
            if (w) {
                copy.push_back(w);
            }
        }
        for (SwWidget* w : copy) {
            c->removeDesignWidget(w);
        }
    };

    auto saveToPath = [&](const SwString& path) -> bool {
        auto* c = panel ? panel->canvas() : nullptr;
        if (!c) {
            return false;
        }
        const SwString xml = SwCreatorSwuiSerializer::serializeCanvas(c);
        if (xml.isEmpty()) {
            SwMessageBox::information(&mainWindow, "Save", "Nothing to save.");
            return false;
        }

        SwFile f(path);
        if (!f.open(SwFile::Write)) {
            SwMessageBox::information(&mainWindow, "Save", "Failed to open file for writing.");
            return false;
        }
        const bool ok = f.write(xml);
        f.close();
        if (!ok) {
            SwMessageBox::information(&mainWindow, "Save", "Failed to write file.");
            return false;
        }

        currentPath = path;
        dirty = false;
        updateTitle();
        return true;
    };

    auto saveAs = [&]() -> bool {
        SwString startDir;
        if (!currentPath.isEmpty()) {
            SwFile f(currentPath);
            startDir = f.getDirectory();
        }
        const SwString path = SwFileDialog::getSaveFileName(&mainWindow,
                                                            "Save As...",
                                                            startDir,
                                                            "SwUI (*.swui);;All files (*)");
        if (path.isEmpty()) {
            return false;
        }
        return saveToPath(path);
    };

    auto save = [&]() -> bool {
        if (currentPath.isEmpty()) {
            return saveAs();
        }
        return saveToPath(currentPath);
    };

    auto maybeSave = [&]() -> bool {
        if (!dirty) {
            return true;
        }

        SwMessageBox box(&mainWindow);
        box.setWindowTitle("Unsaved changes");
        box.setText("Save changes before continuing?");
        box.setStandardButtons(SwMessageBox::Yes | SwMessageBox::No | SwMessageBox::Cancel);
        (void)box.exec();
        const int clicked = box.clickedButton();
        if (clicked == SwMessageBox::NoButton || clicked == SwMessageBox::Cancel) {
            return false;
        }
        if (clicked == SwMessageBox::Yes) {
            return save();
        }
        return true; // No => discard
    };

    auto openFile = [&]() {
        if (!maybeSave()) {
            return;
        }

        SwString startDir;
        if (!currentPath.isEmpty()) {
            SwFile f(currentPath);
            startDir = f.getDirectory();
        }
        const SwString path = SwFileDialog::getOpenFileName(&mainWindow,
                                                            "Open...",
                                                            startDir,
                                                            "SwUI (*.swui);;All files (*)");
        if (path.isEmpty()) {
            return;
        }

        SwFile f(path);
        if (!f.open(SwFile::Read)) {
            SwMessageBox::information(&mainWindow, "Open", "Failed to open file.");
            return;
        }
        const SwString content = f.readAll();
        f.close();
        if (content.isEmpty()) {
            SwMessageBox::information(&mainWindow, "Open", "File is empty.");
            return;
        }

        suppressDirty = true;
        clearCanvas();

        SwString error;
        if (!SwCreatorSwuiSerializer::deserializeCanvas(content, panel->canvas(), &error)) {
            suppressDirty = false;
            SwMessageBox::information(&mainWindow, "Open", error.isEmpty() ? "Failed to load file." : error);
            return;
        }

        if (auto* c = panel->canvas()) {
            const auto& widgets = c->designWidgets();
            for (SwWidget* w : widgets) {
                hookMoveResize(w);
            }
        }

        currentPath = path;
        dirty = false;
        updateTitle();
        suppressDirty = false;
    };

    auto closeFile = [&]() {
        if (!maybeSave()) {
            return;
        }
        suppressDirty = true;
        clearCanvas();
        currentPath = SwString();
        dirty = false;
        updateTitle();
        suppressDirty = false;
    };

    // Mark dirty on any document-level change (widgets + inspector edits).
    SwObject::connect(panel, &SwCreatorMainPanel::documentModified, &mainWindow, [&]() { markDirty(); });

    // Track geometry edits (drag/resize + inspector x/y/width/height).
    if (auto* c = panel->canvas()) {
        SwObject::connect(c, &SwCreatorFormCanvas::widgetAdded, &mainWindow, [&](SwWidget* w) { hookMoveResize(w); });
    }

    // Menu: File
    if (auto* bar = mainWindow.menuBar()) {
        SwMenu* fileMenu = bar->addMenu("File");
        if (fileMenu) {
            fileMenu->addAction("Open...", [openFile]() { openFile(); });
            fileMenu->addAction("Close", [closeFile]() { closeFile(); });
            fileMenu->addSeparator();
            fileMenu->addAction("Save", [save]() { (void)save(); });
            fileMenu->addAction("Save As...", [saveAs]() { (void)saveAs(); });
        }
    }

    // Shortcuts
    {
        auto* scOpen = new SwShortcut(SwKeySequence::fromString("Ctrl+O"), &mainWindow);
        SwObject::connect(scOpen, &SwShortcut::activated, &mainWindow, [openFile]() { openFile(); });

        auto* scSave = new SwShortcut(SwKeySequence::fromString("Ctrl+S"), &mainWindow);
        SwObject::connect(scSave, &SwShortcut::activated, &mainWindow, [save]() { (void)save(); });

        auto* scSaveAs = new SwShortcut(SwKeySequence::fromString("Ctrl+Shift+S"), &mainWindow);
        SwObject::connect(scSaveAs, &SwShortcut::activated, &mainWindow, [saveAs]() { (void)saveAs(); });

        auto* scClose = new SwShortcut(SwKeySequence::fromString("Ctrl+W"), &mainWindow);
        SwObject::connect(scClose, &SwShortcut::activated, &mainWindow, [closeFile]() { closeFile(); });
    }

    updateTitle();

    mainWindow.show();

    SwCrashReport report;
    if (SwCrashHandler::takeLastCrashReport(report)) {
        SwString text = "A crash report was generated in:\n" + report.crashDir;
        SwString details;
        if (!report.timestamp.isEmpty()) {
            details += "Time: " + report.timestamp + "\n";
        }
        if (!report.logPath.isEmpty()) {
            details += "Log: " + report.logPath + "\n";
        }
        if (!report.dumpPath.isEmpty()) {
            details += "Dump: " + report.dumpPath + "\n";
        }
        SwMessageBox box(&mainWindow);
        box.setWindowTitle("Crash report");
        box.setText(text);
        box.setInformativeText(details.trimmed());
        box.setStandardButtons(SwMessageBox::Ok);
        (void)box.exec();
    }

    return app.exec();
}
