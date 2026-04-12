#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwCMakeSyntaxTheme {
    SwTextCharFormat commandFormat;
    SwTextCharFormat keywordFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat variableFormat;
    SwTextCharFormat generatorExprFormat;
    SwTextCharFormat propertyFormat;
};

inline SwTextCharFormat swCMakeMakeFormat_(const SwColor& color,
                                            FontWeight weight = Normal,
                                            bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) {
        format.setFontWeight(weight);
    }
    if (italic) {
        format.setFontItalic(true);
    }
    return format;
}

inline SwCMakeSyntaxTheme swCMakeSyntaxThemeVsCodeDark() {
    SwCMakeSyntaxTheme theme;
    theme.commandFormat       = swCMakeMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.keywordFormat       = swCMakeMakeFormat_(SwColor{197, 134, 192});
    theme.commentFormat       = swCMakeMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat        = swCMakeMakeFormat_(SwColor{206, 145, 120});
    theme.variableFormat      = swCMakeMakeFormat_(SwColor{156, 220, 254});
    theme.generatorExprFormat = swCMakeMakeFormat_(SwColor{78, 201, 176});
    theme.propertyFormat      = swCMakeMakeFormat_(SwColor{79, 193, 255});
    return theme;
}

class SwCMakeSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwCMakeSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwCMakeSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swCMakeSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwCMakeSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        if (previousBlockState() == BracketCommentState) {
            const int end = findBracketCommentEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.commentFormat);
                setCurrentBlockState(BracketCommentState);
                return;
            }
            setFormat(0, end, m_theme.commentFormat);
            cursor = end;
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '#') {
                if (cursor + 1 < len && text[cursor + 1] == '[') {
                    int bracketLen = 1;
                    int scan = cursor + 2;
                    while (scan < len && text[scan] == '=') { ++bracketLen; ++scan; }
                    if (scan < len && text[scan] == '[') {
                        const int end = findBracketCommentEnd_(text, cursor);
                        if (end < 0) {
                            setFormat(cursor, len - cursor, m_theme.commentFormat);
                            setCurrentBlockState(BracketCommentState);
                            return;
                        }
                        setFormat(cursor, end - cursor, m_theme.commentFormat);
                        cursor = end;
                        continue;
                    }
                }
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (ch == '"') {
                const int start = cursor;
                ++cursor;
                while (cursor < len) {
                    if (text[cursor] == '\\' && cursor + 1 < len) {
                        cursor += 2;
                        continue;
                    }
                    if (text[cursor] == '$' && cursor + 1 < len && text[cursor + 1] == '{') {
                        if (cursor > start) {
                            setFormat(start, cursor - start, m_theme.stringFormat);
                        }
                        const int varEnd = parseVariable_(text, cursor);
                        setFormat(cursor, varEnd - cursor, m_theme.variableFormat);
                        cursor = varEnd;
                        continue;
                    }
                    if (text[cursor] == '"') {
                        ++cursor;
                        break;
                    }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '$' && cursor + 1 < len && text[cursor + 1] == '{') {
                const int end = parseVariable_(text, cursor);
                setFormat(cursor, end - cursor, m_theme.variableFormat);
                cursor = end;
                continue;
            }

            if (ch == '$' && cursor + 1 < len && text[cursor + 1] == '<') {
                const int start = cursor;
                int depth = 0;
                while (cursor < len) {
                    if (text[cursor] == '<') { ++depth; }
                    else if (text[cursor] == '>') {
                        --depth;
                        if (depth <= 0) { ++cursor; break; }
                    }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.generatorExprFormat);
                continue;
            }

            if (isWordStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isWordChar_(text[cursor])) {
                    ++cursor;
                }
                const SwString word = text.substr(start, cursor - start);

                int next = cursor;
                while (next < len && std::isspace(static_cast<unsigned char>(text[next]))) { ++next; }
                const bool isCall = next < len && text[next] == '(';

                const SwString lower = word.toLower();
                if (isCall && isCommand_(lower)) {
                    setFormat(start, cursor - start, m_theme.commandFormat);
                } else if (isProperty_(word)) {
                    setFormat(start, cursor - start, m_theme.propertyFormat);
                } else if (isKeywordArg_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                }
                continue;
            }

            ++cursor;
        }
    }

