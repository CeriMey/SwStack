#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "SwCoreApplication.h"
#include "SwCrashHandler.h"
#include "SwDir.h"
#include "SwStandardLocation.h"
#include "SwTableDb.h"
#include "platform/SwDbPlatform.h"

namespace {

void ensureCrashDumpsEnabled_() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "SW_CRASH_DUMPS") == 0 && value) {
        std::free(value);
        return;
    }
    _putenv_s("SW_CRASH_DUMPS", "1");
#else
    const char* value = std::getenv("SW_CRASH_DUMPS");
    if (value) {
        return;
    }
    (void)setenv("SW_CRASH_DUMPS", "1", 0);
#endif
}

struct TestRunner_ {
    int failures = 0;

    bool expect(bool condition, const std::string& name, const std::string& detail = std::string()) {
        std::cout << "[TEST] " << name << " -> " << (condition ? "PASS" : "FAIL");
        if (!detail.empty()) {
            std::cout << " (" << detail << ")";
        }
        std::cout << std::endl;
        if (!condition) {
            ++failures;
        }
        return condition;
    }
};

SwString makeTempRoot_(const SwString& prefix) {
    static unsigned long long counter = 0;
    ++counter;
    const unsigned long long token =
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    return swDbPlatform::joinPath(
        SwStandardLocation::standardLocation(SwStandardLocationId::Temp),
        prefix + "-" + SwString::number(static_cast<long long>(token + counter)));
}

SwEmbeddedDbOptions tableDbOptions_(const SwString& rootPath) {
    SwEmbeddedDbOptions options;
    options.dbPath = swDbPlatform::joinPath(rootPath, "db");
    options.commitWindowMs = 0;
    options.memTableBytes = 8ull * 1024ull;
    options.inlineBlobThresholdBytes = 256ull;
    options.enableShmNotifications = false;
    return options;
}

SwTableColumn makeColumn_(const SwString& columnId,
                          const SwString& type,
                          bool required = false,
                          bool nullable = true,
                          const SwJsonValue& defaultValue = SwJsonValue()) {
    SwTableColumn column;
    column.columnId = columnId;
    column.name = columnId;
    column.type = type;
    column.required = required;
    column.nullable = nullable;
    column.defaultValue = defaultValue;
    return column;
}

SwTableIndex makeIndex_(const SwString& indexId,
                        const SwString& columnId,
                        bool exact = true,
                        bool prefix = true,
                        bool sortable = true) {
    SwTableIndex index;
    index.indexId = indexId;
    index.columnId = columnId;
    index.exact = exact;
    index.prefix = prefix;
    index.sortable = sortable;
    return index;
}

SwTableSchema makePeopleSchema_() {
    SwTableSchema schema;
    schema.tableId = "people";
    schema.name = "People";
    schema.columns.append(makeColumn_("email", "string", true, false));
    schema.columns.append(makeColumn_("age", "integer", true, false));
    schema.columns.append(makeColumn_("score", "number", true, false));
    schema.columns.append(makeColumn_("active", "boolean", true, false));
    schema.columns.append(makeColumn_("joinedAt", "datetime", true, false));
    schema.columns.append(makeColumn_("profile", "json", false, true));
    schema.indexes.append(makeIndex_("people.email", "email", true, true, true));
    schema.indexes.append(makeIndex_("people.age", "age", true, false, true));
    schema.indexes.append(makeIndex_("people.joinedAt", "joinedAt", true, true, true));
    return schema;
}

SwJsonObject makeProfile_(const std::string& tier, bool flagged) {
    SwJsonObject profile;
    profile["tier"] = tier;
    profile["flagged"] = flagged;
    return profile;
}

SwJsonObject makePerson_(const std::string& email,
                         const SwJsonValue& age,
                         const SwJsonValue& score,
                         const SwJsonValue& active,
                         const std::string& joinedAt,
                         const SwJsonObject& profile) {
    SwJsonObject row;
    row["email"] = email;
    row["age"] = age;
    row["score"] = score;
    row["active"] = active;
    row["joinedAt"] = joinedAt;
    row["profile"] = profile;
    return row;
}

