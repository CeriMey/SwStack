#pragma once

#include "SwSyntaxHighlighter.h"
#include <cctype>

struct SwSqlSyntaxTheme {
    SwTextCharFormat keywordFormat;
    SwTextCharFormat functionFormat;
    SwTextCharFormat typeFormat;
    SwTextCharFormat commentFormat;
    SwTextCharFormat stringFormat;
    SwTextCharFormat numberFormat;
    SwTextCharFormat operatorFormat;
    SwTextCharFormat variableFormat;
};

inline SwTextCharFormat swSqlMakeFormat_(const SwColor& color,
                                          FontWeight weight = Normal,
                                          bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) { format.setFontWeight(weight); }
    if (italic) { format.setFontItalic(true); }
    return format;
}

inline SwSqlSyntaxTheme swSqlSyntaxThemeVsCodeDark() {
    SwSqlSyntaxTheme theme;
    theme.keywordFormat  = swSqlMakeFormat_(SwColor{86, 156, 214}, Medium);
    theme.functionFormat = swSqlMakeFormat_(SwColor{220, 220, 170});
    theme.typeFormat     = swSqlMakeFormat_(SwColor{78, 201, 176});
    theme.commentFormat  = swSqlMakeFormat_(SwColor{106, 153, 85}, Normal, true);
    theme.stringFormat   = swSqlMakeFormat_(SwColor{206, 145, 120});
    theme.numberFormat   = swSqlMakeFormat_(SwColor{181, 206, 168});
    theme.operatorFormat = swSqlMakeFormat_(SwColor{212, 212, 212});
    theme.variableFormat = swSqlMakeFormat_(SwColor{156, 220, 254});
    return theme;
}

class SwSqlSyntaxHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SwSqlSyntaxHighlighter, SwSyntaxHighlighter)

