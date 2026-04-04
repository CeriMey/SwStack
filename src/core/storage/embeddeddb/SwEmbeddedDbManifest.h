namespace swEmbeddedDbDetail {

class ManifestStore_ {
public:
    explicit ManifestStore_(SwEmbeddedDb& db)
        : db_(db) {
    }

    SwDbStatus loadFromDisk(bool createIfMissing) {
        db_.manifest_ = swEmbeddedDbDetail::Manifest_();
        db_.mutable_ = swEmbeddedDbDetail::MemTable_();
        db_.immutables_.clear();
        db_.tableHandles_.clear();
        db_.primaryTableHandlesNewestFirst_.clear();
        db_.indexTableHandlesNewestFirst_.clear();
        db_.readOnlySnapshotState_.reset();
        db_.lastVisibleSequence_ = 0;

        const SwString currentPath = swDbPlatform::joinPath(db_.dbPath_, "CURRENT");
        std::string currentManifest;
        if (!swDbPlatform::readWholeFile(currentPath, currentManifest, nullptr)) {
            if (db_.options_.readOnly || !createIfMissing) {
                return SwDbStatus(SwDbStatus::NotOpen, "CURRENT is missing");
            }
            db_.manifest_.manifestId = 1;
            db_.manifest_.activeWalId = 1;
            db_.manifest_.replayFromWalId = 1;
            db_.manifest_.nextWalId = 2;
            db_.manifest_.nextTableId = 1;
            const SwDbStatus persistStatus = persistManifestLocked();
            if (!persistStatus.ok()) {
                return persistStatus;
            }
        } else {
            const SwDbStatus status = loadManifestFile(SwString(SwEmbeddedDb::trimText_(currentManifest)));
            if (!status.ok()) {
                return status;
            }
        }

        const SwDbStatus openTablesStatus = db_.openTablesLocked_();
        if (!openTablesStatus.ok()) {
            return openTablesStatus;
        }

        const SwDbStatus replayStatus = db_.replayWalLocked_();
        if (!replayStatus.ok()) {
            return replayStatus;
        }

        if (!db_.options_.readOnly) {
            normalizeIdsLocked();
        }
        db_.lastDurableSequence_ = db_.lastVisibleSequence_;
        db_.nextSequence_ = db_.lastVisibleSequence_ + 1;
        db_.metrics_.lastDurableSequence = db_.lastDurableSequence_;
        db_.rebuildReadOnlySnapshotLocked_();
        return SwDbStatus::success();
    }

    void normalizeIdsLocked() {
        unsigned long long maxWalId = 0;
        const SwList<SwString> walFiles = swDbPlatform::listFiles(db_.walDir_, "WAL-", ".log");
        for (std::size_t i = 0; i < walFiles.size(); ++i) {
            maxWalId = std::max(
                maxWalId,
                swEmbeddedDbDetail::parseNumericSuffix_(swDbPlatform::fileName(walFiles[i]), "WAL-", ".log"));
        }
        db_.manifest_.nextWalId = std::max(db_.manifest_.nextWalId, maxWalId + 1);
        db_.manifest_.activeWalId = std::max(db_.manifest_.activeWalId, maxWalId == 0 ? 1ull : maxWalId);

        unsigned long long maxTableId = 0;
        for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
            maxTableId = std::max(maxTableId, db_.manifest_.tables[i].fileId);
            maxTableId = std::max(maxTableId, db_.manifest_.tables[i].blobFileId);
        }
        db_.manifest_.nextTableId = std::max(db_.manifest_.nextTableId, maxTableId + 1);
    }

    SwDbStatus loadManifestFile(const SwString& fileName) {
        std::string text;
        SwString error;
        if (!swDbPlatform::readWholeFile(swDbPlatform::joinPath(db_.dbPath_, fileName), text, &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        SwJsonDocument doc = SwJsonDocument::fromJson(text);
        if (!doc.isObject()) {
            return SwDbStatus(SwDbStatus::Corruption, "manifest is not a JSON object");
        }
        SwJsonObject obj = doc.object();
        db_.manifest_.manifestId = static_cast<unsigned long long>(obj["manifestId"].toLongLong());
        db_.manifest_.maxSequence = static_cast<unsigned long long>(obj["maxSequence"].toLongLong());
        db_.manifest_.replayFromWalId = static_cast<unsigned long long>(obj["replayFromWalId"].toLongLong());
        db_.manifest_.activeWalId = static_cast<unsigned long long>(obj["activeWalId"].toLongLong());
        db_.manifest_.nextWalId = static_cast<unsigned long long>(obj["nextWalId"].toLongLong());
        db_.manifest_.nextTableId = static_cast<unsigned long long>(obj["nextTableId"].toLongLong());
        db_.manifest_.tables.clear();
        if (obj["tables"].isArray()) {
            SwJsonArray tables = obj["tables"].toArray();
            for (std::size_t i = 0; i < tables.size(); ++i) {
                if (!tables[i].isObject()) {
                    continue;
                }
                SwJsonObject row = tables[i].toObject();
                swEmbeddedDbDetail::TableMeta_ meta;
                meta.kind = row["kind"].toInt();
                meta.level = row["level"].toInt();
                meta.fileId = static_cast<unsigned long long>(row["fileId"].toLongLong());
                meta.minSequence = static_cast<unsigned long long>(row["minSequence"].toLongLong());
                meta.maxSequence = static_cast<unsigned long long>(row["maxSequence"].toLongLong());
                meta.recordCount = static_cast<unsigned long long>(row["recordCount"].toLongLong());
                meta.blobFileId = static_cast<unsigned long long>(row["blobFileId"].toLongLong());
                meta.fileName = row["fileName"].toString();
                meta.indexName = row["indexName"].toString();
                db_.manifest_.tables.append(meta);
            }
        }
        return SwDbStatus::success();
    }

    SwDbStatus persistManifestLocked() {
        SwJsonObject root;
        root["manifestId"] = static_cast<long long>(db_.manifest_.manifestId);
        root["maxSequence"] = static_cast<long long>(db_.manifest_.maxSequence);
        root["replayFromWalId"] = static_cast<long long>(db_.manifest_.replayFromWalId);
        root["activeWalId"] = static_cast<long long>(db_.manifest_.activeWalId);
        root["nextWalId"] = static_cast<long long>(db_.manifest_.nextWalId);
        root["nextTableId"] = static_cast<long long>(db_.manifest_.nextTableId);
        SwJsonArray tables;
        for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
            const swEmbeddedDbDetail::TableMeta_& meta = db_.manifest_.tables[i];
            SwJsonObject row;
            row["kind"] = meta.kind;
            row["level"] = meta.level;
            row["fileId"] = static_cast<long long>(meta.fileId);
            row["minSequence"] = static_cast<long long>(meta.minSequence);
            row["maxSequence"] = static_cast<long long>(meta.maxSequence);
            row["recordCount"] = static_cast<long long>(meta.recordCount);
            row["blobFileId"] = static_cast<long long>(meta.blobFileId);
            row["fileName"] = meta.fileName.toStdString();
            row["indexName"] = meta.indexName.toStdString();
            tables.append(row);
        }
        root["tables"] = tables;

        SwJsonDocument doc(root);
        const SwString fileName = swEmbeddedDbDetail::manifestFileName_(db_.manifest_.manifestId);
        SwString error;
        if (!swDbPlatform::writeWholeFileAtomically(swDbPlatform::joinPath(db_.dbPath_, fileName),
                                                    doc.toJson(SwJsonDocument::JsonFormat::Compact),
                                                    &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        if (!swDbPlatform::writeWholeFileAtomically(swDbPlatform::joinPath(db_.dbPath_, "CURRENT"),
                                                    fileName.toStdString() + "\n",
                                                    &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        db_.manifest_.manifestId += 1;
        db_.publishShmNotificationLocked_();
        return SwDbStatus::success();
    }

    SwDbStatus persistOptions() {
        SwJsonObject root;
        root["dbPath"] = db_.dbPath_.toStdString();
        root["readOnly"] = db_.options_.readOnly;
        root["lazyWrite"] = db_.options_.lazyWrite;
        root["commitWindowMs"] = static_cast<long long>(db_.options_.commitWindowMs);
        root["memTableBytes"] = static_cast<long long>(db_.options_.memTableBytes);
        root["inlineBlobThresholdBytes"] = static_cast<long long>(db_.options_.inlineBlobThresholdBytes);
        root["readCacheBytes"] = static_cast<long long>(db_.options_.readCacheBytes);
        root["maxBackgroundJobs"] = db_.options_.maxBackgroundJobs;
        root["enableShmNotifications"] = db_.options_.enableShmNotifications;
        SwJsonDocument doc(root);
        SwString error;
        if (!swDbPlatform::writeWholeFileAtomically(swDbPlatform::joinPath(db_.dbPath_, "OPTIONS.json"),
                                                    doc.toJson(SwJsonDocument::JsonFormat::Pretty),
                                                    &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        return SwDbStatus::success();
    }

private:
    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline SwDbStatus SwEmbeddedDb::loadFromDisk_(bool createIfMissing) {
    return swEmbeddedDbDetail::ManifestStore_(*this).loadFromDisk(createIfMissing);
}

inline void SwEmbeddedDb::normalizeIdsLocked_() {
    swEmbeddedDbDetail::ManifestStore_(*this).normalizeIdsLocked();
}

inline SwDbStatus SwEmbeddedDb::loadManifestFile_(const SwString& fileName) {
    return swEmbeddedDbDetail::ManifestStore_(*this).loadManifestFile(fileName);
}

inline SwDbStatus SwEmbeddedDb::persistManifestLocked_() {
    return swEmbeddedDbDetail::ManifestStore_(*this).persistManifestLocked();
}

inline SwDbStatus SwEmbeddedDb::persistOptions_() {
    return swEmbeddedDbDetail::ManifestStore_(*this).persistOptions();
}
