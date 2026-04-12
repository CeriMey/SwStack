#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwGoSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat builtinFormat;
    SwTextCharFormat constantFormat;
    SwTextCharFormat packageFormat;
};

inline SwTextCharFormat swGoMakeFormat_(const SwColor& color,
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

inline SwGoSyntaxTheme swGoSyntaxThemeVsCodeDark() {
    SwGoSyntaxTheme theme;
    theme.keywordFormat  = swGoMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.typeFormat     = swGoMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat  = swGoMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat   = swGoMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat   = swGoMakeFormat_(SwColor{181, 206, 168});
    theme.functionFormat = swGoMakeFormat_(SwColor{220, 220, 170});
    theme.builtinFormat  = swGoMakeFormat_(SwColor{220, 220, 170});
    theme.constantFormat = swGoMakeFormat_(SwColor{79, 193, 255});
    theme.packageFormat  = swGoMakeFormat_(SwColor{78, 201, 176});
    return theme;
}

class SwGoSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwGoSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwGoSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swGoSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwGoSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        if (previousBlockState() == BlockCommentState) {
            const int end = findBlockCommentEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.commentFormat);
                setCurrentBlockState(BlockCommentState);
                return;
            }
            setFormat(0, end, m_theme.commentFormat);
            cursor = end;
        }

        bool expectPackageName = false;

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '/' && cursor + 1 < len) {
                if (text[cursor + 1] == '/') {
                    setFormat(cursor, len - cursor, m_theme.commentFormat);
                    return;
                }
                if (text[cursor + 1] == '*') {
                    const int end = findBlockCommentEnd_(text, cursor + 2);
                    if (end < 0) {
                        setFormat(cursor, len - cursor, m_theme.commentFormat);
                        setCurrentBlockState(BlockCommentState);
                        return;
                    }
                    setFormat(cursor, end - cursor, m_theme.commentFormat);
                    cursor = end;
                    continue;
                }
            }

            if (ch == '"') {
                const int start = cursor;
                ++cursor;
                while (cursor < len) {
                    if (text[cursor] == '\\') { cursor += 2; continue; }
                    if (text[cursor] == '"') { ++cursor; break; }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '`') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && text[cursor] != '`') { ++cursor; }
                if (cursor < len) { ++cursor; }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '\'') {
                const int start = cursor;
                ++cursor;
                if (cursor < len && text[cursor] == '\\') { cursor += 2; }
                else if (cursor < len) { ++cursor; }
                if (cursor < len && text[cursor] == '\'') { ++cursor; }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) ||
                (ch == '.' && cursor + 1 < len && std::isdigit(static_cast<unsigned char>(text[cursor + 1])))) {
                const int start = cursor;
                cursor = parseNumber_(text, cursor);
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isIdentStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                const SwString word = text.substr(start, cursor - start);

                if (expectPackageName) {
                    setFormat(start, cursor - start, m_theme.packageFormat);
                    expectPackageName = false;
                    continue;
                }

                int peek = cursor;
                while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                const bool isCall = peek < len && text[peek] == '(';

                if (isKeyword_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                    if (word == SwString("package") || word == SwString("import")) {
                        expectPackageName = true;
                    }
                    continue;
                }
                if (isType_(word)) {
                    setFormat(start, cursor - start, m_theme.typeFormat);
                    continue;
                }
                if (isBuiltin_(word)) {
                    setFormat(start, cursor - start, m_theme.builtinFormat);
                    continue;
                }
                if (isConstant_(word)) {
                    setFormat(start, cursor - start, m_theme.constantFormat);
                    continue;
                }
                if (isCall) {
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
        BlockCommentState = 1
    };

    static bool isIdentStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isIdentPart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_';
    }

    static int findBlockCommentEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor + 1 < len) {
            if (text[cursor] == '*' && text[cursor + 1] == '/') { return cursor + 2; }
            ++cursor;
        }
        return -1;
    }

    static int parseNumber_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        if (cursor + 1 < len && text[cursor] == '0') {
            const char n = text[cursor + 1];
            if (n == 'x' || n == 'X') {
                cursor += 2;
                while (cursor < len && (std::isxdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_')) { ++cursor; }
                return cursor;
            }
            if (n == 'o' || n == 'O') {
                cursor += 2;
                while (cursor < len && ((text[cursor] >= '0' && text[cursor] <= '7') || text[cursor] == '_')) { ++cursor; }
                return cursor;
            }
            if (n == 'b' || n == 'B') {
                cursor += 2;
                while (cursor < len && (text[cursor] == '0' || text[cursor] == '1' || text[cursor] == '_')) { ++cursor; }
                return cursor;
            }
        }
        while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_')) { ++cursor; }
        if (cursor < len && text[cursor] == '.') {
            ++cursor;
            while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_')) { ++cursor; }
        }
        if (cursor < len && (text[cursor] == 'e' || text[cursor] == 'E')) {
            ++cursor;
            if (cursor < len && (text[cursor] == '+' || text[cursor] == '-')) { ++cursor; }
            while (cursor < len && std::isdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
        }
        if (cursor < len && text[cursor] == 'i') { ++cursor; }
        return cursor;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "break", "case", "chan", "const", "continue", "default", "defer",
            "else", "fallthrough", "for", "func", "go", "goto", "if",
            "import", "interface", "map", "package", "range", "return",
            "select", "struct", "switch", "type", "var"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isType_(const SwString& word) {
        static const char* const types[] = {
            "bool", "byte", "complex64", "complex128", "error", "float32",
            "float64", "int", "int8", "int16", "int32", "int64", "rune",
            "string", "uint", "uint8", "uint16", "uint32", "uint64",
            "uintptr", "any", "comparable"
        };
        for (const char* t : types) {
            if (word == SwString(t)) { return true; }
        }
        return false;
    }

    static bool isBuiltin_(const SwString& word) {
        static const char* const builtins[] = {
            "append", "cap", "clear", "close", "complex", "copy", "delete",
            "imag", "len", "make", "max", "min", "new", "panic", "print",
            "println", "real", "recover"
        };
        for (const char* b : builtins) {
            if (word == SwString(b)) { return true; }
        }
        return false;
    }

    static bool isConstant_(const SwString& word) {
        static const char* const constants[] = {
            "true", "false", "nil", "iota"
        };
        for (const char* c : constants) {
            if (word == SwString(c)) { return true; }
        }
        return false;
    }

    SwGoSyntaxTheme m_theme;
};
