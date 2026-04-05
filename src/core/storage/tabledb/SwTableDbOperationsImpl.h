#pragma once

#include <algorithm>
#include <functional>
#include <vector>

inline SwDbStatus SwTableDb::insertRow(const SwTableSchema& schema,
                                       const SwJsonObject& input,
                                       SwJsonObject* createdOut) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }
    const SwDbStatus schemaStatus = validateSchema_(schema);
    if (!schemaStatus.ok()) {
        return schemaStatus;
    }
    SwJsonObject row;
    const SwDbStatus normalizeStatus = normalizeRowForCreate_(schema, input, row);
    if (!normalizeStatus.ok()) {
        return normalizeStatus;
    }
    SwDbWriteBatch batch;
    batch.put(rowPrimaryKey_(schema.tableId, row.value("rowId").toString().c_str()),
              jsonBytes_(row),
              secondaryKeysForRow_(schema, row));
    const SwDbStatus writeStatus = db_.write(batch);
    if (writeStatus.ok() && createdOut) {
        *createdOut = row;
    }
    return writeStatus;
}

inline SwDbStatus SwTableDb::getRow(const SwTableSchema& schema,
                                    const SwString& rowId,
                                    SwJsonObject* rowOut) const {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }
    if (!rowOut) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output row");
    }
    const SwDbStatus schemaStatus = validateSchema_(schema);
    if (!schemaStatus.ok()) {
        return schemaStatus;
    }
    SwByteArray bytes;
    const SwDbStatus getStatus = db_.get(rowPrimaryKey_(schema.tableId, rowId.trimmed()), &bytes, nullptr);
    if (!getStatus.ok()) {
        return getStatus;
    }
    if (!parseJsonObject_(bytes, *rowOut)) {
        return SwDbStatus(SwDbStatus::Corruption, "Corrupted table row");
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::updateRow(const SwTableSchema& schema,
                                       const SwString& rowId,
                                       const SwJsonObject& patch,
                                       SwJsonObject* updatedOut) {
    SwJsonObject currentRow;
    const SwDbStatus getStatus = getRow(schema, rowId, &currentRow);
    if (!getStatus.ok()) {
        return getStatus;
    }
    SwMutexLocker locker(&mutex_);
    SwJsonObject nextRow;
    const SwDbStatus normalizeStatus = normalizeRowForUpdate_(schema, currentRow, patch, nextRow);
    if (!normalizeStatus.ok()) {
        return normalizeStatus;
    }
    SwDbWriteBatch batch;
    batch.put(rowPrimaryKey_(schema.tableId, rowId.trimmed()),
              jsonBytes_(nextRow),
              secondaryKeysForRow_(schema, nextRow));
    const SwDbStatus writeStatus = db_.write(batch);
    if (writeStatus.ok() && updatedOut) {
        *updatedOut = nextRow;
    }
    return writeStatus;
}

inline SwDbStatus SwTableDb::deleteRow(const SwTableSchema& schema, const SwString& rowId) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }
    SwDbWriteBatch batch;
    batch.erase(rowPrimaryKey_(schema.tableId, rowId.trimmed()));
    return db_.write(batch);
}

