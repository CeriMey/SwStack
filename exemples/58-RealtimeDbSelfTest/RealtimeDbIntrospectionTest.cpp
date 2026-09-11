#include <core/storage/SwRealtimeDb.h>
#include <core/storage/realtimedb/SwRealtimeDbJson.h>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
SwJsonObject object(std::initializer_list<std::pair<const char*, SwJsonValue>> fields) {
    SwJsonObject out;
    for (const auto& field : fields) out[field.first] = field.second;
    return out;
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void check() {
    SwRealtimeDbOptions options; options.storage.persistent = false;
    int evaluations = 0;
    SwRealtimeDb store([&](const SwString&, const SwJsonObject&) { ++evaluations; return SwJsonArray(); }, options);
    const auto session = store.execute(object({{"op", "hello"}, {"actor", "catalog"}}))["session"];
    const auto columns = object({{"id", "string"}, {"value", object({{"type", "number"}, {"unit", "deg"}})}});
    for (int i = 0; i < 121; ++i) {
        const auto name = SwString(i % 2 ? "camera.Status.t" : "gimbal.Status.t") + SwString::number(1000+i);
        store.execute(object({{"op", "register_table"}, {"session", session}, {"table", name},
            {"schema_version", 1}, {"key", "id"}, {"columns", columns},
            {"metadata", object({{"description", "Angle"}})}}));
    }
    SwJsonArray dependencies; dependencies.append("gimbal.Status.t1000");
    store.execute(object({{"op", "register_view"}, {"session", session}, {"table", "gimbal.Derived.view"},
        {"schema_version", 1}, {"key", "id"}, {"columns", columns}, {"dependencies", dependencies}, {"script", "return [];"}}));
    store.execute(object({{"op", "subscribe"}, {"session", session}, {"table", "gimbal.Status.t1000"}, {"mode", "write"}}));
    SwJsonArray rows; rows.append(object({{"id", "current"}, {"value", 1}}));
    store.execute(object({{"op", "write"}, {"session", session}, {"table", "gimbal.Status.t1000"}, {"rows", rows}}));
    const int before = evaluations;
    auto query = object({{"op", "introspect"}, {"summary", true}, {"include_runtime", false}, {"limit", 17}});
    std::set<SwString> names;
    SwString previous;
    for (;;) {
        const auto page = store.execute(query);
        require(page["total"].toInt() == 122, "total must precede pagination");
        require(!page.contains("actors") && !page.contains("subscriptions"), "runtime diagnostics excluded");
        require(page["tables"].toArray().size() <= 17, "bounded page");
        for (const auto& value : page["tables"].toArray()) {
            const auto row = value.toObject(); const auto name = row["table"].toString();
            require(previous.isEmpty() || previous < name, "ordered unique pages"); previous = name;
            require(!row.contains("columns") && !row.contains("metadata") && !row.contains("dependencies"), "summary stays light");
            require(row["owner"].toString() == "catalog" && row["owner_online"].toBool(), "summary retains owner status");
            if (name == "gimbal.Derived.view") {
                require(row["dirty"].toBool() && !row["valid"].toBool(), "pending view validity retained");
                require(!row["error"].toString().isEmpty() && !row["writable"].toBool(), "pending error and writeability retained");
            }
            names.insert(name);
        }
        if (!page.contains("next_offset")) break;
        query["offset"] = page["next_offset"];
    }
    require(names.size() == 122 && evaluations == before, "introspection must not evaluate views");
    query["offset"] = 0; query["namespace"] = "gimbal"; query["schema"] = "Status";
    require(store.execute(query)["total"].toInt() == 61, "namespace and schema filter before count");
    query["offset"] = 60;
    auto page = store.execute(query);
    require(page["tables"].toArray().size() == 1 && !page.contains("next_offset"), "last filtered page");
    query["offset"] = 100000;
    require(store.execute(query)["tables"].toArray().isEmpty(), "offset beyond end");
    query["offset"] = 0; query["limit"] = 0;
    page = store.execute(query);
    require(page["tables"].toArray().isEmpty() && page["total"].toInt() == 61 && !page.contains("next_offset"), "count only");
    auto detail = object({{"op", "introspect"}, {"table", "gimbal.Status.t1000"}, {"include_runtime", false}});
    page = store.execute(detail);
    const auto row = page["tables"].toArray()[0].toObject();
    require(row["columns"].toObject() == columns && row["metadata"].toObject()["description"].toString() == "Angle", "full table details preserved");
    detail["table"] = "gimbal.Derived.view"; detail["include_scripts"] = true;
    require(store.execute(detail)["tables"].toArray()[0].toObject()["script"].toString() == "return [];", "explicit view script");
    detail["table"] = "missing";
    require(store.execute(detail)["tables"].toArray().isEmpty(), "missing exact table");
    const auto full = store.execute(object({{"op", "introspect"}}));
    require(full["tables"].toArray().size() == 122 && full["actors"].toArray().size() == 1 &&
            full["subscriptions"].toArray().size() == 1, "default introspection remains complete");
    for (const auto& invalid : {object({{"limit", -1}}), object({{"limit", 1025}}), object({{"limit", 1.5}}),
         object({{"offset", -1}}), object({{"offset", "1"}}), object({{"offset", 100001}}), object({{"summary", 1}}), object({{"include_runtime", "false"}})}) {
        auto bad = invalid; bad["op"] = "introspect";
        bool rejected = false; try { store.execute(bad); } catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid pagination rejected");
    }
}
}
int main() {
    try { check(); std::cout << "PASS catalog pagination, filters, details, validity and validation\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
