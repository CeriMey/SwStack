#include "StoreState.hpp"
#include "../JsonSize.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace swRealtimeDbDetail {

SwString requiredString(const SwJsonObject& object, const char* field, std::size_t maxLength) {
    const auto& value = object[field];
    if (!value.isString()) throw std::runtime_error(std::string("expected string: ") + field);
    const auto result = value.toString();
    if (result.isEmpty() || result.size() > maxLength)
        throw std::runtime_error(std::string("invalid string length: ") + field);
    return result;
}

void validateName(const SwString& name) {
    if (name.isEmpty() || name.size() > 128) throw std::runtime_error("invalid name length");
    for (const unsigned char c : name.toStdString()) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed) throw std::runtime_error("names allow only letters, digits, _, . and -");
    }
    if (name == "__proto__" || name == "prototype" || name == "constructor")
        throw std::runtime_error("reserved name");
}

SwString columnType(const SwJsonValue& descriptor) {
    const auto object = descriptor.toObjectPtr();
    return object ? static_cast<const SwJsonObject&>(*object)["type"].toString() : descriptor.toString();
}
bool columnNullable(const SwJsonValue& descriptor) {
    const auto object = descriptor.toObjectPtr();
    return object && static_cast<const SwJsonObject&>(*object)["nullable"].toBool();
}
SwJsonObject qualifiedIdentity(const SwString& name) {
    SwJsonObject result;
    const auto parts = name.split('.');
    if (parts.size() == 3) { result["namespace"] = parts[0]; result["schema"] = parts[1]; result["name"] = parts[2]; }
    else { result["namespace"] = ""; result["schema"] = ""; result["name"] = name; }
    return result;
}
void validateActor(const SwString& name) {
    if (name.isEmpty() || name.size() > 128) throw std::runtime_error("invalid actor length");
    std::size_t start = 0;
    const auto text = name.toStdString();
    while (start < text.size()) {
        const auto end = text.find('/', start);
        validateName(SwString(text.substr(start, end == std::string::npos ? end : end - start)));
        if (end == std::string::npos) return;
        start = end + 1;
    }
    throw std::runtime_error("actor namespace segments must be nonempty");
}

void validateJson(const SwJsonObject& object, unsigned depth) {
    if (depth > 32) throw std::runtime_error("JSON nesting exceeds 32 levels");
    for (const auto& entry : object.dataRef()) validateJson(entry.second, depth + 1);
}

void validateJson(const SwJsonValue& value, unsigned depth) {
    if (depth > 32) throw std::runtime_error("JSON nesting exceeds 32 levels");
    if (value.isDouble() && !std::isfinite(value.toDouble()))
        throw std::runtime_error("non-finite JSON number");
    if (value.isObject()) {
        const auto object = value.toObjectPtr();
        if (object) validateJson(*object, depth);
    } else if (value.isArray()) {
        const auto array = value.toArrayPtr();
        if (array) for (const auto& entry : array->dataRef()) validateJson(entry, depth + 1);
    }
}

SwString decimal(std::uint64_t value) { return SwString(std::to_string(value)); }

std::uint64_t parseRevision(const SwString& text) {
    if (text.isEmpty() || text.size() > 20) throw std::runtime_error("invalid revision");
    std::uint64_t result = 0;
    for (const char c : text.toStdString()) {
        if (c < '0' || c > '9' || result > (std::numeric_limits<std::uint64_t>::max() - (c - '0')) / 10)
            throw std::runtime_error("invalid revision");
        result = result * 10 + (c - '0');
    }
    return result;
}

SwString token() {
    std::random_device random;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) stream << std::setw(8) << static_cast<std::uint32_t>(random());
    return SwString(stream.str());
}

std::int64_t wallTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

SwJsonArray rowArray(Rows rows) {
    SwJsonArray result;
    for (auto& entry : rows) result.append(std::move(entry.second));
    return result;
}

} // namespace swRealtimeDbDetail

