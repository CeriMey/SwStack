#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwCSharpSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat attributeFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat preprocessorFormat;
    SwTextCharFormat constantFormat;
};

inline SwTextCharFormat swCSharpMakeFormat_(const SwColor& color,
                                             FontWeight weight = Normal,
                                             bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) { format.setFontWeight(weight); }
    if (italic) { format.setFontItalic(true); }
    return format;
}

inline SwCSharpSyntaxTheme swCSharpSyntaxThemeVsCodeDark() {
    SwCSharpSyntaxTheme theme;
    theme.keywordFormat      = swCSharpMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.typeFormat         = swCSharpMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat      = swCSharpMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat       = swCSharpMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat       = swCSharpMakeFormat_(SwColor{181, 206, 168});
    theme.attributeFormat    = swCSharpMakeFormat_(SwColor{220, 220, 170});
    theme.functionFormat     = swCSharpMakeFormat_(SwColor{220, 220, 170});
    theme.preprocessorFormat = swCSharpMakeFormat_(SwColor{197, 134, 192});
    theme.constantFormat     = swCSharpMakeFormat_(SwColor{79, 193, 255});
    return theme;
}

class SwCSharpSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwCSharpSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwCSharpSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swCSharpSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwCSharpSyntaxTheme& theme) {
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

        int firstNonSpace = 0;
        while (firstNonSpace < len && std::isspace(static_cast<unsigned char>(text[firstNonSpace]))) {
            ++firstNonSpace;
        }
        if (firstNonSpace < len && text[firstNonSpace] == '#') {
            setFormat(firstNonSpace, len - firstNonSpace, m_theme.preprocessorFormat);
            return;
        }

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

            if (ch == '@' && cursor + 1 < len && text[cursor + 1] == '"') {
                const int start = cursor;
                cursor += 2;
                while (cursor < len) {
                    if (text[cursor] == '"') {
                        if (cursor + 1 < len && text[cursor + 1] == '"') { cursor += 2; continue; }
                        ++cursor;
                        break;
                    }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '$' && cursor + 1 < len && text[cursor + 1] == '"') {
                const int start = cursor;
                cursor += 2;
                while (cursor < len) {
                    if (text[cursor] == '\\') { cursor += 2; continue; }
                    if (text[cursor] == '"') { ++cursor; break; }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
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

            if (ch == '\'') {
                const int start = cursor;
                ++cursor;
                if (cursor < len && text[cursor] == '\\') { cursor += 2; }
                else if (cursor < len) { ++cursor; }
                if (cursor < len && text[cursor] == '\'') { ++cursor; }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '[') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                if (cursor < len && text[cursor] == ']') {
                    ++cursor;
                    setFormat(start, cursor - start, m_theme.attributeFormat);
                    continue;
                }
                cursor = start + 1;
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

                if (isKeyword_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                } else if (isType_(word)) {
                    setFormat(start, cursor - start, m_theme.typeFormat);
                } else if (isCall) {
                    setFormat(start, cursor - start, m_theme.functionFormat);
                } else if (word.size() > 0 && std::isupper(static_cast<unsigned char>(word[0]))) {
                    setFormat(start, cursor - start, m_theme.typeFormat);
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
        return std::isalpha(u) != 0 || u == '_' || u == '@';
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
                goto suffix;
            }
            if (n == 'b' || n == 'B') {
                cursor += 2;
                while (cursor < len && (text[cursor] == '0' || text[cursor] == '1' || text[cursor] == '_')) { ++cursor; }
                goto suffix;
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
    suffix:
        while (cursor < len && (text[cursor] == 'f' || text[cursor] == 'F' ||
                                text[cursor] == 'd' || text[cursor] == 'D' ||
                                text[cursor] == 'm' || text[cursor] == 'M' ||
                                text[cursor] == 'l' || text[cursor] == 'L' ||
                                text[cursor] == 'u' || text[cursor] == 'U')) { ++cursor; }
        return cursor;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "abstract", "as", "base", "bool", "break", "byte", "case", "catch",
            "char", "checked", "class", "const", "continue", "decimal", "default",
            "delegate", "do", "double", "else", "enum", "event", "explicit",
            "extern", "false", "finally", "fixed", "float", "for", "foreach",
            "goto", "if", "implicit", "in", "int", "interface", "internal",
            "is", "lock", "long", "namespace", "new", "null", "object",
            "operator", "out", "override", "params", "private", "protected",
            "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
            "sizeof", "stackalloc", "static", "string", "struct", "switch",
            "this", "throw", "true", "try", "typeof", "uint", "ulong",
            "unchecked", "unsafe", "ushort", "using", "virtual", "void",
            "volatile", "while", "async", "await", "dynamic", "nameof",
            "var", "when", "where", "yield", "get", "set", "init", "value",
            "add", "remove", "partial", "global", "record", "required",
            "with", "not", "and", "or"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isType_(const SwString& word) {
        static const char* const types[] = {
            "String", "Int32", "Int64", "Boolean", "Double", "Float", "Decimal",
            "Byte", "Char", "Object", "Type", "Void", "Array", "List",
            "Dictionary", "HashSet", "Queue", "Stack", "LinkedList",
            "Task", "Action", "Func", "Predicate", "IEnumerable",
            "IList", "IDictionary", "ICollection", "IDisposable",
            "IComparable", "IEquatable", "EventHandler", "Nullable",
            "Span", "Memory", "ReadOnlySpan", "ValueTask",
            "Exception", "ArgumentException", "InvalidOperationException",
            "NullReferenceException", "IndexOutOfRangeException"
        };
        for (const char* t : types) {
            if (word == SwString(t)) { return true; }
        }
        return false;
    }

    SwCSharpSyntaxTheme m_theme;
};
