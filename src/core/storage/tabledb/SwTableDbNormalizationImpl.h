#pragma once

#include <cmath>
#include <cstring>

inline SwDbStatus SwTableDb::coerceValue_(const SwString& columnType,
                                          const SwJsonValue& input,
                                          bool nullable,
                                          SwJsonValue& outValue) {
    const SwString normalized = swTableDbDetail::normalizedType_(columnType);
    if (input.isNull()) {
        if (!nullable) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Null value is not allowed");
        }
        outValue = SwJsonValue();
        return SwDbStatus::success();
    }
    if (normalized == "string") {
        if (input.isObject() || input.isArray()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Object/array cannot be coerced to string");
        }
        outValue = SwJsonValue(input.toString());
        return SwDbStatus::success();
    }
    if (normalized == "integer") {
        long long value = 0;
        if (input.isInt() || input.isBool()) {
            value = static_cast<long long>(input.toInteger(0));
        } else if (input.isDouble()) {
            const double raw = input.toDouble();
            if (std::floor(raw) != raw) {
                return SwDbStatus(SwDbStatus::InvalidArgument, "Integer value is not exact");
            }
            value = static_cast<long long>(raw);
        } else if (input.isString()) {
            if (!swTableDbDetail::parseLongLongString_(SwString(input.toString()), value)) {
                return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid integer string");
            }
        } else {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Value cannot be coerced to integer");
        }
        outValue = SwJsonValue(value);
        return SwDbStatus::success();
    }
    if (normalized == "number") {
        double value = 0.0;
        if (input.isDouble() || input.isInt() || input.isBool()) {
            value = input.toDouble();
        } else if (input.isString()) {
            if (!swTableDbDetail::parseDoubleString_(SwString(input.toString()), value)) {
                return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid number string");
            }
        } else {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Value cannot be coerced to number");
        }
        outValue = SwJsonValue(value);
        return SwDbStatus::success();
    }
    if (normalized == "boolean") {
        if (input.isBool() || input.isInt() || input.isDouble()) {
            outValue = SwJsonValue(input.toBool(false));
            return SwDbStatus::success();
        }
        if (input.isString()) {
            const SwString lowered = SwString(input.toString()).trimmed().toLower();
            if (lowered == "true" || lowered == "1") {
                outValue = SwJsonValue(true);
                return SwDbStatus::success();
            }
            if (lowered == "false" || lowered == "0") {
                outValue = SwJsonValue(false);
                return SwDbStatus::success();
            }
        }
        return SwDbStatus(SwDbStatus::InvalidArgument, "Value cannot be coerced to boolean");
    }
    if (normalized == "datetime") {
        if (!input.isString()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Datetime must be a string");
        }
        const SwString value = SwString(input.toString()).trimmed();
        if (value.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Datetime cannot be empty");
        }
        outValue = SwJsonValue(value.toStdString());
        return SwDbStatus::success();
    }
    if (normalized == "json") {
        outValue = input;
        return SwDbStatus::success();
    }
    return SwDbStatus(SwDbStatus::InvalidArgument, "Unsupported column type");
}

inline SwDbStatus SwTableDb::normalizeRowForCreate_(const SwTableSchema& schema,
                                                    const SwJsonObject& input,
                                                    SwJsonObject& outRow) {
    outRow = SwJsonObject();
    for (SwJsonObject::ConstIterator it = input.begin(); it != input.end(); ++it) {
        if (isBuiltInField_(it.key()) || !findColumn_(schema, it.key())) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Unknown or reserved column in row payload");
        }
    }
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const SwTableColumn& column = schema.columns[i];
        SwJsonValue value;
        if (input.contains(column.columnId)) {
            const SwDbStatus status = coerceValue_(column.type, input.value(column.columnId), column.nullable, value);
            if (!status.ok()) {
                return status;
            }
        } else if (!column.defaultValue.isNull()) {
            const SwDbStatus status = coerceValue_(column.type, column.defaultValue, column.nullable, value);
            if (!status.ok()) {
                return status;
            }
        } else if (!column.nullable || column.required) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing required column");
        } else {
            value = SwJsonValue();
        }
        outRow[column.columnId] = value;
    }
    const SwString timestamp = nowIso_();
    outRow["rowId"] = nextRowId_().toStdString();
    outRow["createdAt"] = timestamp.toStdString();
    outRow["updatedAt"] = timestamp.toStdString();
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::normalizeRowForUpdate_(const SwTableSchema& schema,
                                                    const SwJsonObject& currentRow,
                                                    const SwJsonObject& patch,
                                                    SwJsonObject& outRow) {
    outRow = currentRow;
    for (SwJsonObject::ConstIterator it = patch.begin(); it != patch.end(); ++it) {
        if (isBuiltInField_(it.key()) || !findColumn_(schema, it.key())) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Unknown or reserved column in row patch");
        }
        const SwTableColumn* column = findColumn_(schema, it.key());
        SwJsonValue value;
        const SwDbStatus status = coerceValue_(column->type, it.value(), column->nullable, value);
        if (!status.ok()) {
            return status;
        }
        outRow[it.key()] = value;
    }
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const SwTableColumn& column = schema.columns[i];
        if ((!outRow.contains(column.columnId) || outRow.value(column.columnId).isNull()) &&
            (!column.nullable || column.required)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Patch would violate non-null column");
        }
    }
    outRow["updatedAt"] = nowIso_().toStdString();
    return SwDbStatus::success();
}

