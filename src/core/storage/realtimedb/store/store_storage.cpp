#include "StoreState.hpp"
#include <core/runtime/SwThreadPool.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

// This bridge is defined only in the store implementation. No public API can
// hand caller-owned mutable aliases to SwTableDb's consuming path.
namespace swTableDbDetail {
struct OwnedRowsAccess_ {
    static SwDbStatus write(SwTableDb& db, const SwTableDbPreparedSchema& schema,
                            SwJsonArray&& rows, bool replace, const SwList<SwString>& erased = {}) {
        return db.writeRowsOwned_(schema, std::move(rows), replace, erased);
    }
};
}

namespace {
constexpr const char* orderColumn = "__sw_rtdb";
void checked(const SwDbStatus& status) {
    if (!status.ok()) throw std::runtime_error("SwTableDb: " + status.message().toStdString());
}
SwTableColumn column(const SwString& name, const SwString& type) {
    SwTableColumn result;
    result.columnId = name; result.type = type;
    result.required = true; result.nullable = false;
    return result;
}
SwTableSchema catalogSchema() {
    SwTableSchema result;
    result.tableId = "rtdb.catalog";
    result.rowMode = SwTableRowMode::Exact;
    result.primaryKey = "table";
    result.columns.append(column("table", "string"));
    result.columns.append(column("owner", "string"));
    result.columns.append(column("definition", "json"));
    return result;
}
}

SwRealtimeDb::State::State(Transform evaluator, const SwRealtimeDbOptions& settings, PrepareView compiler)
    : options(settings), transform(std::move(evaluator)), prepare(std::move(compiler)) {
    if (!options.defaultMaxRows || options.defaultMaxRows > swRealtimeDbDetail::kMaxRows)
        throw std::invalid_argument("defaultMaxRows must be between 1 and 1024");
    if (options.defaultOverflowPolicy != "reject" && options.defaultOverflowPolicy != "evict_oldest_write")
        throw std::invalid_argument("unknown default overflow policy");
    if (options.storage.readOnly) throw std::invalid_argument("Realtime database needs a writable backend");
    if (options.viewWorkerCount>8) throw std::invalid_argument("viewWorkerCount must be between 0 and 8");
    if (options.viewWorkerCount) {
        viewWorkers=std::make_unique<SwThreadPool>();
        viewWorkers->setMaxThreadCount(static_cast<int>(options.viewWorkerCount));
    }
    database.setStorageDir(options.storage.dbPath);
    checked(database.open(options.storage));
    restore();
}
SwRealtimeDb::State::~State() = default;

const SwTableDbPreparedSchema& SwRealtimeDb::State::storageSchema(const Table& table) const {
    if (table.storage) return *table.storage;
    SwTableSchema result;
    result.tableId = "rtdb.data." + table.name;
    result.rowMode = SwTableRowMode::Exact;
    result.primaryKey = table.key;
    for (auto it = table.columns.begin(); it != table.columns.end(); ++it) {
        auto type = swRealtimeDbDetail::columnType(it.value());
        if (type == "bool") type = "boolean";
        else if (type == "object" || type == "array") type = "json";
        auto field = column(it.key(), type);
        field.nullable = swRealtimeDbDetail::columnNullable(it.value());
        result.columns.append(field);
        if (it.key() != table.key && it.value().isObject() && it.value().toObject()["indexed"].toBool()) {
            SwTableIndex index;
            index.indexId = it.key();
            index.columnId = it.key();
            index.prefix = false;
            result.indexes.append(index);
        }
    }
    result.columns.append(column(orderColumn, "string"));
    SwTableDbPreparedSchema prepared;
    checked(SwTableDb::prepareSchema(result, &prepared));
    table.storage = std::make_shared<const SwTableDbPreparedSchema>(std::move(prepared));
    return *table.storage;
}

swRealtimeDbDetail::Rows SwRealtimeDb::State::readRows(const Table& table) const {
    if (table.maxRows == 1 && table.writeOrder.size() <= 1) {
        swRealtimeDbDetail::Rows rows;
        if (table.writeOrder.empty()) return rows;
        // Retention already identifies the only stored key. A point read
        // returns its detached JSON directly, avoiding the iterator's second
        // deep copy and retaining no database snapshot across JS execution.
        SwJsonObject row;
        checked(database.getRow(storageSchema(table), table.writeOrder.begin()->first, &row));
        row.remove(orderColumn);
        rows.emplace(row[table.key].toString(), std::move(row));
        return rows;
    }
    SwJsonArray stored;
    checked(database.readRows(storageSchema(table), &stored));
    swRealtimeDbDetail::Rows rows;
    for (auto& value : stored) {
        auto row = std::move(*value.toObjectPtr());
        row.remove(orderColumn);
        rows.emplace(row[table.key].toString(), std::move(row));
    }
    return rows;
}

bool SwRealtimeDb::State::rowsEqual(const Table& table, const swRealtimeDbDetail::Rows& rows) const {
    if (table.writeOrder.size() != rows.size()) return false;
    const auto& schema = storageSchema(table);
    for (const auto& row : rows) {
        if (!table.writeOrder.count(row.first)) return false;
        SwDbJsonRecord previous;
        checked(database.getRowRecord(schema, row.first, &previous));
        if (!previous.equals(row.second, orderColumn)) return false;
    }
    return true;
}

