struct SwEmbeddedDb::WriteRequest_ {
    SwDbWriteBatch batch;
    SwByteArray walFrame;
    unsigned long long sequence{0};
    bool waitForDurable{true};
    bool done{false};
    SwDbStatus status;
    std::mutex mutex;
    std::condition_variable cv;
};

namespace swEmbeddedDbDetail {

class WriteCoordinator_ {
public:
    explicit WriteCoordinator_(SwEmbeddedDb& db)
        : db_(db) {
    }

    SwDbStatus write(const SwDbWriteBatch& batch) {
        std::shared_ptr<SwEmbeddedDb::WriteRequest_> request(new SwEmbeddedDb::WriteRequest_());
        request->batch = batch;
        return submitRequest(request);
    }

    SwDbStatus writeMutable(SwDbWriteBatch& batch) {
        std::shared_ptr<SwEmbeddedDb::WriteRequest_> request(new SwEmbeddedDb::WriteRequest_());
        request->batch = std::move(batch);
        batch.clear();
        return submitRequest(request);
    }

private:
    SwDbStatus submitRequest(const std::shared_ptr<SwEmbeddedDb::WriteRequest_>& request) {
        if (!db_.opened_.load(std::memory_order_acquire)) {
            return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
        }
        if (db_.options_.readOnly) {
            return SwDbStatus(SwDbStatus::ReadOnly, "database opened read-only");
        }
        if (!request || request->batch.isEmpty()) {
            return SwDbStatus::success();
        }

        const SwDbStatus prepareStatus = prepareBatchForCommit(request->batch);
        if (!prepareStatus.ok()) {
            return prepareStatus;
        }

        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_) {
                return SwDbStatus(SwDbStatus::NotOpen, "database is closing");
            }
            if (!db_.backgroundWriteError_.ok()) {
                return db_.backgroundWriteError_;
            }
            request->sequence = db_.nextSequence_++;
            request->waitForDurable = !db_.options_.lazyWrite;
        }

        const std::chrono::steady_clock::time_point encodeStart = std::chrono::steady_clock::now();
        request->walFrame.clear();
        request->walFrame.reserve(static_cast<std::size_t>(estimateWalBytes(request->batch) + 32u));
        swEmbeddedDbDetail::WalManager_::appendWalFrame(request->walFrame, request->sequence, request->batch);
        const unsigned long long walEncodeMicros = static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - encodeStart).count());

        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_) {
                return SwDbStatus(SwDbStatus::NotOpen, "database is closing");
            }
            if (!db_.backgroundWriteError_.ok()) {
                return db_.backgroundWriteError_;
            }

            const std::chrono::steady_clock::time_point applyStart = std::chrono::steady_clock::now();
            applyBatchLockedMutable(request->sequence, request->batch);
            db_.metrics_.writeBatchCount += 1;
            db_.metrics_.walEncodeMicros += walEncodeMicros;
            db_.metrics_.applyBatchMicros += static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - applyStart).count());
            db_.pendingWrites_.push_back(request);
            db_.metrics_.pendingWriteCount = static_cast<unsigned long long>(db_.pendingWrites_.size());

            if (db_.mutable_.approximateBytes >= db_.options_.memTableBytes) {
                db_.rotateMutableToImmutableLocked_();
                db_.scheduleFlushLocked_();
            }
        }

        db_.wakeWriteService_();
        if (!request->waitForDurable) {
            return SwDbStatus::success();
        }

        std::unique_lock<std::mutex> waitLock(request->mutex);
        request->cv.wait(waitLock, [&request]() { return request->done; });
        return request->status;
    }

    SwDbStatus prepareBatchForCommit(SwDbWriteBatch& batch) {
        (void)batch;
        return SwDbStatus::success();
    }

    static unsigned long long estimateWalBytes(const SwDbWriteBatch& batch) {
        return batch.estimatedWalBytes();
    }

    static std::size_t initialMemTableBucketReserve_(const SwEmbeddedDb& db) {
        return swEmbeddedDbDetail::memTableBucketReserveHint_(db.options_);
    }

