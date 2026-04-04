namespace swEmbeddedDbDetail {

class TableHandle_ {
public:
    bool open(const SwString& dbPath,
              const TableMeta_& meta,
              const SwEmbeddedDbOptions& options,
              const std::shared_ptr<ReadCacheManager_>& readCache,
              SwDbStatus& outStatus);
    void close();
    bool findRecord(const SwByteArray& key, TableRecord_& outRecord) const;
    bool iterateAll(SwList<TableRecord_>& outRecords) const;
    int kind() const { return kind_; }
    const SwString& indexName() const { return indexName_; }
    const TableMeta_& meta() const { return meta_; }
    const SwByteArray& maxUserKey() const { return maxUserKey_; }
    std::shared_ptr<const CachedBlock_> acquireScanBlock(std::size_t blockIndex) const;

private:
    bool parse_(SwDbStatus& outStatus);
    bool parseSparseIndex_(unsigned long long sparseOffset, unsigned long long sparseEnd, SwDbStatus& outStatus);
    bool parseBloom_(unsigned long long bloomOffset, unsigned long long bloomEnd, SwDbStatus& outStatus);
    bool validateBlocks_(SwDbStatus& outStatus) const;
    bool resolveKeyRange_(SwDbStatus& outStatus);
    bool locateBlockPayload_(const BlockIndexEntry_& block,
                             const char*& payloadCur,
                             const char*& payloadEnd,
                             unsigned int* crcOut = nullptr) const;
    bool decodeBlock_(const BlockIndexEntry_& block, SwList<TableRecord_>& outRecords) const;
    bool scanBlockForKey_(const BlockIndexEntry_& block, const SwByteArray& key, TableRecord_& outRecord) const;
    int findCandidateBlockIndex_(const SwByteArray& key) const;
    bool decodeCachedBlock_(std::size_t blockIndex, CachedBlock_& outBlock) const;
    SwString blockCacheKey_(std::size_t blockIndex) const;

    TableMeta_ meta_;
    swDbPlatform::MappedFile mapped_;
    SwList<BlockIndexEntry_> blocks_;
    BloomFilter_ bloom_;
    int kind_{0};
    unsigned long long minSequence_{0};
    unsigned long long maxSequence_{0};
    unsigned long long recordCount_{0};
    SwString indexName_;
    SwByteArray minUserKey_;
    SwByteArray maxUserKey_;
    std::shared_ptr<ReadCacheManager_> readCache_;
    friend class TableRecordCursor_;
};

} // namespace swEmbeddedDbDetail

inline bool swEmbeddedDbDetail::TableHandle_::open(const SwString& dbPath,
                                                   const TableMeta_& meta,
                                                   const SwEmbeddedDbOptions& options,
                                                   const std::shared_ptr<ReadCacheManager_>& readCache,
                                                   SwDbStatus& outStatus) {
    close();
    meta_ = meta;
    readCache_ = options.readCacheBytes > 0 ? readCache : std::shared_ptr<ReadCacheManager_>();
    SwString error;
    if (!mapped_.openReadOnly(swDbPlatform::joinPath(swDbPlatform::joinPath(dbPath, "tables"), meta.fileName), &error)) {
        outStatus = SwDbStatus(SwDbStatus::IoError, error);
        return false;
    }
    return parse_(outStatus);
}

inline void swEmbeddedDbDetail::TableHandle_::close() {
    mapped_.close();
    blocks_.clear();
    indexName_.clear();
    kind_ = 0;
    minSequence_ = 0;
    maxSequence_ = 0;
    recordCount_ = 0;
    minUserKey_.clear();
    maxUserKey_.clear();
    readCache_.reset();
}

inline bool swEmbeddedDbDetail::TableHandle_::findRecord(const SwByteArray& key, TableRecord_& outRecord) const {
    if (!mapped_.isOpen() ||
        blocks_.isEmpty() ||
        !bloom_.mayContain(key) ||
        (!minUserKey_.isEmpty() && key < minUserKey_) ||
        (!maxUserKey_.isEmpty() && maxUserKey_ < key)) {
        return false;
    }
    const int candidate = findCandidateBlockIndex_(key);
    return candidate >= 0 && scanBlockForKey_(blocks_[static_cast<std::size_t>(candidate)], key, outRecord);
}

