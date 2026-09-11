namespace swEmbeddedDbDetail {

inline bool materializeReadEntry_(const std::shared_ptr<SnapshotState_>& snapshot,
                                  const ReadPrimaryRow_& row,
                                  const SwByteArray& secondaryKey,
                                  SwDbEntry& outEntry) {
    if (!snapshot) {
        return false;
    }
    PrimaryRecord_ record = row.record;
    if (!snapshot->resolveValue(record)) {
        return false;
    }
    outEntry.primaryKey = row.primaryKey;
    outEntry.secondaryKey = secondaryKey;
    outEntry.value = record.value;
    outEntry.secondaryKeys = record.secondaryKeys;
    outEntry.sequence = record.sequence;
    return true;
}

class ReadModelPrimaryIteratorState_ : public IteratorState_ {
public:
    ReadModelPrimaryIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                   const std::shared_ptr<ReadModel_>& readModel,
                                   const SwByteArray& startKey,
                                   const SwByteArray& endKey)
        : snapshot_(snapshot),
          readModel_(readModel),
          endKey_(endKey) {
        locateStart_(startKey);
    }

    bool next(SwDbEntry& outEntry) override {
        while (readModel_ &&
               segmentIndex_ < readModel_->primarySegments.size()) {
            const ReadPrimarySegment_& segment = readModel_->primarySegments[segmentIndex_];
            if (rowIndex_ >= segment.rows.size()) {
                ++segmentIndex_;
                rowIndex_ = 0;
                continue;
            }
            const ReadPrimaryRow_& row = segment.rows[rowIndex_++];
            if (!endKey_.isEmpty() && !(row.primaryKey < endKey_)) {
                return false;
            }
            return materializeReadEntry_(snapshot_, row, SwByteArray(), outEntry);
        }
        return false;
    }

private:
    void locateStart_(const SwByteArray& startKey) {
        segmentIndex_ = 0;
        rowIndex_ = 0;
        if (!readModel_ || readModel_->primarySegments.isEmpty() || startKey.isEmpty()) {
            return;
        }

        SwList<ReadPrimarySegment_>::const_iterator begin = readModel_->primarySegments.begin();
        SwList<ReadPrimarySegment_>::const_iterator end = readModel_->primarySegments.end();
        SwList<ReadPrimarySegment_>::const_iterator it = std::lower_bound(
            begin,
            end,
            startKey,
            [](const ReadPrimarySegment_& segment, const SwByteArray& key) { return segment.lastKey < key; });
        segmentIndex_ = static_cast<std::size_t>(std::distance(begin, it));
        if (segmentIndex_ >= readModel_->primarySegments.size()) {
            return;
        }

        const ReadPrimarySegment_& segment = readModel_->primarySegments[segmentIndex_];
        rowIndex_ = static_cast<std::size_t>(
            std::lower_bound(segment.rows.begin(),
                             segment.rows.end(),
                             startKey,
                             [](const ReadPrimaryRow_& row, const SwByteArray& key) { return row.primaryKey < key; }) -
            segment.rows.begin());
    }

    std::shared_ptr<SnapshotState_> snapshot_;
    std::shared_ptr<ReadModel_> readModel_;
    SwByteArray endKey_;
    std::size_t segmentIndex_{0};
    std::size_t rowIndex_{0};
};

