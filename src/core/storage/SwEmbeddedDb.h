#pragma once

#include "SwByteArray.h"
#include "SwHash.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwSharedMemorySignal.h"
#include "SwString.h"
#include "SwThreadPool.h"
#include "platform/SwDbPlatform.h"
#include "third_party/miniz/miniz.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* kSwLogCategory_SwEmbeddedDb = "sw.core.storage.swembeddeddb";

class SwDbStatus {
public:
    enum Code {
        Ok = 0,
        NotFound,
        Busy,
        ReadOnly,
        InvalidArgument,
        NotOpen,
        IoError,
        Corruption
    };

    SwDbStatus(Code code = Ok, const SwString& message = SwString())
        : code_(code),
          message_(message) {
    }

    bool ok() const { return code_ == Ok; }
    Code code() const { return code_; }
    const SwString& message() const { return message_; }
    static SwDbStatus success() { return SwDbStatus(); }

private:
    Code code_;
    SwString message_;
};

struct SwEmbeddedDbOptions {
    SwString dbPath;
    bool readOnly{false};
    bool lazyWrite{false};
    unsigned long long commitWindowMs{20};
    unsigned long long memTableBytes{64ull * 1024ull * 1024ull};
    unsigned long long inlineBlobThresholdBytes{256ull * 1024ull};
    unsigned long long readCacheBytes{512ull * 1024ull * 1024ull};
    int maxBackgroundJobs{2};
    bool enableShmNotifications{false};
};

struct SwDbMetrics {
    unsigned long long lastVisibleSequence{0};
    unsigned long long lastDurableSequence{0};
    unsigned long long writeBatchCount{0};
    unsigned long long walFrameCount{0};
    unsigned long long walBytes{0};
    unsigned long long flushCount{0};
    unsigned long long compactionCount{0};
    unsigned long long getCount{0};
    unsigned long long snapshotCount{0};
    unsigned long long blobBytesWritten{0};
    unsigned long long blobBytesRead{0};
    unsigned long long tableCount{0};
    unsigned long long pendingWriteCount{0};
    unsigned long long walEncodeMicros{0};
    unsigned long long walAppendMicros{0};
    unsigned long long walSyncMicros{0};
    unsigned long long applyBatchMicros{0};
    unsigned long long readModelBuildMicros{0};
    unsigned long long readModelMergeMicros{0};
    unsigned long long readModelSortMicros{0};
    unsigned long long readModelPrimaryRowCount{0};
    unsigned long long readModelIndexRowCount{0};
    unsigned long long readCacheResidentBytes{0};
    unsigned long long readCacheEntryCount{0};
    unsigned long long readCacheHitCount{0};
    unsigned long long readCacheMissCount{0};
};

class SwDbWriteBatch {
public:
    struct Operation {
        enum Type { Put, Erase };
        Type type{Put};
        SwByteArray primaryKey;
        bool valueInline{true};
        SwByteArray value;
        unsigned long long blobFileId{0};
        unsigned long long blobOffset{0};
        unsigned int blobLength{0};
        unsigned int blobCrc32{0};
        unsigned int blobFlags{0};
        SwMap<SwString, SwList<SwByteArray>> secondaryKeys;
    };

    void put(const SwByteArray& primaryKey,
             const SwByteArray& value,
             const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys = SwMap<SwString, SwList<SwByteArray>>()) {
        Operation op;
        op.type = Operation::Put;
        op.primaryKey = primaryKey;
        op.valueInline = true;
        op.value = value;
        op.secondaryKeys = secondaryKeys;
        operations_.append(op);
        walEstimateBytes_ += estimateOperationWalBytes_(op);
    }

    void putBlobRef(const SwByteArray& primaryKey,
                    unsigned long long blobFileId,
                    unsigned long long blobOffset,
                    unsigned int blobLength,
                    unsigned int blobCrc32,
                    unsigned int blobFlags,
                    const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys =
                        SwMap<SwString, SwList<SwByteArray>>()) {
        Operation op;
        op.type = Operation::Put;
        op.primaryKey = primaryKey;
        op.valueInline = false;
        op.blobFileId = blobFileId;
        op.blobOffset = blobOffset;
        op.blobLength = blobLength;
        op.blobCrc32 = blobCrc32;
        op.blobFlags = blobFlags;
        op.secondaryKeys = secondaryKeys;
        operations_.append(op);
        walEstimateBytes_ += estimateOperationWalBytes_(op);
    }

