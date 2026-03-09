#include "SwCreatorDocument.h"

#include "designer/SwCreatorFormCanvas.h"
#include "serialization/SwCreatorSwuiSerializer.h"
#include "serialization/SwCreatorQtUiImporter.h"

#include "SwObject.h"
#include "core/io/SwFile.h"
#include "SwFileDialog.h"
#include "SwMessageBox.h"
#include "SwWidget.h"

#include <vector>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SwCreatorDocument::SwCreatorDocument(SwObject* parent)
    : SwObject(parent)
{}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

SwString SwCreatorDocument::currentPath() const
{
    return m_currentPath;
}

bool SwCreatorDocument::isDirty() const
{
    return m_dirty;
}

SwString SwCreatorDocument::windowTitle() const
{
    SwString title("SwCreator");
    if (!m_currentPath.isEmpty()) {
        SwFile f(m_currentPath);
        title = "SwCreator - " + f.fileName();
    }
    if (m_dirty) {
        title += "*";
    }
    return title;
}

// ---------------------------------------------------------------------------
// Dirty tracking
// ---------------------------------------------------------------------------

void SwCreatorDocument::markDirty()
{
    if (m_suppressDirty || m_dirty) {
        return;
    }
    m_dirty = true;
    emit documentModified();
    emit titleChanged(windowTitle());
}

void SwCreatorDocument::suppressNextDirty()
{
    m_suppressDirty = true;
}

// ---------------------------------------------------------------------------
// hookWidget
// ---------------------------------------------------------------------------

void SwCreatorDocument::hookWidget(SwWidget* w)
{
    if (!w) {
        return;
    }
    if (m_hookedWidgets.find(w) != m_hookedWidgets.end()) {
        return;
    }
    m_hookedWidgets.insert(w);

    SwObject::connect(w, &SwWidget::moved, this, [this](int, int) { markDirty(); });
    SwObject::connect(w, &SwWidget::resized, this, [this](int, int) { markDirty(); });
}

// ---------------------------------------------------------------------------
// clearCanvas_ (private)
// ---------------------------------------------------------------------------

void SwCreatorDocument::clearCanvas_(SwCreatorFormCanvas* canvas)
{
    if (!canvas) {
        return;
    }
    m_hookedWidgets.clear();

    std::vector<SwWidget*> copy;
    const auto& widgets = canvas->designWidgets();
    copy.reserve(widgets.size());
    for (SwWidget* w : widgets) {
        if (w) {
            copy.push_back(w);
        }
    }
    for (SwWidget* w : copy) {
        canvas->removeDesignWidget(w);
    }

    const SwSize defaultSize = SwCreatorFormCanvas::defaultFormSize();
    canvas->setFormSize(defaultSize.width, defaultSize.height);
}

// ---------------------------------------------------------------------------
// saveToPath_ (private)
// ---------------------------------------------------------------------------

bool SwCreatorDocument::saveToPath_(SwCreatorFormCanvas* canvas, SwWidget* dialogParent, const SwString& path)
{
    if (!canvas) {
        return false;
    }
    const SwString xml = SwCreatorSwuiSerializer::serializeCanvas(canvas);
    if (xml.isEmpty()) {
        SwMessageBox::information(dialogParent, "Save", "Nothing to save.");
        return false;
    }

    SwFile f(path);
    if (!f.open(SwFile::Write)) {
        SwMessageBox::information(dialogParent, "Save", "Failed to open file for writing.");
        return false;
    }
    const bool ok = f.write(xml);
    f.close();
    if (!ok) {
        SwMessageBox::information(dialogParent, "Save", "Failed to write file.");
        return false;
    }

    m_currentPath = path;
    m_dirty = false;
    emit titleChanged(windowTitle());
    return true;
}

// ---------------------------------------------------------------------------
// saveAs
// ---------------------------------------------------------------------------

