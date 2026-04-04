namespace swEmbeddedDbDetail {

struct CachedPrimaryRow_ {
    SwByteArray primaryKey;
    PrimaryRecord_ record;
};

struct CachedIndexRow_ {
    SwByteArray compositeKey;
    SwByteArray secondaryKey;
    SwByteArray primaryKey;
    SecondaryEntry_ entry;
};

struct CachedBlock_ {
    int kind{0};
    unsigned long long approximateBytes{0};
    SwList<CachedPrimaryRow_> primaryRows;
    SwList<CachedIndexRow_> indexRows;
};

class ReadCacheManager_ {
public:
    explicit ReadCacheManager_(unsigned long long budgetBytes)
        : budgetBytes_(budgetBytes) {
    }

    std::shared_ptr<const CachedBlock_> find(const SwString& cacheKey) {
        std::lock_guard<std::mutex> lock(mutex_);
        const SwHash<SwString, std::shared_ptr<CachedBlock_> >::const_iterator it = blocks_.find(cacheKey);
        if (it == blocks_.end()) {
            missCount_ += 1;
            return std::shared_ptr<const CachedBlock_>();
        }
        hitCount_ += 1;
        touchLocked_(cacheKey);
        return it->second;
    }

    std::shared_ptr<const CachedBlock_> getOrCreate(const SwString& cacheKey,
                                                    const std::function<std::shared_ptr<CachedBlock_>()>& creator) {
        if (!creator) {
            return std::shared_ptr<const CachedBlock_>();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const SwHash<SwString, std::shared_ptr<CachedBlock_> >::const_iterator it = blocks_.find(cacheKey);
            if (it != blocks_.end()) {
                hitCount_ += 1;
                touchLocked_(cacheKey);
                return it->second;
            }
            missCount_ += 1;
        }

        std::shared_ptr<CachedBlock_> created = creator();
        if (!created) {
            return std::shared_ptr<const CachedBlock_>();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const SwHash<SwString, std::shared_ptr<CachedBlock_> >::const_iterator existing = blocks_.find(cacheKey);
        if (existing != blocks_.end()) {
            hitCount_ += 1;
            touchLocked_(cacheKey);
            return existing->second;
        }

        if (budgetBytes_ > 0 && created->approximateBytes <= budgetBytes_) {
            evictForLocked_(created->approximateBytes);
        }

        blocks_[cacheKey] = created;
        blockBytes_[cacheKey] = created->approximateBytes;
        lruKeys_.append(cacheKey);
        residentBytes_ += created->approximateBytes;
        return created;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        blocks_.clear();
        blockBytes_.clear();
        lruKeys_.clear();
        residentBytes_ = 0;
        hitCount_ = 0;
        missCount_ = 0;
    }

    unsigned long long residentBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return residentBytes_;
    }

    unsigned long long entryCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<unsigned long long>(blocks_.size());
    }

    unsigned long long hitCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hitCount_;
    }

    unsigned long long missCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return missCount_;
    }

private:
    void touchLocked_(const SwString& cacheKey) {
        lruKeys_.removeAll(cacheKey);
        lruKeys_.append(cacheKey);
    }

    void evictForLocked_(unsigned long long incomingBytes) {
        while (budgetBytes_ > 0 &&
               residentBytes_ + incomingBytes > budgetBytes_ &&
               !lruKeys_.isEmpty()) {
            const SwString eldestKey = lruKeys_.first();
            lruKeys_.removeAt(0);
            const SwHash<SwString, unsigned long long>::const_iterator bytesIt = blockBytes_.find(eldestKey);
            if (bytesIt != blockBytes_.end()) {
                if (residentBytes_ >= bytesIt->second) {
                    residentBytes_ -= bytesIt->second;
                } else {
                    residentBytes_ = 0;
                }
                blockBytes_.remove(eldestKey);
            }
            blocks_.remove(eldestKey);
        }
    }

    unsigned long long budgetBytes_{0};
    mutable std::mutex mutex_;
    SwHash<SwString, std::shared_ptr<CachedBlock_> > blocks_;
    SwHash<SwString, unsigned long long> blockBytes_;
    SwList<SwString> lruKeys_;
    unsigned long long residentBytes_{0};
    unsigned long long hitCount_{0};
    unsigned long long missCount_{0};
};

} // namespace swEmbeddedDbDetail
