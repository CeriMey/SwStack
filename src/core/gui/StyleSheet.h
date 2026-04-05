/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 ***************************************************************************************************/

#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Sw.h"
#include "SwMap.h"
#include "SwString.h"

class StyleSheet {
public:
    enum StateFlag {
        StateNone = 0x00,
        StateHovered = 0x01,
        StatePressed = 0x02,
        StateDisabled = 0x04,
        StateFocused = 0x08,
        StateChecked = 0x10
    };

    struct BoxEdges {
        int top{0};
        int right{0};
        int bottom{0};
        int left{0};
    };

    struct Rule {
        SwString selector;
        SwString typeSelector;
        SwString objectName;
        unsigned int stateMask{StateNone};
        int order{0};
        SwMap<SwString, SwString> properties;
    };

    SwMap<SwString, SwMap<SwString, SwString>> styles;

    StyleSheet() {
        colorNames = {
            {"red", makeColor(255, 0, 0)},
            {"green", makeColor(0, 255, 0)},
            {"blue", makeColor(0, 0, 255)},
            {"yellow", makeColor(255, 255, 0)},
            {"black", makeColor(0, 0, 0)},
            {"white", makeColor(255, 255, 255)},
            {"gray", makeColor(128, 128, 128)},
            {"cyan", makeColor(0, 255, 255)},
            {"magenta", makeColor(255, 0, 255)},
            {"orange", makeColor(255, 165, 0)},
            {"purple", makeColor(128, 0, 128)},
            {"brown", makeColor(165, 42, 42)},
            {"pink", makeColor(255, 192, 203)},
            {"lime", makeColor(0, 255, 0)},
            {"olive", makeColor(128, 128, 0)},
            {"navy", makeColor(0, 0, 128)},
            {"teal", makeColor(0, 128, 128)},
            {"maroon", makeColor(128, 0, 0)},
            {"silver", makeColor(192, 192, 192)},
            {"gold", makeColor(255, 215, 0)}
        };
    }

    void clear() {
        styles.clear();
        rules_.clear();
        nextOrder_ = 0;
    }

    void mergeFrom(const StyleSheet& other) {
        for (const auto& selectorEntry : other.styles) {
            for (const auto& propEntry : selectorEntry.second) {
                styles[selectorEntry.first][propEntry.first] = propEntry.second;
            }
        }
        for (const Rule& rule : other.rules_) {
            Rule merged = rule;
            merged.order = nextOrder_++;
            rules_.push_back(merged);
        }
    }

    void parseStyleSheet(const SwString& css) {
        clear();
        const std::string stripped = stripComments_(css.toStdString());
        size_t pos = 0;
        while (true) {
            const size_t open = stripped.find('{', pos);
            if (open == std::string::npos) {
                break;
            }
            const size_t close = stripped.find('}', open + 1);
            if (close == std::string::npos) {
                break;
            }
            parseBlock_(trim_(stripped.substr(pos, open - pos)), stripped.substr(open + 1, close - open - 1));
            pos = close + 1;
        }
    }

    SwString getStyleProperty(const SwString& selector, const SwString& property) const {
        auto selectorIt = styles.find(selector);
        if (selectorIt != styles.end()) {
            auto propIt = selectorIt->second.find(property);
            if (propIt != selectorIt->second.end()) {
                return propIt->second;
            }
        }
        const SwString alias = aliasSelector_(selector);
        if (alias != selector) {
            selectorIt = styles.find(alias);
            if (selectorIt != styles.end()) {
                auto propIt = selectorIt->second.find(property);
                if (propIt != selectorIt->second.end()) {
                    return propIt->second;
                }
            }
        }
        return SwString();
    }

    SwString resolveStyleProperty(const SwList<SwString>& selectors,
                                  const SwString& objectName,
                                  unsigned int stateMask,
                                  const SwString& property) const {
        return resolveProperty_(selectors, objectName, stateMask, property, nullptr);
    }

    bool hasExplicitStateProperty(const SwList<SwString>& selectors,
                                  const SwString& objectName,
                                  unsigned int stateMask,
                                  const SwString& property) const {
        bool matchedState = false;
        (void)resolveProperty_(selectors, objectName, stateMask, property, &matchedState);
        return matchedState;
    }