inline SwString SwTableDb::columnTypeForField_(const SwTableSchema& schema, const SwString& columnId) {
    if (columnId == "rowId" || columnId == "createdAt" || columnId == "updatedAt") {
        return "string";
    }
    const SwTableColumn* column = findColumn_(schema, columnId);
    return column ? column->type : SwString();
}

inline SwDbStatus SwTableDb::validateQuery_(const SwTableSchema& schema, const SwTableQuery& query) {
    const int limit = query.limit <= 0 ? 50 : query.limit;
    if (limit > 250) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Query limit exceeds maximum");
    }
    const SwString direction = query.sortDirection.trimmed().isEmpty() ? SwString("desc")
                                                                       : query.sortDirection.trimmed().toLower();
    if (direction != "asc" && direction != "desc") {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid sort direction");
    }
    const SwString sortBy = query.sortBy.trimmed().isEmpty() ? SwString("updatedAt") : query.sortBy.trimmed();
    if (!isBuiltInField_(sortBy) && !findIndexByColumn_(schema, sortBy)) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Sort column is not indexed");
    }
    for (std::size_t i = 0; i < query.filters.size(); ++i) {
        const SwTableQueryFilter& filter = query.filters[i];
        const SwString type = columnTypeForField_(schema, filter.columnId);
        if (type.trimmed().isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Filter targets unknown column");
        }
        if (!isBuiltInField_(filter.columnId) && !findIndexByColumn_(schema, filter.columnId)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Filter column is not indexed");
        }
        const SwString op = filter.op.trimmed().isEmpty() ? SwString("eq") : filter.op.trimmed().toLower();
        if (op != "eq" && op != "prefix") {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Unsupported filter operator");
        }
        if (op == "prefix" && !requiresStringPrefixSupport_(filter.columnId, type)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Prefix filters require string-like indexed columns");
        }
    }
    return SwDbStatus::success();
}

inline bool SwTableDb::matchesFilter_(const SwTableSchema& schema,
                                      const SwJsonObject& row,
                                      const SwTableQueryFilter& filter) {
    const SwString columnType = columnTypeForField_(schema, filter.columnId);
    SwJsonValue filterValue;
    if (!coerceValue_(columnType, filter.value, true, filterValue).ok()) {
        return false;
    }
    const SwJsonValue rowValue = row.value(filter.columnId);
    const SwString op = filter.op.trimmed().isEmpty() ? SwString("eq") : filter.op.trimmed().toLower();
    if (op == "eq") {
        return compareFieldValues_(schema, filter.columnId, rowValue, filterValue) == 0;
    }
    const SwString lhs = swTableDbDetail::toFieldString_(rowValue);
    const SwString rhs = swTableDbDetail::toFieldString_(filterValue);
    return lhs.startsWith(rhs);
}

inline int SwTableDb::compareFieldValues_(const SwTableSchema& schema,
                                          const SwString& columnId,
                                          const SwJsonValue& lhs,
                                          const SwJsonValue& rhs) {
    const SwString type = swTableDbDetail::normalizedType_(columnTypeForField_(schema, columnId));
    if (type == "integer") {
        const long long left = static_cast<long long>(lhs.toInteger(0));
        const long long right = static_cast<long long>(rhs.toInteger(0));
        return left < right ? -1 : (left > right ? 1 : 0);
    }
    if (type == "number") {
        const double left = lhs.toDouble();
        const double right = rhs.toDouble();
        return left < right ? -1 : (left > right ? 1 : 0);
    }
    if (type == "boolean") {
        const bool left = lhs.toBool(false);
        const bool right = rhs.toBool(false);
        return left == right ? 0 : (left ? 1 : -1);
    }
    const std::string left = lhs.toString();
    const std::string right = rhs.toString();
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}

inline SwJsonValue SwTableDb::sortValueForRow_(const SwJsonObject& row, const SwString& sortBy) {
    return row.value(sortBy);
}

inline SwString SwTableDb::makeCursor_(const SwJsonValue& sortValue, const SwString& rowId) {
    SwJsonObject object;
    object["sortValue"] = sortValue;
    object["rowId"] = rowId.toStdString();
    return SwString(jsonBytes_(object).toBase64().toStdString());
}

inline bool SwTableDb::parseCursor_(const SwString& cursor, SwJsonValue& sortValueOut, SwString& rowIdOut) {
    sortValueOut = SwJsonValue();
    rowIdOut.clear();
    if (cursor.trimmed().isEmpty()) {
        return true;
    }
    SwJsonObject object;
    if (!parseJsonObject_(SwByteArray::fromBase64(SwByteArray(cursor.toStdString())), object)) {
        return false;
    }
    sortValueOut = object.value("sortValue");
    rowIdOut = object.value("rowId").toString().c_str();
    return true;
}

