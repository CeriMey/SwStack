#include "SwEmbeddedDb.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void ok(const SwDbStatus& status, const char* operation) {
    require(status.ok(), std::string(operation) + ": " + status.message().toStdString());
}

SwByteArray bytes(const std::string& value) { return SwByteArray::fromStdString(value); }

SwMap<SwString, SwList<SwByteArray>> index(const std::string& key,
                                         const std::string& name = "group") {
    SwMap<SwString, SwList<SwByteArray>> keys;
    keys[SwString(name)].append(bytes(key));
    return keys;
}

std::vector<std::string> keys(SwDbIterator it) {
    std::vector<std::string> result;
    for (; it.isValid(); it.next()) result.push_back(it.current().primaryKey.toStdString());
    return result;
}

void expectValue(const SwDbSnapshot& snapshot, const char* key, const std::string& expected) {
    SwByteArray value;
    ok(snapshot.get(bytes(key), &value), "snapshot get");
    require(value.toStdString() == expected, "snapshot value mismatch");
}

#include "EmbeddedDbJsonTest.h"

void testMemory(const std::filesystem::path& root) {
    SwEmbeddedDb db;
    SwEmbeddedDbOptions options;
    options.persistent = false;
    options.dbPath = SwString((root / "must-not-exist").string());
    options.memTableBytes = 1;                 // Must never trigger a disk flush.
    options.inlineBlobThresholdBytes = 1;      // Large values remain in memory.
    options.enableShmNotifications = true;     // Must not create IPC objects either.
    options.lazyWrite = true;
    ok(db.open(options), "open memory");
    require(!db.isPersistent(), "memory mode was not selected");

    const std::string large(512 * 1024, 'x');
    auto duplicates = index("alpha");
    duplicates["group"].append(bytes("alpha"));
    SwDbWriteBatch initial;
    initial.put(bytes("a"), bytes(large), duplicates);
    initial.put(bytes("b"), bytes("old-b"), index("beta"));
    initial.put(bytes("c"), bytes("old-c"), index("beta"));
    ok(db.write(std::move(initial)), "initial batch");
    SwDbSnapshot before = db.createSnapshot();
    require(before.isValid(), "memory snapshot is invalid");
    require(db.metricsSnapshot().memoryIndexEntryCount == 3, "duplicate secondary key was retained twice");
    require(keys(before.scanPrimary(bytes("b"), bytes("d"))) == std::vector<std::string>({"b", "c"}),
            "primary range order/bounds");
    require(keys(before.scanIndex("group", bytes("beta"), bytes("gamma"))) ==
                std::vector<std::string>({"b", "c"}), "secondary range order/bounds");
    SwDbIterator survivingIterator = before.scanIndex("group");

    SwDbWriteBatch next;
    next.put(bytes("a"), bytes("new-a"), index("gamma"));
    next.erase(bytes("b"));
    next.erase(bytes("missing"));
    next.put(bytes("d"), bytes("new-d"), index("beta"));
    ok(db.write(next), "replace/delete batch");
    expectValue(before, "a", large);
    expectValue(before, "b", "old-b");
    SwDbSnapshot after = db.createSnapshot();
    expectValue(after, "a", "new-a");
    require(after.get(bytes("b"), nullptr).code() == SwDbStatus::NotFound, "deleted key remains visible");
    require(keys(after.scanIndex("group", bytes("alpha"), bytes("beta"))).empty(), "old index survives update");
    require(keys(after.scanIndex("group", bytes("beta"), bytes("gamma"))) ==
                std::vector<std::string>({"c", "d"}), "index update/delete inconsistent");
    require(after.visibleSequence() == before.visibleSequence() + 1, "batch must publish one sequence");
    for (auto it = after.scanPrimary(); it.isValid(); it.next()) {
        if (it.current().primaryKey != bytes("c")) {
            require(it.current().sequence == after.visibleSequence(), "one batch has different record sequences");
        }
    }

    SwDbWriteBatch invalid;
    invalid.put(bytes("a"), bytes("must-not-appear"));
    invalid.putBlobRef(bytes("external-blob"), 1, 0, 10, 0, 0);
    require(db.write(invalid).code() == SwDbStatus::InvalidArgument, "disk blob accepted in memory database");
    expectValue(db.createSnapshot(), "a", "new-a");
    require(db.createSnapshot().visibleSequence() == after.visibleSequence(), "invalid batch advanced sequence");

    SwDbWriteBatch ordered;
    ordered.put(bytes("temp"), bytes("discard"), index("obsolete"));
    ordered.erase(bytes("temp"));
    ordered.put(bytes("temp"), bytes("final"), index("final"));
    ok(db.write(ordered), "same-key ordered operations");
    expectValue(db.createSnapshot(), "temp", "final");
    require(keys(db.scanIndex("group", bytes("obsolete"), bytes("p"))).empty(), "same-batch index not removed");

    SwEmbeddedDb independent;
    ok(independent.open(options), "second independent memory database");
    require(independent.get(bytes("a"), nullptr).code() == SwDbStatus::NotFound,
            "memory instances unexpectedly share data by path");
    independent.close();
    ok(db.sync(), "memory sync");
    ok(db.refresh(), "memory refresh");
    const SwDbMetrics metrics = db.metricsSnapshot();
    require(metrics.walBytes == 0 && metrics.walFrameCount == 0 && metrics.flushCount == 0 &&
                metrics.compactionCount == 0 && metrics.tableCount == 0 && metrics.pendingWriteCount == 0 &&
                metrics.blobBytesWritten == 0 && metrics.lastDurableSequence == 0,
            "memory database performed persistent work");
    require(!std::filesystem::exists(root), "memory mode created its configured directory");
    db.close();
    expectValue(before, "a", large);
    expectValue(after, "a", "new-a");
    require(keys(survivingIterator) == std::vector<std::string>({"a", "b", "c"}), "iterator lost state after close");
    survivingIterator.rewind();
    require(survivingIterator.size() == 3, "iterator rewind/size after close");
    require(db.sync().code() == SwDbStatus::NotOpen, "closed memory sync succeeded");
    require(!db.createSnapshot().isValid(), "closed database produced snapshot");
    ok(db.open(options), "reopen memory");
    require(db.scanPrimary().size() == 0, "memory reopen retained prior data");
    db.close();
    options.dbPath.clear();
    options.lazyWrite = false;
    ok(db.open(options), "memory without path");
    SwDbWriteBatch immediate;
    immediate.put(bytes("only"), bytes("visible"));
    ok(db.write(immediate), "memory with lazyWrite false");
    db.close();
    options.readOnly = true;
    require(db.open(options).code() == SwDbStatus::InvalidArgument, "read-only memory open accepted");
    require(!std::filesystem::exists(root), "memory close/reopen touched configured path");
}

