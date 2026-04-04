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
    rebuildTableCachesLocked_();
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
    snapshotState->mutableMem = mutable_;
    snapshotState->immutableMems = immutables_;
    snapshotState->primaryTablesNewestFirst = primaryTableHandlesNewestFirst_;
    snapshotState->indexTablesNewestFirst = indexTableHandlesNewestFirst_;
    if (!buildReadModelLocked_(snapshotState->readModel)) {
        readOnlySnapshotState_.reset();
        return;
    }
    readOnlySnapshotState_ = snapshotState;
}

inline bool SwEmbeddedDb::resolveValueThreadSafe_(swEmbeddedDbDetail::PrimaryRecord_& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolveValueLocked_(record);
}

inline unsigned long long SwEmbeddedDb::nextTableIdThreadSafe_() {
    std::lock_guard<std::mutex> lock(mutex_);
    return manifest_.nextTableId++;
}

inline bool SwEmbeddedDb::externalizeBlobValueThreadSafe_(unsigned long long blobFileId,
                                                          swEmbeddedDbDetail::PrimaryRecord_& record) {
    return externalizeBlobValue_(blobFileId, record);
}
