namespace swEmbeddedDbDetail {

class TableWriter_ {
public:
    explicit TableWriter_(SwEmbeddedDb& db)
        : db_(db) {
    }

    SwDbStatus flushBlock(swDbPlatform::RandomAccessFile& file,
                          unsigned long long& offset,
                          const SwByteArray& firstKey,
                          const SwByteArray& blockPayload,
                          SwList<swEmbeddedDbDetail::BlockIndexEntry_>& blocks) {
        if (blockPayload.isEmpty()) {
            return SwDbStatus::success();
        }

        SwByteArray frame;
        swEmbeddedDbDetail::appendU32LE_(frame, static_cast<unsigned int>(blockPayload.size()));
        swEmbeddedDbDetail::appendU32LE_(frame, swEmbeddedDbDetail::crc32Bytes_(blockPayload));
        frame += blockPayload;

        SwString error;
        if (!file.writeAt(offset, frame.constData(), frame.size(), &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }

        swEmbeddedDbDetail::BlockIndexEntry_ entry;
        entry.firstKey = firstKey;
        entry.offset = offset;
        entry.bytes = static_cast<unsigned int>(frame.size());
        blocks.append(entry);
        offset += static_cast<unsigned long long>(frame.size());
        return SwDbStatus::success();
    }

    SwDbStatus writeTableFile(const swEmbeddedDbDetail::TableMeta_& meta,
                              const SwList<swEmbeddedDbDetail::TableRecord_>& records) {
        if (records.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "cannot write empty table");
        }

        static const unsigned int kBlockSize = 64u * 1024u;
        const SwString tempPath = swDbPlatform::joinPath(db_.tmpDir_, meta.fileName + ".tmp");
        const SwString finalPath = swDbPlatform::joinPath(db_.tableDir_, meta.fileName);

        swDbPlatform::RandomAccessFile file;
        SwString error;
        if (!file.open(tempPath, swDbPlatform::RandomAccessFile::OpenMode::ReadWriteTruncate, &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }

        SwByteArray header;
        header.append(swEmbeddedDbDetail::kTableMagic_, 8);
        swEmbeddedDbDetail::appendU32LE_(header, 1u);
        swEmbeddedDbDetail::appendU32LE_(header, static_cast<unsigned int>(meta.kind));
        swEmbeddedDbDetail::appendU64LE_(header, meta.minSequence);
        swEmbeddedDbDetail::appendU64LE_(header, meta.maxSequence);
        swEmbeddedDbDetail::appendU64LE_(header, meta.recordCount);
        swEmbeddedDbDetail::appendU32LE_(header, static_cast<unsigned int>(meta.indexName.size()));
        swEmbeddedDbDetail::appendU32LE_(header, kBlockSize);
        if (!meta.indexName.isEmpty()) {
            header.append(meta.indexName.data(), meta.indexName.size());
        }

        if (!file.writeAt(0, header.constData(), header.size(), &error)) {
            file.close();
            (void)swDbPlatform::removeFile(tempPath);
            return SwDbStatus(SwDbStatus::IoError, error);
        }

        unsigned long long offset = static_cast<unsigned long long>(header.size());
        swEmbeddedDbDetail::BloomFilter_ bloom;
        bloom.initialize(records.size());
        SwList<swEmbeddedDbDetail::BlockIndexEntry_> blocks;
        SwByteArray blockPayload;
        SwByteArray firstKey;

        for (std::size_t i = 0; i < records.size(); ++i) {
            bloom.add(records[i].userKey);
            SwByteArray encoded;
            swEmbeddedDbDetail::encodeTableRecord_(records[i], encoded);
            if (!blockPayload.isEmpty() &&
                static_cast<unsigned int>(blockPayload.size() + encoded.size()) > kBlockSize) {
                const SwDbStatus blockStatus = flushBlock(file, offset, firstKey, blockPayload, blocks);
                if (!blockStatus.ok()) {
                    file.close();
                    (void)swDbPlatform::removeFile(tempPath);
                    return blockStatus;
                }
                blockPayload.clear();
                firstKey.clear();
            }
            if (blockPayload.isEmpty()) {
                firstKey = records[i].userKey;
            }
            blockPayload += encoded;
        }

        if (!blockPayload.isEmpty()) {
            const SwDbStatus blockStatus = flushBlock(file, offset, firstKey, blockPayload, blocks);
            if (!blockStatus.ok()) {
                file.close();
                (void)swDbPlatform::removeFile(tempPath);
                return blockStatus;
            }
        }

        const unsigned long long sparseOffset = offset;
        SwByteArray sparse;
        swEmbeddedDbDetail::appendU32LE_(sparse, static_cast<unsigned int>(blocks.size()));
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            swEmbeddedDbDetail::appendBytes_(sparse, blocks[i].firstKey);
            swEmbeddedDbDetail::appendU64LE_(sparse, blocks[i].offset);
            swEmbeddedDbDetail::appendU32LE_(sparse, blocks[i].bytes);
        }
        if (!sparse.isEmpty() && !file.writeAt(offset, sparse.constData(), sparse.size(), &error)) {
            file.close();
            (void)swDbPlatform::removeFile(tempPath);
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        offset += static_cast<unsigned long long>(sparse.size());

        const unsigned long long bloomOffset = offset;
        SwByteArray bloomPayload;
        swEmbeddedDbDetail::appendU32LE_(bloomPayload, bloom.hashCount());
        swEmbeddedDbDetail::appendU32LE_(bloomPayload, static_cast<unsigned int>(bloom.bytes().size()));
        if (!bloom.bytes().isEmpty()) {
            bloomPayload += bloom.bytes();
        }
        if (!bloomPayload.isEmpty() && !file.writeAt(offset, bloomPayload.constData(), bloomPayload.size(), &error)) {
            file.close();
            (void)swDbPlatform::removeFile(tempPath);
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        offset += static_cast<unsigned long long>(bloomPayload.size());

        SwByteArray footer;
        footer.append(swEmbeddedDbDetail::kFooterMagic_, 8);
        swEmbeddedDbDetail::appendU64LE_(footer, sparseOffset);
        swEmbeddedDbDetail::appendU64LE_(footer, bloomOffset);
        swEmbeddedDbDetail::appendU64LE_(footer, meta.maxSequence);
        swEmbeddedDbDetail::appendU64LE_(footer, meta.recordCount);
        swEmbeddedDbDetail::appendU32LE_(footer, static_cast<unsigned int>(meta.kind));
        if (!file.writeAt(offset, footer.constData(), footer.size(), &error)) {
            file.close();
            (void)swDbPlatform::removeFile(tempPath);
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        offset += static_cast<unsigned long long>(footer.size());

        if (!file.truncate(offset, &error) || !file.sync(&error)) {
            file.close();
            (void)swDbPlatform::removeFile(tempPath);
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        file.close();

        if (!swDbPlatform::replaceFileAtomically(tempPath, finalPath, &error)) {
            (void)swDbPlatform::removeFile(tempPath);
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        return SwDbStatus::success();
    }

    void cleanupCoveredWalFilesLocked() {
        unsigned long long replayFrom = db_.manifest_.activeWalId;
        if (replayFrom == 0) {
            replayFrom = 1;
        }
        if (db_.mutable_.walId != 0) {
            replayFrom = std::min(replayFrom, db_.mutable_.walId);
        }
        for (std::size_t i = 0; i < db_.immutables_.size(); ++i) {
            if (db_.immutables_[i].walId != 0) {
                replayFrom = std::min(replayFrom, db_.immutables_[i].walId);
            }
        }
        db_.manifest_.replayFromWalId = replayFrom;

        const SwList<SwString> walFiles = swDbPlatform::listFiles(db_.walDir_, "WAL-", ".log");
        for (std::size_t i = 0; i < walFiles.size(); ++i) {
            const unsigned long long walId =
                swEmbeddedDbDetail::parseNumericSuffix_(swDbPlatform::fileName(walFiles[i]), "WAL-", ".log");
            if (walId != 0 && walId < db_.manifest_.replayFromWalId) {
                (void)swDbPlatform::removeFile(walFiles[i]);
            }
        }
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline SwDbStatus SwEmbeddedDb::flushBlock_(swDbPlatform::RandomAccessFile& file,
                                            unsigned long long& offset,
                                            const SwByteArray& firstKey,
                                            const SwByteArray& blockPayload,
                                            SwList<swEmbeddedDbDetail::BlockIndexEntry_>& blocks) {
    return swEmbeddedDbDetail::TableWriter_(*this).flushBlock(file, offset, firstKey, blockPayload, blocks);
}

inline SwDbStatus SwEmbeddedDb::writeTableFile_(const swEmbeddedDbDetail::TableMeta_& meta,
                                                const SwList<swEmbeddedDbDetail::TableRecord_>& records) {
    return swEmbeddedDbDetail::TableWriter_(*this).writeTableFile(meta, records);
}

inline void SwEmbeddedDb::cleanupCoveredWalFilesLocked_() {
    swEmbeddedDbDetail::TableWriter_(*this).cleanupCoveredWalFilesLocked();
}
