#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwPythonSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat builtinFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat decoratorFormat;
    SwTextCharFormat selfFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat classNameFormat;
    SwTextCharFormat magicFormat;
};

inline SwTextCharFormat swPythonMakeFormat_(const SwColor& color,
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

inline SwPythonSyntaxTheme swPythonSyntaxThemeVsCodeDark() {
    SwPythonSyntaxTheme theme;
    theme.keywordFormat   = swPythonMakeFormat_(SwColor{197, 134, 192}, Medium);
    theme.builtinFormat   = swPythonMakeFormat_(SwColor{220, 220, 170});
    theme.commentFormat   = swPythonMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat    = swPythonMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat    = swPythonMakeFormat_(SwColor{181, 206, 168});
    theme.decoratorFormat = swPythonMakeFormat_(SwColor{220, 220, 170});
    theme.selfFormat      = swPythonMakeFormat_(SwColor{86, 156, 214}, Normal, true);
    theme.functionFormat  = swPythonMakeFormat_(SwColor{220, 220, 170});
    theme.classNameFormat = swPythonMakeFormat_(SwColor{78, 201, 176});
    theme.magicFormat     = swPythonMakeFormat_(SwColor{86, 156, 214});
    return theme;
}

class SwPythonSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwPythonSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwPythonSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swPythonSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwPythonSyntaxTheme& theme) {
        m_theme = theme;
        rehighlight();
    }

protected:
    void highlightBlock(const SwString& text) override {
        const int len = static_cast<int>(text.size());
        int cursor = 0;

        setCurrentBlockState(0);

        if (previousBlockState() == TripleSingleState || previousBlockState() == TripleDoubleState) {
            const char q = (previousBlockState() == TripleSingleState) ? '\'' : '"';
            const int end = findTripleQuoteEnd_(text, 0, q);
            if (end < 0) {
                setFormat(0, len, m_theme.stringFormat);
                setCurrentBlockState(previousBlockState());
                return;
            }
            setFormat(0, end, m_theme.stringFormat);
            cursor = end;
        }

        bool expectClassName = false;
        bool expectFuncName = false;

        while (cursor < len) {
            const char ch = text[cursor];

            if (ch == '#') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (cursor + 2 < len &&
                ((ch == '\'' && text[cursor + 1] == '\'' && text[cursor + 2] == '\'') ||
                 (ch == '"' && text[cursor + 1] == '"' && text[cursor + 2] == '"'))) {
                const int end = findTripleQuoteEnd_(text, cursor + 3, ch);
                if (end < 0) {
                    setFormat(cursor, len - cursor, m_theme.stringFormat);
                    setCurrentBlockState(ch == '\'' ? TripleSingleState : TripleDoubleState);
                    return;
                }
                setFormat(cursor, end - cursor, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (ch == '\'' || ch == '"') {
                int prefixLen = 0;
                if (cursor > 0) {
                    const char prev = text[cursor - 1];
                    if (prev == 'f' || prev == 'F' || prev == 'r' || prev == 'R' ||
                        prev == 'b' || prev == 'B' || prev == 'u' || prev == 'U') {
                        prefixLen = 1;
                    }
                }
                const int start = cursor - prefixLen;
                const int end = findClosingQuote_(text, cursor, ch);
                setFormat(start, end - start, m_theme.stringFormat);
                cursor = end;
                continue;
            }

            if (ch == '@' && (cursor == 0 || text[cursor - 1] != ')')) {
                const int start = cursor;
                ++cursor;
                while (cursor < len && (isWordChar_(text[cursor]) || text[cursor] == '.')) {
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.decoratorFormat);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) ||
                (ch == '.' && cursor + 1 < len && std::isdigit(static_cast<unsigned char>(text[cursor + 1])))) {
                const int start = cursor;
                cursor = parseNumber_(text, cursor);
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isWordStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isWordChar_(text[cursor])) {
                    ++cursor;
                }
                const SwString word = text.substr(start, cursor - start);

                if (expectClassName) {
                    setFormat(start, cursor - start, m_theme.classNameFormat);
                    expectClassName = false;
                    continue;
                }
                if (expectFuncName) {
                    setFormat(start, cursor - start, m_theme.functionFormat);
                    expectFuncName = false;
                    continue;
                }

                if (word == SwString("self") || word == SwString("cls")) {
                    setFormat(start, cursor - start, m_theme.selfFormat);
                    continue;
                }

                if (isMagic_(word)) {
                    setFormat(start, cursor - start, m_theme.magicFormat);
                    continue;
                }

                if (isKeyword_(word)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                    if (word == SwString("class")) { expectClassName = true; }
                    else if (word == SwString("def")) { expectFuncName = true; }
                    continue;
                }

                if (isBuiltin_(word)) {
                    setFormat(start, cursor - start, m_theme.builtinFormat);
                    continue;
                }

                int peek = cursor;
                while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                if (peek < len && text[peek] == '(') {
                    setFormat(start, cursor - start, m_theme.functionFormat);
                }
                continue;
            }

            ++cursor;
        }
    }