void SwRealtimeDb::State::storeRows(const Table& table, swRealtimeDbDetail::Rows rows,
                                   const std::map<SwString, std::uint64_t>& order) {
    SwJsonArray stored;
    for (auto& entry : rows) {
        auto row = std::move(entry.second);
        row[orderColumn] = swRealtimeDbDetail::decimal(order.at(entry.first));
        stored.append(std::move(row));
    }
    checked(swTableDbDetail::OwnedRowsAccess_::write(database, storageSchema(table), std::move(stored), true));
}

SwJsonArray SwRealtimeDb::State::retainRows(const Table& table, swRealtimeDbDetail::Rows& rows,
                                          std::map<SwString, std::uint64_t>& order) const {
    const auto evicted = retainKeys(table, order);
    for (const auto& value : evicted) rows.erase(value.toString());
    return evicted;
}

void SwRealtimeDb::State::storeDelta(const Table& table, swRealtimeDbDetail::Rows rows,
                                    const std::map<SwString, std::uint64_t>& order,
                                    const std::set<SwString>& erased) {
    SwJsonArray stored;
    for (auto& entry : rows) {
        auto row = std::move(entry.second);
        row[orderColumn] = swRealtimeDbDetail::decimal(order.at(entry.first));
        stored.append(std::move(row));
    }
    SwList<SwString> keys;
    for (const auto& key : erased) keys.append(key);
    checked(swTableDbDetail::OwnedRowsAccess_::write(database, storageSchema(table), std::move(stored), false, keys));
}

SwJsonArray SwRealtimeDb::State::retainKeys(const Table& table,
                                          std::map<SwString, std::uint64_t>& order) const {
    SwJsonArray evicted;
    if (order.size() <= table.maxRows) return evicted;
    if (table.overflowPolicy == "reject") throw std::runtime_error("row limit exceeded");
    while (order.size() > table.maxRows) {
        const auto oldest = std::min_element(order.begin(), order.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        if (oldest == order.end()) throw std::logic_error("missing row write order");
        evicted.append(oldest->first);
        order.erase(oldest);
    }
    return evicted;
}

void SwRealtimeDb::State::persistDescriptor(const Table& table) {
    SwJsonObject definition;
    definition["table"] = table.name;
    definition["key"] = table.key;
    definition["columns"] = table.columns;
    definition["metadata"] = table.metadata;
    definition["schema_version"] = table.schemaVersion;
    definition["max_rows"] = static_cast<int>(table.maxRows);
    definition["overflow_policy"] = table.overflowPolicy;
    definition["view"] = table.view;
    if (table.view) {
        definition["script"] = table.script;
        if (!table.encodeScript.isEmpty()) {
            definition["encode"] = table.encodeScript;
            definition["write_target"] = table.writeTarget;
            definition["write_mode"] = table.encodeMerge ? "merge" : "replace";
        }
        SwJsonArray deps;
        for (const auto& name : table.dependencies) deps.append(name);
        definition["dependencies"] = deps;
    }
    if (!table.view || !table.encodeScript.isEmpty()) {
        SwJsonArray writers;
        for (const auto& actor : table.writers) writers.append(actor);
        definition["writers"] = writers;
    }
    SwJsonObject row;
    row["table"] = table.name; row["owner"] = table.owner; row["definition"] = definition;
    SwJsonArray rows; rows.append(row);
    checked(database.upsertRows(catalogSchema(), rows));
}

void SwRealtimeDb::State::restore() {
    SwJsonArray catalog;
    checked(database.readRows(catalogSchema(), &catalog));
    if (catalog.size() > swRealtimeDbDetail::kMaxTables) throw std::runtime_error("stored catalog exceeds table limit");
    for (const auto& value : catalog) {
        const auto entry = value.toObject();
        const auto definition = entry["definition"].toObject();
        const auto owner = swRealtimeDbDetail::requiredString(entry, "owner");
        swRealtimeDbDetail::validateActor(owner);
        auto table = parseTable(definition, definition["view"].toBool(), owner, {});
        SwJsonArray stored;
        checked(database.readRows(storageSchema(table), &stored));
        SwJsonArray plain;
        for (const auto& value : stored) {
            auto row = value.toObject();
            const auto order = swRealtimeDbDetail::parseRevision(
                swRealtimeDbDetail::requiredString(row, orderColumn, 20));
            table.writeOrder.emplace(row[table.key].toString(), order);
            writeSequence = std::max(writeSequence, order);
            row.remove(orderColumn); plain.append(row);
        }
        const auto rows = validateRows(table, plain);
        if (rows.size() > table.maxRows) throw std::runtime_error("stored table exceeds retention limit");
        table.bytes = tableBytes(table, rows);
        if (table.bytes > swRealtimeDbDetail::kMaxBytes || bytes > swRealtimeDbDetail::kMaxDatabaseBytes - table.bytes)
            throw std::runtime_error("stored database exceeds byte limit");
        table.valid = false;
        table.error = "awaiting owner registration and fresh data";
        validateGraph(table);
        bytes += table.bytes;
        const auto name = table.name;
        tables.emplace(name, std::move(table));
    }
}
