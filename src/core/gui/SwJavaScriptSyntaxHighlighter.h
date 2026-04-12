#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwJavaScriptSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat templateStringFormat;
    SwTextCharFormat templateExprFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat regexFormat;
    SwTextCharFormat constantFormat;
};

inline SwTextCharFormat swJsMakeFormat_(const SwColor& color,
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

inline SwJavaScriptSyntaxTheme swJsSyntaxThemeVsCodeDark() {
    SwJavaScriptSyntaxTheme theme;
    theme.keywordFormat        = swJsMakeFormat_(SwColor{197, 134, 192}, Medium);
    theme.typeFormat           = swJsMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat        = swJsMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat         = swJsMakeFormat_(SwColor{206, 145, 120});
    theme.templateStringFormat = swJsMakeFormat_(SwColor{206, 145, 120});
    theme.templateExprFormat   = swJsMakeFormat_(SwColor{156, 220, 254});
    theme.numberFormat         = swJsMakeFormat_(SwColor{181, 206, 168});
    theme.functionFormat       = swJsMakeFormat_(SwColor{220, 220, 170});
    theme.regexFormat          = swJsMakeFormat_(SwColor{209, 105, 105});
    theme.constantFormat       = swJsMakeFormat_(SwColor{86, 156, 214});
    return theme;
}

class SwJavaScriptSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwJavaScriptSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwJavaScriptSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swJsSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwJavaScriptSyntaxTheme& theme) {
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

        if (previousBlockState() == TemplateLiteralState) {
            cursor = highlightTemplateLiteral_(text, 0, false);
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '/' && cursor + 1 < len && text[cursor + 1] == '/') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (ch == '/' && cursor + 1 < len && text[cursor + 1] == '*') {
                const int end = findBlockCommentEnd_(text, cursor);
                if (end < 0) {
                    setFormat(cursor, len - cursor, m_theme.commentFormat);
                    setCurrentBlockState(BlockCommentState);
                    return;
                }
                setFormat(cursor, end - cursor, m_theme.commentFormat);
                cursor = end;
                continue;
            }

            if (ch == '`') {
                cursor = highlightTemplateLiteral_(text, cursor, true);
                continue;
            }

            if (ch == '"' || ch == '\'') {
                const int start = cursor;
                const int end = findClosingQuote_(text, cursor, ch);
                setFormat(start, end - start, m_theme.stringFormat);
                cursor = end;
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

                int peek = cursor;
                while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                const bool isCall = peek < len && text[peek] == '(';

                if (isConstant_(word)) {
                    setFormat(start, cursor - start, m_theme.constantFormat);
                } else if (isKeyword_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                } else if (isType_(word)) {
                    setFormat(start, cursor - start, m_theme.typeFormat);
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
        BlockCommentState = 1,
        TemplateLiteralState = 2
    };

    static bool isIdentStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_' || u == '$';
    }

