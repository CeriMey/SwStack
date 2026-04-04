namespace swEmbeddedDbDetail {

class WalManager_ {
public:
    explicit WalManager_(SwEmbeddedDb& db)
        : db_(db) {
    }

    SwDbStatus openActiveWal() {
        db_.activeWalFile_.close();
        SwString error;
        if (!db_.activeWalFile_.open(swDbPlatform::joinPath(db_.walDir_, swEmbeddedDbDetail::walFileName_(db_.manifest_.activeWalId)),
                                     swDbPlatform::RandomAccessFile::OpenMode::AppendCreateBuffered,
                                     &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        const unsigned long long reserveBytes =
            std::max<unsigned long long>(db_.options_.memTableBytes, 1ull * 1024ull * 1024ull);
        db_.walScratch_.reserve(static_cast<std::size_t>(std::min<unsigned long long>(reserveBytes, 256ull * 1024ull)));
        (void)db_.activeWalFile_.reserveAllocation(reserveBytes, nullptr);
        return SwDbStatus::success();
    }

    SwDbStatus openTablesLocked() {
        db_.tableHandles_.clear();
        for (std::size_t i = 0; i < db_.manifest_.tables.size(); ++i) {
            std::shared_ptr<swEmbeddedDbDetail::TableHandle_> handle(new swEmbeddedDbDetail::TableHandle_());
            SwDbStatus status;
            if (!handle->open(db_.dbPath_, db_.manifest_.tables[i], db_.options_, db_.readCacheManager_, status)) {
                return status;
            }
            db_.tableHandles_[db_.manifest_.tables[i].fileName] = handle;
        }
        db_.rebuildTableCachesLocked_();
        return SwDbStatus::success();
    }

    SwDbStatus replayWalLocked() {
        db_.mutable_ = swEmbeddedDbDetail::MemTable_();
        db_.mutable_.walId = db_.manifest_.activeWalId;
        db_.prepareMutableMemTableLocked_();
        db_.lastVisibleSequence_ = db_.manifest_.maxSequence;
        const SwList<SwString> walFiles = swDbPlatform::listFiles(db_.walDir_, "WAL-", ".log");
        for (std::size_t i = 0; i < walFiles.size(); ++i) {
            const unsigned long long walId =
                swEmbeddedDbDetail::parseNumericSuffix_(swDbPlatform::fileName(walFiles[i]), "WAL-", ".log");
            if (walId < db_.manifest_.replayFromWalId) {
                continue;
            }
            const SwDbStatus status = replayWalFileLocked(walId, walFiles[i]);
            if (!status.ok()) {
                return status;
            }
        }
        return SwDbStatus::success();
    }

    SwDbStatus replayWalFileLocked(unsigned long long walId, const SwString& walPath) {
        std::string walBytes;
        SwString error;
        if (!swDbPlatform::readWholeFile(walPath, walBytes, &error)) {
            return SwDbStatus(SwDbStatus::IoError, error);
        }
        if (walBytes.empty()) {
            return SwDbStatus::success();
        }
        const char* cur = walBytes.data();
        const char* end = walBytes.data() + walBytes.size();
        while (cur < end) {
            if (end - cur < 16 || std::memcmp(cur, swEmbeddedDbDetail::kWalMagic_, 8) != 0) {
                break;
            }
            cur += 8;
            unsigned int payloadBytes = 0;
            unsigned int crc = 0;
            if (!swEmbeddedDbDetail::readU32LE_(cur, end, payloadBytes) ||
                !swEmbeddedDbDetail::readU32LE_(cur, end, crc) ||
                static_cast<std::size_t>(end - cur) < payloadBytes) {
                break;
            }
            SwByteArray payload(cur, payloadBytes);
            if (swEmbeddedDbDetail::crc32Bytes_(payload) != crc) {
                break;
            }
            cur += payloadBytes;
            unsigned long long sequence = 0;
            SwDbWriteBatch batch;
            if (!decodeWalPayload(payload, sequence, batch)) {
                break;
            }
            if (sequence <= db_.manifest_.maxSequence) {
                continue;
            }
            db_.mutable_.walId = walId;
            db_.applyBatchLocked_(sequence, batch);
            db_.lastVisibleSequence_ = std::max(db_.lastVisibleSequence_, sequence);
        }
        return SwDbStatus::success();
    }

    static bool decodeWalPayload(const SwByteArray& payload,
                                 unsigned long long& sequence,
                                 SwDbWriteBatch& outBatch) {
        const char* cur = payload.constData();
        const char* end = cur + payload.size();
        outBatch.clear();
        if (!swEmbeddedDbDetail::readU64LE_(cur, end, sequence)) {
            return false;
        }
        unsigned int opCount = 0;
        if (!swEmbeddedDbDetail::readU32LE_(cur, end, opCount)) {
            return false;
        }
        for (unsigned int i = 0; i < opCount; ++i) {
            unsigned int type = 0;
            SwByteArray primaryKey;
            if (!swEmbeddedDbDetail::readU8_(cur, end, type) ||
                !swEmbeddedDbDetail::readBytes_(cur, end, primaryKey)) {
                return false;
            }
            if (type == 0u) {
                SwByteArray value;
                SwMap<SwString, SwList<SwByteArray>> secondaryKeys;
                if (!swEmbeddedDbDetail::readBytes_(cur, end, value) ||
                    !swEmbeddedDbDetail::decodeSecondaryKeys_(cur, end, secondaryKeys)) {
                    return false;
                }
                outBatch.put(primaryKey, value, secondaryKeys);
            } else if (type == 2u) {
                unsigned long long blobFileId = 0;
                unsigned long long blobOffset = 0;
                unsigned int blobLength = 0;
                unsigned int blobCrc32 = 0;
                unsigned int blobFlags = 0;
                SwMap<SwString, SwList<SwByteArray>> secondaryKeys;
                if (!swEmbeddedDbDetail::readU64LE_(cur, end, blobFileId) ||
                    !swEmbeddedDbDetail::readU64LE_(cur, end, blobOffset) ||
                    !swEmbeddedDbDetail::readU32LE_(cur, end, blobLength) ||
                    !swEmbeddedDbDetail::readU32LE_(cur, end, blobCrc32) ||
                    !swEmbeddedDbDetail::readU32LE_(cur, end, blobFlags) ||
                    !swEmbeddedDbDetail::decodeSecondaryKeys_(cur, end, secondaryKeys)) {
                    return false;
                }
                outBatch.putBlobRef(
                    primaryKey, blobFileId, blobOffset, blobLength, blobCrc32, blobFlags, secondaryKeys);
            } else if (type == 1u) {
                outBatch.erase(primaryKey);
            } else {
                return false;
            }
        }
        return cur == end;
    }

    static SwByteArray encodeWalPayload(unsigned long long sequence, const SwDbWriteBatch& batch) {
        SwByteArray payload;
        payload.reserve(estimateWalPayloadBytes(batch));
        swEmbeddedDbDetail::appendU64LE_(payload, sequence);
        swEmbeddedDbDetail::appendU32LE_(payload, static_cast<unsigned int>(batch.operations().size()));
        for (std::size_t i = 0; i < batch.operations().size(); ++i) {
            const SwDbWriteBatch::Operation& op = batch.operations()[i];
            swEmbeddedDbDetail::appendU8_(payload,
                                          op.type == SwDbWriteBatch::Operation::Erase
                                              ? 1u
                                              : (op.valueInline ? 0u : 2u));
            swEmbeddedDbDetail::appendBytes_(payload, op.primaryKey);
            if (op.type == SwDbWriteBatch::Operation::Put) {
                if (op.valueInline) {
                    swEmbeddedDbDetail::appendBytes_(payload, op.value);
                } else {
                    swEmbeddedDbDetail::appendU64LE_(payload, op.blobFileId);
                    swEmbeddedDbDetail::appendU64LE_(payload, op.blobOffset);
                    swEmbeddedDbDetail::appendU32LE_(payload, op.blobLength);
                    swEmbeddedDbDetail::appendU32LE_(payload, op.blobCrc32);
                    swEmbeddedDbDetail::appendU32LE_(payload, op.blobFlags);
                }
                swEmbeddedDbDetail::encodeSecondaryKeys_(op.secondaryKeys, payload);
            }
        }
        return payload;
    }

    static void appendWalFrame(SwByteArray& out, unsigned long long sequence, const SwDbWriteBatch& batch) {
        const std::size_t frameOffset = out.size();
        const unsigned int payloadBytes = static_cast<unsigned int>(estimateWalPayloadBytes(batch));
        const std::size_t frameBytes = 16u + static_cast<std::size_t>(payloadBytes);
        out.resize(frameOffset + frameBytes);

        char* frame = out.data() + static_cast<std::ptrdiff_t>(frameOffset);
        std::memcpy(frame, swEmbeddedDbDetail::kWalMagic_, 8u);
        patchU32LE_(frame + 8u, payloadBytes);
        char* payload = frame + 16u;
        writeWalPayload_(payload, sequence, batch);
        patchU32LE_(frame + 12u, swEmbeddedDbDetail::crc32Bytes_(frame + 16u, payloadBytes));
    }

private:
    static void writeU8To_(char*& out, unsigned int value) {
        *out++ = static_cast<char>(value & 0xffu);
    }

    static void writeU32LETo_(char*& out, unsigned int value) {
        out[0] = static_cast<char>(value & 0xffu);
        out[1] = static_cast<char>((value >> 8) & 0xffu);
        out[2] = static_cast<char>((value >> 16) & 0xffu);
        out[3] = static_cast<char>((value >> 24) & 0xffu);
        out += 4;
    }

    static void writeU64LETo_(char*& out, unsigned long long value) {
        out[0] = static_cast<char>(value & 0xffu);
        out[1] = static_cast<char>((value >> 8) & 0xffu);
        out[2] = static_cast<char>((value >> 16) & 0xffu);
        out[3] = static_cast<char>((value >> 24) & 0xffu);
        out[4] = static_cast<char>((value >> 32) & 0xffu);
        out[5] = static_cast<char>((value >> 40) & 0xffu);
        out[6] = static_cast<char>((value >> 48) & 0xffu);
        out[7] = static_cast<char>((value >> 56) & 0xffu);
        out += 8;
    }

    static void patchU32LE_(char* out, unsigned int value) {
        out[0] = static_cast<char>(value & 0xffu);
        out[1] = static_cast<char>((value >> 8) & 0xffu);
        out[2] = static_cast<char>((value >> 16) & 0xffu);
        out[3] = static_cast<char>((value >> 24) & 0xffu);
    }

    static void writeBytesTo_(char*& out, const SwByteArray& bytes) {
        writeU32LETo_(out, static_cast<unsigned int>(bytes.size()));
        if (!bytes.isEmpty()) {
            std::memcpy(out, bytes.constData(), bytes.size());
            out += bytes.size();
        }
    }

    static void writeStringTo_(char*& out, const SwString& value) {
        writeU32LETo_(out, static_cast<unsigned int>(value.size()));
        if (!value.isEmpty()) {
            std::memcpy(out, value.data(), value.size());
            out += value.size();
        }
    }

    static void writeSecondaryKeysTo_(char*& out, const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys) {
        writeU32LETo_(out, static_cast<unsigned int>(secondaryKeys.size()));
        for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = secondaryKeys.begin();
             it != secondaryKeys.end();
             ++it) {
            writeStringTo_(out, it.key());
            writeU32LETo_(out, static_cast<unsigned int>(it.value().size()));
            for (std::size_t i = 0; i < it.value().size(); ++i) {
                writeBytesTo_(out, it.value()[i]);
            }
        }
    }

    static void writeWalPayload_(char* out, unsigned long long sequence, const SwDbWriteBatch& batch) {
        char* cursor = out;
        writeU64LETo_(cursor, sequence);
        writeU32LETo_(cursor, static_cast<unsigned int>(batch.operations().size()));
        for (std::size_t i = 0; i < batch.operations().size(); ++i) {
            const SwDbWriteBatch::Operation& op = batch.operations()[i];
            writeU8To_(cursor,
                       op.type == SwDbWriteBatch::Operation::Erase
                           ? 1u
                           : (op.valueInline ? 0u : 2u));
            writeBytesTo_(cursor, op.primaryKey);
            if (op.type != SwDbWriteBatch::Operation::Put) {
                continue;
            }
            if (op.valueInline) {
                writeBytesTo_(cursor, op.value);
            } else {
                writeU64LETo_(cursor, op.blobFileId);
                writeU64LETo_(cursor, op.blobOffset);
                writeU32LETo_(cursor, op.blobLength);
                writeU32LETo_(cursor, op.blobCrc32);
                writeU32LETo_(cursor, op.blobFlags);
            }
            writeSecondaryKeysTo_(cursor, op.secondaryKeys);
        }
    }

    static unsigned long long estimateSecondaryKeysBytes(const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys) {
        unsigned long long bytes = 4u;
        for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = secondaryKeys.begin(); it != secondaryKeys.end(); ++it) {
            bytes += 4u + static_cast<unsigned long long>(it.key().size());
            bytes += 4u;
            for (std::size_t i = 0; i < it.value().size(); ++i) {
                bytes += 4u + static_cast<unsigned long long>(it.value()[i].size());
            }
        }
        return bytes;
    }

    static unsigned long long estimateWalPayloadBytes(const SwDbWriteBatch& batch) {
        return batch.estimatedWalBytes();
    }

    SwEmbeddedDb& db_;
};

} // namespace swEmbeddedDbDetail

inline SwDbStatus SwEmbeddedDb::openActiveWal_() {
    return swEmbeddedDbDetail::WalManager_(*this).openActiveWal();
}

inline SwDbStatus SwEmbeddedDb::openTablesLocked_() {
    return swEmbeddedDbDetail::WalManager_(*this).openTablesLocked();
}

inline SwDbStatus SwEmbeddedDb::replayWalLocked_() {
    return swEmbeddedDbDetail::WalManager_(*this).replayWalLocked();
}

inline SwDbStatus SwEmbeddedDb::replayWalFileLocked_(unsigned long long walId, const SwString& walPath) {
    return swEmbeddedDbDetail::WalManager_(*this).replayWalFileLocked(walId, walPath);
}

inline bool SwEmbeddedDb::decodeWalPayload_(const SwByteArray& payload,
                                            unsigned long long& sequence,
                                            SwDbWriteBatch& outBatch) {
    return swEmbeddedDbDetail::WalManager_::decodeWalPayload(payload, sequence, outBatch);
}

inline SwByteArray SwEmbeddedDb::encodeWalPayload_(unsigned long long sequence, const SwDbWriteBatch& batch) {
    return swEmbeddedDbDetail::WalManager_::encodeWalPayload(sequence, batch);
}
