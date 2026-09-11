#pragma once

// Included inside the self-test's anonymous namespace, after its common helpers.
SwJsonObject jsonRow(int number) {
    SwJsonObject child;
    child["number"] = number;
    SwJsonArray array;
    array.append(child);
    SwJsonObject row;
    row["array"] = array;
    row["double"] = -0.0;
    row["integer"] = std::numeric_limits<long long>::max();
    return row;
}

void alterJsonRow(const SwJsonObject& row, int number) {
    // Deliberately mutate children reached from a const public result.
    const auto array = row["array"].toArrayPtr();
    (*array->dataRef()[0].toObjectPtr())["number"] = number;
}

void opaqueJsonRecords(SwEmbeddedDb& db, bool persistent) {
    SwDbJsonRecord missing;
    require(!missing.isValid() && missing.byteSize() == 0 && missing.detach().isEmpty() &&
            !missing.equals({}), "default opaque record is not empty");
    auto input = jsonRow(8);
    input["rounding"] = 0.99999999999999989;
    input["tenth"] = 0.1;
    const SwString excluded = "omit\"\n";
    input[excluded] = "value\"\n";
    const auto original = input;
    SwDbWriteBatch initial;
    initial.putJson(bytes("record"), input);
    initial.putJson(bytes("unrelated"), jsonRow(7));
    std::weak_ptr<const swEmbeddedDbDetail::JsonPayload_> unrelated = initial.operations()[1].jsonPayload;
    ok(db.write(initial), "opaque initial write");
    initial.clear();
    SwDbJsonRecord record;
    ok(db.getJsonRecord(bytes("record"), &record), "opaque point read");
    std::atomic<bool> consistent{true};
    auto reader = [&] {
        for (int i = 0; i < 64; ++i)
            if (record.byteSize() != original.toJsonString().size() || !record.equals(original)) consistent = false;
    };
    std::thread one(reader), two(reader); one.join(); two.join();
    require(consistent.load(), "shared opaque size/equality changed between readers");
    alterJsonRow(input, 999);
    require(record.equals(original), "opaque record admitted caller alias");
    const auto detached = record.detach();
    alterJsonRow(detached, 998);
    require(record.equals(original), "opaque detach exposed a mutable child");
    auto plain = original; plain.remove(excluded);
    require(record.equals(plain, excluded) && record.byteSize(excluded) == plain.toJsonString().size(),
            "opaque exclusion lost escaped key/value or member comma");
    plain[excluded] = jsonRow(500);
    require(record.equals(plain, excluded), "opaque equality excluded only the stored member");
    plain["double"] = 1.0;
    require(!record.equals(plain, excluded), "opaque equality ignored a real row change");
    require(record.byteSize() == original.toJsonString().size(), "opaque full compact size mismatch");
    {
        struct Rounding {
            int previous = std::fegetround();
            ~Rounding() { std::fesetround(previous); }
        } rounding;
        auto excludedOriginal = original; excludedOriginal.remove(excluded);
        for (const auto mode : {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO, FE_TONEAREST}) {
            require(std::fesetround(mode) == 0, "cannot change record size rounding mode");
            require(record.byteSize() == original.toJsonString().size() &&
                    record.byteSize(excluded) == excludedOriginal.toJsonString().size(),
                    "opaque cached size ignored directed numeric rounding");
        }
    }
    SwDbWriteBatch replace;
    replace.putJson(bytes("record"), jsonRow(9));
    replace.putJson(bytes("unrelated"), jsonRow(10));
    ok(db.write(replace), "overwrite while opaque record retained");
    require(persistent || unrelated.expired(), "point handle retained the entire memory state");
    require(record.equals(original), "opaque record changed after a newer commit");
    SwDbWriteBatch edge;
    SwJsonObject sole; sole[excluded] = "\t";
    edge.putJson(bytes("sole"), sole);
    edge.putJson(bytes("empty"), SwJsonObject());
    edge.put(bytes("raw"), bytes(original.toJsonString().toStdString()));
    edge.put(bytes("corrupt"), bytes("invalid-json"));
    ok(db.write(edge), "opaque raw/empty write");
    SwDbJsonRecord value;
    ok(db.getJsonRecord(bytes("sole"), &value), "opaque sole read");
    require(value.byteSize(excluded) == 2 && value.equals({}, excluded), "sole exclusion lost empty object braces");
    ok(db.getJsonRecord(bytes("empty"), &value), "opaque empty read");
    require(value.byteSize(excluded) == 2 && value.equals(sole, excluded), "absent stored exclusion retained candidate member");
    ok(db.getJsonRecord(bytes("raw"), &value), "opaque raw read");
    require(value.equals(original) && value.byteSize() == original.toJsonString().size(), "opaque byte fallback differs");
    require(db.getJsonRecord(bytes("corrupt"), &value).code() == SwDbStatus::Corruption && value.equals(original),
            "opaque corrupt read changed prior output or accepted invalid JSON");
    if (!persistent) {
        SwJsonObject deep; deep["leaf"] = 1;
        for (int level = 0; level < 63; ++level) {
            SwJsonObject parent; parent["child"] = std::move(deep); deep = std::move(parent);
        }
        SwDbWriteBatch nested;
        nested.putJson(bytes("depth64"), deep);
        SwJsonObject deeper; deeper["child"] = std::move(deep);
        nested.putJson(bytes("depth65"), deeper);
        ok(db.write(nested), "opaque bounded depth admission");
        ok(db.getJsonRecord(bytes("depth64"), &value), "opaque depth64 read");
        require(value.byteSize() > 0, "opaque size rejected depth64");
        ok(db.getJsonRecord(bytes("depth65"), &value), "opaque depth65 read");
        bool rejected = false;
        try { (void)value.byteSize(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && value.isValid(), "opaque size failed to bound nested generic JSON");
        SwJsonObject nonfinite; nonfinite["value"] = std::numeric_limits<double>::infinity();
        SwDbWriteBatch nonfiniteBatch; nonfiniteBatch.putJson(bytes("nonfinite"), nonfinite);
        ok(db.write(nonfiniteBatch), "opaque non-finite legacy admission");
        ok(db.getJsonRecord(bytes("nonfinite"), &value), "opaque non-finite record read");
        rejected = false;
        try { (void)value.byteSize(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "opaque size accepted non-finite JSON");
        const auto cycle = std::make_shared<SwJsonObject>();
        (*cycle)["self"] = SwJsonValue(cycle);
        rejected = false;
        try { (void)SwJsonSize(std::numeric_limits<std::size_t>::max()).object(*cycle); }
        catch (const std::runtime_error&) { rejected = true; }
        cycle->remove("self"); // Release the deliberate caller-owned cycle.
        require(rejected, "generic size recursed without a depth bound");
    }
    ok(db.sync(), "opaque sync");
    db.close();
    require(record.equals(original), "opaque record lifetime ended when database closed");
}

void testJsonStorage(const std::filesystem::path& path, bool persistent) {
    SwEmbeddedDbOptions options;
    options.persistent = persistent;
    options.dbPath = SwString(path.string());
    options.commitWindowMs = 0;
    options.inlineBlobThresholdBytes = 1; // Persistent typed writes must still use blobs.
    SwEmbeddedDb db;
    ok(db.open(options), "open JSON database");
    SwDbWriteBatch batch;
    auto input = jsonRow(1);
    const auto original = input;
    const auto originalBytes = bytes(SwJsonDocument(original).toJson().toStdString());
    batch.putJson(bytes("a"), input, index("first"));
    batch.put(bytes("b"), originalBytes, index("first"));
    require(batch.operations()[0].value.isEmpty() && batch.hasJson(),
            "typed admission eagerly materialized JSON bytes");
    std::weak_ptr<const swEmbeddedDbDetail::JsonPayload_> payload = batch.operations()[0].jsonPayload;
    SwDbWriteBatch materialized = batch;
    materialized.materializeJson();
    require(!materialized.hasJson() && materialized.operations()[0].value == originalBytes &&
            materialized.estimatedWalBytes() == batch.estimatedWalBytes(), "typed WAL accounting or bytes differ");
    alterJsonRow(input, 99);
    ok(db.write(batch), "write mixed typed and byte batch");
    require(batch.hasJson(), "const write changed caller batch representation");
    batch.clear();
    require(persistent || !payload.expired(), "memory record did not retain its authoritative payload");

    SwJsonObject result;
    SwMap<SwString, SwList<SwByteArray>> secondary;
    ok(db.getJson(bytes("a"), &result, &secondary), "typed point get");
    require(result == original && secondary == index("first"), "typed admission aliased input or changed indexes");
    require(result["double"].type() == SwJsonValue::Type::Double && std::signbit(result["double"].toDouble()) &&
            result["integer"].toLongLong() == std::numeric_limits<long long>::max(), "typed read changed numeric types");
    alterJsonRow(result, 98);
    ok(db.getJson(bytes("a"), &result), "typed get after caller mutation");
    require(result == original, "typed point read exposed mutable storage");
    ok(db.getJson(bytes("b"), &result), "raw byte record typed get");
    require(result == original, "byte-to-JSON fallback changed object");
    SwByteArray raw;
    ok(db.get(bytes("a"), &raw), "typed record raw get");
    require(raw == originalBytes, "typed-to-byte get changed encoding");

    auto before = db.createSnapshot();
    auto surviving = before.scanIndexJson("group", bytes("first"), bytes("second"));
    require(surviving.isValid() && surviving.current().validJson && surviving.current().value == original,
            "typed index scan failed");
    const auto firstSequence = surviving.current().sequence;
    alterJsonRow(surviving.current().value, 97);
    surviving.rewind();
    require(surviving.current().value == original, "typed iterator exposed mutable storage");
    require(keys(before.scanIndex("group")) == std::vector<std::string>({"a", "b"}), "raw index scan lost typed records");
    require(before.scanPrimary().current().value == originalBytes, "raw primary scan omitted typed value");

    SwDbWriteBatch next;
    next.put(bytes("a"), bytes(SwJsonDocument(jsonRow(2)).toJson().toStdString()), index("second"));
    next.erase(bytes("b"));
    next.putJson(bytes("c"), jsonRow(2), index("second"));
    ok(db.write(std::move(next)), "mixed JSON replacement batch");
    const auto after = db.createSnapshot();
    require(after.visibleSequence() == before.visibleSequence() + 1, "typed batch published multiple sequences");
    ok(before.getJson(bytes("a"), &result), "old typed snapshot get");
    require(result == original, "typed snapshot changed after replacement");
    alterJsonRow(result, 96);
    expectValue(before, "a", originalBytes.toStdString());
    ok(after.getJson(bytes("a"), &result), "typed-to-raw replacement get");
    require(result == jsonRow(2) && after.getJson(bytes("b"), nullptr).code() == SwDbStatus::NotFound,
            "mixed replacement/delete was not coherent");
    require(keys(after.scanIndex("group", bytes("first"), bytes("second"))).empty(),
            "JSON replacement retained obsolete index keys");
    for (auto it = after.scanIndexJson("group"); it.isValid(); it.next()) {
        require(it.current().validJson && it.current().value == jsonRow(2) &&
                it.current().sequence == after.visibleSequence(), "typed index sequence/value differs from batch");
    }

    SwDbWriteBatch corrupt;
    corrupt.put(bytes("bad"), bytes("not JSON"));
    corrupt.putJson(bytes("good"), jsonRow(3));
    ok(db.write(corrupt), "mixed malformed raw and typed records");
    require(db.getJson(bytes("bad"), &result).code() == SwDbStatus::Corruption,
            "malformed raw record became an empty JSON object");
    auto malformed = db.scanPrimaryJson(bytes("bad"));
    require(malformed.isValid() && !malformed.current().validJson && malformed.current().value.isEmpty(),
            "typed scan hid malformed raw record");
    malformed.next();
    require(malformed.isValid() && malformed.current().validJson, "typed scan ended after malformed record");
    malformed = {};
    ok(db.sync(), "sync JSON database");
    db.close();
    before = {};
    require(persistent || !payload.expired(), "surviving iterator lost its JSON payload on close");
    surviving.rewind();
    require(surviving.isValid() && surviving.current().value == original &&
            surviving.current().sequence == firstSequence, "typed iterator invalid after close");
    surviving = {};
    require(payload.expired(), "typed payload was retained after its last owner disappeared");
    ok(db.open(options), "reopen JSON database");
    if (persistent) {
        ok(db.getJson(bytes("c"), &result), "reopen persistent typed record");
        require(result == jsonRow(2), "typed write changed persistent format");
        require(db.metricsSnapshot().blobBytesRead > 0, "typed persistence bypassed blob handling");
    } else {
        require(db.getJson(bytes("c"), &result).code() == SwDbStatus::NotFound,
                "memory typed records survived reopen");
        require(!std::filesystem::exists(path), "typed memory storage created files");
    }
    opaqueJsonRecords(db, persistent);
}
