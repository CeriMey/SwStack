#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "SwDir.h"
#include "SwEmbeddedDb.h"
#include "SwStandardLocation.h"
#include "platform/SwDbPlatform.h"

namespace {

static const SwString kGroupIndexName("by_group");

struct BenchOptions {
    SwString dbPath;
    bool lazyWrite{false};
    unsigned long long records{20000};
    unsigned long long batchSize{100};
    unsigned long long valueBytes{512};
    unsigned long long blobBytes{128ull * 1024ull};
    unsigned long long blobEvery{50};
    unsigned long long readSamples{5000};
    unsigned long long scanLimit{0};
    unsigned long long indexCardinality{256};
    unsigned long long commitWindowMs{0};
    unsigned long long memTableBytes{64ull * 1024ull * 1024ull};
    unsigned long long inlineBlobThresholdBytes{32ull * 1024ull};
    int maxBackgroundJobs{2};
    bool assertTargets{false};
    bool keepDb{false};
};

struct PhaseResult {
    SwString name;
    unsigned long long operations{0};
    unsigned long long rows{0};
    unsigned long long bytes{0};
    double totalMs{0.0};
    bool skipped{false};
    SwString note;
    std::vector<double> latencyMicros;
};

struct PhaseSummary {
    double averageMicros{0.0};
    double p50Micros{0.0};
    double p95Micros{0.0};
    double p99Micros{0.0};
    double maxMicros{0.0};
};

struct TargetProfile {
    double maxWriterOpenMs{1000.0};
    double maxReaderOpenMs{1000.0};
    double maxWriteP50Us{500.0};
    double maxWriteP95Us{2000.0};
    double maxWriteP99Us{5000.0};
    double maxGetP50Us{100.0};
    double maxGetP95Us{500.0};
    double maxGetP99Us{2000.0};
    double maxBlobGetP50Us{1000.0};
    double maxBlobGetP95Us{3000.0};
    double maxBlobGetP99Us{5000.0};
    double maxPageScan100Ms{2.0};
    double maxPageScan1000Ms{10.0};
    double maxWriterCloseMs{500.0};
};

static TargetProfile defaultTargetProfile() {
    return TargetProfile();
}

static double elapsedMs(const std::chrono::steady_clock::duration& duration) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli> >(duration).count();
}

static double elapsedMicros(const std::chrono::steady_clock::duration& duration) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::micro> >(duration).count();
}