    void erase(const SwByteArray& primaryKey) {
        Operation op;
        op.type = Operation::Erase;
        op.primaryKey = primaryKey;
        operations_.append(op);
        walEstimateBytes_ += estimateOperationWalBytes_(op);
    }

    const SwList<Operation>& operations() const { return operations_; }
    SwList<Operation>& mutableOperations() { return operations_; }
    unsigned long long estimatedWalBytes() const { return walEstimateBytes_; }
    bool isEmpty() const { return operations_.isEmpty(); }
    void clear() {
        operations_.clear();
        walEstimateBytes_ = 12u;
    }

private:
    static unsigned long long estimateSecondaryKeysWalBytes_(
        const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys) {
        unsigned long long bytes = 4u;
        for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = secondaryKeys.begin();
             it != secondaryKeys.end();
             ++it) {
            bytes += 4u + static_cast<unsigned long long>(it.key().size());
            bytes += 4u;
            for (std::size_t i = 0; i < it.value().size(); ++i) {
                bytes += 4u + static_cast<unsigned long long>(it.value()[i].size());
            }
        }
        return bytes;
    }

    static unsigned long long estimateOperationWalBytes_(const Operation& op) {
        unsigned long long bytes = 1u + 4u + static_cast<unsigned long long>(op.primaryKey.size());
        if (op.type == Operation::Put) {
            bytes += static_cast<unsigned long long>(op.valueInline
                         ? (4u + static_cast<unsigned long long>(op.value.size()))
                         : (8u + 8u + 4u + 4u + 4u));
            bytes += estimateSecondaryKeysWalBytes_(op.secondaryKeys);
        }
        return bytes;
    }

    SwList<Operation> operations_;
    unsigned long long walEstimateBytes_{12u};
};

struct SwDbEntry {
    SwByteArray primaryKey;
    SwByteArray secondaryKey;
    SwByteArray value;
    SwMap<SwString, SwList<SwByteArray>> secondaryKeys;
    unsigned long long sequence{0};
};

namespace swEmbeddedDbDetail {
inline std::size_t memTableBucketReserveHint_(const SwEmbeddedDbOptions& options) {
    const unsigned long long hint = options.memTableBytes / 256ull;
    return static_cast<std::size_t>(std::max<unsigned long long>(512ull,
                                                                 std::min<unsigned long long>(hint, 8192ull)));
}

struct PrimaryRecord_;
class SnapshotState_;
class LifecycleManager_;
class ManifestStore_;
class WalManager_;
class WriteCoordinator_;
class Materializer_;
class ReadModel_;
class TableWriter_;
class BlobGcManager_;
class CompactionManager_;
class ReadCacheManager_;
class IteratorState_ {
public:
    virtual ~IteratorState_() {}
    virtual bool next(SwDbEntry& outEntry) = 0;
};
bool snapshotLookupPrimary_(const std::shared_ptr<SnapshotState_>& snapshot,
                            const SwByteArray& primaryKey,
                            PrimaryRecord_& outRecord,
                            bool resolveBlob);
std::shared_ptr<IteratorState_> createPrimaryIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                                            const SwByteArray& startKey,
                                                            const SwByteArray& endKey);
std::shared_ptr<IteratorState_> createIndexIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                                          const SwString& indexName,
                                                          const SwByteArray& startSecondaryKey,
                                                          const SwByteArray& endSecondaryKey);
}

class SwDbIterator {
public:
    bool isValid() const { return valid_; }
    void next();
    void rewind();
    std::size_t size() const;
    const SwDbEntry& current() const { return current_; }

private:
    friend class SwDbSnapshot;
    friend class SwEmbeddedDb;
    void prime_();
    std::shared_ptr<swEmbeddedDbDetail::IteratorState_> state_;
    std::function<std::shared_ptr<swEmbeddedDbDetail::IteratorState_>()> factory_;
    SwDbEntry current_;
    bool valid_{false};
    mutable bool sizeKnown_{false};
    mutable std::size_t sizeCache_{0};
};