public:
    void applyBatchLocked(unsigned long long sequence, const SwDbWriteBatch& batch) {
        db_.mutable_.walId = db_.manifest_.activeWalId;
        if (db_.mutable_.minSeq == 0 || sequence < db_.mutable_.minSeq) {
            db_.mutable_.minSeq = sequence;
        }
        db_.mutable_.maxSeq = std::max(db_.mutable_.maxSeq, sequence);
        db_.mutable_.primary.reserve(std::max(db_.mutable_.primary.size() + batch.operations().size(),
                                              initialMemTableBucketReserve_(db_)));
        SwHash<SwString, unsigned long long> secondaryReserveGrowth;
        SwHash<SwString, bool> dirtySecondaryIndexes;
        secondaryReserveGrowth.reserve(batch.operations().size());
        dirtySecondaryIndexes.reserve(batch.operations().size());

        for (std::size_t i = 0; i < batch.operations().size(); ++i) {
            const SwDbWriteBatch::Operation& op = batch.operations()[i];
            if (op.type != SwDbWriteBatch::Operation::Put) {
                continue;
            }
            for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = op.secondaryKeys.begin();
                 it != op.secondaryKeys.end();
                 ++it) {
                secondaryReserveGrowth[it.key()] += static_cast<unsigned long long>(it.value().size());
                dirtySecondaryIndexes[it.key()] = true;
            }
        }
        for (SwHash<SwString, unsigned long long>::const_iterator it = secondaryReserveGrowth.begin();
             it != secondaryReserveGrowth.end();
             ++it) {
            SwHash<SwString, swEmbeddedDbDetail::SecondaryMemStore_>::iterator bucketIt =
                db_.mutable_.secondary.find(it->first);
            if (bucketIt == db_.mutable_.secondary.end()) {
                db_.mutable_.secondary.insert(it->first, swEmbeddedDbDetail::SecondaryMemStore_());
                bucketIt = db_.mutable_.secondary.find(it->first);
            }
            bucketIt->second.reserve(std::max(bucketIt->second.size() + static_cast<std::size_t>(it->second),
                                              initialMemTableBucketReserve_(db_)));
        }

        bool primaryOrderDirty = false;

        for (std::size_t i = 0; i < batch.operations().size(); ++i) {
            const SwDbWriteBatch::Operation& op = batch.operations()[i];
            swEmbeddedDbDetail::PrimaryRecord_ previous;
            bool hadPrevious = false;
            const bool knownAbsent =
                !db_.maxKnownPrimaryKey_.isEmpty() && db_.maxKnownPrimaryKey_ < op.primaryKey;
            swEmbeddedDbDetail::PrimaryMemStore_::iterator mutableIt = db_.mutable_.primary.end();
            if (!knownAbsent) {
                mutableIt = db_.mutable_.primary.find(op.primaryKey);
                if (mutableIt != db_.mutable_.primary.end()) {
                    previous = mutableIt->second;
                    hadPrevious = !previous.deleted;
                } else {
                    hadPrevious = db_.lookupPrimaryLocked_(op.primaryKey, previous, false);
                }
            }
            if (hadPrevious && !previous.deleted) {
                for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = previous.secondaryKeys.begin();
                     it != previous.secondaryKeys.end();
                     ++it) {
                    dirtySecondaryIndexes[it.key()] = true;
                }
                emitSecondaryTombstonesLocked(op.primaryKey, previous.secondaryKeys, sequence);
            }

            if (op.type == SwDbWriteBatch::Operation::Erase) {
                const unsigned long long primaryKeyBytes = static_cast<unsigned long long>(op.primaryKey.size());
                if (db_.maxKnownPrimaryKey_.isEmpty() || db_.maxKnownPrimaryKey_ < op.primaryKey) {
                    db_.maxKnownPrimaryKey_ = op.primaryKey;
                }
                swEmbeddedDbDetail::PrimaryRecord_ record;
                record.deleted = true;
                record.sequence = sequence;
                if (mutableIt != db_.mutable_.primary.end()) {
                    mutableIt->second = record;
                } else {
                    db_.mutable_.primary.insert(std::move(op.primaryKey), std::move(record));
                }
                primaryOrderDirty = true;
                db_.mutable_.approximateBytes += primaryKeyBytes + 24u;
                continue;
            }

            swEmbeddedDbDetail::PrimaryRecord_ record;
            record.deleted = false;
            record.sequence = sequence;
            record.inlineValue = op.valueInline;
            if (op.valueInline) {
                record.value = op.value;
            } else {
                record.blobRef.valid = true;
                record.blobRef.fileId = op.blobFileId;
                record.blobRef.offset = op.blobOffset;
                record.blobRef.length = op.blobLength;
                record.blobRef.crc32 = op.blobCrc32;
                record.blobRef.flags = op.blobFlags;
                if (db_.mutable_.blobFileId == 0) {
                    db_.mutable_.blobFileId = op.blobFileId;
                }
            }
            record.secondaryKeys = op.secondaryKeys;
            if (mutableIt != db_.mutable_.primary.end()) {
                mutableIt->second = record;
            } else {
                db_.mutable_.primary.insert(op.primaryKey, record);
            }
            primaryOrderDirty = true;
            db_.mutable_.approximateBytes += static_cast<unsigned long long>(op.primaryKey.size()) +
                                             static_cast<unsigned long long>(op.valueInline ? op.value.size() : 24u) + 64u;
            emitSecondaryUpsertsLocked(op.primaryKey, op.secondaryKeys, sequence);
            if (db_.maxKnownPrimaryKey_.isEmpty() || db_.maxKnownPrimaryKey_ < op.primaryKey) {
                db_.maxKnownPrimaryKey_ = op.primaryKey;
            }
        }
        if (primaryOrderDirty) {
            db_.mutable_.invalidatePrimaryOrder();
        }
        for (SwHash<SwString, bool>::const_iterator it = dirtySecondaryIndexes.begin(); it != dirtySecondaryIndexes.end();
             ++it) {
            if (it->second) {
                db_.mutable_.invalidateSecondaryOrder(it->first);
            }
        }
        db_.lastVisibleSequence_ = std::max(db_.lastVisibleSequence_, sequence);
    }

    void applyBatchLockedMutable(unsigned long long sequence, SwDbWriteBatch& batch) {
        db_.mutable_.walId = db_.manifest_.activeWalId;
        if (db_.mutable_.minSeq == 0 || sequence < db_.mutable_.minSeq) {
            db_.mutable_.minSeq = sequence;
        }
        db_.mutable_.maxSeq = std::max(db_.mutable_.maxSeq, sequence);
        db_.mutable_.primary.reserve(std::max(db_.mutable_.primary.size() + batch.operations().size(),
                                              initialMemTableBucketReserve_(db_)));
        SwHash<SwString, unsigned long long> secondaryReserveGrowth;
        SwHash<SwString, bool> dirtySecondaryIndexes;
        secondaryReserveGrowth.reserve(batch.operations().size());
        dirtySecondaryIndexes.reserve(batch.operations().size());

        for (std::size_t i = 0; i < batch.operations().size(); ++i) {
            const SwDbWriteBatch::Operation& op = batch.operations()[i];
            if (op.type != SwDbWriteBatch::Operation::Put) {
                continue;
            }
            for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = op.secondaryKeys.begin();
                 it != op.secondaryKeys.end();
                 ++it) {
                secondaryReserveGrowth[it.key()] += static_cast<unsigned long long>(it.value().size());
                dirtySecondaryIndexes[it.key()] = true;
            }
        }
        for (SwHash<SwString, unsigned long long>::const_iterator it = secondaryReserveGrowth.begin();
             it != secondaryReserveGrowth.end();
             ++it) {
            SwHash<SwString, swEmbeddedDbDetail::SecondaryMemStore_>::iterator bucketIt =
                db_.mutable_.secondary.find(it->first);
            if (bucketIt == db_.mutable_.secondary.end()) {
                db_.mutable_.secondary.insert(it->first, swEmbeddedDbDetail::SecondaryMemStore_());
                bucketIt = db_.mutable_.secondary.find(it->first);
            }
            bucketIt->second.reserve(std::max(bucketIt->second.size() + static_cast<std::size_t>(it->second),
                                              initialMemTableBucketReserve_(db_)));
        }

        bool primaryOrderDirty = false;
        SwList<SwDbWriteBatch::Operation>& operations = batch.mutableOperations();
        for (std::size_t i = 0; i < operations.size(); ++i) {
            SwDbWriteBatch::Operation& op = operations[i];
            swEmbeddedDbDetail::PrimaryRecord_ previous;
            bool hadPrevious = false;
            const bool knownAbsent =
                !db_.maxKnownPrimaryKey_.isEmpty() && db_.maxKnownPrimaryKey_ < op.primaryKey;
            swEmbeddedDbDetail::PrimaryMemStore_::iterator mutableIt = db_.mutable_.primary.end();
            if (!knownAbsent) {
                mutableIt = db_.mutable_.primary.find(op.primaryKey);
                if (mutableIt != db_.mutable_.primary.end()) {
                    previous = mutableIt->second;
                    hadPrevious = !previous.deleted;
                } else {
                    hadPrevious = db_.lookupPrimaryLocked_(op.primaryKey, previous, false);
                }
            }
            if (hadPrevious && !previous.deleted) {
                for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = previous.secondaryKeys.begin();
                     it != previous.secondaryKeys.end();
                     ++it) {
                    dirtySecondaryIndexes[it.key()] = true;
                }
                emitSecondaryTombstonesLocked(op.primaryKey, previous.secondaryKeys, sequence);
            }

            if (op.type == SwDbWriteBatch::Operation::Erase) {
                swEmbeddedDbDetail::PrimaryRecord_ record;
                record.deleted = true;
                record.sequence = sequence;
                if (mutableIt != db_.mutable_.primary.end()) {
                    mutableIt->second = record;
                } else {
                    db_.mutable_.primary.insert(op.primaryKey, record);
                }
                primaryOrderDirty = true;
                db_.mutable_.approximateBytes += static_cast<unsigned long long>(op.primaryKey.size()) + 24u;
                if (db_.maxKnownPrimaryKey_.isEmpty() || db_.maxKnownPrimaryKey_ < op.primaryKey) {
                    db_.maxKnownPrimaryKey_ = op.primaryKey;
                }
                continue;
            }

            const unsigned long long primaryKeyBytes = static_cast<unsigned long long>(op.primaryKey.size());
            const unsigned long long valueBytes =
                static_cast<unsigned long long>(op.valueInline ? op.value.size() : op.blobLength);
            emitSecondaryUpsertsLocked(op.primaryKey, op.secondaryKeys, sequence);
            if (db_.maxKnownPrimaryKey_.isEmpty() || db_.maxKnownPrimaryKey_ < op.primaryKey) {
                db_.maxKnownPrimaryKey_ = op.primaryKey;
            }

            swEmbeddedDbDetail::PrimaryRecord_ record;
            record.deleted = false;
            record.sequence = sequence;
            record.inlineValue = op.valueInline;
            if (op.valueInline) {
                record.value = std::move(op.value);
            } else {
                record.blobRef.valid = true;
                record.blobRef.fileId = op.blobFileId;
                record.blobRef.offset = op.blobOffset;
                record.blobRef.length = op.blobLength;
                record.blobRef.crc32 = op.blobCrc32;
                record.blobRef.flags = op.blobFlags;
                if (db_.mutable_.blobFileId == 0) {
                    db_.mutable_.blobFileId = op.blobFileId;
                }
            }
            record.secondaryKeys = std::move(op.secondaryKeys);
            if (mutableIt != db_.mutable_.primary.end()) {
                mutableIt->second = std::move(record);
            } else {
                db_.mutable_.primary.insert(std::move(op.primaryKey), std::move(record));
            }
            primaryOrderDirty = true;
            db_.mutable_.approximateBytes += primaryKeyBytes +
                                             static_cast<unsigned long long>(op.valueInline ? valueBytes : 24u) + 64u;
        }
        if (primaryOrderDirty) {
            db_.mutable_.invalidatePrimaryOrder();
        }
        for (SwHash<SwString, bool>::const_iterator it = dirtySecondaryIndexes.begin(); it != dirtySecondaryIndexes.end();
             ++it) {
            if (it->second) {
                db_.mutable_.invalidateSecondaryOrder(it->first);
            }
        }
        db_.lastVisibleSequence_ = std::max(db_.lastVisibleSequence_, sequence);
    }

    void emitSecondaryTombstonesLocked(const SwByteArray& primaryKey,
                                       const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys,
                                       unsigned long long sequence) {
        for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = secondaryKeys.begin(); it != secondaryKeys.end();
             ++it) {
            SwHash<SwString, swEmbeddedDbDetail::SecondaryMemStore_>::iterator bucketIt =
                db_.mutable_.secondary.find(it.key());
            if (bucketIt == db_.mutable_.secondary.end()) {
                db_.mutable_.secondary.insert(it.key(), swEmbeddedDbDetail::SecondaryMemStore_());
                bucketIt = db_.mutable_.secondary.find(it.key());
            }
            swEmbeddedDbDetail::SecondaryMemStore_& bucket = bucketIt->second;
            for (std::size_t i = 0; i < it.value().size(); ++i) {
                swEmbeddedDbDetail::SecondaryEntry_ entry;
                entry.deleted = true;
                entry.sequence = sequence;
                const SwByteArray compositeKey =
                    swEmbeddedDbDetail::encodeIndexCompositeKey_(it.value()[i], primaryKey);
                const swEmbeddedDbDetail::SecondaryMemStore_::iterator entryIt = bucket.find(compositeKey);
                if (entryIt != bucket.end()) {
                    entryIt->second = entry;
                } else {
                    bucket.insert(compositeKey, entry);
                }
                db_.mutable_.approximateBytes +=
                    static_cast<unsigned long long>(primaryKey.size() + it.value()[i].size()) + 32u;
            }
        }
    }

    void emitSecondaryUpsertsLocked(const SwByteArray& primaryKey,
                                    const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys,
                                    unsigned long long sequence) {
        for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = secondaryKeys.begin(); it != secondaryKeys.end();
             ++it) {
            SwHash<SwString, swEmbeddedDbDetail::SecondaryMemStore_>::iterator bucketIt =
                db_.mutable_.secondary.find(it.key());
            if (bucketIt == db_.mutable_.secondary.end()) {
                db_.mutable_.secondary.insert(it.key(), swEmbeddedDbDetail::SecondaryMemStore_());
                bucketIt = db_.mutable_.secondary.find(it.key());
            }
            swEmbeddedDbDetail::SecondaryMemStore_& bucket = bucketIt->second;
            for (std::size_t i = 0; i < it.value().size(); ++i) {
                swEmbeddedDbDetail::SecondaryEntry_ entry;
                entry.deleted = false;
                entry.sequence = sequence;
                const SwByteArray compositeKey =
                    swEmbeddedDbDetail::encodeIndexCompositeKey_(it.value()[i], primaryKey);
                const swEmbeddedDbDetail::SecondaryMemStore_::iterator entryIt = bucket.find(compositeKey);
                if (entryIt != bucket.end()) {
                    entryIt->second = entry;
                } else {
                    bucket.insert(compositeKey, entry);
                }
                db_.mutable_.approximateBytes +=
                    static_cast<unsigned long long>(primaryKey.size() + it.value()[i].size()) + 32u;
            }
        }
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline void SwEmbeddedDb::startWriteService_() {
    stopWriteService_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        writeServiceStop_ = false;
        writeServiceStopped_ = false;
        writeServiceFlushRequested_ = false;
        writeServiceRunning_ = true;
    }
    writeServiceThread_ = std::thread([this]() { this->writeServiceLoop_(); });
}

