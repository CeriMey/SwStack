namespace swEmbeddedDbDetail {

class SnapshotState_ {
public:
    unsigned long long visibleSequence{0};
    SwString blobDir;
    SwEmbeddedDbOptions options;
    std::shared_ptr<ReadModel_> readModel;
    MemTable_ mutableMem;
    SwList<MemTable_> immutableMems;
    std::vector<std::shared_ptr<TableHandle_>> primaryTablesNewestFirst;
    SwHash<SwString, std::vector<std::shared_ptr<TableHandle_>>> indexTablesNewestFirst;

    bool resolveValue(PrimaryRecord_& record) const {
        if (record.inlineValue || !record.blobRef.valid) {
            return true;
        }
        std::shared_ptr<swDbPlatform::RandomAccessFile> file = blobFile_(record.blobRef.fileId);
        if (!file) {
            return false;
        }
        record.value.resize(record.blobRef.length);
        if (!file->readAt(record.blobRef.offset, record.value.data(), record.value.size(), nullptr)) {
            return false;
        }
        if (crc32Bytes_(record.value) != record.blobRef.crc32) {
            return false;
        }
        record.inlineValue = true;
        return true;
    }

    bool lookupPrimary(const SwByteArray& primaryKey, PrimaryRecord_& outRecord, bool resolveBlob) const {
        int tableHint = -1;
        return lookupPrimaryWithHint(primaryKey, outRecord, resolveBlob, &tableHint);
    }

    bool lookupPrimaryWithHint(const SwByteArray& primaryKey,
                               PrimaryRecord_& outRecord,
                               bool resolveBlob,
                               int* tableHint) const {
        const PrimaryMemStore_::const_iterator mutableIt = mutableMem.primary.find(primaryKey);
        if (mutableIt != mutableMem.primary.end()) {
            outRecord = mutableIt->second;
            return !outRecord.deleted && (!resolveBlob || resolveValue(outRecord));
        }
        for (std::size_t i = immutableMems.size(); i > 0; --i) {
            const PrimaryMemStore_::const_iterator immutableIt = immutableMems[i - 1].primary.find(primaryKey);
            if (immutableIt != immutableMems[i - 1].primary.end()) {
                outRecord = immutableIt->second;
                return !outRecord.deleted && (!resolveBlob || resolveValue(outRecord));
            }
        }

        const int hintedIndex = (tableHint && *tableHint >= 0 &&
                                 static_cast<std::size_t>(*tableHint) < primaryTablesNewestFirst.size())
            ? *tableHint
            : -1;
        if (hintedIndex >= 0 &&
            loadPrimaryFromTable_(static_cast<std::size_t>(hintedIndex), primaryKey, outRecord, resolveBlob)) {
            return true;
        }

        for (std::size_t i = 0; i < primaryTablesNewestFirst.size(); ++i) {
            if (static_cast<int>(i) == hintedIndex) {
                continue;
            }
            if (!loadPrimaryFromTable_(i, primaryKey, outRecord, resolveBlob)) {
                continue;
            }
            if (tableHint) {
                *tableHint = static_cast<int>(i);
            }
            return true;
        }
        return false;
    }

private:
    bool loadPrimaryFromTable_(std::size_t tableIndex,
                               const SwByteArray& primaryKey,
                               PrimaryRecord_& outRecord,
                               bool resolveBlob) const {
        if (tableIndex >= primaryTablesNewestFirst.size() || !primaryTablesNewestFirst[tableIndex]) {
            return false;
        }

        TableRecord_ raw;
        if (!primaryTablesNewestFirst[tableIndex]->findRecord(primaryKey, raw)) {
            return false;
        }

        outRecord = PrimaryRecord_();
        outRecord.deleted = (raw.flags & RecordDeleted) != 0u;
        outRecord.sequence = raw.sequence;
        if (outRecord.deleted) {
            return false;
        }
        if (!decodePrimaryPayload_(raw.payload, outRecord)) {
            return false;
        }
        return !resolveBlob || resolveValue(outRecord);
    }

    std::shared_ptr<swDbPlatform::RandomAccessFile> blobFile_(unsigned long long fileId) const {
        {
            std::lock_guard<std::mutex> lock(blobFilesMutex_);
            const SwHash<unsigned long long, std::shared_ptr<swDbPlatform::RandomAccessFile>>::const_iterator cached =
                blobFiles_.find(fileId);
            if (cached != blobFiles_.end()) {
                return cached->second;
            }
        }

        std::shared_ptr<swDbPlatform::RandomAccessFile> opened(new swDbPlatform::RandomAccessFile());
        const SwString blobPath = swDbPlatform::joinPath(blobDir, blobFileName_(fileId));
        if (!opened->open(blobPath, swDbPlatform::RandomAccessFile::OpenMode::ReadOnly, nullptr)) {
            return std::shared_ptr<swDbPlatform::RandomAccessFile>();
        }

        std::lock_guard<std::mutex> lock(blobFilesMutex_);
        const SwHash<unsigned long long, std::shared_ptr<swDbPlatform::RandomAccessFile>>::const_iterator cached =
            blobFiles_.find(fileId);
        if (cached != blobFiles_.end()) {
            return cached->second;
        }
        blobFiles_[fileId] = opened;
        return opened;
    }

    mutable std::mutex blobFilesMutex_;
    mutable SwHash<unsigned long long, std::shared_ptr<swDbPlatform::RandomAccessFile>> blobFiles_;
};

} // namespace swEmbeddedDbDetail