SwJsonObject makeInventoryRow_(const std::string& code,
                               const SwJsonValue& quantity,
                               const SwJsonValue& legacyValue,
                               const SwJsonValue& optionalNote = SwJsonValue()) {
    SwJsonObject row;
    row["code"] = code;
    row["quantity"] = quantity;
    row["legacyValue"] = legacyValue;
    if (!optionalNote.isNull()) {
        row["optionalNote"] = optionalNote;
    }
    return row;
}

std::string describeEmails_(const SwList<SwJsonObject>& rows) {
    std::string joined;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!joined.empty()) {
            joined += ",";
        }
        joined += rows[i].value("email").toString();
    }
    return joined;
}

SwTableSchema makeInventorySchema_() {
    SwTableSchema schema;
    schema.tableId = "inventory";
    schema.name = "Inventory";
    schema.columns.append(makeColumn_("code", "string", true, false));
    schema.columns.append(makeColumn_("quantity", "integer", true, false));
    schema.columns.append(makeColumn_("optionalNote", "string", false, true));
    schema.columns.append(makeColumn_("legacyValue", "string", false, true));
    schema.indexes.append(makeIndex_("inventory.code", "code", true, true, true));
    schema.indexes.append(makeIndex_("inventory.quantity", "quantity", true, false, true));
    return schema;
}

SwTableSchema schemaWithAddedCategory_(const SwTableSchema& current) {
    SwTableSchema schema = current;
    schema.columns.append(makeColumn_("category", "string", false, false, SwJsonValue("misc")));
    return schema;
}

SwTableSchema schemaWithRenamedLegacyLabel_(const SwTableSchema& current) {
    SwTableSchema schema;
    schema.tableId = current.tableId;
    schema.name = current.name;
    schema.columns.append(makeColumn_("code", "string", true, false));
    schema.columns.append(makeColumn_("quantity", "integer", true, false));
    schema.columns.append(makeColumn_("optionalNote", "string", false, true));
    schema.columns.append(makeColumn_("legacyLabel", "string", false, true));
    schema.columns.append(makeColumn_("category", "string", false, false, SwJsonValue("misc")));
    schema.indexes.append(makeIndex_("inventory.code", "code", true, true, true));
    schema.indexes.append(makeIndex_("inventory.quantity", "quantity", true, false, true));
    return schema;
}

SwTableSchema schemaWithoutOptionalNote_(const SwTableSchema& current) {
    SwTableSchema schema;
    schema.tableId = current.tableId;
    schema.name = current.name;
    schema.columns.append(makeColumn_("code", "string", true, false));
    schema.columns.append(makeColumn_("quantity", "integer", true, false));
    schema.columns.append(makeColumn_("legacyLabel", "string", false, true));
    schema.columns.append(makeColumn_("category", "string", false, false, SwJsonValue("misc")));
    schema.indexes.append(makeIndex_("inventory.code", "code", true, true, true));
    schema.indexes.append(makeIndex_("inventory.quantity", "quantity", true, false, true));
    return schema;
}

SwTableSchema schemaWithLegacyInteger_(const SwTableSchema& current) {
    SwTableSchema schema = current;
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        if (schema.columns[i].columnId == "legacyLabel") {
            schema.columns[i].type = "integer";
        }
    }
    return schema;
}

bool openTableDb_(SwTableDb& db, const SwString& rootPath, TestRunner_& runner, const std::string& label) {
    (void)SwDir::removeRecursively(rootPath);
    (void)SwDir::mkpathAbsolute(rootPath);
    const SwDbStatus status = db.open(tableDbOptions_(rootPath));
    return runner.expect(status.ok(), label, status.message().toStdString());
}

