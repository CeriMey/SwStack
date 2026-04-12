#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwYamlSyntaxTheme {
    SwTextCharFormat keyFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat boolNullFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat anchorFormat;
    SwTextCharFormat tagFormat;
    SwTextCharFormat separatorFormat;
};

inline SwTextCharFormat swYamlMakeFormat_(const SwColor& color,
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

inline SwYamlSyntaxTheme swYamlSyntaxThemeVsCodeDark() {
    SwYamlSyntaxTheme theme;
    theme.keyFormat       = swYamlMakeFormat_(SwColor{156, 220, 254});
    theme.stringFormat    = swYamlMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat    = swYamlMakeFormat_(SwColor{181, 206, 168});
    theme.boolNullFormat  = swYamlMakeFormat_(SwColor{86, 156, 214});
    theme.commentFormat   = swYamlMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.anchorFormat    = swYamlMakeFormat_(SwColor{78, 201, 176});
    theme.tagFormat       = swYamlMakeFormat_(SwColor{78, 201, 176});
    theme.separatorFormat = swYamlMakeFormat_(SwColor{197, 134, 192});
    return theme;
}

class SwYamlSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwYamlSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwYamlSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swYamlSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwYamlSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        if (len == 0) {
            return;
        }

        setCurrentBlockState(0);

        int cursor = 0;

        if (text.trimmed() == SwString("---") || text.trimmed() == SwString("...")) {
            setFormat(0, len, m_theme.separatorFormat);
            return;
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '#') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (ch == '&' || ch == '*') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && isAnchorChar_(text[cursor])) { ++cursor; }
                setFormat(start, cursor - start, m_theme.anchorFormat);
                continue;
            }

            if (ch == '!' && cursor + 1 < len) {
                const int start = cursor;
                ++cursor;
                if (cursor < len && text[cursor] == '!') { ++cursor; }
                while (cursor < len && !std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                setFormat(start, cursor - start, m_theme.tagFormat);
                continue;
            }

            if (ch == '"' || ch == '\'') {
                const int start = cursor;
                const int end = findClosingQuote_(text, cursor, ch);
                setFormat(start, end - start, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (isKeyStart_(text, cursor)) {
                const int start = cursor;
                while (cursor < len && text[cursor] != ':') { ++cursor; }
                setFormat(start, cursor - start, m_theme.keyFormat);
                if (cursor < len) {
                    setFormat(cursor, 1, m_theme.separatorFormat);
                    ++cursor;
                }

                while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }

                if (cursor < len && text[cursor] == '#') {
                    setFormat(cursor, len - cursor, m_theme.commentFormat);
                    return;
                }

                if (cursor < len) {
                    highlightValue_(text, cursor);
                }
                return;
            }

            if (ch == '-' && (cursor + 1 >= len || std::isspace(static_cast<unsigned char>(text[cursor + 1])))) {
                setFormat(cursor, 1, m_theme.separatorFormat);
                ++cursor;
                while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                if (cursor < len) {
                    if (isKeyStart_(text, cursor)) {
                        continue;
                    }
                    highlightValue_(text, cursor);
                }
                return;
            }

            highlightValue_(text, cursor);
            return;
        }
    }