inline void SwEmbeddedDb::stopWriteService_() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!writeServiceRunning_ && !writeServiceThread_.joinable()) {
            writeServiceStop_ = false;
            writeServiceStopped_ = true;
            writeServiceFlushRequested_ = false;
            return;
        }
        writeServiceStop_ = true;
    }
    writeServiceCv_.notify_all();
    if (writeServiceThread_.joinable()) {
        writeServiceThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        writeServiceRunning_ = false;
        writeServiceStop_ = false;
        writeServiceStopped_ = true;
        writeServiceFlushRequested_ = false;
    }
    commitCv_.notify_all();
}

inline void SwEmbeddedDb::wakeWriteService_() {
    writeServiceCv_.notify_one();
}

inline SwDbStatus SwEmbeddedDb::drainWriteRequests_(SwList<std::shared_ptr<WriteRequest_>>& groupOut,
                                                    unsigned long long* targetSequenceOut,
                                                    bool* stopOut) {
    if (targetSequenceOut) {
        *targetSequenceOut = 0;
    }
    if (stopOut) {
        *stopOut = false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    writeServiceCv_.wait(lock, [this]() {
        return writeServiceStop_ || !pendingWrites_.empty();
    });

    if (pendingWrites_.empty() && writeServiceStop_) {
        if (stopOut) {
            *stopOut = true;
        }
        return SwDbStatus::success();
    }

    if (options_.commitWindowMs > 0 && !pendingWrites_.empty() && !writeServiceStop_) {
        writeServiceCv_.wait_for(lock,
                                 std::chrono::milliseconds(options_.commitWindowMs),
                                 [this]() { return writeServiceStop_ || writeServiceFlushRequested_; });
    }

    while (!pendingWrites_.empty()) {
        groupOut.append(pendingWrites_.front());
        pendingWrites_.pop_front();
    }
    writeServiceFlushRequested_ = false;
    metrics_.pendingWriteCount = static_cast<unsigned long long>(pendingWrites_.size());
    if (!groupOut.isEmpty() && targetSequenceOut) {
        *targetSequenceOut = groupOut.last()->sequence;
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwEmbeddedDb::commitWriteRequests_(const SwList<std::shared_ptr<WriteRequest_>>& group,
                                                     unsigned long long targetSequence) {
    if (group.isEmpty()) {
        return SwDbStatus::success();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!backgroundWriteError_.ok()) {
            return backgroundWriteError_;
        }
        if (!activeWalFile_.isOpen()) {
            const SwDbStatus openStatus = openActiveWal_();
            if (!openStatus.ok()) {
                return openStatus;
            }
        }
    }

    walScratch_.clear();
    unsigned long long totalWalBytes = 0;
    for (std::size_t i = 0; i < group.size(); ++i) {
        totalWalBytes += static_cast<unsigned long long>(group[i]->walFrame.size());
    }
    walScratch_.reserve(static_cast<std::size_t>(totalWalBytes));
    for (std::size_t i = 0; i < group.size(); ++i) {
        walScratch_ += group[i]->walFrame;
    }

    SwString error;
    const std::chrono::steady_clock::time_point appendStart = std::chrono::steady_clock::now();
    if (!walScratch_.isEmpty() && !activeWalFile_.append(walScratch_.constData(), walScratch_.size(), nullptr, &error)) {
        return SwDbStatus(SwDbStatus::IoError, error);
    }
    const unsigned long long walAppendMicros = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - appendStart).count());

    const std::chrono::steady_clock::time_point syncStart = std::chrono::steady_clock::now();
    if (!activeWalFile_.sync(&error)) {
        return SwDbStatus(SwDbStatus::IoError, error);
    }
    const unsigned long long walSyncMicros = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - syncStart).count());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastDurableSequence_ = std::max(lastDurableSequence_, targetSequence);
        metrics_.lastDurableSequence = lastDurableSequence_;
        metrics_.walAppendMicros += walAppendMicros;
        metrics_.walSyncMicros += walSyncMicros;
        metrics_.walFrameCount += static_cast<unsigned long long>(group.size());
        metrics_.walBytes += static_cast<unsigned long long>(walScratch_.size());
        publishShmNotificationLocked_();
    }
    return SwDbStatus::success();
}