inline std::shared_ptr<const swEmbeddedDbDetail::CachedBlock_>
swEmbeddedDbDetail::TableHandle_::acquireScanBlock(std::size_t blockIndex) const {
    if (!readCache_ || blockIndex >= blocks_.size()) {
        return std::shared_ptr<const CachedBlock_>();
    }
    return readCache_->getOrCreate(blockCacheKey_(blockIndex), [this, blockIndex]() {
        std::shared_ptr<CachedBlock_> block(new CachedBlock_());
        if (!decodeCachedBlock_(blockIndex, *block)) {
            return std::shared_ptr<CachedBlock_>();
        }
        return block;
    });
}

inline bool swEmbeddedDbDetail::TableHandle_::iterateAll(SwList<TableRecord_>& outRecords) const {
    outRecords.clear();
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        SwList<TableRecord_> blockRecords;
        if (!decodeBlock_(blocks_[i], blockRecords)) {
            return false;
        }
        outRecords.append(blockRecords.begin(), blockRecords.end());
    }
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::parse_(SwDbStatus& outStatus) {
    static const unsigned long long kFooterBytes = 44u;
    if (mapped_.size() < kFooterBytes) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "table too small");
        return false;
    }
    const char* cur = mapped_.data();
    const char* end = mapped_.data() + mapped_.size();
    if (std::memcmp(cur, kTableMagic_, 8) != 0) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid table magic");
        return false;
    }
    cur += 8;
    unsigned int version = 0;
    unsigned int kind = 0;
    unsigned int indexNameLen = 0;
    unsigned int blockSize = 0;
    if (!readU32LE_(cur, end, version) ||
        !readU32LE_(cur, end, kind) ||
        !readU64LE_(cur, end, minSequence_) ||
        !readU64LE_(cur, end, maxSequence_) ||
        !readU64LE_(cur, end, recordCount_) ||
        !readU32LE_(cur, end, indexNameLen) ||
        !readU32LE_(cur, end, blockSize) ||
        version != 1u ||
        static_cast<std::size_t>(end - cur) < indexNameLen) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid table header");
        return false;
    }
    indexName_ = SwString(cur, indexNameLen);
    cur += indexNameLen;
    kind_ = static_cast<int>(kind);

    const char* footer = end - static_cast<std::ptrdiff_t>(kFooterBytes);
    if (std::memcmp(footer, kFooterMagic_, 8) != 0) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid table footer");
        return false;
    }
    footer += 8;
    unsigned long long sparseOffset = 0;
    unsigned long long bloomOffset = 0;
    unsigned long long footerMaxSeq = 0;
    unsigned long long footerRecordCount = 0;
    unsigned int footerKind = 0;
    if (!readU64LE_(footer, end, sparseOffset) ||
        !readU64LE_(footer, end, bloomOffset) ||
        !readU64LE_(footer, end, footerMaxSeq) ||
        !readU64LE_(footer, end, footerRecordCount) ||
        !readU32LE_(footer, end, footerKind) ||
        footerMaxSeq != maxSequence_ ||
        footerRecordCount != recordCount_ ||
        footerKind != static_cast<unsigned int>(kind_)) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "table footer mismatch");
        return false;
    }
    return parseSparseIndex_(sparseOffset, bloomOffset, outStatus) &&
           parseBloom_(bloomOffset, static_cast<unsigned long long>(mapped_.size()) - kFooterBytes, outStatus) &&
           validateBlocks_(outStatus) &&
           resolveKeyRange_(outStatus);
}