class ReadModelIndexIteratorState_ : public IteratorState_ {
public:
    ReadModelIndexIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                 const std::shared_ptr<ReadModel_>& readModel,
                                 const SwString& indexName,
                                 const SwByteArray& startSecondaryKey,
                                 const SwByteArray& endSecondaryKey)
        : snapshot_(snapshot),
          readModel_(readModel),
          endSecondaryKey_(endSecondaryKey),
          startCompositeKey_(startSecondaryKey.isEmpty()
                                 ? SwByteArray()
                                 : encodeIndexCompositeKey_(startSecondaryKey, SwByteArray())) {
        if (readModel_) {
            const SwHash<SwString, SwList<ReadIndexSegment_> >::const_iterator bucket =
                readModel_->indexSegments.find(indexName);
            if (bucket != readModel_->indexSegments.end()) {
                indexSegments_ = &bucket->second;
            }
        }
        locateStart_();
    }

    bool next(SwDbEntry& outEntry) override {
        if (!readModel_ || !indexSegments_) {
            return false;
        }

        while (segmentIndex_ < indexSegments_->size()) {
            const ReadIndexSegment_& segment = (*indexSegments_)[segmentIndex_];
            if (rowIndex_ >= segment.rows.size()) {
                ++segmentIndex_;
                rowIndex_ = 0;
                continue;
            }
            const ReadIndexRow_& row = segment.rows[rowIndex_++];
            if (!endSecondaryKey_.isEmpty() && !(row.secondaryKey < endSecondaryKey_)) {
                return false;
            }
            if (row.primarySegmentIndex >= readModel_->primarySegments.size()) {
                return false;
            }
            const ReadPrimarySegment_& primarySegment = readModel_->primarySegments[row.primarySegmentIndex];
            if (row.primaryRowIndex >= primarySegment.rows.size()) {
                return false;
            }
            return materializeReadEntry_(snapshot_, primarySegment.rows[row.primaryRowIndex], row.secondaryKey, outEntry);
        }
        return false;
    }

private:
    void locateStart_() {
        segmentIndex_ = 0;
        rowIndex_ = 0;
        if (!indexSegments_) {
            return;
        }
        const SwList<ReadIndexSegment_>& segments = *indexSegments_;
        if (segments.isEmpty() || startCompositeKey_.isEmpty()) {
            return;
        }

        SwList<ReadIndexSegment_>::const_iterator begin = segments.begin();
        SwList<ReadIndexSegment_>::const_iterator end = segments.end();
        SwList<ReadIndexSegment_>::const_iterator it = std::lower_bound(
            begin,
            end,
            startCompositeKey_,
            [](const ReadIndexSegment_& segment, const SwByteArray& key) { return segment.lastCompositeKey < key; });
        segmentIndex_ = static_cast<std::size_t>(std::distance(begin, it));
        if (segmentIndex_ >= segments.size()) {
            return;
        }

        const ReadIndexSegment_& segment = segments[segmentIndex_];
        rowIndex_ = static_cast<std::size_t>(
            std::lower_bound(segment.rows.begin(),
                             segment.rows.end(),
                             startCompositeKey_,
                             [](const ReadIndexRow_& row, const SwByteArray& key) {
                                 return row.compositeKey < key;
                             }) -
            segment.rows.begin());
    }

    std::shared_ptr<SnapshotState_> snapshot_;
    std::shared_ptr<ReadModel_> readModel_;
    const SwList<ReadIndexSegment_>* indexSegments_{nullptr};
    SwByteArray endSecondaryKey_;
    SwByteArray startCompositeKey_;
    std::size_t segmentIndex_{0};
    std::size_t rowIndex_{0};
};

inline bool snapshotLookupPrimary_(const std::shared_ptr<SnapshotState_>& snapshot,
                                   const SwByteArray& primaryKey,
                                   PrimaryRecord_& outRecord,
                                   bool resolveBlob) {
    return snapshot && snapshot->lookupPrimary(primaryKey, outRecord, resolveBlob);
}