void testMemoryChurn() {
    SwEmbeddedDb db;
    SwEmbeddedDbOptions options;
    options.persistent = false;
    ok(db.open(options), "open churn database");
    for (int i = 0; i < 30000; ++i) {
        const std::string value = std::to_string(i);
        SwDbWriteBatch batch;
        batch.put(bytes("current"), bytes(value), index(value, "changing-index-" + value));
        batch.put(bytes("temporary-" + value), bytes(value), index(value));
        batch.erase(bytes("temporary-" + value));
        ok(db.write(batch), "churn batch");
        if (i % 100 == 0) {
            auto snap = db.createSnapshot();
            expectValue(snap, "current", value);
            require(snap.scanPrimary().size() == 1, "churn retained deleted primary records");
        }
    }
    auto metrics = db.metricsSnapshot();
    require(metrics.memoryRecordCount == 1 && metrics.memoryIndexEntryCount == 1,
            "churn retained old records or index entries");
    SwDbWriteBatch erase;
    erase.erase(bytes("current"));
    ok(db.write(erase), "erase last live record");
    metrics = db.metricsSnapshot();
    require(metrics.memoryRecordCount == 0 && metrics.memoryIndexEntryCount == 0,
            "empty memory database retained live entries");
    require(db.scanPrimary().size() == 0, "empty database still scans rows");
}

void testConcurrentBatches() {
    SwEmbeddedDb db;
    SwEmbeddedDbOptions options;
    options.persistent = false;
    ok(db.open(options), "open concurrent database");
    SwDbWriteBatch initial;
    initial.put(bytes("a"), bytes("0"));
    initial.put(bytes("b"), bytes("0"));
    ok(db.write(initial), "concurrent initial batch");
    std::atomic<bool> started(false), done(false), consistent(true);
    std::atomic<unsigned> observations(0);
    std::thread reader([&] {
        started.store(true);
        do {
            auto snapshot = db.createSnapshot();
            SwByteArray a, b;
            if (!snapshot.get(bytes("a"), &a).ok() || !snapshot.get(bytes("b"), &b).ok() || a != b) {
                consistent.store(false);
            }
            ++observations;
        } while (!done.load());
    });
    while (!started.load()) std::this_thread::yield();
    for (int i = 1; i <= 4000; ++i) {
        SwDbWriteBatch batch;
        batch.put(bytes("a"), bytes(std::to_string(i)));
        batch.put(bytes("b"), bytes(std::to_string(i)));
        if (!db.write(batch).ok()) consistent.store(false);
    }
    done.store(true);
    reader.join();
    require(consistent.load() && observations.load() > 0, "reader observed a partial batch");
}

void testDiskDefault(const std::filesystem::path& path) {
    SwEmbeddedDbOptions options;
    require(options.persistent, "persistence must remain the default");
    options.dbPath = SwString(path.string());
    options.commitWindowMs = 0;
    options.memTableBytes = 64 * 1024;
    options.readCacheBytes = 1024 * 1024;
    SwEmbeddedDb db;
    ok(db.open(options), "open disk default");
    require(db.isPersistent(), "disk default reported memory mode");
    SwDbWriteBatch batch;
    batch.put(bytes("saved"), bytes("durable"), index("disk"));
    ok(db.write(batch), "disk write");
    ok(db.sync(), "disk sync");
    require(db.metricsSnapshot().lastDurableSequence > 0, "disk sequence is not durable");
    require(std::filesystem::exists(path / "LOCK") && std::filesystem::is_directory(path / "wal"),
            "persistent database has no disk layout");
    db.close();
    ok(db.open(options), "reopen disk");
    expectValue(db.createSnapshot(), "saved", "durable");
    require(keys(db.scanIndex("group")) == std::vector<std::string>({"saved"}), "disk index lost on reopen");
    SwDbWriteBatch erase;
    erase.erase(bytes("saved"));
    ok(db.write(erase), "disk erase");
    db.close();
    ok(db.open(options), "reopen disk after erase");
    require(db.get(bytes("saved"), nullptr).code() == SwDbStatus::NotFound, "disk erase lost on reopen");
    db.close();
}

} // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("sw-embedded-memory-test-" + std::to_string(nonce));
    try {
        testMemory(root / "memory");
        testMemoryChurn();
        testConcurrentBatches();
        testDiskDefault(root / "disk");
        testJsonStorage(root / "json-memory", false);
        testJsonStorage(root / "json-disk", true);
        std::filesystem::remove_all(root);
        std::cout << "EmbeddedDb memory and disk-default tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "EmbeddedDb test failed: " << error.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
