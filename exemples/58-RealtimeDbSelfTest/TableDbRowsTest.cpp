#include "SwTableDb.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void success(const SwDbStatus& status) {
    if (!status.ok()) throw std::runtime_error(status.message().toStdString());
}
SwJsonObject object(std::initializer_list<std::pair<const char*, SwJsonValue>> values) {
    SwJsonObject result;
    for (const auto& value : values) result[value.first] = value.second;
    return result;
}
SwJsonArray array(std::initializer_list<SwJsonValue> values) {
    SwJsonArray result;
    for (const auto& value : values) result.append(value);
    return result;
}
SwTableColumn column(const char* name, const char* type) {
    SwTableColumn result;
    result.columnId = name;
    result.name = name;
    result.type = type;
    result.required = true;
    result.nullable = false;
    return result;
}
SwTableSchema schema(const char* name = "rt.data.gimbal") {
    SwTableSchema result;
    result.tableId = name;
    result.rowMode = SwTableRowMode::Exact;
    result.primaryKey = "id";
    result.columns.append(column("id", "string"));
    result.columns.append(column("value", "number"));
    result.columns.append(column("active", "boolean"));
    result.columns.append(column("payload", "json"));
    SwTableIndex index;
    index.indexId = "value";
    index.columnId = "value";
    index.prefix = false;
    result.indexes.append(index);
    return result;
}
SwJsonObject row(const char* id, int value) {
    return object({{"id", id}, {"value", value}, {"active", true},
                   {"payload", object({{"nested", array({false, 4, "x"})}})}});
}
SwJsonArray read(SwTableDb& db, const SwTableSchema& definition) {
    SwJsonArray result;
    success(db.readRows(definition, &result));
    return result;
}

void exerciseExactRows(SwTableDb& db) {
    const auto definition = schema();
    require(read(db, definition).isEmpty(), "new exact table not empty");
    const auto first = row(" head ", 1);
    success(db.replaceRows(definition, array({first, row("z", 2)})));
    const auto before = read(db, definition);
    require(before[0] == SwJsonValue(first), "exact row was transformed or decorated");
    SwJsonObject found;
    success(db.getRow(definition, " head ", &found));
    require(found == first, "explicit primary key failed");
    require(db.getRow(definition, "head", &found).code() == SwDbStatus::NotFound, "exact keys were trimmed");
    require(!db.insertRow(definition, first).ok(), "insert overwrote an existing explicit key");

    auto wrongType = row("new", 7);
    wrongType["value"] = "7";
    require(!db.replaceRows(definition, array({row(" head ", 9), wrongType})).ok(), "string coerced to exact number");
    require(read(db, definition) == before, "failed replacement committed a partial batch");
    require(!db.upsertRows(definition, array({first, first})).ok(), "duplicate batch key accepted");
    require(read(db, definition) == before, "duplicate batch altered rows");
    const SwJsonValue missingObject{std::shared_ptr<SwJsonObject>()};
    require(!db.replaceRows(definition, array({row(" head ", 9), missingObject})).ok(),
            "null object row pointer accepted");
    require(read(db, definition) == before, "null object row partially changed the table");
    auto nonFinite = row("bad", 1);
    nonFinite["payload"] = array({std::numeric_limits<double>::infinity()});
    require(!db.replaceRows(definition, array({nonFinite})).ok(), "nested non-finite JSON accepted");
    auto undeclared = row("bad", 1);
    undeclared["extra"] = true;
    require(!db.replaceRows(definition, array({undeclared})).ok(), "undeclared column accepted");
    auto missing = first;
    missing.remove("id");
    require(!db.replaceRows(definition, array({missing})).ok(), "missing primary key accepted");

    success(db.upsertRows(definition, array({row(" head ", 3), row("new", 4)})));
    require(read(db, definition).size() == 3, "upsert removed omitted rows");
    success(db.updateRow(definition, " head ", object({{"value", 5.5}}), &found));
    require(found["value"].toDouble() == 5.5 && !found.contains("updatedAt"), "exact update coerced value or added metadata");
    require(!db.updateRow(definition, " head ", object({{"id", "moved"}})).ok(), "patch changed primary key");

    SwTableQuery query;
    query.sortDirection = "asc";
    query.limit = 2;
    SwTableQueryResult page;
    success(db.queryRows(definition, query, &page));
    require(page.rows.size() == 2 && page.rows[0]["id"].toString() == " head " && page.hasMore,
            "exact primary-key query or pagination failed");
    query.cursor = page.nextCursor;
    success(db.queryRows(definition, query, &page));
    require(page.rows.size() == 1 && page.rows[0]["id"].toString() == "z", "exact query cursor lost row");
    query.cursor.clear();
    query.sortBy = "value";
    success(db.queryRows(definition, query, &page));
    require(page.rows[0]["id"].toString() == "z", "exact secondary index sort failed");

    success(db.replaceRows(definition, array({row(" head ", 8)})));
    require(read(db, definition).size() == 1, "replace retained omitted rows");
    success(db.deleteRow(definition, " head "));
    require(read(db, definition).isEmpty(), "exact delete trimmed the key");
    success(db.replaceRows(definition, array({row("a", 1)})));
    success(db.replaceRows(definition, {}));
    require(read(db, definition).isEmpty(), "empty replacement did not clear table");

    auto namedFields = schema("rt.data.fields");
    namedFields.primaryKey = "rowId";
    namedFields.columns[0].columnId = "rowId";
    namedFields.columns.append(column("updatedAt", "number"));
    auto ownMetadata = row("unused", 4);
    ownMetadata.remove("id");
    ownMetadata["rowId"] = "business-key";
    ownMetadata["updatedAt"] = 123;
    success(db.replaceRows(namedFields, array({ownMetadata})));
    require(read(db, namedFields)[0] == SwJsonValue(ownMetadata), "exact mode reserved or rewrote caller metadata fields");
}

