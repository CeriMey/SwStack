inline SwDbStatus SwEmbeddedDb::flushMemTable_(const swEmbeddedDbDetail::MemTable_& mem, int level) {
    if (mem.primary.isEmpty() && mem.secondary.isEmpty()) {
        return SwDbStatus::success();
    }

    SwList<swEmbeddedDbDetail::TableMeta_> newTables;

    if (!mem.primary.isEmpty()) {
        swEmbeddedDbDetail::TableMeta_ meta;
        meta.kind = swEmbeddedDbDetail::TableKindPrimary;
        meta.level = level;
        meta.fileId = nextTableIdThreadSafe_();
        meta.fileName = SwString("L") + SwString::number(level) + "-" + SwString::number(meta.fileId) + ".sst";

        SwList<swEmbeddedDbDetail::TableRecord_> records;
        unsigned long long minSequence = 0;
        unsigned long long maxSequence = 0;
        unsigned long long blobFileId = mem.blobFileId;
        swDbPlatform::RandomAccessFile blobFile;
        bool blobFileOpened = false;
        bool blobFileDirty = false;

        const SwList<SwByteArray>& orderedPrimaryKeys = mem.orderedPrimaryKeys();
        for (std::size_t i = 0; i < orderedPrimaryKeys.size(); ++i) {
            const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it = mem.primary.find(orderedPrimaryKeys[i]);
            if (it == mem.primary.end()) {
                continue;
            }
            swEmbeddedDbDetail::PrimaryRecord_ record = it->second;
            minSequence = (minSequence == 0) ? record.sequence : std::min(minSequence, record.sequence);
            maxSequence = std::max(maxSequence, record.sequence);

            if (!record.deleted &&
                record.inlineValue &&
                static_cast<unsigned long long>(record.value.size()) > options_.inlineBlobThresholdBytes) {
                if (blobFileId == 0) {
                    blobFileId = nextTableIdThreadSafe_();
                }
                if (!blobFileOpened) {
                    SwString error;
                    if (!blobFile.open(swDbPlatform::joinPath(blobDir_, swEmbeddedDbDetail::blobFileName_(blobFileId)),
                                       swDbPlatform::RandomAccessFile::OpenMode::AppendCreate,
                                       &error)) {
                        return SwDbStatus(SwDbStatus::IoError, error);
                    }
                    blobFileOpened = true;
                }
                if (!externalizeBlobValueToFile_(blobFile, blobFileId, record)) {
                    return SwDbStatus(SwDbStatus::IoError, "failed to externalize blob value");
                }
                blobFileDirty = true;
            }

            swEmbeddedDbDetail::TableRecord_ raw;
            raw.userKey = orderedPrimaryKeys[i];
            raw.sequence = record.sequence;
            raw.flags = 0u;
            if (record.deleted) {
                raw.flags |= swEmbeddedDbDetail::RecordDeleted;
            } else {
                if (!record.inlineValue) {
                    raw.flags |= swEmbeddedDbDetail::RecordBlobRef;
                }
                swEmbeddedDbDetail::encodePrimaryPayload_(record, raw.payload);
            }
            records.append(raw);
        }

        if (blobFileOpened && blobFileDirty) {
            SwString error;
            if (!blobFile.sync(&error)) {
                return SwDbStatus(SwDbStatus::IoError, error);
            }
        }

        meta.minSequence = minSequence;
        meta.maxSequence = maxSequence;
        meta.recordCount = static_cast<unsigned long long>(records.size());
        meta.blobFileId = blobFileHasData_(blobFileId) ? blobFileId : 0;

        const SwDbStatus tableStatus = writeTableFile_(meta, records);
        if (!tableStatus.ok()) {
            return tableStatus;
        }
        newTables.append(meta);
    }

    SwList<SwString> indexNames = mem.secondary.keys();
    std::sort(indexNames.begin(), indexNames.end(), [](const SwString& lhs, const SwString& rhs) {
        return lhs.toStdString() < rhs.toStdString();
    });
    for (std::size_t i = 0; i < indexNames.size(); ++i) {
        const SwHash<SwString, swEmbeddedDbDetail::SecondaryMemStore_>::const_iterator bucketIt =
            mem.secondary.find(indexNames[i]);
        if (bucketIt == mem.secondary.end() || bucketIt->second.isEmpty()) {
            continue;
        }
        const swEmbeddedDbDetail::SecondaryMemStore_& bucket = bucketIt->second;

        swEmbeddedDbDetail::TableMeta_ meta;
        meta.kind = swEmbeddedDbDetail::TableKindIndex;
        meta.level = level;
        meta.fileId = nextTableIdThreadSafe_();
        meta.fileName = SwString("L") + SwString::number(level) + "-" + SwString::number(meta.fileId) + ".sst";
        meta.indexName = indexNames[i];

        SwList<swEmbeddedDbDetail::TableRecord_> records;
        unsigned long long minSequence = 0;
        unsigned long long maxSequence = 0;
        const SwList<SwByteArray>& orderedSecondaryKeys = mem.orderedSecondaryKeys(indexNames[i]);
        for (std::size_t j = 0; j < orderedSecondaryKeys.size(); ++j) {
            const swEmbeddedDbDetail::SecondaryMemStore_::const_iterator it = bucket.find(orderedSecondaryKeys[j]);
            if (it == bucket.end()) {
                continue;
            }
            minSequence = (minSequence == 0) ? it->second.sequence : std::min(minSequence, it->second.sequence);
            maxSequence = std::max(maxSequence, it->second.sequence);
            swEmbeddedDbDetail::TableRecord_ raw;
            raw.userKey = orderedSecondaryKeys[j];
            raw.sequence = it->second.sequence;
            raw.flags = it->second.deleted ? swEmbeddedDbDetail::RecordDeleted : 0u;
            records.append(raw);
        }

        meta.minSequence = minSequence;
        meta.maxSequence = maxSequence;
        meta.recordCount = static_cast<unsigned long long>(records.size());
        const SwDbStatus tableStatus = writeTableFile_(meta, records);
        if (!tableStatus.ok()) {
            return tableStatus;
        }
        newTables.append(meta);
    }

    SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> openedHandles;
    for (std::size_t i = 0; i < newTables.size(); ++i) {
        std::shared_ptr<swEmbeddedDbDetail::TableHandle_> handle(new swEmbeddedDbDetail::TableHandle_());
        SwDbStatus status;
        if (!handle->open(dbPath_, newTables[i], options_, readCacheManager_, status)) {
            return status;
        }
        openedHandles[newTables[i].fileName] = handle;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    swEmbeddedDbDetail::Manifest_ backupManifest = manifest_;
    SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> backupHandles = tableHandles_;

    manifest_.maxSequence = std::max(manifest_.maxSequence, mem.maxSeq);
    for (std::size_t i = 0; i < newTables.size(); ++i) {
        manifest_.tables.append(newTables[i]);
        tableHandles_[newTables[i].fileName] = openedHandles.value(newTables[i].fileName);
    }
    rebuildTableCachesLocked_();

    unsigned long long replayFrom = manifest_.activeWalId;
    if (replayFrom == 0) {
        replayFrom = 1;
    }
    if (mutable_.walId != 0) {
        replayFrom = std::min(replayFrom, mutable_.walId);
    }
    for (std::size_t i = 0; i < immutables_.size(); ++i) {
        if (immutables_[i].walId != 0) {
            replayFrom = std::min(replayFrom, immutables_[i].walId);
        }
    }
    manifest_.replayFromWalId = replayFrom;

    const SwDbStatus persistStatus = persistManifestLocked_();
    if (!persistStatus.ok()) {
        manifest_ = backupManifest;
        tableHandles_ = backupHandles;
        rebuildTableCachesLocked_();
        return persistStatus;
    }

    metrics_.flushCount += 1;
    if (!closing_) {
        cleanupCoveredWalFilesLocked_();
        compactL0Locked_();
        scheduleBlobGcLocked_();
    }
    return SwDbStatus::success();
}

