namespace swEmbeddedDbDetail {

class BlobGcManager_ {
public:
    explicit BlobGcManager_(SwEmbeddedDb& db)
        : db_(db) {
    }

    void scheduleBlobGcLocked() {
        if (db_.blobGcScheduled_ ||
            db_.closing_ ||
            db_.options_.readOnly ||
            db_.compactionScheduled_ ||
            !db_.immutables_.isEmpty() ||
            !db_.mutable_.primary.isEmpty() ||
            !db_.mutable_.secondary.isEmpty()) {
            return;
        }
        db_.blobGcScheduled_ = true;
        SwEmbeddedDb* db = &db_;
        db_.backgroundPool_.start([db]() { db->runBlobGc_(); });
    }

    void runBlobGc() {
        static const unsigned long long kGarbageThresholdPct = 30ull;

        SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_> mergedPrimary;
        SwList<swEmbeddedDbDetail::TableMeta_> oldPrimaryTables;
        unsigned long long stableSequence = 0;
        unsigned long long stableManifestId = 0;

        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_ ||
                db_.options_.readOnly ||
                db_.compactionScheduled_ ||
                !db_.immutables_.isEmpty() ||
                !db_.mutable_.primary.isEmpty() ||
                !db_.mutable_.secondary.isEmpty()) {
                db_.blobGcScheduled_ = false;
                return;
            }
            if (!db_.materializePrimaryLocked_(mergedPrimary)) {
                db_.blobGcScheduled_ = false;
                return;
            }
            stableSequence = db_.lastVisibleSequence_;
            stableManifestId = db_.manifest_.manifestId;
            for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
                if (db_.manifest_.tables[i].kind == swEmbeddedDbDetail::TableKindPrimary) {
                    oldPrimaryTables.append(db_.manifest_.tables[i]);
                }
            }
        }

        const SwList<SwString> blobFiles = swDbPlatform::listFiles(db_.blobDir_, "BLOB-", ".dat");
        SwHash<unsigned long long, unsigned long long> liveBlobBytes;
        for (SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::const_iterator it = mergedPrimary.begin();
             it != mergedPrimary.end();
             ++it) {
            if (it.value().blobRef.valid) {
                liveBlobBytes[it.value().blobRef.fileId] =
                    liveBlobBytes.value(it.value().blobRef.fileId) +
                    static_cast<unsigned long long>(it.value().blobRef.length);
            }
        }

        bool hasGarbage = false;
        for (std::size_t i = 0; i < blobFiles.size(); ++i) {
            const unsigned long long fileId =
                swEmbeddedDbDetail::parseNumericSuffix_(swDbPlatform::fileName(blobFiles[i]), "BLOB-", ".dat");
            swDbPlatform::RandomAccessFile file;
            if (!file.open(blobFiles[i], swDbPlatform::RandomAccessFile::OpenMode::ReadOnly, nullptr)) {
                continue;
            }
            const unsigned long long fileBytes = file.size();
            const unsigned long long liveBytes = liveBlobBytes.value(fileId);
            if (liveBytes == 0 ||
                (fileBytes > 0 && (fileBytes - liveBytes) * 100ull >= fileBytes * kGarbageThresholdPct)) {
                hasGarbage = true;
                break;
            }
        }

        if (!hasGarbage) {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            db_.blobGcScheduled_ = false;
            return;
        }

        swEmbeddedDbDetail::TableMeta_ newPrimaryMeta;
        std::shared_ptr<swEmbeddedDbDetail::TableHandle_> newPrimaryHandle;
        bool hasNewPrimaryTable = false;
        unsigned long long newBlobFileId = 0;

        if (!mergedPrimary.isEmpty()) {
            newPrimaryMeta.kind = swEmbeddedDbDetail::TableKindPrimary;
            newPrimaryMeta.level = 1;
            newPrimaryMeta.fileId = db_.nextTableIdThreadSafe_();
            newPrimaryMeta.fileName = SwString("L1-") + SwString::number(newPrimaryMeta.fileId) + ".sst";

            SwList<swEmbeddedDbDetail::TableRecord_> records;
            swDbPlatform::RandomAccessFile blobFile;
            bool blobFileOpened = false;
            bool blobFileDirty = false;
            for (SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::const_iterator it = mergedPrimary.begin();
                 it != mergedPrimary.end();
                 ++it) {
                swEmbeddedDbDetail::PrimaryRecord_ record = it.value();
                record.inlineValue = true;
                record.blobRef = swEmbeddedDbDetail::BlobRef_();
                if (static_cast<unsigned long long>(record.value.size()) > db_.options_.inlineBlobThresholdBytes) {
                    if (newBlobFileId == 0) {
                        newBlobFileId = db_.nextTableIdThreadSafe_();
                    }
                    if (!blobFileOpened) {
                        SwString error;
                        if (!blobFile.open(swDbPlatform::joinPath(db_.blobDir_,
                                                                  swEmbeddedDbDetail::blobFileName_(newBlobFileId)),
                                           swDbPlatform::RandomAccessFile::OpenMode::AppendCreate,
                                           &error)) {
                            std::lock_guard<std::mutex> lock(db_.mutex_);
                            db_.blobGcScheduled_ = false;
                            swCError(kSwLogCategory_SwEmbeddedDb) << error;
                            return;
                        }
                        blobFileOpened = true;
                    }
                    if (!db_.externalizeBlobValueToFile_(blobFile, newBlobFileId, record)) {
                        std::lock_guard<std::mutex> lock(db_.mutex_);
                        db_.blobGcScheduled_ = false;
                        return;
                    }
                    blobFileDirty = true;
                }

                if (newPrimaryMeta.minSequence == 0 || record.sequence < newPrimaryMeta.minSequence) {
                    newPrimaryMeta.minSequence = record.sequence;
                }
                newPrimaryMeta.maxSequence = std::max(newPrimaryMeta.maxSequence, record.sequence);

                swEmbeddedDbDetail::TableRecord_ raw;
                raw.userKey = it.key();
                raw.sequence = record.sequence;
                raw.flags = record.inlineValue ? 0u : swEmbeddedDbDetail::RecordBlobRef;
                swEmbeddedDbDetail::encodePrimaryPayload_(record, raw.payload);
                records.append(raw);
            }

            if (blobFileOpened && blobFileDirty) {
                SwString error;
                if (!blobFile.sync(&error)) {
                    if (newBlobFileId != 0) {
                        (void)swDbPlatform::removeFile(
                            swDbPlatform::joinPath(db_.blobDir_, swEmbeddedDbDetail::blobFileName_(newBlobFileId)));
                    }
                    std::lock_guard<std::mutex> lock(db_.mutex_);
                    db_.blobGcScheduled_ = false;
                    swCError(kSwLogCategory_SwEmbeddedDb) << error;
                    return;
                }
            }

            newPrimaryMeta.recordCount = static_cast<unsigned long long>(records.size());
            newPrimaryMeta.blobFileId = db_.blobFileHasData_(newBlobFileId) ? newBlobFileId : 0;
            const SwDbStatus tableStatus = db_.writeTableFile_(newPrimaryMeta, records);
            if (!tableStatus.ok()) {
                if (newBlobFileId != 0) {
                    (void)swDbPlatform::removeFile(
                        swDbPlatform::joinPath(db_.blobDir_, swEmbeddedDbDetail::blobFileName_(newBlobFileId)));
                }
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.blobGcScheduled_ = false;
                swCError(kSwLogCategory_SwEmbeddedDb) << tableStatus.message();
                return;
            }

            newPrimaryHandle.reset(new swEmbeddedDbDetail::TableHandle_());
            SwDbStatus openStatus;
            if (!newPrimaryHandle->open(
                    db_.dbPath_, newPrimaryMeta, db_.options_, db_.readCacheManager_, openStatus)) {
                (void)swDbPlatform::removeFile(swDbPlatform::joinPath(db_.tableDir_, newPrimaryMeta.fileName));
                if (newBlobFileId != 0) {
                    (void)swDbPlatform::removeFile(
                        swDbPlatform::joinPath(db_.blobDir_, swEmbeddedDbDetail::blobFileName_(newBlobFileId)));
                }
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.blobGcScheduled_ = false;
                swCError(kSwLogCategory_SwEmbeddedDb) << openStatus.message();
                return;
            }
            hasNewPrimaryTable = true;
        }

        bool committed = false;
        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_ ||
                db_.options_.readOnly ||
                db_.compactionScheduled_ ||
                !db_.immutables_.isEmpty() ||
                !db_.mutable_.primary.isEmpty() ||
                !db_.mutable_.secondary.isEmpty() ||
                db_.lastVisibleSequence_ != stableSequence ||
                db_.manifest_.manifestId != stableManifestId) {
                db_.blobGcScheduled_ = false;
            } else {
                swEmbeddedDbDetail::Manifest_ backupManifest = db_.manifest_;
                SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> backupHandles = db_.tableHandles_;
                db_.removeTablesLocked_(oldPrimaryTables);
                if (hasNewPrimaryTable) {
                    db_.manifest_.tables.append(newPrimaryMeta);
                    db_.tableHandles_[newPrimaryMeta.fileName] = newPrimaryHandle;
                }
                db_.rebuildTableCachesLocked_();

                const SwDbStatus persistStatus = db_.persistManifestLocked_();
                if (!persistStatus.ok()) {
                    db_.manifest_ = backupManifest;
                    db_.tableHandles_ = backupHandles;
                    db_.rebuildTableCachesLocked_();
                    db_.blobGcScheduled_ = false;
                    swCError(kSwLogCategory_SwEmbeddedDb) << persistStatus.message();
                } else {
                    db_.blobGcScheduled_ = false;
                    committed = true;
                }
            }
        }

        if (!committed) {
            if (hasNewPrimaryTable) {
                (void)swDbPlatform::removeFile(swDbPlatform::joinPath(db_.tableDir_, newPrimaryMeta.fileName));
            }
            if (newBlobFileId != 0) {
                (void)swDbPlatform::removeFile(
                    swDbPlatform::joinPath(db_.blobDir_, swEmbeddedDbDetail::blobFileName_(newBlobFileId)));
            }
            return;
        }

        for (std::size_t i = 0; i < oldPrimaryTables.size(); ++i) {
            (void)swDbPlatform::removeFile(swDbPlatform::joinPath(db_.tableDir_, oldPrimaryTables[i].fileName));
        }
        for (std::size_t i = 0; i < blobFiles.size(); ++i) {
            const unsigned long long fileId =
                swEmbeddedDbDetail::parseNumericSuffix_(swDbPlatform::fileName(blobFiles[i]), "BLOB-", ".dat");
            if (fileId != 0 && fileId != newBlobFileId) {
                (void)swDbPlatform::removeFile(blobFiles[i]);
            }
        }
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline void SwEmbeddedDb::scheduleBlobGcLocked_() {
    swEmbeddedDbDetail::BlobGcManager_(*this).scheduleBlobGcLocked();
}

inline void SwEmbeddedDb::runBlobGc_() {
    swEmbeddedDbDetail::BlobGcManager_(*this).runBlobGc();
}
