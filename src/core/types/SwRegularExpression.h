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
#include <regex>
#include "SwString.h"
#include "SwList.h"

class SwRegularExpressionMatch {
public:
    // Constructeur par défaut
    SwRegularExpressionMatch()
        : _match() {}

    // Constructeur avec std::smatch
    explicit SwRegularExpressionMatch(const std::smatch& match)
        : _match(match) {}

    // Constructeur de copie
    SwRegularExpressionMatch(const SwRegularExpressionMatch& other)
        : _match(other._match) {}

    // Opérateur d'affectation
    SwRegularExpressionMatch& operator=(const SwRegularExpressionMatch& other) {
        if (this != &other) {
            _match = other._match;
        }
        return *this;
    }

    // Vérifier si une correspondance existe
    bool hasMatch() const { return !_match.empty(); }

    // Obtenir une correspondance capturée par index
    SwString captured(int index = 0) const {
        if (index >= 0 && index < static_cast<int>(_match.size())) {
            return SwString(_match[index].str().c_str());
        }
        return SwString("");
    }

    // Obtenir la position de début de la correspondance capturée
    int capturedStart(int index = 0) const {
        if (index >= 0 && index < static_cast<int>(_match.size())) {
            return static_cast<int>(_match.position(index));
        }
        return -1; // Retourne -1 si l'index est invalide
    }

    // Obtenir la position de fin de la correspondance capturée
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
    explicit SwRegularExpression(const SwString& pattern = "")
        : _pattern(pattern), _isValid(true) {
        try {
            _regex = std::regex(pattern.toStdString());
        } catch (const std::regex_error&) {
            _isValid = false;
        }
    }

    // Copy constructor
    SwRegularExpression(const SwRegularExpression& other)
        : _pattern(other._pattern), _regex(other._regex), _isValid(other._isValid) {}

    // Copy assignment operator
    SwRegularExpression& operator=(const SwRegularExpression& other) {
        if (this != &other) {
            _pattern = other._pattern;
            _regex = other._regex;
            _isValid = other._isValid;
        }
        return *this;
    }

    bool operator==(const SwRegularExpression& other) const {
        return _pattern == other._pattern && _isValid == other._isValid;
    }

    bool operator!=(const SwRegularExpression& other) const {
        return !(*this == other);
    }

    bool isValid() const { return _isValid; }
    SwString pattern() const { return _pattern; }

    const std::regex& getStdRegex() const { return _regex; }

    SwRegularExpressionMatch match(const SwString& text) const {
        std::smatch match;
        if (std::regex_search(text.toStdString(), match, _regex)) {
            return SwRegularExpressionMatch(match);
        }
        return SwRegularExpressionMatch(std::smatch());
    }

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