void opaqueRows(SwTableDb& db) {
    const auto definition = schema("opaque.rows");
    SwTableDbPreparedSchema prepared;
    success(SwTableDb::prepareSchema(definition, &prepared));
    auto original = row("a", 7);
    success(db.upsertRows(prepared, array({original})));
    SwDbJsonRecord record, unprepared;
    success(db.getRowRecord(prepared, "a", &record));
    success(db.getRowRecord(definition, "a", &unprepared));
    require(record.equals(original) && unprepared.equals(original), "prepared record lookup differs");
    const auto detached = record.detach();
    (*detached["payload"].toObjectPtr())["nested"] = 100;
    require(record.equals(original), "table record detach shared its mutable payload");
    success(db.applyRows(prepared, array({row("b", 8)}), {"a"}));
    require(record.equals(original) && db.getRowRecord(prepared, "a", &unprepared).code() == SwDbStatus::NotFound,
            "table record did not preserve its old value across atomic deletion");
    require(unprepared.equals(original), "missing record read replaced the caller's handle");
    success(db.clearTable(definition));
}

void jsonContainerOwnership() {
    auto array = std::make_shared<SwJsonArray>();
    array->append(true);
    std::weak_ptr<SwJsonArray> oldArray = array;
    SwJsonValue value(array);
    array.reset();
    value = object({{"value", 7}});
    require(oldArray.expired() && !value.toArrayPtr(), "object assignment retained its previous array payload");
    SwJsonValue copied(value);
    (*value.toObjectPtr())["value"] = 8;
    require(copied.isObject() && !copied.toArrayPtr() && copied.toObject()["value"].toInt() == 7,
            "object copy retained hidden array data or shared mutable fields");

    std::weak_ptr<SwJsonObject> oldObject = value.toObjectPtr();
    value.setArray(std::make_shared<SwJsonArray>());
    require(oldObject.expired() && !value.toObjectPtr(), "setArray retained its previous object payload");
    value.toArrayPtr()->append(object({{"nested", 9}}));
    copied = value;
    (*value.toArrayPtr())[0] = 10;
    require(copied.isArray() && !copied.toObjectPtr() && copied.toArray()[0].toObject()["nested"].toInt() == 9,
            "array copy retained hidden object data or shared mutable fields");

    oldArray = value.toArrayPtr();
    value.setObject(std::make_shared<SwJsonObject>());
    require(oldArray.expired() && !value.toArrayPtr(), "setObject retained its previous array payload");
    oldObject = value.toObjectPtr();
    value.setArray({});
    require(oldObject.expired() && value.isArray() && !value.toObjectPtr() && !value.toArrayPtr(),
            "null array assignment retained the old object");

    value.setArray(std::make_shared<SwJsonArray>());
    value.toArrayPtr()->append(object({{"borrowed", 11}}));
    const auto& borrowed = *value.toArrayPtr()->dataRef()[0].toObjectPtr();
    value = borrowed;
    require(value.toObject()["borrowed"].toInt() == 11 && !value.toArrayPtr(),
            "object assignment invalidated its borrowed source before copying");
}

