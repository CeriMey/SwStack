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

#include "SwCoreApplication.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwString.h"
#include "SwThreadPool.h"
#include "SwTimer.h"
#include "SwVector.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <memory>

struct SwCppCompletionItem {
    SwString displayText;
    SwString insertText;
    SwString toolTip;
};

class SwCppCompletionIndex : public SwObject {
    SW_OBJECT(SwCppCompletionIndex, SwObject)

public:
    struct Stats {
        size_t textLength{0};
        int lineCount{0};
        int scopeCount{0};
        int typeCount{0};
        int symbolCount{0};
        long long lastBuildMs{0};
        bool ready{false};
    };

    explicit SwCppCompletionIndex(SwObject* parent = nullptr);

    void setThreadPool(SwThreadPool* pool);
    SwThreadPool* threadPool() const;

    void setTextProvider(const std::function<SwString()>& provider);
    void setAsyncEnabled(bool on);
    bool asyncEnabled() const;

    void setDebounceInterval(int ms);
    int debounceInterval() const;

    void scheduleRebuild();
    void rebuildNow();
    void rebuildNow(const SwString& text);

    bool isReady() const;
    bool hasPendingBuild() const;
    Stats stats() const;

    SwList<SwCppCompletionItem> completionItems(size_t cursorPos,
                                                const SwString& prefix,
                                                int maxItems = 256) const;
    bool shouldPreferArrowAccess(size_t cursorPos) const;

signals:
    DECLARE_SIGNAL_VOID(indexUpdated)

private:
    struct MemberInfo {
        SwString name;
        bool method{false};
    };

    struct TypeInfo {
        SwString qualifiedName;
        SwString leafName;
        SwList<MemberInfo> members;
    };

    struct VariableInfo {
        SwString typeName;
        bool isPointer{false};
    };

    struct ScopeInfo {
        size_t begin{0};
        size_t end{0};
        int parent{-1};
        SwString className;
        SwMap<SwString, VariableInfo> variables;
    };

    struct AccessInfo {
        bool valid{false};
        bool usesArrow{false};
        size_t operatorPos{static_cast<size_t>(-1)};
        SwString baseName;
        SwString memberPrefix;
    };

    struct Snapshot {
        SwString text;
        SwString sanitized;
        SwList<SwString> globalWords;
        SwList<SwString> typeNames;
        SwMap<SwString, TypeInfo> types;
        SwVector<ScopeInfo> scopes;
        Stats stats;
    };

    static std::shared_ptr<Snapshot> buildSnapshot_(const SwString& text);
    void startAsyncRebuild_();
    void applySnapshot_(const std::shared_ptr<Snapshot>& snapshot);

    static bool isIdentifierStart_(char ch);
    static bool isIdentifierPart_(char ch);
    static bool startsWithKeywordAt_(const SwString& text, size_t pos, const char* keyword);
    static size_t skipSpaces_(const SwString& text, size_t pos);
    static SwString readIdentifier_(const SwString& text, size_t& pos);
    static SwString readQualifiedIdentifier_(const SwString& text, size_t& pos);
    static SwString sanitizeCodeForParsing_(const SwString& text);
    static SwString collapseWhitespace_(const SwString& text);
    static SwString extractIdentifierBefore_(const SwString& text, size_t pos);
    static SwList<SwString> splitTopLevelComma_(const SwString& text);
    static size_t findMatchingBrace_(const SwString& text, size_t openBrace);
    static SwString leafNameOfQualified_(const SwString& qualifiedName);
    static bool matchesPrefix_(const SwString& value, const SwString& prefix);
    static void appendUniqueWord_(SwList<SwString>& words, const SwString& word);
    static void appendUniqueString_(SwList<SwString>& words, const SwString& word);
    static void registerType_(Snapshot& snapshot, const SwString& qualifiedTypeName);
    static void appendTypeMember_(Snapshot& snapshot, const SwString& qualifiedTypeName, const MemberInfo& member);
    static SwString resolveKnownTypeName_(const Snapshot& snapshot, const SwString& candidate);
    static SwString resolveTypeNameAt_(const SwString& statement, const Snapshot& snapshot, size_t& typeEnd);
    static void registerDeclaredVariables_(const SwString& statement,
                                          const Snapshot& snapshot,
                                          SwMap<SwString, VariableInfo>& variables);
    static void registerFunctionParameters_(const SwString& header,
                                            const Snapshot& snapshot,
                                            SwMap<SwString, VariableInfo>& variables);
    static void collectDeclaredTypes_(Snapshot& snapshot,
                                      const SwString& sanitized,
                                      size_t begin,
                                      size_t end,
                                      const SwList<SwString>& scope);
    static void collectScopeSymbols_(const SwString& original,
                                     const SwString& sanitized,
                                     size_t begin,
                                     size_t end,
                                     SwList<SwString>& words);
    static void buildScopeIndex_(Snapshot& snapshot,
                                 const SwString& sanitized,
                                 size_t begin,
                                 size_t end,
                                 int scopeIndex);
    static int innermostScope_(const Snapshot& snapshot, size_t cursorPos);
    static int innermostClassScope_(const Snapshot& snapshot, size_t cursorPos);
    static void collectVisibleVariables_(const Snapshot& snapshot,
                                         size_t cursorPos,
                                         SwMap<SwString, VariableInfo>& variables);
    static AccessInfo accessInfoAt_(const SwString& text, size_t cursorPos);

