namespace swEmbeddedDbDetail {

inline SwByteArray recordBytes_(const PrimaryRecord_& record) {
    return record.jsonPayload ? record.jsonPayload->bytes() : record.value;
}

inline bool recordJson_(const PrimaryRecord_& record, SwJsonObject& out) {
    if (record.jsonPayload) {
        record.jsonPayload->copyTo(out);
        return true;
    }
    return parseJsonObject_(record.value, out);
}

class MemoryManager_ {
public:
    explicit MemoryManager_(SwEmbeddedDb& db) : db_(db) {}

    SwDbStatus get(const SwByteArray& key, SwByteArray* valueOut,
                   SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) {
        SwEmbeddedDbLock_ lock(db_.mutex_);
        if (!db_.opened_.load(std::memory_order_acquire) || !db_.memoryState_) {
            return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
        }
        ++db_.metrics_.getCount;
        const auto found = db_.memoryState_->primary.find(key);
        if (found == db_.memoryState_->primary.end()) {
            return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
        }
        if (valueOut) *valueOut = recordBytes_(found->second);
        if (secondaryKeysOut) *secondaryKeysOut = found->second.secondaryKeys;
        return SwDbStatus::success();
    }

    SwDbStatus getJson(const SwByteArray& key, SwJsonObject* valueOut,
                      SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) {
        PrimaryRecord_ record;
        {
            SwEmbeddedDbLock_ lock(db_.mutex_);
            if (!db_.opened_.load(std::memory_order_acquire) || !db_.memoryState_) {
                return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
            }
            ++db_.metrics_.getCount;
            const auto found = db_.memoryState_->primary.find(key);
            if (found == db_.memoryState_->primary.end()) {
                return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
            }
            // Retain this record only: a point read never owns MemoryState_ or
            // forces an unrelated write to copy the entire database.
            record = found->second;
        }
        if (valueOut && !recordJson_(record, *valueOut)) {
            return SwDbStatus(SwDbStatus::Corruption, "value is not a JSON object");
        }
        if (secondaryKeysOut) *secondaryKeysOut = record.secondaryKeys;
        return SwDbStatus::success();
    }

    SwDbStatus getJsonRecord(const SwByteArray& key, SwDbJsonRecord* out) {
        PrimaryRecord_ record;
        {
            SwEmbeddedDbLock_ lock(db_.mutex_);
            if (!db_.opened_.load(std::memory_order_acquire) || !db_.memoryState_)
                return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
            ++db_.metrics_.getCount;
            const auto found = db_.memoryState_->primary.find(key);
            if (found == db_.memoryState_->primary.end())
                return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
            // Do not retain MemoryState_ or copy secondary index metadata.
            record.jsonPayload = found->second.jsonPayload;
            if (!record.jsonPayload) record.value = found->second.value;
        }
        if (record.jsonPayload) out->payload_ = std::move(record.jsonPayload);
        else if (!SwDbJsonRecord::fromBytes_(record.value, *out))
            return SwDbStatus(SwDbStatus::Corruption, "value is not a JSON object");
        return SwDbStatus::success();
    }

    SwDbStatus write(const SwDbWriteBatch& batch) {
        SwEmbeddedDbLock_ lock(db_.mutex_);
        if (!db_.opened_.load(std::memory_order_acquire) || db_.closing_ || !db_.memoryState_) {
            return SwDbStatus(SwDbStatus::NotOpen, "database is not open");
        }
        // Validate the complete batch before changing its first record. A disk
        // blob reference cannot be resolved by a database that never opens files.
        for (const auto& op : batch.operations()) {
            if (op.type == SwDbWriteBatch::Operation::Put && !op.valueInline) {
                return SwDbStatus(SwDbStatus::InvalidArgument,
                                  "disk blob references are unavailable in memory mode");
            }
        }
        if (batch.isEmpty()) return SwDbStatus::success();
        if (!db_.memoryState_.unique()) {
            db_.memoryState_.reset(new MemoryState_(*db_.memoryState_));
        }
        MemoryState_& state = *db_.memoryState_;
        const unsigned long long sequence = db_.nextSequence_++;
        for (const auto& op : batch.operations()) {
            const auto old = state.primary.find(op.primaryKey);
            if (old != state.primary.end()) {
                eraseIndexes_(state, op.primaryKey, old->second.secondaryKeys);
                state.primary.erase(old);
            }
            if (op.type == SwDbWriteBatch::Operation::Erase) continue;
            PrimaryRecord_ record;
            record.sequence = sequence;
            record.value = op.jsonPayload ? SwByteArray() : op.value;
            record.jsonPayload = op.jsonPayload;
            record.secondaryKeys = op.secondaryKeys;
            state.primary.emplace(op.primaryKey, std::move(record));
            insertIndexes_(state, op.primaryKey, op.secondaryKeys);
        }
        db_.lastVisibleSequence_ = sequence;
        ++db_.metrics_.writeBatchCount;
        // No WAL, pending write, tombstone, overlay, or durability claim.
        return SwDbStatus::success();
    }

private:
    static void eraseIndexes_(MemoryState_& state, const SwByteArray& primaryKey,
                              const SwMap<SwString, SwList<SwByteArray>>& keys) {
        for (auto it = keys.begin(); it != keys.end(); ++it) {
            const auto bucket = state.indexes.find(it.key());
            if (bucket == state.indexes.end()) continue;
            for (const auto& secondaryKey : it.value()) {
                state.indexEntryCount -= bucket->second.erase(
                    encodeIndexCompositeKey_(secondaryKey, primaryKey));
            }
            if (bucket->second.empty()) state.indexes.erase(bucket);
        }
    }

