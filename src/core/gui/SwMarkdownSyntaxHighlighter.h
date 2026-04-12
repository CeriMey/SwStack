#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwMarkdownSyntaxTheme {
    SwTextCharFormat headingFormat;
    SwTextCharFormat boldFormat;
    SwTextCharFormat italicFormat;
    SwTextCharFormat codeFormat;
    SwTextCharFormat linkFormat;
    SwTextCharFormat urlFormat;
    SwTextCharFormat listMarkerFormat;
    SwTextCharFormat blockquoteFormat;
    SwTextCharFormat hrFormat;
    SwTextCharFormat imageFormat;
};

inline SwTextCharFormat swMdMakeFormat_(const SwColor& color,
                                         FontWeight weight = Normal,
                                         bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) { format.setFontWeight(weight); }
    if (italic) { format.setFontItalic(true); }
    return format;
}

inline SwMarkdownSyntaxTheme swMarkdownSyntaxThemeVsCodeDark() {
    SwMarkdownSyntaxTheme theme;
    theme.headingFormat    = swMdMakeFormat_(SwColor{86, 156, 214}, Bold);
    theme.boldFormat       = swMdMakeFormat_(SwColor{212, 212, 212}, Bold);
    theme.italicFormat     = swMdMakeFormat_(SwColor{212, 212, 212}, Normal, true);
    theme.codeFormat       = swMdMakeFormat_(SwColor{206, 145, 120});
    theme.linkFormat       = swMdMakeFormat_(SwColor{78, 201, 176});
    theme.urlFormat        = swMdMakeFormat_(SwColor{86, 156, 214});
    theme.listMarkerFormat = swMdMakeFormat_(SwColor{197, 134, 192});
    theme.blockquoteFormat = swMdMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.hrFormat         = swMdMakeFormat_(SwColor{197, 134, 192});
    theme.imageFormat      = swMdMakeFormat_(SwColor{220, 220, 170});
    return theme;
}

class SwMarkdownSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwMarkdownSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwMarkdownSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swMarkdownSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwMarkdownSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        if (len == 0) {
            setCurrentBlockState(0);
            return;
        }

        setCurrentBlockState(0);

        if (previousBlockState() == FencedCodeState) {
            if (text.trimmed().startsWith("```")) {
                setFormat(0, len, m_theme.codeFormat);
                setCurrentBlockState(0);
            } else {
                setFormat(0, len, m_theme.codeFormat);
                setCurrentBlockState(FencedCodeState);
            }
            return;
        }

        int cursor = 0;

        if (text.trimmed().startsWith("```")) {
            setFormat(0, len, m_theme.codeFormat);
            setCurrentBlockState(FencedCodeState);
            return;
        }

        {
            const SwString trimmed = text.trimmed();
            bool isHr = true;
            if (trimmed.size() >= 3) {
                const char first = trimmed[0];
                if (first == '-' || first == '*' || first == '_') {
                    for (size_t i = 0; i < trimmed.size(); ++i) {
                        if (trimmed[i] != first && trimmed[i] != ' ') { isHr = false; break; }
                    }
                    if (isHr) {
                        setFormat(0, len, m_theme.hrFormat);
                        return;
                    }
                }
            }
        }

        if (text[0] == '#') {
            int hLevel = 0;
            while (hLevel < len && hLevel < 6 && text[hLevel] == '#') { ++hLevel; }
            if (hLevel < len && text[hLevel] == ' ') {
                setFormat(0, len, m_theme.headingFormat);
                return;
            }
        }

        if (text[0] == '>') {
            setFormat(0, len, m_theme.blockquoteFormat);
            return;
        }

        while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
        if (cursor < len) {
            if ((text[cursor] == '-' || text[cursor] == '*' || text[cursor] == '+') &&
                cursor + 1 < len && text[cursor + 1] == ' ') {
                setFormat(cursor, 1, m_theme.listMarkerFormat);
                cursor += 2;
            } else if (std::isdigit(static_cast<unsigned char>(text[cursor]))) {
                int numEnd = cursor;
                while (numEnd < len && std::isdigit(static_cast<unsigned char>(text[numEnd]))) { ++numEnd; }
                if (numEnd < len && (text[numEnd] == '.' || text[numEnd] == ')') &&
                    numEnd + 1 < len && text[numEnd + 1] == ' ') {
                    setFormat(cursor, numEnd - cursor + 1, m_theme.listMarkerFormat);
                    cursor = numEnd + 2;
                }
            }
        }

        highlightInline_(text, cursor);
    }

