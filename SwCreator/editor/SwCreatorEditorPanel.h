#pragma once

#include "SwString.h"
#include "SwWidget.h"

class SwCreatorEditorTabWidget;
class SwFileExplorer;
class SwSplitter;

class SwCreatorEditorPanel : public SwWidget {
    SW_OBJECT(SwCreatorEditorPanel, SwWidget)

public:
    explicit SwCreatorEditorPanel(SwWidget* parent = nullptr);

    void openPath(const SwString& path);
    bool openFileDialog(SwWidget* dialogParent);
    bool openFolderDialog(SwWidget* dialogParent);
    void setRootPath(const SwString& rootPath);
    void reloadExplorer();

    bool saveCurrent();
    bool saveAll();
    bool closeCurrentFile();

    SwString rootPath() const;
    SwString currentFilePath() const;
    SwString windowTitle() const;
    bool hasDirtyFiles() const;
    bool hasCurrentFileDirty() const;

signals:
    DECLARE_SIGNAL_VOID(stateChanged);

private:
    void emitStateChanged_();

    SwSplitter* m_splitter{nullptr};
    SwFileExplorer* m_explorer{nullptr};
    SwCreatorEditorTabWidget* m_tabs{nullptr};
    SwString m_rootPath;
};
