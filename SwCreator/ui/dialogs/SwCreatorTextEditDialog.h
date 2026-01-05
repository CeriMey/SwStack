#pragma once
/***************************************************************************************************
 * SwCreatorTextEditDialog
 *
 * Reusable multi-line text editor dialog (dockable via SwCreatorDockDialog).
 * Intended for long string properties like StyleSheet, JSON, scripts, etc.
 **************************************************************************************************/

#include "ui/dialogs/SwCreatorDockDialog.h"

#include "SwString.h"

#include <functional>

class SwPlainTextEdit;
class SwPushButton;

class SwCreatorTextEditDialog : public SwCreatorDockDialog {
    SW_OBJECT(SwCreatorTextEditDialog, SwCreatorDockDialog)

public:
    explicit SwCreatorTextEditDialog(SwWidget* parent = nullptr);

    void setText(const SwString& text);
    SwString text() const;

    void setPlaceholderText(const SwString& text);

    void setOnApply(const std::function<void(const SwString&)>& handler);

signals:
    DECLARE_SIGNAL(applied, SwString);

private:
    void buildUi_();

    SwPlainTextEdit* m_edit{nullptr};
    SwPushButton* m_apply{nullptr};
    SwPushButton* m_close{nullptr};
    std::function<void(const SwString&)> m_onApply;
};

