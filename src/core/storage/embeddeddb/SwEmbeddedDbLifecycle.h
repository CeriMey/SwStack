namespace swEmbeddedDbDetail {

class LifecycleManager_ {
public:
    explicit LifecycleManager_(SwEmbeddedDb& db)
        : db_(db) {
    }

    SwDbStatus open(const SwEmbeddedDbOptions& options) {
        db_.close();
        if (options.dbPath.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "dbPath is required");
        }
        db_.options_ = options;
        if (db_.options_.maxBackgroundJobs <= 0) {
            db_.options_.maxBackgroundJobs = 1;
        }
        db_.backgroundPool_.setMaxThreadCount(db_.options_.maxBackgroundJobs);
        db_.readCacheManager_.reset(new swEmbeddedDbDetail::ReadCacheManager_(db_.options_.readCacheBytes));

        db_.dbPath_ = swDbPlatform::normalizePath(db_.options_.dbPath);
        db_.walDir_ = swDbPlatform::joinPath(db_.dbPath_, "wal");
        db_.tableDir_ = swDbPlatform::joinPath(db_.dbPath_, "tables");
        db_.blobDir_ = swDbPlatform::joinPath(db_.dbPath_, "blobs");
        db_.tmpDir_ = swDbPlatform::joinPath(db_.dbPath_, "tmp");

        if (!db_.options_.readOnly) {
            if (!swDbPlatform::ensureDirectory(db_.dbPath_) ||
                !swDbPlatform::ensureDirectory(db_.walDir_) ||
                !swDbPlatform::ensureDirectory(db_.tableDir_) ||
                !swDbPlatform::ensureDirectory(db_.blobDir_) ||
                !swDbPlatform::ensureDirectory(db_.tmpDir_)) {
                return SwDbStatus(SwDbStatus::IoError, "failed to create database layout");
            }
            SwString lockError;
            if (!db_.writerLock_.lockExclusive(swDbPlatform::joinPath(db_.dbPath_, "LOCK"), &lockError)) {
                return SwDbStatus(SwDbStatus::Busy, lockError);
            }
        } else if (!swDbPlatform::directoryExists(db_.dbPath_)) {
            return SwDbStatus(SwDbStatus::NotOpen, "database directory does not exist");
        }

        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            const SwDbStatus status = db_.loadFromDisk_(true);
            if (!status.ok()) {
                return status;
            }
            if (!db_.options_.readOnly) {
                const SwDbStatus optionsStatus = db_.persistOptions_();
                if (!optionsStatus.ok()) {
                    return optionsStatus;
                }
                const SwDbStatus walStatus = db_.openActiveWal_();
                if (!walStatus.ok()) {
                    return walStatus;
                }
            }
        }

        if (!db_.options_.readOnly) {
            db_.startWriteService_();
        }

        db_.setupShmNotifications_();
        db_.opened_.store(true, std::memory_order_release);
        if (!db_.options_.readOnly) {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            db_.publishShmNotificationLocked_();
        }
        return SwDbStatus::success();
    }

    void close() {
        const bool wasOpen = db_.opened_.exchange(false, std::memory_order_acq_rel);
        if (!wasOpen && !db_.writerLock_.isLocked()) {
            resetState();
            return;
        }

        if (!db_.options_.readOnly && db_.writerLock_.isLocked()) {
            {
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.closing_ = true;
            }
            const SwDbStatus syncStatus = db_.syncInternal_(true);
            if (!syncStatus.ok()) {
                swCError(kSwLogCategory_SwEmbeddedDb) << syncStatus.message();
            }
            db_.stopWriteService_();
            drainPendingWrites();
            db_.flushAllSync_();
        } else {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            db_.closing_ = true;
        }

        db_.backgroundPool_.waitForDone(15000);
        db_.teardownShmNotifications_();
        db_.activeWalFile_.close();
        db_.writerLock_.unlock();
        resetState();
    }

    SwDbStatus get(const SwByteArray& primaryKey,
                   SwByteArray* valueOut,
                   SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) {
        if (!db_.opened_.load(std::memory_order_acquire)) {
            return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
        }
        db_.maybeRefreshFromNotifications_();
        if (db_.options_.readOnly) {
            std::shared_ptr<swEmbeddedDbDetail::SnapshotState_> snapshotState;
            {
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.metrics_.getCount += 1;
                snapshotState = db_.readOnlySnapshotState_;
            }
            if (snapshotState) {
                swEmbeddedDbDetail::PrimaryRecord_ record;
                if (!swEmbeddedDbDetail::snapshotLookupPrimary_(snapshotState, primaryKey, record, true)) {
                    return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
                }
                if (valueOut) {
                    *valueOut = record.value;
                }
                if (secondaryKeysOut) {
                    *secondaryKeysOut = record.secondaryKeys;
                }
                return SwDbStatus::success();
            }
        }
        std::lock_guard<std::mutex> lock(db_.mutex_);
        db_.metrics_.getCount += 1;
        swEmbeddedDbDetail::PrimaryRecord_ record;
        if (!db_.lookupPrimaryLocked_(primaryKey, record, true)) {
            return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
        }
        if (valueOut) {
            *valueOut = record.value;
        }
        if (secondaryKeysOut) {
            *secondaryKeysOut = record.secondaryKeys;
        }
        return SwDbStatus::success();
    }

    SwDbIterator scanPrimary(const SwByteArray& startKey, const SwByteArray& endKey) {
        db_.maybeRefreshFromNotifications_();
        return createSnapshot().scanPrimary(startKey, endKey);
    }

    SwDbIterator scanIndex(const SwString& indexName,
                           const SwByteArray& startSecondaryKey,
                           const SwByteArray& endSecondaryKey) {
        db_.maybeRefreshFromNotifications_();
        return createSnapshot().scanIndex(indexName, startSecondaryKey, endSecondaryKey);
    }

    SwDbStatus refresh() {
        if (!db_.opened_.load(std::memory_order_acquire)) {
            return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
        }
        if (!db_.options_.readOnly) {
            return SwDbStatus::success();
        }
        std::lock_guard<std::mutex> lock(db_.mutex_);
        return db_.loadFromDisk_(false);
    }

    SwDbSnapshot createSnapshot() {
        SwDbSnapshot snapshot;
        if (!db_.opened_.load(std::memory_order_acquire)) {
            return snapshot;
        }
        db_.maybeRefreshFromNotifications_();
        std::lock_guard<std::mutex> lock(db_.mutex_);
        db_.metrics_.snapshotCount += 1;
        if (db_.options_.readOnly && db_.readOnlySnapshotState_) {
            snapshot.state_ = db_.readOnlySnapshotState_;
        } else {
            snapshot.state_.reset(new swEmbeddedDbDetail::SnapshotState_());
            snapshot.state_->visibleSequence = db_.lastVisibleSequence_;
            snapshot.state_->blobDir = db_.blobDir_;
            snapshot.state_->options = db_.options_;
            snapshot.state_->mutableMem = db_.mutable_;
            snapshot.state_->immutableMems = db_.immutables_;
            snapshot.state_->primaryTablesNewestFirst = db_.primaryTableHandlesNewestFirst_;
            snapshot.state_->indexTablesNewestFirst = db_.indexTableHandlesNewestFirst_;
            if (!db_.buildReadModelLocked_(snapshot.state_->readModel)) {
                snapshot.state_.reset();
                return snapshot;
            }
        }
        snapshot.valid_ = true;
        snapshot.visibleSequence_ = snapshot.state_->visibleSequence;
        return snapshot;
    }

    void resetState() {
        std::lock_guard<std::mutex> lock(db_.mutex_);
        db_.manifest_ = swEmbeddedDbDetail::Manifest_();
        db_.mutable_ = swEmbeddedDbDetail::MemTable_();
        db_.immutables_.clear();
        db_.tableHandles_.clear();
        db_.primaryTableHandlesNewestFirst_.clear();
        db_.indexTableHandlesNewestFirst_.clear();
        db_.readOnlySnapshotState_.reset();
        db_.pendingWrites_.clear();
        db_.writeServiceStop_ = false;
        db_.writeServiceRunning_ = false;
        db_.writeServiceStopped_ = false;
        db_.writeServiceFlushRequested_ = false;
        db_.flushScheduled_ = false;
        db_.compactionScheduled_ = false;
        db_.blobGcScheduled_ = false;
        db_.closing_ = false;
        db_.lastVisibleSequence_ = 0;
        db_.lastDurableSequence_ = 0;
        db_.nextSequence_ = 1;
        db_.maxKnownPrimaryKey_.clear();
        db_.walScratch_.clear();
        db_.backgroundWriteError_ = SwDbStatus::success();
        db_.metrics_ = SwDbMetrics();
        db_.readCacheManager_.reset();
        db_.shmRefreshHint_.store(false, std::memory_order_release);
        db_.dbPath_.clear();
        db_.walDir_.clear();
        db_.tableDir_.clear();
        db_.blobDir_.clear();
        db_.tmpDir_.clear();
    }

    void drainPendingWrites() {
        std::deque<std::shared_ptr<SwEmbeddedDb::WriteRequest_>> pending;
        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            pending.swap(db_.pendingWrites_);
            db_.metrics_.pendingWriteCount = 0;
        }
        for (std::size_t i = 0; i < pending.size(); ++i) {
            std::lock_guard<std::mutex> doneLock(pending[i]->mutex);
            pending[i]->status = SwDbStatus(SwDbStatus::NotOpen, "database is closing");
            pending[i]->done = true;
            pending[i]->cv.notify_all();
        }
    }

    static SwDbMetrics metricsSnapshot(const SwEmbeddedDb& db) {
        std::lock_guard<std::mutex> lock(db.mutex_);
        SwDbMetrics metrics = db.metrics_;
        metrics.pendingWriteCount = static_cast<unsigned long long>(db.pendingWrites_.size());
        metrics.tableCount = static_cast<unsigned long long>(db.manifest_.tables.size());
        metrics.lastVisibleSequence = db.lastVisibleSequence_;
        metrics.lastDurableSequence = db.lastDurableSequence_;
        if (db.readCacheManager_) {
            metrics.readCacheResidentBytes = db.readCacheManager_->residentBytes();
            metrics.readCacheEntryCount = db.readCacheManager_->entryCount();
            metrics.readCacheHitCount = db.readCacheManager_->hitCount();
            metrics.readCacheMissCount = db.readCacheManager_->missCount();
        }
        return metrics;
    }

    static std::string trimText(const std::string& input) {
        std::size_t begin = 0;
        while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
            ++begin;
        }
        std::size_t end = input.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
            --end;
        }
        return input.substr(begin, end - begin);
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline SwEmbeddedDb::SwEmbeddedDb() {
    backgroundPool_.setMaxThreadCount(1);
    backgroundPool_.setExpiryTimeout(-1);
}

