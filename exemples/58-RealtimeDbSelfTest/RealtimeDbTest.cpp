#include "SwRealtimeDb.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <atomic>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

using Store = SwRealtimeDb;

namespace {

Store transientStore(Store::Transform transform) {
    SwRealtimeDbOptions options;
    options.storage.persistent = false;
    return Store(std::move(transform), options);
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

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function> void rejects(Function action, const char* message) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

SwString hello(Store& store, const char* actor) {
    return store.execute(object({{"op", "hello"}, {"actor", actor}}))["session"].toString();
}

SwJsonObject descriptor(const SwString& session, const char* name) {
    return object({{"op", "register_table"}, {"session", session}, {"table", name},
        {"key", "id"}, {"schema_version", 1},
        {"columns", object({{"id", "string"}, {"value", "number"}})}});
}

SwJsonObject view(const SwString& session, const char* name, const char* dependency,
                  const char* script = nullptr) {
    auto result = descriptor(session, name);
    result["op"] = "register_view";
    result["dependencies"] = array({dependency});
    result["script"] = script ? script : dependency;
    return result;
}

SwJsonObject read(Store& store, const char* table) {
    return store.execute(object({{"op", "read"}, {"table", table}}));
}

SwJsonObject write(Store& store, const SwString& session, const char* table,
                   const SwJsonArray& rows) {
    return store.execute(object({{"op", "write"}, {"session", session}, {"table", table}, {"rows", rows}}));
}

SwJsonObject writeDefinitionRequest(const SwString& session, const char* table, const SwJsonArray& rows) {
    auto definition = descriptor(session, table);
    definition.remove("op");
    definition.remove("session");
    return object({{"op", "write"}, {"session", session}, {"table", table},
                   {"definition", definition}, {"rows", rows}});
}

SwJsonArray oneRow(double value = 1) {
    return array({object({{"id", "one"}, {"value", value}})});
}

SwJsonObject changes(Store& store, const SwJsonObject& snapshot, const char* mode = "write") {
    return store.execute(object({{"op", "changes"}, {"epoch", snapshot["epoch"]},
        {"after", snapshot["cursor"]}, {"table", snapshot["table"]}, {"mode", mode}}));
}

SwJsonArray transform(const SwString& script, const SwJsonObject& tables) {
    if (script == "throw") throw std::runtime_error("deliberate view error");
    if (script == "bad") return array({object({{"id", "one"}, {"value", "not a number"}})});
    return tables[script].toArray();
}

SwJsonObject subscribe(Store& store, const SwString& session, const char* table, const char* mode = "write") {
    return store.execute(object({{"op", "subscribe"}, {"session", session}, {"table", table}, {"mode", mode}}));
}

void cachedViewChain() {
    for (unsigned workers : {0u, 2u}) {
        int parent = 0, child = 0;
        SwRealtimeDbOptions options; options.storage.persistent = false; options.viewWorkerCount = workers;
        Store store([&](const SwString& script, const SwJsonObject& input) {
            if (script == "source") { ++parent; return oneRow(input[script].toArray()[0].toObject()["value"].toDouble() > 0 ? 1 : 0); }
            ++child; return input[script].toArray();
        }, options);
        const auto owner = hello(store, "owner");
        store.execute(descriptor(owner, "source"));
        store.execute(view(owner, "middle", "source"));
        store.execute(view(owner, "leaf", "middle"));
        write(store, owner, "source", oneRow(1));
        require(parent == 0 && child == 0, "unread chain ran JavaScript");
        read(store, "leaf");
        require(parent == 1 && child == 1, "first read failed to populate caches");
        subscribe(store, owner, "leaf");
        write(store, owner, "source", oneRow(2));
        require(parent == 2 && child == 1, "unchanged intermediate result recomputed child");
        write(store, owner, "source", oneRow(-1));
        require(parent == 3 && child == 2, "changed intermediate result failed to recompute child");
    }
}

void diamondViews() {
    std::atomic<int> calls{0};
    SwRealtimeDbOptions options;
    options.storage.persistent = false;
    options.viewWorkerCount = 2;
    SwRealtimeDb store([&](const SwString&, const SwJsonObject& tables) {
        ++calls;
        double sum = 0;
        for (const auto& table : tables.data()) for (const auto& row : table.second.toArray())
            sum += row.toObject()["value"].toDouble();
        return oneRow(sum);
    }, options);
    const auto owner = hello(store, "owner");
    auto join = view(owner, "join", "left");
    join["dependencies"] = array({"left", "right"});
    store.execute(join);
    store.execute(view(owner, "left", "source"));
    store.execute(view(owner, "right", "source"));
    store.execute(view(owner, "unused", "source"));
    store.execute(descriptor(owner, "source"));
    store.execute(descriptor(owner, "unrelated"));
    write(store, owner, "unrelated", oneRow(7));
    calls = 0;
    write(store, owner, "source", oneRow(4));
    require(calls == 0, "unobserved write eagerly evaluated views");
    require(read(store, "join")["rows"] == SwJsonValue(oneRow(8)) && calls == 3,
            "diamond evaluated twice or before both dependencies");
    require(read(store, "join")["rows"] == SwJsonValue(oneRow(8)) && calls == 3,
            "clean view read ran its transform again");
    write(store, owner, "source", oneRow(4));
    require(read(store, "join")["rows"] == SwJsonValue(oneRow(8)) && calls == 3,
            "identical source write invalidated cached views");
    const auto watcher = subscribe(store, owner, "join");
    calls = 0;
    write(store, owner, "source", oneRow(5));
    require(calls == 3 && read(store, "join")["rows"] == SwJsonValue(oneRow(10)),
            "observed diamond was not refreshed or computed an unused branch");
    calls = 0;
    const auto written = read(store, "source");
    write(store, owner, "source", oneRow(5));
    require(calls == 0, "identical observed write reran transforms");
    require(!changes(store, written)["events"].toArray().isEmpty(), "identical source write lost its write notification");
    require(changes(store, written, "change")["events"].toArray().isEmpty(), "identical write emitted a change notification");
    calls = 0;
    write(store, owner, "unrelated", oneRow(7));
    require(calls == 0, "unrelated write recalculated views");
    join["dependencies"] = array({"unrelated"});
    join["replace"] = true;
    store.execute(join);
    calls = 0;
    write(store, owner, "source", oneRow(6));
    require(calls == 0 && read(store, "join")["rows"] == SwJsonValue(oneRow(7)),
            "replaced dependency graph retained the old active ancestors");
    store.execute(object({{"op", "unsubscribe"}, {"session", owner},
                          {"subscription", watcher["subscription"]}}));
    write(store, owner, "unrelated", oneRow(9));
    require(calls == 0, "last unsubscribe left eager evaluation active");
    require(read(store, "join")["rows"] == SwJsonValue(oneRow(9)) && calls == 1,
            "unsubscribed view read did not get the latest source");
}

void demandViews() {
    int calls = 0;
    auto store = transientStore([&](const SwString& script, const SwJsonObject& tables) {
        ++calls;
        return transform(script, tables);
    });
    const auto owner = hello(store, "owner");
    store.execute(view(owner, "derived", "source"));
    const auto inspect = [&] {
        return store.execute(object({{"op", "introspect"}, {"table", "derived"}}))["tables"].toArray()[0].toObject();
    };
    require(inspect()["error"].toString() == "missing dependency: source", "pending dependency error was hidden");
    store.execute(descriptor(owner, "source"));
    require(inspect()["error"].toString() == "invalid dependency: source", "unpublished dependency looked ready");
    write(store, owner, "source", oneRow(1));
    write(store, owner, "source", oneRow(2));
    const auto beforeRejectedRead = store.execute(object({{"op", "introspect"}}));
    rejects([&] { store.execute(object({{"op", "read"}, {"table", "derived"}, {"keys", array({1})}})); },
            "invalid keys accepted by a dirty view");
    rejects([&] { store.execute(object({{"op", "read"}, {"table", "derived"}, {"limit", -1}})); },
            "invalid limit accepted by a dirty view");
    rejects([&] { store.execute(object({{"op", "read"}, {"table", "derived"}, {"order_by", "absent"}})); },
            "unknown ordering column accepted by a dirty view");
    rejects([&] { store.execute(object({{"op", "read_many"}, {"tables", array({"derived", "derived"})}})); },
            "duplicate batch name accepted");
    require(store.execute(object({{"op", "introspect"}}))["revision"] == beforeRejectedRead["revision"],
            "rejected read materialized a view");
    auto state = inspect();
    require(calls == 0 && state["dirty"].toBool() && !state["valid"].toBool() &&
            state["error"].toString() == "awaiting evaluation", "introspection ran JS or hid pending evaluation");
    require(read(store, "derived")["rows"] == SwJsonValue(oneRow(2)) && calls == 1,
            "on-demand materialization did not coalesce unused updates");
    require(!inspect()["dirty"].toBool() && inspect()["valid"].toBool(), "materialized view still reported pending");
    store.execute(view(owner, "downstream", "derived"));
    write(store, owner, "source", oneRow(3));
    calls = 0;
    const auto batch = store.execute(object({{"op", "read_many"}, {"tables", array({"source", "derived", "downstream"})}}));
    const auto snapshots = batch["tables"].toObject();
    require(calls == 2, "read_many evaluated a shared dependency twice");
    for (const auto& name : {"source", "derived", "downstream"}) {
        require(snapshots[name].toObject()["rows"] == SwJsonValue(oneRow(3)) &&
                snapshots[name].toObject()["cursor"] == batch["cursor"], "read_many returned mixed materialization cursors");
    }
    const auto writeWatcher = subscribe(store, owner, "downstream", "write");
    const auto changeWatcher = subscribe(store, owner, "downstream", "change");
    const auto before = read(store, "downstream");
    calls = 0;
    write(store, owner, "source", oneRow(3));
    write(store, owner, "source", oneRow(4));
    write(store, owner, "source", oneRow(3));
    require(calls == 4, "identical input recomputed views or a changed input was skipped");
    require(changes(store, before)["events"].toArray().size() == 2 &&
            changes(store, before, "change")["events"].toArray().size() == 2,
            "observed views lost transient changes or fabricated an equal-input evaluation");
    store.execute(object({{"op", "unsubscribe"}, {"session", owner}, {"subscription", writeWatcher["subscription"]}}));
    calls = 0;
    write(store, owner, "source", oneRow(5));
    require(calls == 2, "removing one subscription stopped another subscriber");
    store.execute(object({{"op", "unsubscribe"}, {"session", owner}, {"subscription", changeWatcher["subscription"]}}));
    calls = 0;
    write(store, owner, "source", oneRow(6));
    require(calls == 0, "unused descendant still computed after unsubscribe");
    subscribe(store, owner, "downstream");
    require(calls == 2 && read(store, "downstream")["rows"] == SwJsonValue(oneRow(6)),
            "subscription activation did not materialize its current snapshot");
}

void tablesAndJournal() {
    auto store = transientStore(transform);
    require(store.execute(object({{"op", "introspect"}}))["tables"].toArray().isEmpty(), "store starts nonempty");
    const auto session = hello(store, "producer");
    rejects([&] { hello(store, "producer"); }, "duplicate live actor accepted");
    auto schema = descriptor(session, "state");
    auto invalidSchema = schema;
    invalidSchema["key"] = "missing";
    rejects([&] { store.execute(invalidSchema); }, "invalid schema accepted");
    require(store.execute(object({{"op", "introspect"}}))["tables"].toArray().isEmpty(), "failed registration mutated store");
    store.execute(schema);
    require(!read(store, "state")["valid"].toBool(), "unpublished table is fresh");
    const auto first = write(store, session, "state", oneRow());
    require(first["valid"].toBool(), "publication did not validate table");
    write(store, session, "state", oneRow());
    const auto writes = changes(store, first)["events"].toArray();
    require(writes.size() == 1 && !writes[0].toObject()["changed"].toBool(), "equal write was lost or marked changed");
    require(changes(store, first, "change")["events"].toArray().isEmpty(), "equal write notified onChange");

    const auto beforeInvalid = read(store, "state");
    rejects([&] { write(store, session, "state", array({object({{"id", "one"}, {"value", 7}}), object({{"id", "two"}})})); },
            "partial batch accepted");
    require(read(store, "state") == beforeInvalid, "failed batch partially committed");
    rejects([&] { write(store, session, "state", array({oneRow()[0], oneRow()[0]})); }, "duplicate row key accepted");
    rejects([&] { write(store, session, "state", array({object({{"id", "one"}, {"value", 1}, {"extra", true}})})); },
            "undeclared column accepted");
    rejects([&] { write(store, session, "state", oneRow(std::numeric_limits<double>::infinity())); }, "infinite number accepted");
    const auto other = hello(store, "other");
    rejects([&] { write(store, other, "state", oneRow()); }, "unauthorized writer accepted");
    rejects([&] { store.execute(descriptor(other, "state")); }, "table ownership stolen");
    schema["max_rows"] = 1;
    rejects([&] { store.execute(schema); }, "descriptor change accepted");
    auto shared = descriptor(session, "shared");
    shared["writers"] = array({"other"});
    store.execute(shared);
    rejects([&] { write(store, other, "shared", oneRow()); }, "external writer initialized an unpublished table");
    write(store, session, "shared", SwJsonArray());
    write(store, other, "shared", oneRow());
    require(read(store, "shared")["valid"].toBool(), "declared writer rejected");

    auto erased = store.execute(object({{"op", "erase"}, {"session", session},
        {"table", "state"}, {"keys", array({"one"})}}));
    require(erased["rows"].toArray().isEmpty(), "erase failed");
    for (int i = 0; i < 260; ++i) write(store, session, "state", oneRow());
    require(changes(store, first)["resync_required"].toBool(), "journal overrun not detected");
    auto wrongEpoch = read(store, "state");
    wrongEpoch["epoch"] = "previous-server";
    require(changes(store, wrongEpoch)["resync_required"].toBool(), "restart epoch not detected");
    auto badCursor = read(store, "state");
    badCursor["cursor"] = "18446744073709551616";
    rejects([&] { changes(store, badCursor); }, "revision overflow accepted");
}

void reactiveViews() {
    int calls = 0;
    auto store = transientStore([&](const SwString& script, const SwJsonObject& tables) { ++calls; return transform(script, tables); });
    const auto session = hello(store, "owner");
    store.execute(view(session, "final", "middle"));
    store.execute(view(session, "middle", "source"));
    require(!read(store, "final")["valid"].toBool(), "view with missing dependency valid");
    require(calls == 0, "pending views evaluated");
    store.execute(descriptor(session, "source"));
    write(store, session, "source", oneRow(42));
    require(read(store, "final")["rows"] == SwJsonValue(oneRow(42)), "view graph did not resolve in dependency order");
    require(read(store, "final")["valid"].toBool(), "computed view invalid");
    require(calls == 2, "view recomputed more than once per source publication");
    subscribe(store, session, "final");
    const auto initial = read(store, "final");
    write(store, session, "source", oneRow(42));
    require(calls == 2 && changes(store, initial)["events"].toArray().isEmpty(), "equal upstream write recomputed a cached view");
    require(changes(store, initial, "change")["events"].toArray().isEmpty(), "equal view result notified onChange");
    rejects([&] { write(store, session, "final", oneRow()); }, "view accepted direct writes");
    store.execute(view(session, "cycle_a", "cycle_b"));
    const auto beforeCycle = store.execute(object({{"op", "introspect"}}));
    rejects([&] { store.execute(view(session, "cycle_b", "cycle_a")); }, "cycle accepted");
    const auto afterCycle = store.execute(object({{"op", "introspect"}}));
    require(afterCycle["tables"] == beforeCycle["tables"] && afterCycle["revision"] == beforeCycle["revision"],
            "cycle rejection mutated graph");
    store.execute(view(session, "bad_script", "source", "throw"));
    require(!read(store, "bad_script")["valid"].toBool(), "script exception did not invalidate view");
    store.execute(view(session, "bad_rows", "source", "bad"));
    require(!read(store, "bad_rows")["valid"].toBool(), "view bypassed row schema");
    require(read(store, "source")["valid"].toBool(), "failing view invalidated producer");
}

void replaceViews() {
    auto store=transientStore(transform);const auto owner=hello(store,"owner"),other=hello(store,"other");
    store.execute(descriptor(owner,"a"));store.execute(descriptor(owner,"b"));
    write(store,owner,"a",oneRow(1));write(store,owner,"b",oneRow(2));
    store.execute(view(owner,"derived","a"));store.execute(view(owner,"downstream","derived"));
    auto replacement=view(owner,"derived","b");
    rejects([&]{store.execute(replacement);},"implicit view replacement accepted");
    replacement["replace"]=true;auto forbidden=replacement;forbidden["session"]=other;
    rejects([&]{store.execute(forbidden);},"other actor replaced view");
    store.execute(replacement);
    require(read(store,"downstream")["rows"]==SwJsonValue(oneRow(2)),"replacement did not recompute descendants");
    const auto before=read(store,"derived");
    auto cycle=view(owner,"derived","downstream");cycle["replace"]=true;
    rejects([&]{store.execute(cycle);},"replacement introduced cycle");
    require(read(store,"derived")==before,"rejected replacement mutated view");
    auto schema=replacement;schema["columns"]=object({{"id","string"},{"value","string"}});
    rejects([&]{store.execute(schema);},"replacement changed schema");
    auto waiting=view(owner,"derived","future");waiting["replace"]=true;store.execute(waiting);
    require(!read(store,"downstream")["valid"].toBool(),"missing replacement dependency did not invalidate graph");
    store.execute(descriptor(owner,"future"));write(store,owner,"future",oneRow(3));
    require(read(store,"downstream")["rows"]==SwJsonValue(oneRow(3)),"replacement did not recover on dependency creation");
}

void limits() {
    auto store = transientStore(transform);
    const auto session = hello(store, "owner");
    auto limited = descriptor(session, "limited");
    limited["max_rows"] = 1;
    store.execute(limited);
    write(store, session, "limited", oneRow());
    const auto before = read(store, "limited");
    rejects([&] { write(store, session, "limited", array({object({{"id", "two"}, {"value", 2}})})); }, "row capacity ignored");
    require(read(store, "limited") == before, "row capacity failure changed table");
    auto large = descriptor(session, "large");
    large["columns"] = object({{"id", "string"}, {"payload", "string"}});
    store.execute(large);
    const auto largeBefore = read(store, "large");
    rejects([&] { write(store, session, "large", array({object({{"id", "one"},
        {"payload", SwString(std::string(1024 * 1024 - 256, 'x'))}})})); }, "per-table byte limit ignored");
    require(read(store, "large") == largeBefore, "byte capacity failure changed table");
    for (int i = 0; i < 1022; ++i) {
        const auto name = std::string("table_") + std::to_string(i);
        store.execute(descriptor(session, name.c_str()));
    }
    rejects([&] { store.execute(descriptor(session, "too_many")); }, "table capacity ignored");
}

void jsonValidation() {
    auto store = transientStore(transform);
    const auto nest = [](unsigned levels, SwJsonValue value) {
        for (unsigned i = 0; i < levels; ++i) {
            if (i % 2) value = object({{"child", value}});
            else value = array({value});
        }
        return value;
    };
    auto request = object({{"op", "introspect"}, {"extra", nest(31, true)},
        {"empty_object", SwJsonValue(std::shared_ptr<SwJsonObject>())},
        {"empty_array", SwJsonValue(std::shared_ptr<SwJsonArray>())}});
    const auto original = request.toJsonString();
    const auto before = store.execute(request);
    require(request.toJsonString() == original, "JSON validation mutated borrowed input");
    request["extra"] = nest(32, true);
    rejects([&] { store.execute(request); }, "JSON nesting beyond 32 levels accepted");
    for (const double value : {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        request["extra"] = nest(30, value);
        rejects([&] { store.execute(request); }, "nested non-finite request accepted");
    }
    require(store.execute(object({{"op", "introspect"}})) == before,
            "rejected JSON request changed store state");
}

void journalByteBound() {
    auto store = transientStore(transform);
    const auto session = hello(store, "producer");
    store.execute(descriptor(session, "source"));
    SwJsonArray rows;
    for (int i = 0; i < 64; ++i) {
        rows.append(object({{"id", SwString(std::string(125, 'k') + std::to_string(i))}, {"value", i}}));
    }
    const auto initial = write(store, session, "source", rows);
    for (int i = 0; i < 80; ++i) write(store, session, "source", rows);
    auto response = changes(store, initial);
    require(response["resync_required"].toBool(), "journal byte overrun not detected");
    require(response.toJsonString().size() < 1024 * 1024, "resync response too large");
    auto recent = read(store, "source");
    const auto revision = std::stoull(recent["cursor"].toString().toStdString());
    recent["cursor"] = SwString(std::to_string(revision - 20));
    response = changes(store, recent);
    require(!response["resync_required"].toBool() && response["events"].toArray().size() == 20,
            "retained long-key events unavailable");
    require(response.toJsonString().size() < 1024 * 1024, "journal response exceeds IPC bound");
    auto summaryRequest = object({{"op", "changes"}, {"epoch", recent["epoch"]},
                                  {"after", recent["cursor"]}, {"summary", true}});
    auto summary = store.execute(summaryRequest);
    require(summary["tables"].toArray() == array({"source"}) && !summary.contains("events") &&
            !summary["catalog_changed"].toBool() && summary["revision"] == response["revision"],
            "journal summary lost cursor or duplicated table names");
    summaryRequest["table"] = "unrelated";
    require(store.execute(summaryRequest)["tables"].toArray().isEmpty(), "summary ignored table selector");
    summaryRequest["epoch"] = "old-epoch";
    summary = store.execute(summaryRequest);
    require(summary["resync_required"].toBool() && summary["tables"].toArray().isEmpty(),
            "summary lost epoch resynchronization");
}

void subscriptions() {
    auto store = transientStore(transform);
    const auto observer = hello(store, "observer");
    const auto other = hello(store, "other");
    auto request = object({{"op", "subscribe"}, {"session", observer}, {"table", "not_created"}, {"mode", "write"}});
    const auto first = store.execute(request);
    const auto second = store.execute(request);
    require(first["subscription"] != second["subscription"], "distinct subscriptions were merged");
    auto inspection = store.execute(object({{"op", "introspect"}}));
    require(inspection["subscriptions"].toArray().size() == 2, "subscriptions not introspectable");
    require(inspection["actors"].toArray().size() == 2, "actors not introspectable");
    require(inspection.toJsonString().toStdString().find(observer.toStdString()) == std::string::npos,
            "introspection discloses session token");
    auto remove = object({{"op", "unsubscribe"}, {"session", other}, {"subscription", first["subscription"]}});
    rejects([&] { store.execute(remove); }, "foreign session removed subscription");
    remove["session"] = observer;
    store.execute(remove);
    inspection = store.execute(object({{"op", "introspect"}}));
    require(inspection["subscriptions"].toArray().size() == 1, "unsubscribe removed another watcher");
    request["mode"] = "invalid";
    rejects([&] { store.execute(request); }, "unsupported subscription mode accepted");
    request["mode"] = "change";
    for (int i = 0; i < 1023; ++i) store.execute(request);
    rejects([&] { store.execute(request); }, "subscription capacity ignored");
}

std::set<SwString> rowKeys(Store& store, const char* table) {
    std::set<SwString> keys;
    for (const auto& row : read(store, table)["rows"].toArray()) keys.insert(row.toObject()["id"].toString());
    return keys;
}

void retention() {
    auto store = transientStore(transform);
    const auto owner = hello(store, "devices/owner");
    const auto consumer = hello(store, "interfaces/consumer");
    auto bounded = descriptor(owner, "recent");
    bounded["max_rows"] = 3;
    bounded["overflow_policy"] = "evict_oldest_write";
    bounded["writers"] = array({"interfaces/consumer"});
    store.execute(bounded);
    store.execute(view(consumer, "derived_recent", "recent"));
    const auto row = [](const char* id, double value = 1) { return object({{"id", id}, {"value", value}}); };
    write(store, owner, "recent", array({row("a"), row("b"), row("c")}));
    const auto snapshot = read(store, "recent");
    subscribe(store, consumer, "derived_recent");
    const auto viewSnapshot = read(store, "derived_recent");
    write(store, consumer, "recent", array({row("a")}));
    require(changes(store, snapshot)["events"].toArray().size() == 1,
            "equal authorized write did not publish an event");
    require(changes(store, snapshot, "change")["events"].toArray().isEmpty(),
            "write-order metadata was mistaken for a value change");
    require(changes(store, viewSnapshot)["events"].toArray().isEmpty() &&
            changes(store, viewSnapshot, "change")["events"].toArray().isEmpty(),
            "equal source write emitted a derived write without a recalculation");
    read(store, "recent");
    write(store, owner, "recent", array({row("d")}));
    require(rowKeys(store, "recent") == std::set<SwString>{"a", "c", "d"},
            "eviction ignored equal-write recency or a read refreshed retention order");
    require(rowKeys(store, "derived_recent") == rowKeys(store, "recent"), "view retained an evicted source row");

    const auto beforeInvalid = read(store, "recent");
    rejects([&] { write(store, owner, "recent", array({row("e"), object({{"id", "f"}, {"value", "invalid"}})})); },
            "invalid retention batch accepted");
    require(read(store, "recent") == beforeInvalid, "failed validation evicted rows or changed revision");
    write(store, owner, "recent", array({row("e"), row("f")}));
    require(rowKeys(store, "recent") == std::set<SwString>{"d", "e", "f"}, "batch eviction did not use write recency");

    auto hundred = descriptor(owner, "hundred");
    hundred["max_rows"] = 100;
    hundred["overflow_policy"] = "evict_oldest_write";
    store.execute(hundred);
    SwJsonArray initial;
    for (int i = 0; i < 100; ++i) {
        initial.append(object({{"id", SwString::number(i + 1000)}, {"value", i}}));
    }
    write(store, owner, "hundred", initial);
    write(store, owner, "hundred", array({row("new")}));
    require(rowKeys(store, "hundred").size() == 100 && !rowKeys(store, "hundred").count("1000"),
            "max_rows=100 did not retain exactly the latest 100 written rows");

    auto commands = descriptor(owner, "commands");
    commands["max_rows"] = 1; // Generic default must remain reject.
    store.execute(commands);
    write(store, owner, "commands", array({row("pending")}));
    const auto pending = read(store, "commands");
    rejects([&] { write(store, owner, "commands", array({row("next")})); }, "generic default silently evicted a row");
    require(read(store, "commands") == pending, "reject policy lost a pending command");
}

void firstWriteCreatesTable() {
    auto store = transientStore(transform);
    const auto owner = hello(store, "devices/producer");
    auto definition = descriptor(owner, "automatic");
    definition.remove("op");
    definition.remove("session");
    const auto initial = store.execute(object({{"op", "introspect"}}));
    auto request = object({{"op", "write"}, {"session", owner}, {"table", "automatic"},
                          {"definition", definition}, {"rows", array({object({{"id", "one"}, {"value", "invalid"}})})}});
    rejects([&] { store.execute(request); }, "invalid first write accepted");
    const auto rejected = store.execute(object({{"op", "introspect"}}));
    require(rejected["tables"] == initial["tables"] && rejected["revision"] == initial["revision"],
            "invalid first write left a declared table or journal entries");
    request["rows"] = oneRow(7);
    const auto first = store.execute(request);
    require(first["valid"].toBool() && first["rows"] == SwJsonValue(oneRow(7)),
            "first write failed to create and publish the table atomically");
    store.execute(request);
    require(changes(store, first)["events"].toArray().size() == 1 &&
            changes(store, first, "change")["events"].toArray().isEmpty(),
            "repeated definition write changed registration or equal-write semantics");
    auto conflicting = definition;
    conflicting["columns"] = object({{"id", "string"}, {"value", "string"}});
    request["definition"] = conflicting;
    const auto before = read(store, "automatic");
    rejects([&] { store.execute(request); }, "first-write API replaced an existing schema");
    require(read(store, "automatic") == before, "rejected schema replacement changed rows");
    request["definition"] = definition;
    request["session"] = hello(store, "other");
    rejects([&] { store.execute(request); }, "first-write API stole table ownership");
    request["session"] = owner;
    request["table"] = "different";
    rejects([&] { store.execute(request); }, "outer table and descriptor name mismatch accepted");
    request.remove("definition");
    rejects([&] { store.execute(request); }, "unknown table write inferred a schema without a descriptor");

    auto independent = transientStore(transform);
    require(independent.execute(object({{"op", "introspect"}}))["tables"].toArray().isEmpty(),
            "in-memory stores shared or persisted table state");
}

void incrementalRows() {
    auto store = transientStore(transform);
    const auto owner = hello(store, "owner");
    auto definition = descriptor(owner, "delta");
    definition["columns"] = object({{"id", "string"}, {"value", "number"}, {"payload", "string"}});
    definition["max_rows"] = 3;
    definition["overflow_policy"] = "evict_oldest_write";
    store.execute(definition);
    const auto baseline = store.execute(object({{"op", "introspect"}}))["data_bytes"].toLongLong();
    std::map<SwString, SwJsonObject> expected;
    std::map<SwString, unsigned> order;
    unsigned sequence = 0;
    const auto verify = [&] {
        SwJsonArray rows;
        auto bytes = baseline;
        for (const auto& entry : expected) {
            rows.append(entry.second);
            bytes += entry.second.toJsonString().size() + entry.first.size() + 4;
        }
        require(read(store, "delta")["rows"] == SwJsonValue(rows), "incremental mutations diverged from reference rows");
        require(store.execute(object({{"op", "introspect"}}))["data_bytes"].toLongLong() == bytes,
                "incremental byte accounting diverged after replacement, eviction or erase");
    };
    const auto publish = [&](const SwJsonArray& rows) {
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const auto key = row["id"].toString();
            expected[key] = row;
            order[key] = ++sequence;
        }
        while (order.size() > 3) {
            const auto oldest = std::min_element(order.begin(), order.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            expected.erase(oldest->first);
            order.erase(oldest);
        }
        const auto reply = store.execute(object({{"op", "write"}, {"session", owner}, {"table", "delta"},
                                                 {"rows", rows}, {"include_rows", false}}));
        require(!reply.contains("rows") && reply["row_count"].toInt() == static_cast<int>(expected.size()),
                "incremental compact acknowledgement exposed rows or wrong retention count");
        verify();
    };
    publish({});
    for (int i = 0; i < 80; ++i) {
        const auto key = SwString::number(i % 7);
        const auto payload = SwString(std::string((i * 79) % 300, 'x') + "\n\\\"é");
        publish(array({object({{"id", key}, {"value", i}, {"payload", payload}})}));
        if (i % 7 == 0) publish(array({expected.begin()->second})); // Equal writes refresh retention.
        if (i % 3 == 0) {
            const auto removed = SwString::number((i + 1) % 7);
            store.execute(object({{"op", "erase"}, {"session", owner}, {"table", "delta"},
                                  {"keys", array({removed, "absent"})}, {"include_rows", false}}));
            expected.erase(removed);
            order.erase(removed);
            verify();
        }
    }
    // A batch may evict a row it has just introduced. It must not be both
    // upserted and erased in the underlying atomic delta.
    SwJsonArray larger;
    for (int i = 0; i < 5; ++i) larger.append(object({{"id", "new" + SwString::number(i)},
        {"value", i}, {"payload", "batch"}}));
    publish(larger);
}

void catalogNotifications() {
    auto store = transientStore(transform);
    const auto owner = hello(store, "devices/owner");
    const auto observer = hello(store, "interfaces/observer");
    const auto subscription = store.execute(object({{"op", "subscribe"}, {"session", observer},
                                                    {"scope", "catalog"}, {"mode", "create"}}));
    const auto initial = store.execute(object({{"op", "introspect"}}));
    const auto catalog = [&](const SwJsonObject& cursor) {
        return store.execute(object({{"op", "changes"}, {"scope", "catalog"},
            {"epoch", cursor["epoch"]}, {"after", cursor["revision"]}}));
    };
    store.execute(view(observer, "future_view", "future_source"));
    auto events = catalog(initial)["events"].toArray();
    require(events.size() == 1 && events[0].toObject()["kind"].toString() == "created" &&
            events[0].toObject()["table"].toString() == "future_view",
            "catalog failed to report a newly declared pending view exactly once");
    require(!read(store, "future_view")["valid"].toBool() &&
            !read(store, "future_view")["error"].toString().isEmpty(),
            "missing dependency did not expose a pending view error");
    store.execute(descriptor(owner, "future_source"));
    require(!read(store, "future_view")["valid"].toBool(), "empty uninitialized source made its view valid");
    events = catalog(initial)["events"].toArray();
    require(events.size() == 2 && events[1].toObject()["table"].toString() == "future_source",
            "catalog did not report creation of the missing source");
    const auto summary = store.execute(object({{"op", "changes"}, {"scope", "catalog"},
        {"epoch", initial["epoch"]}, {"after", initial["revision"]}, {"summary", true}}));
    require(summary["catalog_changed"].toBool() &&
            summary["tables"].toArray() == array({"future_source", "future_view"}),
            "summary omitted table creation notification");
    const auto beforeWrites = store.execute(object({{"op", "introspect"}}));
    write(store, owner, "future_source", oneRow());
    write(store, owner, "future_source", oneRow());
    store.execute(descriptor(owner, "future_source"));
    require(read(store, "future_view")["valid"].toBool(), "first source publication failed to resolve pending view");
    require(catalog(beforeWrites)["events"].toArray().isEmpty(),
            "writes, view recomputation or repeated registration fabricated creation events");
    store.execute(object({{"op", "unsubscribe"}, {"session", observer},
                          {"subscription", subscription["subscription"]}}));
    require(store.execute(object({{"op", "introspect"}}))["subscriptions"].toArray().isEmpty(),
            "catalog subscription could not be removed");
    auto previousEpoch = beforeWrites;
    previousEpoch["epoch"] = "previous-instance";
    require(catalog(previousEpoch)["resync_required"].toBool(), "catalog omitted restart resynchronization");
}

void storageModes() {
    struct TemporaryPath {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("sw-realtimedb-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryPath() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } temporary;
    SwRealtimeDbOptions options;
    require(options.storage.persistent, "generic storage must be persistent by default");
    require(options.defaultMaxRows == 1024 && options.defaultOverflowPolicy == "reject",
            "generic retention defaults must not depend on a host application");
    options.storage.dbPath = (temporary.path / "persistent").string();
    SwString oldEpoch;
    // Reads return primary-key order, independently of publication array order.
    const auto retained = array({object({{"id", "old_only"}, {"value", 12}}), oneRow(9)[0]});
    {
        Store store(transform, options);
        const auto owner = hello(store, "devices/owner");
        const auto consumer = hello(store, "interfaces/consumer");
        store.execute(descriptor(owner, "source"));
        store.execute(view(consumer, "derived", "source"));
        write(store, owner, "source", retained);
        require(read(store, "derived")["rows"] == SwJsonValue(retained), "materialized view did not persist its source snapshot");
        store.execute(writeDefinitionRequest(owner, "automatic_persisted", retained));
        oldEpoch = read(store, "source")["epoch"].toString();
    }
    {
        Store reopened(transform, options);
        const auto catalog = reopened.execute(object({{"op", "introspect"}}));
        require(catalog["tables"].toArray().size() == 3, "persistent reopen lost table/view descriptors");
        require(catalog["actors"].toArray().isEmpty(), "persistent reopen restored live actor sessions");
        require(catalog["epoch"].toString() != oldEpoch, "persistent reopen reused the service epoch");
        require(read(reopened, "source")["rows"] == SwJsonValue(retained) &&
                read(reopened, "derived")["rows"] == SwJsonValue(retained),
                "persistent reopen lost retained source or view rows");
        require(!read(reopened, "source")["valid"].toBool() && !read(reopened, "derived")["valid"].toBool(),
                "persistent rows must stay stale until their producers return");
        const auto owner = hello(reopened, "devices/owner");
        const auto consumer = hello(reopened, "interfaces/consumer");
        reopened.execute(descriptor(owner, "source"));
        reopened.execute(view(consumer, "derived", "source"));
        require(!read(reopened, "source")["valid"].toBool(), "re-registering persisted schema freshened measurements");
        write(reopened, owner, "source", oneRow(11));
        require(read(reopened, "source")["rows"] == SwJsonValue(oneRow(11)) &&
                read(reopened, "derived")["rows"] == SwJsonValue(oneRow(11)),
                "persistent recovery must replace stale rows with a fresh producer snapshot");

        // This producer uses only write + definition for its second table. It
        // must recover the persisted contract without a register_table call.
        const auto stale = read(reopened, "automatic_persisted");
        require(!stale["valid"].toBool() && stale["rows"] == SwJsonValue(retained),
                "persisted automatic table did not retain stale rows");
        auto request = writeDefinitionRequest(owner, "automatic_persisted",
            array({object({{"id", "one"}, {"value", "invalid"}})}));
        rejects([&] { reopened.execute(request); }, "invalid definition write recovered a persisted table");
        require(read(reopened, "automatic_persisted") == stale, "invalid persisted recovery changed rows or validity");
        rejects([&] { write(reopened, owner, "automatic_persisted", oneRow()); },
                "failed persisted recovery attached the new owner session");
        request["rows"] = oneRow(21);
        const auto recovered = reopened.execute(request);
        require(recovered["valid"].toBool() && recovered["rows"] == SwJsonValue(oneRow(21)),
                "definition write did not recover persisted table as a fresh snapshot");
        const auto created = reopened.execute(object({{"op", "changes"}, {"scope", "catalog"},
            {"mode", "create"}, {"epoch", stale["epoch"]}, {"after", stale["cursor"]}}));
        require(created["events"].toArray().isEmpty(), "persisted recovery fabricated a table creation");
    }
    options.storage.persistent = false;
    const auto memoryPath = temporary.path / "must_not_exist";
    options.storage.dbPath = memoryPath.string();
    {
        Store memory(transform, options);
        const auto owner = hello(memory, "memory_owner");
        memory.execute(descriptor(owner, "memory_table"));
        write(memory, owner, "memory_table", oneRow());
    }
    require(!std::filesystem::exists(memoryPath), "in-memory RtDb created storage files");
}

void leases() {
    int observedCalls = 0;
    auto store = transientStore([&](const SwString& script, const SwJsonObject& inputs) {
        if (script == "observer_source") ++observedCalls;
        return transform(script, inputs);
    });
    auto producer = hello(store, "producer");
    const auto consumer = hello(store, "consumer");
    store.execute(object({{"op", "subscribe"}, {"session", producer}, {"table", "source"}, {"mode", "write"}}));
    store.execute(object({{"op", "subscribe"}, {"session", consumer}, {"table", "derived"}, {"mode", "change"}}));
    store.execute(descriptor(producer, "source"));
    store.execute(view(consumer, "derived", "source"));
    const auto retained = array({oneRow(9)[0], object({{"id", "old_only"}, {"value", 12}})});
    write(store, producer, "source", retained);
    store.execute(writeDefinitionRequest(producer, "automatic_lease", retained));
    store.execute(view(consumer, "automatic_derived", "automatic_lease"));
    // This source and view outlive their only observer. Removing an expired
    // subscription must stop eager work independently of producer freshness.
    store.execute(descriptor(consumer, "observer_source"));
    write(store, consumer, "observer_source", oneRow(1));
    store.execute(view(consumer, "observer_view", "observer_source"));
    subscribe(store, producer, "observer_view");
    observedCalls = 0;
    write(store, consumer, "observer_source", oneRow(2));
    require(observedCalls == 1, "live observer failed to activate its view");
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    store.execute(object({{"op", "heartbeat"}, {"session", consumer}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    store.expire();
    const auto observers = store.execute(object({{"op", "introspect"}}))["subscriptions"].toArray();
    require(observers.size() == 1 && observers[0].toObject()["actor"].toString() == "consumer",
            "expiration did not remove only expired subscriptions");
    observedCalls = 0;
    write(store, consumer, "observer_source", oneRow(3));
    require(observedCalls == 0, "expired observer kept eager view evaluation active");
    require(read(store, "observer_view")["rows"] == SwJsonValue(oneRow(3)) && observedCalls == 1,
            "view with an expired observer failed to materialize on demand");
    require(!read(store, "source")["valid"].toBool(), "expired producer remains fresh");
    require(!read(store, "derived")["valid"].toBool(), "stale source did not invalidate view");
    require(read(store, "source")["rows"].toArray().size() == 2, "expiration destroyed retained data");
    rejects([&] { write(store, producer, "source", oneRow()); }, "expired session accepted");
    producer = hello(store, "producer");
    store.execute(object({{"op", "heartbeat"}, {"session", producer}}));
    require(!read(store, "source")["valid"].toBool(), "heartbeat freshened retained data");
    const auto staleDerived = read(store, "automatic_derived");
    // Materializing an unobserved stale descendant can advance the global
    // journal cursor. Take the atomicity baseline after that explicit read.
    const auto staleAutomatic = read(store, "automatic_lease");
    require(!staleAutomatic["valid"].toBool() && !staleDerived["valid"].toBool(),
            "expired automatic table or its view remained valid");
    auto automatic = writeDefinitionRequest(producer, "automatic_lease",
        array({object({{"id", "one"}, {"value", "invalid"}})}));
    rejects([&] { store.execute(automatic); }, "invalid definition write recovered an expired table");
    require(read(store, "automatic_lease") == staleAutomatic, "failed lease recovery mutated retained table");
    rejects([&] { write(store, producer, "automatic_lease", oneRow()); },
            "failed lease recovery attached the new owner session");
    automatic["rows"] = oneRow(31);
    const auto recoveredAutomatic = store.execute(automatic);
    require(recoveredAutomatic["valid"].toBool() && recoveredAutomatic["rows"] == SwJsonValue(oneRow(31)),
            "definition write did not reclaim an expired table without registration");
    require(read(store, "automatic_derived")["valid"].toBool() &&
            read(store, "automatic_derived")["rows"] == SwJsonValue(oneRow(31)),
            "automatic lease recovery did not refresh its dependent view");
    const auto created = store.execute(object({{"op", "changes"}, {"scope", "catalog"},
        {"mode", "create"}, {"epoch", staleAutomatic["epoch"]}, {"after", staleAutomatic["cursor"]}}));
    require(created["events"].toArray().isEmpty(), "lease recovery fabricated a table creation");
    rejects([&] { write(store, producer, "source", oneRow()); }, "new lease wrote before descriptor registration");
    store.execute(descriptor(producer, "source"));
    require(!read(store, "source")["valid"].toBool(), "registration freshened retained data");
    rejects([&] { store.execute(object({{"op", "erase"}, {"session", producer}, {"table", "source"},
        {"keys", array({"absent"})}})); }, "erase freshened stale rows");
    write(store, producer, "source", oneRow(9));
    require(read(store, "source")["valid"].toBool() && read(store, "derived")["valid"].toBool(), "publication did not recover graph");
    require(read(store, "source")["rows"] == SwJsonValue(oneRow(9)), "new lease freshened omitted old rows");
}

void indexedReads() {
    auto store = transientStore(transform);
    const auto owner = hello(store, "owner");
    auto definition = [&](const char* name, const char* field, const char* type, bool indexed, bool nullable = false) {
        auto result = descriptor(owner, name);
        result["columns"] = object({{"id", "string"}, {field,
            object({{"type", type}, {"nullable", nullable}, {"indexed", indexed}})}});
        return result;
    };
    auto invalid = definition("invalid", "value", "integer", true);
    auto columns = invalid["columns"].toObject();
    columns["value"] = object({{"type", "integer"}, {"indexed", "true"}});
    invalid["columns"] = columns;
    rejects([&] { store.execute(invalid); }, "non-boolean indexed descriptor accepted");
    rejects([&] { store.execute(definition("invalid", "value", "object", true)); },
            "non-scalar index accepted");
    int caseNumber = 0;
    auto compare = [&](const char* field, const char* type, bool nullable, const SwJsonArray& values) {
        const SwString indexed = SwString("indexed_") + SwString::number(++caseNumber);
        const SwString plain = SwString("plain_") + SwString::number(caseNumber);
        store.execute(definition(indexed.c_str(), field, type, true, nullable));
        store.execute(definition(plain.c_str(), field, type, false, nullable));
        SwJsonArray rows;
        for (std::size_t i = 0; i < values.size(); ++i)
            rows.append(object({{"id", SwString::number(values.size() - i)}, {field, values[i]}}));
        for (const auto& name : {indexed, plain}) write(store, owner, name.c_str(), rows);
        const auto verify = [&] {
            for (int limit : {0, 1, 2, 250, 251, 1024}) {
                auto request = object({{"op", "read"}, {"table", indexed}, {"order_by", field}, {"limit", limit}});
                const auto actual = store.execute(request);
                request["table"] = plain;
                const auto expected = store.execute(request);
                require(actual["rows"] == expected["rows"] && actual["cursor"] == expected["cursor"],
                        "indexed selection differs from stable RtDb ordering or snapshot cursor");
                for (const auto& row : actual["rows"].toArray())
                    require(!row.toObject().contains("__sw_rtdb"), "indexed read leaked retention metadata");
            }
            auto request = object({{"op", "read"}, {"table", indexed}, {"order_by", field}, {"limit", 1},
                                   {"keys", array({"1", "2", "absent"})}});
            const auto selected = store.execute(request)["rows"];
            request["table"] = plain;
            require(selected == store.execute(request)["rows"], "index ignored an explicit keys restriction");
            const auto full = read(store, indexed.c_str())["rows"].toArray();
            const auto first = store.execute(object({{"op", "read"}, {"table", indexed}, {"limit", 1}}));
            require(first["rows"] == SwJsonValue(full.isEmpty() ? SwJsonArray() : array({full[0]})),
                    "limited primary-key scan changed ordinary read ordering");
        };
        verify();
        const auto updated = object({{"id", SwString::number(values.size())}, {field, values[values.size() - 1]}});
        for (const auto& name : {indexed, plain}) {
            write(store, owner, name.c_str(), array({updated}));
            store.execute(object({{"op", "erase"}, {"session", owner}, {"table", name}, {"keys", array({"2"})}}));
        }
        verify();
    };
    compare("value", "integer", false, array({std::numeric_limits<long long>::max(), 0,
        std::numeric_limits<long long>::min(), 7, 7}));
    compare("value", "string", false, array({"", "aa", "é", "a", "a"}));
    compare("value", "integer", true, array({SwJsonValue(), 0, -7, 7, SwJsonValue()}));
    compare("value", "number", false, array({-0.0, 0.0, -4.25, 1e200, -1e200}));
    compare(" value ", "integer", false, array({4, 2, 3, 1, 1}));
    SwJsonArray many;
    for (int i = 0; i < 260; ++i) many.append((260 - i) % 17);
    compare("value", "integer", false, many);
    auto derived = definition("derived", "value", "integer", true);
    derived["op"] = "register_view";
    derived["dependencies"] = array({"indexed_1"});
    derived["script"] = "indexed_1";
    store.execute(derived);
    write(store, owner, "indexed_1", array({object({{"id", "3"}, {"value", 100}})}));
    auto selection = object({{"op", "read"}, {"table", "derived"}, {"order_by", "value"}, {"limit", 1}});
    const auto materialized = store.execute(selection);
    selection["table"] = "indexed_1";
    const auto source = store.execute(selection);
    require(materialized["valid"].toBool() && materialized["rows"] == source["rows"] &&
            materialized["cursor"] == source["cursor"], "indexed lazy view read used its previous materialization");

    struct TemporaryPath {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("sw-rtdb-index-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryPath() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } temporary;
    SwRealtimeDbOptions options;
    options.storage.dbPath = temporary.path.string();
    auto persisted = definition("persisted", "value", "integer", true);
    const auto firstRows = array({object({{"id", "a"}, {"value", 9}}), object({{"id", "b"}, {"value", -5}})});
    const auto firstRead = object({{"op", "read"}, {"table", "persisted"}, {"order_by", "value"}, {"limit", 1}});
    {
        Store disk(transform, options);
        const auto session = hello(disk, "owner");
        persisted["session"] = session;
        disk.execute(persisted);
        write(disk, session, "persisted", firstRows);
    }
    {
        Store disk(transform, options);
        require(disk.execute(firstRead)["rows"] == SwJsonValue(array({firstRows[1]})),
                "native index lost its entries after persistent reopen");
        const auto session = hello(disk, "owner");
        persisted["session"] = session;
        disk.execute(persisted);
        const auto fresh = array({object({{"id", "new"}, {"value", 42}})});
        write(disk, session, "persisted", fresh);
        require(disk.execute(firstRead)["rows"] == SwJsonValue(fresh),
                "fresh producer snapshot retained stale native index entries");
    }
}

#include "RealtimeDbSingleRowTest.h"

} // namespace

int main() {
    try {
        const auto run = [](const char* name, const auto& test) {
            try { test(); }
            catch (const std::exception& error) {
                throw std::runtime_error(std::string(name) + ": " + error.what());
            }
        };
        run("tablesAndJournal", tablesAndJournal);
        run("cachedViewChain", cachedViewChain);
        run("diamondViews", diamondViews);
        run("demandViews", demandViews);
        run("reactiveViews", reactiveViews);
        run("replaceViews", replaceViews);
        run("limits", limits);
        run("jsonValidation", jsonValidation);
        run("journalByteBound", journalByteBound);
        run("subscriptions", subscriptions);
        run("retention", retention);
        run("incrementalRows", incrementalRows);
        run("singleRowSnapshots", singleRowSnapshots);
        run("preparedRowValidation", preparedRowValidation);
        run("indexedReads", indexedReads);
        run("firstWriteCreatesTable", firstWriteCreatesTable);
        run("catalogNotifications", catalogNotifications);
        run("storageModes", storageModes);
        run("leases", leases);
        std::cout << "RtDb store tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RtDb store test failed: " << error.what() << '\n';
        return 1;
    }
}