private:
    enum BlockState {
        NormalState = 0,
        BracketCommentState = 1
    };

    static bool isWordStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isWordChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_';
    }

    static int parseVariable_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        if (start + 1 >= len || text[start] != '$' || text[start + 1] != '{') {
            return start;
        }
        int cursor = start + 2;
        int depth = 1;
        while (cursor < len && depth > 0) {
            if (text[cursor] == '{') { ++depth; }
            else if (text[cursor] == '}') { --depth; }
            ++cursor;
        }
        return cursor;
    }

    static int findBracketCommentEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor + 1 < len) {
            if (text[cursor] == ']' && text[cursor + 1] == '#') {
                return cursor + 2;
            }
            ++cursor;
        }
        return -1;
    }

    static bool isCommand_(const SwString& word) {
        static const char* const cmds[] = {
            "cmake_minimum_required", "project", "add_executable", "add_library",
            "target_link_libraries", "target_include_directories", "target_compile_definitions",
            "target_compile_options", "target_compile_features", "target_sources",
            "set", "unset", "option", "list", "string", "math", "file",
            "find_package", "find_library", "find_path", "find_program", "find_file",
            "include", "include_directories", "link_directories", "link_libraries",
            "add_subdirectory", "add_definitions", "add_compile_definitions",
            "add_compile_options", "add_custom_command", "add_custom_target",
            "add_dependencies", "add_test",
            "install", "configure_file", "execute_process", "get_filename_component",
            "get_target_property", "set_target_properties", "set_property",
            "get_property", "define_property",
            "if", "elseif", "else", "endif",
            "foreach", "endforeach", "while", "endwhile",
            "function", "endfunction", "macro", "endmacro",
            "return", "break", "continue",
            "message", "cmake_policy", "cmake_parse_arguments",
            "enable_testing", "enable_language",
            "mark_as_advanced", "separate_arguments",
            "source_group", "export", "fetchcontent_declare",
            "fetchcontent_makeavailable", "fetchcontent_populate",
            "fetchcontent_getproperties"
        };
        for (const char* c : cmds) {
            if (word == SwString(c)) { return true; }
        }
        return false;
    }

    static bool isKeywordArg_(const SwString& word) {
        static const char* const keywords[] = {
            "PUBLIC", "PRIVATE", "INTERFACE", "REQUIRED", "COMPONENTS",
            "CONFIG", "MODULE", "IMPORTED", "GLOBAL", "ALIAS",
            "STATIC", "SHARED", "OBJECT", "EXCLUDE_FROM_ALL",
            "DESTINATION", "TARGETS", "FILES", "DIRECTORY", "PROGRAMS",
            "COMMAND", "WORKING_DIRECTORY", "DEPENDS", "COMMENT", "VERBATIM",
            "APPEND", "PARENT_SCOPE", "CACHE", "FORCE", "INTERNAL", "STRING",
            "FILEPATH", "PATH", "BOOL", "ON", "OFF", "TRUE", "FALSE",
            "AND", "OR", "NOT", "STREQUAL", "MATCHES", "VERSION_LESS",
            "VERSION_GREATER", "VERSION_EQUAL", "DEFINED", "EXISTS",
            "IS_DIRECTORY", "IS_ABSOLUTE", "FATAL_ERROR", "WARNING",
            "STATUS", "AUTHOR_WARNING", "DEPRECATION", "NOTICE",
            "SEND_ERROR", "CHECK_START", "CHECK_PASS", "CHECK_FAIL",
            "OUTPUT_VARIABLE", "RESULT_VARIABLE", "ERROR_VARIABLE",
            "INPUT_FILE", "OUTPUT_FILE", "NEWLINE_STYLE", "UNIX", "WIN32",
            "DOWNLOAD", "UPLOAD", "GLOB", "GLOB_RECURSE", "CONFIGURE",
            "GENERATE", "READ", "WRITE", "APPEND", "RENAME", "REMOVE",
            "MAKE_DIRECTORY", "RELATIVE_PATH", "TO_CMAKE_PATH",
            "TO_NATIVE_PATH", "COPY", "INSTALL", "REGEX", "REPLACE",
            "FIND", "SUBSTRING", "LENGTH", "TOLOWER", "TOUPPER",
            "COMPARE", "STRIP", "RANDOM", "TIMESTAMP", "UUID",
            "LESS", "GREATER", "EQUAL", "LESS_EQUAL", "GREATER_EQUAL",
            "SORT", "REVERSE", "REMOVE_ITEM", "REMOVE_AT",
            "REMOVE_DUPLICATES", "FILTER", "INCLUDE", "EXCLUDE",
            "GET", "JOIN", "SUBLIST", "POP_BACK", "POP_FRONT",
            "PREPEND", "TRANSFORM"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isProperty_(const SwString& word) {
        if (word.size() < 3) { return false; }
        if (word.startsWith("CMAKE_") || word.startsWith("PROJECT_")) {
            return true;
        }
        return false;
    }

    SwCMakeSyntaxTheme m_theme;
};
