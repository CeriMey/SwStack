namespace swEmbeddedDbDetail {

inline void mergeMemTableInto_(MemTable_& target, const MemTable_& source) {
    if (source.walId != 0 && (target.walId == 0 || source.walId < target.walId)) {
        target.walId = source.walId;
    }
    if (source.blobFileId != 0) {
        target.blobFileId = std::max(target.blobFileId, source.blobFileId);
    }
    if (source.minSeq != 0 && (target.minSeq == 0 || source.minSeq < target.minSeq)) {
        target.minSeq = source.minSeq;
    }
    target.maxSeq = std::max(target.maxSeq, source.maxSeq);
    target.approximateBytes += source.approximateBytes;

    for (swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it = source.primary.begin();
         it != source.primary.end();
         ++it) {
        swEmbeddedDbDetail::PrimaryMemStore_::iterator existing = target.primary.find(it->first);
        if (existing == target.primary.end() || existing->second.sequence <= it->second.sequence) {
            target.primary[it->first] = it->second;
        }
    }

    for (SwHash<SwString, swEmbeddedDbDetail::SecondaryMemStore_>::const_iterator bucketIt = source.secondary.begin();
         bucketIt != source.secondary.end();
         ++bucketIt) {
        swEmbeddedDbDetail::SecondaryMemStore_& targetBucket = target.secondary[bucketIt->first];
        for (swEmbeddedDbDetail::SecondaryMemStore_::const_iterator entryIt = bucketIt->second.begin();
             entryIt != bucketIt->second.end();
             ++entryIt) {
            swEmbeddedDbDetail::SecondaryMemStore_::iterator existing = targetBucket.find(entryIt->first);
            if (existing == targetBucket.end() || existing->second.sequence <= entryIt->second.sequence) {
                targetBucket[entryIt->first] = entryIt->second;
            }
        }
    }
}

inline MemTable_ mergeMemTablesForFlush_(const SwList<MemTable_>& mems) {
    MemTable_ merged;
    for (std::size_t i = 0; i < mems.size(); ++i) {
        mergeMemTableInto_(merged, mems[i]);
    }
    merged.invalidatePrimaryOrder();
    merged.invalidateAllSecondaryOrders();
    return merged;
}

} // namespace swEmbeddedDbDetail

inline void SwEmbeddedDb::prepareMutableMemTableLocked_() {
    mutable_.primary.reserve(swEmbeddedDbDetail::memTableBucketReserveHint_(options_));
    mutable_.secondary.reserve(4u);
}

inline void SwEmbeddedDb::rotateMutableToImmutableLocked_() {
    if (mutable_.primary.isEmpty() && mutable_.secondary.isEmpty()) {
        return;
    }
    immutables_.append(mutable_);
    manifest_.activeWalId = manifest_.nextWalId++;
    mutable_ = swEmbeddedDbDetail::MemTable_();
    mutable_.walId = manifest_.activeWalId;
    prepareMutableMemTableLocked_();
    activeWalFile_.close();
    if (!closing_) {
        (void)openActiveWal_();
    }
}

inline void SwEmbeddedDb::scheduleFlushLocked_() {
    if (flushScheduled_ || closing_ || immutables_.isEmpty()) {
        return;
    }
    flushScheduled_ = true;
    backgroundPool_.start([this]() { this->flushBackground_(); });
}

inline void SwEmbeddedDb::flushBackground_() {
    while (true) {
        swEmbeddedDbDetail::MemTable_ mem;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (immutables_.isEmpty() || closing_) {
                flushScheduled_ = false;
                return;
            }
            mem = immutables_.first();
            immutables_.removeAt(0);
        }
        const SwDbStatus status = flushMemTable_(mem, 0);
        if (!status.ok()) {
            swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
        }
    }
}

inline void SwEmbeddedDb::flushAllSync_() {
    SwList<swEmbeddedDbDetail::MemTable_> pendingMems;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingMems.append(immutables_.begin(), immutables_.end());
        if (!mutable_.primary.isEmpty() || !mutable_.secondary.isEmpty()) {
            pendingMems.append(mutable_);
        }
        immutables_.clear();
        mutable_ = swEmbeddedDbDetail::MemTable_();
        mutable_.walId = manifest_.activeWalId;
        prepareMutableMemTableLocked_();
        activeWalFile_.close();
    }

    if (pendingMems.isEmpty()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!closing_) {
                cleanupCoveredWalFilesLocked_();
            }
            (void)persistManifestLocked_();
        }
        return;
    }

    const swEmbeddedDbDetail::MemTable_ merged = swEmbeddedDbDetail::mergeMemTablesForFlush_(pendingMems);
    const SwDbStatus status = flushMemTable_(merged, 0);
    if (!status.ok()) {
        swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
    }
}