// Two-way sorted merge of the shared base read model with the writer overlay
// (records written since the base was built). The overlay always wins on key
// collisions (its sequences are newer); deleted overlay entries suppress the
// matching base rows.
class OverlayMergingPrimaryIteratorState_ : public IteratorState_ {
public:
    OverlayMergingPrimaryIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                        const SwByteArray& startKey,
                                        const SwByteArray& endKey)
        : snapshot_(snapshot),
          endKey_(endKey),
          base_(new ReadModelPrimaryIteratorState_(snapshot, snapshot->readModel, startKey, endKey)) {
        overlayIt_ = startKey.isEmpty() ? snapshot_->overlay->primary.begin()
                                        : snapshot_->overlay->primary.lower_bound(startKey);
    }

    bool next(SwDbEntry& outEntry) override {
        while (true) {
            if (!baseLoaded_) {
                // IteratorState_ conflates end-of-range and failure (e.g. a
                // blob-resolve error): a false from the base is treated as
                // exhaustion and the overlay keeps streaming — aborting the
                // whole merge would wrongly drop overlay keys that sort after
                // the base's last row.
                baseValid_ = base_->next(baseEntry_);
                baseLoaded_ = true;
            }
            const bool overlayValid = overlayCurrentValid_();
            if (!overlayValid && !baseValid_) {
                return false;
            }

            bool takeOverlay = false;
            bool alsoConsumeBase = false;
            if (!overlayValid) {
                takeOverlay = false;
            } else if (!baseValid_) {
                takeOverlay = true;
            } else if (overlayIt_->first < baseEntry_.primaryKey) {
                takeOverlay = true;
            } else if (baseEntry_.primaryKey < overlayIt_->first) {
                takeOverlay = false;
            } else {
                takeOverlay = true;
                alsoConsumeBase = true;
            }

            if (takeOverlay) {
                const SwByteArray key = overlayIt_->first;
                PrimaryRecord_ record = overlayIt_->second;
                ++overlayIt_;
                if (alsoConsumeBase) {
                    baseLoaded_ = false;
                }
                if (record.deleted) {
                    continue;
                }
                if (!snapshot_->resolveValue(record)) {
                    return false;
                }
                outEntry.primaryKey = key;
                outEntry.secondaryKey = SwByteArray();
                outEntry.value = record.value;
                outEntry.secondaryKeys = record.secondaryKeys;
                outEntry.sequence = record.sequence;
                return true;
            }

            baseLoaded_ = false;
            outEntry = baseEntry_;
            return true;
        }
    }

private:
    bool overlayCurrentValid_() const {
        if (overlayIt_ == snapshot_->overlay->primary.end()) {
            return false;
        }
        if (!endKey_.isEmpty() && !(overlayIt_->first < endKey_)) {
            return false;
        }
        return true;
    }

    std::shared_ptr<SnapshotState_> snapshot_;
    SwByteArray endKey_;
    std::shared_ptr<ReadModelPrimaryIteratorState_> base_;
    std::map<SwByteArray, PrimaryRecord_>::const_iterator overlayIt_;
    SwDbEntry baseEntry_;
    bool baseValid_{false};
    bool baseLoaded_{false};
};

class OverlayMergingIndexIteratorState_ : public IteratorState_ {
public:
    OverlayMergingIndexIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                      const SwString& indexName,
                                      const SwByteArray& startSecondaryKey,
                                      const SwByteArray& endSecondaryKey,
                                      const std::map<SwByteArray, OverlayIndexEntry_>* overlayIndex)
        : snapshot_(snapshot),
          endSecondaryKey_(endSecondaryKey),
          overlayIndex_(overlayIndex),
          base_(new ReadModelIndexIteratorState_(snapshot,
                                                 snapshot->readModel,
                                                 indexName,
                                                 startSecondaryKey,
                                                 endSecondaryKey)) {
        if (startSecondaryKey.isEmpty()) {
            overlayIt_ = overlayIndex_->begin();
        } else {
            overlayIt_ = overlayIndex_->lower_bound(
                encodeIndexCompositeKey_(startSecondaryKey, SwByteArray()));
        }
    }

    bool next(SwDbEntry& outEntry) override {
        while (true) {
            if (!baseLoaded_) {
                baseValid_ = base_->next(baseEntry_);
                if (baseValid_) {
                    baseCompositeKey_ = encodeIndexCompositeKey_(baseEntry_.secondaryKey, baseEntry_.primaryKey);
                }
                baseLoaded_ = true;
            }
            const bool overlayValid = overlayCurrentValid_();
            if (!overlayValid && !baseValid_) {
                return false;
            }

            bool takeOverlay = false;
            bool alsoConsumeBase = false;
            if (!overlayValid) {
                takeOverlay = false;
            } else if (!baseValid_) {
                takeOverlay = true;
            } else if (overlayIt_->first < baseCompositeKey_) {
                takeOverlay = true;
            } else if (baseCompositeKey_ < overlayIt_->first) {
                takeOverlay = false;
            } else {
                takeOverlay = true;
                alsoConsumeBase = true;
            }

            if (takeOverlay) {
                const OverlayIndexEntry_ entry = overlayIt_->second;
                ++overlayIt_;
                if (alsoConsumeBase) {
                    baseLoaded_ = false;
                }
                if (entry.deleted) {
                    continue;
                }
                PrimaryRecord_ record;
                if (!snapshot_->lookupPrimary(entry.primaryKey, record, true)) {
                    continue;
                }
                outEntry.primaryKey = entry.primaryKey;
                outEntry.secondaryKey = entry.secondaryKey;
                outEntry.value = record.value;
                outEntry.secondaryKeys = record.secondaryKeys;
                outEntry.sequence = record.sequence;
                return true;
            }

            baseLoaded_ = false;
            outEntry = baseEntry_;
            return true;
        }
    }