namespace swEmbeddedDbDetail {
struct BlobRef_ {
    bool valid{false};
    unsigned long long fileId{0};
    unsigned long long offset{0};
    unsigned int length{0};
    unsigned int crc32{0};
    unsigned int flags{0};
};

struct PrimaryRecord_ {
    bool deleted{false};
    unsigned long long sequence{0};
    bool inlineValue{true};
    SwByteArray value;
    BlobRef_ blobRef;
    SwMap<SwString, SwList<SwByteArray>> secondaryKeys;
};

struct SecondaryEntry_ {
    bool deleted{false};
    unsigned long long sequence{0};
};

struct ByteArrayHash_ {
    std::size_t operator()(const SwByteArray& value) const;
};

typedef SwHash<SwByteArray, PrimaryRecord_, ByteArrayHash_> PrimaryMemStore_;
typedef SwHash<SwByteArray, SecondaryEntry_, ByteArrayHash_> SecondaryMemStore_;

struct MemTable_ {
    unsigned long long walId{0};
    unsigned long long blobFileId{0};
    unsigned long long minSeq{0};
    unsigned long long maxSeq{0};
    unsigned long long approximateBytes{0};
    PrimaryMemStore_ primary;
    SwHash<SwString, SecondaryMemStore_> secondary;

    const SwList<SwByteArray>& orderedPrimaryKeys() const;
    const SwList<SwByteArray>& orderedSecondaryKeys(const SwString& indexName) const;
    void invalidatePrimaryOrder();
    void invalidateSecondaryOrder(const SwString& indexName);
    void invalidateAllSecondaryOrders();

private:
    static const SwList<SwByteArray>& emptyOrderedKeys_();

    mutable bool primaryOrderDirty_{true};
    mutable SwList<SwByteArray> orderedPrimaryKeys_;
    mutable SwHash<SwString, SwList<SwByteArray>> orderedSecondaryKeys_;
    mutable SwHash<SwString, bool> secondaryOrderDirty_;
};

struct TableMeta_ {
    int kind{1};
    int level{0};
    unsigned long long fileId{0};
    unsigned long long minSequence{0};
    unsigned long long maxSequence{0};
    unsigned long long recordCount{0};
    unsigned long long blobFileId{0};
    SwString fileName;
    SwString indexName;
};

struct Manifest_ {
    unsigned long long manifestId{0};
    unsigned long long maxSequence{0};
    unsigned long long replayFromWalId{1};
    unsigned long long activeWalId{1};
    unsigned long long nextWalId{2};
    unsigned long long nextTableId{1};
    SwList<TableMeta_> tables;
};

struct TableRecord_ {
    SwByteArray userKey;
    unsigned long long sequence{0};
    unsigned int flags{0};
    SwByteArray payload;
};

struct TableRecordView_ {
    const char* userKeyData{nullptr};
    unsigned int userKeyBytes{0};
    unsigned long long sequence{0};
    unsigned int flags{0};
    const char* payloadData{nullptr};
    unsigned int payloadBytes{0};
};

struct BlockIndexEntry_ {
    SwByteArray firstKey;
    unsigned long long offset{0};
    unsigned int bytes{0};
};

class TableHandle_;
class SnapshotState_;
class IteratorState_;
}

class SwDbSnapshot {
public:
    SwDbStatus get(const SwByteArray& primaryKey,
                   SwByteArray* valueOut,
                   SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut = nullptr) const;
    SwDbIterator scanPrimary(const SwByteArray& startKey = SwByteArray(),
                             const SwByteArray& endKey = SwByteArray()) const;
    SwDbIterator scanIndex(const SwString& indexName,
                           const SwByteArray& startSecondaryKey = SwByteArray(),
                           const SwByteArray& endSecondaryKey = SwByteArray()) const;
    bool isValid() const { return valid_; }
    unsigned long long visibleSequence() const { return visibleSequence_; }

private:
    friend class SwEmbeddedDb;
    friend class swEmbeddedDbDetail::LifecycleManager_;
    bool valid_{false};
    unsigned long long visibleSequence_{0};
    std::shared_ptr<swEmbeddedDbDetail::SnapshotState_> state_;
};

class SwEmbeddedDb {
public:
    SwEmbeddedDb();
    ~SwEmbeddedDb();

