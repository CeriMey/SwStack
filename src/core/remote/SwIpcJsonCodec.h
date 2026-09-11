#pragma once
#include "SwSharedMemorySignal.h"
#include "SwJsonDocument.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace sw { namespace ipc { namespace jsonwire {
// Decimal strings preserve every bit of 64-bit integers through JSON clients.
// Reject overflow, fractional integers and trailing text instead of truncating.
template<class T> bool encodeInteger(detail::Encoder& enc, const SwJsonValue& value) {
    T parsed{};
    if (value.isString()) {
        const std::string text = value.toString().toStdString();
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return false;
    } else if (value.isInt()) {
        const auto number = value.toLongLong();
        if constexpr (std::is_signed<T>::value) {
            if (number < std::numeric_limits<T>::min() || number > std::numeric_limits<T>::max()) return false;
        } else {
            if (number < 0 || static_cast<uint64_t>(number) > std::numeric_limits<T>::max()) return false;
        }
        parsed = static_cast<T>(number);
    } else if (value.isDouble()) {
        const double number = value.toDouble();
        // Above 2^53 a JSON double cannot distinguish neighbouring integers.
        if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > 9007199254740991.0 ||
            static_cast<long double>(number) < std::numeric_limits<T>::min() ||
            static_cast<long double>(number) > std::numeric_limits<T>::max()) return false;
        parsed = static_cast<T>(number);
    } else return false;
    return SwAny::serializeBinary(enc, parsed);
}
template<class T> bool decodeInteger(detail::Decoder& dec, SwJsonValue& out) {
    T value{};
    if (!SwAny::deserializeBinary(dec, value)) return false;
    if constexpr (sizeof(T) == 8) out = SwJsonValue(std::to_string(value));
    else out = SwJsonValue(static_cast<long long>(value));
    return true;
}
template<class T> bool encodeFloat(detail::Encoder& enc, const SwJsonValue& value) {
    double number{};
    if (value.isDouble()) number = value.toDouble();
    else if (value.isString()) {
        const auto text = value.toString().toStdString();
        try { size_t used = 0; number = std::stod(text, &used); if (used != text.size()) return false; }
        catch (...) { return false; }
    } else return false;
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<T>::max()) return false;
    return SwAny::serializeBinary(enc, static_cast<T>(number));
}
template<class T> bool decodeFloat(detail::Decoder& dec, SwJsonValue& out) {
    T value{};
    if (!SwAny::deserializeBinary(dec, value) || !std::isfinite(value)) return false;
    out = SwJsonValue(static_cast<double>(value)); return true;
}
inline bool encode(detail::Encoder& enc, const std::string& type, const SwJsonValue& value, SwString& error) {
    error = SwString("rpc: invalid value or insufficient payload space for ") + type;
    bool ok = false;
#define SW_JSON_INTEGER(T) if (type == #T) ok = encodeInteger<T>(enc, value); else
    SW_JSON_INTEGER(int8_t) SW_JSON_INTEGER(uint8_t)
    SW_JSON_INTEGER(int16_t) SW_JSON_INTEGER(uint16_t)
    SW_JSON_INTEGER(int32_t) SW_JSON_INTEGER(uint32_t)
    SW_JSON_INTEGER(int64_t) SW_JSON_INTEGER(uint64_t)
#undef SW_JSON_INTEGER
    if (type == "int") ok = encodeInteger<int>(enc, value);
    else if (type == "float") ok = encodeFloat<float>(enc, value);
    else if (type == "double") ok = encodeFloat<double>(enc, value);
    else if (type == "bool" && value.isBool()) ok = SwAny::serializeBinary(enc, value.toBool());
    else if (type == "SwString" && value.isString()) ok = SwAny::serializeBinary(enc, value.toString());
    else if (type == "SwByteArray" && value.isString()) ok = SwAny::serializeBinary(enc, SwByteArray(value.toString().toStdString()));
    if (ok) error.clear();
    return ok;
}
inline bool decode(detail::Decoder& dec, const std::string& type, SwJsonValue& out, SwString& error) {
    error = SwString("rpc: invalid payload or unsupported type ") + type;
    bool ok = false;
#define SW_JSON_INTEGER(T) if (type == #T) ok = decodeInteger<T>(dec, out); else
    SW_JSON_INTEGER(int8_t) SW_JSON_INTEGER(uint8_t)
    SW_JSON_INTEGER(int16_t) SW_JSON_INTEGER(uint16_t)
    SW_JSON_INTEGER(int32_t) SW_JSON_INTEGER(uint32_t)
    SW_JSON_INTEGER(int64_t) SW_JSON_INTEGER(uint64_t)
#undef SW_JSON_INTEGER
    if (type == "int") ok = decodeInteger<int>(dec, out);
    else if (type == "float") ok = decodeFloat<float>(dec, out);
    else if (type == "double") ok = decodeFloat<double>(dec, out);
    else if (type == "bool") { bool v{}; ok = SwAny::deserializeBinary(dec, v); if (ok) out = SwJsonValue(v); }
    else if (type == "SwString") { SwString v; ok = SwAny::deserializeBinary(dec, v); if (ok) out = SwJsonValue(v); }
    else if (type == "SwByteArray") { SwByteArray v; ok = SwAny::deserializeBinary(dec, v); if (ok) out = SwJsonValue(SwString(v.constData(), v.size())); }
    if (ok) error.clear();
    return ok;
}
}}}