SwRealtimeDb::State::Table SwRealtimeDb::State::parseTable(const SwJsonObject& request, bool view,
    const SwString& owner, const SwString& ownerSession) const {
    Table result;
    result.name = swRealtimeDbDetail::requiredString(request, "table");
    result.owner = owner;
    result.ownerSession = ownerSession;
    result.maxRows = options.defaultMaxRows;
    result.overflowPolicy = options.defaultOverflowPolicy;
    result.key = swRealtimeDbDetail::requiredString(request, "key");
    result.view = view;
    swRealtimeDbDetail::validateName(result.name);
    swRealtimeDbDetail::validateName(result.key);
    if (!request["schema_version"].isInt() || request["schema_version"].toDouble() != 1)
        throw std::runtime_error("schema_version must be 1");
    if (!request["columns"].isObject()) throw std::runtime_error("columns must be an object");
    result.columns = request["columns"].toObject();
    result.metadata = request["metadata"].toObject();
    if (result.columns.isEmpty() || result.columns.size() > swRealtimeDbDetail::kMaxColumns)
        throw std::runtime_error("a table needs 1 to 64 columns");
    using Type = Table::ColumnType;
    const std::map<SwString, Type> types{{"string", Type::String}, {"bool", Type::Bool},
        {"number", Type::Number}, {"integer", Type::Integer}, {"object", Type::Object},
        {"array", Type::Array}, {"json", Type::Json}};
    for (auto it = result.columns.begin(); it != result.columns.end(); ++it) {
        // Column labels come from schemas: spaces, signs and units are legitimate.
        const auto label = it.key();
        if (label.empty() || label.size() > 128 || label == "__proto__" || label == "prototype" || label == "constructor")
            throw std::runtime_error("invalid or reserved column name");
        for (unsigned char c : label) if (c < 32) throw std::runtime_error("control character in column name");
        if (it.key() == "__sw_rtdb") throw std::runtime_error("reserved storage column");
        const auto type = swRealtimeDbDetail::columnType(it.value());
        if (!types.count(type))
            throw std::runtime_error("unsupported column type");
        result.validationColumns.push_back({label, types.at(type), swRealtimeDbDetail::columnNullable(it.value())});
        if (it.value().isObject()) {
            const auto column = it.value().toObject();
            if (column.contains("indexed") && !column["indexed"].isBool())
                throw std::runtime_error("column indexed must be a boolean");
            if (column["indexed"].toBool() && type != "integer" && type != "number" && type != "string")
                throw std::runtime_error("indexed columns require integer, number or string values");
        }
    }
    if (swRealtimeDbDetail::columnType(result.columns[result.key]) != "string" || swRealtimeDbDetail::columnNullable(result.columns[result.key]))
        throw std::runtime_error("primary key must name a string column");
    if (request.contains("max_rows")) {
        const auto value = request["max_rows"];
        if (!value.isInt() || value.toDouble() < 1 || value.toDouble() > swRealtimeDbDetail::kMaxRows)
            throw std::runtime_error("max_rows must be between 1 and 1024");
        result.maxRows = static_cast<std::size_t>(value.toInt());
    }
    if (request.contains("writers")) {
        if (view && !request.contains("encode")) throw std::runtime_error("read-only views cannot declare writers");
        if (!request["writers"].isArray() || request["writers"].toArray().size() > 64)
            throw std::runtime_error("writers must be an array of at most 64 actors");
        for (const auto& writer : request["writers"].toArray()) {
            if (!writer.isString()) throw std::runtime_error("writer must be an actor name");
            swRealtimeDbDetail::validateActor(writer.toString());
            result.writers.insert(writer.toString());
        }
    }
    if (request.contains("overflow_policy")) {
        result.overflowPolicy = swRealtimeDbDetail::requiredString(request, "overflow_policy");
        if (result.overflowPolicy != "reject" && result.overflowPolicy != "evict_oldest_write")
            throw std::runtime_error("overflow_policy must be reject or evict_oldest_write");
    }
    if (view) {
        result.script = swRealtimeDbDetail::requiredString(request,
            request.contains("decode") ? "decode" : "script", 64 * 1024);
        if (request.contains("decode") && request.contains("script") && request["script"].toString()!=result.script)
            throw std::runtime_error("script and decode must describe the same read transform");
        if (!request["dependencies"].isArray()) throw std::runtime_error("dependencies must be an array");
        const auto dependencies = request["dependencies"].toArray();
        if (dependencies.isEmpty() || dependencies.size() > swRealtimeDbDetail::kMaxTables)
            throw std::runtime_error("a view needs 1 to 256 dependencies");
        std::set<SwString> seen;
        for (const auto& dependency : dependencies) {
            if (!dependency.isString()) throw std::runtime_error("dependency must be a table name");
            const auto name = dependency.toString();
            swRealtimeDbDetail::validateName(name);
            if (!seen.insert(name).second) throw std::runtime_error("duplicate view dependency");
            result.dependencies.push_back(name);
        }
        if (request.contains("encode") || request.contains("write_target")) {
            result.encodeScript = swRealtimeDbDetail::requiredString(request, "encode", 64 * 1024);
            result.writeTarget = swRealtimeDbDetail::requiredString(request, "write_target");
            const auto mode=request.contains("write_mode") ? swRealtimeDbDetail::requiredString(request,"write_mode") : SwString("replace");
            if(mode!="replace" && mode!="merge") throw std::runtime_error("write_mode must be replace or merge");
            result.encodeMerge=mode=="merge";
            swRealtimeDbDetail::validateName(result.writeTarget);
            if (!seen.count(result.writeTarget)) throw std::runtime_error("write_target must be a view dependency");
            if (prepare) result.encodeProgram = prepare(result.encodeScript);
        }
        if (prepare) result.program = prepare(result.script);
        result.error = "awaiting dependencies";
    }
    if (request.contains("write_mode") && result.encodeScript.isEmpty())
        throw std::runtime_error("write_mode requires encode and write_target");
    if (!view && (request.contains("encode") || request.contains("decode") || request.contains("write_target")))
        throw std::runtime_error("encode, decode and write_target belong to views");
    if (request.contains("projection"))
        throw std::runtime_error("JSON calculation plans are unsupported; provide a JavaScript view script");
    result.bytes = tableBytes(result, {});
    return result;
}

