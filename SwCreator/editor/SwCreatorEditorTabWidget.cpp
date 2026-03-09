#include "editor/SwCreatorEditorTabWidget.h"

#include "editor/SwCreatorEditorPage.h"
#include "editor/SwCreatorEditorPaths.h"

#include "SwMessageBox.h"

SwCreatorEditorTabWidget::SwCreatorEditorTabWidget(SwWidget* parent)
    : SwTabWidget(parent) {
    setTabStyle(SwTabWidget::TabStyle::Underline);
    setTabsFillSpace(false);
    setUsesScrollButtons(true);
    setTabsClosable(true);
    setMovable(true);
    setStyleSheet(R"(
        SwCreatorEditorTabWidget {
            background-color: rgb(255, 255, 255);
            border-width: 0px;
        }
        SwTabWidget {
            background-color: rgb(255, 255, 255);
            border-width: 0px;
            color: rgb(30, 41, 59);
        }
    )");

    SwObject::connect(this, &SwTabWidget::tabCloseRequested, this, [this](int index) {
        SwCreatorEditorPage* page = pageAt_(index);
        if (page) {
            (void)closeFile(page->filePath());
        }
    });
    SwObject::connect(this, &SwTabWidget::currentChanged, this, [this](int) {
        emit currentFileChanged(currentFilePath());
    });
}

bool SwCreatorEditorTabWidget::openFile(const SwString& rawFilePath) {
    const SwString filePath = swCreatorEditorNormalizePath(rawFilePath);
    const int existingIndex = indexOfPath_(filePath);
    if (existingIndex >= 0) {
        setCurrentIndex(existingIndex);
        emit currentFileChanged(currentFilePath());
        return true;
    }

    SwCreatorEditorPage* page = new SwCreatorEditorPage(this);
    if (!page->openFile(filePath)) {
        delete page;
        return false;
    }

    SwObject::connect(page, &SwCreatorEditorPage::dirtyStateChanged, this, [this, page](bool dirty) {
        onPageDirtyStateChanged_(page, dirty);
    });

    const int index = addTab(page, page->displayName());
    m_pages.insert(filePath, page);
    updateTabLabel_(page);
    setCurrentIndex(index);

    emit dirtyFilesChanged();
    emit currentFileChanged(currentFilePath());
    return true;
}

bool SwCreatorEditorTabWidget::saveCurrent() {
    SwCreatorEditorPage* page = pageAt_(currentIndex());
    return page ? page->save() : true;
}

bool SwCreatorEditorTabWidget::saveAll() {
    bool allSaved = true;
    const SwList<SwString> paths = m_pages.keys();
    for (size_t i = 0; i < paths.size(); ++i) {
        SwCreatorEditorPage* page = m_pages.value(paths[i], nullptr);
        if (!page || !page->isDirty()) {
            continue;
        }
        if (!page->save()) {
            allSaved = false;
            break;
        }
    }
    if (allSaved) {
        emit dirtyFilesChanged();
    }
    return allSaved;
}

bool SwCreatorEditorTabWidget::closeCurrentFile() {
    SwCreatorEditorPage* page = pageAt_(currentIndex());
    return page ? closeFile(page->filePath()) : true;
}

bool SwCreatorEditorTabWidget::closeFile(const SwString& rawFilePath) {
    const SwString filePath = swCreatorEditorNormalizePath(rawFilePath);
    SwCreatorEditorPage* page = m_pages.value(filePath, nullptr);
    if (!page) {
        return true;
    }

    if (!confirmClose_(page)) {
        return false;
    }

    const int index = indexOfPage_(page);
    m_pages.remove(filePath);
    if (index >= 0) {
        removeTab(index);
    }

    emit dirtyFilesChanged();
    emit currentFileChanged(currentFilePath());
    return true;
}

bool SwCreatorEditorTabWidget::hasDirtyFiles() const {
    for (SwMap<SwString, SwCreatorEditorPage*>::const_iterator it = m_pages.begin(); it != m_pages.end(); ++it) {
        const SwCreatorEditorPage* page = it.value();
        if (page && page->isDirty()) {
            return true;
        }
    }
    return false;
}

