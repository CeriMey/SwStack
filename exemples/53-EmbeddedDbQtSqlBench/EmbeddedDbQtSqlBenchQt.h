#pragma once

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#ifdef SIGNAL
#undef SIGNAL
#endif

#ifdef SLOT
#undef SLOT
#endif

#include "SwDir.h"
#include "SwStandardLocation.h"
#include "platform/SwDbPlatform.h"

#include "EmbeddedDbQtSqlBenchShared.h"
#include "EmbeddedDbQtSqlBenchSw.h"

namespace swEmbeddedDbQtSqlBench {

class QtSqlRunner {
public:
    QtSqlRunner(const BenchOptions& options,
                const WorkloadModel& workload,
                const SwString& sqliteFilePath)
        : options_(options),
          workload_(workload),
          sqliteFilePath_(sqliteFilePath) {
    }

    bool run(EngineResults& outResults) {
        outResults.engineName = "QSqlSQLite";
        outResults.note = SwString("journal_mode=") + options_.sqliteJournalMode +
                          ", synchronous=" + options_.sqliteSynchronous;

        QSqlDatabase writerDb;
        if (!openWriter_(writerDb, outResults.writerOpen)) {
            return false;
        }
        if (!benchmarkWrites_(writerDb, outResults.write)) {
            PhaseResult ignoredClose;
            closeConnection_(writerDb, writerConnectionName_, ignoredClose, false);
            return false;
        }
        closeConnection_(writerDb, writerConnectionName_, outResults.writerClose, true);

        QSqlDatabase readerDb;
        if (!openReader_(readerDb, outResults.readerOpen)) {
            return false;
        }
        const bool ok =
            benchmarkGets_(readerDb, false, outResults.get) &&
            benchmarkGets_(readerDb, true, outResults.blobGet) &&
            benchmarkPrimaryPageScan_(readerDb, 100, outResults.primaryPage100) &&
            benchmarkPrimaryPageScan_(readerDb, 1000, outResults.primaryPage1000) &&
            benchmarkPrimaryScan_(readerDb, outResults.primaryScan) &&
            benchmarkIndexScan_(readerDb, outResults.indexScan);
        PhaseResult ignoredClose;
        closeConnection_(readerDb, readerConnectionName_, ignoredClose, false);
        return ok;
    }

private:
    bool openWriter_(QSqlDatabase& outDb, PhaseResult& outPhase) {
        outPhase.name = "writer-open";
        outPhase.operations = 1;
        writerConnectionName_ = QString::fromLatin1("qt-sqlite-writer-%1")
            .arg(static_cast<qulonglong>(std::chrono::steady_clock::now().time_since_epoch().count()));

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        outDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), writerConnectionName_);
        outDb.setDatabaseName(toQString(sqliteFilePath_));
        if (!outDb.open()) {
            std::cerr << "QSql writer open failed: " << outDb.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!applyPragmas_(outDb) || !createSchema_(outDb)) {
            return false;
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - start);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);
        outPhase.note = outResultsNote_();
        return true;
    }

    bool openReader_(QSqlDatabase& outDb, PhaseResult& outPhase) {
        outPhase.name = "reader-open";
        outPhase.operations = 1;
        readerConnectionName_ = QString::fromLatin1("qt-sqlite-reader-%1")
            .arg(static_cast<qulonglong>(std::chrono::steady_clock::now().time_since_epoch().count()));

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        outDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), readerConnectionName_);
        outDb.setDatabaseName(toQString(sqliteFilePath_));
        if (!outDb.open()) {
            std::cerr << "QSql reader open failed: " << outDb.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!applyPragmas_(outDb)) {
            return false;
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - start);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);
        outPhase.note = outResultsNote_();
        return true;
    }

    bool applyPragmas_(QSqlDatabase& db) const {
        QSqlQuery pragma(db);
        if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=%1").arg(toQString(options_.sqliteJournalMode)))) {
            std::cerr << "QSql PRAGMA journal_mode failed: " << pragma.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!pragma.exec(QStringLiteral("PRAGMA synchronous=%1").arg(toQString(options_.sqliteSynchronous)))) {
            std::cerr << "QSql PRAGMA synchronous failed: " << pragma.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"))) {
            std::cerr << "QSql PRAGMA temp_store failed: " << pragma.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!pragma.exec(QStringLiteral("PRAGMA cache_size=-65536"))) {
            std::cerr << "QSql PRAGMA cache_size failed: " << pragma.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!pragma.exec(QStringLiteral("PRAGMA mmap_size=268435456"))) {
            std::cerr << "QSql PRAGMA mmap_size failed: " << pragma.lastError().text().toStdString() << std::endl;
            return false;
        }
        return true;
    }

    bool createSchema_(QSqlDatabase& db) const {
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS records ("
                "primary_key TEXT PRIMARY KEY NOT NULL,"
                "value BLOB NOT NULL,"
                "group_key TEXT NOT NULL)"))) {
            std::cerr << "QSql CREATE TABLE failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }
        if (!query.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_records_group_key ON records(group_key, primary_key)"))) {
            std::cerr << "QSql CREATE INDEX failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }
        return true;
    }

    void closeConnection_(QSqlDatabase& db,
                          const QString& connectionName,
                          PhaseResult& outPhase,
                          bool measure) const {
        outPhase.name = measure ? SwString("writer-close") : SwString("close");
        outPhase.operations = 1;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        if (measure) {
            outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - start);
            outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);
        }
    }

    bool benchmarkWrites_(QSqlDatabase& db, PhaseResult& outPhase) const {
        outPhase.name = "write";
        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();

        QSqlQuery insert(db);
        if (!insert.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO records(primary_key, value, group_key) VALUES(?, ?, ?)"))) {
            std::cerr << "QSql prepare INSERT failed: " << insert.lastError().text().toStdString() << std::endl;
            return false;
        }

        unsigned long long recordId = 0;
        while (recordId < options_.records) {
            unsigned long long batchBytes = 0;
            unsigned long long batchCount = 0;
            const std::chrono::steady_clock::time_point batchStart = std::chrono::steady_clock::now();
            if (!db.transaction()) {
                std::cerr << "QSql transaction begin failed: " << db.lastError().text().toStdString() << std::endl;
                return false;
            }

            for (; recordId < options_.records && batchCount < options_.batchSize; ++recordId, ++batchCount) {
                const unsigned long long valueBytes = workload_.expectedValueBytes(recordId);
                const SwByteArray primaryKey = workload_.primaryKey(recordId);
                const SwByteArray value = workload_.payload(recordId, valueBytes);
                const SwByteArray groupKey = workload_.groupKey(recordId % options_.indexCardinality);

                insert.bindValue(0, toQString(primaryKey));
                insert.bindValue(1, toQByteArray(value));
                insert.bindValue(2, toQString(groupKey));
                if (!insert.exec()) {
                    std::cerr << "QSql INSERT failed at record " << recordId << ": "
                              << insert.lastError().text().toStdString() << std::endl;
                    (void)db.rollback();
                    return false;
                }
                batchBytes += valueBytes;
            }

            if (!db.commit()) {
                std::cerr << "QSql commit failed: " << db.lastError().text().toStdString() << std::endl;
                (void)db.rollback();
                return false;
            }

            outPhase.latencyMicros.push_back(elapsedMicros(std::chrono::steady_clock::now() - batchStart));
            outPhase.operations += batchCount;
            outPhase.rows += batchCount;
            outPhase.bytes += batchBytes;
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        outPhase.note = SwString("logical-bytes=") + humanBytes(outPhase.bytes);
        return true;
    }

    bool benchmarkGets_(QSqlDatabase& db, bool blobsOnly, PhaseResult& outPhase) const {
        outPhase.name = blobsOnly ? SwString("get-blob") : SwString("get");
        const unsigned long long population = blobsOnly ? workload_.blobRecordCount() : options_.records;
        if (population == 0 || options_.readSamples == 0) {
            outPhase.skipped = true;
            outPhase.note = population == 0 ? SwString("no eligible records") : SwString("read-samples=0");
            return true;
        }

        QSqlQuery query(db);
        if (!query.prepare(QStringLiteral("SELECT value, group_key FROM records WHERE primary_key = ?"))) {
            std::cerr << "QSql prepare SELECT failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }

        const unsigned long long sampleCount = std::min(options_.readSamples, population);
        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();
        for (unsigned long long sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            unsigned long long recordId = 0;
            if (blobsOnly) {
                const unsigned long long blobOrdinal = workload_.sampleRecordId(sampleIndex, population, 0x123456789ull);
                recordId = ((blobOrdinal + 1ull) * options_.blobEvery) - 1ull;
            } else {
                recordId = workload_.sampleRecordId(sampleIndex, population, 0xfeedfaceull);
            }

            query.bindValue(0, toQString(workload_.primaryKey(recordId)));
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            if (!query.exec() || !query.next()) {
                std::cerr << "QSql SELECT failed for record " << recordId << ": "
                          << query.lastError().text().toStdString() << std::endl;
                return false;
            }
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

            const QByteArray value = query.value(0).toByteArray();
            const QString groupKey = query.value(1).toString();
            if (value != toQByteArray(workload_.payload(recordId, workload_.expectedValueBytes(recordId)))) {
                std::cerr << "QSql payload mismatch for record " << recordId << std::endl;
                return false;
            }
            if (groupKey != toQString(workload_.groupKey(recordId % options_.indexCardinality))) {
                std::cerr << "QSql group key mismatch for record " << recordId << std::endl;
                return false;
            }

            outPhase.latencyMicros.push_back(elapsedMicros(end - start));
            outPhase.operations += 1;
            outPhase.rows += 1;
            outPhase.bytes += static_cast<unsigned long long>(value.size());
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        return true;
    }

    bool benchmarkPrimaryPageScan_(QSqlDatabase& db,
                                   unsigned long long pageRows,
                                   PhaseResult& outPhase) const {
        outPhase.name = SwString("scan-primary-page-") + SwString::number(pageRows);
        if (pageRows == 0 || options_.records < pageRows) {
            outPhase.skipped = true;
            outPhase.note = SwString("records<") + SwString::number(pageRows);
            return true;
        }

        QSqlQuery query(db);
        query.setForwardOnly(true);
        if (!query.prepare(QStringLiteral(
                "SELECT primary_key, value FROM records ORDER BY primary_key LIMIT ?"))) {
            std::cerr << "QSql prepare page scan failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }
        query.bindValue(0, QVariant::fromValue(static_cast<qlonglong>(pageRows)));

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (!query.exec()) {
            std::cerr << "QSql page scan failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }
        while (query.next()) {
            outPhase.rows += 1;
            outPhase.operations += 1;
            outPhase.bytes += static_cast<unsigned long long>(query.value(1).toByteArray().size());
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - start);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);
        return outPhase.rows == pageRows;
    }

    bool benchmarkPrimaryScan_(QSqlDatabase& db, PhaseResult& outPhase) const {
        outPhase.name = "scan-primary";
        const unsigned long long expectedRows =
            options_.scanLimit == 0 ? options_.records : std::min(options_.scanLimit, options_.records);

        QSqlQuery query(db);
        query.setForwardOnly(true);
        if (!query.exec(QStringLiteral("SELECT primary_key, value FROM records ORDER BY primary_key"))) {
            std::cerr << "QSql primary scan failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }

        QString previousKey;
        bool hasPrevious = false;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        while (query.next()) {
            const QString primaryKey = query.value(0).toString();
            if (hasPrevious && primaryKey < previousKey) {
                return false;
            }
            hasPrevious = true;
            previousKey = primaryKey;
            outPhase.rows += 1;
            outPhase.operations += 1;
            outPhase.bytes += static_cast<unsigned long long>(query.value(1).toByteArray().size());
            if (options_.scanLimit > 0 && outPhase.rows >= options_.scanLimit) {
                break;
            }
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - start);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);
        return outPhase.rows == expectedRows;
    }

    bool benchmarkIndexScan_(QSqlDatabase& db, PhaseResult& outPhase) const {
        outPhase.name = "scan-index";
        const unsigned long long targetGroup =
            options_.records == 0 ? 0 : (options_.records / 2ull) % options_.indexCardinality;
        const QString groupKey = toQString(workload_.groupKey(targetGroup));
        unsigned long long expectedRows = 0;
        if (targetGroup < options_.records) {
            expectedRows = 1ull + ((options_.records - 1ull - targetGroup) / options_.indexCardinality);
        }
        if (options_.scanLimit > 0) {
            expectedRows = std::min(expectedRows, options_.scanLimit);
        }

        QSqlQuery query(db);
        query.setForwardOnly(true);
        if (!query.prepare(QStringLiteral(
                "SELECT primary_key, value, group_key FROM records WHERE group_key = ? ORDER BY primary_key"))) {
            std::cerr << "QSql prepare index scan failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }
        query.bindValue(0, groupKey);

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (!query.exec()) {
            std::cerr << "QSql index scan failed: " << query.lastError().text().toStdString() << std::endl;
            return false;
        }
        while (query.next()) {
            if (query.value(2).toString() != groupKey) {
                return false;
            }
            outPhase.rows += 1;
            outPhase.operations += 1;
            outPhase.bytes += static_cast<unsigned long long>(query.value(1).toByteArray().size());
            if (options_.scanLimit > 0 && outPhase.rows >= options_.scanLimit) {
                break;
            }
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - start);
        outPhase.latencyMicros.push_back(outPhase.totalMs * 1000.0);
        outPhase.note = SwString("group=") + SwString(groupKey.toStdString());
        return outPhase.rows == expectedRows;
    }

    SwString outResultsNote_() const {
        return SwString("journal_mode=") + options_.sqliteJournalMode + ", synchronous=" + options_.sqliteSynchronous;
    }

    const BenchOptions& options_;
    const WorkloadModel& workload_;
    SwString sqliteFilePath_;
    mutable QString writerConnectionName_;
    mutable QString readerConnectionName_;
};

class EmbeddedDbQtSqlBenchRunner {
public:
    EmbeddedDbQtSqlBenchRunner(const BenchOptions& options, QCoreApplication& app)
        : options_(options),
          workload_(options),
          app_(app) {
    }

    int run() {
        if (!preparePaths_()) {
            return 1;
        }

        configureQtPaths_();
        printConfiguration_();

        if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
            std::cerr << "QSQLITE driver not available. Drivers=";
            const QStringList drivers = QSqlDatabase::drivers();
            for (int i = 0; i < drivers.size(); ++i) {
                if (i > 0) {
                    std::cerr << ",";
                }
                std::cerr << drivers[i].toStdString();
            }
            std::cerr << std::endl;
            cleanup_();
            return 1;
        }

        EngineResults swResults;
        if (!EmbeddedDbRunner(options_, workload_, swDbPath_).run(swResults)) {
            cleanup_();
            return 1;
        }
        printEngineResults_(swResults);

        EngineResults sqliteResults;
        if (!QtSqlRunner(options_, workload_, sqliteDbFile_).run(sqliteResults)) {
            cleanup_();
            return 1;
        }
        printEngineResults_(sqliteResults);

        printComparison_(swResults, sqliteResults);
        cleanup_();
        return 0;
    }

private:
    bool preparePaths_() {
        bool shouldReset = false;
        if (!options_.dbRoot.isEmpty()) {
            rootPath_ = swDbPlatform::normalizePath(options_.dbRoot);
            autoCleanup_ = false;
            if (swDbPlatform::directoryExists(rootPath_) || swDbPlatform::fileExists(rootPath_)) {
                std::cerr << "Refusing to reuse explicit --db-root because the path already exists: "
                          << rootPath_.toStdString() << std::endl;
                return false;
            }
        } else {
            const SwString baseTemp = SwStandardLocation::standardLocation(SwStandardLocationId::Temp);
            const unsigned long long token = static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            rootPath_ = swDbPlatform::joinPath(baseTemp,
                                               SwString("SwEmbeddedDbQtSqlBench-") + SwString::number(token));
            autoCleanup_ = !options_.keepDb;
            shouldReset = true;
        }

        if (shouldReset) {
            (void)SwDir::removeRecursively(rootPath_);
        }
        swDbPath_ = swDbPlatform::joinPath(rootPath_, "swembedded");
        sqliteDbDir_ = swDbPlatform::joinPath(rootPath_, "qtsqlite");
        sqliteDbFile_ = swDbPlatform::joinPath(sqliteDbDir_, "compare.sqlite");

        return SwDir::mkpathAbsolute(rootPath_) &&
               SwDir::mkpathAbsolute(swDbPath_) &&
               SwDir::mkpathAbsolute(sqliteDbDir_);
    }

    void cleanup_() {
        if (autoCleanup_) {
            (void)SwDir::removeRecursively(rootPath_);
        }
    }

    void configureQtPaths_() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QString pluginsPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
#else
        const QString pluginsPath = QLibraryInfo::location(QLibraryInfo::PluginsPath);
#endif
        if (!pluginsPath.isEmpty()) {
            app_.addLibraryPath(pluginsPath);
        }
    }

    void printConfiguration_() const {
        std::cout << "[CONFIG] root=" << rootPath_.toStdString() << std::endl;
        std::cout << "[CONFIG] sw-db=" << swDbPath_.toStdString() << std::endl;
        std::cout << "[CONFIG] sqlite-db=" << sqliteDbFile_.toStdString() << std::endl;
        std::cout << "[CONFIG] records=" << options_.records
                  << " batch-size=" << options_.batchSize
                  << " value-bytes=" << options_.valueBytes
                  << " blob-bytes=" << options_.blobBytes
                  << " blob-every=" << options_.blobEvery
                  << " read-samples=" << options_.readSamples
                  << " scan-limit=" << options_.scanLimit
                  << " index-cardinality=" << options_.indexCardinality << std::endl;
        std::cout << "[CONFIG] sw.commit-window-ms=" << options_.commitWindowMs
                  << " sw.memtable-bytes=" << options_.memTableBytes
                  << " sw.inline-blob-threshold-bytes=" << options_.inlineBlobThresholdBytes
                  << " sw.max-background-jobs=" << options_.maxBackgroundJobs << std::endl;
        std::cout << "[CONFIG] sqlite.journal-mode=" << options_.sqliteJournalMode.toStdString()
                  << " sqlite.synchronous=" << options_.sqliteSynchronous.toStdString()
                  << " keep-db=" << (options_.keepDb ? "true" : "false") << std::endl;
    }

    static void printEngineResults_(const EngineResults& results) {
        std::cout << "\n===== " << results.engineName.toStdString() << " =====" << std::endl;
        if (!results.note.isEmpty()) {
            std::cout << "[NOTE][" << results.engineName.toStdString() << "] "
                      << results.note.toStdString() << std::endl;
        }
        printPhase(results.engineName, results.writerOpen);
        printPhase(results.engineName, results.write);
        printPhase(results.engineName, results.writerClose);
        printPhase(results.engineName, results.readerOpen);
        printPhase(results.engineName, results.get);
        printPhase(results.engineName, results.blobGet);
        printPhase(results.engineName, results.primaryPage100);
        printPhase(results.engineName, results.primaryPage1000);
        printPhase(results.engineName, results.primaryScan);
        printPhase(results.engineName, results.indexScan);
    }

    static void printComparison_(const EngineResults& swResults, const EngineResults& sqliteResults) {
        std::cout << "\n===== Comparison =====" << std::endl;
        compareTotalPhase("writer-open", swResults.writerOpen, sqliteResults.writerOpen);
        compareLatencyPhase("write", swResults.write, sqliteResults.write);
        compareTotalPhase("writer-close", swResults.writerClose, sqliteResults.writerClose);
        compareTotalPhase("reader-open", swResults.readerOpen, sqliteResults.readerOpen);
        compareLatencyPhase("get", swResults.get, sqliteResults.get);
        compareLatencyPhase("get-blob", swResults.blobGet, sqliteResults.blobGet);
        compareTotalPhase("scan-primary-page-100", swResults.primaryPage100, sqliteResults.primaryPage100);
        compareTotalPhase("scan-primary-page-1000", swResults.primaryPage1000, sqliteResults.primaryPage1000);
        compareTotalPhase("scan-primary", swResults.primaryScan, sqliteResults.primaryScan);
        compareTotalPhase("scan-index", swResults.indexScan, sqliteResults.indexScan);
    }

    BenchOptions options_;
    WorkloadModel workload_;
    QCoreApplication& app_;
    SwString rootPath_;
    SwString swDbPath_;
    SwString sqliteDbDir_;
    SwString sqliteDbFile_;
    bool autoCleanup_{false};
};

} // namespace swEmbeddedDbQtSqlBench
