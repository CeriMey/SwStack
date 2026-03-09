#pragma once
#include "SwObject.h"
#include "SwString.h"
#include <unordered_set>

class SwWidget;
class SwCreatorFormCanvas;

class SwCreatorDocument : public SwObject {
    SW_OBJECT(SwCreatorDocument, SwObject)
public:
    explicit SwCreatorDocument(SwObject* parent = nullptr);

    SwString currentPath() const;
    bool isDirty() const;
    SwString windowTitle() const;

    void markDirty();
    void suppressNextDirty();

    // Canvas operations (pass the canvas + a parent widget for dialogs)
    void hookWidget(SwWidget* w);
    void openFile(SwCreatorFormCanvas* canvas, SwWidget* dialogParent);
    bool save(SwCreatorFormCanvas* canvas, SwWidget* dialogParent);
    bool saveAs(SwCreatorFormCanvas* canvas, SwWidget* dialogParent);
    bool maybeSave(SwCreatorFormCanvas* canvas, SwWidget* dialogParent);
    void closeFile(SwCreatorFormCanvas* canvas, SwWidget* dialogParent);

signals:
    DECLARE_SIGNAL(titleChanged, const SwString&);
    DECLARE_SIGNAL_VOID(documentModified);

private:
    void clearCanvas_(SwCreatorFormCanvas* canvas);
    bool saveToPath_(SwCreatorFormCanvas* canvas, SwWidget* dialogParent, const SwString& path);

    SwString m_currentPath;
    bool m_dirty{false};
    bool m_suppressDirty{false};
    std::unordered_set<SwWidget*> m_hookedWidgets;
};
