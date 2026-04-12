#pragma once

#include "SwString.h"

class SwCodeEditor;

enum class SwFileLanguage {
    PlainText,
    Cpp,
    Python,
    JavaScript,
    Java,
    CSharp,
    Rust,
    Go,
    Shell,
    Batch,
    CMake,
    Xml,
    Json,
    Yaml,
    Css,
    Sql,
    Lua,
    Ini,
    Markdown
};

SwFileLanguage swDetectFileLanguage(const SwString& filePath);
void swCreatorConfigureCodeEditor(SwCodeEditor* editor, const SwString& filePath);
