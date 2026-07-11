inline void SwEmbeddedDb::removeTablesLocked_(const SwList<swEmbeddedDbDetail::TableMeta_>& toRemove) {
    for (std::size_t i = 0; i < toRemove.size(); ++i) {
        for (std::size_t j = 0; j < manifest_.tables.size(); ++j) {
            if (manifest_.tables[j].fileName == toRemove[i].fileName) {
                manifest_.tables.removeAt(j);
                break;
            }
        }
        tableHandles_.remove(toRemove[i].fileName);
    }
    // Drop the cached writer snapshot: it pins handles of the removed tables,
    // which would block the file deletions that follow a compaction.
    invalidateWriterReadCacheLocked_();
    rebuildTableCachesLocked_();
}

inline void SwEmbeddedDb::invalidateWriterReadCacheLocked_() {
    writerSnapshotState_.reset();
    writerOverlay_ = swEmbeddedDbDetail::WriterOverlay_();
    writerOverlaySnapshot_.reset();
    writerSnapshotGeneration_ = 0;
    writerOverlaySnapshotGeneration_ = 0;
}

inline void SwEmbeddedDb::rebuildTableCachesLocked_() {
    primaryTableHandlesNewestFirst_.clear();
    indexTableHandlesNewestFirst_.clear();

    SwList<swEmbeddedDbDetail::TableMeta_> sortedTables = manifest_.tables;
    std::sort(sortedTables.begin(), sortedTables.end(), swEmbeddedDbDetail::compareTableMetaNewestFirst_);
    for (std::size_t i = 0; i < sortedTables.size(); ++i) {
        const SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>>::const_iterator handleIt =
            tableHandles_.find(sortedTables[i].fileName);
        if (handleIt == tableHandles_.end()) {
            continue;
        }
        if (sortedTables[i].kind == swEmbeddedDbDetail::TableKindPrimary) {
            primaryTableHandlesNewestFirst_.push_back(handleIt->second);
            if (!handleIt->second->maxUserKey().isEmpty() &&
                (maxKnownPrimaryKey_.isEmpty() || maxKnownPrimaryKey_ < handleIt->second->maxUserKey())) {
                maxKnownPrimaryKey_ = handleIt->second->maxUserKey();
            }
        } else if (sortedTables[i].kind == swEmbeddedDbDetail::TableKindIndex) {
            indexTableHandlesNewestFirst_[sortedTables[i].indexName].push_back(handleIt->second);
        }
    }
}

inline void SwEmbeddedDb::rebuildReadOnlySnapshotLocked_() {
    if (!options_.readOnly) {
        readOnlySnapshotState_.reset();
        return;
    }

    std::shared_ptr<swEmbeddedDbDetail::SnapshotState_> snapshotState(new swEmbeddedDbDetail::SnapshotState_());
    snapshotState->visibleSequence = lastVisibleSequence_;
    snapshotState->blobDir = blobDir_;
    snapshotState->options = options_;
    snapshotState->mutableMem.reset(new swEmbeddedDbDetail::MemTable_(mutable_));
    for (std::size_t i = 0; i < immutables_.size(); ++i) {
        snapshotState->immutableMems.append(
            std::shared_ptr<const swEmbeddedDbDetail::MemTable_>(
                new swEmbeddedDbDetail::MemTable_(immutables_[i])));
    }
    snapshotState->primaryTablesNewestFirst = primaryTableHandlesNewestFirst_;
    snapshotState->indexTablesNewestFirst = indexTableHandlesNewestFirst_;
    if (!buildReadModelLocked_(snapshotState->readModel)) {
        readOnlySnapshotState_.reset();
        return;
    }
    readOnlySnapshotState_ = snapshotState;
}

inline bool SwEmbeddedDb::resolveValueThreadSafe_(swEmbeddedDbDetail::PrimaryRecord_& record) {
    SwEmbeddedDbLock_ lock(mutex_);
    return resolveValueLocked_(record);
}

inline unsigned long long SwEmbeddedDb::nextTableIdThreadSafe_() {
    SwEmbeddedDbLock_ lock(mutex_);
    return manifest_.nextTableId++;
}

inline bool SwEmbeddedDb::externalizeBlobValueThreadSafe_(unsigned long long blobFileId,
                                                          swEmbeddedDbDetail::PrimaryRecord_& record) {
    return externalizeBlobValue_(blobFileId, record);
}