inline bool swEmbeddedDbDetail::TableHandle_::parseSparseIndex_(unsigned long long sparseOffset,
                                                                unsigned long long sparseEnd,
                                                                SwDbStatus& outStatus) {
    blocks_.clear();
    const char* cur = mapped_.data() + sparseOffset;
    const char* end = mapped_.data() + sparseEnd;
    unsigned int blockCount = 0;
    if (!readU32LE_(cur, end, blockCount)) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid sparse index");
        return false;
    }
    for (unsigned int i = 0; i < blockCount; ++i) {
        BlockIndexEntry_ entry;
        if (!readBytes_(cur, end, entry.firstKey) ||
            !readU64LE_(cur, end, entry.offset) ||
            !readU32LE_(cur, end, entry.bytes)) {
            outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid sparse entry");
            return false;
        }
        blocks_.append(entry);
    }
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::parseBloom_(unsigned long long bloomOffset,
                                                          unsigned long long bloomEnd,
                                                          SwDbStatus& outStatus) {
    const char* cur = mapped_.data() + bloomOffset;
    const char* end = mapped_.data() + bloomEnd;
    unsigned int hashCount = 0;
    unsigned int byteCount = 0;
    if (!readU32LE_(cur, end, hashCount) ||
        !readU32LE_(cur, end, byteCount) ||
        static_cast<std::size_t>(end - cur) < byteCount) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid bloom section");
        return false;
    }
    bloom_.assign(SwByteArray(cur, byteCount), hashCount);
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::validateBlocks_(SwDbStatus& outStatus) const {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        const char* payloadCur = nullptr;
        const char* payloadEnd = nullptr;
        unsigned int crc = 0;
        if (!locateBlockPayload_(blocks_[i], payloadCur, payloadEnd, &crc)) {
            outStatus = SwDbStatus(SwDbStatus::Corruption, "invalid table block header");
            return false;
        }
        if (crc32Bytes_(payloadCur, static_cast<std::size_t>(payloadEnd - payloadCur)) != crc) {
            outStatus = SwDbStatus(SwDbStatus::Corruption, "table block checksum mismatch");
            return false;
        }
    }
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::resolveKeyRange_(SwDbStatus& outStatus) {
    minUserKey_.clear();
    maxUserKey_.clear();
    if (blocks_.isEmpty()) {
        return true;
    }

    minUserKey_ = blocks_.first().firstKey;
    SwList<TableRecord_> records;
    if (!decodeBlock_(blocks_.last(), records)) {
        outStatus = SwDbStatus(SwDbStatus::Corruption, "failed to decode last table block");
        return false;
    }
    if (!records.isEmpty()) {
        maxUserKey_ = records.last().userKey;
    }
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::locateBlockPayload_(const BlockIndexEntry_& block,
                                                                  const char*& payloadCur,
                                                                  const char*& payloadEnd,
                                                                  unsigned int* crcOut) const {
    payloadCur = nullptr;
    payloadEnd = nullptr;
    if (block.offset + block.bytes > mapped_.size()) {
        return false;
    }

    const char* cur = mapped_.data() + block.offset;
    const char* end = cur + block.bytes;
    unsigned int payloadBytes = 0;
    unsigned int crc = 0;
    if (!readU32LE_(cur, end, payloadBytes) ||
        !readU32LE_(cur, end, crc) ||
        static_cast<std::size_t>(end - cur) < payloadBytes) {
        return false;
    }

    payloadCur = cur;
    payloadEnd = cur + payloadBytes;
    if (crcOut) {
        *crcOut = crc;
    }
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::decodeBlock_(const BlockIndexEntry_& block,
                                                           SwList<TableRecord_>& outRecords) const {
    outRecords.clear();
    const char* payloadCur = nullptr;
    const char* payloadEnd = nullptr;
    if (!locateBlockPayload_(block, payloadCur, payloadEnd)) {
        return false;
    }
    while (payloadCur < payloadEnd) {
        TableRecord_ record;
        if (!decodeTableRecord_(payloadCur, payloadEnd, record)) {
            return false;
        }
        outRecords.append(record);
    }
    return true;
}

inline bool swEmbeddedDbDetail::TableHandle_::scanBlockForKey_(const BlockIndexEntry_& block,
                                                               const SwByteArray& key,
                                                               TableRecord_& outRecord) const {
    const char* payloadCur = nullptr;
    const char* payloadEnd = nullptr;
    if (!locateBlockPayload_(block, payloadCur, payloadEnd)) {
        return false;
    }
    while (payloadCur < payloadEnd) {
        unsigned int keyBytes = 0;
        if (!readU32LE_(payloadCur, payloadEnd, keyBytes) ||
            static_cast<std::size_t>(payloadEnd - payloadCur) < keyBytes) {
            return false;
        }
        const char* recordKeyData = payloadCur;
        payloadCur += keyBytes;

        int keyCompare = 0;
        const std::size_t sharedBytes = std::min<std::size_t>(static_cast<std::size_t>(keyBytes), key.size());
        if (sharedBytes > 0) {
            keyCompare = std::memcmp(recordKeyData, key.constData(), sharedBytes);
        }
        if (keyCompare == 0) {
            if (keyBytes < key.size()) {
                keyCompare = -1;
            } else if (keyBytes > key.size()) {
                keyCompare = 1;
            }
        }

        unsigned long long sequence = 0;
        unsigned int flags = 0;
        unsigned int valueBytes = 0;
        if (!readU64LE_(payloadCur, payloadEnd, sequence) ||
            !readU32LE_(payloadCur, payloadEnd, flags) ||
            !readU32LE_(payloadCur, payloadEnd, valueBytes) ||
            static_cast<std::size_t>(payloadEnd - payloadCur) < valueBytes) {
            return false;
        }

        if (keyCompare == 0) {
            outRecord.userKey = key;
            outRecord.sequence = sequence;
            outRecord.flags = flags;
            outRecord.payload = SwByteArray(payloadCur, valueBytes);
            return true;
        }
        if (keyCompare < 0) {
            payloadCur += valueBytes;
            continue;
        }
        if (keyCompare > 0) {
            return false;
        }
        payloadCur += valueBytes;
    }
    return false;
}

inline bool swEmbeddedDbDetail::TableHandle_::decodeCachedBlock_(std::size_t blockIndex,
                                                                 CachedBlock_& outBlock) const {
    outBlock = CachedBlock_();
    outBlock.kind = kind_;
    if (blockIndex >= blocks_.size()) {
        return false;
    }

    const char* payloadCur = nullptr;
    const char* payloadEnd = nullptr;
    if (!locateBlockPayload_(blocks_[blockIndex], payloadCur, payloadEnd)) {
        return false;
    }

    while (payloadCur < payloadEnd) {
        TableRecordView_ raw;
        if (!decodeTableRecordView_(payloadCur, payloadEnd, raw)) {
            return false;
        }

        if (kind_ == TableKindPrimary) {
            CachedPrimaryRow_ row;
            row.primaryKey = SwByteArray(raw.userKeyData, raw.userKeyBytes);
            row.record.deleted = (raw.flags & RecordDeleted) != 0u;
            row.record.sequence = raw.sequence;
            if (!row.record.deleted &&
                !decodePrimaryPayload_(raw.payloadData, raw.payloadBytes, row.record)) {
                return false;
            }
            outBlock.approximateBytes += static_cast<unsigned long long>(row.primaryKey.size());
            outBlock.approximateBytes += static_cast<unsigned long long>(row.record.value.size());
            for (SwMap<SwString, SwList<SwByteArray> >::const_iterator it = row.record.secondaryKeys.begin();
                 it != row.record.secondaryKeys.end();
                 ++it) {
                outBlock.approximateBytes += static_cast<unsigned long long>(it.key().size());
                for (std::size_t i = 0; i < it.value().size(); ++i) {
                    outBlock.approximateBytes += static_cast<unsigned long long>(it.value()[i].size());
                }
            }
            outBlock.approximateBytes += 96u;
            outBlock.primaryRows.append(std::move(row));
            continue;
        }

        CachedIndexRow_ row;
        row.compositeKey = SwByteArray(raw.userKeyData, raw.userKeyBytes);
        if (!decodeIndexCompositeKey_(row.compositeKey, row.secondaryKey, row.primaryKey)) {
            return false;
        }
        row.entry.deleted = (raw.flags & RecordDeleted) != 0u;
        row.entry.sequence = raw.sequence;
        outBlock.approximateBytes += static_cast<unsigned long long>(row.compositeKey.size() +
                                                                     row.secondaryKey.size() +
                                                                     row.primaryKey.size());
        outBlock.approximateBytes += 64u;
        outBlock.indexRows.append(std::move(row));
    }
    return true;
}

inline int swEmbeddedDbDetail::TableHandle_::findCandidateBlockIndex_(const SwByteArray& key) const {
    if (blocks_.isEmpty()) {
        return -1;
    }

    std::size_t low = 0;
    std::size_t high = blocks_.size();
    while (low < high) {
        const std::size_t mid = low + ((high - low) / 2);
        if (blocks_[mid].firstKey <= key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low == 0) {
        return -1;
    }
    return static_cast<int>(low - 1);
}

inline SwString swEmbeddedDbDetail::TableHandle_::blockCacheKey_(std::size_t blockIndex) const {
    return meta_.fileName + SwString("#") + SwString::number(static_cast<long long>(blockIndex));
}