inline SwEmbeddedDb::~SwEmbeddedDb() {
    close();
}

inline SwDbStatus SwEmbeddedDb::open(const SwEmbeddedDbOptions& options) {
    return swEmbeddedDbDetail::LifecycleManager_(*this).open(options);
}

inline void SwEmbeddedDb::close() {
    swEmbeddedDbDetail::LifecycleManager_(*this).close();
}

inline SwDbStatus SwEmbeddedDb::get(const SwByteArray& primaryKey,
                                    SwByteArray* valueOut,
                                    SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) {
    return swEmbeddedDbDetail::LifecycleManager_(*this).get(primaryKey, valueOut, secondaryKeysOut);
}

inline SwDbIterator SwEmbeddedDb::scanPrimary(const SwByteArray& startKey, const SwByteArray& endKey) {
    return swEmbeddedDbDetail::LifecycleManager_(*this).scanPrimary(startKey, endKey);
}

inline SwDbIterator SwEmbeddedDb::scanIndex(const SwString& indexName,
                                            const SwByteArray& startSecondaryKey,
                                            const SwByteArray& endSecondaryKey) {
    return swEmbeddedDbDetail::LifecycleManager_(*this).scanIndex(indexName, startSecondaryKey, endSecondaryKey);
}

inline SwDbStatus SwEmbeddedDb::refresh() {
    return swEmbeddedDbDetail::LifecycleManager_(*this).refresh();
}

inline SwDbSnapshot SwEmbeddedDb::createSnapshot() {
    return swEmbeddedDbDetail::LifecycleManager_(*this).createSnapshot();
}

inline SwDbMetrics SwEmbeddedDb::metricsSnapshot() const {
    return swEmbeddedDbDetail::LifecycleManager_::metricsSnapshot(*this);
}

inline std::string SwEmbeddedDb::trimText_(const std::string& input) {
    return swEmbeddedDbDetail::LifecycleManager_::trimText(input);
}

inline void SwEmbeddedDb::resetState_() {
    swEmbeddedDbDetail::LifecycleManager_(*this).resetState();
}

inline void SwEmbeddedDb::drainPendingWrites_() {
    swEmbeddedDbDetail::LifecycleManager_(*this).drainPendingWrites();
}