    SwDbStatus open(const SwEmbeddedDbOptions& options);
    void close();
    SwDbStatus get(const SwByteArray& primaryKey,
                   SwByteArray* valueOut,
                   SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut = nullptr);
    SwDbStatus write(const SwDbWriteBatch& batch);
    SwDbStatus write(SwDbWriteBatch&& batch);
    SwDbStatus sync();
    SwDbIterator scanPrimary(const SwByteArray& startKey = SwByteArray(),
                             const SwByteArray& endKey = SwByteArray());
    SwDbIterator scanIndex(const SwString& indexName,
                           const SwByteArray& startSecondaryKey = SwByteArray(),
                           const SwByteArray& endSecondaryKey = SwByteArray());
    SwDbStatus refresh();
    SwDbSnapshot createSnapshot();
    SwDbMetrics metricsSnapshot() const;

private:
    friend class swEmbeddedDbDetail::LifecycleManager_;
    friend class swEmbeddedDbDetail::ManifestStore_;
    friend class swEmbeddedDbDetail::WalManager_;
    friend class swEmbeddedDbDetail::WriteCoordinator_;
    friend class swEmbeddedDbDetail::Materializer_;
    friend class swEmbeddedDbDetail::TableWriter_;
    friend class swEmbeddedDbDetail::BlobGcManager_;
    friend class swEmbeddedDbDetail::CompactionManager_;
    struct WriteRequest_;
    void resetState_();
    void drainPendingWrites_();
    SwDbStatus loadFromDisk_(bool createIfMissing);
    void normalizeIdsLocked_();
    SwDbStatus loadManifestFile_(const SwString& fileName);
    SwDbStatus persistManifestLocked_();
    SwDbStatus persistOptions_();
    SwDbStatus openActiveWal_();
    SwDbStatus openTablesLocked_();
    SwDbStatus replayWalLocked_();
    SwDbStatus replayWalFileLocked_(unsigned long long walId, const SwString& walPath);
    static bool decodeWalPayload_(const SwByteArray& payload, unsigned long long& sequence, SwDbWriteBatch& outBatch);
    static SwByteArray encodeWalPayload_(unsigned long long sequence, const SwDbWriteBatch& batch);
    SwDbStatus syncInternal_(bool allowClosed);
    SwDbStatus drainWriteRequests_(SwList<std::shared_ptr<WriteRequest_>>& groupOut,
                                   unsigned long long* targetSequenceOut,
                                   bool* stopOut);
    SwDbStatus commitWriteRequests_(const SwList<std::shared_ptr<WriteRequest_>>& group,
                                    unsigned long long targetSequence);
    void finalizeWriteRequests_(const SwList<std::shared_ptr<WriteRequest_>>& group,
                                const SwDbStatus& status,
                                unsigned long long durableSequence);
    void startWriteService_();
    void stopWriteService_();
    void wakeWriteService_();
    void writeServiceLoop_();
    void applyBatchLocked_(unsigned long long sequence, const SwDbWriteBatch& batch);
    void emitSecondaryTombstonesLocked_(const SwByteArray& primaryKey,
                                        const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys,
                                        unsigned long long sequence);
    void emitSecondaryUpsertsLocked_(const SwByteArray& primaryKey,
                                     const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys,
                                     unsigned long long sequence);
    bool lookupPrimaryLocked_(const SwByteArray& primaryKey,
                              swEmbeddedDbDetail::PrimaryRecord_& outRecord,
                              bool resolveValue);
    bool resolveValueLocked_(swEmbeddedDbDetail::PrimaryRecord_& record);
    bool materializePrimaryLocked_(SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>& mergedPrimary);
    bool buildReadModelLocked_(std::shared_ptr<swEmbeddedDbDetail::ReadModel_>& outReadModel);
    void prepareMutableMemTableLocked_();
    void rotateMutableToImmutableLocked_();
    void scheduleFlushLocked_();
    void flushBackground_();
    void flushAllSync_();
    SwDbStatus flushMemTable_(const swEmbeddedDbDetail::MemTable_& mem, int level);
    bool externalizeBlobValue_(unsigned long long blobFileId, swEmbeddedDbDetail::PrimaryRecord_& record);
    bool externalizeBlobValueToFile_(swDbPlatform::RandomAccessFile& blobFile,
                                     unsigned long long blobFileId,
                                     swEmbeddedDbDetail::PrimaryRecord_& record);
    bool blobFileHasData_(unsigned long long blobFileId) const;
    SwDbStatus writeTableFile_(const swEmbeddedDbDetail::TableMeta_& meta,
                               const SwList<swEmbeddedDbDetail::TableRecord_>& records);
    SwDbStatus flushBlock_(swDbPlatform::RandomAccessFile& file,
                           unsigned long long& offset,
                           const SwByteArray& firstKey,
                           const SwByteArray& blockPayload,
                           SwList<swEmbeddedDbDetail::BlockIndexEntry_>& blocks);
    void cleanupCoveredWalFilesLocked_();
    void compactL0Locked_();
    void runL0Compaction_(SwList<swEmbeddedDbDetail::TableMeta_> primaryL0,
                          SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexL0);
    void scheduleBlobGcLocked_();
    void runBlobGc_();
    void removeTablesLocked_(const SwList<swEmbeddedDbDetail::TableMeta_>& toRemove);
    void rebuildTableCachesLocked_();
    void rebuildReadOnlySnapshotLocked_();
    void maybeRefreshFromNotifications_();
    void setupShmNotifications_();
    void teardownShmNotifications_();
    void publishShmNotificationLocked_();
    SwString notificationObjectName_() const;
    bool resolveValueThreadSafe_(swEmbeddedDbDetail::PrimaryRecord_& record);
    unsigned long long nextTableIdThreadSafe_();
    bool externalizeBlobValueThreadSafe_(unsigned long long blobFileId,
                                         swEmbeddedDbDetail::PrimaryRecord_& record);
    static std::string trimText_(const std::string& input);

