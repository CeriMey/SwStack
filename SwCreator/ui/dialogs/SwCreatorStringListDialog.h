#pragma once
/***************************************************************************************************
 * SwCreatorStringListDialog
 *
 * Reusable list-of-strings editor dialog (dockable via SwCreatorDockDialog).
 * Intended for editing items of widgets like SwComboBox / SwListWidget in SwCreator.
 **************************************************************************************************/

#include "ui/dialogs/SwCreatorDockDialog.h"

#include "SwString.h"
#include "core/types/SwVector.h"

#include <functional>

class SwLineEdit;
class SwListWidget;
class SwPushButton;
class SwToolButton;

class SwCreatorStringListDialog : public SwCreatorDockDialog {
    SW_OBJECT(SwCreatorStringListDialog, SwCreatorDockDialog)

public:
    explicit SwCreatorStringListDialog(SwWidget* parent = nullptr);

    void setItems(const SwVector<SwString>& items);
    SwVector<SwString> items() const;

    void setOnApply(const std::function<void(const SwVector<SwString>&)>& handler);

signals:
    DECLARE_SIGNAL(applied, SwVector<SwString>);

private:
    void buildUi_();
    void rebuildList_(int selectRow);
    void syncEditorFromSelection_();
    void commitCurrentEdit_();

    int currentRow_() const;
    void setCurrentRow_(int row);

    void insertItem_(int index, const SwString& text);

    SwListWidget* m_list{nullptr};
    SwLineEdit* m_edit{nullptr};
    SwToolButton* m_add{nullptr};
    SwToolButton* m_remove{nullptr};
    SwToolButton* m_up{nullptr};
    SwToolButton* m_down{nullptr};

    SwPushButton* m_apply{nullptr};
    SwPushButton* m_close{nullptr};

    SwVector<SwString> m_items;
    std::function<void(const SwVector<SwString>&)> m_onApply;
};

