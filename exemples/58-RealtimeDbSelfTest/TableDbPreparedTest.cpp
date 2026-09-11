#include <core/storage/SwTableDb.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void success(const SwDbStatus& status) { if (!status.ok()) throw std::runtime_error(status.message().toStdString()); }
SwJsonArray rows(std::initializer_list<SwJsonObject> values) {
    SwJsonArray result; for (const auto& value : values) result.append(value); return result;
}
SwTableSchema schema(const SwString& name) {
    SwTableSchema result; result.tableId = name; result.rowMode = SwTableRowMode::Exact; result.primaryKey = "id";
    SwTableColumn id; id.columnId = "id"; id.type = "string"; id.required = true; id.nullable = false;
    result.columns.append(id);
    auto score = id; score.columnId = "score"; score.type = " INTEGER "; result.columns.append(score);
    auto payload = id; payload.columnId = "payload"; payload.type = "json"; result.columns.append(payload);
    auto note = id; note.columnId = "note"; note.required = false; note.nullable = true; result.columns.append(note);
    SwTableIndex index; index.columnId = "score"; index.indexId = "by-score"; result.indexes.append(index);
    return result;
}
SwJsonObject row(const SwString& key, int score) {
    SwJsonObject result, payload; payload["value"] = score;
    result["id"] = key; result["score"] = score; result["payload"] = payload; return result;
}
SwTableDbPreparedSchema prepare(const SwTableSchema& definition) {
    SwTableDbPreparedSchema prepared; success(SwTableDb::prepareSchema(definition, &prepared)); return prepared;
}
void verify(SwTableDb& db) {
    const auto original = schema("prepared");
    auto source = original;
    auto prepared = prepare(source);
    auto retained = prepared; prepared = {};
    source.tableId = "changed"; source.columns[1].type = "boolean"; source.indexes[0].columnId = "missing";
    require(retained.isValid() && !prepared.isValid(), "Prepared handle lifetime is tied to the original handle");
    success(db.replaceRows(retained, rows({row("b",2), row("a",1)})));
    SwJsonObject found; success(db.getRow(original,"a",&found));
    require(found == row("a",1), "Preparing a schema changed Exact row data or table identity");
    require(!db.getRow(source,"a",&found).ok(), "Mutable schema API skipped its current validation");
    success(db.getRow(retained,"a",&found));
    (*found["payload"].toObjectPtr())["value"] = 90;
    success(db.getRow(retained,"a",&found));
    require(found == row("a",1), "Prepared read exposed mutable storage");
    auto input = row("c",3); auto alias = input["payload"].toObjectPtr();
    success(db.upsertRows(retained,rows({input}))); (*alias)["value"] = 91;
    success(db.getRow(retained,"c",&found)); require(found == row("c",3), "Prepared write retained an input alias");

    SwJsonArray before, actual; success(db.readRows(original,&before)); success(db.readRows(retained,&actual));
    require(actual == before, "Prepared snapshot differs from ordinary snapshot");
    auto invalid = row("bad",4); invalid["extra"] = true;
    require(!db.applyRows(retained,rows({row("a",8),invalid}),{}).ok(), "Invalid prepared batch committed a prefix");
    success(db.readRows(retained,&actual)); require(actual == before, "Invalid prepared batch changed values");
    SwList<SwString> erased; erased.append("a");
    require(!db.applyRows(retained,rows({row("a",9)}),erased).ok(), "Duplicate write/erase key was accepted");
    success(db.readRows(retained,&actual)); require(actual == before, "Duplicate key changed rows");

    // Existing and prepared writes must agree on numeric/null/key/depth checks.
    const auto reference = schema("reference");
    std::vector<SwJsonObject> candidates;
    auto candidate = row("x",1); candidate["score"] = 3.0; candidates.push_back(candidate);
    candidate["score"] = std::numeric_limits<long long>::max(); candidates.push_back(candidate);
    candidate["score"] = std::ldexp(1.0,63); candidates.push_back(candidate);
    candidate["score"] = 1.5; candidates.push_back(candidate);
    candidate = row("x",1); candidate["score"] = "1"; candidates.push_back(candidate);
    candidate = row("x",1); candidate["note"] = SwJsonValue(); candidates.push_back(candidate);
    candidate = row("x",1); candidate["payload"] = SwJsonValue(); candidates.push_back(candidate);
    candidate = row("x",1); candidate["id"] = ""; candidates.push_back(candidate);
    candidate = row("x",1); candidate["payload"] = std::numeric_limits<double>::infinity(); candidates.push_back(candidate);
    candidate = row("x",1); candidate.remove("score"); candidates.push_back(candidate);
    candidate = row("x",1); candidate["extra"] = 0; candidates.push_back(candidate);
    SwJsonObject nested; nested["value"] = 0;
    for (unsigned depth=0; depth<64; ++depth) {SwJsonObject parent;parent["child"]=std::move(nested);nested=std::move(parent);}
    candidate = row("x",1); candidate["payload"] = std::move(nested); candidates.push_back(candidate);
    for (const auto& value : candidates) {
        const auto a = db.upsertRows(reference,rows({value}));
        const auto b = db.upsertRows(retained,rows({value}));
        require(a.code() == b.code(), "Prepared value validation diverged from ordinary Exact validation");
    }
    success(db.replaceRows(retained,rows({row("a",1),row("b",2),row("c",2)})));
    SwTableQuery query; query.sortBy = "score"; query.sortDirection = "asc"; query.limit = 2;
    SwTableQueryResult ordinary, fast;
    success(db.queryRows(original,query,&ordinary)); success(db.queryRows(retained,query,&fast));
    require(ordinary.rows == fast.rows && ordinary.nextCursor == fast.nextCursor && ordinary.hasMore == fast.hasMore,
            "Prepared index encoding, ties or cursor differs");
    query.cursor = fast.nextCursor;
    success(db.queryRows(original,query,&ordinary)); success(db.queryRows(retained,query,&fast));
    require(ordinary.rows == fast.rows && ordinary.rows.size() == 1, "Prepared indexed second page differs");
    query.cursor.clear(); query.sortBy = "unknown";
    require(db.queryRows(original,query,&ordinary).code() == db.queryRows(retained,query,&fast).code(),
            "Prepared query skipped selector validation");
    success(db.applyRows(retained,rows({row("b",0)}),erased));
    query.sortBy = "score"; query.limit = 10;
    success(db.queryRows(original,query,&ordinary)); success(db.queryRows(retained,query,&fast));
    require(ordinary.rows == fast.rows && fast.rows.size() == 2 && fast.rows[0]["id"].toString() == "b",
            "Prepared atomic delta left old/missing index entries");
    success(db.replaceRows(retained,rows({row("last",5)})));
    success(db.readRows(original,&actual)); require(actual == rows({row("last",5)}), "Prepared replacement retained omitted rows");

    const auto good = retained;
    auto bad = original; bad.columns.append(bad.columns[0]);
    require(!SwTableDb::prepareSchema(bad,&retained).ok() && retained.isValid(), "Failed preparation discarded a valid handle");
    success(db.readRows(retained,&actual)); require(actual == rows({row("last",5)}), "Failed preparation replaced its output");
    require(!SwTableDb::prepareSchema(original,nullptr).ok(), "Missing prepared output accepted");
    SwTableDbPreparedSchema empty;
    require(!db.getRow(empty,"a",&found).ok() && !db.applyRows(empty,{},{}).ok(), "Unprepared descriptor accepted");
    require(!db.getRow(good,"a",nullptr).ok() && !db.readRows(good,nullptr).ok(), "Prepared read accepted missing output");
    auto invalidIndex = original; invalidIndex.indexes[0].columnId = "absent";
    require(!SwTableDb::prepareSchema(invalidIndex,&empty).ok(), "Prepared invalid index accepted");
    invalidIndex = original; invalidIndex.indexes[0].columnId = "payload";
    require(!SwTableDb::prepareSchema(invalidIndex,&empty).ok(), "Prepared JSON index accepted");

    // Prepared metadata can also serve Managed reads, while Exact-only writes
    // keep rejecting Managed schemas instead of bypassing normalization.
    SwTableSchema managed; managed.tableId = "managed";
    SwTableColumn value; value.columnId = "name"; managed.columns.append(value);
    const auto managedPrepared = prepare(managed);
    SwJsonObject managedInput, created; managedInput["name"] = "value";
    success(db.insertRow(managed,managedInput,&created));
    success(db.getRow(managedPrepared," "+created["rowId"].toString()+" ",&found));
    require(found == created && !db.replaceRows(managedPrepared,rows({created})).ok(), "Managed prepared semantics changed");
}
}

int main() {
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("sw-prepared-schema-"+std::to_string(token));
    try {
        SwTableDbPreparedSchema surviving;
        {
            auto temporary = schema("prepared"); surviving = prepare(temporary);
        }
        SwTableDb memory; SwEmbeddedDbOptions options; options.persistent = false;
        success(memory.open(options)); verify(memory); memory.close();
        SwJsonObject found; require(memory.getRow(surviving,"last",&found).code() == SwDbStatus::NotOpen,
                                    "Prepared access ignored a closed database");
        SwTableDb disk; options.persistent = true; options.dbPath = SwString(path.string());
        success(disk.open(options)); verify(disk); success(disk.sync()); disk.close();
        success(disk.open(options)); success(disk.getRow(surviving,"last",&found));
        require(found == row("last",5), "Prepared handle depended on original database lifetime");
        disk.close(); std::filesystem::remove_all(path);
        std::cout << "PASS: prepared descriptor lifetime/isolation, exact validation, ownership, indexes and disk/memory parity\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; std::filesystem::remove_all(path); return 1;
    }
}