public:
    explicit SwSqlSyntaxHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document)
        , m_theme(swSqlSyntaxThemeVsCodeDark()) {
        setDocument(document);
    }

    void setTheme(const SwSqlSyntaxTheme& theme) {
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

            if (ch == '-' && cursor + 1 < len && text[cursor + 1] == '-') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (ch == '/' && cursor + 1 < len && text[cursor + 1] == '*') {
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

            if (ch == '#') {
                setFormat(cursor, len - cursor, m_theme.commentFormat);
                return;
            }

            if (ch == '\'') {
                const int start = cursor;
                ++cursor;
                while (cursor < len) {
                    if (text[cursor] == '\'' && cursor + 1 < len && text[cursor + 1] == '\'') {
                        cursor += 2;
                        continue;
                    }
                    if (text[cursor] == '\'') { ++cursor; break; }
                    ++cursor;
                }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '"') {
                const int start = cursor;
                ++cursor;
                while (cursor < len && text[cursor] != '"') { ++cursor; }
                if (cursor < len) { ++cursor; }
                setFormat(start, cursor - start, m_theme.stringFormat);
                continue;
            }

            if (ch == '@' || ch == ':') {
                if (cursor + 1 < len && isIdentStart_(text[cursor + 1])) {
                    const int start = cursor;
                    ++cursor;
                    while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                    setFormat(start, cursor - start, m_theme.variableFormat);
                    continue;
                }
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) ||
                (ch == '.' && cursor + 1 < len && std::isdigit(static_cast<unsigned char>(text[cursor + 1])))) {
                const int start = cursor;
                while (cursor < len && (std::isdigit(static_cast<unsigned char>(text[cursor])) || text[cursor] == '.')) {
                    ++cursor;
                }
                if (cursor < len && (text[cursor] == 'e' || text[cursor] == 'E')) {
                    ++cursor;
                    if (cursor < len && (text[cursor] == '+' || text[cursor] == '-')) { ++cursor; }
                    while (cursor < len && std::isdigit(static_cast<unsigned char>(text[cursor]))) { ++cursor; }
                }
                setFormat(start, cursor - start, m_theme.numberFormat);
                continue;
            }

            if (isIdentStart_(ch)) {
                const int start = cursor;
                while (cursor < len && isIdentPart_(text[cursor])) { ++cursor; }
                const SwString word = text.substr(start, cursor - start);
                const SwString upper = word.toUpper();

                int peek = cursor;
                while (peek < len && std::isspace(static_cast<unsigned char>(text[peek]))) { ++peek; }
                const bool isCall = peek < len && text[peek] == '(';

                if (isKeyword_(upper)) {
                    setFormat(start, cursor - start, m_theme.keywordFormat);
                } else if (isType_(upper)) {
                    setFormat(start, cursor - start, m_theme.typeFormat);
                } else if (isFunction_(upper) || isCall) {
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
        while (cursor + 1 < len) {
            if (text[cursor] == '*' && text[cursor + 1] == '/') { return cursor + 2; }
            ++cursor;
        }
        return -1;
    }

    static bool isKeyword_(const SwString& word) {
        static const char* const keywords[] = {
            "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE",
            "SET", "DELETE", "CREATE", "DROP", "ALTER", "TABLE", "INDEX",
            "VIEW", "DATABASE", "SCHEMA", "GRANT", "REVOKE",
            "AND", "OR", "NOT", "IN", "EXISTS", "BETWEEN", "LIKE", "IS",
            "NULL", "TRUE", "FALSE",
            "JOIN", "INNER", "LEFT", "RIGHT", "OUTER", "FULL", "CROSS", "ON",
            "AS", "ORDER", "BY", "ASC", "DESC", "GROUP", "HAVING",
            "LIMIT", "OFFSET", "UNION", "ALL", "DISTINCT", "TOP",
            "CASE", "WHEN", "THEN", "ELSE", "END",
            "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION", "SAVEPOINT",
            "IF", "WHILE", "LOOP", "FOR", "RETURN", "DECLARE", "CURSOR",
            "FETCH", "OPEN", "CLOSE", "DEALLOCATE",
            "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "UNIQUE",
            "CHECK", "CONSTRAINT", "DEFAULT", "AUTO_INCREMENT",
            "CASCADE", "RESTRICT", "NO", "ACTION",
            "ADD", "COLUMN", "RENAME", "TO", "MODIFY", "TRUNCATE",
            "EXPLAIN", "ANALYZE", "DESCRIBE", "SHOW", "USE",
            "EXCEPT", "INTERSECT", "WITH", "RECURSIVE",
            "TRIGGER", "PROCEDURE", "FUNCTION", "REPLACE",
            "TEMPORARY", "TEMP", "NATURAL", "USING"
        };
        for (const char* kw : keywords) {
            if (word == SwString(kw)) { return true; }
        }
        return false;
    }

    static bool isType_(const SwString& word) {
        static const char* const types[] = {
            "INT", "INTEGER", "SMALLINT", "BIGINT", "TINYINT",
            "FLOAT", "DOUBLE", "REAL", "DECIMAL", "NUMERIC",
            "CHAR", "VARCHAR", "TEXT", "NCHAR", "NVARCHAR", "NTEXT",
            "CLOB", "BLOB", "BINARY", "VARBINARY",
            "DATE", "TIME", "DATETIME", "TIMESTAMP", "INTERVAL",
            "BOOLEAN", "BOOL", "BIT",
            "JSON", "JSONB", "XML", "UUID", "SERIAL", "BIGSERIAL",
            "MONEY", "BYTEA", "ARRAY", "ENUM"
        };
        for (const char* t : types) {
            if (word == SwString(t)) { return true; }
        }
        return false;
    }

    static bool isFunction_(const SwString& word) {
        static const char* const funcs[] = {
            "COUNT", "SUM", "AVG", "MIN", "MAX",
            "COALESCE", "NULLIF", "CAST", "CONVERT",
            "UPPER", "LOWER", "TRIM", "LTRIM", "RTRIM",
            "SUBSTRING", "CONCAT", "LENGTH", "REPLACE",
            "ROUND", "FLOOR", "CEIL", "CEILING", "ABS", "MOD",
            "NOW", "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP",
            "EXTRACT", "DATEADD", "DATEDIFF", "DATE_FORMAT",
            "ROW_NUMBER", "RANK", "DENSE_RANK", "LAG", "LEAD",
            "FIRST_VALUE", "LAST_VALUE", "NTH_VALUE", "NTILE",
            "OVER", "PARTITION", "ROWS", "RANGE",
            "STRING_AGG", "GROUP_CONCAT", "ARRAY_AGG",
            "IFNULL", "IIF", "ISNULL"
        };
        for (const char* f : funcs) {
            if (word == SwString(f)) { return true; }
        }
        return false;
    }

    SwSqlSyntaxTheme m_theme;
};