void jsonMoves() {
    auto source = object({{"child", object({{"number", 7}})}});
    const auto child = source["child"].toObjectPtr();
    SwJsonValue moved(std::move(source));
    require(moved.isValid() && moved.toObjectPtr()->value("child").toObject()["number"].toInt() == 7 &&
            (*moved.toObjectPtr())["child"].toObjectPtr() == child, "object move cloned or lost its child");
    source["reused"] = true;
    require(!moved.toObjectPtr()->contains("reused"), "moved-from object cannot be reused independently");
    SwJsonValue detached(moved);
    (*child)["number"] = 8;
    require(detached.toObject()["child"].toObject()["number"].toInt() == 7,
            "lvalue copy after a move shared its mutable child");

    SwJsonArray sourceArray;
    sourceArray.append(std::move(moved));
    require(moved.isNull() && moved.isValid() && sourceArray[0].toObjectPtr()->value("child").toObject()["number"].toInt() == 8,
            "append did not consume a value into a valid moved-from state");
    const auto first = sourceArray[0].toObjectPtr();
    SwJsonValue movedArray(std::move(sourceArray));
    require(movedArray.isArray() && (*movedArray.toArrayPtr())[0].toObjectPtr() == first,
            "array move cloned or lost its first object");
    sourceArray.append(false);
    require(movedArray.toArrayPtr()->size() == 1 && (*movedArray.toArrayPtr())[0].isObject(),
            "moved-from array cannot be reused independently");

    // std::vector must accept an rvalue alias of an existing element even when
    // growth relocates the vector. The original slot is left valid and null.
    SwJsonArray alias;
    alias.append(object({{"value", 11}}));
    while (alias.size() < alias.dataRef().capacity()) alias.append(false);
    const auto size = alias.size();
    const auto identity = alias[0].toObjectPtr();
    alias.append(std::move(alias[0]));
    require(alias.size() == size + 1 && alias[0].isNull() && alias[0].isValid() &&
            alias[size].toObjectPtr() == identity, "self-aliased append failed during vector growth");

    SwJsonValue root(object({{"child", array({object({{"value", 13}})})}}));
    std::weak_ptr<SwJsonObject> oldParent = root.toObjectPtr();
    auto& borrowed = (*root.toObjectPtr())["child"];
    root = std::move(borrowed);
    require(oldParent.expired() && root.isArray() && root.isValid() &&
            (*root.toArrayPtr())[0].toObject()["value"].toInt() == 13,
            "move assignment released a borrowed source before detaching it");
    root = std::move(root);
    require(root.isArray() && root.isValid() && root.toArrayPtr()->size() == 1, "self move changed JSON value");

    // Direct container moves must detach before destroying the array/object
    // that owns the borrowed container, including opposite payload types.
    const auto nested = (*root.toArrayPtr())[0].toObjectPtr();
    std::weak_ptr<SwJsonArray> oldArray = root.toArrayPtr();
    root = std::move(*nested);
    require(oldArray.expired() && root.isObject() && !root.toArrayPtr() && root.toObject()["value"].toInt() == 13,
            "borrowed object move retained old array or lost its fields");
    oldParent = root.toObjectPtr();
    root = std::move(*root.toObjectPtr());
    require(oldParent.expired() && root.isObject() && root.toObject()["value"].toInt() == 13,
            "move from own object payload failed");
    root = array({19});
    oldArray = root.toArrayPtr();
    root = std::move(*root.toArrayPtr());
    require(oldArray.expired() && root.isArray() && root.toArrayPtr()->size() == 1 &&
            (*root.toArrayPtr())[0].toInt() == 19, "move from own array payload failed");
}

