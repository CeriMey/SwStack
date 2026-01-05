#include "SwGuiApplication.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwLabel.h"
#include "SwWidgetSnapshot.h"

#include "src/SwNodeEditorView.hpp"

#include <algorithm>
#include <cctype>
#include <string>

static std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

int main(int argc, char** argv) {
    SwGuiApplication app;

    SwMainWindow window(L"NodeEditor (Sw)", 1040, 720);
    window.resize(1040, 720);
    window.setStyleSheet("SwMainWindow { background-color: rgb(241, 245, 249); }");

    auto* title = new SwLabel("NodeEditor (Sw) - drag nodes, drag from output port to connect, double-click empty to add nodes",
                              &window);
    title->resize(1000, 28);
    title->setStyleSheet(R"(
        SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(15, 23, 42); font-size: 14px; }
    )");

    auto* view = new swnodeeditor::SwNodeEditorView(&window);

    auto* layout = new SwVerticalLayout(&window);
    layout->setMargin(20);
    layout->setSpacing(12);
    window.setLayout(layout);
    layout->addWidget(title, 0, 28);
    layout->addWidget(view, 1, 0);

    view->populateDemoGraph();

    SwString out;
    if (argc > 1 && argv[1]) {
        out = SwString(argv[1]);
    }
    if (!out.isEmpty()) {
        const bool outIsDir = (out.endsWith("/") || out.endsWith("\\"));
        std::string arg2 = (argc > 2 && argv[2]) ? std::string(argv[2]) : std::string();
        const std::string arg2Lower = toLowerAscii(arg2);
        const bool suiteMode = outIsDir && (arg2Lower == "suite" || arg2Lower == "all");

        if (suiteMode) {
            bool ok = true;

            view->setScale(1.0);
            view->setScroll(0.0, 0.0);
            ok = ok && SwWidgetSnapshot::savePng(&window, out + "node_editor.png");

            view->setScale(0.10);
            view->setScroll(0.0, 0.0);
            ok = ok && SwWidgetSnapshot::savePng(&window, out + "node_editor_zoom_out.png");

            view->setScale(2.0);
            view->setScroll(0.0, 0.0);
            ok = ok && SwWidgetSnapshot::savePng(&window, out + "node_editor_zoom_in.png");

            return ok ? 0 : 1;
        }

        if (outIsDir) {
            out.append("node_editor.png");
        }

        const bool ok = SwWidgetSnapshot::savePng(&window, out);
        return ok ? 0 : 1;
    }

    window.show();
    return app.exec();
}
