#include "editor/SwCreatorEditorCodeSupport.h"
#include "editor/SwCreatorEditorLanguageSupport.h"
#include "theme/SwCreatorTheme.h"

#include "SwCodeEditor.h"
#include "SwCompleter.h"
#include "SwCppSyntaxHighlighter.h"
#include "SwPythonSyntaxHighlighter.h"
#include "SwJavaScriptSyntaxHighlighter.h"
#include "SwJavaSyntaxHighlighter.h"
#include "SwCSharpSyntaxHighlighter.h"
#include "SwRustSyntaxHighlighter.h"
#include "SwGoSyntaxHighlighter.h"
#include "SwShellSyntaxHighlighter.h"
#include "SwBatchSyntaxHighlighter.h"
#include "SwCMakeSyntaxHighlighter.h"
#include "SwXmlSyntaxHighlighter.h"
#include "SwJsonSyntaxHighlighter.h"
#include "SwYamlSyntaxHighlighter.h"
#include "SwCssSyntaxHighlighter.h"
#include "SwSqlSyntaxHighlighter.h"
#include "SwLuaSyntaxHighlighter.h"
#include "SwIniSyntaxHighlighter.h"
#include "SwMarkdownSyntaxHighlighter.h"

namespace {

SwString extractFileName_(const SwString& filePath) {
    SwString normalized = filePath;
    for (size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] == '\\') {
            normalized[i] = '/';
        }
    }
    const size_t lastSlash = normalized.lastIndexOf('/');
    if (lastSlash != std::wstring::npos && lastSlash + 1 < normalized.size()) {
        return normalized.substr(lastSlash + 1);
    }
    return normalized;
}

} // namespace

