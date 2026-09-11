#pragma once

#include <cmath>
#include <set>
#include <vector>

namespace swTableDbDetail {

inline bool finiteJson_(const SwJsonValue& value, unsigned depth = 0);

inline bool finiteJson_(const SwJsonObject& object, unsigned depth = 0) {
    if (depth > 64) return false;
    for (const auto& entry : object.dataRef()) {
        if (!finiteJson_(entry.second, depth + 1)) return false;
    }
    return true;
}

inline bool finiteJson_(const SwJsonValue& value, unsigned depth) {
    if (depth > 64 || (value.isDouble() && !std::isfinite(value.toDouble()))) return false;
    if (value.isObject()) {
        const auto object = value.toObjectPtr();
        if (object && !finiteJson_(*object, depth)) return false;
    } else if (value.isArray()) {
        const auto array = value.toArrayPtr();
        if (array) for (const auto& entry : array->dataRef()) {
            if (!finiteJson_(entry, depth + 1)) return false;
        }
    }
    return true;
}

inline bool exactColumnValue_(const SwTableColumn& column, const SwJsonValue& value) {
    if (value.isNull()) return column.nullable;
    const SwString type = normalizedType_(column.type);
    if (type == "integer") {
        if (value.type() == SwJsonValue::Type::Integer) return true;
        const double lower = static_cast<double>(std::numeric_limits<long long>::min());
        return value.isDouble() && value.isInt() && value.toDouble() >= lower && value.toDouble() < -lower;
    }
    return ((type == "string" || type == "datetime") && value.isString()) ||
           (type == "number" && value.isDouble()) ||
           (type == "boolean" && value.isBool()) || type == "json";
}

} // namespace swTableDbDetail

inline SwDbStatus SwTableDb::validateExactRow_(const SwTableSchema& schema,
                                               const SwJsonObject& input) {
    if (!swTableDbDetail::finiteJson_(input)) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Exact rows require finite JSON with at most 64 nesting levels");
    }
    for (SwJsonObject::ConstIterator it = input.begin(); it != input.end(); ++it) {
        const SwTableColumn* column = findColumn_(schema, it.key());
        if (!column || !swTableDbDetail::exactColumnValue_(*column, it.value())) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Unknown column or exact row type mismatch");
        }
    }
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const SwTableColumn& column = schema.columns[i];
        if ((column.required || !column.nullable) && !input.contains(column.columnId)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing required column");
        }
    }
    const SwJsonValue& key = input[schema.primaryKey];
    if (!key.isString() || key.toString().isEmpty() || key.toString().size() > 128) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Exact row key must be a string of 1 to 128 bytes");
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::normalizeExactRow_(const SwTableSchema& schema,
                                                const SwJsonObject& input,
                                                SwJsonObject& outRow) {
    const auto status = validateExactRow_(schema, input);
    if (!status.ok()) return status;
    outRow = input;
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::readRows(const SwTableSchema& schema, SwJsonArray* rowsOut) const {
    return readRows_(schema, rowsOut, nullptr);
}

inline SwDbStatus SwTableDb::readRows_(const SwTableSchema& schema, SwJsonArray* rowsOut,
                                      const PreparedSchema_* prepared) const {
    SwDbSnapshot snapshot;
    {
        SwMutexLocker locker(&mutex_);
        if (!opened_) return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
        if (!rowsOut) return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output rows");
        const SwDbStatus status = prepared ? SwDbStatus::success() : validateSchema_(schema);
        if (!status.ok()) return status;
        snapshot = db_.createSnapshot();
    }
    if (!snapshot.isValid()) return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    const SwByteArray start = prepared ? prepared->rowPrefix : rowPrefix_(schema.tableId);
    SwByteArray end = start;
    end.append('\xff');
    SwJsonArray rows;
    for (SwDbJsonIterator it = snapshot.scanPrimaryJson(start, end); it.isValid(); it.next()) {
        if (!it.current().validJson) {
            return SwDbStatus(SwDbStatus::Corruption, "Corrupted table row");
        }
        rows.append(it.current().value);
    }
    *rowsOut = std::move(rows);
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::replaceRows(const SwTableSchema& schema, const SwJsonArray& rows) {
    return writeRows_(schema, rows, true);
}

inline SwDbStatus SwTableDb::upsertRows(const SwTableSchema& schema, const SwJsonArray& rows) {
    return writeRows_(schema, rows, false);
}

inline SwDbStatus SwTableDb::applyRows(const SwTableSchema& schema, const SwJsonArray& rows,
                                      const SwList<SwString>& eraseKeys) {
    return writeRows_(schema, rows, false, eraseKeys);
}

inline SwDbStatus SwTableDb::writeRows_(const SwTableSchema& schema, const SwJsonArray& rows, bool replace,
                                       const SwList<SwString>& eraseKeys, const PreparedSchema_* prepared, SwJsonArray* owned) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    const SwDbStatus schemaStatus = prepared ? SwDbStatus::success() : validateSchema_(schema);
    if (!schemaStatus.ok()) return schemaStatus;
    if (schema.rowMode != SwTableRowMode::Exact) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Atomic row batches require an Exact schema");
    }
    std::set<SwByteArray> keys;
    SwDbWriteBatch batch;
    const SwJsonObject empty;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].isObject()) return SwDbStatus(SwDbStatus::InvalidArgument, "Every row must be an object");
        // Public Exact writes admit a deep copy. Only the private store-owned
        // path can transfer this row after validation and index extraction.
        const auto object = rows[i].toObjectPtr();
        const SwJsonObject& row = object ? *object : empty;
        const SwDbStatus status = prepared ? validateExactRow_(*prepared, row) : validateExactRow_(schema, row);
        if (!status.ok()) return status;
        const auto rowId = row[schema.primaryKey].toString();
        const SwByteArray key = prepared ? prepared->rowPrefix + SwByteArray(rowId.toUtf8()) : rowPrimaryKey_(schema.tableId, rowId);
        if (!keys.insert(key).second) return SwDbStatus(SwDbStatus::InvalidArgument, "Duplicate primary key within row batch");
        const auto secondary = prepared ? secondaryKeysForRow_(*prepared, row) : secondaryKeysForRow_(schema, row);
        if (owned && object) batch.putJsonOwned_(key, std::move(*object), secondary);
        else batch.putJson(key, row, secondary);
    }
    for (const auto& rowId : eraseKeys) {
        if (rowId.isEmpty() || rowId.size() > 128)
            return SwDbStatus(SwDbStatus::InvalidArgument, "Exact row key must be a string of 1 to 128 bytes");
        const SwByteArray key = prepared ? prepared->rowPrefix + SwByteArray(rowId.toUtf8()) : rowPrimaryKey_(schema.tableId, rowId);
        if (!keys.insert(key).second)
            return SwDbStatus(SwDbStatus::InvalidArgument, "Duplicate primary key within row batch");
        batch.erase(key);
    }
    if (replace) {
        const SwByteArray start = prepared ? prepared->rowPrefix : rowPrefix_(schema.tableId);
        SwByteArray end = start;
        end.append('\xff');
        for (SwDbJsonIterator it = db_.scanPrimaryJson(start, end); it.isValid(); it.next()) {
            if (!keys.count(it.current().primaryKey)) batch.erase(it.current().primaryKey);
        }
    }
    // Each primary key occurs once. Snapshot iterators from replacement scans
    // are already destroyed, so a memory-only commit does not copy the whole
    // database to preserve an internal reader. Publish this batch atomically.
    return batch.isEmpty() ? SwDbStatus::success() : db_.write(batch);
}
