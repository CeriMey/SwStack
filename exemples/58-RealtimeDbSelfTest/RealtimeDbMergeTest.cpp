#include "SwRealtimeDb.h"
#include "SwRealtimeDbJsEvaluator.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Store = SwRealtimeDb;
SwJsonObject object(std::initializer_list<std::pair<const char*, SwJsonValue>> values) {
    SwJsonObject result; for (const auto& value : values) result[value.first] = value.second; return result;
}
SwJsonArray array(std::initializer_list<SwJsonValue> values) {
    SwJsonArray result; for (const auto& value : values) result.append(value); return result;
}
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Function> void rejects(Function action) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("invalid merge accepted");
}
SwString hello(Store& store, const char* actor) {
    return store.execute(object({{"op", "hello"}, {"actor", actor}}))["session"].toString();
}
SwJsonObject definition() {
    return object({{"table", "source"}, {"key", "id"}, {"schema_version", 1}, {"max_rows", 2},
        {"overflow_policy", "evict_oldest_write"}, {"writers", array({"writer"})},
        {"columns", object({{"id", "string"}, {"meta", "object"}, {"value", object({{"type", "integer"}, {"indexed", true}})},
            {"optional", object({{"type", "string"}, {"nullable", true}})}, {"items", "array"}})}});
}
SwJsonObject initial(const char* key = "a") {
    return object({{"id", key}, {"value", 1}, {"items", array({1, 2})},
        {"meta", object({{"sequence", 5}, {"times", object({{"x.y", 10}, {"keep", 20}})}, {"keep", "retained"}})}});
}
SwJsonObject increment(const char* key, SwJsonArray path, SwJsonValue amount) {
    return object({{"key", key}, {"path", path}, {"amount", amount}});
}
SwJsonObject patch(Store& store, const SwString& session, SwJsonArray rows, SwJsonArray increments = {}, bool create = false) {
    auto request = object({{"op", "write"}, {"session", session}, {"table", "source"},
        {"merge", true}, {"include_rows", false}, {"rows", rows}, {"increments", increments}});
    if (create) request["definition"] = definition();
    return store.execute(request);
}
SwJsonObject row(Store& store, const char* key = "a") {
    const auto result = store.execute(object({{"op", "read"}, {"table", "source"}, {"keys", array({key})}}))["rows"].toArray();
    return result.isEmpty() ? SwJsonObject() : result[0].toObject();
}
void validMerges() {
    SwRealtimeDbOptions options; options.storage.persistent = false;
    SwRealtimeDbJsEvaluator evaluator(50);
    Store store([&](const SwString& script, const SwJsonObject& inputs) { return evaluator.evaluate(script, inputs); }, options);
    const auto owner = hello(store, "owner"), writer = hello(store, "writer"), stranger = hello(store, "stranger");
    const auto created = patch(store, owner, array({initial()}), {}, true);
    require(!created.contains("rows") && row(store)["optional"].isNull(), "sparse insertion did not initialize nullable columns");
    auto view = object({{"op", "register_view"}, {"session", owner}, {"table", "derived"}, {"key", "id"}, {"schema_version", 1},
        {"columns", object({{"id", "string"}, {"count", object({{"type", "integer"}, {"indexed", true}})}})},
        {"dependencies", array({"source"})},
        {"script", "if(tables.source.some(function(r){return r.value<0;}))throw Error('invalid source');"
                   "return tables.source.map(function(r){return {id:r.id,count:r.meta.sequence};});"}});
    store.execute(view);
    store.execute(object({{"op", "subscribe"}, {"session", owner}, {"table", "derived"}, {"mode", "write"}}));
    const auto state = store.execute(object({{"op", "introspect"}}));
    auto sparse = object({{"id", "a"}, {"value", 9}, {"items", array({3})},
        {"meta", object({{"times", object({{"x.y", 30}})}})}});
    const auto inputBefore = sparse;
    const auto direct = object({{"op", "write"}, {"session", writer}, {"table", "source"}, {"merge", true},
        {"rows", array({sparse})}, {"increments", array({increment("a", array({"meta", "sequence"}), 2)})}});
    const auto directBefore = direct;
    store.execute(direct);
    require(direct == directBefore, "merge mutated the execute request through a borrowed pointer");
    require(sparse == inputBefore, "merge mutated the caller's patch");
    const auto current = row(store), meta = current["meta"].toObject();
    require(current["value"].toInt() == 9 && current["items"].toArray().size() == 1 && meta["sequence"].toInt() == 7,
            "merge or increment produced incorrect values");
    require(meta["keep"].toString() == "retained" && meta["times"].toObject()["keep"].toInt() == 20 &&
            meta["times"].toObject()["x.y"].toInt() == 30, "nested object merge lost omitted or dotted fields");
    const auto changes = store.execute(object({{"op", "changes"}, {"after", state["revision"]}, {"epoch", state["epoch"]}}))["events"].toArray();
    require(changes.size() == 2 && changes[0].toObject()["table"].toString() == "source" &&
            changes[1].toObject()["table"].toString() == "derived", "merge lost ordered source/view journal events");
    const auto beforeEqual = store.execute(object({{"op", "introspect"}}));
    patch(store, owner, array({object({{"id", "a"}, {"value", 9}})}));
    const auto equal = store.execute(object({{"op", "changes"}, {"after", beforeEqual["revision"]}, {"epoch", beforeEqual["epoch"]}}))["events"].toArray();
    require(equal.size() == 1 && equal[0].toObject()["table"].toString() == "source" && !equal[0].toObject()["changed"].toBool(),
            "equal sparse rewrite must notify source writes without recomputing the view");
    const auto restoredBefore = store.execute(object({{"op", "introspect"}}));
    // A patch can change a value and an increment restore it in the same write.
    // Equality must compare final values, ignoring only internal recency metadata.
    patch(store, owner, array({object({{"id", "a"}, {"value", 10}})}),
          array({increment("a", array({"value"}), -1)}));
    const auto restored = store.execute(object({{"op", "changes"}, {"after", restoredBefore["revision"]},
        {"epoch", restoredBefore["epoch"]}}))["events"].toArray();
    require(restored.size() == 1 && restored[0].toObject()["table"].toString() == "source" &&
            !restored[0].toObject()["changed"].toBool() &&
            store.execute(object({{"op", "introspect"}}))["data_bytes"] == restoredBefore["data_bytes"],
            "restored merge changed equality or retained-byte accounting");
    patch(store, owner, array({object({{"id", "a"}, {"optional", "value"}})}));
    patch(store, owner, array({object({{"id", "a"}, {"optional", SwJsonValue()}})}));
    require(row(store).contains("optional") && row(store)["optional"].isNull(), "merge null deleted a column");
    patch(store, owner, array({object({{"id", "a"}})}), array({increment("a", array({"meta", "times", "x.y"}), 0.5)}));
    require(row(store)["meta"].toObject()["times"].toObject()["x.y"].toDouble() == 30.5, "nested numeric increment changed dotted path meaning");
    patch(store, owner, array({initial("b")}));
    patch(store, owner, array({object({{"id", "a"}})})); // Recency advances even if no value changes.
    patch(store, owner, array({initial("c")}));
    require(row(store, "b").isEmpty() && !row(store, "a").isEmpty(), "merge did not retain latest write recency");
    const auto ordered = store.execute(object({{"op", "read"}, {"table", "source"}, {"order_by", "value"}, {"limit", 1}}))["rows"].toArray();
    require(ordered[0].toObject()["id"].toString() == "c", "merge did not update the native secondary index");
    const auto beforeFailure = row(store);
    rejects([&] { patch(store, stranger, array({object({{"id", "a"}, {"value", 44}})})); });
    require(row(store) == beforeFailure, "unauthorized merge changed stored rows");
    // Two ordered patches in one native operation must see each other's commit.
    SwJsonArray operations;
    for (int i = 0; i < 2; ++i) operations.append(object({{"op", "write"}, {"table", "source"}, {"merge", true},
        {"rows", array({object({{"id", "a"}})})}, {"increments", array({increment("a", array({"meta", "sequence"}), 1)})}}));
    const auto batched = store.execute(object({{"op", "mutate_many"}, {"session", owner}, {"operations", operations}}));
    require(batched["complete"].toBool() && row(store)["meta"].toObject()["sequence"].toInt() == 9, "ordered batch increments lost an update");
    auto derived = [&] { return store.execute(object({{"op", "read"}, {"table", "derived"}, {"order_by", "count"}, {"limit", 10}})); };
    const auto retainedView = derived()["rows"].toArray();
    require(retainedView.size() == 2 && retainedView[0].toObject()["id"].toString() == "c" &&
            retainedView[1].toObject()["id"].toString() == "a", "view retained an evicted source row or outdated index");
    patch(store, owner, array({object({{"id", "a"}, {"value", -1}})}));
    require(!derived()["valid"].toBool(), "invalid transform left its view valid");
    store.execute(object({{"op", "erase"}, {"session", owner}, {"table", "source"}, {"keys", array({"a"})}}));
    const auto recovered = derived();
    const auto recoveredRows = recovered["rows"].toArray();
    require(recovered["valid"].toBool() && recoveredRows.size() == 1 && recoveredRows[0].toObject()["id"].toString() == "c",
            "view recovery retained omitted stale data or secondary keys");
    store.execute(object({{"op", "erase"}, {"session", owner}, {"table", "source"}, {"keys", array({"c"})}}));
    require(derived()["rows"].toArray().isEmpty(), "empty transform output retained old view rows");
    const auto beforeDescriptor = store.execute(object({{"op", "introspect"}}))["data_bytes"].toLongLong();
    const auto metadata = object({{"description", SwString(std::string(4096, 'x'))}});
    auto replaced = view; replaced["replace"] = true; replaced["metadata"] = metadata;
    store.execute(replaced);
    const auto expectedBytes = beforeDescriptor + static_cast<long long>(metadata.toJsonString().size()) - 2;
    require(store.execute(object({{"op", "introspect"}}))["data_bytes"].toLongLong() == expectedBytes,
            "view replacement did not update descriptor byte accounting");
    patch(store, owner, {}); // Reevaluate an empty view using its replacement descriptor.
    require(store.execute(object({{"op", "introspect"}}))["data_bytes"].toLongLong() == expectedBytes,
            "view reevaluation reused an obsolete descriptor byte count");
}
void invalidMerges() {
    SwRealtimeDbOptions options; options.storage.persistent = false; Store store({}, options);
    const auto owner = hello(store, "owner"); patch(store, owner, array({initial()}), {}, true);
    const auto original = row(store);
    auto rejected = [&](const SwJsonArray& rows, const SwJsonArray& increments = SwJsonArray()) {
        const auto before = store.execute(object({{"op", "introspect"}}));
        try { rejects([&] { patch(store, owner, rows, increments); }); }
        catch (const std::exception& error) {
            throw std::runtime_error(std::string(error.what()) + ": rows=" + rows.toJsonString().toStdString() +
                " increments=" + increments.toJsonString().toStdString());
        }
        const auto after = store.execute(object({{"op", "introspect"}}));
        require(row(store) == original && after["revision"] == before["revision"] && after["data_bytes"] == before["data_bytes"],
                "invalid merge partially committed values or events");
    };
    rejected(array({object({{"id", "a"}, {"unknown", 1}})}));
    rejected(array({object({{"id", "a"}, {"meta", "wrong"}})}));
    rejected(array({object({{"value", 9}})}));
    rejected(array({object({{"id", "a"}}), object({{"id", "a"}})}));
    rejected(array({object({{"id", "a"}, {"value", 2}}), object({{"id", "b"}})}));
    const auto onlyKey = array({object({{"id", "a"}})});
    for (const auto& incrementValue : {
        increment("a", {}, 1), increment("a", array({"id"}), 1), increment("a", array({"unknown"}), 1),
        increment("missing", array({"value"}), 1), increment("a", array({"meta", "keep"}), 1),
        increment("a", array({"items", "0"}), 1), increment("a", array({"value"}), "1")})
        rejected(onlyKey, array({incrementValue}));
    rejected(onlyKey, array({increment("a", array({"value"}), 1), increment("a", array({"value"}), 2)}));
    rejected(onlyKey, array({increment("a", array({"meta"}), 1), increment("a", array({"meta", "sequence"}), 1)}));
    SwJsonArray tooMany; for (int i = 0; i < 65; ++i) tooMany.append(increment("a", array({"meta", SwString::number(i)}), 1));
    rejected(onlyKey, tooMany);
    rejected(onlyKey, array({SwJsonValue(std::shared_ptr<SwJsonObject>())}));
    auto extreme = object({{"id", "a"}, {"meta", object({{"sequence", std::numeric_limits<long long>::max()}})}});
    rejected(array({extreme}), array({increment("a", array({"meta", "sequence"}), 1)}));
    extreme["meta"] = object({{"sequence", std::numeric_limits<long long>::min()}});
    rejected(array({extreme}), array({increment("a", array({"meta", "sequence"}), -1)}));
    rejected(array({object({{"id", "a"}, {"meta", object({{"sequence", std::numeric_limits<double>::max()}})}})}),
             array({increment("a", array({"meta", "sequence"}), std::numeric_limits<double>::max())}));
    rejected(onlyKey, array({increment("a", array({"value"}), 9223372036854775808.0)}));
    SwJsonArray path; path.append("meta"); for (int i = 0; i < 31; ++i) path.append("nested");
    auto tooDeep = path; tooDeep.append("nested");
    rejected(onlyKey, array({increment("a", tooDeep, 1)}));
    const auto malformed = object({{"op", "write"}, {"session", owner}, {"table", "source"}, {"rows", onlyKey}, {"merge", "true"}});
    rejects([&] { store.execute(malformed); });
    auto noMerge = malformed; noMerge["merge"] = false; noMerge["increments"] = array({increment("a", array({"value"}), 1)});
    rejects([&] { store.execute(noMerge); });
    // Row depth starts at zero: 32 path segments reach the allowed depth 32.
    patch(store, owner, onlyKey, array({increment("a", path, 1)}));
    const auto boundary = row(store);
    const SwJsonValue* leaf = &boundary["meta"];
    for (int i = 0; i < 31; ++i) {
        const auto parent = leaf->toObjectPtr();
        require(bool(parent), "maximum-depth increment lost a path object");
        leaf = &static_cast<const SwJsonObject&>(*parent)["nested"];
    }
    require(leaf->toInt() == 1, "maximum-depth increment did not commit its value");
}
void transformAliasIsolation() {
    SwRealtimeDbOptions options; options.storage.persistent = false;
    std::shared_ptr<SwJsonObject> retained;
    Store store([&](const SwString&, const SwJsonObject&) {
        retained = std::make_shared<SwJsonObject>(object({{"id", "a"}, {"payload", object({{"value", 1}})}}));
        SwJsonArray output; output.append(SwJsonValue(retained)); return output;
    }, options);
    const auto owner = hello(store, "owner"); patch(store, owner, array({initial()}), {}, true);
    auto view = object({{"op", "register_view"}, {"session", owner}, {"table", "alias"}, {"key", "id"},
        {"schema_version", 1}, {"columns", object({{"id", "string"}, {"payload", "object"}})},
        {"dependencies", array({"source"})}, {"script", "custom transform"}});
    store.execute(view);
    auto snapshot = store.execute(object({{"op", "read"}, {"table", "alias"}}));
    (*(*retained)["payload"].toObjectPtr())["value"] = 2;
    (*snapshot["rows"].toArrayPtr()->dataRef()[0].toObjectPtr())["payload"] = object({{"value", 3}});
    const auto current = store.execute(object({{"op", "read"}, {"table", "alias"}}));
    require(current["rows"].toArray()[0].toObject()["payload"].toObject()["value"].toInt() == 1,
            "owned publication retained a public Transform or read snapshot alias");
}

