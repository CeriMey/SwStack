#include "editor/SwCreatorEditorPage.h"

#include "editor/SwCreatorEditorCodeSupport.h"
#include "editor/SwCreatorEditorPaths.h"

#include "SwCodeEditor.h"
#include "SwFile.h"
#include "SwFileInfo.h"
#include "SwLayout.h"
#include "SwMessageBox.h"

SwCreatorEditorPage::SwCreatorEditorPage(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwCreatorEditorPage { background-color: rgb(30, 30, 30); border-width: 0px; }");

    SwVerticalLayout* layout = new SwVerticalLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);
    setLayout(layout);

    m_editor = new SwCodeEditor(this);
    layout->addWidget(m_editor, 1, 0);

    SwObject::connect(m_editor, &SwPlainTextEdit::textChanged, this, [this]() { onEditorTextChanged_(); });
}

bool SwCreatorEditorPage::openFile(const SwString& rawFilePath) {
    const SwString normalizedPath = swCreatorEditorNormalizePath(rawFilePath);
    const SwFileInfo info(normalizedPath.toStdString());
    if (!info.exists() || !info.isFile()) {
        SwMessageBox::warning(this,
                              "Open",
                              SwString("File not found:\n") + normalizedPath);
        return false;
    }

    SwFile file(normalizedPath);
    if (!file.open(SwFile::Read)) {
        SwMessageBox::warning(this,
                              "Open",
                              SwString("Failed to open file:\n") + normalizedPath);
        return false;
    }

    const SwString content = file.readAll();
    file.close();

    m_filePath = normalizedPath;
    m_savedText = content;

    swCreatorConfigureCodeEditor(m_editor, m_filePath);

    m_loading = true;
    m_editor->setPlainText(content);
    m_loading = false;

    setDirtyState_(false);
    return true;
}

bool SwCreatorEditorPage::save() {
    if (m_filePath.isEmpty()) {
        return false;
    }

    SwFile file(m_filePath);
    if (!file.open(SwFile::Write)) {
        SwMessageBox::warning(this,
                              "Save",
                              SwString("Failed to open file for writing:\n") + m_filePath);
        return false;
    }

    const SwString content = m_editor ? m_editor->toPlainText() : SwString();
    if (!file.write(content)) {
        file.close();
        SwMessageBox::warning(this,
                              "Save",
                              SwString("Failed to write file:\n") + m_filePath);
        return false;
    }

    file.close();
    m_savedText = content;
    setDirtyState_(false);
    return true;
}

SwString SwCreatorEditorPage::filePath() const {
    return m_filePath;
}

SwString SwCreatorEditorPage::displayName() const {
    return swCreatorEditorFileName(m_filePath);
}

bool SwCreatorEditorPage::isDirty() const {
    return m_dirty;
}

SwCodeEditor* SwCreatorEditorPage::editor() const {
    return m_editor;
}

void SwCreatorEditorPage::onEditorTextChanged_() {
    if (m_loading || !m_editor) {
        return;
    }
    setDirtyState_(m_editor->toPlainText() != m_savedText);
}

void SwCreatorEditorPage::setDirtyState_(bool dirty) {
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyStateChanged(m_dirty);
}