private:
    bool overlayCurrentValid_() const {
        if (overlayIt_ == overlayIndex_->end()) {
            return false;
        }
        if (!endSecondaryKey_.isEmpty() && !(overlayIt_->second.secondaryKey < endSecondaryKey_)) {
            return false;
        }
        return true;
    }

    std::shared_ptr<SnapshotState_> snapshot_;
    SwByteArray endSecondaryKey_;
    const std::map<SwByteArray, OverlayIndexEntry_>* overlayIndex_{nullptr};
    std::shared_ptr<ReadModelIndexIteratorState_> base_;
    std::map<SwByteArray, OverlayIndexEntry_>::const_iterator overlayIt_;
    SwDbEntry baseEntry_;
    SwByteArray baseCompositeKey_;
    bool baseValid_{false};
    bool baseLoaded_{false};
};

inline std::shared_ptr<IteratorState_> createPrimaryIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                                                   const SwByteArray& startKey,
                                                                   const SwByteArray& endKey) {
    if (snapshot && snapshot->memory) {
        return std::shared_ptr<IteratorState_>(
            new MemoryPrimaryIteratorState_(snapshot->memory, startKey, endKey));
    }
    if (!snapshot || !snapshot->readModel) {
        return std::shared_ptr<IteratorState_>();
    }
    if (snapshot->overlay && !snapshot->overlay->primary.empty()) {
        return std::shared_ptr<IteratorState_>(
            new OverlayMergingPrimaryIteratorState_(snapshot, startKey, endKey));
    }
    return std::shared_ptr<IteratorState_>(
        new ReadModelPrimaryIteratorState_(snapshot, snapshot->readModel, startKey, endKey));
}

inline std::shared_ptr<IteratorState_> createIndexIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                                                 const SwString& indexName,
                                                                 const SwByteArray& startSecondaryKey,
                                                                 const SwByteArray& endSecondaryKey) {
    if (snapshot && snapshot->memory) {
        return std::shared_ptr<IteratorState_>(
            new MemoryIndexIteratorState_(snapshot->memory, indexName, startSecondaryKey, endSecondaryKey));
    }
    if (!snapshot || !snapshot->readModel) {
        return std::shared_ptr<IteratorState_>();
    }
    if (snapshot->overlay) {
        const SwHash<SwString, std::map<SwByteArray, OverlayIndexEntry_>>::const_iterator bucket =
            snapshot->overlay->indexes.find(indexName);
        if (bucket != snapshot->overlay->indexes.end() && !bucket->second.empty()) {
            return std::shared_ptr<IteratorState_>(
                new OverlayMergingIndexIteratorState_(snapshot,
                                                      indexName,
                                                      startSecondaryKey,
                                                      endSecondaryKey,
                                                      &bucket->second));
        }
    }
    return std::shared_ptr<IteratorState_>(
        new ReadModelIndexIteratorState_(snapshot, snapshot->readModel, indexName, startSecondaryKey, endSecondaryKey));
}

} // namespace swEmbeddedDbDetail
