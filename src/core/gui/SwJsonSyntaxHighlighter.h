#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwJsonSyntaxTheme {
    SwTextCharFormat keyFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat boolNullFormat;
    SwTextCharFormat bracketFormat;
    SwTextCharFormat colonFormat;
};

inline SwTextCharFormat swJsonMakeFormat_(const SwColor& color,
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

inline SwJsonSyntaxTheme swJsonSyntaxThemeVsCodeDark() {
    SwJsonSyntaxTheme theme;
    theme.keyFormat      = swJsonMakeFormat_(SwColor{156, 220, 254});
    theme.stringFormat   = swJsonMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat   = swJsonMakeFormat_(SwColor{181, 206, 168});
    theme.boolNullFormat = swJsonMakeFormat_(SwColor{86, 156, 214});
    theme.bracketFormat  = swJsonMakeFormat_(SwColor{212, 212, 212});
    theme.colonFormat    = swJsonMakeFormat_(SwColor{212, 212, 212});
    return theme;
}

class SwJsonSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwJsonSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwJsonSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swJsonSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwJsonSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        while (cursor < len) {
            const char ch = text[cursor];

            if (std::isspace(static_cast<unsigned char>(ch))) {
                ++cursor;
                continue;
            }

            if (ch == '{' || ch == '}' || ch == '[' || ch == ']') {
                setFormat(cursor, 1, m_theme.bracketFormat);
                ++cursor;
                continue;
            }

            if (ch == ':') {
                setFormat(cursor, 1, m_theme.colonFormat);
                ++cursor;
                continue;
            }

            if (ch == ',') {
                ++cursor;
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
                    if (text[cursor] == '"') {
                        ++cursor;
                        break;
                    }
                    ++cursor;
                }

                int peek = cursor;
                while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                if (peek < len && text[peek] == ':') {
                    setFormat(start, cursor - start, m_theme.keyFormat);
                } else {
                    setFormat(start, cursor - start, m_theme.stringFormat);
                }
                continue;
            }

            if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
                const int start = cursor;
                if (ch == '-') { ++cursor; }
                while (cursor < len && std::isdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                if (cursor < len && text[cursor] == '.') {
                    ++cursor;
                    while (cursor < len && std::isdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                }
                if (cursor < len && (text[cursor] == 'e' || text[cursor] == 'E')) {
                    ++cursor;
                    if (cursor < len && (text[cursor] == '+' || text[cursor] == '-')) { ++cursor; }
                    while (cursor < len && std::isdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                }
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isWordStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isWordChar_(text[cursor])) { ++cursor; }
                const SwString word = text.substr(start, cursor - start);
                if (word == SwString("true") || word == SwString("false") || word == SwString("null")) {
                    setFormat(start, cursor - start, m_theme.boolNullFormat);
                }
                continue;
            }

            ++cursor;
        }
    }

private:
    static bool isWordStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0;
    }

    static bool isWordChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0;
    }

    SwJsonSyntaxTheme m_theme;
};
