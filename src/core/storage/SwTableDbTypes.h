#pragma once

#include "SwJsonObject.h"
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

struct SwTableSchema {
    SwString tableId;
    SwString name;
    SwList<SwTableColumn> columns;
    SwList<SwTableIndex> indexes;
};

struct SwTableQueryFilter {
    SwString columnId;
    SwString op = "eq";
    SwJsonValue value;
};

struct SwTableQuery {
    int limit = 50;
    SwString cursor;
    SwString sortBy = "updatedAt";
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
