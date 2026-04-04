#pragma once

#include "EmbeddedDbQtSqlBenchShared.h"

namespace swEmbeddedDbQtSqlBench {

class EmbeddedDbRunner {
public:
    EmbeddedDbRunner(const BenchOptions& options, const WorkloadModel& workload, const SwString& dbPath)
        : options_(options),
          workload_(workload),
          dbPath_(dbPath) {
    }

    bool run(EngineResults& outResults) {
        outResults.engineName = "SwEmbeddedDb";
        outResults.note = options_.lazyWrite ? "lazy-write=true" : "lazy-write=false";

        SwEmbeddedDb writer;
        SwEmbeddedDbOptions writerOptions;
        writerOptions.dbPath = dbPath_;
        writerOptions.readOnly = false;
        writerOptions.lazyWrite = options_.lazyWrite;
        writerOptions.commitWindowMs = options_.commitWindowMs;
        writerOptions.memTableBytes = options_.memTableBytes;
        writerOptions.inlineBlobThresholdBytes = options_.inlineBlobThresholdBytes;
        writerOptions.maxBackgroundJobs = options_.maxBackgroundJobs;
        writerOptions.enableShmNotifications = false;

        outResults.writerOpen.name = "writer-open";
        outResults.writerOpen.operations = 1;
        const std::chrono::steady_clock::time_point writerOpenStart = std::chrono::steady_clock::now();
        const SwDbStatus openWriterStatus = writer.open(writerOptions);
        outResults.writerOpen.totalMs = elapsedMs(std::chrono::steady_clock::now() - writerOpenStart);
        outResults.writerOpen.latencyMicros.push_back(outResults.writerOpen.totalMs * 1000.0);
        if (!openWriterStatus.ok()) {
            std::cerr << "SwEmbeddedDb writer open failed: " << openWriterStatus.message().toStdString() << std::endl;
            return false;
        }

        if (!benchmarkWrites_(writer, outResults.write)) {
            writer.close();
            return false;
        }

        outResults.writerClose.name = "writer-close";
        outResults.writerClose.operations = 1;
        const std::chrono::steady_clock::time_point writerCloseStart = std::chrono::steady_clock::now();
        writer.close();
        outResults.writerClose.totalMs = elapsedMs(std::chrono::steady_clock::now() - writerCloseStart);
        outResults.writerClose.latencyMicros.push_back(outResults.writerClose.totalMs * 1000.0);

        SwEmbeddedDb reader;
        SwEmbeddedDbOptions readerOptions = writerOptions;
        readerOptions.readOnly = true;

        outResults.readerOpen.name = "reader-open";
        outResults.readerOpen.operations = 1;
        const std::chrono::steady_clock::time_point readerOpenStart = std::chrono::steady_clock::now();
        const SwDbStatus openReaderStatus = reader.open(readerOptions);
        outResults.readerOpen.totalMs = elapsedMs(std::chrono::steady_clock::now() - readerOpenStart);
        outResults.readerOpen.latencyMicros.push_back(outResults.readerOpen.totalMs * 1000.0);
        if (!openReaderStatus.ok()) {
            std::cerr << "SwEmbeddedDb reader open failed: " << openReaderStatus.message().toStdString() << std::endl;
            return false;
        }

        const bool ok =
            benchmarkGets_(reader, false, outResults.get) &&
            benchmarkGets_(reader, true, outResults.blobGet) &&
            benchmarkPrimaryPageScan_(reader, 100, outResults.primaryPage100) &&
            benchmarkPrimaryPageScan_(reader, 1000, outResults.primaryPage1000) &&
            benchmarkPrimaryScan_(reader, outResults.primaryScan) &&
            benchmarkIndexScan_(reader, outResults.indexScan);
        reader.close();
        return ok;
    }

private:
    bool benchmarkWrites_(SwEmbeddedDb& writer, PhaseResult& outPhase) const {
        outPhase.name = "write";
        const std::chrono::steady_clock::time_point overallStart = std::chrono::steady_clock::now();

        SwDbWriteBatch batch;
        unsigned long long recordId = 0;
        while (recordId < options_.records) {
            batch.clear();
            unsigned long long batchBytes = 0;
            unsigned long long batchCount = 0;
            for (; recordId < options_.records && batchCount < options_.batchSize; ++recordId, ++batchCount) {
                const unsigned long long valueBytes = workload_.expectedValueBytes(recordId);
                batch.put(workload_.primaryKey(recordId),
                          workload_.payload(recordId, valueBytes),
                          workload_.secondaryKeys(recordId));
                batchBytes += valueBytes;
            }

            const std::chrono::steady_clock::time_point writeStart = std::chrono::steady_clock::now();
            const SwDbStatus status = writer.write(std::move(batch));
            const std::chrono::steady_clock::time_point writeEnd = std::chrono::steady_clock::now();
            if (!status.ok()) {
                std::cerr << "SwEmbeddedDb write failed: " << status.message().toStdString() << std::endl;
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

    bool benchmarkGets_(SwEmbeddedDb& reader, bool blobsOnly, PhaseResult& outPhase) const {
        outPhase.name = blobsOnly ? SwString("get-blob") : SwString("get");
        const unsigned long long population = blobsOnly ? workload_.blobRecordCount() : options_.records;
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
                const unsigned long long blobOrdinal = workload_.sampleRecordId(sampleIndex, population, 0x123456789ull);
                recordId = ((blobOrdinal + 1ull) * options_.blobEvery) - 1ull;
            } else {
                recordId = workload_.sampleRecordId(sampleIndex, population, 0xfeedfaceull);
            }

            SwByteArray value;
            SwMap<SwString, SwList<SwByteArray> > secondaryKeys;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const SwDbStatus status = reader.get(workload_.primaryKey(recordId), &value, &secondaryKeys);
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            if (!status.ok()) {
                std::cerr << "SwEmbeddedDb get failed for record " << recordId << std::endl;
                return false;
            }
            if (value != workload_.payload(recordId, workload_.expectedValueBytes(recordId))) {
                std::cerr << "SwEmbeddedDb payload mismatch for record " << recordId << std::endl;
                return false;
            }
            const SwByteArray expectedGroup = workload_.groupKey(recordId % options_.indexCardinality);
            const SwMap<SwString, SwList<SwByteArray> >::const_iterator it = secondaryKeys.find(kGroupIndexName);
            if (it == secondaryKeys.end() || it.value().isEmpty() || it.value()[0] != expectedGroup) {
                std::cerr << "SwEmbeddedDb secondary index mismatch for record " << recordId << std::endl;
                return false;
            }

            outPhase.latencyMicros.push_back(elapsedMicros(end - start));
            outPhase.operations += 1;
            outPhase.rows += 1;
            outPhase.bytes += value.size();
        }
        outPhase.totalMs = elapsedMs(std::chrono::steady_clock::now() - overallStart);
        return true;
    }

    bool benchmarkPrimaryPageScan_(SwEmbeddedDb& reader,
                                   unsigned long long pageRows,
                                   PhaseResult& outPhase) const {
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
        return outPhase.rows == pageRows;
    }

    bool benchmarkPrimaryScan_(SwEmbeddedDb& reader, PhaseResult& outPhase) const {
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
        return outPhase.rows == expectedRows;
    }

    bool benchmarkIndexScan_(SwEmbeddedDb& reader, PhaseResult& outPhase) const {
        outPhase.name = "scan-index";
        const unsigned long long targetGroup =
            options_.records == 0 ? 0 : (options_.records / 2ull) % options_.indexCardinality;
        const SwByteArray startKey = workload_.groupKey(targetGroup);
        const SwByteArray endKey = workload_.groupKey(targetGroup + 1ull);
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
        outPhase.note = SwString("group=") + SwString(startKey);
        return outPhase.rows == expectedRows;
    }

    const BenchOptions& options_;
    const WorkloadModel& workload_;
    SwString dbPath_;
};

} // namespace swEmbeddedDbQtSqlBench