private:
    static bool isAnchorChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) || u == '_' || u == '-' || u == '.';
    }

    static int findClosingQuote_(const SwString& text, int start, char quote) {
        const int len = static_cast<int>(text.size());
        int cursor = start + 1;
        while (cursor < len) {
            if (text[cursor] == '\\' && cursor + 1 < len) { cursor += 2; continue; }
            if (text[cursor] == quote) {
                if (quote == '\'' && cursor + 1 < len && text[cursor + 1] == '\'') {
                    cursor += 2;
                    continue;
                }
                return cursor + 1;
            }
            ++cursor;
        }
        return len;
    }

    static bool isKeyStart_(const SwString& text, int pos) {
        const int len = static_cast<int>(text.size());
        if (pos >= len) { return false; }

        if (text[pos] == '"' || text[pos] == '\'') {
            const char q = text[pos];
            int cursor = pos + 1;
            while (cursor < len && text[cursor] != q) {
                if (text[cursor] == '\\') { ++cursor; }
                ++cursor;
            }
            if (cursor < len) { ++cursor; }
            while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
            return cursor < len && text[cursor] == ':' &&
                   (cursor + 1 >= len || std::isspace(static_cast<unsigned char>(text[cursor + 1])));
        }

        int cursor = pos;
        while (cursor < len && text[cursor] != ':' && text[cursor] != '#') {
            ++cursor;
        }
        if (cursor >= len || text[cursor] != ':') { return false; }
        return cursor + 1 >= len || std::isspace(static_cast<unsigned char>(text[cursor + 1]));
    }

    void highlightValue_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;

        while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
        if (cursor >= len) { return; }

        if (text[cursor] == '#') {
            setFormat(cursor, len - cursor, m_theme.commentFormat);
            return;
        }

        if (text[cursor] == '"' || text[cursor] == '\'') {
            const int s = cursor;
            const int end = findClosingQuote_(text, cursor, text[cursor]);
            setFormat(s, end - s, m_theme.stringFormat);
            cursor = end;
            while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
            if (cursor < len && text[cursor] == '#') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
            }
            return;
        }

        int valEnd = cursor;
        int commentStart = -1;
        while (valEnd < len) {
            if (text[valEnd] == '#' && valEnd > 0 && std::isspace(static_cast<unsigned char>(text[valEnd - 1]))) {
                commentStart = valEnd;
                break;
            }
            ++valEnd;
        }
        if (commentStart < 0) { valEnd = len; }

        const SwString value = text.substr(cursor, valEnd - cursor).trimmed();

        if (isBoolOrNull_(value)) {
            setFormat(cursor, valEnd - cursor, m_theme.boolNullFormat);
        } else if (isNumber_(value)) {
            setFormat(cursor, valEnd - cursor, m_theme.numberFormat);
        } else if (!value.isEmpty()) {
            setFormat(cursor, valEnd - cursor, m_theme.stringFormat);
        }

        if (commentStart >= 0) {
            setFormat(commentStart, len - commentStart, m_theme.commentFormat);
        }
    }

    static bool isBoolOrNull_(const SwString& value) {
        return value == SwString("true") || value == SwString("false") ||
               value == SwString("True") || value == SwString("False") ||
               value == SwString("TRUE") || value == SwString("FALSE") ||
               value == SwString("yes") || value == SwString("no") ||
               value == SwString("Yes") || value == SwString("No") ||
               value == SwString("YES") || value == SwString("NO") ||
               value == SwString("null") || value == SwString("Null") ||
               value == SwString("NULL") || value == SwString("~");
    }

    static bool isNumber_(const SwString& value) {
        if (value.isEmpty()) { return false; }
        int cursor = 0;
        const int len = static_cast<int>(value.size());
        if (value[0] == '-' || value[0] == '+') { ++cursor; }
        if (cursor >= len) { return false; }

        if (cursor + 1 < len && value[cursor] == '0') {
            const char n = value[cursor + 1];
            if (n == 'x' || n == 'X' || n == 'o' || n == 'O') {
                cursor += 2;
                while (cursor < len && std::isxdigit(static_cast<unsigned char>(value[cursor]))) { ++cursor; }
                return cursor == len;
            }
        }

        bool hasDot = false;
        bool hasDigit = false;
        while (cursor < len) {
            if (std::isdigit(static_cast<unsigned char>(value[cursor]))) {
                hasDigit = true;
                ++cursor;
            } else if (value[cursor] == '.' && !hasDot) {
                hasDot = true;
                ++cursor;
            } else if ((value[cursor] == 'e' || value[cursor] == 'E') && hasDigit) {
                ++cursor;
                if (cursor < len && (value[cursor] == '+' || value[cursor] == '-')) { ++cursor; }
                while (cursor < len && std::isdigit(static_cast<unsigned char>(value[cursor]))) { ++cursor; }
                return cursor == len;
            } else {
                break;
            }
        }
        return hasDigit && cursor == len;
    }

    SwYamlSyntaxTheme m_theme;
};
