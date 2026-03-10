#include "editor/SwCreatorEditorPanel.h"

#include "editor/SwCreatorEditorPaths.h"
#include "editor/SwCreatorEditorTabWidget.h"
#include "theme/SwCreatorTheme.h"

#include "SwDir.h"
#include "SwFileDialog.h"
#include "SwFileExplorer.h"
#include "SwFileInfo.h"
#include "SwLayout.h"
#include "SwMessageBox.h"
#include "SwSplitter.h"

namespace {

SwWidget* dialogHost_(SwWidget* preferredParent, SwWidget* fallbackParent) {
    SwWidget* current = preferredParent ? preferredParent : fallbackParent;
    SwWidget* root = current;
    while (current) {
        root = current;
        current = dynamic_cast<SwWidget*>(current->parent());
    }
    return root;
}

} // namespace

SwCreatorEditorPanel::SwCreatorEditorPanel(SwWidget* parent)
    : SwWidget(parent) {
    const auto& th = SwCreatorTheme::current();
    setStyleSheet("SwCreatorEditorPanel { background-color: " + SwCreatorTheme::rgb(th.surface1) + "; border-width: 0px; }");

    SwVerticalLayout* layout = new SwVerticalLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);
    setLayout(layout);

    m_splitter = new SwSplitter(SwSplitter::Orientation::Horizontal, this);
    m_splitter->setHandleWidth(4);
    layout->addWidget(m_splitter, 1, 0);

    m_explorer = new SwFileExplorer(m_splitter, false, false);
    m_explorer->setTitle("Explorer");
    m_explorer->setStyleSheet(
        "SwFileExplorer { background-color: " + SwCreatorTheme::rgb(th.surface2) + "; border-width: 0px; }");
    if (m_explorer->treeWidget()) {
        m_explorer->treeWidget()->setStyleSheet(
            "SwTreeWidget { background-color: " + SwCreatorTheme::rgb(th.surface2)
            + "; border-color: " + SwCreatorTheme::rgb(th.borderLight)
            + "; border-width: 0px; border-radius: 0px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + "; }");
    }
    m_tabs = new SwCreatorEditorTabWidget(m_splitter);
    m_tabs->setMinimumSize(420, 0);

    m_splitter->addWidget(m_explorer);
    m_splitter->addWidget(m_tabs);
    m_splitter->setSizes(SwVector<int>{220, 1220});

    SwObject::connect(m_explorer, &SwFileExplorer::pathActivated, this, [this](const SwString& filePath) {
        if (m_tabs && m_tabs->openFile(filePath)) {
            m_explorer->setCurrentPath(filePath);
            emitStateChanged_();
        }
    });
    SwObject::connect(m_tabs, &SwCreatorEditorTabWidget::currentFileChanged, this, [this](const SwString& filePath) {
        if (m_explorer) {
            m_explorer->setCurrentPath(filePath);
        }
        emitStateChanged_();
    });
    SwObject::connect(m_tabs, &SwCreatorEditorTabWidget::dirtyFilesChanged, this, [this]() {
        if (m_explorer) {
            m_explorer->setMarkedPaths(m_tabs ? m_tabs->dirtyFiles() : SwMap<SwString, bool>());
        }
        emitStateChanged_();
    });
}

void SwCreatorEditorPanel::openPath(const SwString& rawPath) {
    const SwString resolvedPath = swCreatorEditorResolveExistingPath(rawPath.isEmpty() ? SwDir::currentPath() : rawPath);
    const SwFileInfo info(resolvedPath.toStdString());

    if (info.exists() && info.isFile()) {
        setRootPath(swCreatorEditorParentPath(resolvedPath));
        if (m_tabs) {
            (void)m_tabs->openFile(resolvedPath);
        }
        if (m_explorer) {
            m_explorer->setCurrentPath(resolvedPath);
        }
        emitStateChanged_();
        return;
    }

    if (info.exists() && info.isDir()) {
        setRootPath(resolvedPath);
        emitStateChanged_();
        return;
    }

    SwMessageBox::warning(this,
                          "Path",
                          SwString("Path not found:\n") + resolvedPath);
    setRootPath(SwDir::currentPath());
    emitStateChanged_();
}

