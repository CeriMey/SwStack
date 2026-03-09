#pragma once
/***************************************************************************************************
 * SwCreatorMainPanel - main "Designer" layout:
 * - Left: widget palette
 * - Center: workspace + canvas
 * - Right: hierarchy tree + property inspector
 **************************************************************************************************/

#include "SwAbstractItemModel.h"
#include "SwWidget.h"
#include "commands/SwCreatorCommandStack.h"
#include "designer/SwCreatorSystemDragDrop.h"

#include <map>

class SwSplitter;
class SwFrame;
class SwTreeWidget;
class SwStandardItem;
class SwMenu;

class SwCreatorWorkspace;
class SwCreatorFormCanvas;
class SwCreatorWidgetPalette;
class SwCreatorPropertyInspector;

class SwCreatorMainPanel : public SwWidget, public SwCreatorSystemDragDrop::DropHandler {
    SW_OBJECT(SwCreatorMainPanel, SwWidget)

public:
    explicit SwCreatorMainPanel(SwWidget* parent = nullptr);

    SwCreatorFormCanvas* canvas() const;
    void setSidebarsVisible(bool visible);

signals:
    DECLARE_SIGNAL_VOID(documentModified);

protected:
    void resizeEvent(ResizeEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;

private:
    bool canAcceptDrop(const SwCreatorSystemDragDrop::Payload& payload, int clientX, int clientY) override;
    void onDragOver(const SwCreatorSystemDragDrop::Payload& payload, int clientX, int clientY) override;
    void onDragLeave() override;
    void onDrop(const SwCreatorSystemDragDrop::Payload& payload, int clientX, int clientY) override;
    SwPoint windowClientToCanvasLocal_(int clientX, int clientY) const;
    SwWidget* rootWidget_() const;
    void scheduleDeferredLayoutRefresh_(int passes = 1);

    void updateLayout_();
    void wireEvents_();
    void rebuildHierarchy_();
    void setSelected_(SwWidget* w);
    void rebuildProperties_();
    void showHierarchyContextMenu_(int globalX, int globalY);
    void onHierarchyDragDropped_(const SwModelIndex& dragged, const SwModelIndex& droppedOn);

    void copySelected_();
    void cutSelected_();
    void paste_();
    void deleteSelected_();
    SwString clipboardText_() const;
    void setClipboardText_(const SwString& text) const;

    SwSplitter* m_splitter{nullptr};

    SwFrame* m_palettePanel{nullptr};
    SwCreatorWidgetPalette* m_palette{nullptr};

    SwCreatorWorkspace* m_workspace{nullptr};

    SwFrame* m_inspectorPanel{nullptr};
    SwSplitter* m_inspectorSplitter{nullptr};
    SwTreeWidget* m_hierarchyTree{nullptr};
    SwCreatorPropertyInspector* m_propertyInspector{nullptr};
    SwMenu* m_hierarchyContextMenu{nullptr};

    std::map<SwStandardItem*, SwWidget*> m_hierarchyItemToWidget;
    std::map<SwWidget*, SwModelIndex> m_widgetToHierarchyIndex;

    SwWidget* m_selected{nullptr};

    SwCreatorCommandStack m_commands;
    int m_pasteSerial{0};
    bool m_deferredLayoutRefreshQueued{false};
    int m_deferredLayoutRefreshRemaining{0};

    SwCreatorSystemDragDrop::Registration m_dropRegistration;
};
