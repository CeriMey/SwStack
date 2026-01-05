#include "ui/SwCreatorMainPanel.h"
#include "ui/SwCreatorFormCanvas.h"

#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwScrollArea.h"
#include "SwToolBox.h"
#include "SwWidgetSnapshot.h"

namespace {
template <typename T>
T* findDescendantOfType(SwObject* root) {
    if (!root) {
        return nullptr;
    }
    if (auto* casted = dynamic_cast<T*>(root)) {
        return casted;
    }
    for (SwObject* child : root->getChildren()) {
        if (auto* found = findDescendantOfType<T>(child)) {
            return found;
        }
    }
    return nullptr;
}

template <typename T>
T* findAncestorOfType(SwObject* start) {
    SwObject* it = start;
    while (it) {
        if (auto* casted = dynamic_cast<T*>(it)) {
            return casted;
        }
        it = it->parent();
    }
    return nullptr;
}
} // namespace

int main(int argc, char** argv) {
    SwGuiApplication app;

    SwMainWindow window(L"SwCreator Snapshot", 1480, 880);
    window.resize(1480, 880);
    window.setStyleSheet("SwMainWindow { background-color: rgb(226, 232, 240); }");

    auto* panel = new SwCreatorMainPanel(window.centralWidget());
    {
        auto* layout = new SwVerticalLayout(window.centralWidget());
        layout->setMargin(0);
        layout->setSpacing(0);
        layout->addWidget(panel, 1, 0);
        window.setLayout(layout);
    }

    if (auto* bar = window.menuBar()) {
        (void)bar->addMenu("File");
        (void)bar->addMenu("Edit");
        (void)bar->addMenu("View");
    }

    if (auto* c = panel->canvas()) {
        SwRect r = c->getRect();

        SwWidget* intro = c->createWidgetAt("SwLabel", r.x + 80, r.y + 70);
        if (intro) {
            intro->setProperty("Text", SwAny(SwString("Layout container demo")));
        }

        SwWidget* layoutBox = c->createLayoutContainerAt("SwVerticalLayout", r.x + 340, r.y + 140);
        if (layoutBox) {
            const SwRect lr = layoutBox->getRect();
            SwWidget* title = c->createWidgetAt("SwLabel", lr.x + 20, lr.y + 20);
            if (title) {
                title->setProperty("Text", SwAny(SwString("Inside layout")));
            }
            SwWidget* edit = c->createWidgetAt("SwLineEdit", lr.x + 20, lr.y + 60);
            if (edit) {
                edit->setProperty("Text", SwAny(SwString("Hello")));
            }
            SwWidget* btn = c->createWidgetAt("SwPushButton", lr.x + 20, lr.y + 110);
            if (btn) {
                btn->setProperty("Text", SwAny(SwString("Validate")));
            }
        }
    }

    SwString outDir;
    if (argc > 1 && argv[1]) {
        outDir = SwString(argv[1]);
    } else {
        outDir = SwString("d:/coreSwExample/build-codex-sanity/");
    }
    if (!outDir.endsWith("/") && !outDir.endsWith("\\")) {
        outDir += "/";
    }

    const bool okWindow = SwWidgetSnapshot::savePng(&window, outDir + "swcreator_window.png");
    const bool okCanvas = panel && panel->canvas() ? SwWidgetSnapshot::savePng(panel->canvas(), outDir + "swcreator_canvas.png") : false;

    bool okPaletteStates = true;
    if (auto* paletteToolBox = findDescendantOfType<SwToolBox>(panel)) {
        SwWidget* paletteShotTarget = paletteToolBox;
        if (auto* scroll = findAncestorOfType<SwScrollArea>(paletteToolBox)) {
            paletteShotTarget = scroll;
        }

        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox.png");

        for (int i = 0; i < paletteToolBox->count(); ++i) {
            paletteToolBox->setItemExpanded(i, true);
        }
        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox_all_open.png");

        for (int i = 0; i < paletteToolBox->count(); ++i) {
            paletteToolBox->setItemExpanded(i, false);
        }
        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox_all_closed.png");

        if (paletteToolBox->count() > 0) {
            paletteToolBox->setItemExpanded(paletteToolBox->count() - 1, true);
        }
        okPaletteStates = okPaletteStates && SwWidgetSnapshot::savePng(paletteShotTarget, outDir + "swcreator_palette_toolbox_last_open.png");
    }

    return (okWindow && okCanvas && okPaletteStates) ? 0 : 1;
}