void movedRowOwnership(SwTableDb& db) {
    const auto definition = schema("rt.data.moved-ownership");
    auto input = row("a", 1);
    const auto expected = input;
    const auto external = input["payload"].toObjectPtr();
    SwJsonArray inputs;
    inputs.append(std::move(input));
    success(db.replaceRows(definition, inputs));
    (*(*external)["nested"].toArrayPtr())[0] = true;
    SwJsonObject found;
    success(db.getRow(definition, "a", &found));
    require(found == expected, "moving input bypassed the storage admission deep copy");
    SwJsonValue retained(std::move(found));
    success(db.replaceRows(definition, array({row("a", 2)})));
    require(retained.toObject() == expected, "later commit changed a moved public read");
    (*retained.toObjectPtr())["value"] = 99;
    success(db.getRow(definition, "a", &found));
    require(found["value"].toInt() == 2, "moved public read exposed mutable database storage");
}

void jsonValidation(SwTableDb& db) {
    const auto definition = schema("rt.data.json_validation");
    const auto nest = [](unsigned levels, SwJsonValue value) {
        for (unsigned i = 0; i < levels; ++i) {
            if (i % 2) value = object({{"child", value}});
            else value = array({value});
        }
        return value;
    };
    auto input = row("valid", 1);
    input["payload"] = nest(63, true);
    const auto original = input;
    success(db.replaceRows(definition, array({input})));
    const auto before = read(db, definition);
    require(input == original && before == array({original}), "JSON validation changed a deeply nested exact row");
    input["payload"] = nest(64, true);
    require(!db.replaceRows(definition, array({input})).ok(), "JSON nesting beyond 64 levels accepted");
    for (const double value : {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        input["payload"] = nest(62, value);
        require(!db.upsertRows(definition, array({input})).ok(), "deeply nested non-finite row accepted");
    }
    require(read(db, definition) == before, "rejected JSON row changed stored snapshot");
    input["payload"] = object({{"object", SwJsonValue(std::shared_ptr<SwJsonObject>())},
                                {"array", SwJsonValue(std::shared_ptr<SwJsonArray>())}});
    const auto emptyInput = input.toJsonString();
    success(db.replaceRows(definition, array({input})));
    require(input.toJsonString() == emptyInput && read(db, definition)[0].toJsonString() == emptyInput,
            "null container pointers were not treated as empty containers");
}

void coherentSnapshots(SwTableDb& db) {
    const auto definition = schema("rt.data.atomic");
    success(db.replaceRows(definition, array({row("a", 0), row("b", 0)})));
    std::atomic<bool> finished(false);
    std::atomic<bool> failed(false);
    std::thread writer([&] {
        for (int i = 1; i <= 100; ++i) {
            if (!db.replaceRows(definition, array({row("a", i), row("b", i)})).ok()) failed = true;
        }
        finished = true;
    });
    do {
        SwJsonArray values;
        if (!db.readRows(definition, &values).ok() || values.size() != 2 ||
            values[0].toObject()["value"] != values[1].toObject()["value"]) failed = true;
    } while (!finished);
    writer.join();
    require(!failed, "snapshot observed a partially replaced table");
}

void atomicDeltas(SwTableDb& db) {
    const auto definition = schema("rt.data.delta");
    success(db.replaceRows(definition, array({row("a", 1), row("b", 2), row("c", 3)})));
    success(db.applyRows(definition, array({row("a", 5), row("d", 4)}), {"b"}));
    const auto expected = array({row("a", 5), row("c", 3), row("d", 4)});
    require(read(db, definition) == expected, "delta removed untouched rows or retained an erased key");
    auto invalid = row("bad", 1);
    invalid["value"] = "wrong type";
    require(!db.applyRows(definition, array({row("a", 9), invalid}), {"c"}).ok(),
            "invalid delta rows accepted");
    require(!db.applyRows(definition, array({row("a", 9)}), {"a"}).ok(),
            "delta accepted the same key in updates and erasures");
    require(!db.applyRows(definition, {}, {"a", "a"}).ok(), "delta accepted duplicate erasures");
    require(!db.applyRows(definition, array({row("a", 9)}), {""}).ok(), "delta accepted an empty erasure key");
    require(read(db, definition) == expected, "failed delta partially changed stored rows");
    success(db.applyRows(definition, {}, {"absent"}));
    require(read(db, definition) == expected, "erasing an absent key changed another row");
    SwTableQuery query;
    query.sortBy = "value";
    query.sortDirection = "asc";
    SwTableQueryResult result;
    success(db.queryRows(definition, query, &result));
    require(result.rows.size() == 3 && result.rows[0]["id"].toString() == "c" &&
            result.rows[2]["id"].toString() == "a", "delta failed to maintain secondary indexes");
}

void coherentDeltaSnapshots(SwTableDb& db) {
    const auto definition = schema("rt.data.delta_atomic");
    success(db.replaceRows(definition, array({row("a", 0), row("b", 0)})));
    std::atomic<bool> finished(false), failed(false);
    std::thread writer([&] {
        for (int i = 1; i <= 100; ++i) {
            const char* next = i % 2 ? "c" : "b";
            const char* removed = i % 2 ? "b" : "c";
            if (!db.applyRows(definition, array({row("a", i), row(next, i)}), {removed}).ok()) failed = true;
        }
        finished = true;
    });
    do {
        SwJsonArray values;
        if (!db.readRows(definition, &values).ok() || values.size() != 2 ||
            values[0].toObject()["value"] != values[1].toObject()["value"]) failed = true;
    } while (!finished);
    writer.join();
    require(!failed, "snapshot observed separate delta update and erase commits");
}

void managedDefaults(SwTableDb& db) {
    SwTableSchema definition;
    definition.tableId = "managed";
    definition.columns.append(column("value", "integer"));
    SwJsonObject created;
    success(db.insertRow(definition, object({{"value", "12"}}), &created));
    require(created["value"].isInt() && created["value"].toInt() == 12, "managed coercion changed");
    require(created.contains("rowId") && created.contains("createdAt") && created.contains("updatedAt"),
            "managed metadata defaults changed");
    SwTableQueryResult result;
    success(db.queryRows(definition, {}, &result));
    require(result.rows.size() == 1, "managed default updatedAt query changed");
}

void rowOwnership(SwTableDb& db) {
    const auto definition = schema("rt.data.ownership");
    const auto original = row("a", 1);
    auto input = array({original});
    success(db.replaceRows(definition, input));
    const auto alter = [](const SwJsonObject& value) {
        const auto payload = value["payload"].toObjectPtr();
        (*(*payload)["nested"].toArrayPtr())[0] = true;
    };
    alter(*input[0].toObjectPtr());
    SwJsonObject found;
    success(db.getRow(definition, "a", &found));
    require(found == original, "stored table row aliased caller input");
    alter(found);
    auto rows = read(db, definition);
    require(rows == array({original}), "point read exposed mutable table payload");
    alter(*rows[0].toObjectPtr());
    SwTableQuery query;
    query.sortBy = "value";
    SwTableQueryResult result;
    success(db.queryRows(definition, query, &result));
    require(result.rows.size() == 1 && result.rows[0] == original, "table scan exposed mutable table payload");
    alter(result.rows[0]);
    const auto retained = read(db, definition);
    require(retained == array({original}), "indexed query exposed mutable table payload");
    success(db.applyRows(definition, array({row("a", 2), row("b", 3)}), {}));
    require(retained == array({original}), "later commit changed a retained table read");
    success(db.clearTable(definition));
    require(read(db, definition).isEmpty() && retained == array({original}),
            "clear retained database rows or invalidated caller-owned rows");
}

void managedByteCompatibility(SwTableDb& db) {
    SwTableSchema definition;
    definition.tableId = "managed.invalid-json";
    definition.columns.append(column("value", "number"));
    // Managed mode historically accepts this coercion, then reports corrupted
    // JSON on read. Native Exact storage must not alter this unrelated policy.
    SwJsonObject created, readBack;
    success(db.insertRow(definition, object({{"value", std::numeric_limits<double>::infinity()}}), &created));
    require(db.getRow(definition, created["rowId"].toString(), &readBack).code() == SwDbStatus::Corruption,
            "native storage changed Managed non-finite byte compatibility");
    success(db.insertRow(definition, object({{"value", 1}}), &created));
    success(db.updateRow(definition, created["rowId"].toString(),
                         object({{"value", std::numeric_limits<double>::infinity()}})));
    require(db.getRow(definition, created["rowId"].toString(), &readBack).code() == SwDbStatus::Corruption,
            "native update changed Managed non-finite byte compatibility");
}

class CommaDecimal : public std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

void numberRoundTrips() {
    const auto values = array({3.141592653589793, 1e-12, 1e20, 1.0, -0.0,
        std::numeric_limits<long long>::max(), std::numeric_limits<long long>::min(), SwJsonValue()});
    const auto input = object({{"values", values}, {"pi", 3.141592653589793}});
    const auto previousLocale = std::locale();
    std::locale::global(std::locale(previousLocale, new CommaDecimal));
    const auto encodedDocument = SwJsonDocument(input).toJson();
    const auto encodedObject = input.toJsonString();
    const auto encodedArray = values.toJsonString();
    std::locale::global(previousLocale);
    SwString error;
    auto decoded = SwJsonDocument::fromJson(encodedDocument, error);
    require(error.isEmpty() && decoded.object() == input, "document JSON number round-trip lost precision or representation");
    decoded = SwJsonDocument::fromJson(encodedObject, error);
    require(error.isEmpty() && decoded.object() == input, "object JSON number round-trip lost precision or representation");
    decoded = SwJsonDocument::fromJson(encodedArray, error);
    require(error.isEmpty() && decoded.array() == values, "array JSON number round-trip lost precision or representation");
    require(std::signbit(decoded.array()[4].toDouble()), "JSON round-trip lost negative zero");
    require(values[3].type() == SwJsonValue::Type::Double && values[5].type() == SwJsonValue::Type::Integer,
            "JSON stored type accessor misclassified numbers");
}

} // namespace