void runTypedCrudAndQueryTests_(TestRunner_& runner) {
    const SwString rootPath = makeTempRoot_("SwTableDbTypedSelfTest");
    SwTableDb db;
    if (!openTableDb_(db, rootPath, runner, "Open typed table database")) {
        return;
    }

    const SwTableSchema schema = makePeopleSchema_();
    runner.expect(SwTableDb::validateSchema(schema).ok(), "Validate typed schema");

    SwTableSchema invalidSchema = schema;
    invalidSchema.indexes.append(makeIndex_("people.profile", "profile", true, false, false));
    const SwDbStatus invalidSchemaStatus = SwTableDb::validateSchema(invalidSchema);
    runner.expect(invalidSchemaStatus.code() == SwDbStatus::InvalidArgument,
                  "Reject JSON indexed schema",
                  invalidSchemaStatus.message().toStdString());

    SwJsonObject createdAlice;
    const SwDbStatus insertAliceStatus = db.insertRow(
        schema,
        makePerson_("alice@example.com", "41", "98.5", "true", "2026-04-04T10:00:00Z", makeProfile_("gold", true)),
        &createdAlice);
    runner.expect(insertAliceStatus.ok(), "Insert typed row with coercion", insertAliceStatus.message().toStdString());
    runner.expect(createdAlice.contains("rowId") && !createdAlice.value("rowId").toString().empty(),
                  "Created row gets server rowId");
    runner.expect(createdAlice.value("age").isInt() && createdAlice.value("age").toInteger() == 41,
                  "Integer column coerces from string");
    runner.expect(createdAlice.value("score").toDouble() == 98.5, "Number column coerces from string");
    runner.expect(createdAlice.value("active").toBool(false), "Boolean column coerces from string");
    runner.expect(createdAlice.value("joinedAt").toString() == "2026-04-04T10:00:00Z",
                  "Datetime column keeps ISO string");
    runner.expect(createdAlice.value("profile").isObject() &&
                      createdAlice.value("profile").toObject().value("tier").toString() == "gold",
                  "JSON column keeps object payload");

    SwJsonObject createdAlbert;
    const SwDbStatus insertAlbertStatus = db.insertRow(
        schema,
        makePerson_("albert@example.com", 36, 88.25, true, "2026-04-04T09:00:00Z", makeProfile_("silver", false)),
        &createdAlbert);
    runner.expect(insertAlbertStatus.ok(), "Insert second typed row", insertAlbertStatus.message().toStdString());

    SwJsonObject createdBruno;
    const SwDbStatus insertBrunoStatus = db.insertRow(
        schema,
        makePerson_("bruno@example.com", 52, 71.0, false, "2026-04-04T08:00:00Z", makeProfile_("bronze", false)),
        &createdBruno);
    runner.expect(insertBrunoStatus.ok(), "Insert third typed row", insertBrunoStatus.message().toStdString());

    const SwDbStatus invalidIntegerStatus = db.insertRow(
        schema,
        makePerson_("broken@example.com",
                    SwJsonValue(makeProfile_("broken", false)),
                    12.0,
                    true,
                    "2026-04-04T07:00:00Z",
                    makeProfile_("broken", false)),
        nullptr);
    runner.expect(!invalidIntegerStatus.ok(),
                  "Reject incompatible integer coercion",
                  invalidIntegerStatus.message().toStdString());

    SwTableQuery exactQuery;
    exactQuery.limit = 5;
    exactQuery.sortBy = "email";
    exactQuery.sortDirection = "asc";
    SwTableQueryFilter exactFilter;
    exactFilter.columnId = "email";
    exactFilter.op = "eq";
    exactFilter.value = "alice@example.com";
    exactQuery.filters.append(exactFilter);
    SwTableQueryResult exactResult;
    const SwDbStatus exactStatus = db.queryRows(schema, exactQuery, &exactResult);
    runner.expect(exactStatus.ok(), "Exact indexed query succeeds", exactStatus.message().toStdString());
    runner.expect(exactResult.rows.size() == 1 &&
                      exactResult.rows[0].value("email").toString() == "alice@example.com",
                  "Exact indexed query returns Alice");

    SwTableQuery prefixQuery;
    prefixQuery.limit = 1;
    prefixQuery.sortBy = "email";
    prefixQuery.sortDirection = "asc";
    SwTableQueryFilter prefixFilter;
    prefixFilter.columnId = "email";
    prefixFilter.op = "prefix";
    prefixFilter.value = "al";
    prefixQuery.filters.append(prefixFilter);
    SwTableQueryResult prefixPageOne;
    const SwDbStatus prefixStatus = db.queryRows(schema, prefixQuery, &prefixPageOne);
    runner.expect(prefixStatus.ok(), "Prefix indexed query succeeds", prefixStatus.message().toStdString());
    runner.expect(prefixPageOne.rows.size() == 1 &&
                      prefixPageOne.rows[0].value("email").toString() == "albert@example.com",
                  "Prefix query first page respects sort order");
    runner.expect(prefixPageOne.hasMore && !prefixPageOne.nextCursor.isEmpty(),
                  "Prefix query paginates with cursor");

    prefixQuery.cursor = prefixPageOne.nextCursor;
    SwTableQueryResult prefixPageTwo;
    const SwDbStatus prefixPageTwoStatus = db.queryRows(schema, prefixQuery, &prefixPageTwo);
    runner.expect(prefixPageTwoStatus.ok(), "Prefix query second page succeeds", prefixPageTwoStatus.message().toStdString());
    runner.expect(prefixPageTwo.rows.size() == 1 &&
                      prefixPageTwo.rows[0].value("email").toString() == "alice@example.com" &&
                      !prefixPageTwo.hasMore,
                  "Prefix query cursor returns remaining row");

    SwTableQuery sortQuery;
    sortQuery.limit = 3;
    sortQuery.sortBy = "age";
    sortQuery.sortDirection = "desc";
    SwTableQueryFilter sortFilter;
    sortFilter.columnId = "joinedAt";
    sortFilter.op = "prefix";
    sortFilter.value = "2026-04-04T";
    sortQuery.filters.append(sortFilter);
    SwTableQueryResult sortResult;
    const SwDbStatus sortStatus = db.queryRows(schema, sortQuery, &sortResult);
    runner.expect(sortStatus.ok(), "Sortable index query succeeds", sortStatus.message().toStdString());
    const bool ageDescending = sortResult.rows.size() == 3 &&
                               sortResult.rows[0].value("age").toInteger() >= sortResult.rows[1].value("age").toInteger() &&
                               sortResult.rows[1].value("age").toInteger() >= sortResult.rows[2].value("age").toInteger() &&
                               sortResult.rows[0].value("email").toString() == "bruno@example.com";
    runner.expect(ageDescending,
                  "Sortable index orders rows by indexed integer",
                  describeEmails_(sortResult.rows));

    SwTableQuery invalidFilterQuery;
    invalidFilterQuery.limit = 5;
    invalidFilterQuery.sortBy = "email";
    SwTableQueryFilter invalidFilter;
    invalidFilter.columnId = "profile";
    invalidFilter.op = "eq";
    invalidFilter.value = "gold";
    invalidFilterQuery.filters.append(invalidFilter);
    SwTableQueryResult invalidFilterResult;
    const SwDbStatus invalidFilterStatus = db.queryRows(schema, invalidFilterQuery, &invalidFilterResult);
    runner.expect(invalidFilterStatus.code() == SwDbStatus::InvalidArgument,
                  "Reject query on non indexed column",
                  invalidFilterStatus.message().toStdString());

    db.close();
    (void)SwDir::removeRecursively(rootPath);
}