void staleMerge() {
    const auto folder = std::filesystem::temp_directory_path() / ("rtdb-merge-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SwRealtimeDbOptions options; options.storage.dbPath = SwString(folder.string());
    try {
        { Store store({}, options); const auto owner = hello(store, "owner");
          patch(store, owner, array({initial("a"), initial("b")}), {}, true); }
        { Store store({}, options); const auto owner = hello(store, "owner"), writer = hello(store, "writer");
          rejects([&] { patch(store, writer, array({object({{"id", "a"}, {"value", 99}})})); });
          patch(store, owner, array({object({{"id", "a"}, {"value", 8}})}), array({increment("a", array({"meta", "sequence"}), 1)}), true);
          require(row(store)["meta"].toObject()["sequence"].toInt() == 6 && row(store)["items"].toArray().size() == 2 && row(store, "b").isEmpty(),
                  "owner recovery did not merge selected retained data and replace omitted stale rows"); }
        std::filesystem::remove_all(folder);
    } catch (...) { std::filesystem::remove_all(folder); throw; }
}
}
int main() {
    try { validMerges(); invalidMerges(); staleMerge(); transformAliasIsolation();
        std::cout << "PASS: atomic sparse merge, increments, null/arrays, ordered events/indexes/retention and owner recovery\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
