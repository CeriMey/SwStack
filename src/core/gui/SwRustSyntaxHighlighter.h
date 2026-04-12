#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwRustSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat macroFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat lifetimeFormat;
    SwTextCharFormat attributeFormat;
    SwTextCharFormat constantFormat;
};

inline SwTextCharFormat swRustMakeFormat_(const SwColor& color,
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

inline SwRustSyntaxTheme swRustSyntaxThemeVsCodeDark() {
    SwRustSyntaxTheme theme;
    theme.keywordFormat   = swRustMakeFormat_(SwColor{197, 134, 192}, Medium);
    theme.typeFormat      = swRustMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat   = swRustMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat    = swRustMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat    = swRustMakeFormat_(SwColor{181, 206, 168});
    theme.macroFormat     = swRustMakeFormat_(SwColor{79, 193, 255});
    theme.functionFormat  = swRustMakeFormat_(SwColor{220, 220, 170});
    theme.lifetimeFormat  = swRustMakeFormat_(SwColor{86, 156, 214}, Normal, true);
    theme.attributeFormat = swRustMakeFormat_(SwColor{220, 220, 170});
    theme.constantFormat  = swRustMakeFormat_(SwColor{79, 193, 255});
    return theme;
}

class SwRustSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwRustSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwRustSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swRustSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwRustSyntaxTheme& theme) {
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

            if (ch == '#' && cursor + 1 < len && (text[cursor + 1] == '[' || text[cursor + 1] == '!')) {
                const int start = cursor;
                if (text[cursor + 1] == '!') { ++cursor; }
                ++cursor;
                if (cursor < len && text[cursor] == '[') {
                    int depth = 1;
                    ++cursor;
                    while (cursor < len && depth > 0) {
                        if (text[cursor] == '[') { ++depth; }
                        else if (text[cursor] == ']') { --depth; }
                        ++cursor;
                    }
                }
                setFormat(start, cursor - start, m_theme.attributeFormat);
                continue;
            }

            if (ch == '"') {
                if (cursor > 0 && text[cursor - 1] == 'r') {
                    const int start = cursor - 1;
                    int hashes = 0;
                    int scan = cursor - 1;
                    while (scan > 0 && text[scan - 1] == '#') { --scan; ++hashes; }
                    ++cursor;
                    while (cursor < len) {
                        if (text[cursor] == '"') {
                            int endHashes = 0;
                            int peek = cursor + 1;
                            while (peek < len && text[peek] == '#' && endHashes < hashes) { ++peek; ++endHashes; }
                            if (endHashes == hashes) { cursor = peek; break; }
                        }
                        ++cursor;
                    }
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
                if (cursor + 1 < len && isIdentStart_(text[cursor + 1]) &&
                    (cursor + 2 >= len || text[cursor + 2] == ',' || text[cursor + 2] == '>' ||
                     text[cursor + 2] == ')' || text[cursor + 2] == ':' ||
                     text[cursor + 2] == '+' || text[cursor + 2] == ' ' ||
                     isIdentPart_(text[cursor + 2]))) {
                    if (cursor + 2 < len && isIdentPart_(text[cursor + 2]) &&
                        !(cursor + 3 < len && text[cursor + 3] == '\'')) {
                        const int start = cursor;
                        ++cursor;
                        while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                        setFormat(start, cursor - start, m_theme.lifetimeFormat);
                        continue;
                    }
                }
                const int start = cursor;
                ++cursor;
                if (cursor < len && text[cursor] == '\\') {
                    cursor += 2;
                } else if (cursor < len) {
                    ++cursor;
                }
                if (cursor < len && text[cursor] == '\'') { ++cursor; }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch))) {
                const int start = cursor;
                cursor = parseNumber_(text, cursor);
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isIdentStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                const SwString word = text.substr(start, cursor - start);

                if (cursor < len && text[cursor] == '!') {
                    setFormat(start, cursor - start + 1, m_theme.macroFormat);
                    ++cursor;
                    continue;
                }

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
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isIdentPart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_';
    }

    static int findBlockCommentEnd_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        int depth = 1;
        while (cursor + 1 < len && depth > 0) {
            if (text[cursor] == '/' && text[cursor + 1] == '*') { ++depth; cursor += 2; continue; }
            if (text[cursor] == '*' && text[cursor + 1] == '/') { --depth; cursor += 2; continue; }
            ++cursor;
        }
        return depth == 0 ? cursor : -1;
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
            if (n == 'o' || n == 'O') {
                cursor += 2;
                while (cursor < len && ((text[cursor] >= '0' && text[cursor] <= '7') || text[cursor] == '_')) { ++cursor; }
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
        while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
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
            "as", "async", "await", "break", "const", "continue", "crate",
            "dyn", "else", "enum", "extern", "false", "fn", "for", "if",
            "impl", "in", "let", "loop", "match", "mod", "move", "mut",
            "pub", "ref", "return", "self", "Self", "static", "struct",
            "super", "trait", "true", "type", "unsafe", "use", "where",
            "while", "yield", "abstract", "become", "box", "do", "final",
            "macro", "override", "priv", "try", "typeof", "unsized",
            "virtual"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isType_(const SwString& word) {
        static const char* const types[] = {
            "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64", "i128",
            "isize", "str", "u8", "u16", "u32", "u64", "u128", "usize",
            "String", "Vec", "Box", "Rc", "Arc", "Cell", "RefCell", "Mutex",
            "RwLock", "Option", "Result", "HashMap", "HashSet", "BTreeMap",
            "BTreeSet", "VecDeque", "LinkedList", "BinaryHeap",
            "Path", "PathBuf", "OsStr", "OsString", "CStr", "CString",
            "Cow", "Pin", "PhantomData", "MaybeUninit"
        };
        for (const char* t : types) {
            if (word == SwString(t)) { return true; }
        }
        return false;
    }

    SwRustSyntaxTheme m_theme;
};
