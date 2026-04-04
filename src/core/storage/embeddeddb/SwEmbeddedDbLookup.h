inline bool SwEmbeddedDb::lookupPrimaryLocked_(const SwByteArray& primaryKey,
                                               swEmbeddedDbDetail::PrimaryRecord_& outRecord,
                                               bool resolveValue) {
    const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator mutableIt = mutable_.primary.find(primaryKey);
    if (mutableIt != mutable_.primary.end()) {
        outRecord = mutableIt->second;
        return !outRecord.deleted && (!resolveValue || resolveValueLocked_(outRecord));
    }
    for (std::size_t i = immutables_.size(); i > 0; --i) {
        const swEmbeddedDbDetail::PrimaryMemStore_::const_iterator immutableIt =
            immutables_[i - 1].primary.find(primaryKey);
        if (immutableIt != immutables_[i - 1].primary.end()) {
            outRecord = immutableIt->second;
            return !outRecord.deleted && (!resolveValue || resolveValueLocked_(outRecord));
        }
    }
    if (!maxKnownPrimaryKey_.isEmpty() && maxKnownPrimaryKey_ < primaryKey) {
        return false;
    }

    for (std::size_t i = 0; i < primaryTableHandlesNewestFirst_.size(); ++i) {
        swEmbeddedDbDetail::TableRecord_ raw;
        if (!primaryTableHandlesNewestFirst_[i] || !primaryTableHandlesNewestFirst_[i]->findRecord(primaryKey, raw)) {
            continue;
        }
        outRecord = swEmbeddedDbDetail::PrimaryRecord_();
        outRecord.deleted = (raw.flags & swEmbeddedDbDetail::RecordDeleted) != 0u;
        outRecord.sequence = raw.sequence;
        if (outRecord.deleted) {
            return false;
        }
        if (!swEmbeddedDbDetail::decodePrimaryPayload_(raw.payload, outRecord)) {
            return false;
        }
        return !resolveValue || resolveValueLocked_(outRecord);
    }
    return false;
}

inline bool SwEmbeddedDb::resolveValueLocked_(swEmbeddedDbDetail::PrimaryRecord_& record) {
    if (record.inlineValue || !record.blobRef.valid) {
        return true;
    }
    swDbPlatform::RandomAccessFile file;
    SwString error;
    const SwString blobPath = swDbPlatform::joinPath(blobDir_, swEmbeddedDbDetail::blobFileName_(record.blobRef.fileId));
    if (!file.open(blobPath, swDbPlatform::RandomAccessFile::OpenMode::ReadOnly, &error)) {
        return false;
    }
    record.value.resize(record.blobRef.length);
    if (!file.readAt(record.blobRef.offset, record.value.data(), record.value.size(), &error)) {
        return false;
    }
    if (swEmbeddedDbDetail::crc32Bytes_(record.value) != record.blobRef.crc32) {
        return false;
    }
    metrics_.blobBytesRead += static_cast<unsigned long long>(record.value.size());
    record.inlineValue = true;
    return true;
}