static bool parseUnsignedLongLong(const char* text, unsigned long long& valueOut) {
    if (!text || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    valueOut = value;
    return true;
}

static bool parseInt(const char* text, int& valueOut) {
    unsigned long long parsed = 0;
    if (!parseUnsignedLongLong(text, parsed) ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    valueOut = static_cast<int>(parsed);
    return true;
}

static SwString humanBytes(unsigned long long bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unitIndex = 0;
    while (value >= 1024.0 && unitIndex + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unitIndex;
    }
    return SwString::number(value, 'f', value < 10.0 ? 2 : 1) + " " + units[unitIndex];
}

static PhaseSummary summarizeLatencies(const std::vector<double>& latencyMicros) {
    PhaseSummary summary;
    if (latencyMicros.empty()) {
        return summary;
    }

    std::vector<double> ordered = latencyMicros;
    std::sort(ordered.begin(), ordered.end());

    double total = 0.0;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        total += ordered[i];
    }
    summary.averageMicros = total / static_cast<double>(ordered.size());
    summary.maxMicros = ordered.back();

    const auto percentile = [&](double ratio) -> double {
        if (ordered.size() == 1) {
            return ordered.front();
        }
        const double index = ratio * static_cast<double>(ordered.size() - 1);
        const std::size_t lower = static_cast<std::size_t>(index);
        const std::size_t upper = std::min(lower + 1, ordered.size() - 1);
        const double fraction = index - static_cast<double>(lower);
        return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction;
    };

    summary.p50Micros = percentile(0.50);
    summary.p95Micros = percentile(0.95);
    summary.p99Micros = percentile(0.99);
    return summary;
}

static unsigned long long splitMix64(unsigned long long value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

static void printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  --db-path <path>\n"
        << "  --records <count>\n"
        << "  --batch-size <count>\n"
        << "  --value-bytes <bytes>\n"
        << "  --blob-bytes <bytes>\n"
        << "  --blob-every <count>        0 disables blob records\n"
        << "  --read-samples <count>\n"
        << "  --scan-limit <count>        0 scans the full visible range\n"
        << "  --index-cardinality <count>\n"
        << "  --commit-window-ms <count>\n"
        << "  --lazy-write\n"
        << "  --memtable-bytes <bytes>\n"
        << "  --inline-blob-threshold-bytes <bytes>\n"
        << "  --max-background-jobs <count>\n"
        << "  --assert-targets            fail if default latency objectives are not met\n"
        << "  --keep-db\n"
        << "  --help\n";
}

static bool parseBenchOptions(int argc, char* argv[], BenchOptions& outOptions, bool& helpRequestedOut) {
    helpRequestedOut = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i] ? argv[i] : "");
        const auto requireValue = [&](const char* optionName) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << optionName << std::endl;
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help") {
            printUsage(argv[0]);
            helpRequestedOut = true;
            return false;
        }
        if (arg == "--db-path") {
            const char* value = requireValue("--db-path");
            if (!value) {
                return false;
            }
            outOptions.dbPath = SwString(value);
            continue;
        }
        if (arg == "--lazy-write") {
            outOptions.lazyWrite = true;
            continue;
        }

        unsigned long long parsedU64 = 0;
        int parsedInt = 0;
        if (arg == "--records") {
            const char* value = requireValue("--records");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --records" << std::endl;
                return false;
            }
            outOptions.records = parsedU64;
        } else if (arg == "--batch-size") {
            const char* value = requireValue("--batch-size");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --batch-size" << std::endl;
                return false;
            }
            outOptions.batchSize = parsedU64;
        } else if (arg == "--value-bytes") {
            const char* value = requireValue("--value-bytes");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --value-bytes" << std::endl;
                return false;
            }
            outOptions.valueBytes = parsedU64;
        } else if (arg == "--blob-bytes") {
            const char* value = requireValue("--blob-bytes");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --blob-bytes" << std::endl;
                return false;
            }
            outOptions.blobBytes = parsedU64;
        } else if (arg == "--blob-every") {
            const char* value = requireValue("--blob-every");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --blob-every" << std::endl;
                return false;
            }
            outOptions.blobEvery = parsedU64;
        } else if (arg == "--read-samples") {
            const char* value = requireValue("--read-samples");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --read-samples" << std::endl;
                return false;
            }
            outOptions.readSamples = parsedU64;
        } else if (arg == "--scan-limit") {
            const char* value = requireValue("--scan-limit");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --scan-limit" << std::endl;
                return false;
            }
            outOptions.scanLimit = parsedU64;
        } else if (arg == "--index-cardinality") {
            const char* value = requireValue("--index-cardinality");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --index-cardinality" << std::endl;
                return false;
            }
            outOptions.indexCardinality = parsedU64;
        } else if (arg == "--commit-window-ms") {
            const char* value = requireValue("--commit-window-ms");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --commit-window-ms" << std::endl;
                return false;
            }
            outOptions.commitWindowMs = parsedU64;
        } else if (arg == "--memtable-bytes") {
            const char* value = requireValue("--memtable-bytes");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --memtable-bytes" << std::endl;
                return false;
            }
            outOptions.memTableBytes = parsedU64;
        } else if (arg == "--inline-blob-threshold-bytes") {
            const char* value = requireValue("--inline-blob-threshold-bytes");
            if (!value || !parseUnsignedLongLong(value, parsedU64)) {
                std::cerr << "Invalid --inline-blob-threshold-bytes" << std::endl;
                return false;
            }
            outOptions.inlineBlobThresholdBytes = parsedU64;
        } else if (arg == "--max-background-jobs") {
            const char* value = requireValue("--max-background-jobs");
            if (!value || !parseInt(value, parsedInt)) {
                std::cerr << "Invalid --max-background-jobs" << std::endl;
                return false;
            }
            outOptions.maxBackgroundJobs = parsedInt;
        } else if (arg == "--assert-targets") {
            outOptions.assertTargets = true;
        } else if (arg == "--keep-db") {
            outOptions.keepDb = true;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return false;
        }
    }

    if (outOptions.records == 0) {
        std::cerr << "--records must be > 0" << std::endl;
        return false;
    }
    if (outOptions.batchSize == 0) {
        std::cerr << "--batch-size must be > 0" << std::endl;
        return false;
    }
    if (outOptions.indexCardinality == 0) {
        std::cerr << "--index-cardinality must be > 0" << std::endl;
        return false;
    }
    if (outOptions.maxBackgroundJobs <= 0) {
        std::cerr << "--max-background-jobs must be > 0" << std::endl;
        return false;
    }

    return true;
}

