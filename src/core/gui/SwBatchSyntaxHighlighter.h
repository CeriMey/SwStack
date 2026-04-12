#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwBatchSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat commandFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat variableFormat;
    SwTextCharFormat labelFormat;
    SwTextCharFormat operatorFormat;
    SwTextCharFormat echoTextFormat;
};

inline SwTextCharFormat swBatchMakeFormat_(const SwColor& color,
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

inline SwBatchSyntaxTheme swBatchSyntaxThemeVsCodeDark() {
    SwBatchSyntaxTheme theme;
    theme.keywordFormat   = swBatchMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.commandFormat   = swBatchMakeFormat_(SwColor{220, 220, 170});
    theme.commentFormat   = swBatchMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat    = swBatchMakeFormat_(SwColor{206, 145, 120});
    theme.variableFormat  = swBatchMakeFormat_(SwColor{156, 220, 254});
    theme.labelFormat     = swBatchMakeFormat_(SwColor{78, 201, 176});
    theme.operatorFormat  = swBatchMakeFormat_(SwColor{212, 212, 212});
    theme.echoTextFormat  = swBatchMakeFormat_(SwColor{206, 145, 120});
    return theme;
}

class SwBatchSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwBatchSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwBatchSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swBatchSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwBatchSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        if (len == 0) {
            return;
        }

        int cursor = 0;
        setCurrentBlockState(0);

        while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
            ++cursor;
        }
        if (cursor >= len) {
            return;
        }

        if (text[cursor] == ':' && cursor + 1 < len && text[cursor + 1] == ':') {
            setFormat(cursor, len - cursor, m_theme.commentFormat);
            return;
        }

        {
            const SwString trimmed = text.trimmed();
            const SwString lower = trimmed.toLower();
            if (lower.startsWith("rem ") || lower == SwString("rem")) {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }
        }

        if (text[cursor] == ':' && cursor + 1 < len && text[cursor + 1] != ':') {
            int end = cursor + 1;
            while (end < len && !std::isspace(static_cast<unsigned char>(text[end]))) {
                ++end;
            }
            setFormat(cursor, end - cursor, m_theme.labelFormat);
            return;
        }

        if (text[cursor] == '@') {
            setFormat(cursor, 1, m_theme.operatorFormat);
            ++cursor;
            while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
                ++cursor;
            }
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '%') {
                const int end = parseVariable_(text, cursor);
                if (end > cursor + 1) {
                    setFormat(cursor, end - cursor, m_theme.variableFormat);
                    cursor = end;
                    continue;
                }
            }

            if (ch == '"') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && text[cursor] != '"') {
                    ++cursor;
                }
                if (cursor < len) {
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (isWordStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isWordChar_(text[cursor])) {
                    ++cursor;
                }
                const SwString word = text.substr(start, cursor - start);
                const SwString wordLower = word.toLower();
                if (isKeyword_(wordLower)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                } else if (isCommand_(wordLower)) {
                    setFormat(start, cursor - start, m_theme.commandFormat);
                }

                if (wordLower == SwString("echo")) {
                    while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
                        ++cursor;
                    }
                    if (cursor < len) {
                        highlightEchoContent_(text, cursor);
                        return;
                    }
                }
                continue;
            }

            ++cursor;
        }
    }

private:
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
        if (start >= len || text[start] != '%') {
            return start;
        }

        if (start + 1 < len && text[start + 1] == '~') {
            int cursor = start + 2;
            while (cursor < len && (std::isalpha(static_cast<unsigned char>(text[cursor])) ||
                                    std::isdigit(static_cast<unsigned char>(text[cursor])))) {
                ++cursor;
            }
            return cursor;
        }

        if (start + 1 < len && std::isdigit(static_cast<unsigned char>(text[start + 1]))) {
            return start + 2;
        }

        if (start + 1 < len && text[start + 1] == '%') {
            int cursor = start + 2;
            while (cursor < len && text[cursor] != '%') {
                ++cursor;
            }
            if (cursor < len) {
                ++cursor;
            }
            return cursor;
        }

        int cursor = start + 1;
        while (cursor < len && isWordChar_(text[cursor])) {
            ++cursor;
        }
        if (cursor < len && text[cursor] == '%') {
            ++cursor;
        }
        return cursor;
    }

    void highlightEchoContent_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor < len) {
            if (text[cursor] == '%') {
                if (cursor > start) {
                    setFormat(start, cursor - start, m_theme.echoTextFormat);
                }
                const int end = parseVariable_(text, cursor);
                setFormat(cursor, end - cursor, m_theme.variableFormat);
                cursor = end;
                start = cursor;
                continue;
            }
            ++cursor;
        }
        if (cursor > start) {
            setFormat(start, cursor - start, m_theme.echoTextFormat);
        }
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "if", "else", "for", "in", "do", "goto", "call", "exit",
            "set", "setlocal", "endlocal", "shift", "not", "exist",
            "defined", "equ", "neq", "lss", "leq", "gtr", "geq",
            "errorlevel", "cmdextversion", "enabledelayedexpansion",
            "disabledelayedexpansion", "enableextensions", "disableextensions"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isCommand_(const SwString& word) {
        static const char* const cmds[] = {
            "echo", "pause", "cls", "type", "copy", "xcopy", "robocopy",
            "move", "del", "erase", "rd", "rmdir", "md", "mkdir", "ren",
            "rename", "dir", "cd", "chdir", "pushd", "popd", "title",
            "color", "mode", "start", "taskkill", "tasklist", "ping",
            "ipconfig", "netstat", "findstr", "attrib", "icacls",
            "reg", "sc", "net", "where", "wmic", "powershell",
            "assoc", "ftype", "path", "prompt", "verify", "vol"
        };
        for (const char* c : cmds) {
            if (word == SwString(c)) { return true; }
        }
        return false;
    }

    SwBatchSyntaxTheme m_theme;
};