    std::function<SwString()> m_textProvider;
    std::shared_ptr<Snapshot> m_snapshot;
    std::shared_ptr<int> m_lifeToken{std::make_shared<int>(0)};
    SwTimer* m_rebuildTimer{nullptr};
    SwThreadPool* m_threadPool{nullptr};
    int m_rebuildDebounceMs{120};
    int m_requestedBuildId{0};
    bool m_asyncEnabled{true};
    bool m_buildInFlight{false};
    bool m_rebuildQueued{false};
};

inline SwCppCompletionIndex::SwCppCompletionIndex(SwObject* parent)
    : SwObject(parent)
    , m_rebuildTimer(new SwTimer(this)) {
    m_rebuildTimer->setInterval(m_rebuildDebounceMs);
    m_rebuildTimer->setSingleShot(true);
    SwObject::connect(m_rebuildTimer, &SwTimer::timeout, this, [this]() {
        startAsyncRebuild_();
    });
}

inline void SwCppCompletionIndex::setThreadPool(SwThreadPool* pool) {
    m_threadPool = pool ? pool : SwThreadPool::globalInstance();
}

inline SwThreadPool* SwCppCompletionIndex::threadPool() const {
    return m_threadPool ? m_threadPool : SwThreadPool::globalInstance();
}

inline void SwCppCompletionIndex::setTextProvider(const std::function<SwString()>& provider) {
    m_textProvider = provider;
}

inline void SwCppCompletionIndex::setAsyncEnabled(bool on) {
    m_asyncEnabled = on;
}

inline bool SwCppCompletionIndex::asyncEnabled() const {
    return m_asyncEnabled;
}

inline void SwCppCompletionIndex::setDebounceInterval(int ms) {
    m_rebuildDebounceMs = std::max(0, ms);
    if (m_rebuildTimer) {
        m_rebuildTimer->setInterval(m_rebuildDebounceMs);
    }
}

inline int SwCppCompletionIndex::debounceInterval() const {
    return m_rebuildDebounceMs;
}

inline void SwCppCompletionIndex::scheduleRebuild() {
    if (!m_textProvider) {
        return;
    }
    m_rebuildQueued = true;
    if (!m_asyncEnabled || m_rebuildDebounceMs <= 0) {
        startAsyncRebuild_();
        return;
    }
    if (m_rebuildTimer) {
        m_rebuildTimer->start(m_rebuildDebounceMs);
    }
}

inline void SwCppCompletionIndex::rebuildNow() {
    if (!m_textProvider) {
        return;
    }
    if (m_rebuildTimer && m_rebuildTimer->isActive()) {
        m_rebuildTimer->stop();
    }
    applySnapshot_(buildSnapshot_(m_textProvider()));
}

inline void SwCppCompletionIndex::rebuildNow(const SwString& text) {
    if (m_rebuildTimer && m_rebuildTimer->isActive()) {
        m_rebuildTimer->stop();
    }
    applySnapshot_(buildSnapshot_(text));
}

inline bool SwCppCompletionIndex::isReady() const {
    return static_cast<bool>(m_snapshot) && m_snapshot->stats.ready;
}

inline bool SwCppCompletionIndex::hasPendingBuild() const {
    return m_buildInFlight || m_rebuildQueued || (m_rebuildTimer && m_rebuildTimer->isActive());
}

inline SwCppCompletionIndex::Stats SwCppCompletionIndex::stats() const {
    return m_snapshot ? m_snapshot->stats : Stats();
}

inline bool SwCppCompletionIndex::isIdentifierStart_(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return std::isalpha(uch) != 0 || uch == static_cast<unsigned char>('_');
}

inline bool SwCppCompletionIndex::isIdentifierPart_(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || uch == static_cast<unsigned char>('_');
}

inline bool SwCppCompletionIndex::startsWithKeywordAt_(const SwString& text,
                                                       size_t pos,
                                                       const char* keyword) {
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
    if (pos > 0 && isIdentifierPart_(text[pos - 1])) {
        return false;
    }
    const size_t end = pos + needle.size();
    return end >= text.size() || !isIdentifierPart_(text[end]);
}

inline size_t SwCppCompletionIndex::skipSpaces_(const SwString& text, size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    return pos;
}