SwString SwCreatorEditorTabWidget::currentFilePath() const {
    const SwCreatorEditorPage* page = pageAt_(currentIndex());
    return page ? page->filePath() : SwString();
}

SwMap<SwString, bool> SwCreatorEditorTabWidget::dirtyFiles() const {
    SwMap<SwString, bool> dirty;
    for (SwMap<SwString, SwCreatorEditorPage*>::const_iterator it = m_pages.begin(); it != m_pages.end(); ++it) {
        const SwCreatorEditorPage* page = it.value();
        if (page && page->isDirty()) {
            dirty.insert(it.key(), true);
        }
    }
    return dirty;
}

SwList<SwString> SwCreatorEditorTabWidget::openFilePaths() const {
    SwList<SwString> paths;
    paths.reserve(static_cast<size_t>(count()));
    for (int i = 0; i < count(); ++i) {
        const SwCreatorEditorPage* page = pageAt_(i);
        if (page) {
            paths.append(page->filePath());
        }
    }
    return paths;
}

SwCreatorEditorPage* SwCreatorEditorTabWidget::pageAt_(int index) const {
    return dynamic_cast<SwCreatorEditorPage*>(widget(index));
}

int SwCreatorEditorTabWidget::indexOfPage_(const SwCreatorEditorPage* page) const {
    if (!page) {
        return -1;
    }
    for (int i = 0; i < count(); ++i) {
        if (widget(i) == page) {
            return i;
        }
    }
    return -1;
}

int SwCreatorEditorTabWidget::indexOfPath_(const SwString& rawFilePath) const {
    const SwString filePath = swCreatorEditorNormalizePath(rawFilePath);
    return indexOfPage_(m_pages.value(filePath, nullptr));
}

SwString SwCreatorEditorTabWidget::tabLabelForPage_(SwCreatorEditorPage* page) const {
    if (!page) {
        return SwString();
    }

    SwString label = page->displayName();
    int duplicates = 0;
    for (SwMap<SwString, SwCreatorEditorPage*>::const_iterator it = m_pages.begin(); it != m_pages.end(); ++it) {
        SwCreatorEditorPage* other = it.value();
        if (other && other->displayName() == page->displayName()) {
            ++duplicates;
        }
    }
    if (duplicates > 1) {
        const SwString directoryName = swCreatorEditorDirectoryName(page->filePath());
        if (!directoryName.isEmpty()) {
            label += SwString(" [") + directoryName + "]";
        }
    }

    if (page->isDirty()) {
        label = SwString("* ") + label;
    }
    return label;
}

bool SwCreatorEditorTabWidget::confirmClose_(SwCreatorEditorPage* page) {
    if (!page || !page->isDirty()) {
        return true;
    }

    const int clicked = SwMessageBox::question(
        this,
        "Unsaved changes",
        SwString("Save changes to \"") + page->displayName() + "\" before closing?",
        SwMessageBox::Yes | SwMessageBox::No | SwMessageBox::Cancel);

    if (clicked == SwMessageBox::Cancel || clicked == SwMessageBox::NoButton) {
        return false;
    }
    if (clicked == SwMessageBox::Yes) {
        return page->save();
    }
    return true;
}

void SwCreatorEditorTabWidget::updateTabLabel_(SwCreatorEditorPage* page) {
    const int index = indexOfPage_(page);
    if (index < 0) {
        return;
    }
    setTabText(index, tabLabelForPage_(page));
}

void SwCreatorEditorTabWidget::onPageDirtyStateChanged_(SwCreatorEditorPage* page, bool dirty) {
    if (!page) {
        return;
    }
    updateTabLabel_(page);
    emit fileDirtyStateChanged(page->filePath(), dirty);
    emit dirtyFilesChanged();
    if (page == pageAt_(currentIndex())) {
        emit currentFileChanged(currentFilePath());
    }
}
