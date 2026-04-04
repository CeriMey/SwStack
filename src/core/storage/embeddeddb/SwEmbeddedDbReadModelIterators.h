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

inline std::shared_ptr<IteratorState_> createPrimaryIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                                                   const SwByteArray& startKey,
                                                                   const SwByteArray& endKey) {
    if (!snapshot || !snapshot->readModel) {
        return std::shared_ptr<IteratorState_>();
    }
    return std::shared_ptr<IteratorState_>(
        new ReadModelPrimaryIteratorState_(snapshot, snapshot->readModel, startKey, endKey));
}

inline std::shared_ptr<IteratorState_> createIndexIteratorState_(const std::shared_ptr<SnapshotState_>& snapshot,
                                                                 const SwString& indexName,
                                                                 const SwByteArray& startSecondaryKey,
                                                                 const SwByteArray& endSecondaryKey) {
    if (!snapshot || !snapshot->readModel) {
        return std::shared_ptr<IteratorState_>();
    }
    return std::shared_ptr<IteratorState_>(
        new ReadModelIndexIteratorState_(snapshot, snapshot->readModel, indexName, startSecondaryKey, endSecondaryKey));
}

} // namespace swEmbeddedDbDetail
