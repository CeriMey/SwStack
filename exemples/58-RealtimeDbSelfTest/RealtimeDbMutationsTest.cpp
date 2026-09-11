#include "SwRealtimeDb.h"
#include "SwRealtimeDbJsEvaluator.h"

#include <iostream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
SwJsonObject object(std::initializer_list<std::pair<const char*, SwJsonValue>> values) {
    SwJsonObject result;
    for (const auto& item : values) result[item.first] = item.second;
    return result;
}
SwJsonArray array(std::initializer_list<SwJsonValue> values) {
    SwJsonArray result;
    for (const auto& item : values) result.append(item);
    return result;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function) {
    try { function(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("invalid batch accepted");
}
SwJsonObject definition(const char* table) {
    return object({{"table", table}, {"key", "id"}, {"schema_version", 1},
        {"columns", object({{"id", "string"}, {"value", "integer"}})}});
}
SwJsonObject write(const char* table, int value, bool create = false) {
    auto result = object({{"op", "write"}, {"table", table},
        {"rows", array({object({{"id", "current"}, {"value", value}})})}});
    if (create) result["definition"] = definition(table);
    return result;
}
SwJsonObject batch(SwRealtimeDb& store, const SwString& session, const SwJsonArray& operations) {
    return store.execute(object({{"op", "mutate_many"}, {"session", session}, {"operations", operations}}));
}
SwJsonObject read(SwRealtimeDb& store, const char* table) {
    return store.execute(object({{"op", "read"}, {"table", table}}));
}
SwJsonArray events(SwRealtimeDb& store, const SwString& epoch, const SwString& after, const char* mode = "write") {
    return store.execute(object({{"op", "changes"}, {"epoch", epoch}, {"after", after}, {"mode", mode}}))["events"].toArray();
}
int value(SwRealtimeDb& store, const char* table) {
    return read(store, table)["rows"].toArray()[0].toObject()["value"].toInt();
}
}

int main() {
    try {
        SwRealtimeDbOptions options; options.storage.persistent = false;
        SwRealtimeDbJsEvaluator evaluator(50);
        int evaluations = 0;
        SwRealtimeDb store([&](const SwString& script, const SwJsonObject& inputs) {
            ++evaluations; return evaluator.evaluate(script, inputs);
        }, options);
        const auto ownerReply = store.execute(object({{"op", "hello"}, {"actor", "owner"}}));
        const auto owner = ownerReply["session"].toString(), epoch = ownerReply["epoch"].toString();
        const auto other = store.execute(object({{"op", "hello"}, {"actor", "other"}}))["session"].toString();
        require(batch(store, owner, array({write("source", 0, true)}))["complete"].toBool(), "first write did not create table");
        auto view = definition("derived");
        view["op"] = "register_view"; view["session"] = owner;
        view["dependencies"] = array({"source"});
        view["script"] = "return [{id:'current', value:tables.source[0].value * 2}];";
        store.execute(view);
        store.execute(object({{"op", "subscribe"}, {"session", owner}, {"table", "derived"}, {"mode", "write"}}));
        const auto before = store.execute(object({{"op", "introspect"}}))["revision"].toString();
        evaluations = 0;
        const auto ordered = batch(store, owner, array({write("source", 1), write("source", 1), write("source", 2)}));
        require(ordered["complete"].toBool() && ordered["applied"].toInt() == 3, "ordered writes failed");
        require(evaluations == 2 && value(store, "derived") == 4, "batch skipped intermediate JS evaluations");
        const auto journal = events(store, epoch, before);
        require(journal.size() == 5, "batch lost writes or recomputed an equal input");
        const char* expectedTables[] = {"source", "derived", "source", "source", "derived"};
        for (int i = 0; i < 5; ++i) {
            const auto entry = journal[i].toObject();
            require(entry["table"].toString() == expectedTables[i], "write/view ordering changed");
            require(entry["changed"].toBool() == (i != 2), "equal source write changed flag lost");
        }
        require(events(store, epoch, before, "change").size() == 4, "write and change subscriptions became equivalent");
        for (const auto& item : ordered["results"].toArray()) {
            const auto result = item.toObject();
            require(!result.contains("rows") && !result.contains("evicted_keys") && result["outcome"].toString() == "applied",
                    "mutation acknowledgement is not compact");
        }
        // A compact child receipt must still include every observed view
        // revision produced by this write, while keeping its own table revision.
        for (bool includeRows : {false, true}) {
            auto operation = write("source", 15); operation["include_rows"] = includeRows;
            const auto result = batch(store, owner, array({operation}))["results"].toArray()[0].toObject();
            const auto source = read(store, "source"), derived = read(store, "derived");
            require(result["revision"] == source["revision"] && result["cursor"] == derived["cursor"] &&
                    (includeRows ? result["cursor"] == result["revision"] :
                     result["cursor"].toString().toLongLong() > result["revision"].toString().toLongLong()) &&
                    result["row_count"].toInt() == 1 && result["evicted_count"].toInt() == 0,
                    "compact receipt lost source or derived revisions");
        }
        for (const auto bad : {SwJsonValue(), SwJsonValue("false")}) {
            auto operation = write("source", 16); operation["include_rows"] = bad;
            const auto result = batch(store, owner, array({write("created-options", 1, true), operation,
                write("source", 17)}));
            require(!result["complete"].toBool() && result["failed_index"].toInt() == 1 &&
                    result["applied"].toInt() == 1 && value(store, "source") == 15,
                    "malformed include_rows changed ordered validation");
        }
        const SwJsonValue nullObject{std::shared_ptr<SwJsonObject>()};
        const auto nullChild = batch(store, owner, array({write("created-options", 2), nullObject,
            write("created-options", 3)}));
        require(!nullChild["complete"].toBool() && nullChild["failed_index"].toInt() == 1 &&
                nullChild["results"].toArray()[1].toObject()["outcome"].toString() == "failed" &&
                value(store, "created-options") == 2, "null object child crashed or bypassed ordered rejection");
        auto retained = write("retained", 1, true);
        (*retained["definition"].toObjectPtr())["max_rows"] = 1;
        (*retained["definition"].toObjectPtr())["overflow_policy"] = "evict_oldest_write";
        batch(store, owner, array({retained}));
        auto replacement = write("retained", 2);
        (*(*replacement["rows"].toArrayPtr())[0].toObjectPtr())["id"] = "next";
        const auto eviction = batch(store, owner, array({replacement}))["results"].toArray()[0].toObject();
        require(eviction["row_count"].toInt() == 1 && eviction["evicted_count"].toInt() == 1 &&
                value(store, "retained") == 2, "compact mutation receipt lost eviction accounting");
        batch(store, other, array({write("foreign", 9, true)}));
        const auto partial = batch(store, owner, array({write("created", 3, true), write("foreign", 4), write("skipped", 5, true)}));
        const auto outcomes = partial["results"].toArray();
        require(!partial["complete"].toBool() && partial["applied"].toInt() == 1 && partial["failed_index"].toInt() == 1,
                "partial failure did not retain confirmed count");
        require(outcomes[0].toObject()["outcome"].toString() == "applied" &&
                outcomes[1].toObject()["outcome"].toString() == "failed" &&
                outcomes[2].toObject()["outcome"].toString() == "not_run", "partial outcomes are ambiguous");
        require(value(store, "created") == 3 && value(store, "foreign") == 9, "failed batch rolled back or bypassed ownership");
        rejects([&] { read(store, "skipped"); });
        auto erase = object({{"op", "erase"}, {"table", "created"}, {"keys", array({"current"})}});
        const auto erased = batch(store, owner, array({erase, write("created", 7), erase}));
        require(erased["complete"].toBool() && read(store, "created")["rows"].toArray().isEmpty(), "erase/write sequence reordered");
        auto duplicate = write("created", 1); duplicate["rows"] = array({object({{"id", "x"}, {"value", 1}}), object({{"id", "x"}, {"value", 2}})});
        const auto invalid = batch(store, owner, array({write("created", 8), duplicate, write("created", 10)}));
        require(!invalid["complete"].toBool() && value(store, "created") == 8, "per-operation row validation changed");
        auto forbiddenSession = write("foreign", 1); forbiddenSession["session"] = other;
        require(!batch(store, owner, array({forbiddenSession}))["complete"].toBool() && value(store, "foreign") == 9,
                "nested session bypassed batch authority");
        require(!batch(store, owner, array({object({{"op", "read"}, {"table", "source"}})}))["complete"].toBool(),
                "non-mutation accepted in batch");
        const auto malformed = batch(store, owner, array({write("created", 11), 42, write("created", 12)}));
        require(!malformed["complete"].toBool() && value(store, "created") == 11, "malformed entry did not fail at its ordered position");
        SwJsonArray maximum;
        for (int i = 0; i < 64; ++i) maximum.append(write("created", i));
        const auto bounded = batch(store, owner, maximum);
        require(bounded["complete"].toBool() && bounded["results"].toArray().size() == 64 &&
                bounded.toJsonString().size() < 16 * 1024 && value(store, "created") == 63, "maximum batch or compact reply limit failed");
        maximum.append(write("created", 64));
        rejects([&] { batch(store, owner, maximum); });
        rejects([&] { batch(store, owner, {}); });
        rejects([&] { batch(store, "invalid-session", array({write("created", 99)})); });
        auto oversized = write("created", 99); oversized["padding"] = SwString(std::string(1024 * 1024, 'x'));
        rejects([&] { batch(store, owner, array({oversized})); });
        require(value(store, "created") == 63, "envelope rejection committed data");
        std::cout << "PASS: ordered bounded mutations, partial commits, ownership, creation, erasure and exact JS events\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
