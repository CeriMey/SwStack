#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "SwCoreApplication.h"
#include "SwCrashHandler.h"
#include "SwDir.h"
#include "SwEmbeddedDb.h"
#include "SwFileInfo.h"
#include "SwProcess.h"
#include "SwStandardLocation.h"
#include "platform/SwDbPlatform.h"

namespace {

static void ensureCrashDumpsEnabled() {
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

struct TestResult {
    std::string name;
    bool success;
    std::string detail;
};

static SwByteArray toBytes(const std::string& value) {
    return SwByteArray::fromStdString(value);
}

static bool waitUntil(SwCoreApplication& app, int timeoutMs, const std::function<bool()>& predicate) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    app.processEvent(false);
    return predicate();
}

static int runChildAndWait(SwCoreApplication& app,
                           const SwString& program,
                           const SwStringList& arguments,
                           int timeoutMs,
                           SwString* stdoutOut,
                           SwString* stderrOut) {
    SwProcess process;
    std::atomic<bool> done(false);
    int exitCode = -999;

    SwObject::connect(&process, SIGNAL(readyReadStdOut), std::function<void()>([&]() {
        if (stdoutOut) {
            *stdoutOut += process.read();
        } else {
            (void)process.read();
        }
    }));
    SwObject::connect(&process, SIGNAL(readyReadStdErr), std::function<void()>([&]() {
        if (stderrOut) {
            *stderrOut += process.readStdErr();
        } else {
            (void)process.readStdErr();
        }
    }));
    SwObject::connect(&process, SIGNAL(processTerminated), std::function<void(int)>([&](int code) {
        exitCode = code;
        done.store(true, std::memory_order_release);
    }));

    if (!process.start(program, arguments, ProcessFlags::CreateNoWindow)) {
        return -1000;
    }

    const bool finished = waitUntil(app, timeoutMs, [&]() {
        return done.load(std::memory_order_acquire);
    });
    if (!finished) {
        process.kill();
        (void)waitUntil(app, 2000, [&]() {
            return done.load(std::memory_order_acquire);
        });
    }
    if (process.isOpen()) {
        process.close();
    }
    return exitCode;
}

static SwMap<SwString, SwList<SwByteArray>> makeSecondaryKeys(const SwString& indexName,
                                                              const SwByteArray& indexKey) {
    SwMap<SwString, SwList<SwByteArray>> secondaryKeys;
    SwList<SwByteArray> values;
    values.append(indexKey);
    secondaryKeys[indexName] = values;
    return secondaryKeys;
}

static SwMap<SwString, SwList<SwByteArray>> makeSecondaryKeys(const SwString& firstName,
                                                              const SwByteArray& firstKey,
                                                              const SwString& secondName,
                                                              const SwByteArray& secondKey) {
    SwMap<SwString, SwList<SwByteArray>> secondaryKeys = makeSecondaryKeys(firstName, firstKey);
    SwList<SwByteArray> values;
    values.append(secondKey);
    secondaryKeys[secondName] = values;
    return secondaryKeys;
}

} // namespace

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);
    ensureCrashDumpsEnabled();
    SwCrashHandler::install("EmbeddedDbSelfTest");

    if (argc >= 2 && std::string(argv[1]) == "--print-last-crash") {
        SwCrashReport report;
        if (!SwCrashHandler::takeLastCrashReport(report)) {
            std::cout << "[CRASH] none" << std::endl;
            return 0;
        }
        std::cout << "[CRASH] dir=" << report.crashDir.toStdString() << std::endl;
        std::cout << "[CRASH] log=" << report.logPath.toStdString() << std::endl;
        std::cout << "[CRASH] dump=" << report.dumpPath.toStdString() << std::endl;
        return 0;
    }

    if (argc >= 3 && std::string(argv[1]) == "--probe-writer") {
        SwEmbeddedDb db;
        SwEmbeddedDbOptions options;
        options.dbPath = SwString(argv[2]);
        const SwDbStatus status = db.open(options);
        if (status.ok()) {
            db.close();
            return 0;
        }
        return static_cast<int>(status.code());
    }

    std::vector<TestResult> results;
    const auto addResult = [&](const std::string& name, bool success, const std::string& detail = std::string()) {
        results.push_back({name, success, detail});
        std::cout << "[TEST] " << name << " -> " << (success ? "PASS" : "FAIL");
        if (!detail.empty()) {
            std::cout << " (" << detail << ")";
        }
        std::cout << std::endl;
    };

    const SwString baseTemp = SwStandardLocation::standardLocation(SwStandardLocationId::Temp);
    const unsigned long long token = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const SwString dbPath =
        swDbPlatform::joinPath(baseTemp, SwString("SwEmbeddedDbSelfTest-") + SwString::number(token));
    (void)SwDir::removeRecursively(dbPath);
    (void)SwDir::mkpathAbsolute(dbPath);

    SwEmbeddedDb writer;
    SwEmbeddedDb reader;

    SwEmbeddedDbOptions writerOptions;
    writerOptions.dbPath = dbPath;
    writerOptions.commitWindowMs = 0;
    writerOptions.memTableBytes = 256;
    writerOptions.inlineBlobThresholdBytes = 32;
    writerOptions.maxBackgroundJobs = 2;
    writerOptions.enableShmNotifications = true;

    const SwDbStatus openWriterStatus = writer.open(writerOptions);
    addResult("Writer opens database", openWriterStatus.ok(), openWriterStatus.message().toStdString());
    if (!openWriterStatus.ok()) {
        return 1;
    }

    SwDbWriteBatch batch;
    batch.put(toBytes("alpha"),
              toBytes("value-alpha"),
              makeSecondaryKeys("by_group", toBytes("g1"), "by_tag", toBytes("t1")));
    const SwDbStatus writeAlphaStatus = writer.write(batch);
    addResult("Small primary write succeeds", writeAlphaStatus.ok(), writeAlphaStatus.message().toStdString());

    SwByteArray alphaValue;
    SwMap<SwString, SwList<SwByteArray>> alphaIndexes;
    const SwDbStatus getAlphaStatus = writer.get(toBytes("alpha"), &alphaValue, &alphaIndexes);
    addResult("Writer reads back small value",
              getAlphaStatus.ok() && alphaValue == toBytes("value-alpha"),
              getAlphaStatus.message().toStdString());

    const SwDbIterator alphaIndexIt = writer.scanIndex("by_group", toBytes("g1"), toBytes("g2"));
    addResult("Secondary index scan returns alpha",
              alphaIndexIt.size() == 1 && alphaIndexIt.isValid() && alphaIndexIt.current().primaryKey == toBytes("alpha"));

    const SwByteArray largeValue(std::string(512, 'B'));
    batch.clear();
    batch.put(toBytes("blob-key"),
              largeValue,
              makeSecondaryKeys("by_group", toBytes("g2")));
    const SwDbStatus writeBlobStatus = writer.write(batch);
    addResult("Large write succeeds", writeBlobStatus.ok(), writeBlobStatus.message().toStdString());

    const bool flushFinished = waitUntil(app, 5000, [&]() {
        const SwDbMetrics metrics = writer.metricsSnapshot();
        return metrics.flushCount >= 1 && metrics.blobBytesWritten >= static_cast<unsigned long long>(largeValue.size());
    });
    addResult("Large write flushes and externalizes blob", flushFinished);

    SwByteArray blobValue;
    const SwDbStatus getBlobStatus = writer.get(toBytes("blob-key"), &blobValue, nullptr);
    addResult("Writer resolves blob-backed value",
              getBlobStatus.ok() && blobValue == largeValue,
              getBlobStatus.message().toStdString());

    const SwList<SwString> blobFiles =
        swDbPlatform::listFiles(swDbPlatform::joinPath(dbPath, "blobs"), "BLOB-", ".dat");
    addResult("Blob file exists on disk", !blobFiles.isEmpty());

    SwEmbeddedDbOptions readerOptions = writerOptions;
    readerOptions.readOnly = true;
    readerOptions.enableShmNotifications = true;
    const SwDbStatus openReaderStatus = reader.open(readerOptions);
    addResult("Read-only reader opens database", openReaderStatus.ok(), openReaderStatus.message().toStdString());

    SwByteArray readerBlobValue;
    const SwDbStatus readerBlobStatus = reader.get(toBytes("blob-key"), &readerBlobValue, nullptr);
    addResult("Read-only reader sees flushed data",
              readerBlobStatus.ok() && readerBlobValue == largeValue,
              readerBlobStatus.message().toStdString());

    const SwString selfPath = SwString(SwFileInfo(argv[0]).absoluteFilePath());
    SwString childStdOut;
    SwString childStdErr;
    SwStringList childArgs;
    childArgs.append("--probe-writer");
    childArgs.append(dbPath);
    const int childExitCode = runChildAndWait(app, selfPath, childArgs, 5000, &childStdOut, &childStdErr);
    addResult("Second writer process is rejected",
              childExitCode == static_cast<int>(SwDbStatus::Busy),
              childStdErr.toStdString());

    batch.clear();
    batch.put(toBytes("tail-key"),
              toBytes("tail-value"),
              makeSecondaryKeys("by_group", toBytes("g3")));
    const SwDbStatus writeTailStatus = writer.write(batch);
    addResult("Tail WAL write succeeds", writeTailStatus.ok(), writeTailStatus.message().toStdString());

    SwByteArray readerTailValue;
    const bool autoRefreshWorked = waitUntil(app, 5000, [&]() {
        readerTailValue.clear();
        return reader.get(toBytes("tail-key"), &readerTailValue, nullptr).ok() &&
               readerTailValue == toBytes("tail-value");
    });
    addResult("Reader auto-refreshes from SHM notification", autoRefreshWorked);

    const SwDbIterator tailIndexIt = reader.scanIndex("by_group", toBytes("g3"), toBytes("g4"));
    addResult("Reader scanIndex sees SHM-refreshed WAL entry",
              tailIndexIt.size() == 1 && tailIndexIt.isValid() && tailIndexIt.current().primaryKey == toBytes("tail-key"));

    const SwList<SwString> blobFilesBeforeGc =
        swDbPlatform::listFiles(swDbPlatform::joinPath(dbPath, "blobs"), "BLOB-", ".dat");

    const SwByteArray gcDeadValue(std::string(768, 'D'));
    const SwByteArray gcLiveValue(std::string(768, 'L'));
    batch.clear();
    batch.put(toBytes("gc-dead"),
              gcDeadValue,
              makeSecondaryKeys("by_group", toBytes("g4")));
    batch.put(toBytes("gc-live"),
              gcLiveValue,
              makeSecondaryKeys("by_group", toBytes("g5")));
    const SwDbStatus writeGcSeedStatus = writer.write(batch);
    addResult("GC seed write succeeds", writeGcSeedStatus.ok(), writeGcSeedStatus.message().toStdString());

    const bool gcSeedFlushed = waitUntil(app, 5000, [&]() {
        return swDbPlatform::listFiles(swDbPlatform::joinPath(dbPath, "blobs"), "BLOB-", ".dat").size() >=
               blobFilesBeforeGc.size() + 1;
    });
    addResult("GC seed creates additional blob storage", gcSeedFlushed);

    batch.clear();
    batch.erase(toBytes("gc-dead"));
    const SwDbStatus gcDeleteStatus = writer.write(batch);
    addResult("GC delete write succeeds", gcDeleteStatus.ok(), gcDeleteStatus.message().toStdString());

    const bool blobGcCompleted = waitUntil(app, 8000, [&]() {
        const SwList<SwString> blobFilesAfterGc =
            swDbPlatform::listFiles(swDbPlatform::joinPath(dbPath, "blobs"), "BLOB-", ".dat");
        if (blobFilesAfterGc.size() != blobFilesBeforeGc.size() + 1) {
            return false;
        }

        SwByteArray liveValue;
        const bool liveOk = writer.get(toBytes("gc-live"), &liveValue, nullptr).ok() && liveValue == gcLiveValue;
        SwByteArray deadValue;
        const bool deadMissing = writer.get(toBytes("gc-dead"), &deadValue, nullptr).code() == SwDbStatus::NotFound;
        return liveOk && deadMissing;
    });
    addResult("Blob GC rewrites live blobs and drops dead ones", blobGcCompleted);

    reader.close();
    writer.close();
    (void)SwDir::removeRecursively(dbPath);

    const SwString lazyDbPath =
        swDbPlatform::joinPath(baseTemp, SwString("SwEmbeddedDbLazySelfTest-") + SwString::number(token + 1ull));
    (void)SwDir::removeRecursively(lazyDbPath);
    (void)SwDir::mkpathAbsolute(lazyDbPath);

    SwEmbeddedDb lazyWriter;
    SwEmbeddedDb lazyReader;

    SwEmbeddedDbOptions lazyWriterOptions;
    lazyWriterOptions.dbPath = lazyDbPath;
    lazyWriterOptions.lazyWrite = true;
    lazyWriterOptions.commitWindowMs = 1000;
    lazyWriterOptions.memTableBytes = 4096;
    lazyWriterOptions.inlineBlobThresholdBytes = 64;
    lazyWriterOptions.maxBackgroundJobs = 1;
    lazyWriterOptions.enableShmNotifications = true;

    const SwDbStatus openLazyWriterStatus = lazyWriter.open(lazyWriterOptions);
    addResult("Lazy writer opens database", openLazyWriterStatus.ok(), openLazyWriterStatus.message().toStdString());

    SwEmbeddedDbOptions lazyReaderOptions = lazyWriterOptions;
    lazyReaderOptions.readOnly = true;
    const SwDbStatus openLazyReaderStatus = lazyReader.open(lazyReaderOptions);
    addResult("Lazy reader opens database", openLazyReaderStatus.ok(), openLazyReaderStatus.message().toStdString());

    SwDbWriteBatch lazyBatch;
    lazyBatch.put(toBytes("lazy-key"),
                  toBytes("lazy-value"),
                  makeSecondaryKeys("by_group", toBytes("lazy-group")));
    const SwDbStatus lazyWriteStatus = lazyWriter.write(lazyBatch);
    addResult("Lazy write succeeds", lazyWriteStatus.ok(), lazyWriteStatus.message().toStdString());

    const SwDbMetrics lazyMetricsBeforeSync = lazyWriter.metricsSnapshot();
    addResult("Lazy write is visible before durable sync",
              lazyMetricsBeforeSync.lastVisibleSequence == 1 &&
                  lazyMetricsBeforeSync.lastDurableSequence == 0);

    SwByteArray lazyWriterValue;
    const SwDbStatus lazyWriterGetStatus = lazyWriter.get(toBytes("lazy-key"), &lazyWriterValue, nullptr);
    addResult("Lazy writer reads in-memory value immediately",
              lazyWriterGetStatus.ok() && lazyWriterValue == toBytes("lazy-value"),
              lazyWriterGetStatus.message().toStdString());

    SwByteArray lazyReaderValue;
    const SwDbStatus lazyReaderBeforeSyncStatus = lazyReader.refresh();
    const SwDbStatus lazyReaderGetBeforeSync = lazyReader.get(toBytes("lazy-key"), &lazyReaderValue, nullptr);
    addResult("Read-only reader does not see lazy value before sync",
              lazyReaderBeforeSyncStatus.ok() && lazyReaderGetBeforeSync.code() == SwDbStatus::NotFound,
              lazyReaderGetBeforeSync.message().toStdString());

    const SwDbStatus lazySyncStatus = lazyWriter.sync();
    addResult("Lazy writer sync succeeds", lazySyncStatus.ok(), lazySyncStatus.message().toStdString());

    const bool lazyDurableReached = waitUntil(app, 3000, [&]() {
        const SwDbMetrics metrics = lazyWriter.metricsSnapshot();
        return metrics.lastVisibleSequence == metrics.lastDurableSequence && metrics.lastDurableSequence == 1;
    });
    addResult("Lazy writer reaches durable sequence after sync", lazyDurableReached);

    lazyReaderValue.clear();
    const SwDbStatus lazyReaderAfterSyncRefresh = lazyReader.refresh();
    const SwDbStatus lazyReaderGetAfterSync = lazyReader.get(toBytes("lazy-key"), &lazyReaderValue, nullptr);
    addResult("Read-only reader sees lazy value after sync",
              lazyReaderAfterSyncRefresh.ok() &&
                  lazyReaderGetAfterSync.ok() &&
                  lazyReaderValue == toBytes("lazy-value"),
              lazyReaderGetAfterSync.message().toStdString());

    lazyReader.close();
    lazyWriter.close();
    (void)SwDir::removeRecursively(lazyDbPath);

    std::cout << "\n===== SwEmbeddedDb Self-Test Summary =====\n";
    bool allPassed = true;
    for (std::size_t i = 0; i < results.size(); ++i) {
        allPassed = allPassed && results[i].success;
        std::cout << (results[i].success ? "[PASS] " : "[FAIL] ") << results[i].name;
        if (!results[i].detail.empty()) {
            std::cout << " -> " << results[i].detail;
        }
        std::cout << std::endl;
    }

    return allPassed ? 0 : 1;
}