    mutable std::mutex mutex_;
    std::condition_variable commitCv_;
    std::atomic<bool> opened_{false};
    SwEmbeddedDbOptions options_;
    SwString dbPath_;
    SwString walDir_;
    SwString tableDir_;
    SwString blobDir_;
    SwString tmpDir_;
    swEmbeddedDbDetail::Manifest_ manifest_;
    swEmbeddedDbDetail::MemTable_ mutable_;
    SwList<swEmbeddedDbDetail::MemTable_> immutables_;
    SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> tableHandles_;
    std::vector<std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> primaryTableHandlesNewestFirst_;
    SwHash<SwString, std::vector<std::shared_ptr<swEmbeddedDbDetail::TableHandle_>>> indexTableHandlesNewestFirst_;
    std::shared_ptr<swEmbeddedDbDetail::SnapshotState_> readOnlySnapshotState_;
    swDbPlatform::FileLock writerLock_;
    swDbPlatform::RandomAccessFile activeWalFile_;
    std::deque<std::shared_ptr<WriteRequest_>> pendingWrites_;
    std::condition_variable writeServiceCv_;
    std::thread writeServiceThread_;
    bool writeServiceRunning_{false};
    bool writeServiceStop_{false};
    bool writeServiceStopped_{false};
    bool writeServiceFlushRequested_{false};
    bool flushScheduled_{false};
    bool compactionScheduled_{false};
    bool blobGcScheduled_{false};
    bool closing_{false};
    unsigned long long lastVisibleSequence_{0};
    unsigned long long lastDurableSequence_{0};
    unsigned long long nextSequence_{1};
    SwByteArray maxKnownPrimaryKey_;
    SwByteArray walScratch_;
    SwDbStatus backgroundWriteError_;
    SwDbMetrics metrics_;
    SwThreadPool backgroundPool_;
    std::shared_ptr<swEmbeddedDbDetail::ReadCacheManager_> readCacheManager_;
    std::unique_ptr<sw::ipc::Registry> shmRegistry_;
    std::unique_ptr<sw::ipc::Signal<unsigned long long, unsigned long long> > shmNotification_;
    sw::ipc::Signal<unsigned long long, unsigned long long>::Subscription shmNotificationSub_;
    std::atomic<bool> shmRefreshHint_{false};
};

#include "embeddeddb/SwEmbeddedDbCodec.h"
#include "embeddeddb/SwEmbeddedDbMemTable.h"
#include "embeddeddb/SwEmbeddedDbBloomFilter.h"
#include "embeddeddb/SwEmbeddedDbReadCache.h"
#include "embeddeddb/SwEmbeddedDbTable.h"
#include "embeddeddb/SwEmbeddedDbSnapshotState.h"
#include "embeddeddb/SwEmbeddedDbReadModel.h"
#include "embeddeddb/SwEmbeddedDbReadModelIterators.h"
#include "embeddeddb/SwEmbeddedDbSnapshotFacade.h"
#include "embeddeddb/SwEmbeddedDbManifest.h"
#include "embeddeddb/SwEmbeddedDbWal.h"
#include "embeddeddb/SwEmbeddedDbWriteCoordinator.h"
#include "embeddeddb/SwEmbeddedDbLifecycle.h"
#include "embeddeddb/SwEmbeddedDbLookup.h"
#include "embeddeddb/SwEmbeddedDbMaterializer.h"
#include "embeddeddb/SwEmbeddedDbBlobGcWorker.h"
#include "embeddeddb/SwEmbeddedDbBlobStore.h"
#include "embeddeddb/SwEmbeddedDbFlushScheduler.h"
#include "embeddeddb/SwEmbeddedDbNotifications.h"
#include "embeddeddb/SwEmbeddedDbTableWriter.h"
#include "embeddeddb/SwEmbeddedDbMemtableFlusher.h"
#include "embeddeddb/SwEmbeddedDbCompactionWorker.h"
#include "embeddeddb/SwEmbeddedDbMaintenanceHelpers.h"