    SwColor parseColor(const SwString& color, float* alphaOut = nullptr) {
        if (alphaOut) {
            *alphaOut = 1.0f;
        }
        std::string value = trim_(color.toStdString());
        if (value.empty()) {
            return makeColor(0, 0, 0);
        }
        if (value == "transparent") {
            if (alphaOut) {
                *alphaOut = 0.0f;
            }
            return makeColor(0, 0, 0);
        }
        if (value.find("linear-gradient") != std::string::npos) {
            const size_t open = value.find('(');
            const size_t close = value.rfind(')');
            if (open != std::string::npos && close != std::string::npos && close > open + 1) {
                const std::string inner = value.substr(open + 1, close - open - 1);
                const size_t comma = inner.find(',');
                return parseColor(SwString(comma != std::string::npos ? inner.substr(0, comma) : inner), alphaOut);
            }
        }
        if (value[0] == '#') {
            if (value.length() != 7) {
                throw std::invalid_argument("Invalid hex color");
            }
            unsigned int rgb = 0;
            std::stringstream ss;
            ss << std::hex << value.substr(1);
            ss >> rgb;
            return makeColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
        if (value.find("rgba(") == 0 && value.back() == ')') {
            const std::string inner = value.substr(5, value.size() - 6);
            std::stringstream ss(inner);
            std::string item;
            int rgba[3] = {0, 0, 0};
            int i = 0;
            while (i < 3 && std::getline(ss, item, ',')) {
                rgba[i] = std::max(0, std::min(255, std::stoi(trim_(item))));
                ++i;
            }
            if (i != 3) {
                throw std::invalid_argument("Invalid rgba");
            }
            std::string alphaPart;
            if (std::getline(ss, alphaPart, ',')) {
                float alpha = 1.0f;
                try {
                    alpha = std::stof(trim_(alphaPart));
                } catch (...) {
                    alpha = 1.0f;
                }
                if (alpha > 1.0f) {
                    alpha /= 255.0f;
                }
                if (alphaOut) {
                    *alphaOut = std::max(0.0f, std::min(1.0f, alpha));
                }
            }
            return makeColor(rgba[0], rgba[1], rgba[2]);
        }
        if (value.find("rgb(") == 0 && value.back() == ')') {
            const std::string inner = value.substr(4, value.size() - 5);
            std::stringstream ss(inner);
            std::string item;
            int rgb[3] = {0, 0, 0};
            int i = 0;
            while (std::getline(ss, item, ',') && i < 3) {
                rgb[i] = std::max(0, std::min(255, std::stoi(trim_(item))));
                ++i;
            }
            if (i != 3) {
                throw std::invalid_argument("Invalid rgb");
            }
            return makeColor(rgb[0], rgb[1], rgb[2]);
        }
        auto it = colorNames.find(SwString(value));
        return it != colorNames.end() ? it->second : makeColor(0, 0, 0);
    }

    static int parsePixelValue(const SwString& value, int defaultValue) {
        std::string s = trim_(value.toStdString());
        if (s.empty()) {
            return defaultValue;
        }
        const size_t pos = s.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        try {
            return std::stoi(s);
        } catch (...) {
            return defaultValue;
        }
    }

    static BoxEdges parseBoxEdges(const SwString& value, const BoxEdges& defaults = BoxEdges()) {
        BoxEdges edges = defaults;
        const std::vector<std::string> tokens = splitSpace_(value.toStdString());
        if (tokens.empty()) {
            return edges;
        }
        if (tokens.size() == 1) {
            const int v = parsePixelValue(SwString(tokens[0]), edges.top);
            edges.top = edges.right = edges.bottom = edges.left = v;
        } else if (tokens.size() == 2) {
            const int v = parsePixelValue(SwString(tokens[0]), edges.top);
            const int h = parsePixelValue(SwString(tokens[1]), edges.right);
            edges.top = edges.bottom = v;
            edges.left = edges.right = h;
        } else if (tokens.size() == 3) {
            edges.top = parsePixelValue(SwString(tokens[0]), edges.top);
            edges.right = edges.left = parsePixelValue(SwString(tokens[1]), edges.right);
            edges.bottom = parsePixelValue(SwString(tokens[2]), edges.bottom);
        } else {
            edges.top = parsePixelValue(SwString(tokens[0]), edges.top);
            edges.right = parsePixelValue(SwString(tokens[1]), edges.right);
            edges.bottom = parsePixelValue(SwString(tokens[2]), edges.bottom);
            edges.left = parsePixelValue(SwString(tokens[3]), edges.left);
        }
        return edges;
    }

    static FontWeight parseFontWeightValue(const SwString& value, FontWeight fallback) {
        const std::string lower = lower_(trim_(value.toStdString()));
        if (lower.empty()) {
            return fallback;
        }
        if (lower == "normal") return Normal;
        if (lower == "medium") return Medium;
        if (lower == "semibold" || lower == "demibold") return SemiBold;
        if (lower == "bold") return Bold;
        if (lower == "extrabold" || lower == "ultrabold") return ExtraBold;
        if (lower == "light") return Light;
        if (lower == "thin") return Thin;
        const int numeric = parsePixelValue(value, static_cast<int>(fallback));
        if (numeric >= Heavy) return Heavy;
        if (numeric >= ExtraBold) return ExtraBold;
        if (numeric >= Bold) return Bold;
        if (numeric >= SemiBold) return SemiBold;
        if (numeric >= Medium) return Medium;
        if (numeric >= Normal) return Normal;
        if (numeric >= Light) return Light;
        return fallback;
    }

    static SwString normalizeFontFamily(const SwString& value) {
        std::string family = trim_(value.toStdString());
        if (family.empty()) {
            return SwString();
        }
        const size_t comma = family.find(',');
        if (comma != std::string::npos) {
            family = family.substr(0, comma);
        }
        family = trim_(family);
        if (family.size() >= 2) {
            const char first = family.front();
            const char last = family.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                family = family.substr(1, family.size() - 2);
            }
        }
        return SwString(family);
    }

private:
    struct ParsedSelector_ {
        SwString selector;
        SwString typeSelector;
        SwString objectName;
        unsigned int stateMask{StateNone};
    };

