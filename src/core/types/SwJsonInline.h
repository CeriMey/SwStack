/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 *
 * Licensed under the Apache License, Version 2.0.
 ***************************************************************************************************/

#pragma once

#include "SwJsonArray.h"
#include "SwJsonObject.h"

namespace swJsonDetail {

inline SwString indentation(int indentLevel)
{
    return indentLevel > 0 ? SwString(static_cast<size_t>(indentLevel) * 2, ' ') : SwString();
}

inline void appendJsonValue(const SwJsonValue& value, SwString& out, bool compact, int indentLevel)
{
    if (value.isString()) {
        out += SwString("\"") + SwJsonValue::escapeString(value.toString()) + "\"";
        return;
    }
    if (value.isBool()) {
        out += value.toBool() ? "true" : "false";
        return;
    }
    if (value.isInt()) {
        out += SwString::number(value.toLongLong());
        return;
    }
    if (value.isDouble()) {
        out += SwString::number(value.toDouble());
        return;
    }
    if (value.isObject()) {
        auto object = value.toObjectPtr();
        out += object ? object->toJsonString(compact, indentLevel) : "{}";
        return;
    }
    if (value.isArray()) {
        auto array = value.toArrayPtr();
        out += array ? array->toJsonString(compact, indentLevel) : "[]";
        return;
    }
    out += "null";
}

} // namespace swJsonDetail

inline SwJsonObject SwJsonValue::toObject() const
{
    if (type_ == Type::Object && objectValue_) return *objectValue_;
    return SwJsonObject();
}

inline SwJsonArray SwJsonValue::toArray() const
{
    if (type_ == Type::Array && arrayValue_) return *arrayValue_;
    return SwJsonArray();
}

inline SwString SwJsonValue::toJsonString(bool compact, int indentLevel) const
{
    SwString out;
    swJsonDetail::appendJsonValue(*this, out, compact, indentLevel);
    return out;
}

inline bool SwJsonValue::operator==(const SwJsonValue& other) const
{
    if (type_ != other.type_) return false;
    switch (type_) {
    case Type::Null:
        return true;
    case Type::Boolean:
        return boolValue_ == other.boolValue_;
    case Type::Integer:
        return intValue_ == other.intValue_;
    case Type::Double:
        return doubleValue_ == other.doubleValue_;
    case Type::String:
        return stringValue_ == other.stringValue_;
    case Type::Object:
        if (!objectValue_ || !other.objectValue_) return objectValue_ == other.objectValue_;
        return *objectValue_ == *other.objectValue_;
    case Type::Array:
        if (!arrayValue_ || !other.arrayValue_) return arrayValue_ == other.arrayValue_;
        return *arrayValue_ == *other.arrayValue_;
    }
    return false;
}

inline SwString SwJsonObject::toJsonString(bool compact, int indentLevel) const
{
    SwString out;
    const SwString indent = swJsonDetail::indentation(indentLevel);
    const SwString childIndent = swJsonDetail::indentation(indentLevel + 1);

    out += "{";

    bool first = true;
    for (const auto& pair : data_) {
        if (!first) out += compact ? "," : ",\n";
        first = false;

        if (!compact) out += SwString("\n") + childIndent;

        out += SwString("\"") + SwJsonValue::escapeString(pair.first) + "\":";
        if (!compact) out += " ";
        swJsonDetail::appendJsonValue(pair.second, out, compact, indentLevel + 1);
    }

    if (!compact && !data_.empty()) out += SwString("\n") + indent;
    out += "}";
    return out;
}

inline SwString SwJsonArray::toJsonString(bool compact, int indentLevel) const
{
    SwString out;
    const SwString indent = swJsonDetail::indentation(indentLevel);
    const SwString childIndent = swJsonDetail::indentation(indentLevel + 1);

    out += compact ? "[" : "[\n";
    for (size_t i = 0; i < data_.size(); ++i) {
        if (i > 0) out += compact ? "," : ",\n";
        if (!compact) out += childIndent;
        swJsonDetail::appendJsonValue(data_[i], out, compact, indentLevel + 1);
    }

    if (!compact && !data_.empty()) out += SwString("\n") + indent;
    out += "]";
    return out;
}
