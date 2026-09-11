#pragma once

inline SwDbStatus SwEmbeddedDb::getJsonRecord(const SwByteArray& key, SwDbJsonRecord* out) {
    if (!out) return SwDbStatus(SwDbStatus::InvalidArgument, "Missing record output");
    if (!options_.persistent) return swEmbeddedDbDetail::MemoryManager_(*this).getJsonRecord(key, out);
    SwByteArray bytes;
    const auto status = get(key, &bytes);
    if (!status.ok()) return status;
    if (!SwDbJsonRecord::fromBytes_(bytes, *out))
        return SwDbStatus(SwDbStatus::Corruption, "value is not a JSON object");
    return SwDbStatus::success();
}

inline SwDbStatus SwDbSnapshot::getJson(const SwByteArray& primaryKey,
                                       SwJsonObject* valueOut,
                                       SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) const {
    if (!valid_ || !state_) return SwDbStatus(SwDbStatus::NotOpen, "snapshot is not valid");
    swEmbeddedDbDetail::PrimaryRecord_ record;
    if (!swEmbeddedDbDetail::snapshotLookupPrimary_(state_, primaryKey, record, true)) {
        return SwDbStatus(SwDbStatus::NotFound, "primary key not found");
    }
    if (valueOut && !swEmbeddedDbDetail::recordJson_(record, *valueOut)) {
        return SwDbStatus(SwDbStatus::Corruption, "value is not a JSON object");
    }
    if (secondaryKeysOut) *secondaryKeysOut = record.secondaryKeys;
    return SwDbStatus::success();
}

inline SwDbJsonIterator SwDbSnapshot::scanPrimaryJson(const SwByteArray& startKey,
                                                     const SwByteArray& endKey) const {
    SwDbJsonIterator it;
    if (!valid_ || !state_) return it;
    const auto state = state_;
    it.factory_ = [state, startKey, endKey]() {
        return swEmbeddedDbDetail::createPrimaryIteratorState_(state, startKey, endKey);
    };
    it.rewind();
    return it;
}

inline SwDbJsonIterator SwDbSnapshot::scanIndexJson(const SwString& indexName,
                                                   const SwByteArray& startKey,
                                                   const SwByteArray& endKey) const {
    SwDbJsonIterator it;
    if (!valid_ || !state_) return it;
    const auto state = state_;
    it.factory_ = [state, indexName, startKey, endKey]() {
        return swEmbeddedDbDetail::createIndexIteratorState_(state, indexName, startKey, endKey);
    };
    it.rewind();
    return it;
}

inline SwDbStatus SwEmbeddedDb::getJson(const SwByteArray& primaryKey,
                                       SwJsonObject* valueOut,
                                       SwMap<SwString, SwList<SwByteArray>>* secondaryKeysOut) {
    if (!options_.persistent) {
        return swEmbeddedDbDetail::MemoryManager_(*this).getJson(primaryKey, valueOut, secondaryKeysOut);
    }
    SwByteArray bytes;
    const auto status = get(primaryKey, valueOut ? &bytes : nullptr, secondaryKeysOut);
    if (!status.ok()) return status;
    if (valueOut && !swEmbeddedDbDetail::parseJsonObject_(bytes, *valueOut)) {
        return SwDbStatus(SwDbStatus::Corruption, "value is not a JSON object");
    }
    return SwDbStatus::success();
}

inline SwDbJsonIterator SwEmbeddedDb::scanPrimaryJson(const SwByteArray& startKey,
                                                     const SwByteArray& endKey) {
    return createSnapshot().scanPrimaryJson(startKey, endKey);
}

inline SwDbJsonIterator SwEmbeddedDb::scanIndexJson(const SwString& indexName,
                                                   const SwByteArray& startKey,
                                                   const SwByteArray& endKey) {
    return createSnapshot().scanIndexJson(indexName, startKey, endKey);
}
