#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwLuaSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat builtinFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat constantFormat;
    SwTextCharFormat selfFormat;
};

inline SwTextCharFormat swLuaMakeFormat_(const SwColor& color,
                                          FontWeight weight = Normal,
                                          bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) { format.setFontWeight(weight); }
    if (italic) { format.setFontItalic(true); }
    return format;
}

inline SwLuaSyntaxTheme swLuaSyntaxThemeVsCodeDark() {
    SwLuaSyntaxTheme theme;
    theme.keywordFormat  = swLuaMakeFormat_(SwColor{197, 134, 192}, Medium);
    theme.builtinFormat  = swLuaMakeFormat_(SwColor{220, 220, 170});
    theme.commentFormat  = swLuaMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat   = swLuaMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat   = swLuaMakeFormat_(SwColor{181, 206, 168});
    theme.functionFormat = swLuaMakeFormat_(SwColor{220, 220, 170});
    theme.constantFormat = swLuaMakeFormat_(SwColor{86, 156, 214});
    theme.selfFormat     = swLuaMakeFormat_(SwColor{86, 156, 214}, Normal, true);
    return theme;
}

class SwLuaSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwLuaSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwLuaSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swLuaSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwLuaSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        if (previousBlockState() == LongCommentState) {
            const int end = findLongBracketEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.commentFormat);
                setCurrentBlockState(LongCommentState);
                return;
            }
            setFormat(0, end, m_theme.commentFormat);
            cursor = end;
        }

        if (previousBlockState() == LongStringState) {
            const int end = findLongBracketEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.stringFormat);
                setCurrentBlockState(LongStringState);
                return;
            }
            setFormat(0, end, m_theme.stringFormat);
            cursor = end;
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '-' && cursor + 1 < len && text[cursor + 1] == '-') {
                if (cursor + 2 < len && text[cursor + 2] == '[') {
                    const int level = longBracketLevel_(text, cursor + 2);
                    if (level >= 0) {
                        const int contentStart = cursor + 4 + level;
                        const int end = findLongBracketEnd_(text, contentStart);
                        if (end < 0) {
                            setFormat(cursor, len - cursor, m_theme.commentFormat);
                            setCurrentBlockState(LongCommentState);
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

            if (ch == '[') {
                const int level = longBracketLevel_(text, cursor);
                if (level >= 0) {
                    const int contentStart = cursor + 2 + level;
                    const int end = findLongBracketEnd_(text, contentStart);
                    if (end < 0) {
                        setFormat(cursor, len - cursor, m_theme.stringFormat);
                        setCurrentBlockState(LongStringState);
                        return;
                    }
                    setFormat(cursor, end - cursor, m_theme.stringFormat);
                    cursor = end;
                    continue;
                }
            }

            if (ch == '"' || ch == '\'') {
                const int start = cursor;
                ++cursor;
                while (cursor < len) {
                    if (text[cursor] == '\\') { cursor += 2; continue; }
                    if (text[cursor] == ch) { ++cursor; break; }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) ||
                (ch == '.' && cursor + 1 < len && std::isdigit(static_cast<unsigned char>(text[cursor + 1])))) {
                const int start = cursor;
                if (cursor + 1 < len && text[cursor] == '0' && (text[cursor + 1] == 'x' || text[cursor + 1] == 'X')) {
                    cursor += 2;
                    while (cursor < len && std::isxdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                } else {
                    while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '.')) { ++cursor; }
                }
                if (cursor < len && (text[cursor] == 'e' || text[cursor] == 'E')) {
                    ++cursor;
                    if (cursor < len && (text[cursor] == '+' || text[cursor] == '-')) { ++cursor; }
                    while (cursor < len && std::isdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                }
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isIdentStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                const SwString word = text.substr(start, cursor - start);

                if (word == SwString("self")) {
                    setFormat(start, cursor - start, m_theme.selfFormat);
                    continue;
                }

                int peek = cursor;
                while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                const bool isCall = peek < len && text[peek] == '(';

                if (isKeyword_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                } else if (isConstant_(word)) {
                    setFormat(start, cursor - start, m_theme.constantFormat);
                } else if (isBuiltin_(word)) {
                    setFormat(start, cursor - start, m_theme.builtinFormat);
                } else if (isCall) {
                    setFormat(start, cursor - start, m_theme.functionFormat);
                }
                continue;
            }

            ++cursor;
        }
    }

private:
    enum BlockState {
        NormalState = 0,
        LongCommentState = 1,
        LongStringState = 2
    };

    static bool isIdentStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isIdentPart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_';
    }

    static int longBracketLevel_(const SwString& text, int pos) {
        const int len = static_cast<int>(text.size());
        if (pos >= len || text[pos] != '[') { return -1; }
        int cursor = pos + 1;
        int level = 0;
        while (cursor < len && text[cursor] == '=') { ++level; ++cursor; }
        if (cursor < len && text[cursor] == '[') { return level; }
        return -1;
    }

    static int findLongBracketEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor < len) {
            if (text[cursor] == ']') {
                int eqCount = 0;
                int scan = cursor + 1;
                while (scan < len && text[scan] == '=') { ++eqCount; ++scan; }
                if (scan < len && text[scan] == ']') {
                    return scan + 1;
                }
            }
            ++cursor;
        }
        return -1;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "and", "break", "do", "else", "elseif", "end", "for",
            "function", "goto", "if", "in", "local", "not", "or",
            "repeat", "return", "then", "until", "while"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isConstant_(const SwString& word) {
        return word == SwString("true") || word == SwString("false") || word == SwString("nil");
    }

    static bool isBuiltin_(const SwString& word) {
        static const char* const builtins[] = {
            "assert", "collectgarbage", "dofile", "error", "getmetatable",
            "ipairs", "load", "loadfile", "next", "pairs", "pcall",
            "print", "rawequal", "rawget", "rawlen", "rawset", "require",
            "select", "setmetatable", "tonumber", "tostring", "type",
            "unpack", "xpcall",
            "io", "os", "string", "table", "math", "coroutine",
            "debug", "package", "utf8"
        };
        for (const char* b : builtins) {
            if (word == SwString(b)) { return true; }
        }
        return false;
    }

    SwLuaSyntaxTheme m_theme;
};