inline SwString SwCppCompletionIndex::readIdentifier_(const SwString& text, size_t& pos) {
    pos = skipSpaces_(text, pos);
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

inline SwString SwCppCompletionIndex::readQualifiedIdentifier_(const SwString& text, size_t& pos) {
    pos = skipSpaces_(text, pos);
    if (pos >= text.size() || !isIdentifierStart_(text[pos])) {
        return SwString();
    }

    const size_t start = pos;
    ++pos;
    while (pos < text.size() && isIdentifierPart_(text[pos])) {
        ++pos;
    }

    while (pos + 1 < text.size() && text[pos] == ':' && text[pos + 1] == ':') {
        const size_t separatorPos = pos;
        pos += 2;
        if (pos >= text.size() || !isIdentifierStart_(text[pos])) {
            pos = separatorPos;
            break;
        }
        ++pos;
        while (pos < text.size() && isIdentifierPart_(text[pos])) {
            ++pos;
        }
    }

    return text.substr(start, pos - start);
}

inline SwString SwCppCompletionIndex::sanitizeCodeForParsing_(const SwString& text) {
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

inline SwString SwCppCompletionIndex::collapseWhitespace_(const SwString& text) {
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

inline SwString SwCppCompletionIndex::extractIdentifierBefore_(const SwString& text, size_t pos) {
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

inline SwList<SwString> SwCppCompletionIndex::splitTopLevelComma_(const SwString& text) {
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
        } else if (ch == ',' && angleDepth == 0 && parenDepth == 0 &&
                   bracketDepth == 0 && braceDepth == 0) {
            parts.append(text.substr(segmentStart, i - segmentStart));
            segmentStart = i + 1;
        }
    }

    if (segmentStart <= text.size()) {
        parts.append(text.substr(segmentStart));
    }
    return parts;
}

inline size_t SwCppCompletionIndex::findMatchingBrace_(const SwString& text, size_t openBrace) {
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

inline SwString SwCppCompletionIndex::leafNameOfQualified_(const SwString& qualifiedName) {
    const size_t separator = qualifiedName.lastIndexOf(SwString("::"));
    if (separator == static_cast<size_t>(-1)) {
        return qualifiedName;
    }
    return qualifiedName.substr(separator + 2);
}

inline bool SwCppCompletionIndex::matchesPrefix_(const SwString& value, const SwString& prefix) {
    if (prefix.isEmpty()) {
        return true;
    }
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char lhs = static_cast<unsigned char>(value[i]);
        const unsigned char rhs = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

inline void SwCppCompletionIndex::appendUniqueString_(SwList<SwString>& words, const SwString& word) {
    if (word.isEmpty()) {
        return;
    }
    for (int i = 0; i < words.size(); ++i) {
        if (words[i] == word) {
            return;
        }
    }
    words.append(word);
}

inline void SwCppCompletionIndex::appendUniqueWord_(SwList<SwString>& words, const SwString& word) {
    if (word.size() < 2 || !isIdentifierStart_(word[0])) {
        return;
    }
    appendUniqueString_(words, word);
}

inline void SwCppCompletionIndex::registerType_(Snapshot& snapshot, const SwString& qualifiedTypeName) {
    if (qualifiedTypeName.isEmpty() || snapshot.types.contains(qualifiedTypeName)) {
        return;
    }

    TypeInfo info;
    info.qualifiedName = qualifiedTypeName;
    info.leafName = leafNameOfQualified_(qualifiedTypeName);
    snapshot.types.insert(qualifiedTypeName, info);
    appendUniqueString_(snapshot.typeNames, qualifiedTypeName);
    appendUniqueWord_(snapshot.globalWords, info.leafName);
}

inline void SwCppCompletionIndex::appendTypeMember_(Snapshot& snapshot,
                                                    const SwString& qualifiedTypeName,
                                                    const MemberInfo& member) {
    if (qualifiedTypeName.isEmpty() || member.name.isEmpty()) {
        return;
    }
    registerType_(snapshot, qualifiedTypeName);
    TypeInfo info = snapshot.types.value(qualifiedTypeName);
    for (int i = 0; i < info.members.size(); ++i) {
        if (info.members[i].name == member.name && info.members[i].method == member.method) {
            return;
        }
    }
    info.members.append(member);
    snapshot.types.insert(qualifiedTypeName, info);
}

inline SwString SwCppCompletionIndex::resolveKnownTypeName_(const Snapshot& snapshot, const SwString& candidate) {
    if (candidate.isEmpty()) {
        return SwString();
    }
    if (snapshot.types.contains(candidate)) {
        return candidate;
    }

    SwString resolved;
    for (int i = 0; i < snapshot.typeNames.size(); ++i) {
        const SwString& typeName = snapshot.typeNames[i];
        if (typeName == candidate || typeName.endsWith(SwString("::") + candidate)) {
            if (!resolved.isEmpty() && resolved != typeName) {
                return SwString();
            }
            resolved = typeName;
        }
    }
    return resolved;
}

inline SwString SwCppCompletionIndex::resolveTypeNameAt_(const SwString& statement,
                                                         const Snapshot& snapshot,
                                                         size_t& typeEnd) {
    static const char* kLeadingQualifiers[] = {
        "const", "constexpr", "static", "inline", "volatile", "mutable", "typename", "extern", "friend"
    };
    static const char* kPostQualifiers[] = {
        "const", "volatile", "constexpr"
    };

    size_t pos = skipSpaces_(statement, 0);
    while (pos < statement.size()) {
        size_t tokenPos = pos;
        const SwString token = readIdentifier_(statement, tokenPos);
        bool isQualifier = false;
        for (size_t i = 0; i < sizeof(kLeadingQualifiers) / sizeof(kLeadingQualifiers[0]); ++i) {
            if (token == SwString(kLeadingQualifiers[i])) {
                isQualifier = true;
                break;
            }
        }
        if (token.isEmpty() || !isQualifier) {
            break;
        }
        pos = skipSpaces_(statement, tokenPos);
    }

    size_t candidatePos = pos;
    const SwString candidate = readQualifiedIdentifier_(statement, candidatePos);
    if (candidate.isEmpty()) {
        typeEnd = static_cast<size_t>(-1);
        return SwString();
    }

    size_t afterType = skipSpaces_(statement, candidatePos);
    if (afterType < statement.size() && statement[afterType] == '<') {
        int depth = 0;
        for (size_t i = afterType; i < statement.size(); ++i) {
            if (statement[i] == '<') {
                ++depth;
            } else if (statement[i] == '>' && depth > 0) {
                --depth;
                if (depth == 0) {
                    afterType = skipSpaces_(statement, i + 1);
                    break;
                }
            }
        }
    }

    while (afterType < statement.size()) {
        size_t tokenPos = afterType;
        const SwString token = readIdentifier_(statement, tokenPos);
        bool isQualifier = false;
        for (size_t i = 0; i < sizeof(kPostQualifiers) / sizeof(kPostQualifiers[0]); ++i) {
            if (token == SwString(kPostQualifiers[i])) {
                isQualifier = true;
                break;
            }
        }
        if (token.isEmpty() || !isQualifier) {
            break;
        }
        afterType = skipSpaces_(statement, tokenPos);
    }

    typeEnd = afterType;
    return resolveKnownTypeName_(snapshot, candidate);
}

inline void SwCppCompletionIndex::registerDeclaredVariables_(const SwString& statement,
                                                             const Snapshot& snapshot,
                                                             SwMap<SwString, VariableInfo>& variables) {
    const SwString compact = collapseWhitespace_(statement);
    if (compact.isEmpty()) {
        return;
    }

    size_t tokenPos = 0;
    const SwString firstToken = readIdentifier_(compact, tokenPos);
    static const char* kIgnored[] = {
        "if", "for", "while", "switch", "catch", "return",
        "class", "struct", "enum", "namespace", "using", "typedef",
        "template", "friend", "static_assert"
    };
    for (size_t i = 0; i < sizeof(kIgnored) / sizeof(kIgnored[0]); ++i) {
        if (firstToken == SwString(kIgnored[i])) {
            return;
        }
    }

    size_t typeEnd = static_cast<size_t>(-1);
    const SwString resolvedType = resolveTypeNameAt_(compact, snapshot, typeEnd);
    if (resolvedType.isEmpty() || typeEnd == static_cast<size_t>(-1) || typeEnd >= compact.size()) {
        return;
    }

    const SwList<SwString> declarators = splitTopLevelComma_(compact.substr(typeEnd).trimmed());
    for (int i = 0; i < declarators.size(); ++i) {
        SwString part = collapseWhitespace_(declarators[i]).trimmed();
        const size_t equalPos = part.firstIndexOf('=');
        if (equalPos != static_cast<size_t>(-1)) {
            part = part.substr(0, equalPos).trimmed();
        }
        const SwString declaredName = extractIdentifierBefore_(part, part.size());
        if (declaredName.isEmpty()) {
            continue;
        }

        VariableInfo info;
        info.typeName = resolvedType;
        const size_t namePos = part.lastIndexOf(declaredName);
        info.isPointer = (namePos == static_cast<size_t>(-1))
            ? (part.firstIndexOf('*') != static_cast<size_t>(-1))
            : (part.substr(0, namePos).firstIndexOf('*') != static_cast<size_t>(-1));
        variables.insert(declaredName, info);
    }
}

inline void SwCppCompletionIndex::registerFunctionParameters_(const SwString& header,
                                                              const Snapshot& snapshot,
                                                              SwMap<SwString, VariableInfo>& variables) {
    const SwString compact = collapseWhitespace_(header);
    const size_t openParen = compact.firstIndexOf('(');
    const size_t closeParen = compact.lastIndexOf(')');
    if (openParen == static_cast<size_t>(-1) || closeParen == static_cast<size_t>(-1) || closeParen <= openParen) {
        return;
    }

    const SwString params = compact.substr(openParen + 1, closeParen - openParen - 1).trimmed();
    if (params.isEmpty()) {
        return;
    }

    const SwList<SwString> parts = splitTopLevelComma_(params);
    for (int i = 0; i < parts.size(); ++i) {
        const SwString param = collapseWhitespace_(parts[i]).trimmed();
        if (!param.isEmpty() && param != SwString("void")) {
            registerDeclaredVariables_(param, snapshot, variables);
        }
    }
}

inline void SwCppCompletionIndex::collectDeclaredTypes_(Snapshot& snapshot,
                                                        const SwString& sanitized,
                                                        size_t begin,
                                                        size_t end,
                                                        const SwList<SwString>& scope) {
    size_t i = begin;
    while (i < end) {
        if (startsWithKeywordAt_(sanitized, i, "namespace")) {
            size_t pos = i + SwString("namespace").size();
            const SwString namespaceName = readIdentifier_(sanitized, pos);
            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                    SwList<SwString> nestedScope = scope;
                    if (!namespaceName.isEmpty()) {
                        nestedScope.append(namespaceName);
                    }
                    collectDeclaredTypes_(snapshot, sanitized, pos + 1, closeBrace, nestedScope);
                    i = closeBrace + 1;
                    continue;
                }
            }
            i = (pos < end) ? (pos + 1) : end;
            continue;
        }

        const bool isClass = startsWithKeywordAt_(sanitized, i, "class");
        const bool isStruct = startsWithKeywordAt_(sanitized, i, "struct");
        const bool isEnum = startsWithKeywordAt_(sanitized, i, "enum");
        if (isClass || isStruct || isEnum) {
            size_t pos = i + (isClass ? 5 : (isStruct ? 6 : 4));
            pos = skipSpaces_(sanitized, pos);
            if (isEnum && startsWithKeywordAt_(sanitized, pos, "class")) {
                pos += 5;
            }

            const SwString typeName = readIdentifier_(sanitized, pos);
            SwString qualified = typeName;
            if (!scope.isEmpty()) {
                qualified = scope[0];
                for (int s = 1; s < scope.size(); ++s) {
                    qualified += "::";
                    qualified += scope[s];
                }
                if (!typeName.isEmpty()) {
                    qualified += "::";
                    qualified += typeName;
                }
            }
            if (!qualified.isEmpty()) {
                registerType_(snapshot, qualified);
            }

            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                    if (!qualified.isEmpty() && !isEnum) {
                        const SwString body = sanitized.substr(pos + 1, closeBrace - pos - 1);
                        size_t statementStart = 0;
                        size_t bodyPos = 0;
                        while (bodyPos < body.size()) {
                            if (body[bodyPos] == ';') {
                                const SwString statement = collapseWhitespace_(body.substr(statementStart, bodyPos - statementStart));
                                if (!statement.isEmpty()) {
                                    const size_t parenPos = statement.firstIndexOf('(');
                                    MemberInfo member;
                                    if (parenPos != static_cast<size_t>(-1)) {
                                        member.name = extractIdentifierBefore_(statement, parenPos);
                                        member.method = true;
                                    } else {
                                        member.name = extractIdentifierBefore_(statement, statement.size());
                                        member.method = false;
                                    }
                                    if (!member.name.isEmpty() &&
                                        member.name != leafNameOfQualified_(qualified) &&
                                        member.name != (SwString("~") + leafNameOfQualified_(qualified))) {
                                        appendTypeMember_(snapshot, qualified, member);
                                    }
                                }
                                statementStart = bodyPos + 1;
                            }
                            ++bodyPos;
                        }
                    }

                    SwList<SwString> nestedScope = scope;
                    if (!typeName.isEmpty()) {
                        nestedScope.append(typeName);
                    }
                    collectDeclaredTypes_(snapshot, sanitized, pos + 1, closeBrace, nestedScope);
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
            collectDeclaredTypes_(snapshot, sanitized, i + 1, closeBrace, scope);
            i = closeBrace + 1;
            continue;
        }

        ++i;
    }
}

inline void SwCppCompletionIndex::collectScopeSymbols_(const SwString& original,
                                                       const SwString& sanitized,
                                                       size_t begin,
                                                       size_t end,
                                                       SwList<SwString>& words) {
    size_t statementStart = begin;
    size_t i = begin;

    while (i < end) {
        if (sanitized[i] == '{') {
            const SwString header = original.substr(statementStart, i - statementStart);
            const SwString compact = collapseWhitespace_(header);
            const size_t parenPos = compact.firstIndexOf('(');
            if (parenPos != static_cast<size_t>(-1)) {
                appendUniqueWord_(words, extractIdentifierBefore_(compact, parenPos));
            }
            const size_t closeBrace = findMatchingBrace_(sanitized, i);
            if (closeBrace == static_cast<size_t>(-1)) {
                break;
            }
            collectScopeSymbols_(original, sanitized, i + 1, closeBrace, words);
            i = closeBrace + 1;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == ';') {
            const SwString statement = collapseWhitespace_(original.substr(statementStart, i - statementStart));
            appendUniqueWord_(words, extractIdentifierBefore_(statement, statement.size()));
            statementStart = i + 1;
        }

        ++i;
    }
}

inline void SwCppCompletionIndex::buildScopeIndex_(Snapshot& snapshot,
                                                   const SwString& sanitized,
                                                   size_t begin,
                                                   size_t end,
                                                   int scopeIndex) {
    size_t statementStart = begin;
    size_t i = begin;
    while (i < end) {
        const bool isNamespace = startsWithKeywordAt_(sanitized, i, "namespace");
        const bool isClass = startsWithKeywordAt_(sanitized, i, "class");
        const bool isStruct = startsWithKeywordAt_(sanitized, i, "struct");
        const bool isEnum = startsWithKeywordAt_(sanitized, i, "enum");

        if (isNamespace || isClass || isStruct || isEnum) {
            size_t pos = i + (isNamespace ? 9 : (isClass ? 5 : (isStruct ? 6 : 4)));
            pos = skipSpaces_(sanitized, pos);
            if (isEnum && startsWithKeywordAt_(sanitized, pos, "class")) {
                pos += 5;
            }
            const SwString typeName = (isClass || isStruct) ? readIdentifier_(sanitized, pos) : SwString();
            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                    ScopeInfo child;
                    child.begin = pos + 1;
                    child.end = closeBrace;
                    child.parent = scopeIndex;
                    child.className = (isClass || isStruct) ? typeName : SwString();
                    snapshot.scopes.push_back(child);
                    buildScopeIndex_(snapshot, sanitized, pos + 1, closeBrace, static_cast<int>(snapshot.scopes.size()) - 1);
                    i = closeBrace + 1;
                    statementStart = i;
                    continue;
                }
            }
            i = (pos < end) ? (pos + 1) : end;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == '{') {
            const size_t closeBrace = findMatchingBrace_(sanitized, i);
            if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                break;
            }
            ScopeInfo child;
            child.begin = i + 1;
            child.end = closeBrace;
            child.parent = scopeIndex;
            registerFunctionParameters_(sanitized.substr(statementStart, i - statementStart), snapshot, child.variables);
            snapshot.scopes.push_back(child);
            buildScopeIndex_(snapshot, sanitized, i + 1, closeBrace, static_cast<int>(snapshot.scopes.size()) - 1);
            i = closeBrace + 1;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == ';') {
            registerDeclaredVariables_(sanitized.substr(statementStart, i - statementStart),
                                       snapshot,
                                       snapshot.scopes[static_cast<size_t>(scopeIndex)].variables);
            statementStart = i + 1;
        }

        ++i;
    }
}

