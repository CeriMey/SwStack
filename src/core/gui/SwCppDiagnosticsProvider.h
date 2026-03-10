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

#include "SwTextDiagnostics.h"

#include <cctype>

class SwCppDiagnosticsProvider : public SwTextDiagnosticsProvider {
    SW_OBJECT(SwCppDiagnosticsProvider, SwTextDiagnosticsProvider)

public:
    explicit SwCppDiagnosticsProvider(SwTextDocument* parent = nullptr)
        : SwTextDiagnosticsProvider(parent) {
        if (parent) {
            setDocument(parent);
        }
    }

protected:
    void analyzeDocument(const SwString& text, SwList<SwTextDiagnostic>& diagnostics) override {
        enum class ParseState {
            Normal,
            LineComment,
            BlockComment,
            StringLiteral,
            CharLiteral
        };

        struct DelimiterEntry {
            char opening{'\0'};
            int position{0};
        };

        auto appendError = [&diagnostics](int start, int length, const SwString& message) {
            SwTextDiagnostic diagnostic;
            diagnostic.range.start = start;
            diagnostic.range.length = std::max(1, length);
            diagnostic.severity = SwTextDiagnosticSeverity::Error;
            diagnostic.message = message;
            diagnostics.append(diagnostic);
        };

        auto matchingOpening = [](char closing) {
            switch (closing) {
                case ')': return '(';
                case ']': return '[';
                case '}': return '{';
                default: return '\0';
            }
        };

        auto matchingClosing = [](char opening) {
            switch (opening) {
                case '(': return ')';
                case '[': return ']';
                case '{': return '}';
                default: return '\0';
            }
        };

        auto appendMissingClosingForEntry = [&appendError, &matchingClosing](const DelimiterEntry& entry) {
            const char expectedClosing = matchingClosing(entry.opening);
            appendError(entry.position,
                        1,
                        SwString("Missing closing delimiter '") + SwString(1, expectedClosing) + "'.");
        };

        ParseState state = ParseState::Normal;
        SwList<DelimiterEntry> delimiters;
        bool escaped = false;
        int literalStart = -1;
        int commentStart = -1;

        for (int i = 0; i < static_cast<int>(text.size()); ++i) {
            const char ch = text[static_cast<size_t>(i)];
            const char next = (i + 1 < static_cast<int>(text.size())) ? text[static_cast<size_t>(i + 1)] : '\0';

            switch (state) {
                case ParseState::Normal:
                    if (ch == '/' && next == '/') {
                        state = ParseState::LineComment;
                        ++i;
                        continue;
                    }
                    if (ch == '/' && next == '*') {
                        state = ParseState::BlockComment;
                        commentStart = i;
                        ++i;
                        continue;
                    }
                    if (ch == '"') {
                        state = ParseState::StringLiteral;
                        literalStart = i;
                        escaped = false;
                        continue;
                    }
                    if (ch == '\'') {
                        state = ParseState::CharLiteral;
                        literalStart = i;
                        escaped = false;
                        continue;
                    }
                    if (ch == '(' || ch == '[' || ch == '{') {
                        DelimiterEntry entry;
                        entry.opening = ch;
                        entry.position = i;
                        delimiters.append(entry);
                        continue;
                    }
                    if (ch == ')' || ch == ']' || ch == '}') {
                        if (delimiters.isEmpty()) {
                            appendError(i,
                                        1,
                                        SwString("Unexpected closing delimiter '") + SwString(1, ch) + "'.");
                            continue;
                        }

                        const DelimiterEntry top = delimiters.last();
                        const char expectedClosing = matchingClosing(top.opening);
                        if (expectedClosing == ch) {
                            delimiters.removeLast();
                            continue;
                        }

                        const char expectedOpening = matchingOpening(ch);
                        int matchingIndex = -1;
                        const int delimiterCount = static_cast<int>(delimiters.size());
                        for (int stackIndex = delimiterCount - 1; stackIndex >= 0; --stackIndex) {
                            if (delimiters[static_cast<size_t>(stackIndex)].opening == expectedOpening) {
                                matchingIndex = stackIndex;
                                break;
                            }
                        }

                        if (matchingIndex >= 0) {
                            for (int stackIndex = delimiterCount - 1; stackIndex > matchingIndex; --stackIndex) {
                                const DelimiterEntry dangling = delimiters[static_cast<size_t>(stackIndex)];
                                appendMissingClosingForEntry(dangling);
                                delimiters.removeAt(static_cast<size_t>(stackIndex));
                            }
                            delimiters.removeAt(static_cast<size_t>(matchingIndex));
                            continue;
                        }

                        appendError(i,
                                    1,
                                    SwString("Unexpected closing delimiter '") +
                                        SwString(1, ch) +
                                        "'; expected '" +
                                        SwString(1, expectedClosing) +
                                        "'.");
                        continue;
                    }
                    break;

                case ParseState::LineComment:
                    if (ch == '\n') {
                        state = ParseState::Normal;
                    }
                    break;

                case ParseState::BlockComment:
                    if (ch == '*' && next == '/') {
                        state = ParseState::Normal;
                        commentStart = -1;
                        ++i;
                    }
                    break;

                case ParseState::StringLiteral:
                    if (!escaped && ch == '"') {
                        state = ParseState::Normal;
                        literalStart = -1;
                        break;
                    }
                    if (ch == '\n') {
                        appendError(literalStart,
                                    std::max(1, i - literalStart),
                                    "Unterminated string literal.");
                        state = ParseState::Normal;
                        literalStart = -1;
                        escaped = false;
                        break;
                    }
                    escaped = (ch == '\\') ? !escaped : false;
                    break;

                case ParseState::CharLiteral:
                    if (!escaped && ch == '\'') {
                        state = ParseState::Normal;
                        literalStart = -1;
                        break;
                    }
                    if (ch == '\n') {
                        appendError(literalStart,
                                    std::max(1, i - literalStart),
                                    "Unterminated character literal.");
                        state = ParseState::Normal;
                        literalStart = -1;
                        escaped = false;
                        break;
                    }
                    escaped = (ch == '\\') ? !escaped : false;
                    break;
            }
        }

        if (state == ParseState::BlockComment && commentStart >= 0) {
            appendError(commentStart,
                        std::max(1, static_cast<int>(text.size()) - commentStart),
                        "Unterminated block comment.");
        } else if (state == ParseState::StringLiteral && literalStart >= 0) {
            appendError(literalStart,
                        std::max(1, static_cast<int>(text.size()) - literalStart),
                        "Unterminated string literal.");
        } else if (state == ParseState::CharLiteral && literalStart >= 0) {
            appendError(literalStart,
                        std::max(1, static_cast<int>(text.size()) - literalStart),
                        "Unterminated character literal.");
        }

        for (int i = 0; i < delimiters.size(); ++i) {
            const DelimiterEntry entry = delimiters[i];
            appendMissingClosingForEntry(entry);
        }

        appendUnresolvedQualifiedTypeDiagnostics_(text, diagnostics);
    }

private:
    struct QualifiedTypeReference {
        SwString fullName;
        SwString prefix;
        SwString leaf;
        int start{0};
        int length{0};
        int leafStart{0};
        int leafLength{0};
    };

