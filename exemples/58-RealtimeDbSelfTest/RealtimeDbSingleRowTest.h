#pragma once

// Included beside the other store tests, sharing their request helpers.
void singleRowSnapshots() {
    auto store = transientStore([](const SwString&, const SwJsonObject& inputs) {
        // Const JSON values still expose mutable children. Inputs must remain
        // detached even when a public transform deliberately uses those APIs.
        auto rows = inputs["single"].toArrayPtr();
        if (rows && !rows->isEmpty()) {
            auto row = (*rows)[0].toObjectPtr();
            (*(*row)["payload"].toObjectPtr())["nested"] = 99;
        }
        return rows ? *rows : SwJsonArray();
    });
    const auto owner = hello(store, "owner");
    auto definition = descriptor(owner, "single");
    definition["columns"] = object({{"id", "string"}, {"payload", "object"}, {"value", "integer"}});
    definition["max_rows"] = 1;
    definition["overflow_policy"] = "evict_oldest_write";
    store.execute(definition);
    auto derived = definition;
    derived["op"] = "register_view";
    derived["table"] = "derived_single";
    derived["dependencies"] = array({"single"});
    derived["script"] = "mutate_input";
    store.execute(derived);
    require(read(store, "single")["rows"].toArray().isEmpty(), "empty depth-one read invented a row");
    const auto row = [](const char* id, int value) {
        return object({{"id", id}, {"value", value}, {"payload", object({{"nested", value}})}});
    };
    const auto expected = array({row(" first ", 7)});
    write(store, owner, "single", expected);
    auto snapshot = read(store, "single");
    auto exposed = (*snapshot["rows"].toArrayPtr())[0].toObjectPtr();
    (*(*exposed)["payload"].toObjectPtr())["nested"] = -1;
    require(read(store, "single")["rows"] == SwJsonValue(expected), "public depth-one snapshot aliases storage");
    const auto derivedRows = read(store, "derived_single")["rows"].toArray();
    require(derivedRows.size() == 1 && derivedRows[0].toObject()["payload"].toObject()["nested"].toInt() == 99,
            "public transform did not receive its mutable detached input");
    require(read(store, "single")["rows"] == SwJsonValue(expected), "transform input aliases depth-one storage");
    const auto verifyRead = [&](SwJsonObject selection, const SwJsonArray& wanted) {
        selection["op"] = "read";
        selection["table"] = "single";
        require(store.execute(selection)["rows"] == SwJsonValue(wanted), "depth-one selector changed rows");
    };
    verifyRead(object({{"limit", 1}}), expected);
    verifyRead(object({{"order_by", "value"}, {"limit", 1}}), expected);
    verifyRead(object({{"keys", array({" first ", "missing"})}}), expected);
    verifyRead(object({{"keys", array({"missing"})}}), {});
    verifyRead(object({{"limit", 0}}), {});
    write(store, owner, "single", array({row("second", 8), row("third", 9)}));
    require(read(store, "single")["rows"] == SwJsonValue(array({row("third", 9)})),
            "depth-one point read missed batch eviction order");
    store.execute(object({{"op", "erase"}, {"session", owner}, {"table", "single"}, {"keys", array({"third"})}}));
    require(read(store, "single")["rows"].toArray().isEmpty() && read(store, "derived_single")["rows"].toArray().isEmpty(),
            "depth-one erase retained a source or derived row");
}

void preparedRowValidation() {
    auto store = transientStore(transform);
    const auto owner = hello(store, "owner");
    auto definition = descriptor(owner, "prepared");
    definition["columns"] = object({{"id", "string"}, {"flag", "bool"}, {"integer", "integer"},
        {"number", "number"}, {"object", "object"}, {"array", "array"}, {"json", "json"},
        {"optional", object({{"type", "number"}, {"nullable", true}})}});
    definition["max_rows"] = 1;
    store.execute(definition);
    auto good = object({{"id", "one"}, {"flag", true}, {"integer", 4}, {"number", 4.25},
        {"object", object({{"x", 1}})}, {"array", array({1, 2})}, {"json", array({false, "x"})},
        {"optional", SwJsonValue()}});
    write(store, owner, "prepared", array({good}));
    const auto before = read(store, "prepared");
    for (const auto* field : {"id", "flag", "integer", "number", "object", "array"}) {
        auto bad = good;
        bad[field] = SwJsonValue();
        rejects([&] { write(store, owner, "prepared", array({bad})); }, "prepared type accepted forbidden null");
        require(read(store, "prepared") == before, "prepared validation failure altered table state");
    }
    auto fractional = good;
    fractional["integer"] = 4.5;
    rejects([&] { write(store, owner, "prepared", array({fractional})); }, "prepared integer accepted fractional value");
    store.execute(object({{"op", "write"}, {"session", owner}, {"table", "prepared"}, {"merge", true},
        {"rows", array({object({{"id", "one"}, {"number", 8.5}})})}}));
    good["number"] = 8.5;
    require(read(store, "prepared")["rows"] == SwJsonValue(array({good})),
            "prepared merge lost a retained or nullable field");
}