inline std::shared_ptr<SwCppCompletionIndex::Snapshot> SwCppCompletionIndex::buildSnapshot_(const SwString& text) {
    const auto started = std::chrono::steady_clock::now();

    std::shared_ptr<Snapshot> snapshot(new Snapshot());
    snapshot->text = text;
    snapshot->sanitized = sanitizeCodeForParsing_(text);

    static const char* kWords[] = {
        "auto", "bool", "break", "case", "catch", "char", "class", "concept",
        "const", "constexpr", "continue", "double", "else", "enum", "explicit",
        "false", "float", "for", "if", "include", "inline", "int", "namespace",
        "noexcept", "nullptr", "override", "private", "protected", "public", "return",
        "static", "std", "string", "string_view", "struct", "switch", "template",
        "this", "throw", "true", "typename", "using", "vector", "virtual", "void",
        "while"
    };
    for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); ++i) {
        appendUniqueWord_(snapshot->globalWords, SwString(kWords[i]));
    }

    const SwList<SwString> rootScope;
    collectDeclaredTypes_(*snapshot, snapshot->sanitized, 0, snapshot->sanitized.size(), rootScope);
    collectScopeSymbols_(snapshot->text, snapshot->sanitized, 0, snapshot->sanitized.size(), snapshot->globalWords);

    ScopeInfo root;
    root.begin = 0;
    root.end = snapshot->sanitized.size();
    root.parent = -1;
    snapshot->scopes.push_back(root);
    buildScopeIndex_(*snapshot, snapshot->sanitized, 0, snapshot->sanitized.size(), 0);

    snapshot->stats.textLength = snapshot->text.size();
    snapshot->stats.lineCount = 1;
    for (size_t i = 0; i < snapshot->text.size(); ++i) {
        if (snapshot->text[i] == '\n') {
            ++snapshot->stats.lineCount;
        }
    }
    snapshot->stats.scopeCount = static_cast<int>(snapshot->scopes.size());
    snapshot->stats.typeCount = static_cast<int>(snapshot->typeNames.size());
    snapshot->stats.symbolCount = static_cast<int>(snapshot->globalWords.size());
    snapshot->stats.lastBuildMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    snapshot->stats.ready = true;
    return snapshot;
}

