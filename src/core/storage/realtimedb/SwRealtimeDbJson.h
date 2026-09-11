#pragma once
#include <core/types/SwJsonDocument.h>
#include <stdexcept>
namespace swRealtimeDbDetail {
inline SwString json(const SwJsonObject& object) {
    return SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact);
}
inline SwJsonObject parseObject(const SwString& text) {
    if (text.size() > 2 * 1024 * 1024) throw std::invalid_argument("JSON exceeds 2 MiB");
    SwJsonDocument document;
    SwString error;
    if (!document.loadFromJson(text, error) || !document.isObject())
        throw std::invalid_argument("Expected a JSON object");
    return document.object();
}
inline SwJsonObject operation(const char* op) {
    SwJsonObject request;
    request["op"] = SwJsonValue(op);
    return request;
}
} // namespace swRealtimeDbDetail