private:
    enum BlockState {
        NormalState = 0,
        FencedCodeState = 1
    };

    void highlightInline_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '`') {
                if (cursor + 1 < len && text[cursor + 1] == '`') {
                    const int codeStart = cursor;
                    cursor += 2;
                    while (cursor + 1 < len && !(text[cursor] == '`' && text[cursor + 1] == '`')) { ++cursor; }
                    if (cursor + 1 < len) { cursor += 2; }
                    setFormat(codeStart, cursor - codeStart, m_theme.codeFormat);
                    continue;
                }
                const int codeStart = cursor;
                ++cursor;
                while (cursor < len && text[cursor] != '`') { ++cursor; }
                if (cursor < len) { ++cursor; }
                setFormat(codeStart, cursor - codeStart, m_theme.codeFormat);
                continue;
            }

            if (ch == '!' && cursor + 1 < len && text[cursor + 1] == '[') {
                const int imgStart = cursor;
                cursor += 2;
                while (cursor < len && text[cursor] != ']') { ++cursor; }
                if (cursor < len) { ++cursor; }
                if (cursor < len && text[cursor] == '(') {
                    ++cursor;
                    while (cursor < len && text[cursor] != ')') { ++cursor; }
                    if (cursor < len) { ++cursor; }
                }
                setFormat(imgStart, cursor - imgStart, m_theme.imageFormat);
                continue;
            }

            if (ch == '[') {
                const int linkStart = cursor;
                ++cursor;
                const int textStart = cursor;
                while (cursor < len && text[cursor] != ']') { ++cursor; }
                const int textEnd = cursor;
                if (cursor < len) { ++cursor; }
                if (cursor < len && text[cursor] == '(') {
                    setFormat(linkStart, textEnd - linkStart + 1, m_theme.linkFormat);
                    const int urlStart = cursor;
                    ++cursor;
                    while (cursor < len && text[cursor] != ')') { ++cursor; }
                    if (cursor < len) { ++cursor; }
                    setFormat(urlStart, cursor - urlStart, m_theme.urlFormat);
                    continue;
                }
                continue;
            }

            if (ch == '*' || ch == '_') {
                if (cursor + 2 < len && text[cursor + 1] == ch && text[cursor + 2] == ch) {
                    const int start2 = cursor;
                    cursor += 3;
                    while (cursor + 2 < len && !(text[cursor] == ch && text[cursor + 1] == ch && text[cursor + 2] == ch)) {
                        ++cursor;
                    }
                    if (cursor + 2 < len) { cursor += 3; }
                    setFormat(start2, cursor - start2, m_theme.boldFormat);
                    continue;
                }
                if (cursor + 1 < len && text[cursor + 1] == ch) {
                    const int start2 = cursor;
                    cursor += 2;
                    while (cursor + 1 < len && !(text[cursor] == ch && text[cursor + 1] == ch)) {
                        ++cursor;
                    }
                    if (cursor + 1 < len) { cursor += 2; }
                    setFormat(start2, cursor - start2, m_theme.boldFormat);
                    continue;
                }
                const int start2 = cursor;
                ++cursor;
                while (cursor < len && text[cursor] != ch) { ++cursor; }
                if (cursor < len) { ++cursor; }
                setFormat(start2, cursor - start2, m_theme.italicFormat);
                continue;
            }

            if (ch == '~' && cursor + 1 < len && text[cursor + 1] == '~') {
                const int start2 = cursor;
                cursor += 2;
                while (cursor + 1 < len && !(text[cursor] == '~' && text[cursor + 1] == '~')) { ++cursor; }
                if (cursor + 1 < len) { cursor += 2; }
                setFormat(start2, cursor - start2, m_theme.codeFormat);
                continue;
            }

            ++cursor;
        }
    }

    SwMarkdownSyntaxTheme m_theme;
};
