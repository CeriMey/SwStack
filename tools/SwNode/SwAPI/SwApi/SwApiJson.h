#pragma once

#include "SwJsonDocument.h"
#include "SwJsonValue.h"
#include "SwString.h"

class SwApiJson {
public:
    static SwString toJson(const SwJsonObject& o, bool pretty);
    static SwString toJson(const SwJsonArray& a, bool pretty);
    static SwString toJson(const SwJsonValue& v, bool pretty);

    static bool parse(const SwString& json, SwJsonDocument& outDoc, SwString& err);
    static bool parseObject(const SwString& json, SwJsonObject& out, SwString& err);
    static bool parseArray(const SwString& json, SwJsonArray& out, SwString& err);

    static bool tryGetPath(const SwJsonValue& root, const SwString& path, SwJsonValue& out, SwString& err);
};
