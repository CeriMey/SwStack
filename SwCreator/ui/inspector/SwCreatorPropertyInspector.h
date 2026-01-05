#pragma once
/***************************************************************************************************
 * SwCreatorPropertyInspector - right-bottom panel:
 * - Groups properties by owning class (SwObject/SwWidget/Derived) + Dynamic
 * - Supports add/remove/rename of dynamic properties
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwModelIndex.h"

#include <map>

class SwFrame;
class SwLineEdit;
class SwLabel;
class SwMenu;
class SwToolButton;
class SwTreeWidget;
class SwStandardItem;
class SwCreatorFormCanvas;
class SwCreatorTextEditDialog;
class SwCreatorStringListDialog;

class SwCreatorPropertyInspector : public SwWidget {
    SW_OBJECT(SwCreatorPropertyInspector, SwWidget)

public:
    explicit SwCreatorPropertyInspector(SwWidget* parent = nullptr);

    void setTarget(SwWidget* widget);
    SwWidget* target() const;

signals:
    DECLARE_SIGNAL_VOID(hierarchyNeedsRebuild);
    DECLARE_SIGNAL_VOID(canvasNeedsUpdate);
    DECLARE_SIGNAL_VOID(documentModified);

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    enum class AddType { String, Bool, Int };

    void buildUi_();
    void updateLayout_();
    void rebuild_();

    void onAddClicked_();
    void addDynamic_(AddType type);
    void onRemoveClicked_();
    void onSelectionChanged_(const SwModelIndex& current);

    void ensureTextEditDialog_();
    void ensureStringListDialog_();
    void closeEditorDialogs_();

    void openStyleSheetEditor_(const SwRect& anchorRect);
    void openItemsEditor_(const SwRect& anchorRect);

    void setEditorsForRow_(const SwModelIndex& nameIndex,
                           const SwModelIndex& valueIndex,
                           const SwString& propName,
                           bool isDynamic,
                           bool isReadOnly,
                           const SwAny& value);

    SwString uniqueDynamicName_(const SwString& base) const;

    SwWidget* m_target{nullptr};

    SwFrame* m_header{nullptr};
    SwLabel* m_title{nullptr};
    SwToolButton* m_add{nullptr};
    SwToolButton* m_remove{nullptr};
    SwMenu* m_addMenu{nullptr};

    SwTreeWidget* m_tree{nullptr};

    std::map<SwStandardItem*, SwString> m_itemToProperty;
    std::map<SwStandardItem*, bool> m_itemToDynamic;
    std::map<SwString, SwModelIndex> m_propertyToIndex;

    SwString m_currentProperty;
    bool m_currentDynamic{false};

    SwCreatorTextEditDialog* m_textEditDialog{nullptr};
    SwCreatorStringListDialog* m_stringListDialog{nullptr};

    SwLineEdit* m_styleSheetPreview{nullptr};
    SwToolButton* m_styleSheetEditButton{nullptr};

    SwLineEdit* m_itemsPreview{nullptr};
    SwToolButton* m_itemsEditButton{nullptr};
};