inline int SwCppCompletionIndex::innermostScope_(const Snapshot& snapshot, size_t cursorPos) {
    const size_t cursor = std::min(cursorPos, snapshot.text.size());
    int bestIndex = 0;
    size_t bestSpan = static_cast<size_t>(-1);
    for (int i = 0; i < snapshot.scopes.size(); ++i) {
        const ScopeInfo& scope = snapshot.scopes[static_cast<size_t>(i)];
        if (cursor < scope.begin || cursor > scope.end) {
            continue;
        }
        const size_t span = scope.end - scope.begin;
        if (span <= bestSpan) {
            bestSpan = span;
            bestIndex = i;
        }
    }
    return bestIndex;
}

inline int SwCppCompletionIndex::innermostClassScope_(const Snapshot& snapshot, size_t cursorPos) {
    int scopeIndex = innermostScope_(snapshot, cursorPos);
    while (scopeIndex >= 0) {
        const ScopeInfo& scope = snapshot.scopes[static_cast<size_t>(scopeIndex)];
        if (!scope.className.isEmpty()) {
            return scopeIndex;
        }
        scopeIndex = scope.parent;
    }
    return -1;
}

inline void SwCppCompletionIndex::collectVisibleVariables_(const Snapshot& snapshot,
                                                           size_t cursorPos,
                                                           SwMap<SwString, VariableInfo>& variables) {
    SwVector<int> chain;
    int scopeIndex = innermostScope_(snapshot, cursorPos);
    while (scopeIndex >= 0) {
        chain.push_back(scopeIndex);
        scopeIndex = snapshot.scopes[static_cast<size_t>(scopeIndex)].parent;
    }

    for (int i = static_cast<int>(chain.size()) - 1; i >= 0; --i) {
        const ScopeInfo& scope = snapshot.scopes[static_cast<size_t>(chain[static_cast<size_t>(i)])];
        const SwList<SwString> names = scope.variables.keys();
        for (int keyIndex = 0; keyIndex < names.size(); ++keyIndex) {
            variables.insert(names[keyIndex], scope.variables.value(names[keyIndex]));
        }
    }

    const int classScopeIndex = innermostClassScope_(snapshot, cursorPos);
    if (classScopeIndex >= 0) {
        const ScopeInfo& classScope = snapshot.scopes[static_cast<size_t>(classScopeIndex)];
        VariableInfo selfInfo;
        selfInfo.typeName = resolveKnownTypeName_(snapshot, classScope.className);
        if (selfInfo.typeName.isEmpty()) {
            selfInfo.typeName = classScope.className;
        }
        selfInfo.isPointer = true;
        variables.insert(SwString("this"), selfInfo);
    }
}

