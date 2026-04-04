inline void SwDbIterator::prime_() {
    valid_ = state_ && state_->next(current_);
}

inline void SwDbIterator::next() {
    if (!valid_) {
        return;
    }
    valid_ = state_ && state_->next(current_);
}

inline void SwDbIterator::rewind() {
    if (!factory_) {
        state_.reset();
        valid_ = false;
        return;
    }
    state_ = factory_();
    prime_();
}

inline std::size_t SwDbIterator::size() const {
    if (sizeKnown_) {
        return sizeCache_;
    }
    sizeCache_ = 0;
    if (!factory_) {
        sizeKnown_ = true;
        return 0;
    }
    std::shared_ptr<swEmbeddedDbDetail::IteratorState_> probe = factory_();
    SwDbEntry row;
    while (probe && probe->next(row)) {
        ++sizeCache_;
    }
    sizeKnown_ = true;
    return sizeCache_;
}

inline SwDbStatus SwDbSnapshot::get(const SwByteArray& primaryKey,
                                    SwByteArray* valueOut,
                                    SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) const {
    if (!valid_ || !state_) {
        return SwDbStatus(SwDbStatus::NotOpen, "snapshot is not valid");
    }
    swEmbeddedDbDetail::PrimaryRecord_ record;
    if (!swEmbeddedDbDetail::snapshotLookupPrimary_(state_, primaryKey, record, true)) {
        return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
    }
    if (valueOut) {
        *valueOut = record.value;
    }
    if (secondaryKeysOut) {
        *secondaryKeysOut = record.secondaryKeys;
    }
    return SwDbStatus::success();
}

inline SwDbIterator SwDbSnapshot::scanPrimary(const SwByteArray& startKey, const SwByteArray& endKey) const {
    SwDbIterator it;
    if (!valid_ || !state_) {
        return it;
    }
    const std::shared_ptr<swEmbeddedDbDetail::SnapshotState_> snapshotState = state_;
    it.factory_ = [snapshotState, startKey, endKey]() {
        return swEmbeddedDbDetail::createPrimaryIteratorState_(snapshotState, startKey, endKey);
    };
    it.rewind();
    return it;
}

inline SwDbIterator SwDbSnapshot::scanIndex(const SwString& indexName,
                                            const SwByteArray& startSecondaryKey,
                                            const SwByteArray& endSecondaryKey) const {
    SwDbIterator it;
    if (!valid_ || !state_) {
        return it;
    }
    const std::shared_ptr<swEmbeddedDbDetail::SnapshotState_> snapshotState = state_;
    it.factory_ = [snapshotState, indexName, startSecondaryKey, endSecondaryKey]() {
        return swEmbeddedDbDetail::createIndexIteratorState_(snapshotState, indexName, startSecondaryKey, endSecondaryKey);
    };
    it.rewind();
    return it;
}
