#pragma once
#include <core/types/SwJsonObject.h>
#include <core/types/SwJsonArray.h>
#include <algorithm>
#include <cfenv>
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#include <charconv>
#endif
#include <cmath>
#include <limits>
#include <stdexcept>

// Exact compact SwJson size, without materializing an encoded object/tree.
class SwJsonSize {
public:
    explicit SwJsonSize(std::size_t limit, unsigned maxDepth = 64) : limit_(limit), maxDepth_(maxDepth) {}
    std::size_t object(const SwJsonObject& input, unsigned depth = 0) {
        if (depth > maxDepth_) throw std::runtime_error("JSON nesting exceeds depth limit");
        add(2);
        bool first = true;
        for (const auto& field : input.dataRef()) {
            if (!first) add(1);
            first = false;
            string(field.first); add(1); value(field.second, depth + 1);
        }
        return size_;
    }
    // Encoded member without braces or a comma, at root-object depth.
    std::size_t member(const SwString& name, const SwJsonValue& input) {
        string(name); add(1); value(input, 1); return size_;
    }
private:
    void add(std::size_t bytes) {
        if (bytes > limit_ - size_) throw std::runtime_error("JSON exceeds size limit");
        size_ += bytes;
    }
    void string(const SwString& text) {
        add(2);
        for (const unsigned char character : text.toStdString()) {
            add(character == '"' || character == '\\' || character == '\b' || character == '\f' ||
                character == '\n' || character == '\r' || character == '\t' ? 2 : (character < 32 ? 6 : 1));
        }
    }
    void number(const SwJsonValue& input) {
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
        const bool integer = input.type() == SwJsonValue::Type::Integer;
        // SwJson's ostream respects directed rounding; to_chars always rounds
        // to nearest. Keep the writer's exact length in those uncommon modes.
        if (!integer && std::fegetround() != FE_TONEAREST) { add(input.toJsonString().size()); return; }
        char buffer[64];
        const auto result = integer
            ? std::to_chars(buffer, buffer + sizeof(buffer), input.toLongLong())
            : std::to_chars(buffer, buffer + sizeof(buffer), input.toDouble(),
                            std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) throw std::runtime_error("JSON number size overflow");
        auto bytes = static_cast<std::size_t>(result.ptr - buffer);
        if (!integer && std::find(buffer, result.ptr, '.') == result.ptr &&
            std::find(buffer, result.ptr, 'e') == result.ptr) bytes += 2; // SwJson's .0
        add(bytes);
#else
        // EmbeddedDb/TableDb also support the framework's C++11 baseline.
        // Only the scalar is encoded; the object/tree remains unmaterialized.
        add(input.toJsonString().size());
#endif
    }
    void value(const SwJsonValue& input, unsigned depth) {
        if (depth > maxDepth_) throw std::runtime_error("JSON nesting exceeds depth limit");
        if (input.isObject()) {
            const auto fields = input.toObjectPtr();
            if (fields) object(*fields, depth); else add(2);
        } else if (input.isArray()) {
            add(2);
            const auto items = input.toArrayPtr();
            if (items) {
                bool first = true;
                for (const auto& item : items->dataRef()) {
                    if (!first) add(1);
                    first = false; value(item, depth + 1);
                }
            }
        } else if (input.isString()) string(input.stringRef());
        else if (input.isDouble()) {
            if (!std::isfinite(input.toDouble())) throw std::runtime_error("non-finite JSON number");
            number(input);
        } else add(input.isBool() ? (input.toBool() ? 4 : 5) : 4);
    }
    const std::size_t limit_;
    const unsigned maxDepth_;
    std::size_t size_{0};
};