inline SwDbStatus SwTableDb::queryRows(const SwTableSchema& schema,
                                       const SwTableQuery& query,
                                       SwTableQueryResult* outResult) const {
    SwDbSnapshot snapshot;
    {
        SwMutexLocker locker(&mutex_);
        if (!opened_) {
            return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
        }
        if (!outResult) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing query result");
        }
        const SwDbStatus schemaStatus = validateSchema_(schema);
        if (!schemaStatus.ok()) {
            return schemaStatus;
        }
        const SwDbStatus queryStatus = validateQuery_(schema, query);
        if (!queryStatus.ok()) {
            return queryStatus;
        }
        snapshot = db_.createSnapshot();
    }
    if (!snapshot.isValid()) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }

    const SwString sortBy = query.sortBy.trimmed().isEmpty() ? SwString("updatedAt") : query.sortBy.trimmed();
    const SwString sortDirection = query.sortDirection.trimmed().isEmpty() ? SwString("desc")
                                                                           : query.sortDirection.trimmed().toLower();
    const bool descending = sortDirection == "desc";

    SwString scanField = "rowId";
    SwString scanOp;
    SwJsonValue scanValue;
    for (std::size_t i = 0; i < query.filters.size(); ++i) {
        if (isBuiltInField_(query.filters[i].columnId) || findIndexByColumn_(schema, query.filters[i].columnId)) {
            scanField = query.filters[i].columnId;
            scanOp = query.filters[i].op.trimmed().isEmpty() ? SwString("eq") : query.filters[i].op.trimmed().toLower();
            scanValue = query.filters[i].value;
            break;
        }
    }
    if (scanField == "rowId" && scanOp.isEmpty() && sortBy != "rowId") {
        scanField = sortBy;
    }

    SwByteArray primaryStart;
    SwByteArray primaryEnd;
    SwString indexName;
    SwByteArray indexStart;
    SwByteArray indexEnd;
    if (scanField == "rowId") {
        primaryStart = rowPrefix_(schema.tableId);
        primaryEnd = primaryStart;
        primaryEnd.append('\xff');
        if (scanOp == "eq" || scanOp == "prefix") {
            const SwString prefix = scanValue.toString().c_str();
            primaryStart = rowPrimaryStart_(schema.tableId, prefix);
            primaryEnd = rowPrimaryEnd_(schema.tableId, prefix);
        }
    } else {
        indexName = swTableDbDetail::indexNameForField_(schema, scanField);
        if (scanOp == "eq" || scanOp == "prefix") {
            bool ok = false;
            indexStart = encodeIndexValue_(columnTypeForField_(schema, scanField), scanValue, &ok);
            if (!ok) {
                return SwDbStatus(SwDbStatus::InvalidArgument, "Unable to encode indexed filter");
            }
            indexEnd = indexStart;
            indexEnd.append('\xff');
        }
    }

    SwJsonValue cursorSortValue;
    SwString cursorRowId;
    if (!parseCursor_(query.cursor, cursorSortValue, cursorRowId)) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid cursor");
    }

    const bool hasCursor = !query.cursor.trimmed().isEmpty();
    const int limit = query.limit <= 0 ? 50 : query.limit;
    const std::size_t maxRows = static_cast<std::size_t>(limit + 1);

    auto compareOrderedValues = [&](const SwJsonValue& lhsValue,
                                    const SwString& lhsRowId,
                                    const SwJsonValue& rhsValue,
                                    const SwString& rhsRowId) -> int {
        int valueCompare = compareFieldValues_(schema, sortBy, lhsValue, rhsValue);
        if (valueCompare == 0) {
            if (lhsRowId < rhsRowId) {
                valueCompare = -1;
            } else if (lhsRowId > rhsRowId) {
                valueCompare = 1;
            } else {
                valueCompare = 0;
            }
        }
        return descending ? -valueCompare : valueCompare;
    };

    auto compareRows = [&](const SwJsonObject& lhs, const SwJsonObject& rhs) -> int {
        return compareOrderedValues(sortValueForRow_(lhs, sortBy),
                                    lhs.value("rowId").toString().c_str(),
                                    sortValueForRow_(rhs, sortBy),
                                    rhs.value("rowId").toString().c_str());
    };

    auto matchesAllFilters = [&](const SwJsonObject& row) -> bool {
        for (std::size_t i = 0; i < query.filters.size(); ++i) {
            if (!matchesFilter_(schema, row, query.filters[i])) {
                return false;
            }
        }
        return true;
    };

    auto rowAfterCursor = [&](const SwJsonObject& row) -> bool {
        if (!hasCursor) {
            return true;
        }
        return compareOrderedValues(sortValueForRow_(row, sortBy),
                                    row.value("rowId").toString().c_str(),
                                    cursorSortValue,
                                    cursorRowId) > 0;
    };

    auto scanRows = [&](const std::function<bool(const SwJsonObject&)>& visitor) {
        if (scanField == "rowId") {
            for (SwDbIterator it = snapshot.scanPrimary(primaryStart, primaryEnd); it.isValid(); it.next()) {
                SwJsonObject row;
                if (parseJsonObject_(it.current().value, row) && !visitor(row)) {
                    break;
                }
            }
            return;
        }

        for (SwDbIterator it = snapshot.scanIndex(indexName, indexStart, indexEnd); it.isValid(); it.next()) {
            SwJsonObject row;
            if (parseJsonObject_(it.current().value, row) && !visitor(row)) {
                break;
            }
        }
    };

    outResult->rows.clear();
    outResult->nextCursor.clear();
    outResult->hasMore = false;

    std::vector<SwJsonObject> selected;
    if (scanField == sortBy) {
        if (!descending) {
            bool collecting = !hasCursor;
            scanRows([&](const SwJsonObject& row) -> bool {
                if (!matchesAllFilters(row)) {
                    return true;
                }
                if (!collecting) {
                    if (!rowAfterCursor(row)) {
                        return true;
                    }
                    collecting = true;
                }
                selected.push_back(row);
                return selected.size() < maxRows;
            });
        } else {
            bool passedCursorRange = !hasCursor;
            std::vector<SwJsonObject> tail;
            scanRows([&](const SwJsonObject& row) -> bool {
                if (!matchesAllFilters(row)) {
                    return true;
                }
                if (!passedCursorRange) {
                    if (!rowAfterCursor(row)) {
                        return false;
                    }
                    passedCursorRange = true;
                } else if (hasCursor && !rowAfterCursor(row)) {
                    return false;
                }
                tail.push_back(row);
                if (tail.size() > maxRows) {
                    tail.erase(tail.begin());
                }
                return true;
            });
            for (std::vector<SwJsonObject>::reverse_iterator it = tail.rbegin(); it != tail.rend(); ++it) {
                selected.push_back(*it);
            }
        }
    } else {
        scanRows([&](const SwJsonObject& row) -> bool {
            if (!matchesAllFilters(row) || !rowAfterCursor(row)) {
                return true;
            }
            if (selected.size() < maxRows) {
                selected.push_back(row);
                return true;
            }

            std::size_t worstIndex = 0;
            for (std::size_t i = 1; i < selected.size(); ++i) {
                if (compareRows(selected[worstIndex], selected[i]) < 0) {
                    worstIndex = i;
                }
            }
            if (compareRows(row, selected[worstIndex]) < 0) {
                selected[worstIndex] = row;
            }
            return true;
        });
        std::sort(selected.begin(), selected.end(), [&](const SwJsonObject& lhs, const SwJsonObject& rhs) {
            return compareRows(lhs, rhs) < 0;
        });
    }

    for (std::size_t i = 0; i < selected.size() && i < static_cast<std::size_t>(limit); ++i) {
        outResult->rows.append(selected[i]);
    }
    outResult->hasMore = selected.size() > static_cast<std::size_t>(limit);
    if (outResult->hasMore && !outResult->rows.isEmpty()) {
        const SwJsonObject& tail = outResult->rows[outResult->rows.size() - 1];
        outResult->nextCursor =
            makeCursor_(sortValueForRow_(tail, sortBy), tail.value("rowId").toString().c_str());
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::clearTable(const SwTableSchema& schema) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }
    SwDbWriteBatch batch;
    SwByteArray start = rowPrefix_(schema.tableId);
    SwByteArray end = start;
    end.append('\xff');
    for (SwDbIterator it = db_.scanPrimary(start, end); it.isValid(); it.next()) {
        batch.erase(it.current().primaryKey);
    }
    if (batch.isEmpty()) {
        return SwDbStatus::success();
    }
    return db_.write(batch);
}