class EmbeddedDbBenchRunner {
public:
    explicit EmbeddedDbBenchRunner(const BenchOptions& options)
        : options_(options) {
    }

    int run() {
        if (!prepareDbPath_()) {
            return 1;
        }

        printConfiguration_();

        SwEmbeddedDb writer;
        SwEmbeddedDbOptions writerOptions = makeDbOptions_(false);
        PhaseResult writerOpenPhase;
        writerOpenPhase.name = "writer-open";
        writerOpenPhase.operations = 1;
        const std::chrono::steady_clock::time_point writerOpenStart = std::chrono::steady_clock::now();
        const SwDbStatus openWriterStatus = writer.open(writerOptions);
        writerOpenPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - writerOpenStart);
        writerOpenPhase.latencyMicros.push_back(writerOpenPhase.totalMs * 1000.0);
        printPhase_(writerOpenPhase);
        if (!openWriterStatus.ok()) {
            std::cerr << "Writer open failed: " << openWriterStatus.message().toStdString() << std::endl;
            cleanup_();
            return 1;
        }

        PhaseResult writePhase;
        if (!benchmarkWrites_(writer, writePhase)) {
            writer.close();
            cleanup_();
            return 1;
        }
        printPhase_(writePhase);

        const SwDbMetrics writerMetrics = writer.metricsSnapshot();
        printMetrics_("writer", writerMetrics);

        PhaseResult closePhase;
        closePhase.name = "writer-close";
        closePhase.operations = 1;
        const std::chrono::steady_clock::time_point closeStart = std::chrono::steady_clock::now();
        writer.close();
        closePhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - closeStart);
        closePhase.latencyMicros.push_back(closePhase.totalMs * 1000.0);
        printPhase_(closePhase);

        SwEmbeddedDb reader;
        SwEmbeddedDbOptions readerOptions = makeDbOptions_(true);
        PhaseResult readerOpenPhase;
        readerOpenPhase.name = "reader-open";
        readerOpenPhase.operations = 1;
        const std::chrono::steady_clock::time_point readerOpenStart = std::chrono::steady_clock::now();
        const SwDbStatus openReaderStatus = reader.open(readerOptions);
        readerOpenPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - readerOpenStart);
        readerOpenPhase.latencyMicros.push_back(readerOpenPhase.totalMs * 1000.0);
        printPhase_(readerOpenPhase);
        if (!openReaderStatus.ok()) {
            std::cerr << "Reader open failed: " << openReaderStatus.message().toStdString() << std::endl;
            cleanup_();
            return 1;
        }

        PhaseResult getPhase;
        if (!benchmarkGets_(reader, false, getPhase)) {
            reader.close();
            cleanup_();
            return 1;
        }
        printPhase_(getPhase);

        PhaseResult blobGetPhase;
        if (!benchmarkGets_(reader, true, blobGetPhase)) {
            reader.close();
            cleanup_();
            return 1;
        }
        printPhase_(blobGetPhase);

        PhaseResult primaryPage100Phase;
        if (!benchmarkPrimaryPageScan_(reader, 100, primaryPage100Phase)) {
            reader.close();
            cleanup_();
            return 1;
        }
        printPhase_(primaryPage100Phase);

        PhaseResult primaryPage1000Phase;
        if (!benchmarkPrimaryPageScan_(reader, 1000, primaryPage1000Phase)) {
            reader.close();
            cleanup_();
            return 1;
        }
        printPhase_(primaryPage1000Phase);

        PhaseResult primaryScanPhase;
        if (!benchmarkPrimaryScan_(reader, primaryScanPhase)) {
            reader.close();
            cleanup_();
            return 1;
        }
        printPhase_(primaryScanPhase);

        PhaseResult indexScanPhase;
        if (!benchmarkIndexScan_(reader, indexScanPhase)) {
            reader.close();
            cleanup_();
            return 1;
        }
        printPhase_(indexScanPhase);

        const SwDbMetrics readerMetrics = reader.metricsSnapshot();
        printMetrics_("reader", readerMetrics);
        reader.close();

        if (options_.assertTargets) {
            const bool targetsOk = evaluateTargets_(writerOpenPhase,
                                                    writePhase,
                                                    closePhase,
                                                    readerOpenPhase,
                                                    getPhase,
                                                    blobGetPhase,
                                                    primaryPage100Phase,
                                                    primaryPage1000Phase);
            cleanup_();
            return targetsOk ? 0 : 2;
        }

        cleanup_();
        return 0;
    }