private:
    enum BlockState {
        NormalState = 0,
        TripleSingleState = 1,
        TripleDoubleState = 2
    };

    static bool isWordStart_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalpha(u) != 0 || u == '_';
    }

    static bool isWordChar_(char ch) {
        const unsigned char u = static_cast<unsigned char>(ch);
        return std::isalnum(u) != 0 || u == '_';
    }

    static int findClosingQuote_(const SwString& text, int start, char quote) {
        const int len = static_cast<int>(text.size());
        int cursor = start + 1;
        while (cursor < len) {
            if (text[cursor] == '\\') { cursor += 2; continue; }
            if (text[cursor] == quote) { return cursor + 1; }
            ++cursor;
        }
        return len;
    }

    static int findTripleQuoteEnd_(const SwString& text, int start, char quote) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        while (cursor + 2 < len) {
            if (text[cursor] == '\\') { cursor += 2; continue; }
            if (text[cursor] == quote && text[cursor + 1] == quote && text[cursor + 2] == quote) {
                return cursor + 3;
            }
            ++cursor;
        }
        return -1;
    }

    static int parseNumber_(const SwString& text, int start) {
        const int len = static_cast<int>(text.size());
        int cursor = start;
        if (cursor + 1 < len && text[cursor] == '0') {
            const char next = text[cursor + 1];
            if (next == 'x' || next == 'X') {
                cursor += 2;
                while (cursor < len && (std::isxdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '_')) { ++cursor; }
                return cursor;
            }
            if (next == 'o' || next == 'O') {
                cursor += 2;
                while (cursor < len && ((text[cursor] >= '0' && text[cursor] <= '7') || text[cursor] == '_')) { ++cursor; }
                return cursor;
            }
            if (next == 'b' || next == 'B') {
                cursor += 2;
                while (cursor < len && (text[cursor] == '0' || text[cursor] == '1' || text[cursor] == '_')) { ++cursor; }
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
        if (cursor < len && (text[cursor] == 'j' || text[cursor] == 'J')) { ++cursor; }
        return cursor;
    }

    static bool isMagic_(const SwString& word) {
        return word.size() > 4 && word.startsWith("__") && word.endsWith("__");
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "False", "None", "True", "and", "as", "assert", "async", "await",
            "break", "class", "continue", "def", "del", "elif", "else",
            "except", "finally", "for", "from", "global", "if", "import",
            "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
            "return", "try", "while", "with", "yield"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isBuiltin_(const SwString& word) {
        static const char* const builtins[] = {
            "abs", "all", "any", "ascii", "bin", "bool", "breakpoint",
            "bytearray", "bytes", "callable", "chr", "classmethod",
            "compile", "complex", "delattr", "dict", "dir", "divmod",
            "enumerate", "eval", "exec", "filter", "float", "format",
            "frozenset", "getattr", "globals", "hasattr", "hash", "help",
            "hex", "id", "input", "int", "isinstance", "issubclass",
            "iter", "len", "list", "locals", "map", "max", "memoryview",
            "min", "next", "object", "oct", "open", "ord", "pow",
            "print", "property", "range", "repr", "reversed", "round",
            "set", "setattr", "slice", "sorted", "staticmethod", "str",
            "sum", "super", "tuple", "type", "vars", "zip",
            "NotImplemented", "Ellipsis", "__import__",
            "ArithmeticError", "AssertionError", "AttributeError",
            "BaseException", "BlockingIOError", "BrokenPipeError",
            "BufferError", "BytesWarning", "ChildProcessError",
            "ConnectionAbortedError", "ConnectionError",
            "ConnectionRefusedError", "ConnectionResetError",
            "DeprecationWarning", "EOFError", "EnvironmentError",
            "Exception", "FileExistsError", "FileNotFoundError",
            "FloatingPointError", "FutureWarning", "GeneratorExit",
            "IOError", "ImportError", "ImportWarning",
            "IndentationError", "IndexError", "InterruptedError",
            "IsADirectoryError", "KeyError", "KeyboardInterrupt",
            "LookupError", "MemoryError", "ModuleNotFoundError",
            "NameError", "NotADirectoryError", "NotImplementedError",
            "OSError", "OverflowError", "PendingDeprecationWarning",
            "PermissionError", "ProcessLookupError", "RecursionError",
            "ReferenceError", "ResourceWarning", "RuntimeError",
            "RuntimeWarning", "StopAsyncIteration", "StopIteration",
            "SyntaxError", "SyntaxWarning", "SystemError", "SystemExit",
            "TabError", "TimeoutError", "TypeError", "UnboundLocalError",
            "UnicodeDecodeError", "UnicodeEncodeError", "UnicodeError",
            "UnicodeTranslateError", "UnicodeWarning", "UserWarning",
            "ValueError", "Warning", "ZeroDivisionError"
        };
        for (const char* b : builtins) {
            if (word == SwString(b)) { return true; }
        }
        return false;
    }

    SwPythonSyntaxTheme m_theme;
};