    void parseBlock_(const std::string& selectorsText, const std::string& block) {
        const std::vector<std::string> selectors = splitSelectors_(selectorsText);
        const std::vector<std::pair<std::string, std::string>> declarations = parseDecls_(block);
        for (const std::string& selectorText : selectors) {
            ParsedSelector_ parsed = parseSelector_(selectorText);
            if (parsed.selector.isEmpty()) {
                continue;
            }
            Rule rule;
            rule.selector = parsed.selector;
            rule.typeSelector = parsed.typeSelector;
            rule.objectName = parsed.objectName;
            rule.stateMask = parsed.stateMask;
            rule.order = nextOrder_++;
            for (const auto& decl : declarations) {
                appendDecl_(rule, decl.first, decl.second);
            }
            if (!rule.properties.isEmpty()) {
                for (const auto& prop : rule.properties) {
                    styles[rule.selector][prop.first] = prop.second;
                }
                rules_.push_back(rule);
            }
        }
    }

    void appendDecl_(Rule& rule, const std::string& propertyName, const std::string& value) {
        std::string property = lower_(trim_(propertyName));
        const std::string trimmedValue = trim_(value);
        if (property.empty() || trimmedValue.empty()) {
            return;
        }
        unsigned int legacyState = StateNone;
        std::string canonical = property;
        if (legacyStateAlias_(canonical, legacyState)) {
            Rule aliasRule = rule;
            aliasRule.stateMask |= legacyState;
            normalizeDecl_(aliasRule, canonical, trimmedValue);
            mergeProps_(rule, aliasRule);
        }
        normalizeDecl_(rule, property, trimmedValue);
    }

    static void normalizeDecl_(Rule& rule, const std::string& property, const std::string& value) {
        if (property == "background") {
            rule.properties["background-color"] = SwString(value);
            return;
        }
        if (property == "border") {
            borderShorthand_(rule, value);
            return;
        }
        if (property == "border-top" || property == "border-right" || property == "border-bottom" || property == "border-left") {
            borderSideShorthand_(rule, property, value);
            return;
        }
        rule.properties[SwString(property)] = SwString(value);
    }