    static void insertIndexes_(MemoryState_& state, const SwByteArray& primaryKey,
                               const SwMap<SwString, SwList<SwByteArray>>& keys) {
        for (auto it = keys.begin(); it != keys.end(); ++it) {
            if (it.value().isEmpty()) continue;
            auto& bucket = state.indexes[it.key()];
            for (const auto& secondaryKey : it.value()) {
                MemoryIndexEntry_ entry;
                entry.secondaryKey = secondaryKey;
                entry.primaryKey = primaryKey;
                if (bucket.emplace(encodeIndexCompositeKey_(secondaryKey, primaryKey),
                                   std::move(entry)).second) {
                    ++state.indexEntryCount;
                }
            }
        }
    }

    SwEmbeddedDb& db_;
};

inline void memoryReadEntry_(const SwByteArray& primaryKey,
                              const SwByteArray& secondaryKey,
                              const PrimaryRecord_& record, SwDbEntry& out) {
    out.primaryKey = primaryKey;
    out.secondaryKey = secondaryKey;
    out.value = recordBytes_(record);
    out.secondaryKeys = record.secondaryKeys;
    out.sequence = record.sequence;
}

inline void memoryReadEntry_(const SwByteArray& primaryKey,
                             const SwByteArray& secondaryKey,
                             const PrimaryRecord_& record, SwDbJsonEntry& out) {
    out.primaryKey = primaryKey;
    out.secondaryKey = secondaryKey;
    out.value = SwJsonObject();
    out.validJson = recordJson_(record, out.value);
    out.secondaryKeys = record.secondaryKeys;
    out.sequence = record.sequence;
}

class MemoryPrimaryIteratorState_ : public IteratorState_ {
public:
    MemoryPrimaryIteratorState_(const std::shared_ptr<const MemoryState_>& state,
                                const SwByteArray& start, const SwByteArray& end)
        : state_(state), end_(end), next_(state_->primary.lower_bound(start)) {}

    bool next(SwDbEntry& out) override { return nextEntry_(out); }
    bool nextJson(SwDbJsonEntry& out) override { return nextEntry_(out); }

private:
    template<class Entry>
    bool nextEntry_(Entry& out) {
        if (next_ == state_->primary.end() || (!end_.isEmpty() && !(next_->first < end_))) {
            return false;
        }
        memoryReadEntry_(next_->first, SwByteArray(), next_->second, out);
        ++next_;
        return true;
    }

private:
    std::shared_ptr<const MemoryState_> state_;
    SwByteArray end_;
    std::map<SwByteArray, PrimaryRecord_>::const_iterator next_;
};

class MemoryIndexIteratorState_ : public IteratorState_ {
public:
    MemoryIndexIteratorState_(const std::shared_ptr<const MemoryState_>& state,
                              const SwString& indexName,
                              const SwByteArray& start, const SwByteArray& end)
        : state_(state), end_(end) {
        const auto bucket = state_->indexes.find(indexName);
        if (bucket == state_->indexes.end()) return;
        index_ = &bucket->second;
        next_ = start.isEmpty() ? index_->begin()
                               : index_->lower_bound(encodeIndexCompositeKey_(start, SwByteArray()));
    }

    bool next(SwDbEntry& out) override { return nextEntry_(out); }
    bool nextJson(SwDbJsonEntry& out) override { return nextEntry_(out); }

private:
    template<class Entry>
    bool nextEntry_(Entry& out) {
        if (!index_ || next_ == index_->end() ||
            (!end_.isEmpty() && !(next_->second.secondaryKey < end_))) {
            return false;
        }
        const MemoryIndexEntry_& entry = next_->second;
        const auto primary = state_->primary.find(entry.primaryKey);
        if (primary == state_->primary.end()) return false;
        memoryReadEntry_(entry.primaryKey, entry.secondaryKey, primary->second, out);
        ++next_;
        return true;
    }

private:
    std::shared_ptr<const MemoryState_> state_;
    SwByteArray end_;
    const std::map<SwByteArray, MemoryIndexEntry_>* index_{nullptr};
    std::map<SwByteArray, MemoryIndexEntry_>::const_iterator next_;
};

} // namespace swEmbeddedDbDetail
