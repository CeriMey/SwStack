#pragma once

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwSyntaxHighlighter.h"

#include <cctype>

struct SwCppSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat preprocessorFormat;
    SwTextCharFormat macroFormat;
    SwTextCharFormat namespaceFormat;
};

inline SwTextCharFormat swCppMakeFormat_(const SwColor& color,
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

inline SwCppSyntaxTheme swCppSyntaxThemeVsCodeDark() {
    SwCppSyntaxTheme theme;
    theme.keywordFormat = swCppMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.typeFormat = swCppMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat = swCppMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat = swCppMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat = swCppMakeFormat_(SwColor{181, 206, 168});
    theme.functionFormat = swCppMakeFormat_(SwColor{220, 220, 170});
    theme.preprocessorFormat = swCppMakeFormat_(SwColor{197, 134, 192});
    theme.macroFormat = swCppMakeFormat_(SwColor{79, 193, 255});
    theme.namespaceFormat = swCppMakeFormat_(SwColor{78, 201, 176});
    return theme;
}

class SwCppSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwCppSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwCppSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swCppSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwCppSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

    SwCppSyntaxTheme theme() const {
        return m_theme;
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int textLength = static_cast<int>(text.size());
        int cursor = 0;
        int scanStart = 0;

        setCurrentBlockState(-1);

        if (previousBlockState() == BlockCommentState) {
            const int commentEnd = findBlockCommentEnd_(text, 0);
            if (commentEnd < 0) {
                setFormat(0, textLength, m_theme.commentFormat);
                setCurrentBlockState(BlockCommentState);
                return;
            }
            setFormat(0, commentEnd, m_theme.commentFormat);
            cursor = commentEnd;
            scanStart = commentEnd;
        }

        const int firstNonSpace = firstNonSpace_(text, 0);
        if (firstNonSpace >= 0 && firstNonSpace < textLength && text[firstNonSpace] == '#') {
            scanStart = highlightPreprocessor_(text, firstNonSpace);
            cursor = scanStart;
        }

        bool expectTypeName = false;
        bool expectNamespaceName = false;

        while (cursor < textLength) {
            const char ch = text[cursor];
            const unsigned char uch = static_cast<unsigned char>(ch);

            if (cursor < scanStart) {
                ++cursor;
                continue;
            }

            if (cursor + 1 < textLength && text[cursor] == '/' && text[cursor + 1] == '/') {
                setFormat(cursor, textLength - cursor, m_theme.commentFormat);
                return;
            }

            if (cursor + 1 < textLength && text[cursor] == '/' && text[cursor + 1] == '*') {
                const int commentEnd = findBlockCommentEnd_(text, cursor);
                if (commentEnd < 0) {
                    setFormat(cursor, textLength - cursor, m_theme.commentFormat);
                    setCurrentBlockState(BlockCommentState);
                    return;
                }
                setFormat(cursor, commentEnd - cursor, m_theme.commentFormat);
                cursor = commentEnd;
                continue;
            }

            const int prefixLength = stringPrefixLength_(text, cursor);
            if (prefixLength > 0 && cursor + prefixLength < textLength &&
                (text[cursor + prefixLength] == '"' || text[cursor + prefixLength] == '\'')) {
                const int end = parseQuotedLiteral_(text, cursor + prefixLength);
                setFormat(cursor, end - cursor, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (cursor + 1 < textLength && text[cursor] == 'R' && text[cursor + 1] == '"') {
                const int end = parseRawStringLiteral_(text, cursor);
                setFormat(cursor, end - cursor, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (ch == '"' || ch == '\'') {
                const int end = parseQuotedLiteral_(text, cursor);
                setFormat(cursor, end - cursor, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (std::isdigit(uch) != 0 || (ch == '.' && cursor + 1 < textLength &&
                                           std::isdigit(static_cast<unsigned char>(text[cursor + 1])) != 0)) {
                const int end = parseNumberLiteral_(text, cursor);
                setFormat(cursor, end - cursor, m_theme.numberFormat);
                cursor = end;
                continue;
            }

            if (!isIdentifierStart_(ch)) {
                if (ch == ';' || ch == '{' || ch == '}' || ch == '=') {
                    expectTypeName = false;
                    expectNamespaceName = false;
                }
                ++cursor;
                continue;
            }

            const int start = cursor;
            ++cursor;
            while (cursor < textLength && isIdentifierPart_(text[cursor])) {
                ++cursor;
            }

            const SwString word = text.substr(static_cast<size_t>(start), static_cast<size_t>(cursor - start));
            const int nextNonSpace = nextNonSpace_(text, cursor);
            const bool hasScopeResolution = nextNonSpace + 1 < textLength &&
                                            text[nextNonSpace] == ':' &&
                                            text[nextNonSpace + 1] == ':';
            const bool functionCall = nextNonSpace < textLength && text[nextNonSpace] == '(';

            if (expectTypeName) {
                setFormat(start, cursor - start, m_theme.typeFormat);
                expectTypeName = false;
                continue;
            }

            if (expectNamespaceName) {
                setFormat(start, cursor - start, m_theme.namespaceFormat);
                expectNamespaceName = false;
                continue;
            }

            if (isTypeKeyword_(word)) {
                setFormat(start, cursor - start, m_theme.typeFormat);
                continue;
            }

            if (isKeyword_(word)) {
                setFormat(start, cursor - start, m_theme.keywordFormat);
                if (introducesTypeName_(word)) {
                    expectTypeName = true;
                } else if (word == SwString("namespace")) {
                    expectNamespaceName = true;
                }
                continue;
            }

            if (hasScopeResolution) {
                setFormat(start, cursor - start, m_theme.namespaceFormat);
                continue;
            }

            if (isMacroLike_(word)) {
                setFormat(start, cursor - start, m_theme.macroFormat);
                continue;
            }

            if (functionCall) {
                setFormat(start, cursor - start, m_theme.functionFormat);
            }
        }
    }

private:
    enum BlockState {
        BlockCommentState = 1
    };

    static bool isIdentifierStart_(char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalpha(uch) != 0 || uch == static_cast<unsigned char>('_');
    }

    static bool isIdentifierPart_(char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) != 0 || uch == static_cast<unsigned char>('_');
    }

    static int firstNonSpace_(const SwString& text, int start) {
        const int textLength = static_cast<int>(text.size());
        int cursor = std::max(0, start);
        while (cursor < textLength && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        return (cursor < textLength) ? cursor : -1;
    }

    static int nextNonSpace_(const SwString& text, int start) {
        const int textLength = static_cast<int>(text.size());
        int cursor = std::max(0, start);
        while (cursor < textLength && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        return cursor;
    }

    static bool equalsAny_(const SwString& word, const char* const* values, int count) {
        for (int i = 0; i < count; ++i) {
            if (word == SwString(values[i])) {
                return true;
            }
        }
        return false;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "alignas", "alignof", "asm", "auto", "break", "case", "catch", "class",
            "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue",
            "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do",
            "else", "enum", "explicit", "export", "extern", "false", "final", "for",
            "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept",
            "nullptr", "operator", "override", "private", "protected", "public", "register",
            "reinterpret_cast", "requires", "return", "sizeof", "static", "static_assert",
            "struct", "switch", "template", "this", "thread_local", "throw", "true", "try",
            "typedef", "typeid", "typename", "union", "using", "virtual", "volatile",
            "while"
        };
        return equalsAny_(word, keywords, static_cast<int>(sizeof(keywords) / sizeof(keywords[0])));
    }

    static bool isTypeKeyword_(const SwString& word) {
        static const char* const types[] = {
            "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int",
            "long", "short", "signed", "size_t", "ssize_t", "std", "string", "u16string",
            "u32string", "u8string", "unsigned", "void", "wchar_t"
        };
        return equalsAny_(word, types, static_cast<int>(sizeof(types) / sizeof(types[0])));
    }

    static bool introducesTypeName_(const SwString& word) {
        return word == SwString("class") ||
               word == SwString("struct") ||
               word == SwString("enum") ||
               word == SwString("typename") ||
               word == SwString("using") ||
               word == SwString("concept");
    }

    static bool isMacroLike_(const SwString& word) {
        bool hasUpper = false;
        for (size_t i = 0; i < word.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(word[i]);
            if (std::isalpha(ch) != 0) {
                if (std::islower(ch) != 0) {
                    return false;
                }
                hasUpper = true;
                continue;
            }
            if (std::isdigit(ch) != 0 || ch == static_cast<unsigned char>('_')) {
                continue;
            }
            return false;
        }
        return hasUpper;
    }

    static int stringPrefixLength_(const SwString& text, int start) {
        const int textLength = static_cast<int>(text.size());
        if (start + 1 >= textLength) {
            return 0;
        }

        if (text[start] == 'u' && text[start + 1] == '8') {
            return 2;
        }
        if (text[start] == 'u' || text[start] == 'U' || text[start] == 'L') {
            return 1;
        }
        return 0;
    }

    static int parseQuotedLiteral_(const SwString& text, int quoteIndex) {
        const int textLength = static_cast<int>(text.size());
        const char quote = text[quoteIndex];
        int cursor = quoteIndex + 1;
        bool escaped = false;
        while (cursor < textLength) {
            const char ch = text[cursor];
            if (!escaped && ch == quote) {
                return cursor + 1;
            }
            if (!escaped && ch == '\\') {
                escaped = true;
            } else {
                escaped = false;
            }
            ++cursor;
        }
        return textLength;
    }

    static int parseRawStringLiteral_(const SwString& text, int start) {
        const int textLength = static_cast<int>(text.size());
        int cursor = start + 2;
        const int delimiterStart = cursor;
        while (cursor < textLength && text[cursor] != '(') {
            ++cursor;
        }
        if (cursor >= textLength) {
            return textLength;
        }

        const SwString delimiter = text.substr(static_cast<size_t>(delimiterStart),
                                               static_cast<size_t>(cursor - delimiterStart));
        ++cursor;
        while (cursor < textLength) {
            if (text[cursor] == ')') {
                const int suffixStart = cursor + 1;
                if (suffixStart + static_cast<int>(delimiter.size()) < textLength &&
                    text.substr(static_cast<size_t>(suffixStart), delimiter.size()) == delimiter &&
                    text[suffixStart + static_cast<int>(delimiter.size())] == '"') {
                    return suffixStart + static_cast<int>(delimiter.size()) + 1;
                }
            }
            ++cursor;
        }
        return textLength;
    }

    static int parseNumberLiteral_(const SwString& text, int start) {
        const int textLength = static_cast<int>(text.size());
        int cursor = start;

        if (cursor < textLength && text[cursor] == '.') {
            ++cursor;
            while (cursor < textLength &&
                   (std::isdigit(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '\'')) {
                ++cursor;
            }
        } else if (cursor + 1 < textLength && text[cursor] == '0' &&
                   (text[cursor + 1] == 'x' || text[cursor + 1] == 'X')) {
            cursor += 2;
            while (cursor < textLength &&
                   (std::isxdigit(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '\'')) {
                ++cursor;
            }
        } else if (cursor + 1 < textLength && text[cursor] == '0' &&
                   (text[cursor + 1] == 'b' || text[cursor + 1] == 'B')) {
            cursor += 2;
            while (cursor < textLength &&
                   (text[cursor] == '0' || text[cursor] == '1' || text[cursor] == '\'')) {
                ++cursor;
            }
        } else {
            while (cursor < textLength &&
                   (std::isdigit(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '\'')) {
                ++cursor;
            }
            if (cursor < textLength && text[cursor] == '.') {
                ++cursor;
                while (cursor < textLength &&
                       (std::isdigit(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '\'')) {
                    ++cursor;
                }
            }
        }

        if (cursor < textLength && (text[cursor] == 'e' || text[cursor] == 'E' ||
                                    text[cursor] == 'p' || text[cursor] == 'P')) {
            ++cursor;
            if (cursor < textLength && (text[cursor] == '+' || text[cursor] == '-')) {
                ++cursor;
            }
            while (cursor < textLength &&
                   (std::isalnum(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '\'')) {
                ++cursor;
            }
        }

        while (cursor < textLength &&
               (std::isalnum(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '_')) {
            ++cursor;
        }
        return cursor;
    }

    static int findBlockCommentEnd_(const SwString& text, int start) {
        const int textLength = static_cast<int>(text.size());
        int cursor = std::max(0, start);
        while (cursor + 1 < textLength) {
            if (text[cursor] == '*' && text[cursor + 1] == '/') {
                return cursor + 2;
            }
            ++cursor;
        }
        return -1;
    }

    int highlightPreprocessor_(const SwString& text, int hashPos) {
        const int textLength = static_cast<int>(text.size());
        int cursor = hashPos + 1;
        while (cursor < textLength && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }

        const int directiveStart = cursor;
        while (cursor < textLength && isIdentifierPart_(text[cursor])) {
            ++cursor;
        }

        if (directiveStart == cursor) {
            setFormat(hashPos, 1, m_theme.preprocessorFormat);
            return cursor;
        }

        setFormat(hashPos, cursor - hashPos, m_theme.preprocessorFormat);

        const SwString directive = text.substr(static_cast<size_t>(directiveStart),
                                               static_cast<size_t>(cursor - directiveStart));

        if (directive == SwString("include") || directive == SwString("include_next")) {
            const int pathStart = nextNonSpace_(text, cursor);
            if (pathStart < textLength) {
                if (text[pathStart] == '"') {
                    const int pathEnd = parseQuotedLiteral_(text, pathStart);
                    setFormat(pathStart, pathEnd - pathStart, m_theme.stringFormat);
                    return pathEnd;
                }
                if (text[pathStart] == '<') {
                    int pathEnd = pathStart + 1;
                    while (pathEnd < textLength && text[pathEnd] != '>') {
                        ++pathEnd;
                    }
                    if (pathEnd < textLength) {
                        ++pathEnd;
                    }
                    setFormat(pathStart, pathEnd - pathStart, m_theme.stringFormat);
                    return pathEnd;
                }
            }
        } else if (directive == SwString("define")) {
            const int macroStart = nextNonSpace_(text, cursor);
            int macroEnd = macroStart;
            while (macroEnd < textLength && isIdentifierPart_(text[macroEnd])) {
                ++macroEnd;
            }
            if (macroEnd > macroStart) {
                setFormat(macroStart, macroEnd - macroStart, m_theme.macroFormat);
                return macroEnd;
            }
        }

        return cursor;
    }

    SwCppSyntaxTheme m_theme;
};