inline void SwEmbeddedDb::finalizeWriteRequests_(const SwList<std::shared_ptr<WriteRequest_>>& group,
                                                 const SwDbStatus& status,
                                                 unsigned long long durableSequence) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status.ok() && backgroundWriteError_.ok()) {
            backgroundWriteError_ = status;
        }
        if (status.ok()) {
            lastDurableSequence_ = std::max(lastDurableSequence_, durableSequence);
            metrics_.lastDurableSequence = lastDurableSequence_;
        }
    }
    commitCv_.notify_all();

    for (std::size_t i = 0; i < group.size(); ++i) {
        if (!group[i]) {
            continue;
        }
        std::lock_guard<std::mutex> doneLock(group[i]->mutex);
        group[i]->status = status;
        group[i]->done = true;
        group[i]->cv.notify_all();
    }
}

inline void SwEmbeddedDb::writeServiceLoop_() {
    while (true) {
        SwList<std::shared_ptr<WriteRequest_>> group;
        unsigned long long targetSequence = 0;
        bool stop = false;
        const SwDbStatus drainStatus = drainWriteRequests_(group, &targetSequence, &stop);
        if (!drainStatus.ok()) {
            finalizeWriteRequests_(group, drainStatus, 0);
            break;
        }
        if (stop) {
            break;
        }
        const SwDbStatus commitStatus = commitWriteRequests_(group, targetSequence);
        finalizeWriteRequests_(group, commitStatus, commitStatus.ok() ? targetSequence : 0);
        if (!commitStatus.ok()) {
            break;
        }
    }
}

