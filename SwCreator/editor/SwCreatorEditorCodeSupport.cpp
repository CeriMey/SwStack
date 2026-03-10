#include "editor/SwCreatorEditorCodeSupport.h"
#include "theme/SwCreatorTheme.h"

#include "SwCodeEditor.h"
#include "SwCppSyntaxHighlighter.h"

namespace {

bool isCppLikeFile_(const SwString& filePath) {
    const SwString lower = filePath.toLower();
    return lower.endsWith(".c")
        || lower.endsWith(".cc")
        || lower.endsWith(".cpp")
        || lower.endsWith(".cxx")
        || lower.endsWith(".h")
        || lower.endsWith(".hh")
        || lower.endsWith(".hpp")
        || lower.endsWith(".hxx")
        || lower.endsWith(".inl");
}

} // namespace

void swCreatorConfigureCodeEditor(SwCodeEditor* editor, const SwString& filePath) {
    if (!editor) {
        return;
    }

    const auto& th = SwCreatorTheme::current();

    editor->setTheme(swCodeEditorVsCodeDarkTheme());
    editor->setStyleSheet(
        "SwCodeEditor { background-color: " + SwCreatorTheme::rgb(th.editorBg)
        + "; border-width: 0px; color: " + SwCreatorTheme::rgb(th.editorText) + "; }");

    if (!isCppLikeFile_(filePath)) {
        return;
    }

    SwCppSyntaxHighlighter* cppHighlighter = new SwCppSyntaxHighlighter(editor->document());
    cppHighlighter->setTheme(swCppSyntaxThemeVsCodeDark());
    editor->setSyntaxHighlighter(cppHighlighter);
}