    static void borderShorthand_(Rule& rule, const std::string& value) {
        const std::string lower = lower_(trim_(value));
        if (lower == "none" || lower == "0" || lower == "0px") {
            rule.properties["border-width"] = "0";
            rule.properties["border-style"] = "none";
            return;
        }
        const std::vector<std::string> tokens = splitSpace_(value);
        std::string colorPart;
        std::string stylePart;
        std::string widthPart;
        for (const std::string& token : tokens) {
            const std::string tokenLower = lower_(trim_(token));
            if (stylePart.empty() && (tokenLower == "solid" || tokenLower == "none" || tokenLower == "dashed" || tokenLower == "dotted")) {
                stylePart = tokenLower;
                continue;
            }
            if (widthPart.empty() && looksLikeLength_(tokenLower)) {
                widthPart = tokenLower;
                continue;
            }
            if (!colorPart.empty()) {
                colorPart += " ";
            }
            colorPart += token;
        }
        if (!widthPart.empty()) rule.properties["border-width"] = SwString(widthPart);
        if (!stylePart.empty()) rule.properties["border-style"] = SwString(stylePart);
        if (!colorPart.empty()) rule.properties["border-color"] = SwString(trim_(colorPart));
    }

    static void borderSideShorthand_(Rule& rule, const std::string& property, const std::string& value) {
        Rule temp = rule;
        borderShorthand_(temp, value);
        const std::string side = property.substr(std::string("border-").size());
        const SwString width = temp.properties.value("border-width");
        const SwString style = temp.properties.value("border-style");
        const SwString color = temp.properties.value("border-color");
        if (!width.isEmpty()) rule.properties[SwString("border-" + side + "-width")] = width;
        if (!style.isEmpty()) rule.properties[SwString("border-" + side + "-style")] = style;
        if (!color.isEmpty()) rule.properties[SwString("border-" + side + "-color")] = color;
    }

    SwString resolveProperty_(const SwList<SwString>& selectors,
                              const SwString& objectName,
                              unsigned int stateMask,
                              const SwString& property,
                              bool* matchedState) const {
        if (matchedState) {
            *matchedState = false;
        }
        const std::vector<SwString> expanded = expandSelectors_(selectors);
        int bestSpec = -1;
        int bestOrder = -1;
        SwString result;
        for (const Rule& rule : rules_) {
            if ((rule.stateMask & stateMask) != rule.stateMask) {
                continue;
            }
            if (!matches_(rule, expanded, objectName)) {
                continue;
            }
            auto propIt = rule.properties.find(property);
            if (propIt == rule.properties.end()) {
                continue;
            }
            const int spec = specificity_(rule, expanded);
            if (spec > bestSpec || (spec == bestSpec && rule.order >= bestOrder)) {
                bestSpec = spec;
                bestOrder = rule.order;
                result = propIt->second;
                if (matchedState && rule.stateMask != StateNone) {
                    *matchedState = true;
                }
            }
        }
        return result;
    }

    static bool matches_(const Rule& rule, const std::vector<SwString>& selectors, const SwString& objectName) {
        if (!rule.objectName.isEmpty() && rule.objectName != objectName) {
            return false;
        }
        if (rule.typeSelector.isEmpty() || rule.typeSelector == "*") {
            return true;
        }
        for (const SwString& selector : selectors) {
            if (selector == rule.typeSelector) {
                return true;
            }
        }
        return false;
    }

    static int specificity_(const Rule& rule, const std::vector<SwString>& selectors) {
        int spec = 0;
        if (!rule.objectName.isEmpty()) spec += 10000;
        if (!rule.typeSelector.isEmpty() && rule.typeSelector != "*") {
            int rank = 0;
            const SwString canonicalRuleType = canonicalSelector_(rule.typeSelector);
            for (size_t i = 0; i < selectors.size(); ++i) {
                if (canonicalSelector_(selectors[i]) == canonicalRuleType) {
                    rank = static_cast<int>(selectors.size() - i);
                    break;
                }
            }
            spec += 100 + rank;
        }
        unsigned int mask = rule.stateMask;
        while (mask != 0U) {
            spec += 10;
            mask &= (mask - 1U);
        }
        return spec;
    }

