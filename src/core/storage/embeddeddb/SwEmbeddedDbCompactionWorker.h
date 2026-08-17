namespace swEmbeddedDbDetail {

// Deux regimes de fusion par groupe (primaire, ou index par nom) :
// - L0 >= kL0CompactionTrigger : les L0 du groupe fusionnent en un L1 ;
// - L1 >= kL1CompactionTrigger : le groupe ENTIER (L0+L1) fusionne en un seul
//   L1 — c'est la seule passe qui reecrit les gros fichiers, l'amortir limite
//   l'amplification d'ecriture.
// Les tombstones ne sont abandonnes que lorsqu'une passe couvre la TOTALITE
// des L1 du groupe (aucune version plus ancienne d'une cle ne peut alors
// survivre hors de l'ensemble fusionne : un L0 apparu apres la collecte est
// strictement plus recent et prime a la lecture). Le nombre de fichiers par
// groupe reste ainsi borne (< kL0 + kL1) pour toute la vie de la base.
class CompactionManager_ {
public:
    explicit CompactionManager_(SwEmbeddedDb& db)
        : db_(db) {
    }

    void compactL0Locked() {
        static const std::size_t kL0CompactionTrigger = 4u;
        static const std::size_t kL1CompactionTrigger = 4u;

        if (db_.compactionScheduled_ || db_.closing_) {
            return;
        }

        SwList<swEmbeddedDbDetail::TableMeta_> primaryL0;
        SwList<swEmbeddedDbDetail::TableMeta_> primaryL1;
        SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexL0;
        SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexL1;
        for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
            const swEmbeddedDbDetail::TableMeta_& meta = db_.manifest_.tables[i];
            if (meta.level > 1) {
                continue;
            }
            if (meta.kind == swEmbeddedDbDetail::TableKindPrimary) {
                (meta.level == 0 ? primaryL0 : primaryL1).append(meta);
            } else if (meta.kind == swEmbeddedDbDetail::TableKindIndex) {
                (meta.level == 0 ? indexL0 : indexL1)[meta.indexName].append(meta);
            }
        }

        SwList<swEmbeddedDbDetail::TableMeta_> primaryMerge;
        if (primaryL0.size() >= kL0CompactionTrigger || primaryL1.size() >= kL1CompactionTrigger) {
            primaryMerge = primaryL0;
            if (primaryL1.size() >= kL1CompactionTrigger || primaryL1.isEmpty()) {
                primaryMerge.append(primaryL1.begin(), primaryL1.end());
            }
        }
        if (primaryMerge.size() < 2u) {
            primaryMerge.clear();
        }

        SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexMerge;
        SwList<SwString> indexNames = indexL0.keys();
        {
            const SwList<SwString> l1Names = indexL1.keys();
            for (std::size_t i = 0; i < l1Names.size(); ++i) {
                if (!indexL0.contains(l1Names[i])) {
                    indexNames.append(l1Names[i]);
                }
            }
        }
        for (std::size_t i = 0; i < indexNames.size(); ++i) {
            const SwList<swEmbeddedDbDetail::TableMeta_>& l0 = indexL0.value(indexNames[i]);
            const SwList<swEmbeddedDbDetail::TableMeta_>& l1 = indexL1.value(indexNames[i]);
            if (l0.size() < kL0CompactionTrigger && l1.size() < kL1CompactionTrigger) {
                continue;
            }
            SwList<swEmbeddedDbDetail::TableMeta_> merge = l0;
            if (l1.size() >= kL1CompactionTrigger || l1.isEmpty()) {
                merge.append(l1.begin(), l1.end());
            }
            if (merge.size() < 2u) {
                continue;
            }
            indexMerge[indexNames[i]] = merge;
        }

        if (primaryMerge.isEmpty() && indexMerge.isEmpty()) {
            return;
        }

        db_.compactionScheduled_ = true;
        SwEmbeddedDb* db = &db_;
        db_.backgroundPool_.start([db, primaryMerge, indexMerge]() { db->runL0Compaction_(primaryMerge, indexMerge); });
    }

    void runL0Compaction(SwList<swEmbeddedDbDetail::TableMeta_> primaryTables,
                         SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexTables) {
        {
            SwEmbeddedDbLock_ lock(db_.mutex_);
            if (db_.closing_) {
                db_.compactionScheduled_ = false;
                return;
            }
        }

        SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> handles;
        bool primaryCoversAllL1 = false;
        SwHash<SwString, bool> indexCoversAllL1;
        {
            SwEmbeddedDbLock_ lock(db_.mutex_);
            if (db_.closing_) {
                db_.compactionScheduled_ = false;
                return;
            }
            for (std::size_t i = 0; i < primaryTables.size(); ++i) {
                if (db_.tableHandles_.contains(primaryTables[i].fileName)) {
                    handles[primaryTables[i].fileName] = db_.tableHandles_.value(primaryTables[i].fileName);
                }
            }
            const SwList<SwString> names = indexTables.keys();
            for (std::size_t i = 0; i < names.size(); ++i) {
                const SwList<swEmbeddedDbDetail::TableMeta_>& metas = indexTables.value(names[i]);
                for (std::size_t j = 0; j < metas.size(); ++j) {
                    if (db_.tableHandles_.contains(metas[j].fileName)) {
                        handles[metas[j].fileName] = db_.tableHandles_.value(metas[j].fileName);
                    }
                }
            }

            // L'abandon des tombstones exige que la passe couvre TOUS les L1
            // du groupe : une seule autre passe ne peut pas courir (le verrou
            // compactionScheduled_ est global) et les flushs concurrents ne
            // produisent que des L0, le manifest fait donc foi ici.
            SwHash<SwString, bool> mergedNames;
            for (std::size_t i = 0; i < primaryTables.size(); ++i) {
                mergedNames[primaryTables[i].fileName] = true;
            }
            primaryCoversAllL1 = !primaryTables.isEmpty();
            for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
                const swEmbeddedDbDetail::TableMeta_& meta = db_.manifest_.tables[i];
                if (meta.kind == swEmbeddedDbDetail::TableKindPrimary && meta.level == 1 &&
                    !mergedNames.contains(meta.fileName)) {
                    primaryCoversAllL1 = false;
                    break;
                }
            }
            for (std::size_t i = 0; i < names.size(); ++i) {
                SwHash<SwString, bool> groupNames;
                const SwList<swEmbeddedDbDetail::TableMeta_>& metas = indexTables.value(names[i]);
                for (std::size_t j = 0; j < metas.size(); ++j) {
                    groupNames[metas[j].fileName] = true;
                }
                bool coversAll = true;
                for (std::size_t j = 0; j < db_.manifest_.tables.size(); ++j) {
                    const swEmbeddedDbDetail::TableMeta_& meta = db_.manifest_.tables[j];
                    if (meta.kind == swEmbeddedDbDetail::TableKindIndex && meta.level == 1 &&
                        meta.indexName == names[i] && !groupNames.contains(meta.fileName)) {
                        coversAll = false;
                        break;
                    }
                }
                indexCoversAllL1[names[i]] = coversAll;
            }
        }

        SwList<swEmbeddedDbDetail::TableMeta_> newTables;
        SwList<swEmbeddedDbDetail::TableMeta_> toRemove;

        if (!primaryTables.isEmpty()) {
            SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_> latest;
            unsigned long long minSequence = 0;
            unsigned long long maxSequence = 0;
            SwList<swEmbeddedDbDetail::TableMeta_> readTables;
            bool groupComplete = true;

            for (std::size_t i = 0; i < primaryTables.size(); ++i) {
                if (db_.closing_) {
                    SwEmbeddedDbLock_ lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    return;
                }
                if (!handles.contains(primaryTables[i].fileName)) {
                    groupComplete = false;
                    continue;
                }
                SwList<swEmbeddedDbDetail::TableRecord_> records;
                if (!handles.value(primaryTables[i].fileName)->iterateAll(records)) {
                    SwEmbeddedDbLock_ lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    return;
                }
                readTables.append(primaryTables[i]);
                for (std::size_t j = 0; j < records.size(); ++j) {
                    swEmbeddedDbDetail::PrimaryRecord_ record;
                    record.deleted = (records[j].flags & swEmbeddedDbDetail::RecordDeleted) != 0u;
                    record.sequence = records[j].sequence;
                    minSequence = (minSequence == 0) ? record.sequence : std::min(minSequence, record.sequence);
                    maxSequence = std::max(maxSequence, record.sequence);
                    if (!record.deleted && !swEmbeddedDbDetail::decodePrimaryPayload_(records[j].payload, record)) {
                        SwEmbeddedDbLock_ lock(db_.mutex_);
                        db_.compactionScheduled_ = false;
                        return;
                    }
                    if (!latest.contains(records[j].userKey) ||
                        latest.value(records[j].userKey).sequence < record.sequence) {
                        latest[records[j].userKey] = record;
                    }
                }
            }

            const bool dropDeleted = groupComplete && primaryCoversAllL1;
            SwList<swEmbeddedDbDetail::TableRecord_> records;
            for (SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::const_iterator it = latest.begin();
                 it != latest.end();
                 ++it) {
                if (dropDeleted && it.value().deleted) {
                    continue;
                }
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

            if (!records.isEmpty()) {
                swEmbeddedDbDetail::TableMeta_ meta;
                meta.kind = swEmbeddedDbDetail::TableKindPrimary;
                meta.level = 1;
                meta.fileId = db_.nextTableIdThreadSafe_();
                meta.fileName = SwString("L1-") + SwString::number(meta.fileId) + ".sst";
                meta.minSequence = minSequence;
                meta.maxSequence = maxSequence;
                meta.recordCount = static_cast<unsigned long long>(records.size());
                const SwDbStatus status = db_.writeTableFile_(meta, records);
                if (!status.ok()) {
                    SwEmbeddedDbLock_ lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
                    return;
                }
                newTables.append(meta);
            }
            toRemove.append(readTables.begin(), readTables.end());
        }

        SwList<SwString> indexNames = indexTables.keys();
        std::sort(indexNames.begin(), indexNames.end(), [](const SwString& lhs, const SwString& rhs) {
            return lhs.toStdString() < rhs.toStdString();
        });
        for (std::size_t i = 0; i < indexNames.size(); ++i) {
            if (db_.closing_) {
                SwEmbeddedDbLock_ lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                return;
            }
            SwMap<SwByteArray, swEmbeddedDbDetail::SecondaryEntry_> latest;
            unsigned long long minSequence = 0;
            unsigned long long maxSequence = 0;
            SwList<swEmbeddedDbDetail::TableMeta_> readTables;
            bool groupComplete = true;
            const SwList<swEmbeddedDbDetail::TableMeta_>& metas = indexTables.value(indexNames[i]);
            for (std::size_t j = 0; j < metas.size(); ++j) {
                if (!handles.contains(metas[j].fileName)) {
                    groupComplete = false;
                    continue;
                }
                SwList<swEmbeddedDbDetail::TableRecord_> records;
                if (!handles.value(metas[j].fileName)->iterateAll(records)) {
                    SwEmbeddedDbLock_ lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    return;
                }
                readTables.append(metas[j]);
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

            const bool dropDeleted = groupComplete && indexCoversAllL1.value(indexNames[i]);
            SwList<swEmbeddedDbDetail::TableRecord_> records;
            for (SwMap<SwByteArray, swEmbeddedDbDetail::SecondaryEntry_>::const_iterator it = latest.begin();
                 it != latest.end();
                 ++it) {
                if (dropDeleted && it.value().deleted) {
                    continue;
                }
                swEmbeddedDbDetail::TableRecord_ raw;
                raw.userKey = it.key();
                raw.sequence = it.value().sequence;
                raw.flags = it.value().deleted ? swEmbeddedDbDetail::RecordDeleted : 0u;
                records.append(raw);
            }

            if (!records.isEmpty()) {
                swEmbeddedDbDetail::TableMeta_ meta;
                meta.kind = swEmbeddedDbDetail::TableKindIndex;
                meta.level = 1;
                meta.fileId = db_.nextTableIdThreadSafe_();
                meta.fileName = SwString("L1-") + SwString::number(meta.fileId) + ".sst";
                meta.indexName = indexNames[i];
                meta.minSequence = minSequence;
                meta.maxSequence = maxSequence;
                meta.recordCount = static_cast<unsigned long long>(records.size());
                const SwDbStatus status = db_.writeTableFile_(meta, records);
                if (!status.ok()) {
                    SwEmbeddedDbLock_ lock(db_.mutex_);
                    db_.compactionScheduled_ = false;
                    swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
                    return;
                }
                newTables.append(meta);
            }
            toRemove.append(readTables.begin(), readTables.end());
        }

        SwHash<SwString, std::shared_ptr<swEmbeddedDbDetail::TableHandle_>> openedHandles;
        for (std::size_t i = 0; i < newTables.size(); ++i) {
            if (db_.closing_) {
                SwEmbeddedDbLock_ lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                return;
            }
            std::shared_ptr<swEmbeddedDbDetail::TableHandle_> handle(new swEmbeddedDbDetail::TableHandle_());
            SwDbStatus status;
            if (!handle->open(db_.dbPath_, newTables[i], db_.options_, db_.readCacheManager_, status)) {
                SwEmbeddedDbLock_ lock(db_.mutex_);
                db_.compactionScheduled_ = false;
                swCError(kSwLogCategory_SwEmbeddedDb) << status.message();
                return;
            }
            openedHandles[newTables[i].fileName] = handle;
        }

        bool persisted = false;
        {
            SwEmbeddedDbLock_ lock(db_.mutex_);
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

            if (!newTables.isEmpty() || !toRemove.isEmpty()) {
                db_.metrics_.compactionCount += 1;
            }
            db_.compactionScheduled_ = false;
            persisted = true;
            compactL0Locked();
            db_.scheduleBlobGcLocked_();
        }

        if (persisted) {
            // Les vues mappees des tables fusionnees doivent etre relachees
            // avant la suppression : Windows refuse d'effacer un fichier dont
            // une vue est active (l'echec silencieux laisserait des orphelins
            // a chaque compaction ; le balayage d'orphelins de l'open n'est
            // que le filet de securite).
            handles.clear();
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

inline void SwEmbeddedDb::runL0Compaction_(SwList<swEmbeddedDbDetail::TableMeta_> primaryTables,
                                           SwHash<SwString, SwList<swEmbeddedDbDetail::TableMeta_> > indexTables) {
    swEmbeddedDbDetail::CompactionManager_(*this).runL0Compaction(primaryTables, indexTables);
}
