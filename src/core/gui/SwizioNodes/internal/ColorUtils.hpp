#pragma once

#include "core/types/SwString.h"
#include "core/types/Sw.h"
#include "core/types/SwJsonValue.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace SwizioNodes {

inline int clamp255_(int v) { return std::max(0, std::min(255, v)); }

inline SwColor makeColor_(int r, int g, int b) { return SwColor{clamp255_(r), clamp255_(g), clamp255_(b)}; }

inline SwColor parseNamedColor_(SwString name, SwColor fallback)
{
    std::string n = name.trimmed().toLower().toStdString();
    if (n.empty()) {
        return fallback;
    }

    static const std::unordered_map<std::string, SwColor> kNamed = {
        {"black", makeColor_(0, 0, 0)},
        {"white", makeColor_(255, 255, 255)},
        {"gray", makeColor_(128, 128, 128)},
        {"grey", makeColor_(128, 128, 128)},
        {"darkgray", makeColor_(169, 169, 169)},
        {"darkgrey", makeColor_(169, 169, 169)},
        {"lightgray", makeColor_(211, 211, 211)},
        {"lightgrey", makeColor_(211, 211, 211)},
        {"red", makeColor_(255, 0, 0)},
        {"green", makeColor_(0, 128, 0)},
        {"blue", makeColor_(0, 0, 255)},
        {"cyan", makeColor_(0, 255, 255)},
        {"magenta", makeColor_(255, 0, 255)},
        {"yellow", makeColor_(255, 255, 0)},
        {"orange", makeColor_(255, 165, 0)},
        {"darkcyan", makeColor_(0, 139, 139)},
        {"lightcyan", makeColor_(224, 255, 255)},
    };

    auto it = kNamed.find(n);
    if (it != kNamed.end()) {
        return it->second;
    }
    return fallback;
}

inline bool parseHexColor_(const std::string& s, SwColor* out)
{
    if (!out) {
        return false;
    }
    if (s.size() != 7 || s[0] != '#') {
        return false;
    }
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    int r1 = hex(s[1]), r2 = hex(s[2]);
    int g1 = hex(s[3]), g2 = hex(s[4]);
    int b1 = hex(s[5]), b2 = hex(s[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
        return false;
    }
    *out = makeColor_((r1 << 4) | r2, (g1 << 4) | g2, (b1 << 4) | b2);
    return true;
}

inline SwColor parseColorValue_(const SwJsonValue& v, SwColor fallback)
{
    if (v.isArray() && v.toArray()) {
        const SwJsonArray& arr = *v.toArray();
        if (arr.size() >= 3) {
            int r = static_cast<int>(arr[0].toInt());
            int g = static_cast<int>(arr[1].toInt());
            int b = static_cast<int>(arr[2].toInt());
            return makeColor_(r, g, b);
        }
        return fallback;
    }
    if (v.isString()) {
        const std::string s = v.toString();
        SwColor c{};
        if (parseHexColor_(s, &c)) {
            return c;
        }
        return parseNamedColor_(SwString(s), fallback);
    }
    if (v.isObject()) {
        return fallback;
    }
    if (v.isDouble() || v.isInt()) {
        // Single number is not a valid color in our JSON schema.
        return fallback;
    }
    return fallback;
}

inline std::string toHex_(SwColor c)
{
    auto nib = [](int v) -> char {
        v = clamp255_(v);
        const int n = v & 0xF;
        return static_cast<char>(n < 10 ? ('0' + n) : ('A' + (n - 10)));
    };

    auto byteToHex = [&](int v) -> std::string {
        v = clamp255_(v);
        std::string out(2, '0');
        out[0] = nib(v >> 4);
        out[1] = nib(v);
        return out;
    };

    return std::string("#") + byteToHex(c.r) + byteToHex(c.g) + byteToHex(c.b);
}

} // namespace SwizioNodes