SwFileLanguage swDetectFileLanguage(const SwString& filePath) {
    const SwString lower = filePath.toLower();
    const SwString fileName = extractFileName_(lower);

    // ── Special filenames ───────────────────────────────────────────────
    if (fileName == SwString("cmakelists.txt")) { return SwFileLanguage::CMake; }
    if (fileName == SwString("makefile") || fileName == SwString("gnumakefile")) { return SwFileLanguage::Shell; }
    if (fileName == SwString("dockerfile"))    { return SwFileLanguage::Shell; }
    if (fileName == SwString(".gitignore") ||
        fileName == SwString(".gitattributes") ||
        fileName == SwString(".gitmodules") ||
        fileName == SwString(".editorconfig")) { return SwFileLanguage::Ini; }

    // ── Extension lookup ────────────────────────────────────────────────

    // C / C++
    if (lower.endsWith(".c") || lower.endsWith(".cc") || lower.endsWith(".cpp") ||
        lower.endsWith(".cxx") || lower.endsWith(".c++") ||
        lower.endsWith(".h") || lower.endsWith(".hh") || lower.endsWith(".hpp") ||
        lower.endsWith(".hxx") || lower.endsWith(".h++") || lower.endsWith(".inl") ||
        lower.endsWith(".ipp") || lower.endsWith(".tpp") || lower.endsWith(".txx")) {
        return SwFileLanguage::Cpp;
    }

    // Python
    if (lower.endsWith(".py") || lower.endsWith(".pyw") || lower.endsWith(".pyi") ||
        lower.endsWith(".pyx") || lower.endsWith(".pxd")) {
        return SwFileLanguage::Python;
    }

    // JavaScript / TypeScript
    if (lower.endsWith(".js") || lower.endsWith(".jsx") || lower.endsWith(".mjs") ||
        lower.endsWith(".cjs") || lower.endsWith(".ts") || lower.endsWith(".tsx") ||
        lower.endsWith(".mts") || lower.endsWith(".cts")) {
        return SwFileLanguage::JavaScript;
    }

    // Java
    if (lower.endsWith(".java") || lower.endsWith(".gradle") ||
        lower.endsWith(".groovy") || lower.endsWith(".kt") || lower.endsWith(".kts") ||
        lower.endsWith(".scala")) {
        return SwFileLanguage::Java;
    }

    // C#
    if (lower.endsWith(".cs") || lower.endsWith(".csx") || lower.endsWith(".csproj")) {
        return SwFileLanguage::CSharp;
    }

    // Rust
    if (lower.endsWith(".rs")) {
        return SwFileLanguage::Rust;
    }

    // Go
    if (lower.endsWith(".go")) {
        return SwFileLanguage::Go;
    }

    // Shell
    if (lower.endsWith(".sh") || lower.endsWith(".bash") || lower.endsWith(".zsh") ||
        lower.endsWith(".fish") || lower.endsWith(".ksh") || lower.endsWith(".csh") ||
        lower.endsWith(".tcsh") || lower.endsWith(".profile") ||
        lower.endsWith(".bashrc") || lower.endsWith(".zshrc") ||
        lower.endsWith(".bash_profile") || lower.endsWith(".bash_aliases")) {
        return SwFileLanguage::Shell;
    }

    // Batch / cmd
    if (lower.endsWith(".bat") || lower.endsWith(".cmd") || lower.endsWith(".btm")) {
        return SwFileLanguage::Batch;
    }

    // CMake
    if (lower.endsWith(".cmake") || lower.endsWith(".cmake.in")) {
        return SwFileLanguage::CMake;
    }

    // XML / HTML / SVG
    if (lower.endsWith(".xml") || lower.endsWith(".html") || lower.endsWith(".htm") ||
        lower.endsWith(".xhtml") || lower.endsWith(".svg") || lower.endsWith(".xsl") ||
        lower.endsWith(".xslt") || lower.endsWith(".xsd") || lower.endsWith(".dtd") ||
        lower.endsWith(".plist") || lower.endsWith(".xaml") || lower.endsWith(".ui") ||
        lower.endsWith(".qrc") || lower.endsWith(".vcxproj") || lower.endsWith(".sln") ||
        lower.endsWith(".csproj") || lower.endsWith(".fsproj") || lower.endsWith(".targets") ||
        lower.endsWith(".props") || lower.endsWith(".resx") || lower.endsWith(".manifest") ||
        lower.endsWith(".swui")) {
        return SwFileLanguage::Xml;
    }

    // JSON
    if (lower.endsWith(".json") || lower.endsWith(".jsonc") || lower.endsWith(".json5") ||
        lower.endsWith(".geojson") || lower.endsWith(".jsonl") ||
        lower.endsWith(".webmanifest") || lower.endsWith(".har")) {
        return SwFileLanguage::Json;
    }

    // YAML
    if (lower.endsWith(".yml") || lower.endsWith(".yaml") ||
        lower.endsWith(".clang-format") || lower.endsWith(".clang-tidy")) {
        return SwFileLanguage::Yaml;
    }

    // CSS / SCSS / LESS
    if (lower.endsWith(".css") || lower.endsWith(".scss") || lower.endsWith(".sass") ||
        lower.endsWith(".less") || lower.endsWith(".styl") || lower.endsWith(".stylus")) {
        return SwFileLanguage::Css;
    }

    // SQL
    if (lower.endsWith(".sql") || lower.endsWith(".ddl") || lower.endsWith(".dml") ||
        lower.endsWith(".pgsql") || lower.endsWith(".plsql") || lower.endsWith(".tsql")) {
        return SwFileLanguage::Sql;
    }

    // Lua
    if (lower.endsWith(".lua") || lower.endsWith(".luau") || lower.endsWith(".nse")) {
        return SwFileLanguage::Lua;
    }

    // INI / Config / TOML / Properties
    if (lower.endsWith(".ini") || lower.endsWith(".cfg") || lower.endsWith(".conf") ||
        lower.endsWith(".config") || lower.endsWith(".toml") ||
        lower.endsWith(".properties") || lower.endsWith(".env") ||
        lower.endsWith(".desktop") || lower.endsWith(".service") ||
        lower.endsWith(".timer") || lower.endsWith(".socket") ||
        lower.endsWith(".target") || lower.endsWith(".mount") ||
        lower.endsWith(".automount") || lower.endsWith(".path") ||
        lower.endsWith(".rc") || lower.endsWith(".cnf")) {
        return SwFileLanguage::Ini;
    }

    // Markdown
    if (lower.endsWith(".md") || lower.endsWith(".markdown") || lower.endsWith(".mdown") ||
        lower.endsWith(".mkd") || lower.endsWith(".mdx") || lower.endsWith(".rst")) {
        return SwFileLanguage::Markdown;
    }

    return SwFileLanguage::PlainText;
}

