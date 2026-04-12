#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwCssSyntaxTheme {
    SwTextCharFormat selectorFormat;
    SwTextCharFormat propertyFormat;
    SwTextCharFormat valueFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat unitFormat;
    SwTextCharFormat colorFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat atRuleFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat importantFormat;
    SwTextCharFormat variableFormat;
};

inline SwTextCharFormat swCssMakeFormat_(const SwColor& color,
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

inline SwCssSyntaxTheme swCssSyntaxThemeVsCodeDark() {
    SwCssSyntaxTheme theme;
    theme.selectorFormat  = swCssMakeFormat_(SwColor{215, 186, 125});
    theme.propertyFormat  = swCssMakeFormat_(SwColor{156, 220, 254});
    theme.valueFormat     = swCssMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat    = swCssMakeFormat_(SwColor{181, 206, 168});
    theme.unitFormat      = swCssMakeFormat_(SwColor{181, 206, 168});
    theme.colorFormat     = swCssMakeFormat_(SwColor{206, 145, 120});
    theme.stringFormat    = swCssMakeFormat_(SwColor{206, 145, 120});
    theme.commentFormat   = swCssMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.atRuleFormat    = swCssMakeFormat_(SwColor{197, 134, 192});
    theme.functionFormat  = swCssMakeFormat_(SwColor{220, 220, 170});
    theme.importantFormat = swCssMakeFormat_(SwColor{197, 134, 192}, Medium);
    theme.variableFormat  = swCssMakeFormat_(SwColor{156, 220, 254});
    return theme;
}

class SwCssSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwCssSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwCssSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swCssSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwCssSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        int state = previousBlockState();
        if (state < 0) { state = SelectorState; }
        setCurrentBlockState(state);

        if (state == CommentState) {
            const int end = findCommentEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.commentFormat);
                setCurrentBlockState(CommentState);
                return;
            }
            setFormat(0, end, m_theme.commentFormat);
            cursor = end;
            state = previousContextState_();
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '/' && cursor + 1 < len && text[cursor + 1] == '*') {
                const int end = findCommentEnd_(text, cursor + 2);
                if (end < 0) {
                    setFormat(cursor, len - cursor, m_theme.commentFormat);
                    setCurrentBlockState(CommentState);
                    return;
                }
                setFormat(cursor, end - cursor, m_theme.commentFormat);
                cursor = end;
                continue;
            }

            if (ch == '/' && cursor + 1 < len && text[cursor + 1] == '/') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
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

            if (ch == '@') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && isIdentChar_(text[cursor])) { ++cursor; }
                setFormat(start, cursor - start, m_theme.atRuleFormat);
                continue;
            }

            if (ch == '-' && cursor + 1 < len && text[cursor + 1] == '-') {
                const int start = cursor;
                while (cursor < len && isIdentChar_(text[cursor]) || text[cursor] == '-') { ++cursor; }
                setFormat(start, cursor - start, m_theme.variableFormat);
                continue;
            }

            if (ch == '$' && cursor + 1 < len && isIdentChar_(text[cursor + 1])) {
                const int start = cursor;
                ++cursor;
                while (cursor < len && (isIdentChar_(text[cursor]) || text[cursor] == '-')) { ++cursor; }
                setFormat(start, cursor - start, m_theme.variableFormat);
                continue;
            }

            if (ch == '{') {
                state = PropertyState;
                setCurrentBlockState(PropertyState);
                ++cursor;
                continue;
            }

            if (ch == '}') {
                state = SelectorState;
                setCurrentBlockState(SelectorState);
                ++cursor;
                continue;
            }

            if (state == SelectorState) {
                const int start = cursor;
                while (cursor < len && text[cursor] != '{' && text[cursor] != '/' &&
                       text[cursor] != '@' && text[cursor] != '"' && text[cursor] != '\'') {
                    ++cursor;
                }
                if (cursor > start) {
                    setFormat(start, cursor - start, m_theme.selectorFormat);
                }
                continue;
            }

            if (state == PropertyState) {
                if (ch == ':') {
                    state = ValueState;
                    setCurrentBlockState(ValueState);
                    ++cursor;
                    continue;
                }

                if (isIdentStart_(ch) || ch == '-') {
                    const int start = cursor;
                    while (cursor < len && (isIdentChar_(text[cursor]) || text[cursor] == '-')) { ++cursor; }
                    setFormat(start, cursor - start, m_theme.propertyFormat);
                    continue;
                }
            }

            if (state == ValueState) {
                if (ch == ';') {
                    state = PropertyState;
                    setCurrentBlockState(PropertyState);
                    ++cursor;
                    continue;
                }

                if (ch == '!' && cursor + 9 <= len &&
                    text.substr(cursor, 10).toLower() == SwString("!important")) {
                    setFormat(cursor, 10, m_theme.importantFormat);
                    cursor += 10;
                    continue;
                }

                if (ch == '#' && cursor + 1 < len && std::isxdigit(static_cast<unsigned char>(text[cursor + 1]))) {
                    const int start = cursor;
                    ++cursor;
                    while (cursor < len && std::isxdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                    setFormat(start, cursor - start, m_theme.colorFormat);
                    continue;
                }

                if (std::isdigit(static_cast<unsigned char>(ch)) ||
                    (ch == '.' && cursor + 1 < len && std::isdigit(static_cast<unsigned char>(text[cursor + 1]))) ||
                    (ch == '-' && cursor + 1 < len && (std::isdigit(static_cast<unsigned char>(text[cursor + 1])) ||
                     text[cursor + 1] == '.'))) {
                    const int start = cursor;
                    if (ch == '-') { ++cursor; }
                    while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '.')) {
                        ++cursor;
                    }
                    setFormat(start, cursor - start, m_theme.numberFormat);
                    if (cursor < len && isIdentStart_(text[cursor])) {
                        const int unitStart = cursor;
                        while (cursor < len && isIdentChar_(text[cursor])) { ++cursor; }
                        setFormat(unitStart, cursor - unitStart, m_theme.unitFormat);
                    }
                    if (cursor < len && text[cursor] == '%') {
                        setFormat(cursor, 1, m_theme.unitFormat);
                        ++cursor;
                    }
                    continue;
                }

                if (isIdentStart_(ch)) {
                    const int start = cursor;
                    while (cursor < len && (isIdentChar_(text[cursor]) || text[cursor] == '-')) { ++cursor; }
                    int peek = cursor;
                    while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                    if (peek < len && text[peek] == '(') {
                        setFormat(start, cursor - start, m_theme.functionFormat);
                    } else {
                        setFormat(start, cursor - start, m_theme.valueFormat);
                    }
                    continue;
                }
            }

            ++cursor;
        }
    }

private:
    enum BlockState {
        SelectorState = 0,
        PropertyState = 1,
        ValueState = 2,
        CommentState = 3
    };

    static bool isIdentStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isIdentChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_' || u == '-';
    }

    static int findCommentEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor + 1 < len) {
            if (text[cursor] == '*' && text[cursor + 1] == '/') {
                return cursor + 2;
            }
            ++cursor;
        }
        return -1;
    }

    int previousContextState_() const {
        return PropertyState;
    }

    SwCssSyntaxTheme m_theme;
};