inline SwCppCompletionIndex::AccessInfo SwCppCompletionIndex::accessInfoAt_(const SwString& text, size_t cursorPos) {
    AccessInfo info;
    if (text.isEmpty()) {
        return info;
    }

    const size_t cursor = std::min(cursorPos, text.size());
    size_t prefixStart = cursor;
    while (prefixStart > 0 && isIdentifierPart_(text[prefixStart - 1])) {
        --prefixStart;
    }
    info.memberPrefix = text.substr(prefixStart, cursor - prefixStart);

    size_t scan = prefixStart;
    while (scan > 0 && std::isspace(static_cast<unsigned char>(text[scan - 1])) != 0) {
        --scan;
    }

    if (scan >= 2 && text[scan - 2] == '-' && text[scan - 1] == '>') {
        info.usesArrow = true;
        info.operatorPos = scan - 2;
    } else if (scan >= 1 && text[scan - 1] == '.') {
        info.usesArrow = false;
        info.operatorPos = scan - 1;
    } else {
        return info;
    }

    info.baseName = extractIdentifierBefore_(text, info.operatorPos);
    info.valid = !info.baseName.isEmpty();
    return info;
}

inline SwList<SwCppCompletionItem> SwCppCompletionIndex::completionItems(size_t cursorPos,
                                                                         const SwString& prefix,
                                                                         int maxItems) const {
    SwList<SwCppCompletionItem> items;
    if (!m_snapshot || maxItems <= 0) {
        return items;
    }

    const Snapshot& snapshot = *m_snapshot;
    const AccessInfo access = accessInfoAt_(snapshot.text, cursorPos);
    SwMap<SwString, VariableInfo> visibleVariables;
    collectVisibleVariables_(snapshot, cursorPos, visibleVariables);

    if (access.valid && visibleVariables.contains(access.baseName)) {
        const VariableInfo varInfo = visibleVariables.value(access.baseName);
        const SwString typeName = resolveKnownTypeName_(snapshot, varInfo.typeName);
        if (!typeName.isEmpty() && snapshot.types.contains(typeName)) {
            TypeInfo typeInfo = snapshot.types.value(typeName);
            std::stable_sort(typeInfo.members.begin(), typeInfo.members.end(), [](const MemberInfo& lhs, const MemberInfo& rhs) {
                if (lhs.method != rhs.method) {
                    return lhs.method;
                }
                return lhs.name < rhs.name;
            });

            for (int i = 0; i < typeInfo.members.size() && items.size() < maxItems; ++i) {
                if (!matchesPrefix_(typeInfo.members[i].name, prefix) &&
                    !matchesPrefix_(typeInfo.members[i].name, access.memberPrefix)) {
                    continue;
                }
                SwCppCompletionItem item;
                item.insertText = typeInfo.members[i].name;
                item.displayText = typeInfo.members[i].name;
                if (typeInfo.members[i].method) {
                    item.displayText += "()";
                }
                item.toolTip = typeInfo.qualifiedName;
                items.append(item);
            }
            return items;
        }
    }

    SwList<SwString> words = snapshot.globalWords;
    const SwList<SwString> variableNames = visibleVariables.keys();
    for (int i = 0; i < variableNames.size(); ++i) {
        appendUniqueWord_(words, variableNames[i]);
    }

    const int classScopeIndex = innermostClassScope_(snapshot, cursorPos);
    if (classScopeIndex >= 0) {
        const SwString classType = resolveKnownTypeName_(snapshot, snapshot.scopes[static_cast<size_t>(classScopeIndex)].className);
        if (!classType.isEmpty() && snapshot.types.contains(classType)) {
            const TypeInfo typeInfo = snapshot.types.value(classType);
            for (int i = 0; i < typeInfo.members.size(); ++i) {
                appendUniqueWord_(words, typeInfo.members[i].name);
            }
        }
    }

    std::stable_sort(words.begin(), words.end(), [](const SwString& lhs, const SwString& rhs) {
        return lhs < rhs;
    });

    for (int i = 0; i < words.size() && items.size() < maxItems; ++i) {
        if (!matchesPrefix_(words[i], prefix)) {
            continue;
        }
        SwCppCompletionItem item;
        item.displayText = words[i];
        item.insertText = words[i];
        items.append(item);
    }
    return items;
}

