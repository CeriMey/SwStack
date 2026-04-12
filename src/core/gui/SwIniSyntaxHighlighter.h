#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwIniSyntaxTheme {
    SwTextCharFormat sectionFormat;
    SwTextCharFormat keyFormat;
    SwTextCharFormat valueFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat equalFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat boolFormat;
};

inline SwTextCharFormat swIniMakeFormat_(const SwColor& color,
                                          FontWeight weight = Normal,
                                          bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) { format.setFontWeight(weight); }
    if (italic) { format.setFontItalic(true); }
    return format;
}

inline SwIniSyntaxTheme swIniSyntaxThemeVsCodeDark() {
    SwIniSyntaxTheme theme;
    theme.sectionFormat = swIniMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.keyFormat     = swIniMakeFormat_(SwColor{156, 220, 254});
    theme.valueFormat   = swIniMakeFormat_(SwColor{206, 145, 120});
    theme.commentFormat = swIniMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.equalFormat   = swIniMakeFormat_(SwColor{212, 212, 212});
    theme.stringFormat  = swIniMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat  = swIniMakeFormat_(SwColor{181, 206, 168});
    theme.boolFormat    = swIniMakeFormat_(SwColor{86, 156, 214});
    return theme;
}

class SwIniSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwIniSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwIniSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swIniSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwIniSyntaxTheme& theme) {
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
        while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
            ++cursor;
        }
        if (cursor >= len) {
            return;
        }

        const char first = text[cursor];

        if (first == '#' || first == ';') {
            setFormat(0, len, m_theme.commentFormat);
            return;
        }

        if (first == '[') {
            const int start = cursor;
            while (cursor < len && text[cursor] != ']') { ++cursor; }
            if (cursor < len) { ++cursor; }
            setFormat(start, cursor - start, m_theme.sectionFormat);
            if (cursor < len) {
                int remaining = cursor;
                while (remaining < len && std::isspace(static_cast<unsigned char>(text[remaining]))) { ++remaining; }
                if (remaining < len && (text[remaining] == '#' || text[remaining] == ';')) {
                    setFormat(remaining, len - remaining, m_theme.commentFormat);
                }
            }
            return;
        }

        const int keyStart = cursor;
        while (cursor < len && text[cursor] != '=' && text[cursor] != ':') {
            ++cursor;
        }
        if (cursor >= len) {
            setFormat(keyStart, len - keyStart, m_theme.keyFormat);
            return;
        }

        int keyEnd = cursor;
        while (keyEnd > keyStart && std::isspace(static_cast<unsigned char>(text[keyEnd - 1]))) { --keyEnd; }
        setFormat(keyStart, keyEnd - keyStart, m_theme.keyFormat);

        setFormat(cursor, 1, m_theme.equalFormat);
        ++cursor;

        while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
        if (cursor >= len) { return; }

        int commentStart = -1;
        int valEnd = len;
        {
            bool inQuote = false;
            char qChar = 0;
            for (int i = cursor; i < len; ++i) {
                if (!inQuote && (text[i] == '"' || text[i] == '\'')) {
                    inQuote = true;
                    qChar = text[i];
                } else if (inQuote && text[i] == qChar) {
                    inQuote = false;
                } else if (!inQuote && (text[i] == '#' || text[i] == ';') &&
                           i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) {
                    commentStart = i;
                    valEnd = i;
                    break;
                }
            }
        }

        if (cursor < valEnd) {
            if (text[cursor] == '"' || text[cursor] == '\'') {
                const char q = text[cursor];
                int end = cursor + 1;
                while (end < valEnd && text[end] != q) {
                    if (text[end] == '\\') { ++end; }
                    ++end;
                }
                if (end < valEnd) { ++end; }
                setFormat(cursor, end - cursor, m_theme.stringFormat);
            } else {
                const SwString value = text.substr(cursor, valEnd - cursor).trimmed();
                const SwString lower = value.toLower();
                if (lower == SwString("true") || lower == SwString("false") ||
                    lower == SwString("yes") || lower == SwString("no") ||
                    lower == SwString("on") || lower == SwString("off")) {
                    setFormat(cursor, valEnd - cursor, m_theme.boolFormat);
                } else if (isNumber_(value)) {
                    setFormat(cursor, valEnd - cursor, m_theme.numberFormat);
                } else {
                    setFormat(cursor, valEnd - cursor, m_theme.valueFormat);
                }
            }
        }

        if (commentStart >= 0) {
            setFormat(commentStart, len - commentStart, m_theme.commentFormat);
        }
    }

private:
    static bool isNumber_(const SwString& text) {
        if (text.isEmpty()) { return false; }
        int cursor = 0;
        const int len = static_cast<int>(text.size());
        if (text[0] == '-' || text[0] == '+') { ++cursor; }
        if (cursor >= len) { return false; }
        bool hasDigit = false;
        while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '.')) {
            if (std::isdigit(static_cast<unsigned char>(text[cursor]))) { hasDigit = true; }
            ++cursor;
        }
        return hasDigit && cursor == len;
    }

    SwIniSyntaxTheme m_theme;
};
