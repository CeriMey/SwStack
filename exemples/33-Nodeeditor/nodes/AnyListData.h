#pragma once

#include "SwizioNodes/NodeData"

#include "core/types/SwAny.h"

#include <cstddef>
#include <utility>

namespace swnodeeditor {

class AnyListData final : public SwizioNodes::NodeData {
public:
    AnyListData() = default;
    explicit AnyListData(const SwAnyList& values)
        : m_values(values)
    {
    }
    explicit AnyListData(SwAnyList&& values)
        : m_values(std::move(values))
    {
    }

    static SwizioNodes::NodeDataType Type()
    {
        SwizioNodes::NodeDataType t;
        t.id = "anylist";
        t.name = "AnyList";
        return t;
    }

    SwizioNodes::NodeDataType type() const override { return Type(); }

    const SwAnyList& values() const { return m_values; }
    SwAnyList& values() { return m_values; }

    template <typename T>
    bool tryGet(std::size_t index, T* out) const
    {
        if (!out || index >= m_values.size()) {
            return false;
        }
        const SwAny& any = m_values[index];
        if (any.typeName().empty()) {
            return false;
        }
        try {
            *out = any.get<T>();
            return true;
        } catch (...) {
            return false;
        }
    }

    template <typename T>
    T getOr(std::size_t index, T defaultValue) const
    {
        T out{};
        return tryGet<T>(index, &out) ? out : std::move(defaultValue);
    }

    template <typename T>
    void set(std::size_t index, T&& value)
    {
        if (index >= m_values.size()) {
            while (m_values.size() <= index) {
                m_values.push_back(SwAny());
            }
        }
        m_values[index] = SwAny(std::forward<T>(value));
    }

    struct NumberPayload {
        double value{0.0};
        bool valid{false};
    };

    static constexpr std::size_t NumberValueIndex = 0;
    static constexpr std::size_t NumberValidIndex = 1;

    static SwAnyList makeNumber(double value, bool valid)
    {
        return SwAnyList{SwAny(value), SwAny(valid)};
    }

    static NumberPayload readNumber(const SwAnyList& list)
    {
        NumberPayload out;
        if (list.size() < 2u) {
            return out;
        }
        try {
            out.value = list[NumberValueIndex].get<double>();
        } catch (...) {
            out.value = 0.0;
        }
        try {
            out.valid = list[NumberValidIndex].get<bool>();
        } catch (...) {
            out.valid = false;
        }
        return out;
    }

    NumberPayload readNumber() const { return readNumber(m_values); }

private:
    SwAnyList m_values;
};

} // namespace swnodeeditor