inline SwDbStatus SwTableDb::migrateTable(const SwTableSchema& currentSchema,
                                          const SwTableSchema& nextSchema,
                                          const SwTableMigrationPlan& plan) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }
    const SwDbStatus currentStatus = validateSchema_(currentSchema);
    if (!currentStatus.ok()) {
        return currentStatus;
    }
    const SwDbStatus nextStatus = validateSchema_(nextSchema);
    if (!nextStatus.ok()) {
        return nextStatus;
    }
    SwDbWriteBatch batch;
    SwByteArray start = rowPrefix_(currentSchema.tableId);
    SwByteArray end = start;
    end.append('\xff');
    for (SwDbIterator it = db_.scanPrimary(start, end); it.isValid(); it.next()) {
        SwJsonObject currentRow;
        if (!parseJsonObject_(it.current().value, currentRow)) {
            return SwDbStatus(SwDbStatus::Corruption, "Corrupted row during migration");
        }
        SwJsonObject nextRow;
        const SwDbStatus rowStatus = buildMigratedRow_(currentSchema, nextSchema, plan, currentRow, nextRow);
        if (!rowStatus.ok()) {
            return rowStatus;
        }
        const SwString rowId = currentRow.value("rowId").toString().c_str();
        if (currentSchema.tableId != nextSchema.tableId) {
            batch.erase(rowPrimaryKey_(currentSchema.tableId, rowId));
        }
        batch.put(rowPrimaryKey_(nextSchema.tableId, rowId),
                  jsonBytes_(nextRow),
                  secondaryKeysForRow_(nextSchema, nextRow));
    }
    if (batch.isEmpty()) {
        return SwDbStatus::success();
    }
    return db_.write(batch);
}
