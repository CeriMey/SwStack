#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwXmlSyntaxTheme {
    SwTextCharFormat tagFormat;
    SwTextCharFormat attributeFormat;
    SwTextCharFormat valueFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat cdataFormat;
    SwTextCharFormat entityFormat;
    SwTextCharFormat doctypeFormat;
    SwTextCharFormat piFormat;
};

inline SwTextCharFormat swXmlMakeFormat_(const SwColor& color,
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

inline SwXmlSyntaxTheme swXmlSyntaxThemeVsCodeDark() {
    SwXmlSyntaxTheme theme;
    theme.tagFormat       = swXmlMakeFormat_(SwColor{86, 156, 214});
    theme.attributeFormat = swXmlMakeFormat_(SwColor{156, 220, 254});
    theme.valueFormat     = swXmlMakeFormat_(SwColor{206, 145, 120});
    theme.commentFormat   = swXmlMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.cdataFormat     = swXmlMakeFormat_(SwColor{181, 206, 168});
    theme.entityFormat    = swXmlMakeFormat_(SwColor{78, 201, 176});
    theme.doctypeFormat   = swXmlMakeFormat_(SwColor{197, 134, 192});
    theme.piFormat        = swXmlMakeFormat_(SwColor{197, 134, 192});
    return theme;
}

class SwXmlSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwXmlSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwXmlSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swXmlSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwXmlSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        if (previousBlockState() == CommentState) {
            const int end = findCommentEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.commentFormat);
                setCurrentBlockState(CommentState);
                return;
            }
            setFormat(0, end, m_theme.commentFormat);
            cursor = end;
        }

        if (previousBlockState() == TagState) {
            cursor = highlightInsideTag_(text, cursor);
        }

        if (previousBlockState() == CDataState) {
            const int end = findCDataEnd_(text, 0);
            if (end < 0) {
                setFormat(0, len, m_theme.cdataFormat);
                setCurrentBlockState(CDataState);
                return;
            }
            setFormat(0, end, m_theme.cdataFormat);
            cursor = end;
        }

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '&') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && text[cursor] != ';' && !std::isspace(static_cast<unsigned char>(text[cursor]))) {
                    ++cursor;
                }
                if (cursor < len && text[cursor] == ';') {
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.entityFormat);
                continue;
            }

            if (ch == '<') {
                if (cursor + 3 < len && text[cursor + 1] == '!' && text[cursor + 2] == '-' && text[cursor + 3] == '-') {
                    const int end = findCommentEnd_(text, cursor + 4);
                    if (end < 0) {
                        setFormat(cursor, len - cursor, m_theme.commentFormat);
                        setCurrentBlockState(CommentState);
                        return;
                    }
                    setFormat(cursor, end - cursor, m_theme.commentFormat);
                    cursor = end;
                    continue;
                }

                if (cursor + 8 < len && text.substr(cursor, 9) == SwString("<![CDATA[")) {
                    const int end = findCDataEnd_(text, cursor + 9);
                    if (end < 0) {
                        setFormat(cursor, len - cursor, m_theme.cdataFormat);
                        setCurrentBlockState(CDataState);
                        return;
                    }
                    setFormat(cursor, end - cursor, m_theme.cdataFormat);
                    cursor = end;
                    continue;
                }

                if (cursor + 1 < len && text[cursor + 1] == '!') {
                    const int start = cursor;
                    while (cursor < len && text[cursor] != '>') { ++cursor; }
                    if (cursor < len) { ++cursor; }
                    setFormat(start, cursor - start, m_theme.doctypeFormat);
                    continue;
                }

                if (cursor + 1 < len && text[cursor + 1] == '?') {
                    const int start = cursor;
                    while (cursor + 1 < len) {
                        if (text[cursor] == '?' && text[cursor + 1] == '>') {
                            cursor += 2;
                            break;
                        }
                        ++cursor;
                    }
                    if (cursor >= len) { cursor = len; }
                    setFormat(start, cursor - start, m_theme.piFormat);
                    continue;
                }

                const int tagStart = cursor;
                ++cursor;

                if (cursor < len && text[cursor] == '/') {
                    ++cursor;
                }

                const int nameStart = cursor;
                while (cursor < len && isTagNameChar_(text[cursor])) {
                    ++cursor;
                }
                setFormat(tagStart, cursor - tagStart, m_theme.tagFormat);

                cursor = highlightInsideTag_(text, cursor);
                continue;
            }

            ++cursor;
        }
    }

private:
    enum BlockState {
        NormalState = 0,
        CommentState = 1,
        TagState = 2,
        CDataState = 3
    };

    static bool isTagNameChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_' || u == '-' || u == '.' || u == ':';
    }

    int highlightInsideTag_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;

        while (cursor < len) {
            while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
                ++cursor;
            }
            if (cursor >= len) {
                setCurrentBlockState(TagState);
                return len;
            }

            if (text[cursor] == '>') {
                setFormat(cursor, 1, m_theme.tagFormat);
                return cursor + 1;
            }

            if (text[cursor] == '/' && cursor + 1 < len && text[cursor + 1] == '>') {
                setFormat(cursor, 2, m_theme.tagFormat);
                return cursor + 2;
            }

            if (isTagNameChar_(text[cursor])) {
                const int attrStart = cursor;
                while (cursor < len && isTagNameChar_(text[cursor])) {
                    ++cursor;
                }
                setFormat(attrStart, cursor - attrStart, m_theme.attributeFormat);

                while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
                    ++cursor;
                }
                if (cursor < len && text[cursor] == '=') {
                    ++cursor;
                    while (cursor < len && std::isspace(static_cast<unsigned char>(text[cursor]))) {
                        ++cursor;
                    }
                    if (cursor < len && (text[cursor] == '"' || text[cursor] == '\'')) {
                        const char q = text[cursor];
                        const int valStart = cursor;
                        ++cursor;
                        while (cursor < len && text[cursor] != q) {
                            ++cursor;
                        }
                        if (cursor < len) { ++cursor; }
                        setFormat(valStart, cursor - valStart, m_theme.valueFormat);
                    }
                }
                continue;
            }

            ++cursor;
        }
        setCurrentBlockState(TagState);
        return len;
    }

    static int findCommentEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor + 2 < len) {
            if (text[cursor] == '-' && text[cursor + 1] == '-' && text[cursor + 2] == '>') {
                return cursor + 3;
            }
            ++cursor;
        }
        return -1;
    }

    static int findCDataEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor + 2 < len) {
            if (text[cursor] == ']' && text[cursor + 1] == ']' && text[cursor + 2] == '>') {
                return cursor + 3;
            }
            ++cursor;
        }
        return -1;
    }

    SwXmlSyntaxTheme m_theme;
};
