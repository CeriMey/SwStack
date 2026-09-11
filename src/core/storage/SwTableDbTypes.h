#pragma once

#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwJsonValue.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwString.h"

struct SwTableColumn {
    SwString columnId;
    SwString name;
    SwString type = "string";
    bool required = false;
    bool nullable = true;
    SwJsonValue defaultValue;
};

struct SwTableIndex {
    SwString indexId;
    SwString columnId;
    bool exact = true;
    bool prefix = true;
    bool sortable = true;
};

enum class SwTableRowMode {
    Managed, // Generated rowId and timestamps, with the existing column coercion.
    Exact    // Caller-owned string primary key and JSON values; no added fields.
};

struct SwTableSchema {
    SwString tableId;
    SwString name;
    SwList<SwTableColumn> columns;
    SwList<SwTableIndex> indexes;
    SwTableRowMode rowMode = SwTableRowMode::Managed;
    SwString primaryKey = "rowId";
};

struct SwTableQueryFilter {
    SwString columnId;
    SwString op = "eq";
    SwJsonValue value;
};

struct SwTableQuery {
    int limit = 50;
    SwString cursor;
    // Empty selects updatedAt for managed rows, the primary key for exact rows.
    SwString sortBy;
    SwString sortDirection = "desc";
    SwList<SwTableQueryFilter> filters;
};

struct SwTableQueryResult {
    SwList<SwJsonObject> rows;
    SwString nextCursor;
    bool hasMore = false;
};

struct SwTableMigrationPlan {
    SwMap<SwString, SwString> renamedColumns;
};
