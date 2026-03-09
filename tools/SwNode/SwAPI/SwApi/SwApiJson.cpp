#include "SwApiJson.h"

static SwString normalizedPath(SwString p) {
    p.replace("\\", "/");
    while (p.contains("//")) p.replace("//", "/");
    while (p.startsWith("/")) p = p.mid(1);
    while (p.endsWith("/")) p = p.left(static_cast<int>(p.size()) - 1);
    return p;
}

SwString SwApiJson::toJson(const SwJsonObject& o, bool pretty) {
    SwJsonDocument d;
    d.setObject(o);
    return d.toJson(pretty ? SwJsonDocument::JsonFormat::Pretty : SwJsonDocument::JsonFormat::Compact);
}

SwString SwApiJson::toJson(const SwJsonArray& a, bool pretty) {
    SwJsonDocument d;
    d.setArray(a);
    return d.toJson(pretty ? SwJsonDocument::JsonFormat::Pretty : SwJsonDocument::JsonFormat::Compact);
}

SwString SwApiJson::toJson(const SwJsonValue& v, bool pretty) {
    if (v.isObject()) return toJson(v.toObject(), pretty);
    if (v.isArray()) return toJson(v.toArray(), pretty);
    return SwString(v.toJsonString());
}

bool SwApiJson::parse(const SwString& json, SwJsonDocument& outDoc, SwString& err) {
    err.clear();
    outDoc = SwJsonDocument();
    return outDoc.loadFromJson(json.toStdString(), err);
}

bool SwApiJson::parseObject(const SwString& json, SwJsonObject& out, SwString& err) {
    err.clear();
    out = SwJsonObject();
    SwJsonDocument d;
    if (!d.loadFromJson(json.toStdString(), err)) return false;
    if (!d.isObject()) {
        err = "json root is not an object";
        return false;
    }
    out = d.object();
    return true;
}

bool SwApiJson::parseArray(const SwString& json, SwJsonArray& out, SwString& err) {
    err.clear();
    out = SwJsonArray();
    SwJsonDocument d;
    if (!d.loadFromJson(json.toStdString(), err)) return false;
    if (!d.isArray()) {
        err = "json root is not an array";
        return false;
    }
    out = d.array();
    return true;
}

static bool tryParseIndex(const SwString& s, size_t& out) {
    if (s.isEmpty()) return false;
    size_t x = 0;
    const size_t max = static_cast<size_t>(-1);

    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        const size_t digit = static_cast<size_t>(c - '0');
        if (x > (max - digit) / 10) return false;
        x = x * 10 + digit;
    }
    out = x;
    return true;
}

bool SwApiJson::tryGetPath(const SwJsonValue& root, const SwString& rawPath, SwJsonValue& out, SwString& err) {
    err.clear();
    out = SwJsonValue();

    const SwString p = normalizedPath(rawPath.trimmed());
    if (p.isEmpty()) {
        out = root;
        return true;
    }

    SwList<SwString> toks = p.split('/');
    SwJsonValue cur = root;

    for (size_t i = 0; i < toks.size(); ++i) {
        const SwString token = toks[i];
        if (token.isEmpty()) continue;

        if (cur.isObject()) {
            const SwJsonObject obj(cur.toObject());
            if (!obj.contains(token)) {
                err = SwString("path not found: missing key '") + token + "'";
                return false;
            }
            cur = obj[token];
            continue;
        }

        if (cur.isArray()) {
            size_t idx = 0;
            if (!tryParseIndex(token, idx)) {
                err = SwString("path not found: expected array index, got '") + token + "'";
                return false;
            }
            const SwJsonArray arr(cur.toArray());
            if (idx >= arr.size()) {
                err = SwString("path not found: array index out of range: ") + token;
                return false;
            }
            cur = arr[idx];
            continue;
        }

        err = SwString("path not found: leaf is not an object/array at '") + token + "'";
        return false;
    }

    out = cur;
    return true;
}
