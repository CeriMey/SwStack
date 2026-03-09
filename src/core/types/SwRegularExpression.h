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

#ifndef SWREGULAREXPRESSION_H
#define SWREGULAREXPRESSION_H

/**
 * @file src/core/types/SwRegularExpression.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwRegularExpression in the CoreSw fundamental
 * types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the regular expression interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwRegularExpressionMatch and SwRegularExpression.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

#include <regex>
#include "SwString.h"
#include "SwList.h"

class SwRegularExpressionMatch {
public:
    // Constructeur par défaut
    /**
     * @brief Constructs a `SwRegularExpressionMatch` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRegularExpressionMatch()
        : _match() {}

    // Constructeur avec std::smatch
    /**
     * @brief Constructs a `SwRegularExpressionMatch` instance.
     * @param match Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwRegularExpressionMatch(const std::smatch& match)
        : _match(match) {}

    // Constructeur de copie
    /**
     * @brief Constructs a `SwRegularExpressionMatch` instance.
     * @param _match Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRegularExpressionMatch(const SwRegularExpressionMatch& other)
        : _match(other._match) {}

    // Opérateur d'affectation
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwRegularExpressionMatch& operator=(const SwRegularExpressionMatch& other) {
        if (this != &other) {
            _match = other._match;
        }
        return *this;
    }

    // Vérifier si une correspondance existe
    /**
     * @brief Returns whether the object reports match.
     * @return `true` when the object reports match; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool hasMatch() const { return !_match.empty(); }

    // Obtenir une correspondance capturée par index
    /**
     * @brief Performs the `captured` operation.
     * @param index Value passed to the method.
     * @return The requested captured.
     */
    SwString captured(int index = 0) const {
        if (index >= 0 && index < static_cast<int>(_match.size())) {
            return SwString(_match[index].str().c_str());
        }
        return SwString("");
    }

    // Obtenir la position de début de la correspondance capturée
    /**
     * @brief Performs the `capturedStart` operation.
     * @param index Value passed to the method.
     * @return The requested captured Start.
     */
    int capturedStart(int index = 0) const {
        if (index >= 0 && index < static_cast<int>(_match.size())) {
            return static_cast<int>(_match.position(index));
        }
        return -1; // Retourne -1 si l'index est invalide
    }

    // Obtenir la position de fin de la correspondance capturée
    /**
     * @brief Performs the `capturedEnd` operation.
     * @param index Value passed to the method.
     * @return The requested captured End.
     */
    int capturedEnd(int index = 0) const {
        if (index >= 0 && index < static_cast<int>(_match.size())) {
            return static_cast<int>(_match.position(index) + _match.length(index));
        }
        return -1; // Retourne -1 si l'index est invalide
    }
private:
    std::smatch _match;
};



class SwRegularExpression {
public:
    /**
     * @brief Constructs a `SwRegularExpression` instance.
     * @param pattern Pattern used by the operation.
     * @param true Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwRegularExpression(const SwString& pattern = "")
        : _pattern(pattern), _isValid(true) {
        try {
            _regex = std::regex(pattern.toStdString());
        } catch (const std::regex_error&) {
            _isValid = false;
        }
    }

    // Copy constructor
    /**
     * @brief Constructs a `SwRegularExpression` instance.
     * @param _isValid Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRegularExpression(const SwRegularExpression& other)
        : _pattern(other._pattern), _regex(other._regex), _isValid(other._isValid) {}

    // Copy assignment operator
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwRegularExpression& operator=(const SwRegularExpression& other) {
        if (this != &other) {
            _pattern = other._pattern;
            _regex = other._regex;
            _isValid = other._isValid;
        }
        return *this;
    }

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwRegularExpression& other) const {
        return _pattern == other._pattern && _isValid == other._isValid;
    }

    /**
     * @brief Performs the `operator!=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwRegularExpression& other) const {
        return !(*this == other);
    }

    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const { return _isValid; }
    /**
     * @brief Returns the current pattern.
     * @return The current pattern.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString pattern() const { return _pattern; }

    /**
     * @brief Returns the current std Regex.
     * @return The current std Regex.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const std::regex& getStdRegex() const { return _regex; }

    /**
     * @brief Performs the `match` operation.
     * @param text Value passed to the method.
     * @return The requested match.
     */
    SwRegularExpressionMatch match(const SwString& text) const {
        std::smatch match;
        if (std::regex_search(text.toStdString(), match, _regex)) {
            return SwRegularExpressionMatch(match);
        }
        return SwRegularExpressionMatch(std::smatch());
    }

    /**
     * @brief Performs the `globalMatch` operation.
     * @param text Value passed to the method.
     * @return The requested global Match.
     */
    SwList<SwString> globalMatch(const SwString& text) const {
        SwList<SwString> matches;
        try {
            auto begin = std::sregex_iterator(text.toStdString().begin(), text.toStdString().end(), _regex);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                matches.append(SwString(it->str().c_str()));
            }
        } catch (const std::regex_error&) {
            // Handle invalid regex usage
        }
        return matches;
    }

private:
    SwString _pattern;
    std::regex _regex;
    bool _isValid;
};

inline SwString& SwString::remove(const SwRegularExpression& re) {
    if (!re.isValid()) return *this;
    try {
        data_ = std::regex_replace(data_, re.getStdRegex(), std::string());
    } catch (const std::regex_error&) {
        // ignore invalid regex usage
    }
    return *this;
}


#endif // SWREGULAREXPRESSION_H
