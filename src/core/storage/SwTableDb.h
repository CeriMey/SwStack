#pragma once

#include "SwEmbeddedDb.h"
#include "SwMutex.h"
#include "SwTableDbTypes.h"
#include "SwTableDbPreparedSchema.h"

#include <vector>

namespace swTableDbDetail { struct OwnedRowsAccess_; }

class SwTableDb {
public:
    SwTableDb() = default;

    void setStorageDir(const SwString& storageDir);
    const SwString& storageDir() const;

    SwDbStatus open(const SwEmbeddedDbOptions& options = SwEmbeddedDbOptions());
    void close();
    bool isOpen() const;
    SwDbStatus sync();

    SwDbStatus insertRow(const SwTableSchema& schema,
                         const SwJsonObject& input,
                         SwJsonObject* createdOut = nullptr);
    SwDbStatus getRow(const SwTableSchema& schema,
                      const SwString& rowId,
                      SwJsonObject* rowOut) const;
    SwDbStatus getRowRecord(const SwTableSchema& schema, const SwString& rowId, SwDbJsonRecord* recordOut) const;
    SwDbStatus getRowRecord(const SwTableDbPreparedSchema& schema, const SwString& rowId,
                            SwDbJsonRecord* recordOut) const;
    SwDbStatus updateRow(const SwTableSchema& schema,
                         const SwString& rowId,
                         const SwJsonObject& patch,
                         SwJsonObject* updatedOut = nullptr);
    SwDbStatus deleteRow(const SwTableSchema& schema, const SwString& rowId);
    SwDbStatus queryRows(const SwTableSchema& schema,
                         const SwTableQuery& query,
                         SwTableQueryResult* outResult) const;
    SwDbStatus clearTable(const SwTableSchema& schema);
    // Complete snapshot, ordered by primary key. No pagination limit.
    SwDbStatus readRows(const SwTableSchema& schema, SwJsonArray* rowsOut) const;
    // Exact schemas only. Validate the entire batch before one atomic commit.
    // replaceRows removes omitted keys; upsertRows retains them. Neither coerces
    // values nor adds metadata. Duplicate keys reject the entire batch.
    SwDbStatus replaceRows(const SwTableSchema& schema, const SwJsonArray& rows);
    SwDbStatus upsertRows(const SwTableSchema& schema, const SwJsonArray& rows);
    // Apply only the named rows and erasures in one atomic commit. Omitted
    // rows are retained; duplicate keys across either list reject the batch.
    SwDbStatus applyRows(const SwTableSchema& schema, const SwJsonArray& rows,
                         const SwList<SwString>& eraseKeys);
    SwDbStatus migrateTable(const SwTableSchema& currentSchema,
                            const SwTableSchema& nextSchema,
                            const SwTableMigrationPlan& plan = SwTableMigrationPlan());
    static SwDbStatus validateSchema(const SwTableSchema& schema);
    // Validate/copy a descriptor once. Invalid preparation leaves the previous
    // output handle intact; prepared operations still validate every row/query.
    static SwDbStatus prepareSchema(const SwTableSchema& schema, SwTableDbPreparedSchema* preparedOut);
    SwDbStatus getRow(const SwTableDbPreparedSchema& schema, const SwString& rowId, SwJsonObject* rowOut) const;
    SwDbStatus readRows(const SwTableDbPreparedSchema& schema, SwJsonArray* rowsOut) const;
    SwDbStatus queryRows(const SwTableDbPreparedSchema& schema, const SwTableQuery& query, SwTableQueryResult* outResult) const;
    SwDbStatus replaceRows(const SwTableDbPreparedSchema& schema, const SwJsonArray& rows);
    SwDbStatus upsertRows(const SwTableDbPreparedSchema& schema, const SwJsonArray& rows);
    SwDbStatus applyRows(const SwTableDbPreparedSchema& schema, const SwJsonArray& rows,
                         const SwList<SwString>& eraseKeys);

private:
    friend struct swTableDbDetail::OwnedRowsAccess_;
    SwDbStatus writeRowsOwned_(const SwTableDbPreparedSchema& schema, SwJsonArray&& rows,
                               bool replace, const SwList<SwString>& eraseKeys = {});
    struct QuerySortKey_;
    using PreparedSchema_ = SwTableDbPreparedSchema::Data;
    static SwDbStatus validateExactRow_(const PreparedSchema_& schema, const SwJsonObject& input);
    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForRow_(const PreparedSchema_& schema,
                                                                   const SwJsonObject& row);
    SwDbStatus getRow_(const SwTableSchema& schema, const SwString& rowId, SwJsonObject* rowOut,
                       const PreparedSchema_* prepared) const;
    SwDbStatus getRowRecord_(const SwTableSchema& schema, const SwString& rowId, SwDbJsonRecord* recordOut,
                             const PreparedSchema_* prepared) const;
    SwDbStatus readRows_(const SwTableSchema& schema, SwJsonArray* rowsOut, const PreparedSchema_* prepared) const;
    SwDbStatus queryRows_(const SwTableSchema& schema, const SwTableQuery& query, SwTableQueryResult* outResult,
                          const PreparedSchema_* prepared) const;