swRealtimeDbDetail::Rows SwRealtimeDb::State::validateRows(const Table& table, const SwJsonArray& values,
                                                           SwJsonArray* orderedKeys) const {
    return validateRowsImpl(table, values, nullptr, orderedKeys);
}
swRealtimeDbDetail::Rows SwRealtimeDb::State::validateRowsOwned(const Table& table, SwJsonArray&& values,
                                                                SwJsonArray* orderedKeys) const {
    return validateRowsImpl(table, values, &values, orderedKeys);
}
swRealtimeDbDetail::Rows SwRealtimeDb::State::validateRowsImpl(const Table& table, const SwJsonArray& values,
                                                               SwJsonArray* owned, SwJsonArray* orderedKeys) const {
    if (values.size() > swRealtimeDbDetail::kMaxRows ||
        (table.overflowPolicy == "reject" && values.size() > table.maxRows))
        throw std::runtime_error("row limit exceeded");
    swRealtimeDbDetail::Rows result;
    for (const auto& entry : values) {
        if (!entry.isObject()) throw std::runtime_error("every row must be an object");
        const auto object = entry.toObjectPtr();
        if (!object) throw std::runtime_error("every row must be an object");
        const auto& row = static_cast<const SwJsonObject&>(*object);
        if (row.size() != table.columns.size()) throw std::runtime_error("row must contain exactly the declared columns");
        for (const auto& column : table.validationColumns) {
            const auto& value = row[column.name];
            bool match = value.isNull() && column.nullable;
            if (!match) switch (column.type) {
                case Table::ColumnType::String: match = value.isString(); break;
                case Table::ColumnType::Bool: match = value.isBool(); break;
                case Table::ColumnType::Number: match = value.isDouble(); break;
                case Table::ColumnType::Integer: match = value.isInt(); break;
                case Table::ColumnType::Object: match = value.isObject(); break;
                case Table::ColumnType::Array: match = value.isArray(); break;
                case Table::ColumnType::Json: match = true; break;
            }
            if (!match) throw std::runtime_error("row type mismatch for " + column.name.toStdString());
        }
        swRealtimeDbDetail::validateJson(entry);
        const auto key = row[table.key].toString();
        if (key.isEmpty() || key.size() > 128) throw std::runtime_error("row key length must be 1 to 128");
        if (result.count(key)) throw std::runtime_error("duplicate key within batch");
        if (orderedKeys) orderedKeys->append(key);
        if (owned) result.emplace(key, std::move(*object));
        else result.emplace(key, row);
    }
    return result;
}

std::size_t SwRealtimeDb::State::tableBytes(const Table& table, const swRealtimeDbDetail::Rows& rows) const {
    if (!table.descriptorBytes) {
        std::size_t descriptor = swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxDatabaseBytes).object(table.columns)
            + swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxDatabaseBytes).object(table.metadata)
            + table.script.size() + table.encodeScript.size() + table.writeTarget.size() + 512;
        descriptor += table.name.size() + table.owner.size() + table.key.size();
        for (const auto& dep : table.dependencies) descriptor += dep.size() + 4;
        for (const auto& writer : table.writers) descriptor += writer.size() + 4;
        table.descriptorBytes = descriptor;
    }
    std::size_t result = table.descriptorBytes;
    for (const auto& row : rows)
        result += swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxDatabaseBytes).object(row.second)
            + row.first.size() + 4;
    return result;
}

void SwRealtimeDb::State::checkCapacity(const Table& table, std::size_t replacementBytes) const {
    if (replacementBytes > swRealtimeDbDetail::kMaxBytes || bytes - table.bytes > swRealtimeDbDetail::kMaxDatabaseBytes - replacementBytes)
        throw std::runtime_error("RtDb aggregate data limit is 8 MiB");
}