bool SwCreatorEditorPanel::openFileDialog(SwWidget* dialogParent) {
    const SwString startPath = m_rootPath.isEmpty() ? SwDir::currentPath() : m_rootPath;
    const SwString filePath = SwFileDialog::getOpenFileName(dialogHost_(dialogParent, this),
                                                            "Open file",
                                                            startPath,
                                                            "All Files (*.*)");
    if (filePath.isEmpty()) {
        return false;
    }

    openPath(filePath);
    return true;
}

bool SwCreatorEditorPanel::openFolderDialog(SwWidget* dialogParent) {
    const SwString startPath = m_rootPath.isEmpty() ? SwDir::currentPath() : m_rootPath;
    const SwString folderPath = SwFileDialog::getExistingDirectory(dialogHost_(dialogParent, this),
                                                                   "Open folder",
                                                                   startPath);
    if (folderPath.isEmpty()) {
        return false;
    }

    setRootPath(folderPath);
    reloadExplorer();
    return true;
}

void SwCreatorEditorPanel::setRootPath(const SwString& rawRootPath) {
    const SwString normalizedRootPath = swCreatorEditorResolveExistingPath(rawRootPath.isEmpty() ? SwDir::currentPath() : rawRootPath);
    const SwFileInfo info(normalizedRootPath.toStdString());
    if (!info.exists() || !info.isDir()) {
        return;
    }

    m_rootPath = swCreatorEditorNormalizePath(normalizedRootPath);
    if (m_explorer) {
        m_explorer->setRootPath(m_rootPath);
        m_explorer->setMarkedPaths(m_tabs ? m_tabs->dirtyFiles() : SwMap<SwString, bool>());
        m_explorer->setCurrentPath(m_tabs ? m_tabs->currentFilePath() : SwString());
    }
    emitStateChanged_();
}

void SwCreatorEditorPanel::reloadExplorer() {
    if (m_explorer) {
        m_explorer->reload();
        m_explorer->setMarkedPaths(m_tabs ? m_tabs->dirtyFiles() : SwMap<SwString, bool>());
        m_explorer->setCurrentPath(m_tabs ? m_tabs->currentFilePath() : SwString());
    }
    emitStateChanged_();
}

bool SwCreatorEditorPanel::saveCurrent() {
    return m_tabs ? m_tabs->saveCurrent() : true;
}

bool SwCreatorEditorPanel::saveAll() {
    return m_tabs ? m_tabs->saveAll() : true;
}

bool SwCreatorEditorPanel::closeCurrentFile() {
    return m_tabs ? m_tabs->closeCurrentFile() : true;
}

SwString SwCreatorEditorPanel::rootPath() const {
    return m_rootPath;
}

SwString SwCreatorEditorPanel::currentFilePath() const {
    return m_tabs ? m_tabs->currentFilePath() : SwString();
}

SwString SwCreatorEditorPanel::windowTitle() const {
    SwString title("SwCreatorEditor");
    const SwString currentPath = currentFilePath();

    if (!currentPath.isEmpty()) {
        title = swCreatorEditorFileName(currentPath);
        if (hasCurrentFileDirty()) {
            title += " *";
        }
        title += " - SwCreatorEditor";
    }

    return title;
}

bool SwCreatorEditorPanel::hasDirtyFiles() const {
    return m_tabs ? m_tabs->hasDirtyFiles() : false;
}

bool SwCreatorEditorPanel::hasCurrentFileDirty() const {
    if (!m_tabs) {
        return false;
    }
    const SwString currentPath = m_tabs->currentFilePath();
    const SwMap<SwString, bool> dirty = m_tabs->dirtyFiles();
    return dirty.contains(currentPath) && dirty.value(currentPath, false);
}

void SwCreatorEditorPanel::emitStateChanged_() {
    emit stateChanged();
}
