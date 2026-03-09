#include "SwCreatorController.h"
#include "SwCreatorDocument.h"

#include "editor/SwCreatorEditorPanel.h"
#include "designer/SwCreatorMainPanel.h"
#include "designer/SwCreatorFormCanvas.h"
#include "ui/SwCreatorShell.h"

#include "SwMainWindow.h"
#include "SwShortcut.h"
#include "SwMessageBox.h"
#include "core/runtime/SwCrashHandler.h"

namespace {

SwCreatorEditorPanel* editorPanel_(SwCreatorShell* shell) {
    return shell ? shell->editorPanel() : nullptr;
}

void updateWindowTitle_(SwMainWindow* window, SwCreatorShell* shell, SwCreatorDocument* doc) {
    if (!window || !doc) {
        return;
    }

    if (shell && shell->isEditorPageActive()) {
        if (SwCreatorEditorPanel* panel = editorPanel_(shell)) {
            window->setWindowTitle(panel->windowTitle().toStdWString());
            return;
        }
    }

    window->setWindowTitle(doc->windowTitle().toStdWString());
}

void openEditorFileDialog_(SwMainWindow* window, SwCreatorShell* shell) {
    if (!window || !shell) {
        return;
    }
    if (SwCreatorEditorPanel* panel = editorPanel_(shell)) {
        if (panel->openFileDialog(window)) {
            shell->showEditorPage();
        }
    }
}

void openEditorFolderDialog_(SwMainWindow* window, SwCreatorShell* shell) {
    if (!window || !shell) {
        return;
    }
    if (SwCreatorEditorPanel* panel = editorPanel_(shell)) {
        if (panel->openFolderDialog(window)) {
            shell->showEditorPage();
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SwCreatorController::SwCreatorController(SwMainWindow* window,
                                         SwCreatorShell* shell,
                                         SwCreatorDocument* doc,
                                         SwObject* parent)
    : SwObject(parent)
    , m_window(window)
    , m_shell(shell)
    , m_panel(shell ? shell->creatorPanel() : nullptr)
    , m_doc(doc)
{}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void SwCreatorController::setup()
{
    SwCreatorFormCanvas* canvas = m_panel ? m_panel->canvas() : nullptr;
    auto openCurrent = [this, canvas]() {
        if (m_shell && m_shell->isEditorPageActive()) {
            openEditorFileDialog_(m_window, m_shell);
            return;
        }
        m_doc->openFile(canvas, m_window);
    };
    auto saveCurrent = [this, canvas]() {
        if (m_shell && m_shell->isEditorPageActive()) {
            if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
                (void)panel->saveCurrent();
            }
            return;
        }
        (void)m_doc->save(canvas, m_window);
    };
    auto saveAlternate = [this, canvas]() {
        if (m_shell && m_shell->isEditorPageActive()) {
            if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
                (void)panel->saveAll();
            }
            return;
        }
        (void)m_doc->saveAs(canvas, m_window);
    };
    auto closeCurrent = [this, canvas]() {
        if (m_shell && m_shell->isEditorPageActive()) {
            if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
                (void)panel->closeCurrentFile();
            }
            return;
        }
        m_doc->closeFile(canvas, m_window);
    };

    // -- Signal wiring -------------------------------------------------------

    // Any document-level edit from the panel marks the document dirty.
    SwObject::connect(m_panel, &SwCreatorMainPanel::documentModified,
                      m_doc, [this]() { m_doc->markDirty(); });

    // Hook move/resize tracking for every widget placed on the canvas.
    if (canvas) {
        SwObject::connect(canvas, &SwCreatorFormCanvas::widgetAdded,
                          m_doc, [this](SwWidget* w) { m_doc->hookWidget(w); });
        SwObject::connect(canvas, &SwWidget::resized,
                          m_doc, [this](int, int) { m_doc->markDirty(); });
    }

    // Keep the window title in sync with document state.
    SwObject::connect(m_doc, &SwCreatorDocument::titleChanged,
                      m_window, [this](const SwString&) {
                          updateWindowTitle_(m_window, m_shell, m_doc);
                      });
    if (m_shell) {
        SwObject::connect(m_shell, &SwCreatorShell::currentPageChanged,
                          m_window, [this]() { updateWindowTitle_(m_window, m_shell, m_doc); });
        if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
            SwObject::connect(panel, &SwCreatorEditorPanel::stateChanged,
                              m_window, [this]() { updateWindowTitle_(m_window, m_shell, m_doc); });
        }
    }

    // -- File menu -----------------------------------------------------------
    if (auto* bar = m_window->menuBar()) {
        SwMenu* fileMenu = bar->addMenu("File");
        if (fileMenu) {
            fileMenu->addAction("Open...", openCurrent);
            fileMenu->addAction("Open Folder...", [this]() {
                openEditorFolderDialog_(m_window, m_shell);
            });
            fileMenu->addAction("Close", closeCurrent);
            fileMenu->addSeparator();
            fileMenu->addAction("Save", saveCurrent);
            fileMenu->addAction("Save All", [this]() {
                if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
                    (void)panel->saveAll();
                }
            });
            fileMenu->addAction("Save UI As...", [this, canvas]() {
                (void)m_doc->saveAs(canvas, m_window);
            });
            fileMenu->addSeparator();
            fileMenu->addAction("Reload Explorer", [this]() {
                if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
                    panel->reloadExplorer();
                }
            });
        }
    }

    // -- Keyboard shortcuts --------------------------------------------------
    {
        auto* scOpen = new SwShortcut(SwKeySequence::fromString("Ctrl+O"), m_window);
        SwObject::connect(scOpen, &SwShortcut::activated, m_window, openCurrent);

        auto* scOpenFolder = new SwShortcut(SwKeySequence::fromString("Ctrl+Shift+O"), m_window);
        SwObject::connect(scOpenFolder, &SwShortcut::activated, m_window, [this]() {
            openEditorFolderDialog_(m_window, m_shell);
        });

        auto* scSave = new SwShortcut(SwKeySequence::fromString("Ctrl+S"), m_window);
        SwObject::connect(scSave, &SwShortcut::activated, m_window, saveCurrent);

        auto* scSaveAs = new SwShortcut(SwKeySequence::fromString("Ctrl+Shift+S"), m_window);
        SwObject::connect(scSaveAs, &SwShortcut::activated, m_window, saveAlternate);

        auto* scClose = new SwShortcut(SwKeySequence::fromString("Ctrl+W"), m_window);
        SwObject::connect(scClose, &SwShortcut::activated, m_window, closeCurrent);

        auto* scReload = new SwShortcut(SwKeySequence::fromString("F5"), m_window);
        SwObject::connect(scReload, &SwShortcut::activated, m_window, [this]() {
            if (SwCreatorEditorPanel* panel = editorPanel_(m_shell)) {
                panel->reloadExplorer();
            }
        });
    }

    // Emit the initial title so the window reflects the clean state.
    updateWindowTitle_(m_window, m_shell, m_doc);

    // -- Crash report display ------------------------------------------------
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
        SwMessageBox box(m_window);
        box.setWindowTitle("Crash report");
        box.setText(text);
        box.setInformativeText(details.trimmed());
        box.setStandardButtons(SwMessageBox::Ok);
        (void)box.exec();
    }
}
