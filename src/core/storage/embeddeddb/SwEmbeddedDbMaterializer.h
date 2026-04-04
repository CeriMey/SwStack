namespace swEmbeddedDbDetail {

class Materializer_ {
public:
    explicit Materializer_(SwEmbeddedDb& db)
        : db_(db) {
    }

    bool materializePrimaryLocked(SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>& mergedPrimary,
                                  bool resolveValues) {
        mergedPrimary.clear();
        SwList<swEmbeddedDbDetail::TableMeta_> tables = db_.manifest_.tables;
        std::sort(tables.begin(), tables.end(), swEmbeddedDbDetail::compareTableMetaOldestFirst_);
        for (std::size_t i = 0; i < tables.size(); ++i) {
            if (tables[i].kind != swEmbeddedDbDetail::TableKindPrimary || !db_.tableHandles_.contains(tables[i].fileName)) {
                continue;
            }
            SwList<swEmbeddedDbDetail::TableRecord_> records;
            if (!db_.tableHandles_.value(tables[i].fileName)->iterateAll(records)) {
                return false;
            }
            for (std::size_t j = 0; j < records.size(); ++j) {
                swEmbeddedDbDetail::PrimaryRecord_ record;
                record.deleted = (records[j].flags & swEmbeddedDbDetail::RecordDeleted) != 0u;
                record.sequence = records[j].sequence;
                if (!record.deleted && !swEmbeddedDbDetail::decodePrimaryPayload_(records[j].payload, record)) {
                    return false;
                }
                SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::iterator existing =
                    mergedPrimary.find(records[j].userKey);
                if (existing == mergedPrimary.end() || existing.value().sequence < record.sequence) {
                    mergedPrimary[records[j].userKey] = record;
                }
            }
        }
        for (std::size_t i = 0; i < db_.immutables_.size(); ++i) {
            const SwList<SwByteArray>& orderedPrimaryKeys = db_.immutables_[i].orderedPrimaryKeys();
            for (std::size_t j = 0; j < orderedPrimaryKeys.size(); ++j) {
                const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it =
                    db_.immutables_[i].primary.find(orderedPrimaryKeys[j]);
                if (it == db_.immutables_[i].primary.end()) {
                    continue;
                }
                SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::iterator existing =
                    mergedPrimary.find(orderedPrimaryKeys[j]);
                if (existing == mergedPrimary.end() || existing.value().sequence < it->second.sequence) {
                    mergedPrimary[orderedPrimaryKeys[j]] = it->second;
                }
            }
        }
        const SwList<SwByteArray>& orderedMutablePrimaryKeys = db_.mutable_.orderedPrimaryKeys();
        for (std::size_t i = 0; i < orderedMutablePrimaryKeys.size(); ++i) {
            const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it =
                db_.mutable_.primary.find(orderedMutablePrimaryKeys[i]);
            if (it == db_.mutable_.primary.end()) {
                continue;
            }
            SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::iterator existing =
                mergedPrimary.find(orderedMutablePrimaryKeys[i]);
            if (existing == mergedPrimary.end() || existing.value().sequence < it->second.sequence) {
                mergedPrimary[orderedMutablePrimaryKeys[i]] = it->second;
            }
        }

        SwList<SwByteArray> toRemove;
        for (SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>::iterator it = mergedPrimary.begin();
             it != mergedPrimary.end();
             ++it) {
            if (it.value().deleted) {
                toRemove.append(it.key());
                continue;
            }
            if (resolveValues && !db_.resolveValueLocked_(it.value())) {
                return false;
            }
        }
        for (std::size_t i = 0; i < toRemove.size(); ++i) {
            mergedPrimary.remove(toRemove[i]);
        }
        return true;
    }

    bool buildReadModelLocked(std::shared_ptr<swEmbeddedDbDetail::ReadModel_>& outReadModel) {
        outReadModel.reset();
        const std::chrono::steady_clock::time_point buildStart = std::chrono::steady_clock::now();

        std::shared_ptr<swEmbeddedDbDetail::ReadModel_> readModel(new swEmbeddedDbDetail::ReadModel_());
        readModel->visibleSequence = db_.lastVisibleSequence_;

        swEmbeddedDbDetail::PrimaryMemStore_ mergedPrimary;
        unsigned long long primaryRecordHint = 0;
        for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
            if (db_.manifest_.tables[i].kind == swEmbeddedDbDetail::TableKindPrimary) {
                primaryRecordHint += db_.manifest_.tables[i].recordCount;
            }
        }
        mergedPrimary.reserve(static_cast<std::size_t>(
            std::min<unsigned long long>(std::max<unsigned long long>(primaryRecordHint, 1024ull), 131072ull)));

        const std::chrono::steady_clock::time_point mergeStart = std::chrono::steady_clock::now();
        SwList<swEmbeddedDbDetail::TableMeta_> tables = db_.manifest_.tables;
        std::sort(tables.begin(), tables.end(), swEmbeddedDbDetail::compareTableMetaOldestFirst_);
        for (std::size_t i = 0; i < tables.size(); ++i) {
            if (tables[i].kind != swEmbeddedDbDetail::TableKindPrimary || !db_.tableHandles_.contains(tables[i].fileName)) {
                continue;
            }
            SwList<swEmbeddedDbDetail::TableRecord_> records;
            if (!db_.tableHandles_.value(tables[i].fileName)->iterateAll(records)) {
                return false;
            }
            for (std::size_t j = 0; j < records.size(); ++j) {
                swEmbeddedDbDetail::PrimaryRecord_ record;
                record.deleted = (records[j].flags & swEmbeddedDbDetail::RecordDeleted) != 0u;
                record.sequence = records[j].sequence;
                if (!record.deleted && !swEmbeddedDbDetail::decodePrimaryPayload_(records[j].payload, record)) {
                    return false;
                }
                const swEmbeddedDbDetail::PrimaryMemStore_::iterator existing =
                    mergedPrimary.find(records[j].userKey);
                if (existing == mergedPrimary.end() || existing->second.sequence < record.sequence) {
                    mergedPrimary[records[j].userKey] = std::move(record);
                }
            }
        }
        for (std::size_t i = 0; i < db_.immutables_.size(); ++i) {
            const SwList<SwByteArray>& orderedPrimaryKeys = db_.immutables_[i].orderedPrimaryKeys();
            for (std::size_t j = 0; j < orderedPrimaryKeys.size(); ++j) {
                const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it =
                    db_.immutables_[i].primary.find(orderedPrimaryKeys[j]);
                if (it == db_.immutables_[i].primary.end()) {
                    continue;
                }
                const swEmbeddedDbDetail::PrimaryMemStore_::iterator existing =
                    mergedPrimary.find(orderedPrimaryKeys[j]);
                if (existing == mergedPrimary.end() || existing->second.sequence < it->second.sequence) {
                    mergedPrimary[orderedPrimaryKeys[j]] = it->second;
                }
            }
        }
        const SwList<SwByteArray>& orderedMutablePrimaryKeys = db_.mutable_.orderedPrimaryKeys();
        for (std::size_t i = 0; i < orderedMutablePrimaryKeys.size(); ++i) {
            const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it =
                db_.mutable_.primary.find(orderedMutablePrimaryKeys[i]);
            if (it == db_.mutable_.primary.end()) {
                continue;
            }
            const swEmbeddedDbDetail::PrimaryMemStore_::iterator existing =
                mergedPrimary.find(orderedMutablePrimaryKeys[i]);
            if (existing == mergedPrimary.end() || existing->second.sequence < it->second.sequence) {
                mergedPrimary[orderedMutablePrimaryKeys[i]] = it->second;
            }
        }
        const unsigned long long mergeMicros =
            static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                std::chrono::steady_clock::now() - mergeStart)
                                                .count());

        const std::chrono::steady_clock::time_point sortStart = std::chrono::steady_clock::now();
        SwList<SwByteArray> orderedPrimaryKeys = mergedPrimary.keys();
        std::sort(orderedPrimaryKeys.begin(), orderedPrimaryKeys.end(), [](const SwByteArray& lhs, const SwByteArray& rhs) {
            return lhs < rhs;
        });
        const unsigned long long sortMicros =
            static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                std::chrono::steady_clock::now() - sortStart)
                                                .count());

        swEmbeddedDbDetail::ReadPrimarySegment_ primarySegment;
        primarySegment.rows.reserve(swEmbeddedDbDetail::ReadModel_::rowsPerSegment());

        SwHash<SwString, SwList<swEmbeddedDbDetail::ReadIndexRow_> > pendingIndexRows;
        pendingIndexRows.reserve(32u);
        unsigned long long primaryRowCount = 0;
        unsigned long long indexRowCount = 0;
        for (std::size_t i = 0; i < orderedPrimaryKeys.size(); ++i) {
            const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator it = mergedPrimary.find(orderedPrimaryKeys[i]);
            if (it == mergedPrimary.end() || it->second.deleted) {
                continue;
            }
            swEmbeddedDbDetail::ReadPrimaryRow_ primaryRow;
            primaryRow.primaryKey = orderedPrimaryKeys[i];
            primaryRow.record = it->second;

            const unsigned int primarySegmentIndex =
                static_cast<unsigned int>(readModel->primarySegments.size());
            const unsigned int primaryRowIndex =
                static_cast<unsigned int>(primarySegment.rows.size());
            primarySegment.approximateBytes += swEmbeddedDbDetail::estimatePrimaryRowBytes_(primaryRow);
            primarySegment.rows.append(std::move(primaryRow));
            primaryRowCount += 1;
            if (primarySegment.rows.size() >= swEmbeddedDbDetail::ReadModel_::rowsPerSegment()) {
                swEmbeddedDbDetail::finalizePrimarySegment_(primarySegment, *readModel);
                primarySegment.rows.reserve(swEmbeddedDbDetail::ReadModel_::rowsPerSegment());
            }

            for (SwMap<SwString, SwList<SwByteArray> >::const_iterator indexIt = it->second.secondaryKeys.begin();
                 indexIt != it->second.secondaryKeys.end();
                 ++indexIt) {
                SwList<swEmbeddedDbDetail::ReadIndexRow_>& indexRows = pendingIndexRows[indexIt.key()];
                for (std::size_t keyIndex = 0; keyIndex < indexIt.value().size(); ++keyIndex) {
                    swEmbeddedDbDetail::ReadIndexRow_ indexRow;
                    indexRow.secondaryKey = indexIt.value()[keyIndex];
                    indexRow.compositeKey =
                        swEmbeddedDbDetail::encodeIndexCompositeKey_(indexRow.secondaryKey, orderedPrimaryKeys[i]);
                    indexRow.primarySegmentIndex = primarySegmentIndex;
                    indexRow.primaryRowIndex = primaryRowIndex;
                    indexRows.append(std::move(indexRow));
                    indexRowCount += 1;
                }
            }
        }
        swEmbeddedDbDetail::finalizePrimarySegment_(primarySegment, *readModel);

        SwList<SwString> indexNames = pendingIndexRows.keys();
        std::sort(indexNames.begin(), indexNames.end(), [](const SwString& lhs, const SwString& rhs) {
            return lhs.toStdString() < rhs.toStdString();
        });
        for (std::size_t i = 0; i < indexNames.size(); ++i) {
            swEmbeddedDbDetail::ReadIndexSegment_ indexSegment;
            indexSegment.rows.reserve(swEmbeddedDbDetail::ReadModel_::rowsPerSegment());
            SwList<swEmbeddedDbDetail::ReadIndexRow_>& indexRows = pendingIndexRows[indexNames[i]];
            std::sort(indexRows.begin(), indexRows.end(), [](const swEmbeddedDbDetail::ReadIndexRow_& lhs,
                                                             const swEmbeddedDbDetail::ReadIndexRow_& rhs) {
                return lhs.compositeKey < rhs.compositeKey;
            });
            for (std::size_t rowIndex = 0; rowIndex < indexRows.size(); ++rowIndex) {
                indexSegment.approximateBytes += swEmbeddedDbDetail::estimateIndexRowBytes_(indexRows[rowIndex]);
                indexSegment.rows.append(std::move(indexRows[rowIndex]));
                if (indexSegment.rows.size() >= swEmbeddedDbDetail::ReadModel_::rowsPerSegment()) {
                    swEmbeddedDbDetail::finalizeIndexSegment_(indexNames[i], indexSegment, *readModel);
                    indexSegment.rows.reserve(swEmbeddedDbDetail::ReadModel_::rowsPerSegment());
                }
            }
            swEmbeddedDbDetail::finalizeIndexSegment_(indexNames[i], indexSegment, *readModel);
        }

        db_.metrics_.readModelMergeMicros = mergeMicros;
        db_.metrics_.readModelSortMicros = sortMicros;
        db_.metrics_.readModelPrimaryRowCount = primaryRowCount;
        db_.metrics_.readModelIndexRowCount = indexRowCount;
        db_.metrics_.readModelBuildMicros =
            static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                std::chrono::steady_clock::now() - buildStart)
                                                .count());
        outReadModel = readModel;
        return true;
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline bool SwEmbeddedDb::materializePrimaryLocked_(
    SwMap<SwByteArray, swEmbeddedDbDetail::PrimaryRecord_>& mergedPrimary) {
    return swEmbeddedDbDetail::Materializer_(*this).materializePrimaryLocked(mergedPrimary, true);
}

inline bool SwEmbeddedDb::buildReadModelLocked_(
    std::shared_ptr<swEmbeddedDbDetail::ReadModel_>& outReadModel) {
    return swEmbeddedDbDetail::Materializer_(*this).buildReadModelLocked(outReadModel);
}
