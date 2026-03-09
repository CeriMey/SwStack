#pragma once

#include "SwList.h"
#include "SwMap.h"
#include "SwString.h"
#include "SwTabWidget.h"

class SwCreatorEditorPage;

class SwCreatorEditorTabWidget : public SwTabWidget {
    SW_OBJECT(SwCreatorEditorTabWidget, SwTabWidget)

public:
    explicit SwCreatorEditorTabWidget(SwWidget* parent = nullptr);

    bool openFile(const SwString& filePath);
    bool saveCurrent();
    bool saveAll();
    bool closeCurrentFile();
    bool closeFile(const SwString& filePath);
    bool hasDirtyFiles() const;

    SwString currentFilePath() const;
    SwMap<SwString, bool> dirtyFiles() const;
    SwList<SwString> openFilePaths() const;

signals:
    DECLARE_SIGNAL(currentFileChanged, const SwString&);
    DECLARE_SIGNAL(fileDirtyStateChanged, const SwString&, bool);
    DECLARE_SIGNAL_VOID(dirtyFilesChanged);

private:
    SwCreatorEditorPage* pageAt_(int index) const;
    int indexOfPage_(const SwCreatorEditorPage* page) const;
    int indexOfPath_(const SwString& filePath) const;
    SwString tabLabelForPage_(SwCreatorEditorPage* page) const;
    bool confirmClose_(SwCreatorEditorPage* page);
    void updateTabLabel_(SwCreatorEditorPage* page);
    void onPageDirtyStateChanged_(SwCreatorEditorPage* page, bool dirty);

    SwMap<SwString, SwCreatorEditorPage*> m_pages;
};
