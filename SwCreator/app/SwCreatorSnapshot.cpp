#include "ui/SwCreatorShell.h"
#include "ui/SwCreatorMainPanel.h"
#include "ui/SwCreatorFormCanvas.h"
#include "serialization/SwCreatorQtUiImporter.h"
#include "serialization/SwCreatorSwuiSerializer.h"

#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwScrollArea.h"
#include "SwToolBox.h"
#include "SwWidgetSnapshot.h"
#include "core/io/SwFile.h"

namespace {
template <typename T>
T* findDescendantOfType(SwObject* root) {
    if (!root) return nullptr;
    if (auto* casted = dynamic_cast<T*>(root)) return casted;
    for (SwObject* child : root->children()) {
        if (auto* found = findDescendantOfType<T>(child)) return found;
    }
    return nullptr;
}

template <typename T>
T* findAncestorOfType(SwObject* start) {
    SwObject* it = start;
    while (it) {
        if (auto* casted = dynamic_cast<T*>(it)) return casted;
        it = it->parent();
    }
    return nullptr;
}
} // namespace

int main(int argc, char** argv) {
    SwGuiApplication app;

    SwMainWindow window(L"SwCreator Snapshot", 1920, 1080);
    window.resize(1920, 1080);
    window.setStyleSheet("SwMainWindow { background-color: rgb(240, 242, 245); }");

    auto* shell = new SwCreatorShell(window.centralWidget());
    auto* panel = shell ? shell->creatorPanel() : nullptr;
    {
        auto* layout = new SwVerticalLayout(window.centralWidget());
        layout->setMargin(0);
        layout->setSpacing(0);
        layout->addWidget(shell, 1, 0);
        window.setLayout(layout);
    }

    if (shell) {
        shell->showCreatorPage();
    }

    if (auto* bar = window.menuBar()) {
        (void)bar->addMenu("File");
        (void)bar->addMenu("Edit");
        (void)bar->addMenu("View");
    }

    SwString inputFile;
    SwString outDir;

    if (argc > 1 && argv[1]) {
        inputFile = SwString(argv[1]);
    }
    if (argc > 2 && argv[2]) {
        outDir = SwString(argv[2]);
    }

    if (!inputFile.isEmpty()) {
        SwFile f(inputFile);
        if (f.open(SwFile::Read)) {
            const SwString content = f.readAll();
            f.close();

            if (!content.isEmpty() && panel && panel->canvas()) {
                SwString error;
                const bool isQtUi = inputFile.endsWith(".ui") || content.contains("<ui version");
                if (isQtUi) {
                    const SwString swuiXml = SwCreatorQtUiImporter::importFromQtUi(content, &error);
                    if (!swuiXml.isEmpty()) {
                        SwFile dump(outDir.isEmpty() ? SwString(".") + "/imported_mainwindow.swui"
                                                     : outDir + (outDir.endsWith("/") || outDir.endsWith("\\") ? "" : "/") + "imported_mainwindow.swui");
                        if (dump.open(SwFile::Write)) {
                            dump.write(swuiXml);
                            dump.close();
                        }
                        SwCreatorSwuiSerializer::deserializeCanvas(swuiXml, panel->canvas(), &error);
                    }
                } else {
                    SwCreatorSwuiSerializer::deserializeCanvas(content, panel->canvas(), &error);
                }
            }
        }
    } else {
        if (panel) {
            if (auto* c = panel->canvas()) {
                SwRect r = c->frameGeometry();
                SwWidget* intro = c->createWidgetAt("SwLabel", r.x + 80, r.y + 70);
                if (intro) intro->setProperty("Text", SwAny(SwString("Layout container demo")));

                SwWidget* layoutBox = c->createLayoutContainerAt("SwVerticalLayout", r.x + 340, r.y + 140);
                if (layoutBox) {
                    const SwRect lr = layoutBox->frameGeometry();
                    SwWidget* title = c->createWidgetAt("SwLabel", lr.x + 20, lr.y + 20);
                    if (title) title->setProperty("Text", SwAny(SwString("Inside layout")));
                    SwWidget* edit = c->createWidgetAt("SwLineEdit", lr.x + 20, lr.y + 60);
                    if (edit) edit->setProperty("Text", SwAny(SwString("Hello")));
                    SwWidget* btn = c->createWidgetAt("SwPushButton", lr.x + 20, lr.y + 110);
                    if (btn) btn->setProperty("Text", SwAny(SwString("Validate")));
                }
            }
        }
    }

    if (outDir.isEmpty()) {
        if (!inputFile.isEmpty()) {
            SwFile f(inputFile);
            outDir = f.getDirectory();
            if (outDir.isEmpty()) outDir = SwString(".");
        } else {
            outDir = SwString(".");
        }
    }
    if (!outDir.endsWith("/") && !outDir.endsWith("\\")) {
        outDir += "/";
    }

    window.show();
    app.exec(50000);
    app.exec(50000);

    const bool okWindow = SwWidgetSnapshot::savePng(&window, outDir + "swcreator_window.png");

    if (panel) {
        panel->setSidebarsVisible(false);
    }
    app.exec(30000);

    const bool okCanvas = (panel && panel->canvas())
                        ? SwWidgetSnapshot::savePng(panel->canvas(), outDir + "swcreator_canvas.png")
                        : false;

    bool okCanvasDebug = false;
    if (panel && panel->canvas()) {
        panel->canvas()->setDebugOverlay(true);
        okCanvasDebug = SwWidgetSnapshot::savePng(panel->canvas(), outDir + "swcreator_canvas_debug.png");
        panel->canvas()->setDebugOverlay(false);
    }

    if (panel) {
        panel->setSidebarsVisible(true);
    }
    app.exec(50000);

    bool okPaletteStates = true;
    if (auto* paletteToolBox = findDescendantOfType<SwToolBox>(shell)) {
        SwWidget* paletteShotTarget = paletteToolBox;
        if (auto* scroll = findAncestorOfType<SwScrollArea>(paletteToolBox)) {
            paletteShotTarget = scroll;
        }

        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox.png");

        for (int i = 0; i < paletteToolBox->count(); ++i) paletteToolBox->setItemExpanded(i, true);
        app.exec(50000);
        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox_all_open.png");

        for (int i = 0; i < paletteToolBox->count(); ++i) paletteToolBox->setItemExpanded(i, false);
        app.exec(50000);
        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox_all_closed.png");

        if (paletteToolBox->count() > 0) paletteToolBox->setItemExpanded(paletteToolBox->count() - 1, true);
        paletteToolBox->refreshLayout();
        app.exec(50000);
        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox_last_open.png");
    }

    return (okWindow && okCanvas && okCanvasDebug && okPaletteStates) ? 0 : 1;
}