    struct LocalTypeTable {
        SwList<SwString> namespaces;
        SwList<SwString> types;
        SwMap<SwString, SwList<SwString>> typeMembers;
    };

    static bool isBuiltinKnownIdentifier_(const SwString& token) {
        return token == SwString("this") ||
               token == SwString("nullptr") ||
               token == SwString("true") ||
               token == SwString("false");
    }

    static bool isResolvableIdentifier_(const LocalTypeTable& table,
                                        const SwMap<SwString, SwString>& variables,
                                        const SwString& token) {
        if (token.isEmpty()) {
            return false;
        }
        if (variables.contains(token) ||
            containsString_(table.types, token) ||
            containsString_(table.namespaces, token) ||
            !resolveKnownTypeName_(table, token).isEmpty()) {
            return true;
        }
        return isBuiltinKnownIdentifier_(token);
    }

    static SwTextCharFormat unknownMemberFormat_() {
        SwTextCharFormat format;
        format.setForeground(SwColor{120, 124, 132});
        return format;
    }

    static bool isIdentifierStart_(char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalpha(uch) != 0 || uch == static_cast<unsigned char>('_');
    }

    static bool isIdentifierPart_(char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) != 0 || uch == static_cast<unsigned char>('_');
    }

    static size_t skipSpaces_(const SwString& text, size_t pos) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
            ++pos;
        }
        return pos;
    }

    static SwString readIdentifierAt_(const SwString& text, size_t& pos) {
        if (pos >= text.size() || !isIdentifierStart_(text[pos])) {
            return SwString();
        }

        const size_t start = pos;
        ++pos;
        while (pos < text.size() && isIdentifierPart_(text[pos])) {
            ++pos;
        }
        return text.substr(start, pos - start);
    }

    static SwString readIdentifier_(const SwString& text, size_t& pos) {
        pos = skipSpaces_(text, pos);
        return readIdentifierAt_(text, pos);
    }

    static bool isKeywordBoundary_(const SwString& text, size_t pos, size_t length) {
        if (pos > 0 && isIdentifierPart_(text[pos - 1])) {
            return false;
        }

        const size_t end = pos + length;
        return end >= text.size() || !isIdentifierPart_(text[end]);
    }

    static bool startsWithKeywordAt_(const SwString& text, size_t pos, const char* keyword) {
        if (!keyword) {
            return false;
        }

        const SwString needle(keyword);
        if (pos + needle.size() > text.size()) {
            return false;
        }
        if (text.substr(pos, needle.size()) != needle) {
            return false;
        }
        return isKeywordBoundary_(text, pos, needle.size());
    }

    static size_t findMatchingBrace_(const SwString& text, size_t openBrace) {
        if (openBrace >= text.size() || text[openBrace] != '{') {
            return static_cast<size_t>(-1);
        }

        int depth = 1;
        for (size_t i = openBrace + 1; i < text.size(); ++i) {
            if (text[i] == '{') {
                ++depth;
            } else if (text[i] == '}') {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return static_cast<size_t>(-1);
    }

    static SwString sanitizeCodeForSemanticParsing_(const SwString& text) {
        SwString sanitized = text;

        enum class ParseState {
            Normal,
            LineComment,
            BlockComment,
            StringLiteral,
            CharLiteral
        };

        ParseState state = ParseState::Normal;
        bool escaped = false;

        for (size_t i = 0; i < sanitized.size(); ++i) {
            const char ch = sanitized[i];
            const char next = (i + 1 < sanitized.size()) ? sanitized[i + 1] : '\0';

            switch (state) {
                case ParseState::Normal:
                    if (ch == '/' && next == '/') {
                        sanitized[i] = ' ';
                        sanitized[i + 1] = ' ';
                        ++i;
                        state = ParseState::LineComment;
                    } else if (ch == '/' && next == '*') {
                        sanitized[i] = ' ';
                        sanitized[i + 1] = ' ';
                        ++i;
                        state = ParseState::BlockComment;
                    } else if (ch == '"') {
                        sanitized[i] = ' ';
                        state = ParseState::StringLiteral;
                        escaped = false;
                    } else if (ch == '\'') {
                        sanitized[i] = ' ';
                        state = ParseState::CharLiteral;
                        escaped = false;
                    }
                    break;

                case ParseState::LineComment:
                    if (ch != '\n') {
                        sanitized[i] = ' ';
                    } else {
                        state = ParseState::Normal;
                    }
                    break;

                case ParseState::BlockComment:
                    if (ch == '*' && next == '/') {
                        sanitized[i] = ' ';
                        sanitized[i + 1] = ' ';
                        ++i;
                        state = ParseState::Normal;
                    } else if (ch != '\n') {
                        sanitized[i] = ' ';
                    }
                    break;

                case ParseState::StringLiteral:
                    if (ch != '\n') {
                        sanitized[i] = ' ';
                    }
                    if (!escaped && ch == '"') {
                        state = ParseState::Normal;
                    }
                    escaped = (!escaped && ch == '\\');
                    break;

                case ParseState::CharLiteral:
                    if (ch != '\n') {
                        sanitized[i] = ' ';
                    }
                    if (!escaped && ch == '\'') {
                        state = ParseState::Normal;
                    }
                    escaped = (!escaped && ch == '\\');
                    break;
            }
        }

        return sanitized;
    }

    static void appendUniqueString_(SwList<SwString>& values, const SwString& value) {
        if (value.isEmpty()) {
            return;
        }
        for (int i = 0; i < values.size(); ++i) {
            if (values[i] == value) {
                return;
            }
        }
        values.append(value);
    }

    static bool containsString_(const SwList<SwString>& values, const SwString& value) {
        for (int i = 0; i < values.size(); ++i) {
            if (values[i] == value) {
                return true;
            }
        }
        return false;
    }

    static SwString collapseWhitespace_(const SwString& text) {
        SwString collapsed;
        collapsed.reserve(text.size());

        bool pendingSpace = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if (std::isspace(ch) != 0) {
                pendingSpace = !collapsed.isEmpty();
                continue;
            }
            if (pendingSpace) {
                collapsed.append(' ');
                pendingSpace = false;
            }
            collapsed.append(text[i]);
        }

        return collapsed.trimmed();
    }

    static SwString extractIdentifierBefore_(const SwString& text, size_t pos) {
        size_t end = std::min(pos, text.size());
        while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }
        if (end == 0 || !isIdentifierPart_(text[end - 1])) {
            return SwString();
        }

        size_t start = end;
        while (start > 0 && isIdentifierPart_(text[start - 1])) {
            --start;
        }
        if (start > 0 && text[start - 1] == '~') {
            --start;
        }

        return text.substr(start, end - start);
    }

    static SwList<SwString> splitTopLevelComma_(const SwString& text) {
        SwList<SwString> parts;
        size_t segmentStart = 0;
        int angleDepth = 0;
        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if (ch == '<') {
                ++angleDepth;
            } else if (ch == '>' && angleDepth > 0) {
                --angleDepth;
            } else if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')' && parenDepth > 0) {
                --parenDepth;
            } else if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']' && bracketDepth > 0) {
                --bracketDepth;
            } else if (ch == '{') {
                ++braceDepth;
            } else if (ch == '}' && braceDepth > 0) {
                --braceDepth;
            } else if (ch == ',' &&
                       angleDepth == 0 &&
                       parenDepth == 0 &&
                       bracketDepth == 0 &&
                       braceDepth == 0) {
                parts.append(text.substr(segmentStart, i - segmentStart));
                segmentStart = i + 1;
            }
        }

        if (segmentStart <= text.size()) {
            parts.append(text.substr(segmentStart));
        }
        return parts;
    }

    static SwString joinQualifiedName_(const SwList<SwString>& scope, const SwString& leaf) {
        if (scope.isEmpty()) {
            return leaf;
        }

        SwString qualified = scope[0];
        for (int i = 1; i < scope.size(); ++i) {
            qualified += "::";
            qualified += scope[i];
        }

        if (!leaf.isEmpty()) {
            qualified += "::";
            qualified += leaf;
        }
        return qualified;
    }

    static bool isTypeDeclarationHead_(const SwString& text, size_t pos) {
        return startsWithKeywordAt_(text, pos, "class") ||
               startsWithKeywordAt_(text, pos, "struct") ||
               startsWithKeywordAt_(text, pos, "enum");
    }

    static SwString leafNameOfQualified_(const SwString& qualifiedName) {
        const size_t separator = qualifiedName.lastIndexOf(SwString("::"));
        if (separator == static_cast<size_t>(-1)) {
            return qualifiedName;
        }
        return qualifiedName.substr(separator + 2);
    }

    static void appendTypeMember_(LocalTypeTable& table, const SwString& typeName, const SwString& memberName) {
        if (typeName.isEmpty() || memberName.isEmpty()) {
            return;
        }

        SwList<SwString> members = table.typeMembers.value(typeName);
        appendUniqueString_(members, memberName);
        table.typeMembers.insert(typeName, members);
    }

    static SwString extractDeclaredNameFromDeclarator_(const SwString& declarator) {
        SwString part = collapseWhitespace_(declarator).trimmed();
        while (!part.isEmpty() && (part[0] == '*' || part[0] == '&')) {
            part.remove(0, 1);
            part = part.trimmed();
        }

        const size_t equalPos = part.firstIndexOf('=');
        if (equalPos != static_cast<size_t>(-1)) {
            part = part.substr(0, equalPos).trimmed();
        }

        const size_t bracePos = part.firstIndexOf('{');
        if (bracePos != static_cast<size_t>(-1)) {
            part = part.substr(0, bracePos).trimmed();
        }

        const size_t bracketPos = part.firstIndexOf('[');
        if (bracketPos != static_cast<size_t>(-1)) {
            part = part.substr(0, bracketPos).trimmed();
        }

        const size_t parenPos = part.firstIndexOf('(');
        if (parenPos != static_cast<size_t>(-1)) {
            return extractIdentifierBefore_(part, parenPos);
        }

        return extractIdentifierBefore_(part, part.size());
    }

    static void collectTypeMembersFromBody_(const SwString& sanitizedBody,
                                            const SwString& qualifiedTypeName,
                                            LocalTypeTable& table) {
        const SwString typeLeaf = leafNameOfQualified_(qualifiedTypeName);
        size_t statementStart = 0;
        size_t i = 0;

        while (i < sanitizedBody.size()) {
            const char ch = sanitizedBody[i];

            if (startsWithKeywordAt_(sanitizedBody, i, "public") ||
                startsWithKeywordAt_(sanitizedBody, i, "private") ||
                startsWithKeywordAt_(sanitizedBody, i, "protected")) {
                size_t pos = i;
                const SwString access = readIdentifier_(sanitizedBody, pos);
                pos = skipSpaces_(sanitizedBody, pos);
                if (!access.isEmpty() && pos < sanitizedBody.size() && sanitizedBody[pos] == ':') {
                    i = pos + 1;
                    statementStart = i;
                    continue;
                }
            }

            if (isTypeDeclarationHead_(sanitizedBody, i) || startsWithKeywordAt_(sanitizedBody, i, "namespace")) {
                size_t pos = i;
                while (pos < sanitizedBody.size() && sanitizedBody[pos] != '{' && sanitizedBody[pos] != ';') {
                    ++pos;
                }
                if (pos < sanitizedBody.size() && sanitizedBody[pos] == '{') {
                    const size_t closeBrace = findMatchingBrace_(sanitizedBody, pos);
                    if (closeBrace == static_cast<size_t>(-1)) {
                        break;
                    }
                    i = closeBrace + 1;
                    while (i < sanitizedBody.size() && std::isspace(static_cast<unsigned char>(sanitizedBody[i])) != 0) {
                        ++i;
                    }
                    if (i < sanitizedBody.size() && sanitizedBody[i] == ';') {
                        ++i;
                    }
                    statementStart = i;
                    continue;
                }

                i = (pos < sanitizedBody.size()) ? (pos + 1) : sanitizedBody.size();
                statementStart = i;
                continue;
            }

            if (ch == '{') {
                const SwString header = collapseWhitespace_(sanitizedBody.substr(statementStart, i - statementStart));
                if (!header.isEmpty()) {
                    const size_t parenPos = header.firstIndexOf('(');
                    if (parenPos != static_cast<size_t>(-1)) {
                        const SwString memberName = extractIdentifierBefore_(header, parenPos);
                        if (!memberName.isEmpty() &&
                            memberName != typeLeaf &&
                            memberName != (SwString("~") + typeLeaf) &&
                            memberName != SwString("operator")) {
                            appendTypeMember_(table, qualifiedTypeName, memberName);
                        }
                    }
                }

                const size_t closeBrace = findMatchingBrace_(sanitizedBody, i);
                if (closeBrace == static_cast<size_t>(-1)) {
                    break;
                }

                i = closeBrace + 1;
                while (i < sanitizedBody.size() && std::isspace(static_cast<unsigned char>(sanitizedBody[i])) != 0) {
                    ++i;
                }
                if (i < sanitizedBody.size() && sanitizedBody[i] == ';') {
                    ++i;
                }
                statementStart = i;
                continue;
            }

            if (ch == ';') {
                const SwString statement = collapseWhitespace_(sanitizedBody.substr(statementStart, i - statementStart));
                if (!statement.isEmpty()) {
                    const size_t parenPos = statement.firstIndexOf('(');
                    if (parenPos != static_cast<size_t>(-1)) {
                        const SwString memberName = extractIdentifierBefore_(statement, parenPos);
                        if (!memberName.isEmpty() &&
                            memberName != typeLeaf &&
                            memberName != (SwString("~") + typeLeaf) &&
                            memberName != SwString("operator")) {
                            appendTypeMember_(table, qualifiedTypeName, memberName);
                        }
                    } else {
                        const SwList<SwString> declarators = splitTopLevelComma_(statement);
                        for (int partIndex = 0; partIndex < declarators.size(); ++partIndex) {
                            const SwString memberName = extractDeclaredNameFromDeclarator_(declarators[partIndex]);
                            if (!memberName.isEmpty() &&
                                memberName != typeLeaf &&
                                memberName != (SwString("~") + typeLeaf)) {
                                appendTypeMember_(table, qualifiedTypeName, memberName);
                            }
                        }
                    }
                }

                statementStart = i + 1;
            }

            ++i;
        }
    }

    static void collectDeclaredTypes_(const SwString& sanitized,
                                      size_t begin,
                                      size_t end,
                                      const SwList<SwString>& scope,
                                      LocalTypeTable& table) {
        size_t i = begin;

        while (i < end) {
            if (startsWithKeywordAt_(sanitized, i, "namespace")) {
                size_t pos = i + SwString("namespace").size();
                const SwString namespaceName = readIdentifier_(sanitized, pos);
                const bool hasName = !namespaceName.isEmpty();
                if (hasName) {
                    appendUniqueString_(table.namespaces, joinQualifiedName_(scope, namespaceName));
                }

                while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                    ++pos;
                }
                if (pos < end && sanitized[pos] == '{') {
                    const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                    if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                        SwList<SwString> nestedScope = scope;
                        if (hasName) {
                            nestedScope.append(namespaceName);
                        }
                        collectDeclaredTypes_(sanitized, pos + 1, closeBrace, nestedScope, table);
                        i = closeBrace + 1;
                        continue;
                    }
                }

                i = (pos < end) ? (pos + 1) : end;
                continue;
            }

            if (isTypeDeclarationHead_(sanitized, i)) {
                size_t pos = i;
                if (startsWithKeywordAt_(sanitized, i, "enum")) {
                    pos += SwString("enum").size();
                    pos = skipSpaces_(sanitized, pos);
                    if (startsWithKeywordAt_(sanitized, pos, "class")) {
                        pos += SwString("class").size();
                    }
                } else if (startsWithKeywordAt_(sanitized, i, "class")) {
                    pos += SwString("class").size();
                } else {
                    pos += SwString("struct").size();
                }

                const SwString typeName = readIdentifier_(sanitized, pos);
                const bool hasName = !typeName.isEmpty();
                if (hasName) {
                    appendUniqueString_(table.types, joinQualifiedName_(scope, typeName));
                }

                while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                    ++pos;
                }
                if (pos < end && sanitized[pos] == '{') {
                    const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                    if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                        const SwString qualifiedTypeName = hasName ? joinQualifiedName_(scope, typeName) : SwString();
                        if (!qualifiedTypeName.isEmpty()) {
                            collectTypeMembersFromBody_(sanitized.substr(pos + 1, closeBrace - pos - 1),
                                                        qualifiedTypeName,
                                                        table);
                        }
                        SwList<SwString> nestedScope = scope;
                        if (hasName) {
                            nestedScope.append(typeName);
                        }
                        collectDeclaredTypes_(sanitized, pos + 1, closeBrace, nestedScope, table);
                        i = closeBrace + 1;
                        continue;
                    }
                }

                i = (pos < end) ? (pos + 1) : end;
                continue;
            }

            if (sanitized[i] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, i);
                if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                    break;
                }
                collectDeclaredTypes_(sanitized, i + 1, closeBrace, scope, table);
                i = closeBrace + 1;
                continue;
            }

            ++i;
        }
    }

    static size_t skipTemplateArguments_(const SwString& text, size_t pos) {
        if (pos >= text.size() || text[pos] != '<') {
            return pos;
        }

        int depth = 0;
        for (size_t i = pos; i < text.size(); ++i) {
            if (text[i] == '<') {
                ++depth;
            } else if (text[i] == '>' && depth > 0) {
                --depth;
                if (depth == 0) {
                    return i + 1;
                }
            }
        }

        return pos;
    }

    static bool isPostTypeQualifier_(const SwString& token) {
        return token == SwString("const") ||
               token == SwString("volatile") ||
               token == SwString("constexpr");
    }

    static bool isIgnoredStatementHeadForTypeDiagnostics_(const SwString& token) {
        return token == SwString("namespace") ||
               token == SwString("class") ||
               token == SwString("struct") ||
               token == SwString("enum") ||
               token == SwString("using") ||
               token == SwString("typedef") ||
               token == SwString("return") ||
               token == SwString("if") ||
               token == SwString("for") ||
               token == SwString("while") ||
               token == SwString("switch") ||
               token == SwString("catch") ||
               token == SwString("static_assert");
    }

    static bool readQualifiedTypeReference_(const SwString& text, size_t& pos, QualifiedTypeReference& reference) {
        size_t cursor = skipSpaces_(text, pos);
        if (cursor >= text.size() || !isIdentifierStart_(text[cursor])) {
            return false;
        }

        const size_t start = cursor;
        SwString segment = readIdentifierAt_(text, cursor);
        if (segment.isEmpty()) {
            return false;
        }

        int segmentCount = 1;
        size_t leafStart = start;
        SwString leaf = segment;

        while (cursor + 1 < text.size() && text[cursor] == ':' && text[cursor + 1] == ':') {
            cursor += 2;
            if (cursor >= text.size() || !isIdentifierStart_(text[cursor])) {
                break;
            }

            leafStart = cursor;
            leaf = readIdentifierAt_(text, cursor);
            ++segmentCount;
        }

        if (segmentCount < 2) {
            return false;
        }

        const size_t end = cursor;
        reference.fullName = text.substr(start, end - start);
        reference.leaf = leaf;
        reference.start = static_cast<int>(start);
        reference.length = static_cast<int>(end - start);
        reference.leafStart = static_cast<int>(leafStart);
        reference.leafLength = static_cast<int>(leaf.size());

        const int prefixLength = std::max(0, reference.length - reference.leafLength - 2);
        reference.prefix = text.substr(start, static_cast<size_t>(prefixLength));

        pos = end;
        return true;
    }

    static bool isPotentialDeclarationForType_(const SwString& statement, const QualifiedTypeReference& reference) {
        size_t cursor = static_cast<size_t>(reference.start + reference.length);
        cursor = skipSpaces_(statement, cursor);

        const size_t afterTemplate = skipTemplateArguments_(statement, cursor);
        if (afterTemplate != cursor) {
            cursor = skipSpaces_(statement, afterTemplate);
        }

        while (cursor < statement.size() && (statement[cursor] == '*' || statement[cursor] == '&')) {
            ++cursor;
            cursor = skipSpaces_(statement, cursor);
        }

        while (cursor < statement.size()) {
            const size_t tokenStart = cursor;
            const SwString token = readIdentifierAt_(statement, cursor);
            if (token.isEmpty()) {
                break;
            }
            if (!isPostTypeQualifier_(token)) {
                cursor = tokenStart;
                break;
            }
            cursor = skipSpaces_(statement, cursor);
            while (cursor < statement.size() && (statement[cursor] == '*' || statement[cursor] == '&')) {
                ++cursor;
                cursor = skipSpaces_(statement, cursor);
            }
        }

        const SwString declarator = readIdentifierAt_(statement, cursor);
        if (declarator.isEmpty()) {
            return false;
        }

        cursor = skipSpaces_(statement, cursor);
        if (cursor >= statement.size()) {
            return true;
        }

        const char follower = statement[cursor];
        return follower == ';' ||
               follower == '=' ||
               follower == '(' ||
               follower == '[' ||
               follower == '{' ||
               follower == ',' ||
               follower == ')' ||
               follower == ':';
    }

    static SwList<SwString> candidateQualifiedTypesByLeaf_(const LocalTypeTable& table, const SwString& leaf) {
        SwList<SwString> matches;
        for (int i = 0; i < table.types.size(); ++i) {
            const SwString& typeName = table.types[i];
            if (typeName == leaf || typeName.endsWith(SwString("::") + leaf)) {
                appendUniqueString_(matches, typeName);
            }
        }
        return matches;
    }

    static SwString resolveKnownTypeName_(const LocalTypeTable& table, const SwString& candidate) {
        if (candidate.isEmpty()) {
            return SwString();
        }
        if (containsString_(table.types, candidate)) {
            return candidate;
        }

        const SwList<SwString> matches = candidateQualifiedTypesByLeaf_(table, candidate);
        return (matches.size() == 1) ? matches[0] : SwString();
    }

    static bool isLeadingDeclarationQualifier_(const SwString& token) {
        return token == SwString("const") ||
               token == SwString("constexpr") ||
               token == SwString("static") ||
               token == SwString("inline") ||
               token == SwString("volatile") ||
               token == SwString("mutable") ||
               token == SwString("typename") ||
               token == SwString("extern") ||
               token == SwString("friend");
    }

    static SwString resolveTypeNameAt_(const SwString& statement,
                                       const LocalTypeTable& table,
                                       size_t& typeStart,
                                       size_t& typeEnd) {
        size_t pos = skipSpaces_(statement, 0);
        while (pos < statement.size()) {
            const size_t tokenStart = pos;
            size_t tokenCursor = pos;
            const SwString token = readIdentifierAt_(statement, tokenCursor);
            if (token.isEmpty() || !isLeadingDeclarationQualifier_(token)) {
                pos = tokenStart;
                break;
            }
            pos = skipSpaces_(statement, tokenCursor);
        }

        typeStart = pos;

        QualifiedTypeReference reference;
        size_t qualifiedEnd = pos;
        if (readQualifiedTypeReference_(statement, qualifiedEnd, reference)) {
            const SwString resolvedQualified = resolveKnownTypeName_(table, reference.fullName);
            if (!resolvedQualified.isEmpty()) {
                typeEnd = qualifiedEnd;
                return resolvedQualified;
            }
        }

        size_t identifierEnd = pos;
        const SwString identifier = readIdentifierAt_(statement, identifierEnd);
        if (!identifier.isEmpty()) {
            const SwString resolvedUnqualified = resolveKnownTypeName_(table, identifier);
            if (!resolvedUnqualified.isEmpty()) {
                typeEnd = identifierEnd;
                return resolvedUnqualified;
            }
        }

        typeStart = static_cast<size_t>(-1);
        typeEnd = static_cast<size_t>(-1);
        return SwString();
    }

    static void registerDeclaredVariablesInStatement_(const SwString& statement,
                                                      const LocalTypeTable& table,
                                                      SwMap<SwString, SwString>& variables) {
        size_t typeStart = static_cast<size_t>(-1);
        size_t typeEnd = static_cast<size_t>(-1);
        const SwString typeName = resolveTypeNameAt_(statement, table, typeStart, typeEnd);
        if (typeName.isEmpty() || typeEnd == static_cast<size_t>(-1) || typeEnd >= statement.size()) {
            return;
        }

        const SwString declaratorsText = statement.substr(typeEnd);
        const SwList<SwString> declarators = splitTopLevelComma_(declaratorsText);
        for (int i = 0; i < declarators.size(); ++i) {
            const SwString variableName = extractDeclaredNameFromDeclarator_(declarators[i]);
            if (!variableName.isEmpty()) {
                variables.insert(variableName, typeName);
            }
        }
    }

    static bool isTypeDeclarationHeaderText_(const SwString& header) {
        return header.startsWith("class ") ||
               header.startsWith("struct ") ||
               header.startsWith("enum ") ||
               header.startsWith("enum class ");
    }

    static void collectDeclaredVariables_(const SwString& sanitized,
                                          size_t begin,
                                          size_t end,
                                          const LocalTypeTable& table,
                                          SwMap<SwString, SwString>& variables) {
        size_t statementStart = begin;
        size_t i = begin;

        while (i < end) {
            const char ch = sanitized[i];
            if (ch == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, i);
                if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                    break;
                }

                const SwString header = collapseWhitespace_(sanitized.substr(statementStart, i - statementStart));
                if (header.startsWith("namespace ") || (!header.isEmpty() && !isTypeDeclarationHeaderText_(header))) {
                    collectDeclaredVariables_(sanitized, i + 1, closeBrace, table, variables);
                }

                i = closeBrace + 1;
                statementStart = i;
                continue;
            }

            if (ch == ';') {
                if (i > statementStart) {
                    registerDeclaredVariablesInStatement_(sanitized.substr(statementStart, i - statementStart),
                                                          table,
                                                          variables);
                }
                statementStart = i + 1;
            }

            ++i;
        }
    }

    static void appendSemanticError_(SwList<SwTextDiagnostic>& diagnostics,
                                     int start,
                                     int length,
                                     const SwString& message,
                                     const SwTextCharFormat& format = SwTextCharFormat()) {
        SwTextDiagnostic diagnostic;
        diagnostic.range.start = start;
        diagnostic.range.length = std::max(1, length);
        diagnostic.severity = SwTextDiagnosticSeverity::Error;
        diagnostic.message = message;
        diagnostic.format = format;
        diagnostics.append(diagnostic);
    }

    static void analyzeQualifiedTypesInStatement_(const SwString& statement,
                                                  int statementOffset,
                                                  const LocalTypeTable& table,
                                                  SwList<SwTextDiagnostic>& diagnostics) {
        size_t firstTokenPos = 0;
        const SwString firstToken = readIdentifier_(statement, firstTokenPos);
        if (isIgnoredStatementHeadForTypeDiagnostics_(firstToken)) {
            return;
        }

        size_t pos = 0;
        while (pos < statement.size()) {
            QualifiedTypeReference reference;
            size_t candidatePos = pos;
            if (!readQualifiedTypeReference_(statement, candidatePos, reference)) {
                ++pos;
                continue;
            }

            if (!isPotentialDeclarationForType_(statement, reference)) {
                pos = candidatePos;
                continue;
            }

            if (containsString_(table.types, reference.fullName)) {
                pos = candidatePos;
                continue;
            }

            const bool prefixKnown = containsString_(table.namespaces, reference.prefix) ||
                                     containsString_(table.types, reference.prefix);
            const SwList<SwString> alternatives = candidateQualifiedTypesByLeaf_(table, reference.leaf);
            if (!prefixKnown && alternatives.isEmpty()) {
                pos = candidatePos;
                continue;
            }

            int errorStart = statementOffset + reference.start;
            int errorLength = reference.length;
            if (prefixKnown) {
                errorStart = statementOffset + reference.leafStart;
                errorLength = reference.leafLength;
            }

            SwString message = SwString("Unknown type '") + reference.fullName + "'.";
            if (!alternatives.isEmpty()) {
                message += SwString(" Did you mean '") + alternatives[0] + "'?";
            } else if (prefixKnown) {
                message += SwString(" No matching declaration found in '") + reference.prefix + "'.";
            }

            appendSemanticError_(diagnostics, errorStart, errorLength, message);
            pos = candidatePos;
        }
    }

    static void appendUnknownMemberDiagnostics_(const SwString& sanitized,
                                                const LocalTypeTable& table,
                                                const SwMap<SwString, SwString>& variables,
                                                SwList<SwTextDiagnostic>& diagnostics) {
        for (size_t i = 0; i < sanitized.size(); ++i) {
            const bool isArrow = (sanitized[i] == '-' && i + 1 < sanitized.size() && sanitized[i + 1] == '>');
            const bool isDot = sanitized[i] == '.';
            if (!isArrow && !isDot) {
                continue;
            }

            const SwString baseName = extractIdentifierBefore_(sanitized, i);
            if (baseName.isEmpty() || !variables.contains(baseName)) {
                if (isArrow) {
                    ++i;
                }
                continue;
            }

            size_t memberPos = i + (isArrow ? 2 : 1);
            memberPos = skipSpaces_(sanitized, memberPos);
            const size_t memberStart = memberPos;
            const SwString memberName = readIdentifierAt_(sanitized, memberPos);
            if (memberName.isEmpty()) {
                if (isArrow) {
                    ++i;
                }
                continue;
            }

            const SwString resolvedType = resolveKnownTypeName_(table, variables.value(baseName));
            if (resolvedType.isEmpty()) {
                if (isArrow) {
                    ++i;
                }
                continue;
            }

            const SwList<SwString> members = table.typeMembers.value(resolvedType);
            if (!members.isEmpty() && !containsString_(members, memberName)) {
                appendSemanticError_(diagnostics,
                                     static_cast<int>(memberStart),
                                     static_cast<int>(memberName.size()),
                                     SwString("Unknown member '") + memberName + "' on type '" + resolvedType + "'.",
                                     unknownMemberFormat_());
            }

            if (isArrow) {
                ++i;
            }
        }
    }

    static void appendUnknownObjectDiagnostics_(const SwString& sanitized,
                                                const LocalTypeTable& table,
                                                const SwMap<SwString, SwString>& variables,
                                                SwList<SwTextDiagnostic>& diagnostics) {
        for (size_t i = 0; i < sanitized.size();) {
            const size_t identifierStart = i;
            SwString identifier = readIdentifierAt_(sanitized, i);
            if (identifier.isEmpty()) {
                ++i;
                continue;
            }

            const size_t nextPos = skipSpaces_(sanitized, i);
            const bool hasArrow = nextPos + 1 < sanitized.size() &&
                                  sanitized[nextPos] == '-' &&
                                  sanitized[nextPos + 1] == '>';
            const bool hasDot = nextPos < sanitized.size() && sanitized[nextPos] == '.';
            if (!hasArrow && !hasDot) {
                continue;
            }

            const size_t prevPos = skipSpacesBackward_(sanitized, identifierStart);
            if (prevPos != static_cast<size_t>(-1)) {
                const char prev = sanitized[prevPos];
                if (prev == '.' || prev == '>' || prev == ':') {
                    continue;
                }
            }

            if (isResolvableIdentifier_(table, variables, identifier)) {
                continue;
            }

            appendSemanticError_(diagnostics,
                                 static_cast<int>(identifierStart),
                                 static_cast<int>(identifier.size()),
                                 SwString("Unknown identifier '") + identifier + "'.");
        }
    }

    static void collectStatementTypeDiagnostics_(const SwString& text,
                                                 const SwString& sanitized,
                                                 size_t begin,
                                                 size_t end,
                                                 const LocalTypeTable& table,
                                                 SwList<SwTextDiagnostic>& diagnostics) {
        size_t statementStart = begin;
        size_t i = begin;

        while (i < end) {
            const char ch = sanitized[i];

            if (ch == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, i);
                if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                    break;
                }

                collectStatementTypeDiagnostics_(text, sanitized, i + 1, closeBrace, table, diagnostics);
                i = closeBrace + 1;
                statementStart = i;
                continue;
            }

            if (ch == ';') {
                if (i > statementStart) {
                    analyzeQualifiedTypesInStatement_(text.substr(statementStart, i - statementStart),
                                                      static_cast<int>(statementStart),
                                                      table,
                                                      diagnostics);
                }
                statementStart = i + 1;
            }

            ++i;
        }
    }

    static void appendUnresolvedQualifiedTypeDiagnostics_(const SwString& text,
                                                          SwList<SwTextDiagnostic>& diagnostics) {
        const SwString sanitized = sanitizeCodeForSemanticParsing_(text);

        LocalTypeTable table;
        table.namespaces.reserve(8);
        table.types.reserve(16);

        SwList<SwString> rootScope;
        collectDeclaredTypes_(sanitized, 0, sanitized.size(), rootScope, table);
        if (table.types.isEmpty()) {
            return;
        }

        SwMap<SwString, SwString> variables;
        collectDeclaredVariables_(sanitized, 0, sanitized.size(), table, variables);
        collectStatementTypeDiagnostics_(text, sanitized, 0, sanitized.size(), table, diagnostics);
        appendUnknownObjectDiagnostics_(sanitized, table, variables, diagnostics);
        appendUnknownMemberDiagnostics_(sanitized, table, variables, diagnostics);
    }

    static size_t skipSpacesBackward_(const SwString& text, size_t pos) {
        if (pos == 0 || text.isEmpty()) {
            return static_cast<size_t>(-1);
        }

        size_t cursor = std::min(pos, text.size());
        while (cursor > 0) {
            --cursor;
            if (std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
                return cursor;
            }
        }

        return static_cast<size_t>(-1);
    }
};