private:
    bool prepareDbPath_() {
        bool shouldResetDirectory = false;
        if (!options_.dbPath.isEmpty()) {
            dbPath_ = swDbPlatform::normalizePath(options_.dbPath);
            autoCleanup_ = false;
            if (swDbPlatform::directoryExists(dbPath_) || swDbPlatform::fileExists(dbPath_)) {
                std::cerr << "Refusing to reuse explicit --db-path because the path already exists: "
                          << dbPath_.toStdString() << std::endl;
                return false;
            }
        } else {
            const SwString baseTemp = SwStandardLocation::standardLocation(SwStandardLocationId::Temp);
            const unsigned long long token = static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            dbPath_ = swDbPlatform::joinPath(baseTemp,
                                            SwString("SwEmbeddedDbBench-") + SwString::number(token));
            autoCleanup_ = !options_.keepDb;
            shouldResetDirectory = true;
        }

        if (shouldResetDirectory) {
            (void)SwDir::removeRecursively(dbPath_);
        }
        if (!SwDir::mkpathAbsolute(dbPath_)) {
            std::cerr << "Failed to create benchmark directory: " << dbPath_.toStdString() << std::endl;
            return false;
        }
        return true;
    }

    void cleanup_() {
        if (autoCleanup_) {
            (void)SwDir::removeRecursively(dbPath_);
        }
    }

    SwEmbeddedDbOptions makeDbOptions_(bool readOnly) const {
        SwEmbeddedDbOptions dbOptions;
        dbOptions.dbPath = dbPath_;
        dbOptions.readOnly = readOnly;
        dbOptions.lazyWrite = options_.lazyWrite;
        dbOptions.commitWindowMs = options_.commitWindowMs;
        dbOptions.memTableBytes = options_.memTableBytes;
        dbOptions.inlineBlobThresholdBytes = options_.inlineBlobThresholdBytes;
        dbOptions.maxBackgroundJobs = options_.maxBackgroundJobs;
        dbOptions.enableShmNotifications = false;
        return dbOptions;
    }

    void printConfiguration_() const {
        const TargetProfile targets = defaultTargetProfile();
        std::cout << "[CONFIG] db-path=" << dbPath_.toStdString() << std::endl;
        std::cout << "[CONFIG] records=" << options_.records
                  << " batch-size=" << options_.batchSize
                  << " value-bytes=" << options_.valueBytes
                  << " blob-bytes=" << options_.blobBytes
                  << " blob-every=" << options_.blobEvery
                  << " read-samples=" << options_.readSamples
                  << " scan-limit=" << options_.scanLimit
                  << " index-cardinality=" << options_.indexCardinality << std::endl;
        std::cout << "[CONFIG] commit-window-ms=" << options_.commitWindowMs
                  << " lazy-write=" << (options_.lazyWrite ? "true" : "false")
                  << " memtable-bytes=" << options_.memTableBytes
                  << " inline-blob-threshold-bytes=" << options_.inlineBlobThresholdBytes
                  << " max-background-jobs=" << options_.maxBackgroundJobs
                  << " assert-targets=" << (options_.assertTargets ? "true" : "false")
                  << " keep-db=" << (options_.keepDb ? "true" : "false") << std::endl;
        if (options_.blobEvery > 0 && options_.blobBytes <= options_.inlineBlobThresholdBytes) {
            std::cout << "[NOTE] blob records stay inline because blob-bytes <= inline-blob-threshold-bytes"
                      << std::endl;
        }
        if (options_.assertTargets) {
            std::cout << "[TARGETS] writer-open-ms<=" << targets.maxWriterOpenMs
                      << " reader-open-ms<=" << targets.maxReaderOpenMs
                      << " write-p50-us<=" << targets.maxWriteP50Us
                      << " write-p95-us<=" << targets.maxWriteP95Us
                      << " write-p99-us<=" << targets.maxWriteP99Us
                      << " get-p50-us<=" << targets.maxGetP50Us
                      << " get-p95-us<=" << targets.maxGetP95Us
                      << " get-p99-us<=" << targets.maxGetP99Us
                      << " blob-get-p50-us<=" << targets.maxBlobGetP50Us
                      << " blob-get-p95-us<=" << targets.maxBlobGetP95Us
                      << " blob-get-p99-us<=" << targets.maxBlobGetP99Us
                      << " scan100-ms<=" << targets.maxPageScan100Ms
                      << " scan1000-ms<=" << targets.maxPageScan1000Ms
                      << " writer-close-ms<=" << targets.maxWriterCloseMs
                      << std::endl;
        }
    }

    static void printPhase_(const PhaseResult& phase) {
        std::cout << "[PHASE] " << phase.name.toStdString();
        if (phase.skipped) {
            std::cout << " skipped";
            if (!phase.note.isEmpty()) {
                std::cout << " (" << phase.note.toStdString() << ")";
            }
            std::cout << std::endl;
            return;
        }

        const PhaseSummary summary = summarizeLatencies(phase.latencyMicros);
        const double totalSeconds = phase.totalMs / 1000.0;
        const double opsPerSecond = totalSeconds > 0.0 ? static_cast<double>(phase.operations) / totalSeconds : 0.0;
        const double mbPerSecond = totalSeconds > 0.0
            ? (static_cast<double>(phase.bytes) / (1024.0 * 1024.0)) / totalSeconds
            : 0.0;

        std::cout << std::fixed << std::setprecision(2)
                  << " total-ms=" << phase.totalMs
                  << " ops=" << phase.operations
                  << " rows=" << phase.rows
                  << " bytes=" << phase.bytes
                  << " throughput-ops/s=" << opsPerSecond
                  << " throughput-MiB/s=" << mbPerSecond;
        if (!phase.latencyMicros.empty()) {
            std::cout << " avg-us=" << summary.averageMicros
                      << " p50-us=" << summary.p50Micros
                      << " p95-us=" << summary.p95Micros
                      << " p99-us=" << summary.p99Micros
                      << " max-us=" << summary.maxMicros;
        }
        if (!phase.note.isEmpty()) {
            std::cout << " note=\"" << phase.note.toStdString() << "\"";
        }
        std::cout << std::endl;
    }

    static void printMetrics_(const char* label, const SwDbMetrics& metrics) {
        std::cout << "[METRICS] " << label
                  << " last-visible-seq=" << metrics.lastVisibleSequence
                  << " last-durable-seq=" << metrics.lastDurableSequence
                  << " write-batches=" << metrics.writeBatchCount
                  << " wal-frames=" << metrics.walFrameCount
                  << " wal-bytes=" << metrics.walBytes
                  << " wal-encode-us=" << metrics.walEncodeMicros
                  << " wal-append-us=" << metrics.walAppendMicros
                  << " wal-sync-us=" << metrics.walSyncMicros
                  << " apply-us=" << metrics.applyBatchMicros
                  << " read-model-build-us=" << metrics.readModelBuildMicros
                  << " read-model-merge-us=" << metrics.readModelMergeMicros
                  << " read-model-sort-us=" << metrics.readModelSortMicros
                  << " read-model-primary-rows=" << metrics.readModelPrimaryRowCount
                  << " read-model-index-rows=" << metrics.readModelIndexRowCount
                  << " flushes=" << metrics.flushCount
                  << " compactions=" << metrics.compactionCount
                  << " gets=" << metrics.getCount
                  << " snapshots=" << metrics.snapshotCount
                  << " blob-written=" << metrics.blobBytesWritten
                  << " blob-read=" << metrics.blobBytesRead
                  << " tables=" << metrics.tableCount
                  << " pending-writes=" << metrics.pendingWriteCount
                  << std::endl;
    }

    static bool checkTotalMsTarget_(const char* label,
                                    const PhaseResult& phase,
                                    double maxTotalMs,
                                    bool optional,
                                    bool& overallSuccess) {
        if (phase.skipped) {
            if (!optional) {
                std::cout << "[TARGET] FAIL " << label << " skipped unexpectedly" << std::endl;
                overallSuccess = false;
            } else {
                std::cout << "[TARGET] SKIP " << label << " " << phase.note.toStdString() << std::endl;
            }
            return false;
        }

        const bool pass = phase.totalMs <= maxTotalMs;
        std::cout << "[TARGET] " << (pass ? "PASS " : "FAIL ") << label
                  << " actual-ms=" << std::fixed << std::setprecision(2) << phase.totalMs
                  << " target-ms<=" << maxTotalMs << std::endl;
        if (!pass) {
            overallSuccess = false;
        }
        return pass;
    }

    static bool checkLatencyTarget_(const char* label,
                                    const PhaseResult& phase,
                                    double maxP50Us,
                                    double maxP95Us,
                                    double maxP99Us,
                                    bool optional,
                                    bool& overallSuccess) {
        if (phase.skipped) {
            if (!optional) {
                std::cout << "[TARGET] FAIL " << label << " skipped unexpectedly" << std::endl;
                overallSuccess = false;
            } else {
                std::cout << "[TARGET] SKIP " << label << " " << phase.note.toStdString() << std::endl;
            }
            return false;
        }

        const PhaseSummary summary = summarizeLatencies(phase.latencyMicros);
        const bool pass = summary.p50Micros <= maxP50Us &&
                          summary.p95Micros <= maxP95Us &&
                          summary.p99Micros <= maxP99Us;
        std::cout << "[TARGET] " << (pass ? "PASS " : "FAIL ") << label
                  << " p50-us=" << std::fixed << std::setprecision(2) << summary.p50Micros
                  << " p95-us=" << summary.p95Micros
                  << " p99-us=" << summary.p99Micros
                  << " targets=(" << maxP50Us << "," << maxP95Us << "," << maxP99Us << ")"
                  << std::endl;
        if (!pass) {
            overallSuccess = false;
        }
        return pass;
    }

    bool evaluateTargets_(const PhaseResult& writerOpenPhase,
                          const PhaseResult& writePhase,
                          const PhaseResult& closePhase,
                          const PhaseResult& readerOpenPhase,
                          const PhaseResult& getPhase,
                          const PhaseResult& blobGetPhase,
                          const PhaseResult& primaryPage100Phase,
                          const PhaseResult& primaryPage1000Phase) const {
        bool success = true;
        const TargetProfile targets = defaultTargetProfile();

        (void)checkTotalMsTarget_("writer-open", writerOpenPhase, targets.maxWriterOpenMs, false, success);
        (void)checkLatencyTarget_("write", writePhase,
                                  targets.maxWriteP50Us,
                                  targets.maxWriteP95Us,
                                  targets.maxWriteP99Us,
                                  false,
                                  success);
        (void)checkTotalMsTarget_("writer-close", closePhase, targets.maxWriterCloseMs, false, success);
        (void)checkTotalMsTarget_("reader-open", readerOpenPhase, targets.maxReaderOpenMs, false, success);
        (void)checkLatencyTarget_("get", getPhase,
                                  targets.maxGetP50Us,
                                  targets.maxGetP95Us,
                                  targets.maxGetP99Us,
                                  false,
                                  success);
        (void)checkLatencyTarget_("get-blob", blobGetPhase,
                                  targets.maxBlobGetP50Us,
                                  targets.maxBlobGetP95Us,
                                  targets.maxBlobGetP99Us,
                                  true,
                                  success);
        (void)checkTotalMsTarget_("scan-primary-page-100",
                                  primaryPage100Phase,
                                  targets.maxPageScan100Ms,
                                  true,
                                  success);
        (void)checkTotalMsTarget_("scan-primary-page-1000",
                                  primaryPage1000Phase,
                                  targets.maxPageScan1000Ms,
                                  true,
                                  success);
        return success;
    }

    SwByteArray makePrimaryKey_(unsigned long long recordId) const {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "row:%012llu", recordId);
        return SwByteArray(buffer);
    }

    SwByteArray makeGroupKey_(unsigned long long groupId) const {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "group:%010llu", groupId);
        return SwByteArray(buffer);
    }

    SwByteArray makePayload_(unsigned long long recordId, unsigned long long bytes) const {
        static const char alphabet[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
        static const std::size_t alphabetSize = sizeof(alphabet) - 1;

        SwByteArray payload(static_cast<std::size_t>(bytes), '\0');
        for (unsigned long long i = 0; i < bytes; ++i) {
            const unsigned long long mixed = splitMix64(recordId ^ (i * 1315423911ull));
            payload[static_cast<std::size_t>(i)] = alphabet[mixed % alphabetSize];
        }
        return payload;
    }

    SwMap<SwString, SwList<SwByteArray> > makeSecondaryKeys_(unsigned long long recordId) const {
        SwMap<SwString, SwList<SwByteArray> > secondaryKeys;
        SwList<SwByteArray> groupValues;
        groupValues.append(makeGroupKey_(recordId % options_.indexCardinality));
        secondaryKeys[kGroupIndexName] = groupValues;
        return secondaryKeys;
    }

    bool isBlobRecord_(unsigned long long recordId) const {
        return options_.blobEvery > 0 && ((recordId + 1ull) % options_.blobEvery == 0ull);
    }

    unsigned long long blobRecordCount_() const {
        if (options_.blobEvery == 0) {
            return 0;
        }
        return options_.records / options_.blobEvery;
    }

    unsigned long long sampleRecordId_(unsigned long long sampleIndex,
                                       unsigned long long population,
                                       unsigned long long salt) const {
        if (population == 0) {
            return 0;
        }
        return splitMix64(sampleIndex ^ salt) % population;
    }

    bool benchmarkWrites_(SwEmbeddedDb& writer, PhaseResult& outPhase) {
        outPhase.name = "write";
        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();

        SwDbWriteBatch batch;
        unsigned long long recordId = 0;
        while (recordId < options_.records) {
            batch.clear();
            unsigned long long batchBytes = 0;
            unsigned long long batchCount = 0;
            for (; recordId < options_.records && batchCount < options_.batchSize; ++recordId, ++batchCount) {
                const bool blobRecord = isBlobRecord_(recordId);
                const unsigned long long valueBytes = blobRecord ? options_.blobBytes : options_.valueBytes;
                batch.put(makePrimaryKey_(recordId),
                          makePayload_(recordId, valueBytes),
                          makeSecondaryKeys_(recordId));
                batchBytes += valueBytes;
            }

            const std::chrono::steady_clock::time_point writeStart = std::chrono::steady_clock::now();
            const SwDbStatus writeStatus = writer.write(std::move(batch));
            const std::chrono::steady_clock::time_point writeEnd = std::chrono::steady_clock::now();
            if (!writeStatus.ok()) {
                std::cerr << "Write failed at record " << recordId << ": "
                          << writeStatus.message().toStdString() << std::endl;
                return false;
            }

            outPhase.latencyMicros.push_back(elapsedMicros(writeEnd - writeStart));
            outPhase.operations += batchCount;
            outPhase.rows += batchCount;
            outPhase.bytes += batchBytes;
        }

        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        outPhase.note = SwString("logical-bytes=") + humanBytes(outPhase.bytes);
        return true;
    }

    bool benchmarkGets_(SwEmbeddedDb& reader, bool blobsOnly, PhaseResult& outPhase) {
        outPhase.name = blobsOnly ? SwString("get-blob") : SwString("get");
        const unsigned long long population = blobsOnly ? blobRecordCount_() : options_.records;
        if (population == 0 || options_.readSamples == 0) {
            outPhase.skipped = true;
            outPhase.note = population == 0 ? SwString("no eligible records") : SwString("read-samples=0");
            return true;
        }

        const unsigned long long sampleCount = std::min(options_.readSamples, population);
        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();
        for (unsigned long long sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            unsigned long long recordId = 0;
            if (blobsOnly) {
                const unsigned long long blobOrdinal = sampleRecordId_(sampleIndex, population, 0x123456789ull);
                recordId = ((blobOrdinal + 1ull) * options_.blobEvery) - 1ull;
            } else {
                recordId = sampleRecordId_(sampleIndex, population, 0xfeedfaceull);
            }

            SwByteArray value;
            SwMap<SwString, SwList<SwByteArray> > secondaryKeys;
            const std::chrono::steady_clock::time_point getStart = std::chrono::steady_clock::now();
            const SwDbStatus getStatus = reader.get(makePrimaryKey_(recordId), &value, &secondaryKeys);
            const std::chrono::steady_clock::time_point getEnd = std::chrono::steady_clock::now();
            if (!getStatus.ok()) {
                std::cerr << "Get failed for record " << recordId << ": "
                          << getStatus.message().toStdString() << std::endl;
                return false;
            }

            const unsigned long long expectedBytes = isBlobRecord_(recordId) ? options_.blobBytes : options_.valueBytes;
            if (value != makePayload_(recordId, expectedBytes)) {
                std::cerr << "Payload mismatch for record " << recordId << std::endl;
                return false;
            }
            const SwByteArray expectedGroupKey = makeGroupKey_(recordId % options_.indexCardinality);
            const auto it = secondaryKeys.find(kGroupIndexName);
            if (it == secondaryKeys.end() || it->second.isEmpty() || it->second[0] != expectedGroupKey) {
                std::cerr << "Secondary index mismatch for record " << recordId << std::endl;
                return false;
            }

            outPhase.latencyMicros.push_back(elapsedMicros(getEnd - getStart));
            outPhase.operations += 1;
            outPhase.rows += 1;
            outPhase.bytes += value.size();
        }

        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        return true;
    }

    bool benchmarkPrimaryScan_(SwEmbeddedDb& reader, PhaseResult& outPhase) {
        outPhase.name = "scan-primary";
        const unsigned long long expectedRows =
            options_.scanLimit == 0 ? options_.records : std::min(options_.scanLimit, options_.records);

        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();
        SwDbIterator iterator = reader.scanPrimary();
        SwByteArray previousKey;
        bool hasPreviousKey = false;
        while (iterator.isValid()) {
            const SwDbEntry& row = iterator.current();
            if (hasPreviousKey && row.primaryKey < previousKey) {
                std::cerr << "Primary scan returned out-of-order keys" << std::endl;
                return false;
            }
            hasPreviousKey = true;
            previousKey = row.primaryKey;

            outPhase.rows += 1;
            outPhase.operations += 1;
            outPhase.bytes += row.value.size();
            if (options_.scanLimit > 0 && outPhase.rows >= options_.scanLimit) {
                break;
            }
            iterator.next();
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);

        if (outPhase.rows != expectedRows) {
            std::cerr << "Primary scan row count mismatch. expected=" << expectedRows
                      << " actual=" << outPhase.rows << std::endl;
            return false;
        }
        return true;
    }

    bool benchmarkPrimaryPageScan_(SwEmbeddedDb& reader,
                                   unsigned long long pageRows,
                                   PhaseResult& outPhase) {
        outPhase.name = SwString("scan-primary-page-") + SwString::number(pageRows);
        if (pageRows == 0 || options_.records < pageRows) {
            outPhase.skipped = true;
            outPhase.note = SwString("records<") + SwString::number(pageRows);
            return true;
        }

        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();
        SwDbIterator iterator = reader.scanPrimary();
        while (iterator.isValid() && outPhase.rows < pageRows) {
            const SwDbEntry& row = iterator.current();
            outPhase.rows += 1;
            outPhase.operations += 1;
            outPhase.bytes += row.value.size();
            if (outPhase.rows >= pageRows) {
                break;
            }
            iterator.next();
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);

        if (outPhase.rows != pageRows) {
            std::cerr << "Primary page scan row count mismatch. expected=" << pageRows
                      << " actual=" << outPhase.rows << std::endl;
            return false;
        }
        return true;
    }

    bool benchmarkIndexScan_(SwEmbeddedDb& reader, PhaseResult& outPhase) {
        outPhase.name = "scan-index";
        const unsigned long long targetGroup = options_.records == 0 ? 0 : (options_.records / 2ull) % options_.indexCardinality;
        const SwByteArray startKey = makeGroupKey_(targetGroup);
        const SwByteArray endKey = makeGroupKey_(targetGroup + 1ull);
        unsigned long long expectedRows = 0;
        if (targetGroup < options_.records) {
            expectedRows = 1ull + ((options_.records - 1ull - targetGroup) / options_.indexCardinality);
        }
        if (options_.scanLimit > 0) {
            expectedRows = std::min(expectedRows, options_.scanLimit);
        }

        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();
        SwDbIterator iterator = reader.scanIndex(kGroupIndexName, startKey, endKey);
        while (iterator.isValid()) {
            const SwDbEntry& row = iterator.current();
            if (row.secondaryKey != startKey) {
                std::cerr << "Index scan returned an unexpected secondary key" << std::endl;
                return false;
            }
            outPhase.rows += 1;
            outPhase.operations += 1;
            outPhase.bytes += row.value.size();
            if (options_.scanLimit > 0 && outPhase.rows >= options_.scanLimit) {
                break;
            }
            iterator.next();
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);

        if (outPhase.rows != expectedRows) {
            std::cerr << "Index scan row count mismatch. expected=" << expectedRows
                      << " actual=" << outPhase.rows << std::endl;
            return false;
        }

        outPhase.note = SwString("group=") + SwString(startKey);
        return true;
    }

    BenchOptions options_;
    SwString dbPath_;
    bool autoCleanup_{false};
};

} // namespace

int main(int argc, char* argv[]) {
    BenchOptions options;
    bool helpRequested = false;
    if (!parseBenchOptions(argc, argv, options, helpRequested)) {
        return helpRequested ? 0 : 1;
    }

    EmbeddedDbBenchRunner runner(options);
    return runner.run();
}
