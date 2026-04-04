inline bool SwEmbeddedDb::externalizeBlobValue_(unsigned long long blobFileId,
                                                swEmbeddedDbDetail::PrimaryRecord_& record) {
    if (blobFileId == 0 || record.deleted || !record.inlineValue) {
        return true;
    }
    if (static_cast<unsigned long long>(record.value.size()) <= options_.inlineBlobThresholdBytes) {
        return true;
    }

    const SwString blobPath = swDbPlatform::joinPath(blobDir_, swEmbeddedDbDetail::blobFileName_(blobFileId));
    swDbPlatform::RandomAccessFile blobFile;
    SwString error;
    if (!blobFile.open(blobPath, swDbPlatform::RandomAccessFile::OpenMode::AppendCreate, &error)) {
        swCError(kSwLogCategory_SwEmbeddedDb) << error;
        return false;
    }
    if (!externalizeBlobValueToFile_(blobFile, blobFileId, record)) {
        return false;
    }
    if (!blobFile.sync(&error)) {
        swCError(kSwLogCategory_SwEmbeddedDb) << error;
        return false;
    }
    return true;
}

inline bool SwEmbeddedDb::externalizeBlobValueToFile_(swDbPlatform::RandomAccessFile& blobFile,
                                                      unsigned long long blobFileId,
                                                      swEmbeddedDbDetail::PrimaryRecord_& record) {
    if (blobFileId == 0 || record.deleted || !record.inlineValue) {
        return true;
    }
    if (static_cast<unsigned long long>(record.value.size()) <= options_.inlineBlobThresholdBytes) {
        return true;
    }

    SwString error;
    unsigned long long offset = 0;
    if (!record.value.isEmpty() &&
        !blobFile.append(record.value.constData(), record.value.size(), &offset, &error)) {
        swCError(kSwLogCategory_SwEmbeddedDb) << error;
        return false;
    }

    swEmbeddedDbDetail::BlobRef_ ref;
    ref.valid = true;
    ref.fileId = blobFileId;
    ref.offset = offset;
    ref.length = static_cast<unsigned int>(record.value.size());
    ref.crc32 = swEmbeddedDbDetail::crc32Bytes_(record.value);
    ref.flags = 0u;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.blobBytesWritten += static_cast<unsigned long long>(record.value.size());
    }

    record.blobRef = ref;
    record.value.clear();
    record.inlineValue = false;
    return true;
}

inline bool SwEmbeddedDb::blobFileHasData_(unsigned long long blobFileId) const {
    if (blobFileId == 0) {
        return false;
    }

    swDbPlatform::RandomAccessFile file;
    if (!file.open(swDbPlatform::joinPath(blobDir_, swEmbeddedDbDetail::blobFileName_(blobFileId)),
                   swDbPlatform::RandomAccessFile::OpenMode::ReadOnly,
                   nullptr)) {
        return false;
    }
    return file.size() > 0;
}