    static bool isIdentPart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_' || u == '$';
    }

    static int findClosingQuote_(const SwString& text, int start, char quote) {
        const int len = static_cast<int>(text.size());
        int cursor = start + 1;
        while (cursor < len) {
            if (text[cursor] == '\\') { cursor += 2; continue; }
            if (text[cursor] == quote) { return cursor + 1; }
            ++cursor;
        }
        return len;
    }

    static int findBlockCommentEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = (start >= 2 && text[start] != '*') ? start : start;
        while (cursor + 1 < len) {
            if (text[cursor] == '*' && text[cursor + 1] == '/') {
                return cursor + 2;
            }
            ++cursor;
        }
        return -1;
    }

    int highlightTemplateLiteral_(const SwString& text, int start, bool skipOpening) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        if (skipOpening) { ++cursor; }

        const int fmtStart = start;
        while (cursor < len) {
            if (text[cursor] == '\\' && cursor + 1 < len) {
                cursor += 2;
                continue;
            }
            if (text[cursor] == '`') {
                ++cursor;
                setFormat(fmtStart, cursor - fmtStart, m_theme.templateStringFormat);
                return cursor;
            }
            if (text[cursor] == '$' && cursor + 1 < len && text[cursor + 1] == '{') {
                setFormat(fmtStart, cursor - fmtStart, m_theme.templateStringFormat);
                const int exprStart = cursor;
                cursor += 2;
                int depth = 1;
                while (cursor < len && depth > 0) {
                    if (text[cursor] == '{') { ++depth; }
                    else if (text[cursor] == '}') { --depth; }
                    if (depth > 0) { ++cursor; }
                }
                if (cursor < len) { ++cursor; }
                setFormat(exprStart, cursor - exprStart, m_theme.templateExprFormat);
                const int newFmtStart = cursor;
                int subEnd = highlightTemplateLiteral_(text, cursor, false);
                return subEnd;
            }
            ++cursor;
        }
        setFormat(fmtStart, len - fmtStart, m_theme.templateStringFormat);
        setCurrentBlockState(TemplateLiteralState);
        return len;
    }

    static int parseNumber_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        if (cursor + 1 < len && text[cursor] == '0') {
            const char n = text[cursor + 1];
            if (n == 'x' || n == 'X') {
                cursor += 2;
                while (cursor < len && (std::isxdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_')) { ++cursor; }
                if (cursor < len && text[cursor] == 'n') { ++cursor; }
                return cursor;
            }
            if (n == 'o' || n == 'O') {
                cursor += 2;
                while (cursor < len && ((text[cursor] >= '0' && text[cursor] <= '7') || text[cursor] == '_')) { ++cursor; }
                if (cursor < len && text[cursor] == 'n') { ++cursor; }
                return cursor;
            }
            if (n == 'b' || n == 'B') {
                cursor += 2;
                while (cursor < len && (text[cursor] == '0' || text[cursor] == '1' || text[cursor] == '_')) { ++cursor; }
                if (cursor < len && text[cursor] == 'n') { ++cursor; }
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
        if (cursor < len && text[cursor] == 'n') { ++cursor; }
        return cursor;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "break", "case", "catch", "class", "const", "continue", "debugger",
            "default", "delete", "do", "else", "export", "extends", "finally",
            "for", "function", "if", "import", "in", "instanceof", "let", "new",
            "of", "return", "super", "switch", "this", "throw", "try", "typeof",
            "var", "void", "while", "with", "yield",
            "async", "await", "from", "as", "static", "get", "set",
            "enum", "implements", "interface", "package", "private",
            "protected", "public", "abstract", "declare", "type",
            "namespace", "module", "readonly", "keyof", "infer",
            "is", "asserts", "override", "satisfies"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isType_(const SwString& word) {
        static const char* const types[] = {
            "string", "number", "boolean", "symbol", "bigint", "any", "unknown",
            "never", "void", "object", "Array", "Map", "Set", "WeakMap",
            "WeakSet", "Promise", "Date", "RegExp", "Error", "Function",
            "Object", "String", "Number", "Boolean", "Symbol", "BigInt",
            "Record", "Partial", "Required", "Readonly", "Pick", "Omit",
            "Exclude", "Extract", "NonNullable", "ReturnType", "Parameters",
            "InstanceType", "ConstructorParameters", "Awaited"
        };
        for (const char* t : types) {
            if (word == SwString(t)) { return true; }
        }
        return false;
    }

    static bool isConstant_(const SwString& word) {
        static const char* const constants[] = {
            "true", "false", "null", "undefined", "NaN", "Infinity",
            "globalThis", "console", "window", "document", "process",
            "module", "exports", "require", "__dirname", "__filename"
        };
        for (const char* c : constants) {
            if (word == SwString(c)) { return true; }
        }
        return false;
    }

    SwJavaScriptSyntaxTheme m_theme;
};