    static ParsedSelector_ parseSelector_(const std::string& text) {
        ParsedSelector_ out;
        std::string selector = trim_(text);
        if (selector.empty()) {
            return out;
        }
        while (true) {
            const size_t colon = selector.rfind(':');
            if (colon == std::string::npos) {
                break;
            }
            const std::string suffix = lower_(trim_(selector.substr(colon + 1)));
            unsigned int flag = StateNone;
            if (suffix == "hover") flag = StateHovered;
            else if (suffix == "pressed") flag = StatePressed;
            else if (suffix == "disabled") flag = StateDisabled;
            else if (suffix == "focus") flag = StateFocused;
            else if (suffix == "checked") flag = StateChecked;
            else break;
            out.stateMask |= flag;
            selector = trim_(selector.substr(0, colon));
        }
        std::string typeSelector = selector;
        std::string objectName;
        const size_t hash = selector.find('#');
        if (hash != std::string::npos) {
            typeSelector = trim_(selector.substr(0, hash));
            objectName = trim_(selector.substr(hash + 1));
        }
        out.selector = SwString(trim_(selector));
        out.typeSelector = SwString(trim_(typeSelector));
        out.objectName = SwString(objectName);
        return out;
    }

    static std::vector<std::pair<std::string, std::string>> parseDecls_(const std::string& block) {
        std::vector<std::pair<std::string, std::string>> out;
        std::string current;
        int parenDepth = 0;
        bool single = false;
        bool dbl = false;
        for (char ch : block) {
            if (ch == '\'' && !dbl) single = !single;
            else if (ch == '"' && !single) dbl = !dbl;
            else if (!single && !dbl) {
                if (ch == '(') ++parenDepth;
                else if (ch == ')' && parenDepth > 0) --parenDepth;
            }
            if (ch == ';' && parenDepth == 0 && !single && !dbl) {
                appendDeclChunk_(out, current);
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        appendDeclChunk_(out, current);
        return out;
    }

    static void appendDeclChunk_(std::vector<std::pair<std::string, std::string>>& out, const std::string& chunk) {
        const std::string trimmed = trim_(chunk);
        if (trimmed.empty()) {
            return;
        }
        const size_t colon = findColon_(trimmed);
        if (colon == std::string::npos) {
            return;
        }
        out.push_back(std::make_pair(trimmed.substr(0, colon), trimmed.substr(colon + 1)));
    }

    static size_t findColon_(const std::string& text) {
        int parenDepth = 0;
        bool single = false;
        bool dbl = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if (ch == '\'' && !dbl) single = !single;
            else if (ch == '"' && !single) dbl = !dbl;
            else if (!single && !dbl) {
                if (ch == '(') ++parenDepth;
                else if (ch == ')' && parenDepth > 0) --parenDepth;
                else if (ch == ':' && parenDepth == 0) return i;
            }
        }
        return std::string::npos;
    }

    static std::vector<std::string> splitSelectors_(const std::string& text) {
        std::vector<std::string> out;
        std::string current;
        bool single = false;
        bool dbl = false;
        int parenDepth = 0;
        for (char ch : text) {
            if (ch == '\'' && !dbl) single = !single;
            else if (ch == '"' && !single) dbl = !dbl;
            else if (!single && !dbl) {
                if (ch == '(') ++parenDepth;
                else if (ch == ')' && parenDepth > 0) --parenDepth;
            }
            if (ch == ',' && parenDepth == 0 && !single && !dbl) {
                const std::string trimmed = trim_(current);
                if (!trimmed.empty()) out.push_back(trimmed);
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        const std::string trimmed = trim_(current);
        if (!trimmed.empty()) out.push_back(trimmed);
        return out;
    }

    static std::vector<std::string> splitSpace_(const std::string& text) {
        std::vector<std::string> out;
        std::string current;
        bool single = false;
        bool dbl = false;
        int parenDepth = 0;
        for (char ch : text) {
            if (ch == '\'' && !dbl) single = !single;
            else if (ch == '"' && !single) dbl = !dbl;
            else if (!single && !dbl) {
                if (ch == '(') ++parenDepth;
                else if (ch == ')' && parenDepth > 0) --parenDepth;
            }
            if (std::isspace(static_cast<unsigned char>(ch)) && parenDepth == 0 && !single && !dbl) {
                const std::string trimmed = trim_(current);
                if (!trimmed.empty()) out.push_back(trimmed);
                current.clear();
                continue;
            }
            current.push_back(ch);
        }
        const std::string trimmed = trim_(current);
        if (!trimmed.empty()) out.push_back(trimmed);
        return out;
    }

    static std::vector<SwString> expandSelectors_(const SwList<SwString>& selectors) {
        std::vector<SwString> out;
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            addUnique_(out, selector);
            const SwString alias = aliasSelector_(selector);
            if (alias != selector) {
                addUnique_(out, alias);
            }
        }
        return out;
    }

    static void addUnique_(std::vector<SwString>& values, const SwString& value) {
        for (const SwString& existing : values) {
            if (existing == value) {
                return;
            }
        }
        values.push_back(value);
    }

    static bool legacyStateAlias_(std::string& property, unsigned int& stateMask) {
        static const struct { const char* suffix; unsigned int flag; } suffixes[] = {
            {"-hover", StateHovered},
            {"-pressed", StatePressed},
            {"-checked", StateChecked},
            {"-disabled", StateDisabled},
            {"-focus", StateFocused}
        };
        for (const auto& it : suffixes) {
            const std::string suffix(it.suffix);
            if (property.size() > suffix.size() &&
                property.compare(property.size() - suffix.size(), suffix.size(), suffix) == 0) {
                property = property.substr(0, property.size() - suffix.size());
                stateMask = it.flag;
                return true;
            }
        }
        static const struct { const char* prefix; unsigned int flag; } prefixes[] = {
            {"hover-", StateHovered},
            {"pressed-", StatePressed},
            {"checked-", StateChecked},
            {"disabled-", StateDisabled},
            {"focus-", StateFocused}
        };
        for (const auto& it : prefixes) {
            const std::string prefix(it.prefix);
            if (property.find(prefix) == 0 && property.size() > prefix.size()) {
                property = property.substr(prefix.size());
                stateMask = it.flag;
                return true;
            }
        }
        return false;
    }

    static void mergeProps_(Rule& target, const Rule& source) {
        for (const auto& prop : source.properties) {
            target.properties[prop.first] = prop.second;
        }
    }

    static SwString aliasSelector_(const SwString& selector) {
        std::string raw = selector.toStdString();
        if (raw.size() > 2 && raw[0] == 'S' && raw[1] == 'w') {
            raw[0] = 'Q';
            raw.erase(raw.begin() + 1);
            return SwString(raw);
        }
        if (!raw.empty() && raw[0] == 'Q') {
            raw[0] = 'S';
            raw.insert(raw.begin() + 1, 'w');
            return SwString(raw);
        }
        return selector;
    }

    static SwString canonicalSelector_(const SwString& selector) {
        std::string raw = selector.toStdString();
        if (!raw.empty() && raw[0] == 'Q') {
            raw[0] = 'S';
            raw.insert(raw.begin() + 1, 'w');
            return SwString(raw);
        }
        return selector;
    }

    static bool looksLikeLength_(const std::string& token) {
        if (token.empty()) {
            return false;
        }
        const char first = token[0];
        return std::isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '+';
    }

    static std::string stripComments_(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        bool block = false;
        bool line = false;
        bool single = false;
        bool dbl = false;
        for (size_t i = 0; i < input.size(); ++i) {
            const char ch = input[i];
            const char next = (i + 1 < input.size()) ? input[i + 1] : '\0';
            if (block) {
                if (ch == '*' && next == '/') {
                    block = false;
                    ++i;
                }
                continue;
            }
            if (line) {
                if (ch == '\n') {
                    line = false;
                    out.push_back(ch);
                }
                continue;
            }
            if (!single && !dbl) {
                if (ch == '/' && next == '*') {
                    block = true;
                    ++i;
                    continue;
                }
                if (ch == '/' && next == '/') {
                    line = true;
                    ++i;
                    continue;
                }
            }
            if (ch == '\'' && !dbl) single = !single;
            else if (ch == '"' && !single) dbl = !dbl;
            out.push_back(ch);
        }
        return out;
    }

    static std::string trim_(const std::string& value) {
        const size_t start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        const size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    }

    static std::string lower_(std::string value) {
        for (char& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    static SwColor makeColor(int r, int g, int b) {
        SwColor c;
        c.r = r;
        c.g = g;
        c.b = b;
        return c;
    }

    SwMap<SwString, SwColor> colorNames;
    std::vector<Rule> rules_;
    int nextOrder_{0};
};
