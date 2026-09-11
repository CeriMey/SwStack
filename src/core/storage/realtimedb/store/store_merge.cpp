#include "StoreState.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void mergeObject(SwJsonObject& target, const SwJsonObject& patch) {
    for (const auto& field : patch.dataRef()) {
        const auto incoming = field.second.toObjectPtr();
        const auto existing = target[field.first].toObjectPtr();
        if (incoming && existing) mergeObject(*existing, *incoming);
        else target[field.first] = field.second;
    }
}
struct Increment {
    SwString key;
    std::vector<SwString> path;
    SwJsonValue amount;
};
std::vector<Increment> incrementsFor(const SwJsonArray& values, const SwString& primaryKey) {
    if (values.size() > 64) throw std::runtime_error("at most 64 increments per write");
    std::vector<Increment> result;
    for (const auto& value : values) {
        const auto descriptor = value.toObjectPtr();
        if (!descriptor) throw std::runtime_error("increment must be an object");
        const auto& object = *descriptor;
        Increment increment;
        if (!object["key"].isString() || object["key"].toString().isEmpty() || object["key"].toString().size() > 128)
            throw std::runtime_error("increment key needs 1 to 128 bytes");
        increment.key = object["key"].toString();
        const auto path = object["path"].toArrayPtr();
        if (!path || path->isEmpty() || path->size() > 32)
            throw std::runtime_error("increment path needs 1 to 32 field names");
        for (const auto& segment : path->dataRef()) {
            if (!segment.isString() || segment.toString().isEmpty() || segment.toString().size() > 128)
                throw std::runtime_error("increment path fields need 1 to 128 bytes");
            increment.path.push_back(segment.toString());
        }
        if (increment.path.front() == primaryKey) throw std::runtime_error("cannot increment a primary key");
        increment.amount = object["amount"];
        if ((!increment.amount.isInt() && !increment.amount.isDouble()) ||
            !std::isfinite(increment.amount.toDouble())) throw std::runtime_error("increment amount must be finite numeric data");
        for (const auto& previous : result) {
            const auto common = std::min(previous.path.size(), increment.path.size());
            if (previous.key == increment.key && std::equal(previous.path.begin(), previous.path.begin() + common, increment.path.begin()))
                throw std::runtime_error("increment paths must not overlap within a row");
        }
        result.push_back(std::move(increment));
    }
    return result;
}
void incrementRow(SwJsonObject& row, const Increment& increment) {
    SwJsonObject* parent = &row;
    for (std::size_t index = 0; index + 1 < increment.path.size(); ++index) {
        const auto& segment = increment.path[index];
        if (!parent->contains(segment)) (*parent)[segment] = SwJsonObject();
        const auto next = (*parent)[segment].toObjectPtr();
        if (!next) throw std::runtime_error("increment path crosses a non-object value");
        parent = next.get();
    }
    const auto& name = increment.path.back();
    const auto previous = parent->contains(name) ? (*parent)[name] : SwJsonValue(0);
    if (!previous.isInt() && !previous.isDouble()) throw std::runtime_error("increment target must be numeric or absent");
    if (previous.type() == SwJsonValue::Type::Integer && increment.amount.type() == SwJsonValue::Type::Integer) {
        const auto current = previous.toLongLong(), amount = increment.amount.toLongLong();
        if ((amount > 0 && current > std::numeric_limits<long long>::max() - amount) ||
            (amount < 0 && current < std::numeric_limits<long long>::min() - amount))
            throw std::runtime_error("integer increment overflow");
        (*parent)[name] = static_cast<long long>(current + amount);
    } else {
        const auto value = previous.toDouble() + increment.amount.toDouble();
        if (!std::isfinite(value)) throw std::runtime_error("numeric increment overflow");
        (*parent)[name] = value;
    }
}
}

SwJsonArray SwRealtimeDb::State::mergeRows(const Table& target, const SwJsonArray& patches,
                                          const SwJsonArray& values, std::map<SwString, SwDbJsonRecord>& previous) const {
    if (patches.size() > swRealtimeDbDetail::kMaxRows ||
        (target.overflowPolicy == "reject" && patches.size() > target.maxRows)) throw std::runtime_error("row limit exceeded");
    const auto increments = incrementsFor(values, target.key);
    std::set<SwString> keys;
    for (const auto& patch : patches.dataRef()) {
        const auto object = patch.toObjectPtr();
        if (!object) throw std::runtime_error("every patch must be an object");
        const SwJsonObject& fields = *object;
        if (!fields[target.key].isString()) throw std::runtime_error("every patch needs its string primary key");
        const auto key = fields[target.key].toString();
        if (key.isEmpty() || key.size() > 128) throw std::runtime_error("row key length must be 1 to 128");
        if (!keys.insert(key).second) throw std::runtime_error("duplicate key within batch");
    }
    for (const auto& increment : increments) {
        if (!keys.count(increment.key)) throw std::runtime_error("increment key must occur in patched rows");
        if (!target.columns.contains(increment.path.front())) throw std::runtime_error("unknown increment column");
    }
    SwJsonArray rows;
    const auto& schema = storageSchema(target);
    for (const auto& value : patches.dataRef()) {
        const auto& patch = *value.toObjectPtr();
        const auto key = patch[target.key].toString();
        SwJsonObject row;
        SwDbJsonRecord record;
        const auto status = database.getRowRecord(schema, key, &record);
        if (status.ok()) {
            row = record.detach();
            row.remove("__sw_rtdb");
            previous.emplace(key, std::move(record));
        } else if (status.code() != SwDbStatus::NotFound)
            throw std::runtime_error("SwTableDb: " + status.message().toStdString());
        // Owner recovery can merge selected retained rows, exactly as an
        // explicit read + fresh publication did. Other stale rows are omitted.
        mergeObject(row, patch);
        for (const auto& increment : increments) if (increment.key == key) incrementRow(row, increment);
        for (const auto& column : target.validationColumns) {
            if (!row.contains(column.name) && column.nullable) row[column.name] = SwJsonValue();
            const auto& field = static_cast<const SwJsonObject&>(row)[column.name];
            if (column.type == Table::ColumnType::Integer && field.type() == SwJsonValue::Type::Double &&
                (field.toDouble() < -9223372036854775808.0 || field.toDouble() >= 9223372036854775808.0))
                throw std::runtime_error("integer increment or merge value is out of range");
        }
        rows.append(std::move(row));
    }
    return rows; // Full schema/depth/finite validation runs before any commit.
}