int main() {
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("sw-table-exact-" + token);
    try {
        jsonContainerOwnership();
        jsonMoves();
        numberRoundTrips();
        SwEmbeddedDbOptions options;
        options.dbPath = SwString(root.string());
        options.commitWindowMs = 0;
        SwTableDb disk;
        success(disk.open(options));
        exerciseExactRows(disk);
        opaqueRows(disk);
        atomicDeltas(disk);
        managedDefaults(disk);
        rowOwnership(disk);
        movedRowOwnership(disk);
        managedByteCompatibility(disk);
        jsonValidation(disk);
        const auto persisted = schema("rt.data.persisted");
        success(disk.replaceRows(persisted, array({row("saved", 42)})));
        success(disk.applyRows(persisted, array({row("replacement", 43)}), {"saved"}));
        success(disk.sync());
        disk.close();
        success(disk.open(options));
        require(read(disk, persisted) == array({row("replacement", 43)}), "atomic delta did not survive disk reopen");
        disk.close();
        std::filesystem::remove_all(root);

        options.persistent = false;
        SwTableDb memory;
        success(memory.open(options));
        exerciseExactRows(memory);
        opaqueRows(memory);
        jsonValidation(memory);
        atomicDeltas(memory);
        coherentSnapshots(memory);
        coherentDeltaSnapshots(memory);
        managedDefaults(memory);
        rowOwnership(memory);
        movedRowOwnership(memory);
        managedByteCompatibility(memory);
        success(memory.sync());
        memory.close();
        require(!std::filesystem::exists(root), "memory table database wrote files");
        success(memory.open(options));
        require(read(memory, schema()).isEmpty(), "memory table rows survived close/reopen");
        memory.close();
        std::cout << "SwTableDb exact rows tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SwTableDb exact rows test failed: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
