#pragma once

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "SwEmbeddedDb.h"

namespace swEmbeddedDbQtSqlBench {

static const SwString kGroupIndexName("by_group");

struct BenchOptions {
    SwString dbRoot;
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
    SwString sqliteJournalMode{"WAL"};
    SwString sqliteSynchronous{"FULL"};
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

struct EngineResults {
    SwString engineName;
    SwString note;
    PhaseResult writerOpen;
    PhaseResult write;
    PhaseResult writerClose;
    PhaseResult readerOpen;
    PhaseResult get;
    PhaseResult blobGet;
    PhaseResult primaryPage100;
    PhaseResult primaryPage1000;
    PhaseResult primaryScan;
    PhaseResult indexScan;
};

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
    while (value >= 1024.0 && unitIndex + 1 < (sizeof(units) / sizeof(units[0]))) {
        value /= 1024.0;
        ++unitIndex;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unitIndex]);
    return SwString(buffer);
}

static unsigned long long splitMix64(unsigned long long x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

static PhaseSummary summarizeLatencies(const std::vector<double>& latencies) {
    PhaseSummary summary;
    if (latencies.empty()) {
        return summary;
    }

    std::vector<double> ordered = latencies;
    std::sort(ordered.begin(), ordered.end());

    double sum = 0.0;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        sum += ordered[i];
    }

    const auto percentile = [&](double p) -> double {
        const double index = p * static_cast<double>(ordered.size() - 1);
        const std::size_t lower = static_cast<std::size_t>(index);
        const std::size_t upper = std::min(lower + 1u, ordered.size() - 1u);
        const double fraction = index - static_cast<double>(lower);
        return ordered[lower] + ((ordered[upper] - ordered[lower]) * fraction);
    };

    summary.averageMicros = sum / static_cast<double>(ordered.size());
    summary.p50Micros = percentile(0.50);
    summary.p95Micros = percentile(0.95);
    summary.p99Micros = percentile(0.99);
    summary.maxMicros = ordered.back();
    return summary;
}

static void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "  --db-root <path>                      Root directory for benchmark artifacts\n"
              << "  --records <count>                     Number of records to write\n"
              << "  --batch-size <count>                  Records per durable write batch\n"
              << "  --value-bytes <bytes>                 Small value size in bytes\n"
              << "  --blob-bytes <bytes>                  Large value size in bytes\n"
              << "  --blob-every <n>                      Every n-th record uses blob-bytes\n"
              << "  --read-samples <count>                Random reads per read phase\n"
              << "  --scan-limit <count>                  Limit rows for full scan phases (0 = full)\n"
              << "  --index-cardinality <count>           Distinct secondary index values\n"
              << "  --commit-window-ms <ms>               SwEmbeddedDb commit window\n"
              << "  --lazy-write                         SwEmbeddedDb publishes writes before WAL durability\n"
              << "  --memtable-bytes <bytes>              SwEmbeddedDb memtable threshold\n"
              << "  --inline-blob-threshold-bytes <bytes> SwEmbeddedDb inline blob threshold\n"
              << "  --max-background-jobs <count>         SwEmbeddedDb background jobs\n"
              << "  --sqlite-journal-mode <mode>          SQLite PRAGMA journal_mode (default WAL)\n"
              << "  --sqlite-synchronous <mode>           SQLite PRAGMA synchronous (default FULL)\n"
              << "  --keep-db                             Keep benchmark directories on disk\n"
              << "  --help                                Show this message\n";
}

static bool parseBenchOptions(int argc,
                              char** argv,
                              BenchOptions& outOptions,
                              bool& helpRequested) {
    helpRequested = false;
    for (int i = 1; i < argc; ++i) {
        const SwString arg(argv[i] ? argv[i] : "");
        auto requireValue = [&](const char* optionName) -> const char* {
            if (i + 1 >= argc || !argv[i + 1]) {
                std::cerr << "Missing value for " << optionName << std::endl;
                return nullptr;
            }
            ++i;
            return argv[i];
        };

        unsigned long long parsedU64 = 0;
        int parsedInt = 0;

        if (arg == "--help") {
            helpRequested = true;
            printUsage(argv[0]);
            return true;
        } else if (arg == "--db-root") {
            const char* value = requireValue("--db-root");
            if (!value) {
                return false;
            }
            outOptions.dbRoot = SwString(value);
        } else if (arg == "--lazy-write") {
            outOptions.lazyWrite = true;
        } else if (arg == "--records") {
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
        } else if (arg == "--sqlite-journal-mode") {
            const char* value = requireValue("--sqlite-journal-mode");
            if (!value) {
                return false;
            }
            outOptions.sqliteJournalMode = SwString(value).trimmed().toUpper();
        } else if (arg == "--sqlite-synchronous") {
            const char* value = requireValue("--sqlite-synchronous");
            if (!value) {
                return false;
            }
            outOptions.sqliteSynchronous = SwString(value).trimmed().toUpper();
        } else if (arg == "--keep-db") {
            outOptions.keepDb = true;
        } else {
            std::cerr << "Unknown argument: " << arg.toStdString() << std::endl;
            printUsage(argv[0]);
            return false;
        }
    }

    if (outOptions.records == 0 || outOptions.batchSize == 0 || outOptions.indexCardinality == 0) {
        std::cerr << "records, batch-size and index-cardinality must be > 0" << std::endl;
        return false;
    }
    if (outOptions.maxBackgroundJobs <= 0) {
        std::cerr << "--max-background-jobs must be > 0" << std::endl;
        return false;
    }
    return true;
}