    static SwByteArray rowPrefix_(const SwString& tableId);
    static SwByteArray rowPrimaryKey_(const SwString& tableId, const SwString& rowId);
    static SwByteArray rowPrimaryStart_(const SwString& tableId, const SwString& rowPrefix);
    static SwByteArray rowPrimaryEnd_(const SwString& tableId, const SwString& rowPrefix);
    static SwString nowIso_();
    static SwString nextRowId_();
    static SwByteArray jsonBytes_(const SwJsonObject& object);
    static bool parseJsonObject_(const SwByteArray& bytes, SwJsonObject& outObject);

    static bool isSupportedType_(const SwString& type);
    static bool isEmptyValue_(const SwJsonValue& value);
    static bool isBuiltInField_(const SwString& columnId);
    static bool requiresStringPrefixSupport_(const SwString& columnId, const SwString& type);

    static const SwTableColumn* findColumn_(const SwTableSchema& schema, const SwString& columnId);
    static const SwTableIndex* findIndexByColumn_(const SwTableSchema& schema, const SwString& columnId);
    static bool hasDuplicateColumns_(const SwTableSchema& schema);
    static bool hasDuplicateIndexes_(const SwTableSchema& schema);
    static SwDbStatus validateSchema_(const SwTableSchema& schema);

    static SwDbStatus coerceValue_(const SwString& columnType,
                                   const SwJsonValue& input,
                                   bool nullable,
                                   SwJsonValue& outValue);
    static SwDbStatus normalizeRowForCreate_(const SwTableSchema& schema,
                                             const SwJsonObject& input,
                                             SwJsonObject& outRow);
    static SwDbStatus validateExactRow_(const SwTableSchema& schema, const SwJsonObject& input);
    static SwDbStatus normalizeExactRow_(const SwTableSchema& schema,
                                         const SwJsonObject& input,
                                         SwJsonObject& outRow);
    SwDbStatus writeRows_(const SwTableSchema& schema, const SwJsonArray& rows, bool replace,
                          const SwList<SwString>& eraseKeys = {}, const PreparedSchema_* prepared = nullptr,
                          SwJsonArray* owned = nullptr);
    static SwString querySortField_(const SwTableSchema& schema, const SwTableQuery& query);
    static SwDbStatus normalizeRowForUpdate_(const SwTableSchema& schema,
                                             const SwJsonObject& currentRow,
                                             const SwJsonObject& patch,
                                             SwJsonObject& outRow);

    static SwString columnTypeForField_(const SwTableSchema& schema, const SwString& columnId);
    static SwDbStatus validateQuery_(const SwTableSchema& schema, const SwTableQuery& query);
    static bool matchesFilter_(const SwTableSchema& schema,
                               const SwJsonObject& row,
                               const SwTableQueryFilter& filter);
    static int compareFieldValues_(const SwTableSchema& schema,
                                   const SwString& columnId,
                                   const SwJsonValue& lhs,
                                   const SwJsonValue& rhs);
    static SwJsonValue sortValueForRow_(const SwJsonObject& row, const SwString& sortBy);
    static SwString makeCursor_(const SwJsonValue& sortValue, const SwString& rowId);
    static bool parseCursor_(const SwString& cursor, SwJsonValue& sortValueOut, SwString& rowIdOut);

    static SwByteArray encodeIndexValue_(const SwString& columnType, const SwJsonValue& value, bool* okOut = nullptr);
    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForRow_(const SwTableSchema& schema,
                                                                     const SwJsonObject& row);
    static SwDbStatus buildMigratedRow_(const SwTableSchema& currentSchema,
                                        const SwTableSchema& nextSchema,
                                        const SwTableMigrationPlan& plan,
                                        const SwJsonObject& currentRow,
                                        SwJsonObject& outRow);

    mutable SwEmbeddedDb db_;
    mutable SwMutex mutex_;
    SwString storageDir_ = "tabledb";
    bool opened_ = false;
};

#include "tabledb/SwTableDbImpl.h"