void runMigrationTests_(TestRunner_& runner) {
    {
        const SwString rootPath = makeTempRoot_("SwTableDbMigrationSelfTest");
        SwTableDb db;
        if (!openTableDb_(db, rootPath, runner, "Open migration table database")) {
            return;
        }

        const SwTableSchema baseSchema = makeInventorySchema_();
        SwJsonObject createdRow;
        const SwDbStatus insertStatus =
            db.insertRow(baseSchema, makeInventoryRow_("item-1", "7", "12"), &createdRow);
        runner.expect(insertStatus.ok(), "Insert migration baseline row", insertStatus.message().toStdString());
        const SwString rowId = createdRow.value("rowId").toString().c_str();

        const SwTableSchema addedSchema = schemaWithAddedCategory_(baseSchema);
        const SwDbStatus addColumnStatus = db.migrateTable(baseSchema, addedSchema);
        runner.expect(addColumnStatus.ok(), "Add column migration succeeds", addColumnStatus.message().toStdString());

        SwJsonObject addedRow;
        const SwDbStatus addedGetStatus = db.getRow(addedSchema, rowId, &addedRow);
        runner.expect(addedGetStatus.ok(), "Read row after add column migration", addedGetStatus.message().toStdString());
        runner.expect(addedRow.value("category").toString() == "misc", "Added column uses default value");

        const SwTableSchema renamedSchema = schemaWithRenamedLegacyLabel_(addedSchema);
        SwTableMigrationPlan renamePlan;
        renamePlan.renamedColumns["legacyValue"] = "legacyLabel";
        const SwDbStatus renameStatus = db.migrateTable(addedSchema, renamedSchema, renamePlan);
        runner.expect(renameStatus.ok(), "Rename column migration succeeds", renameStatus.message().toStdString());

        SwJsonObject renamedRow;
        const SwDbStatus renamedGetStatus = db.getRow(renamedSchema, rowId, &renamedRow);
        runner.expect(renamedGetStatus.ok(), "Read row after rename migration", renamedGetStatus.message().toStdString());
        runner.expect(!renamedRow.contains("legacyValue") &&
                          renamedRow.value("legacyLabel").toString() == "12",
                      "Rename migration moves stored value");

        const SwTableSchema droppedEmptySchema = schemaWithoutOptionalNote_(renamedSchema);
        const SwDbStatus dropEmptyStatus = db.migrateTable(renamedSchema, droppedEmptySchema);
        runner.expect(dropEmptyStatus.ok(), "Drop empty column migration succeeds", dropEmptyStatus.message().toStdString());

        SwJsonObject droppedEmptyRow;
        const SwDbStatus droppedEmptyGetStatus = db.getRow(droppedEmptySchema, rowId, &droppedEmptyRow);
        runner.expect(droppedEmptyGetStatus.ok(),
                      "Read row after drop empty migration",
                      droppedEmptyGetStatus.message().toStdString());
        runner.expect(!droppedEmptyRow.contains("optionalNote"), "Dropped empty column disappears from row");

        const SwTableSchema compatibleTypeSchema = schemaWithLegacyInteger_(droppedEmptySchema);
        const SwDbStatus compatibleTypeStatus = db.migrateTable(droppedEmptySchema, compatibleTypeSchema);
        runner.expect(compatibleTypeStatus.ok(),
                      "Compatible type migration succeeds",
                      compatibleTypeStatus.message().toStdString());

        SwJsonObject compatibleTypeRow;
        const SwDbStatus compatibleTypeGetStatus = db.getRow(compatibleTypeSchema, rowId, &compatibleTypeRow);
        runner.expect(compatibleTypeGetStatus.ok(),
                      "Read row after compatible type migration",
                      compatibleTypeGetStatus.message().toStdString());
        runner.expect(compatibleTypeRow.value("legacyLabel").isInt() &&
                          compatibleTypeRow.value("legacyLabel").toInteger() == 12,
                      "Compatible type migration coerces persisted value");

        db.close();
        (void)SwDir::removeRecursively(rootPath);
    }

    {
        const SwString rootPath = makeTempRoot_("SwTableDbDropRejectSelfTest");
        SwTableDb db;
        if (!openTableDb_(db, rootPath, runner, "Open drop rejection database")) {
            return;
        }

        SwTableSchema currentSchema;
        currentSchema.tableId = "orders";
        currentSchema.name = "Orders";
        currentSchema.columns.append(makeColumn_("code", "string", true, false));
        currentSchema.columns.append(makeColumn_("status", "string", false, true));
        currentSchema.indexes.append(makeIndex_("orders.code", "code", true, true, true));

        SwJsonObject row;
        row["code"] = "order-1";
        row["status"] = "live";
        const SwDbStatus insertStatus = db.insertRow(currentSchema, row, nullptr);
        runner.expect(insertStatus.ok(), "Insert non empty drop baseline row", insertStatus.message().toStdString());

        SwTableSchema nextSchema;
        nextSchema.tableId = "orders";
        nextSchema.name = "Orders";
        nextSchema.columns.append(makeColumn_("code", "string", true, false));
        nextSchema.indexes.append(makeIndex_("orders.code", "code", true, true, true));

        const SwDbStatus dropNonEmptyStatus = db.migrateTable(currentSchema, nextSchema);
        runner.expect(dropNonEmptyStatus.code() == SwDbStatus::InvalidArgument,
                      "Reject dropping non empty column",
                      dropNonEmptyStatus.message().toStdString());

        db.close();
        (void)SwDir::removeRecursively(rootPath);
    }

    {
        const SwString rootPath = makeTempRoot_("SwTableDbTypeRejectSelfTest");
        SwTableDb db;
        if (!openTableDb_(db, rootPath, runner, "Open type rejection database")) {
            return;
        }

        const SwTableSchema currentSchema = makeInventorySchema_();
        const SwDbStatus insertStatus =
            db.insertRow(currentSchema, makeInventoryRow_("item-2", 5, "not-a-number"), nullptr);
        runner.expect(insertStatus.ok(), "Insert incompatible type baseline row", insertStatus.message().toStdString());

        SwTableSchema renamedSchema = schemaWithRenamedLegacyLabel_(schemaWithAddedCategory_(currentSchema));
        SwTableMigrationPlan renamePlan;
        renamePlan.renamedColumns["legacyValue"] = "legacyLabel";
        const SwDbStatus addColumnStatus = db.migrateTable(currentSchema, schemaWithAddedCategory_(currentSchema));
        runner.expect(addColumnStatus.ok(), "Prepare incompatible type migration add column", addColumnStatus.message().toStdString());
        const SwDbStatus renameStatus =
            db.migrateTable(schemaWithAddedCategory_(currentSchema), renamedSchema, renamePlan);
        runner.expect(renameStatus.ok(), "Prepare incompatible type migration rename column", renameStatus.message().toStdString());

        const SwTableSchema incompatibleTypeSchema = schemaWithLegacyInteger_(renamedSchema);
        const SwDbStatus incompatibleTypeStatus = db.migrateTable(renamedSchema, incompatibleTypeSchema);
        runner.expect(incompatibleTypeStatus.code() == SwDbStatus::InvalidArgument,
                      "Reject incompatible type migration",
                      incompatibleTypeStatus.message().toStdString());

        db.close();
        (void)SwDir::removeRecursively(rootPath);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);
    (void)app;
    ensureCrashDumpsEnabled_();
    SwCrashHandler::install("SwTableDbSelfTest");

    TestRunner_ runner;
    runTypedCrudAndQueryTests_(runner);
    runMigrationTests_(runner);

    if (runner.failures != 0) {
        std::cerr << "SwTableDbSelfTest failed with " << runner.failures << " failing checks" << std::endl;
        return 1;
    }

    std::cout << "SwTableDbSelfTest passed" << std::endl;
    return 0;
}