class WorkloadModel {
public:
    explicit WorkloadModel(const BenchOptions& options)
        : options_(options) {
    }

    SwByteArray primaryKey(unsigned long long recordId) const {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "row:%012llu", recordId);
        return SwByteArray(buffer);
    }

    SwByteArray groupKey(unsigned long long groupId) const {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "group:%010llu", groupId);
        return SwByteArray(buffer);
    }

    SwByteArray payload(unsigned long long recordId, unsigned long long bytes) const {
        static const char alphabet[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
        static const std::size_t alphabetSize = sizeof(alphabet) - 1;

        SwByteArray value(static_cast<std::size_t>(bytes), '\0');
        for (unsigned long long i = 0; i < bytes; ++i) {
            const unsigned long long mixed = splitMix64(recordId ^ (i * 1315423911ull));
            value[static_cast<std::size_t>(i)] = alphabet[mixed % alphabetSize];
        }
        return value;
    }

    SwMap<SwString, SwList<SwByteArray> > secondaryKeys(unsigned long long recordId) const {
        SwMap<SwString, SwList<SwByteArray> > secondary;
        SwList<SwByteArray> values;
        values.append(groupKey(recordId % options_.indexCardinality));
        secondary[kGroupIndexName] = values;
        return secondary;
    }

    bool isBlobRecord(unsigned long long recordId) const {
        return options_.blobEvery > 0 && ((recordId + 1ull) % options_.blobEvery == 0ull);
    }

    unsigned long long expectedValueBytes(unsigned long long recordId) const {
        return isBlobRecord(recordId) ? options_.blobBytes : options_.valueBytes;
    }

    unsigned long long blobRecordCount() const {
        if (options_.blobEvery == 0) {
            return 0;
        }
        return options_.records / options_.blobEvery;
    }

    unsigned long long sampleRecordId(unsigned long long sampleIndex,
                                      unsigned long long population,
                                      unsigned long long salt) const {
        if (population == 0) {
            return 0;
        }
        return splitMix64(sampleIndex ^ salt) % population;
    }

private:
    const BenchOptions& options_;
};

static QByteArray toQByteArray(const SwByteArray& bytes) {
    return QByteArray(bytes.constData(), static_cast<int>(bytes.size()));
}

static QString toQString(const SwByteArray& bytes) {
    return QString::fromLatin1(bytes.constData(), static_cast<int>(bytes.size()));
}

static QString toQString(const SwString& text) {
    return QString::fromUtf8(text.toStdString().c_str());
}

static void printPhase(const SwString& engineName, const PhaseResult& phase) {
    std::cout << "[PHASE][" << engineName.toStdString() << "] " << phase.name.toStdString();
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

static void compareLatencyPhase(const char* label,
                                const PhaseResult& swPhase,
                                const PhaseResult& sqlitePhase) {
    if (swPhase.skipped || sqlitePhase.skipped || swPhase.latencyMicros.empty() || sqlitePhase.latencyMicros.empty()) {
        return;
    }
    const PhaseSummary swSummary = summarizeLatencies(swPhase.latencyMicros);
    const PhaseSummary sqliteSummary = summarizeLatencies(sqlitePhase.latencyMicros);
    const double p50Ratio = sqliteSummary.p50Micros > 0.0 ? swSummary.p50Micros / sqliteSummary.p50Micros : 0.0;
    const double p95Ratio = sqliteSummary.p95Micros > 0.0 ? swSummary.p95Micros / sqliteSummary.p95Micros : 0.0;

    std::cout << "[COMPARE] " << label
              << " sw-p50-us=" << std::fixed << std::setprecision(2) << swSummary.p50Micros
              << " sqlite-p50-us=" << sqliteSummary.p50Micros
              << " sw/sqlite-p50=" << p50Ratio
              << " sw-p95-us=" << swSummary.p95Micros
              << " sqlite-p95-us=" << sqliteSummary.p95Micros
              << " sw/sqlite-p95=" << p95Ratio
              << std::endl;
}

static void compareTotalPhase(const char* label,
                              const PhaseResult& swPhase,
                              const PhaseResult& sqlitePhase) {
    if (swPhase.skipped || sqlitePhase.skipped) {
        return;
    }
    const double ratio = sqlitePhase.totalMs > 0.0 ? swPhase.totalMs / sqlitePhase.totalMs : 0.0;
    std::cout << "[COMPARE] " << label
              << " sw-ms=" << std::fixed << std::setprecision(2) << swPhase.totalMs
              << " sqlite-ms=" << sqlitePhase.totalMs
              << " sw/sqlite=" << ratio
              << std::endl;
}

} // namespace swEmbeddedDbQtSqlBench