inline SwDbStatus SwEmbeddedDb::syncInternal_(bool allowClosed) {
    if (!allowClosed && !opened_.load(std::memory_order_acquire)) {
        return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
    }
    if (options_.readOnly) {
        return SwDbStatus::success();
    }

    unsigned long long targetSequence = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!allowClosed && closing_) {
            return SwDbStatus(SwDbStatus::NotOpen, "database is closing");
        }
        if (!backgroundWriteError_.ok()) {
            return backgroundWriteError_;
        }
        targetSequence = lastVisibleSequence_;
        if (lastDurableSequence_ >= targetSequence && pendingWrites_.empty()) {
            return SwDbStatus::success();
        }
        writeServiceFlushRequested_ = true;
    }

    wakeWriteService_();

    std::unique_lock<std::mutex> lock(mutex_);
    commitCv_.wait(lock, [this, targetSequence]() {
        return !backgroundWriteError_.ok() ||
               lastDurableSequence_ >= targetSequence ||
               (writeServiceStopped_ && pendingWrites_.empty());
    });
    if (!backgroundWriteError_.ok()) {
        return backgroundWriteError_;
    }
    if (lastDurableSequence_ < targetSequence) {
        return SwDbStatus(SwDbStatus::IoError, "failed to durably flush pending writes");
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwEmbeddedDb::write(const SwDbWriteBatch& batch) {
    return swEmbeddedDbDetail::WriteCoordinator_(*this).write(batch);
}

inline SwDbStatus SwEmbeddedDb::write(SwDbWriteBatch&& batch) {
    return swEmbeddedDbDetail::WriteCoordinator_(*this).writeMutable(batch);
}

inline SwDbStatus SwEmbeddedDb::sync() {
    return syncInternal_(false);
}

inline void SwEmbeddedDb::applyBatchLocked_(unsigned long long sequence, const SwDbWriteBatch& batch) {
    swEmbeddedDbDetail::WriteCoordinator_(*this).applyBatchLocked(sequence, batch);
}

inline void SwEmbeddedDb::emitSecondaryTombstonesLocked_(const SwByteArray& primaryKey,
                                                         const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys,
                                                         unsigned long long sequence) {
    swEmbeddedDbDetail::WriteCoordinator_(*this).emitSecondaryTombstonesLocked(primaryKey, secondaryKeys, sequence);
}

inline void SwEmbeddedDb::emitSecondaryUpsertsLocked_(const SwByteArray& primaryKey,
                                                      const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys,
                                                      unsigned long long sequence) {
    swEmbeddedDbDetail::WriteCoordinator_(*this).emitSecondaryUpsertsLocked(primaryKey, secondaryKeys, sequence);
}
