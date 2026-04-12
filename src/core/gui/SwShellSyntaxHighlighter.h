#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwShellSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat builtinFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat variableFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat operatorFormat;
    SwTextCharFormat shebangFormat;
};

inline SwTextCharFormat swShellMakeFormat_(const SwColor& color,
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

inline SwShellSyntaxTheme swShellSyntaxThemeVsCodeDark() {
    SwShellSyntaxTheme theme;
    theme.keywordFormat    = swShellMakeFormat_(SwColor{197, 134, 192}, Medium);
    theme.builtinFormat    = swShellMakeFormat_(SwColor{220, 220, 170});
    theme.commentFormat    = swShellMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat     = swShellMakeFormat_(SwColor{206, 145, 120});
    theme.variableFormat   = swShellMakeFormat_(SwColor{156, 220, 254});
    theme.numberFormat     = swShellMakeFormat_(SwColor{181, 206, 168});
    theme.operatorFormat   = swShellMakeFormat_(SwColor{212, 212, 212});
    theme.shebangFormat    = swShellMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    return theme;
}

class SwShellSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwShellSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwShellSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swShellSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwShellSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        if (previousBlockState() == HeredocState) {
            if (text.trimmed() == m_heredocDelimiter) {
                setFormat(0, len, m_theme.keywordFormat);
                setCurrentBlockState(0);
            } else {
                setFormat(0, len, m_theme.stringFormat);
                setCurrentBlockState(HeredocState);
            }
            return;
        }

        if (len >= 2 && text[0] == '#' && text[1] == '!') {
            setFormat(0, len, m_theme.shebangFormat);
            return;
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '#') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (ch == '\'' && previousBlockState() != SingleQuoteState) {
                const int end = findClosingQuote_(text, cursor, '\'');
                setFormat(cursor, end - cursor, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (ch == '"') {
                const int start = cursor;
                ++cursor;
                while (cursor < len) {
                    if (text[cursor] == '\\' && cursor + 1 < len) {
                        cursor += 2;
                        continue;
                    }
                    if (text[cursor] == '$') {
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

            if (ch == '`') {
                const int end = findClosingQuote_(text, cursor, '`');
                setFormat(cursor, end - cursor, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (ch == '$') {
                const int end = parseVariable_(text, cursor);
                if (end > cursor) {
                    setFormat(cursor, end - cursor, m_theme.variableFormat);
                    cursor = end;
                    continue;
                }
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) &&
                (cursor == 0 || !isWordChar_(text[cursor - 1]))) {
                const int start = cursor;
                while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '.')) {
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isWordStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isWordChar_(text[cursor])) {
                    ++cursor;
                }
                const SwString word = text.substr(start, cursor - start);
                if (isKeyword_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                } else if (isBuiltin_(word)) {
                    setFormat(start, cursor - start, m_theme.builtinFormat);
                }
                continue;
            }

            if (ch == '<' && cursor + 1 < len && text[cursor + 1] == '<') {
                int hStart = cursor + 2;
                if (hStart < len && text[hStart] == '-') { ++hStart; }
                if (hStart < len && (text[hStart] == '\'' || text[hStart] == '"')) {
                    const char q = text[hStart];
                    const int dStart = hStart + 1;
                    int dEnd = dStart;
                    while (dEnd < len && text[dEnd] != q) { ++dEnd; }
                    m_heredocDelimiter = text.substr(dStart, dEnd - dStart);
                    setFormat(cursor, (dEnd < len ? dEnd + 1 : dEnd) - cursor, m_theme.keywordFormat);
                    cursor = dEnd < len ? dEnd + 1 : dEnd;
                    setCurrentBlockState(HeredocState);
                    continue;
                }
                if (hStart < len && isWordStart_(text[hStart])) {
                    const int dStart = hStart;
                    int dEnd = dStart;
                    while (dEnd < len && isWordChar_(text[dEnd])) { ++dEnd; }
                    m_heredocDelimiter = text.substr(dStart, dEnd - dStart);
                    setFormat(cursor, dEnd - cursor, m_theme.keywordFormat);
                    cursor = dEnd;
                    setCurrentBlockState(HeredocState);
                    continue;
                }
            }

            ++cursor;
        }
    }

private:
    enum BlockState {
        NormalState = 0,
        HeredocState = 1,
        SingleQuoteState = 2
    };

    static bool isWordStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isWordChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_';
    }

    static int findClosingQuote_(const SwString& text, int start, char quote) {
        const int len = static_cast<int>(text.size());
        int cursor = start + 1;
        while (cursor < len) {
            if (text[cursor] == '\\' && cursor + 1 < len) {
                cursor += 2;
                continue;
            }
            if (text[cursor] == quote) {
                return cursor + 1;
            }
            ++cursor;
        }
        return len;
    }

    static int parseVariable_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        if (start >= len || text[start] != '$') {
            return start;
        }
        int cursor = start + 1;
        if (cursor >= len) {
            return start + 1;
        }
        if (text[cursor] == '{') {
            ++cursor;
            while (cursor < len && text[cursor] != '}') { ++cursor; }
            if (cursor < len) { ++cursor; }
            return cursor;
        }
        if (text[cursor] == '(') {
            ++cursor;
            int depth = 1;
            while (cursor < len && depth > 0) {
                if (text[cursor] == '(') { ++depth; }
                else if (text[cursor] == ')') { --depth; }
                ++cursor;
            }
            return cursor;
        }
        if (text[cursor] == '?' || text[cursor] == '#' || text[cursor] == '$' ||
            text[cursor] == '!' || text[cursor] == '@' || text[cursor] == '*' ||
            text[cursor] == '-' || text[cursor] == '_') {
            return cursor + 1;
        }
        if (std::isdigit(static_cast<unsigned char>(text[cursor]))) {
            return cursor + 1;
        }
        while (cursor < len && isWordChar_(text[cursor])) { ++cursor; }
        return cursor;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
            "while", "until", "do", "done", "in", "function", "time", "coproc",
            "return", "exit", "break", "continue", "declare", "typeset", "local",
            "export", "readonly", "unset", "shift", "set", "trap", "eval", "exec",
            "source"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isBuiltin_(const SwString& word) {
        static const char* const builtins[] = {
            "echo", "printf", "read", "cd", "pwd", "pushd", "popd", "dirs",
            "let", "test", "true", "false", "getopts", "hash", "type", "bind",
            "help", "builtin", "caller", "command", "compgen", "complete",
            "compopt", "enable", "mapfile", "readarray", "ulimit", "umask",
            "alias", "unalias", "wait", "jobs", "bg", "fg", "kill", "disown",
            "suspend", "logout", "history", "fc", "shopt", "chmod", "chown",
            "mkdir", "rmdir", "rm", "cp", "mv", "ln", "cat", "grep", "sed",
            "awk", "find", "xargs", "sort", "uniq", "wc", "head", "tail",
            "cut", "tr", "tee", "basename", "dirname"
        };
        for (const char* b : builtins) {
            if (word == SwString(b)) { return true; }
        }
        return false;
    }

    SwShellSyntaxTheme m_theme;
    SwString m_heredocDelimiter;
};
