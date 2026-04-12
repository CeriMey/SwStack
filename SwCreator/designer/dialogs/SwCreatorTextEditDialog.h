#pragma once
/***************************************************************************************************
 * SwCreatorTextEditDialog
 *
 * Multi-line code editor dialog (dockable via SwCreatorDockDialog).
 * Used for StyleSheet editing with CSS/QSS syntax highlighting and auto-completion.
 **************************************************************************************************/

#include "designer/dialogs/SwCreatorDockDialog.h"

#include "SwString.h"

#include <functional>

class SwCodeEditor;
class SwCompleter;
class SwCssSyntaxHighlighter;
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
    void setupCompletion_();

    SwCodeEditor* m_editor{nullptr};
    SwCssSyntaxHighlighter* m_highlighter{nullptr};
    SwCompleter* m_completer{nullptr};
    SwPushButton* m_apply{nullptr};
    SwPushButton* m_close{nullptr};
    std::function<void(const SwString&)> m_onApply;
};
