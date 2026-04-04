inline std::size_t ByteArrayHash_::operator()(const SwByteArray& value) const {
    const char* data = value.constData();
    std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
    for (std::size_t i = 0; i < value.size(); ++i) {
        hash ^= static_cast<std::size_t>(static_cast<unsigned char>(data[i]));
        hash *= static_cast<std::size_t>(1099511628211ull);
    }
    return hash;
}

inline const SwList<SwByteArray>& MemTable_::emptyOrderedKeys_() {
    static const SwList<SwByteArray> kEmpty;
    return kEmpty;
}

inline const SwList<SwByteArray>& MemTable_::orderedPrimaryKeys() const {
    if (!primaryOrderDirty_) {
        return orderedPrimaryKeys_;
    }

    orderedPrimaryKeys_.clear();
    orderedPrimaryKeys_.reserve(primary.size());
    for (PrimaryMemStore_::const_iterator it = primary.begin(); it != primary.end(); ++it) {
        orderedPrimaryKeys_.append(it->first);
    }
    std::sort(orderedPrimaryKeys_.begin(), orderedPrimaryKeys_.end());
    primaryOrderDirty_ = false;
    return orderedPrimaryKeys_;
}

inline const SwList<SwByteArray>& MemTable_::orderedSecondaryKeys(const SwString& indexName) const {
    const SwHash<SwString, SecondaryMemStore_>::const_iterator bucketIt = secondary.find(indexName);
    if (bucketIt == secondary.end()) {
        return emptyOrderedKeys_();
    }

    bool dirty = true;
    const SwHash<SwString, bool>::const_iterator dirtyIt = secondaryOrderDirty_.find(indexName);
    if (dirtyIt != secondaryOrderDirty_.end()) {
        dirty = dirtyIt->second;
    }

    if (!dirty) {
        const SwHash<SwString, SwList<SwByteArray>>::iterator cacheIt = orderedSecondaryKeys_.find(indexName);
        if (cacheIt != orderedSecondaryKeys_.end()) {
            return cacheIt->second;
        }
    }

    SwList<SwByteArray> keys;
    keys.reserve(bucketIt->second.size());
    for (SecondaryMemStore_::const_iterator it = bucketIt->second.begin(); it != bucketIt->second.end(); ++it) {
        keys.append(it->first);
    }
    std::sort(keys.begin(), keys.end());
    orderedSecondaryKeys_[indexName] = keys;
    secondaryOrderDirty_[indexName] = false;
    const SwHash<SwString, SwList<SwByteArray>>::iterator cacheIt = orderedSecondaryKeys_.find(indexName);
    return cacheIt != orderedSecondaryKeys_.end() ? cacheIt->second : emptyOrderedKeys_();
}

inline void MemTable_::invalidatePrimaryOrder() {
    primaryOrderDirty_ = true;
}

inline void MemTable_::invalidateSecondaryOrder(const SwString& indexName) {
    secondaryOrderDirty_[indexName] = true;
}

inline void MemTable_::invalidateAllSecondaryOrders() {
    orderedSecondaryKeys_.clear();
    secondaryOrderDirty_.clear();
}