inline bool SwCppCompletionIndex::shouldPreferArrowAccess(size_t cursorPos) const {
    if (!m_snapshot) {
        return false;
    }
    const AccessInfo access = accessInfoAt_(m_snapshot->text, cursorPos);
    if (!access.valid || access.usesArrow) {
        return false;
    }

    SwMap<SwString, VariableInfo> visibleVariables;
    collectVisibleVariables_(*m_snapshot, cursorPos, visibleVariables);
    return visibleVariables.contains(access.baseName) && visibleVariables.value(access.baseName).isPointer;
}

inline void SwCppCompletionIndex::startAsyncRebuild_() {
    if (!m_textProvider) {
        return;
    }
    if (m_buildInFlight) {
        m_rebuildQueued = true;
        return;
    }

    if (m_rebuildTimer && m_rebuildTimer->isActive()) {
        m_rebuildTimer->stop();
    }

    const SwString text = m_textProvider();
    if (!m_asyncEnabled) {
        m_rebuildQueued = false;
        applySnapshot_(buildSnapshot_(text));
        return;
    }

    SwCoreApplication* app = SwCoreApplication::instance(false);
    if (!app) {
        m_rebuildQueued = false;
        applySnapshot_(buildSnapshot_(text));
        return;
    }

    ++m_requestedBuildId;
    const int buildId = m_requestedBuildId;
    m_buildInFlight = true;
    m_rebuildQueued = false;
    const std::weak_ptr<int> weakLife = m_lifeToken;

    threadPool()->start([this, weakLife, app, text, buildId]() {
        const std::shared_ptr<Snapshot> snapshot = buildSnapshot_(text);
        app->postEvent([this, weakLife, snapshot, buildId]() {
            if (weakLife.expired()) {
                return;
            }
            if (buildId < m_requestedBuildId) {
                m_buildInFlight = false;
                if (m_rebuildQueued || buildId != m_requestedBuildId) {
                    startAsyncRebuild_();
                }
                return;
            }
            m_buildInFlight = false;
            applySnapshot_(snapshot);
            if (m_rebuildQueued) {
                startAsyncRebuild_();
            }
        });
    });
}

inline void SwCppCompletionIndex::applySnapshot_(const std::shared_ptr<Snapshot>& snapshot) {
    m_snapshot = snapshot;
    m_buildInFlight = false;
    if (m_snapshot && m_snapshot->stats.ready) {
        indexUpdated();
    }
}
