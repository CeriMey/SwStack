#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwJavaSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat annotationFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat constantFormat;
};

inline SwTextCharFormat swJavaMakeFormat_(const SwColor& color,
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

inline SwJavaSyntaxTheme swJavaSyntaxThemeVsCodeDark() {
    SwJavaSyntaxTheme theme;
    theme.keywordFormat    = swJavaMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.typeFormat       = swJavaMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat    = swJavaMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat     = swJavaMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat     = swJavaMakeFormat_(SwColor{181, 206, 168});
    theme.annotationFormat = swJavaMakeFormat_(SwColor{220, 220, 170});
    theme.functionFormat   = swJavaMakeFormat_(SwColor{220, 220, 170});
    theme.constantFormat   = swJavaMakeFormat_(SwColor{79, 193, 255});
    return theme;
}

class SwJavaSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwJavaSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwJavaSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swJavaSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwJavaSyntaxTheme& theme) {
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
                if (cursor + 2 < len && text[cursor + 1] == '"' && text[cursor + 2] == '"') {
                    const int start = cursor;
                    cursor += 3;
                    while (cursor + 2 < len) {
                        if (text[cursor] == '"' && text[cursor + 1] == '"' && text[cursor + 2] == '"') {
                            cursor += 3;
                            break;
                        }
                        ++cursor;
                    }
                    if (cursor > len) { cursor = len; }
                    setFormat(start, cursor - start, m_theme.stringFormat);
                    continue;
                }
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

            if (ch == '@') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                setFormat(start, cursor - start, m_theme.annotationFormat);
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
                } else if (isAllUpperCase_(word)) {
                    setFormat(start, cursor - start, m_theme.constantFormat);
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
        return std::isalpha(u) != 0 || u == '_' || u == '$';
    }

    static bool isIdentPart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_' || u == '$';
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
                if (cursor < len && (text[cursor] == 'L' || text[cursor] == 'l')) { ++cursor; }
                return cursor;
            }
            if (n == 'b' || n == 'B') {
                cursor += 2;
                while (cursor < len && (text[cursor] == '0' || text[cursor] == '1' || text[cursor] == '_')) { ++cursor; }
                if (cursor < len && (text[cursor] == 'L' || text[cursor] == 'l')) { ++cursor; }
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
        if (cursor < len && (text[cursor] == 'f' || text[cursor] == 'F' ||
                             text[cursor] == 'd' || text[cursor] == 'D' ||
                             text[cursor] == 'L' || text[cursor] == 'l')) { ++cursor; }
        return cursor;
    }

    static bool isAllUpperCase_(const SwString& word) {
        if (word.size() < 2) { return false; }
        for (size_t i = 0; i < word.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(word[i]);
            if (std::isalpha(c) && std::islower(c)) { return false; }
            if (!std::isalnum(c) && c != '_') { return false; }
        }
        return true;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "abstract", "assert", "break", "case", "catch", "class", "const",
            "continue", "default", "do", "else", "enum", "extends", "final",
            "finally", "for", "goto", "if", "implements", "import", "instanceof",
            "interface", "native", "new", "package", "private", "protected",
            "public", "return", "static", "strictfp", "super", "switch",
            "synchronized", "this", "throw", "throws", "transient", "try",
            "void", "volatile", "while", "yield",
            "var", "record", "sealed", "permits", "non-sealed",
            "true", "false", "null"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isType_(const SwString& word) {
        static const char* const types[] = {
            "boolean", "byte", "char", "double", "float", "int", "long", "short",
            "String", "Integer", "Long", "Double", "Float", "Boolean", "Byte",
            "Character", "Short", "Object", "Class", "Void", "Number",
            "List", "Map", "Set", "Collection", "Iterable", "Iterator",
            "Optional", "Stream", "Comparable", "Serializable", "Runnable",
            "Callable", "Future", "CompletableFuture", "Thread",
            "Exception", "RuntimeException", "Error", "Throwable",
            "StringBuilder", "StringBuffer", "ArrayList", "HashMap",
            "HashSet", "LinkedList", "TreeMap", "TreeSet"
        };
        for (const char* t : types) {
            if (word == SwString(t)) { return true; }
        }
        return false;
    }

    SwJavaSyntaxTheme m_theme;
};