bool SwCreatorDocument::saveAs(SwCreatorFormCanvas* canvas, SwWidget* dialogParent)
{
    SwString startDir;
    if (!m_currentPath.isEmpty()) {
        SwFile f(m_currentPath);
        startDir = f.getDirectory();
    }
    const SwString path = SwFileDialog::getSaveFileName(dialogParent,
                                                        "Save As...",
                                                        startDir,
                                                        "SwUI (*.swui);;All files (*)");
    if (path.isEmpty()) {
        return false;
    }
    return saveToPath_(canvas, dialogParent, path);
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

bool SwCreatorDocument::save(SwCreatorFormCanvas* canvas, SwWidget* dialogParent)
{
    if (m_currentPath.isEmpty()) {
        return saveAs(canvas, dialogParent);
    }
    return saveToPath_(canvas, dialogParent, m_currentPath);
}

// ---------------------------------------------------------------------------
// maybeSave
// ---------------------------------------------------------------------------

bool SwCreatorDocument::maybeSave(SwCreatorFormCanvas* canvas, SwWidget* dialogParent)
{
    if (!m_dirty) {
        return true;
    }

    SwMessageBox box(dialogParent);
    box.setWindowTitle("Unsaved changes");
    box.setText("Save changes before continuing?");
    box.setStandardButtons(SwMessageBox::Yes | SwMessageBox::No | SwMessageBox::Cancel);
    (void)box.exec();
    const int clicked = box.clickedButton();
    if (clicked == SwMessageBox::NoButton || clicked == SwMessageBox::Cancel) {
        return false;
    }
    if (clicked == SwMessageBox::Yes) {
        return save(canvas, dialogParent);
    }
    return true; // No => discard
}

// ---------------------------------------------------------------------------
// openFile
// ---------------------------------------------------------------------------

void SwCreatorDocument::openFile(SwCreatorFormCanvas* canvas, SwWidget* dialogParent)
{
    if (!maybeSave(canvas, dialogParent)) {
        return;
    }

    SwString startDir;
    if (!m_currentPath.isEmpty()) {
        SwFile f(m_currentPath);
        startDir = f.getDirectory();
    }
    const SwString path = SwFileDialog::getOpenFileName(dialogParent,
                                                        "Open...",
                                                        startDir,
                                                        "UI Files (*.swui *.ui);;SwUI (*.swui);;Qt Designer UI (*.ui);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    SwFile f(path);
    if (!f.open(SwFile::Read)) {
        SwMessageBox::information(dialogParent, "Open", "Failed to open file.");
        return;
    }
    const SwString content = f.readAll();
    f.close();
    if (content.isEmpty()) {
        SwMessageBox::information(dialogParent, "Open", "File is empty.");
        return;
    }

    m_suppressDirty = true;
    clearCanvas_(canvas);

    SwString error;
    const bool isQtUi = path.endsWith(".ui") || content.contains("<ui version");
    if (isQtUi) {
        if (!SwCreatorQtUiImporter::importToCanvas(content, canvas, &error)) {
            m_suppressDirty = false;
            SwMessageBox::information(dialogParent, "Open", error.isEmpty() ? "Failed to import Qt .ui file." : error);
            return;
        }
    } else {
        if (!SwCreatorSwuiSerializer::deserializeCanvas(content, canvas, &error)) {
            m_suppressDirty = false;
            SwMessageBox::information(dialogParent, "Open", error.isEmpty() ? "Failed to load file." : error);
            return;
        }
    }

    if (canvas) {
        const auto& widgets = canvas->designWidgets();
        for (SwWidget* w : widgets) {
            hookWidget(w);
        }
    }

    m_currentPath = path;
    m_dirty = false;
    m_suppressDirty = false;
    emit titleChanged(windowTitle());
}

// ---------------------------------------------------------------------------
// closeFile
// ---------------------------------------------------------------------------

void SwCreatorDocument::closeFile(SwCreatorFormCanvas* canvas, SwWidget* dialogParent)
{
    if (!maybeSave(canvas, dialogParent)) {
        return;
    }
    m_suppressDirty = true;
    clearCanvas_(canvas);
    m_currentPath = SwString();
    m_dirty = false;
    m_suppressDirty = false;
    emit titleChanged(windowTitle());
}
