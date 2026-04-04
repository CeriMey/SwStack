namespace swEmbeddedDbDetail {

class CompactionManager_ {
public:
    explicit CompactionManager_(SwEmbeddedDb& db)
        : db_(db) {
    }

    void compactL0Locked() {
        static const std::size_t kL0CompactionTrigger = 4u;

        if (db_.compactionScheduled_ || db_.closing_) {
            return;
        }

        SwList<swEmbeddedDbDetail::TableMeta_> primaryL0;
        SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexL0;
        for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
            if (db_.manifest_.tables[i].level != 0) {
                continue;
            }
            if (db_.manifest_.tables[i].kind == swEmbeddedDbDetail::TableKindPrimary) {
                primaryL0.append(db_.manifest_.tables[i]);
            } else if (db_.manifest_.tables[i].kind == swEmbeddedDbDetail::TableKindIndex) {
                indexL0[db_.manifest_.tables[i].indexName].append(db_.manifest_.tables[i]);
            }
        }

        bool shouldCompact = primaryL0.size() >= kL0CompactionTrigger;
        const SwList<SwString> indexNames = indexL0.keys();
        for (std::size_t i = 0; i < indexNames.size(); ++i) {
            if (indexL0.value(indexNames[i]).size() >= kL0CompactionTrigger) {
                shouldCompact = true;
                break;
            }
        }
        if (!shouldCompact) {
            return;
        }

        db_.compactionScheduled_ = true;
        SwEmbeddedDb* db = &db_;
        db_.backgroundPool_.start([db, primaryL0, indexL0]() { db->runL0Compaction_(primaryL0, indexL0); });
    }

    void runL0Compaction(SwList<swEmbeddedDbDetail::TableMeta_> primaryL0,
                         SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexL0) {
        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_) {
                db_.compactionScheduled_ = false;
                return;
            }
        }

        SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> handles;
        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_) {
                db_.compactionScheduled_ = false;
                return;
            }
            for (std::size_t i = 0; i < primaryL0.size(); ++i) {
                if (db_.tableHandles_.contains(primaryL0[i].fileName)) {
                    handles[primaryL0[i].fileName] = db_.tableHandles_.value(primaryL0[i].fileName);
                }
            }
            const SwList<SwString> names = indexL0.keys();
            for (std::size_t i = 0; i < names.size(); ++i) {
                const SwList<swEmbeddedDbDetail::TableMeta_>& metas = indexL0.value(names[i]);
                for (std::size_t j = 0; j < metas.size(); ++j) {
                    if (db_.tableHandles_.contains(metas[j].fileName)) {
                        handles[metas[j].fileName] = db_.tableHandles_.value(metas[j].fileName);
                    }
                }
            }
        }

        SwList<swEmbeddedDbDetail::TableMeta_> newTables;
        SwList<swEmbeddedDbDetail::TableMeta_> toRemove;

        if (!primaryL0.isEmpty()) {
            SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_> latest;
            unsigned long long minSequence = 0;
            unsigned long long maxSequence = 0;

            for (std::size_t i = 0; i < primaryL0.size(); ++i) {
                if (db_.closing_) {
                    std::lock_guard<std::mutex> lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    return;
                }
                if (!handles.contains(primaryL0[i].fileName)) {
                    continue;
                }
                SwList<swEmbeddedDbDetail::TableRecord_> records;
                if (!handles.value(primaryL0[i].fileName)->iterateAll(records)) {
                    std::lock_guard<std::mutex> lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    return;
                }
                for (std::size_t j = 0; j < records.size(); ++j) {
                    swEmbeddedDbDetail::PrimaryRecord_ record;
                    record.deleted = (records[j].flags & swEmbeddedDbDetail::RecordDeleted) != 0u;
                    record.sequence = records[j].sequence;
                    minSequence = (minSequence == 0) ? record.sequence : std::min(minSequence, record.sequence);
                    maxSequence = std::max(maxSequence, record.sequence);
                    if (!record.deleted && !swEmbeddedDbDetail::decodePrimaryPayload_(records[j].payload, record)) {
                        std::lock_guard<std::mutex> lock(db_.mutex_);
                        db_.compactionScheduled_ = false;
                        return;
                    }
                    if (!latest.contains(records[j].userKey) ||
                        latest.value(records[j].userKey).sequence < record.sequence) {
                        latest[records[j].userKey] = record;
                    }
                }
            }

            if (!latest.isEmpty()) {
                swEmbeddedDbDetail::TableMeta_ meta;
                meta.kind = swEmbeddedDbDetail::TableKindPrimary;
                meta.level = 1;
                meta.fileId = db_.nextTableIdThreadSafe_();
                meta.fileName = SwString("L1-") + SwString::number(meta.fileId) + ".sst";
                meta.minSequence = minSequence;
                meta.maxSequence = maxSequence;

                SwList<swEmbeddedDbDetail::TableRecord_> records;
                for (SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::const_iterator it = latest.begin();
                     it != latest.end();
                     ++it) {
                    swEmbeddedDbDetail::TableRecord_ raw;
                    raw.userKey = it.key();
                    raw.sequence = it.value().sequence;
                    raw.flags = it.value().deleted ? swEmbeddedDbDetail::RecordDeleted : 0u;
                    if (!it.value().deleted) {
                        if (!it.value().inlineValue) {
                            raw.flags |= swEmbeddedDbDetail::RecordBlobRef;
                        }
                        swEmbeddedDbDetail::encodePrimaryPayload_(it.value(), raw.payload);
                    }
                    records.append(raw);
                }
                meta.recordCount = static_cast<unsigned long long>(records.size());
                const SwDbStatus status = db_.writeTableFile_(meta, records);
                if (!status.ok()) {
                    std::lock_guard<std::mutex> lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
                    return;
                }
                newTables.append(meta);
                toRemove.append(primaryL0.begin(), primaryL0.end());
            }
        }

        SwList<SwString> indexNames = indexL0.keys();
        std::sort(indexNames.begin(), indexNames.end(), [](const SwString& lhs, const SwString& rhs) {
            return lhs.toStdString() < rhs.toStdString();
        });
        for (std::size_t i = 0; i < indexNames.size(); ++i) {
            if (db_.closing_) {
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                return;
            }
            SwMap<SwByteArray, swEmbeddedDbDetail::SecondaryEntry_> latest;
            unsigned long long minSequence = 0;
            unsigned long long maxSequence = 0;
            const SwList<swEmbeddedDbDetail::TableMeta_>& metas = indexL0.value(indexNames[i]);
            for (std::size_t j = 0; j < metas.size(); ++j) {
                if (!handles.contains(metas[j].fileName)) {
                    continue;
                }
                SwList<swEmbeddedDbDetail::TableRecord_> records;
                if (!handles.value(metas[j].fileName)->iterateAll(records)) {
                    std::lock_guard<std::mutex> lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    return;
                }
                for (std::size_t k = 0; k < records.size(); ++k) {
                    swEmbeddedDbDetail::SecondaryEntry_ entry;
                    entry.deleted = (records[k].flags & swEmbeddedDbDetail::RecordDeleted) != 0u;
                    entry.sequence = records[k].sequence;
                    minSequence = (minSequence == 0) ? entry.sequence : std::min(minSequence, entry.sequence);
                    maxSequence = std::max(maxSequence, entry.sequence);
                    if (!latest.contains(records[k].userKey) ||
                        latest.value(records[k].userKey).sequence < entry.sequence) {
                        latest[records[k].userKey] = entry;
                    }
                }
            }

            if (latest.isEmpty()) {
                continue;
            }

            swEmbeddedDbDetail::TableMeta_ meta;
            meta.kind = swEmbeddedDbDetail::TableKindIndex;
            meta.level = 1;
            meta.fileId = db_.nextTableIdThreadSafe_();
            meta.fileName = SwString("L1-") + SwString::number(meta.fileId) + ".sst";
            meta.indexName = indexNames[i];
            meta.minSequence = minSequence;
            meta.maxSequence = maxSequence;

            SwList<swEmbeddedDbDetail::TableRecord_> records;
            for (SwMap<SwByteArray, swEmbeddedDbDetail::SecondaryEntry_>::const_iterator it = latest.begin();
                 it != latest.end();
                 ++it) {
                swEmbeddedDbDetail::TableRecord_ raw;
                raw.userKey = it.key();
                raw.sequence = it.value().sequence;
                raw.flags = it.value().deleted ? swEmbeddedDbDetail::RecordDeleted : 0u;
                records.append(raw);
            }
            meta.recordCount = static_cast<unsigned long long>(records.size());
            const SwDbStatus status = db_.writeTableFile_(meta, records);
            if (!status.ok()) {
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
                return;
            }
            newTables.append(meta);
            toRemove.append(metas.begin(), metas.end());
        }

        SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> openedHandles;
        for (std::size_t i = 0; i < newTables.size(); ++i) {
            if (db_.closing_) {
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                return;
            }
            std::shared_ptr<swEmbeddedDbDetail::TableHandle_> handle(new swEmbeddedDbDetail::TableHandle_());
            SwDbStatus status;
            if (!handle->open(db_.dbPath_, newTables[i], db_.options_, db_.readCacheManager_, status)) {
                std::lock_guard<std::mutex> lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
                return;
            }
            openedHandles[newTables[i].fileName] = handle;
        }

        bool persisted = false;
        {
            std::lock_guard<std::mutex> lock(db_.mutex_);
            if (db_.closing_) {
                db_.compactionScheduled_ = false;
                return;
            }
            swEmbeddedDbDetail::Manifest_ backupManifest = db_.manifest_;
            SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> backupHandles = db_.tableHandles_;

            db_.removeTablesLocked_(toRemove);
            for (std::size_t i = 0; i < newTables.size(); ++i) {
                db_.manifest_.tables.append(newTables[i]);
                db_.tableHandles_[newTables[i].fileName] = openedHandles.value(newTables[i].fileName);
            }
            db_.rebuildTableCachesLocked_();

            const SwDbStatus persistStatus = db_.persistManifestLocked_();
            if (!persistStatus.ok()) {
                db_.manifest_ = backupManifest;
                db_.tableHandles_ = backupHandles;
                db_.rebuildTableCachesLocked_();
                db_.compactionScheduled_ = false;
                swCError(kSwLogCategory_SwEmbeddedDb) << persistStatus.message();
                return;
            }

            if (!newTables.isEmpty()) {
                db_.metrics_.compactionCount += 1;
            }
            db_.compactionScheduled_ = false;
            persisted = true;
            compactL0Locked();
            db_.scheduleBlobGcLocked_();
        }

        if (persisted) {
            for (std::size_t i = 0; i < toRemove.size(); ++i) {
                (void)swDbPlatform::removeFile(swDbPlatform::joinPath(db_.tableDir_, toRemove[i].fileName));
            }
        }
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline void SwEmbeddedDb::compactL0Locked_() {
    swEmbeddedDbDetail::CompactionManager_(*this).compactL0Locked();
}

inline void SwEmbeddedDb::runL0Compaction_(SwList<swEmbeddedDbDetail::TableMeta_> primaryL0,
                                           SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexL0) {
    swEmbeddedDbDetail::CompactionManager_(*this).runL0Compaction(primaryL0, indexL0);
}