inline SwByteArray SwTableDb::encodeIndexValue_(const SwString& columnType,
                                                const SwJsonValue& value,
                                                bool* okOut) {
    if (okOut) {
        *okOut = true;
    }
    const SwString normalized = swTableDbDetail::normalizedType_(columnType);
    if (value.isNull()) {
        return SwByteArray("~");
    }
    if (normalized == "string" || normalized == "datetime") {
        return SwByteArray(value.toString()).toHex();
    }
    if (normalized == "integer") {
        const std::uint64_t transformed =
            static_cast<std::uint64_t>(static_cast<long long>(value.toInteger(0))) ^ 0x8000000000000000ull;
        return swTableDbDetail::hexWidth64_(transformed);
    }
    if (normalized == "number") {
        double number = value.toDouble();
        std::uint64_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        bits = (bits & 0x8000000000000000ull) ? ~bits : (bits ^ 0x8000000000000000ull);
        return swTableDbDetail::hexWidth64_(bits);
    }
    if (normalized == "boolean") {
        return value.toBool(false) ? SwByteArray("1") : SwByteArray("0");
    }
    if (okOut) {
        *okOut = false;
    }
    return SwByteArray();
}

inline SwMap<SwString, SwList<SwByteArray>> SwTableDb::secondaryKeysForRow_(const SwTableSchema& schema,
                                                                            const SwJsonObject& row) {
    SwMap<SwString, SwList<SwByteArray>> secondary;
    const SwString rowId = row.value("rowId").toString().c_str();
    const SwByteArray rowSuffix = swTableDbDetail::rowSuffix_(rowId);

    SwList<SwByteArray> createdKeys;
    createdKeys.append(encodeIndexValue_("string", row.value("createdAt")) + SwByteArray("\x1f") + rowSuffix);
    secondary.insert(swTableDbDetail::indexNameForField_(schema, "createdAt"), createdKeys);

    SwList<SwByteArray> updatedKeys;
    updatedKeys.append(encodeIndexValue_("string", row.value("updatedAt")) + SwByteArray("\x1f") + rowSuffix);
    secondary.insert(swTableDbDetail::indexNameForField_(schema, "updatedAt"), updatedKeys);

    for (std::size_t i = 0; i < schema.indexes.size(); ++i) {
        const SwTableIndex& index = schema.indexes[i];
        bool ok = false;
        const SwByteArray encoded =
            encodeIndexValue_(columnTypeForField_(schema, index.columnId), row.value(index.columnId), &ok);
        if (!ok) {
            continue;
        }
        SwList<SwByteArray> keys;
        keys.append(encoded + SwByteArray("\x1f") + rowSuffix);
        secondary.insert(swTableDbDetail::indexNameForField_(schema, index.columnId), keys);
    }
    return secondary;
}

inline SwDbStatus SwTableDb::buildMigratedRow_(const SwTableSchema& currentSchema,
                                               const SwTableSchema& nextSchema,
                                               const SwTableMigrationPlan& plan,
                                               const SwJsonObject& currentRow,
                                               SwJsonObject& outRow) {
    SwJsonObject working = currentRow;
    for (SwMap<SwString, SwString>::const_iterator it = plan.renamedColumns.begin(); it != plan.renamedColumns.end(); ++it) {
        if (working.contains(it.key())) {
            working[it.value()] = working.value(it.key());
            working.remove(it.key());
        }
    }
    for (std::size_t i = 0; i < currentSchema.columns.size(); ++i) {
        const SwString columnId = currentSchema.columns[i].columnId;
        if (!findColumn_(nextSchema, columnId) && !plan.renamedColumns.contains(columnId) &&
            working.contains(columnId) && !isEmptyValue_(working.value(columnId))) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Cannot drop non-empty column");
        }
        if (!findColumn_(nextSchema, columnId) && !plan.renamedColumns.contains(columnId)) {
            working.remove(columnId);
        }
    }

    outRow = SwJsonObject();
    outRow["rowId"] = currentRow.value("rowId").toString();
    outRow["createdAt"] = currentRow.value("createdAt").toString();
    outRow["updatedAt"] = nowIso_().toStdString();
    for (std::size_t i = 0; i < nextSchema.columns.size(); ++i) {
        const SwTableColumn& column = nextSchema.columns[i];
        SwJsonValue value;
        if (working.contains(column.columnId)) {
            const SwDbStatus status = coerceValue_(column.type, working.value(column.columnId), column.nullable, value);
            if (!status.ok()) {
                return status;
            }
        } else if (!column.defaultValue.isNull()) {
            const SwDbStatus status = coerceValue_(column.type, column.defaultValue, column.nullable, value);
            if (!status.ok()) {
                return status;
            }
        } else if (!column.nullable || column.required) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Migration would violate non-null column");
        } else {
            value = SwJsonValue();
        }
        outRow[column.columnId] = value;
    }
    return SwDbStatus::success();
}