namespace {

void installSyntaxHighlighter_(SwCodeEditor* editor, SwFileLanguage lang) {
    switch (lang) {
    case SwFileLanguage::Cpp: {
        auto* hl = new SwCppSyntaxHighlighter(editor->document());
        hl->setTheme(swCppSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Python: {
        auto* hl = new SwPythonSyntaxHighlighter(editor->document());
        hl->setTheme(swPythonSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::JavaScript: {
        auto* hl = new SwJavaScriptSyntaxHighlighter(editor->document());
        hl->setTheme(swJsSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Java: {
        auto* hl = new SwJavaSyntaxHighlighter(editor->document());
        hl->setTheme(swJavaSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::CSharp: {
        auto* hl = new SwCSharpSyntaxHighlighter(editor->document());
        hl->setTheme(swCSharpSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Rust: {
        auto* hl = new SwRustSyntaxHighlighter(editor->document());
        hl->setTheme(swRustSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Go: {
        auto* hl = new SwGoSyntaxHighlighter(editor->document());
        hl->setTheme(swGoSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Shell: {
        auto* hl = new SwShellSyntaxHighlighter(editor->document());
        hl->setTheme(swShellSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Batch: {
        auto* hl = new SwBatchSyntaxHighlighter(editor->document());
        hl->setTheme(swBatchSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::CMake: {
        auto* hl = new SwCMakeSyntaxHighlighter(editor->document());
        hl->setTheme(swCMakeSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Xml: {
        auto* hl = new SwXmlSyntaxHighlighter(editor->document());
        hl->setTheme(swXmlSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Json: {
        auto* hl = new SwJsonSyntaxHighlighter(editor->document());
        hl->setTheme(swJsonSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Yaml: {
        auto* hl = new SwYamlSyntaxHighlighter(editor->document());
        hl->setTheme(swYamlSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Css: {
        auto* hl = new SwCssSyntaxHighlighter(editor->document());
        hl->setTheme(swCssSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Sql: {
        auto* hl = new SwSqlSyntaxHighlighter(editor->document());
        hl->setTheme(swSqlSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Lua: {
        auto* hl = new SwLuaSyntaxHighlighter(editor->document());
        hl->setTheme(swLuaSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Ini: {
        auto* hl = new SwIniSyntaxHighlighter(editor->document());
        hl->setTheme(swIniSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::Markdown: {
        auto* hl = new SwMarkdownSyntaxHighlighter(editor->document());
        hl->setTheme(swMarkdownSyntaxThemeVsCodeDark());
        editor->setSyntaxHighlighter(hl);
        break;
    }
    case SwFileLanguage::PlainText:
        break;
    }
}

void installCompletionProvider_(SwCodeEditor* editor, SwFileLanguage lang) {
    SwCompleter* completer = new SwCompleter(editor);
    completer->setCaseSensitivity(Sw::CaseInsensitive);
    completer->setMaxVisibleItems(10);
    editor->setCompleter(completer);
    editor->setAutoCompletionEnabled(true);
    editor->setAutoCompletionMinPrefixLength(2);

    switch (lang) {
    case SwFileLanguage::Cpp:
        editor->setCompletionProvider(swCppCompletionProvider_);
        break;
    case SwFileLanguage::Python:
        editor->setCompletionProvider(swPythonCompletionProvider_);
        break;
    case SwFileLanguage::JavaScript:
        editor->setCompletionProvider(swJavaScriptCompletionProvider_);
        break;
    case SwFileLanguage::Java:
        editor->setCompletionProvider(swJavaCompletionProvider_);
        break;
    case SwFileLanguage::CSharp:
        editor->setCompletionProvider(swCSharpCompletionProvider_);
        break;
    case SwFileLanguage::Rust:
        editor->setCompletionProvider(swRustCompletionProvider_);
        break;
    case SwFileLanguage::Go:
        editor->setCompletionProvider(swGoCompletionProvider_);
        break;
    case SwFileLanguage::Shell:
        editor->setCompletionProvider(swShellCompletionProvider_);
        break;
    case SwFileLanguage::Batch:
        editor->setCompletionProvider(swBatchCompletionProvider_);
        break;
    case SwFileLanguage::CMake:
        editor->setCompletionProvider(swCMakeCompletionProvider_);
        break;
    case SwFileLanguage::Xml:
        editor->setCompletionProvider(swXmlCompletionProvider_);
        break;
    case SwFileLanguage::Css:
        editor->setCompletionProvider(swCssCompletionProvider_);
        break;
    case SwFileLanguage::Sql:
        editor->setCompletionProvider(swSqlCompletionProvider_);
        break;
    case SwFileLanguage::Lua:
        editor->setCompletionProvider(swLuaCompletionProvider_);
        break;
    case SwFileLanguage::Json:
        editor->setCompletionProvider(swJsonCompletionProvider_);
        break;
    case SwFileLanguage::Yaml:
        editor->setCompletionProvider(swYamlCompletionProvider_);
        break;
    case SwFileLanguage::Markdown:
        editor->setCompletionProvider(swMarkdownCompletionProvider_);
        break;
    case SwFileLanguage::Ini:
        editor->setCompletionProvider(swIniCompletionProvider_);
        break;
    case SwFileLanguage::PlainText:
        break;
    }
}

void configureIndentation_(SwCodeEditor* editor, SwFileLanguage lang) {
    editor->setIndentSize(swDefaultIndentSize_(lang));
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

    const SwFileLanguage lang = swDetectFileLanguage(filePath);

    // Syntax highlighting
    installSyntaxHighlighter_(editor, lang);

    // Autocompletion
    installCompletionProvider_(editor, lang);

    // Auto-indentation size
    configureIndentation_(editor, lang);
}
