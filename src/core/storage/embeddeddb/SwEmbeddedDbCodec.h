namespace swEmbeddedDbDetail {

static const char kTableMagic_[] = "SWDBSST1";
static const char kFooterMagic_[] = "SWDBFTR1";
static const char kWalMagic_[] = "SWDBWAL1";

enum TableKind_ {
    TableKindPrimary = 1,
    TableKindIndex = 2
};

enum RecordFlags_ {
    RecordDeleted = 0x1,
    RecordBlobRef = 0x2
};

inline unsigned int crc32Bytes_(const char* data, std::size_t bytes) {
    return static_cast<unsigned int>(mz_crc32(0, reinterpret_cast<const unsigned char*>(data), bytes));
}

inline unsigned int crc32Bytes_(const SwByteArray& data) {
    return data.isEmpty() ? 0u : crc32Bytes_(data.constData(), data.size());
}

inline unsigned long long stableHash64_(const SwString& text) {
    const std::string bytes = text.toStdString();
    unsigned long long hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        hash ^= static_cast<unsigned long long>(static_cast<unsigned char>(bytes[i]));
        hash *= 1099511628211ull;
    }
    return hash;
}

inline void appendU8_(SwByteArray& out, unsigned int value) {
    out.append(static_cast<char>(value & 0xffu));
}

inline void appendU32LE_(SwByteArray& out, unsigned int value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8) & 0xffu),
        static_cast<char>((value >> 16) & 0xffu),
        static_cast<char>((value >> 24) & 0xffu)
    };
    out.append(bytes, sizeof(bytes));
}

inline void appendU64LE_(SwByteArray& out, unsigned long long value) {
    const char bytes[8] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8) & 0xffu),
        static_cast<char>((value >> 16) & 0xffu),
        static_cast<char>((value >> 24) & 0xffu),
        static_cast<char>((value >> 32) & 0xffu),
        static_cast<char>((value >> 40) & 0xffu),
        static_cast<char>((value >> 48) & 0xffu),
        static_cast<char>((value >> 56) & 0xffu)
    };
    out.append(bytes, sizeof(bytes));
}

inline bool readU8_(const char*& cur, const char* end, unsigned int& value) {
    if (cur >= end) {
        return false;
    }
    value = static_cast<unsigned int>(static_cast<unsigned char>(*cur));
    ++cur;
    return true;
}

inline bool readU32LE_(const char*& cur, const char* end, unsigned int& value) {
    if (end - cur < 4) {
        return false;
    }
    value = static_cast<unsigned int>(static_cast<unsigned char>(cur[0])) |
            (static_cast<unsigned int>(static_cast<unsigned char>(cur[1])) << 8) |
            (static_cast<unsigned int>(static_cast<unsigned char>(cur[2])) << 16) |
            (static_cast<unsigned int>(static_cast<unsigned char>(cur[3])) << 24);
    cur += 4;
    return true;
}

inline bool readU64LE_(const char*& cur, const char* end, unsigned long long& value) {
    if (end - cur < 8) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<unsigned long long>(static_cast<unsigned char>(cur[i])) << (i * 8);
    }
    cur += 8;
    return true;
}

inline void appendBytes_(SwByteArray& out, const SwByteArray& bytes) {
    appendU32LE_(out, static_cast<unsigned int>(bytes.size()));
    if (!bytes.isEmpty()) {
        out.append(bytes.constData(), bytes.size());
    }
}

inline bool readBytes_(const char*& cur, const char* end, SwByteArray& out) {
    unsigned int len = 0;
    if (!readU32LE_(cur, end, len) || static_cast<std::size_t>(end - cur) < len) {
        return false;
    }
    out = SwByteArray(cur, len);
    cur += len;
    return true;
}

inline bool readBytesView_(const char*& cur,
                           const char* end,
                           const char*& dataOut,
                           unsigned int& bytesOut) {
    dataOut = nullptr;
    bytesOut = 0;
    unsigned int len = 0;
    if (!readU32LE_(cur, end, len) || static_cast<std::size_t>(end - cur) < len) {
        return false;
    }
    dataOut = cur;
    bytesOut = len;
    cur += len;
    return true;
}

inline void appendString_(SwByteArray& out, const SwString& value) {
    appendU32LE_(out, static_cast<unsigned int>(value.size()));
    if (!value.isEmpty()) {
        out.append(value.data(), value.size());
    }
}

inline bool readString_(const char*& cur, const char* end, SwString& out) {
    unsigned int len = 0;
    if (!readU32LE_(cur, end, len) || static_cast<std::size_t>(end - cur) < len) {
        return false;
    }
    out = SwString(cur, len);
    cur += len;
    return true;
}

inline void encodeSecondaryKeys_(const SwMap<SwString, SwList<SwByteArray>>& secondaryKeys, SwByteArray& out) {
    appendU32LE_(out, static_cast<unsigned int>(secondaryKeys.size()));
    for (SwMap<SwString, SwList<SwByteArray>>::const_iterator it = secondaryKeys.begin(); it != secondaryKeys.end(); ++it) {
        appendString_(out, it.key());
        appendU32LE_(out, static_cast<unsigned int>(it.value().size()));
        for (std::size_t i = 0; i < it.value().size(); ++i) {
            appendBytes_(out, it.value()[i]);
        }
    }
}

inline bool decodeSecondaryKeys_(const char*& cur, const char* end, SwMap<SwString, SwList<SwByteArray>>& out) {
    out.clear();
    unsigned int indexCount = 0;
    if (!readU32LE_(cur, end, indexCount)) {
        return false;
    }
    for (unsigned int i = 0; i < indexCount; ++i) {
        SwString indexName;
        if (!readString_(cur, end, indexName)) {
            return false;
        }
        unsigned int keyCount = 0;
        if (!readU32LE_(cur, end, keyCount)) {
            return false;
        }
        SwList<SwByteArray> keys;
        for (unsigned int j = 0; j < keyCount; ++j) {
            SwByteArray key;
            if (!readBytes_(cur, end, key)) {
                return false;
            }
            keys.append(key);
        }
        out[indexName] = keys;
    }
    return true;
}

inline void appendEncodedFieldKey_(SwByteArray& out, const SwByteArray& key) {
    if (!key.contains('\0')) {
        if (!key.isEmpty()) {
            out.append(key);
        }
        out.append("\0\0", 2);
        return;
    }
    for (std::size_t i = 0; i < key.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(key[i]);
        if (ch == 0u) {
            out.append('\0');
            out.append(static_cast<char>(0xffu));
        } else {
            out.append(static_cast<char>(ch));
        }
    }
    out.append("\0\0", 2);
}

inline SwByteArray encodeFieldKey_(const SwByteArray& key) {
    SwByteArray out;
    out.reserve(key.size() + 2u);
    appendEncodedFieldKey_(out, key);
    return out;
}

inline bool decodeFieldKey_(const char*& cur, const char* end, SwByteArray& out) {
    out.clear();
    while (cur < end) {
        const unsigned char ch = static_cast<unsigned char>(*cur++);
        if (ch != 0u) {
            out.append(static_cast<char>(ch));
            continue;
        }
        if (cur >= end) {
            return false;
        }
        const unsigned char tag = static_cast<unsigned char>(*cur++);
        if (tag == 0u) {
            return true;
        }
        if (tag != 0xffu) {
            return false;
        }
        out.append('\0');
    }
    return false;
}

inline SwByteArray encodeIndexCompositeKey_(const SwByteArray& secondaryKey, const SwByteArray& primaryKey) {
    SwByteArray out;
    out.reserve(secondaryKey.size() + primaryKey.size() + 4u);
    appendEncodedFieldKey_(out, secondaryKey);
    appendEncodedFieldKey_(out, primaryKey);
    return out;
}

inline bool decodeIndexCompositeKey_(const SwByteArray& compositeKey, SwByteArray& secondaryKey, SwByteArray& primaryKey) {
    const char* cur = compositeKey.constData();
    const char* end = cur + compositeKey.size();
    return decodeFieldKey_(cur, end, secondaryKey) && decodeFieldKey_(cur, end, primaryKey) && cur == end;
}

inline void encodePrimaryPayload_(const PrimaryRecord_& record, SwByteArray& out) {
    out.clear();
    appendU8_(out, record.inlineValue ? 0u : 1u);
    SwByteArray secondaryBytes;
    encodeSecondaryKeys_(record.secondaryKeys, secondaryBytes);
    appendBytes_(out, secondaryBytes);
    if (record.inlineValue) {
        appendBytes_(out, record.value);
        return;
    }
    appendU64LE_(out, record.blobRef.fileId);
    appendU64LE_(out, record.blobRef.offset);
    appendU32LE_(out, record.blobRef.length);
    appendU32LE_(out, record.blobRef.crc32);
    appendU32LE_(out, record.blobRef.flags);
}

inline bool decodePrimaryPayload_(const char* payloadData,
                                  std::size_t payloadBytes,
                                  PrimaryRecord_& out) {
    const char* cur = payloadData;
    const char* end = cur + payloadBytes;
    unsigned int mode = 0;
    if (!readU8_(cur, end, mode)) {
        return false;
    }
    const char* secondaryData = nullptr;
    unsigned int secondaryBytes = 0;
    if (!readBytesView_(cur, end, secondaryData, secondaryBytes)) {
        return false;
    }
    const char* secCur = secondaryData;
    const char* secEnd = secondaryData + secondaryBytes;
    if (!decodeSecondaryKeys_(secCur, secEnd, out.secondaryKeys)) {
        return false;
    }
    if (mode == 0u) {
        out.inlineValue = true;
        out.blobRef = BlobRef_();
        const char* valueData = nullptr;
        unsigned int valueBytes = 0;
        if (!readBytesView_(cur, end, valueData, valueBytes) || cur != end) {
            return false;
        }
        out.value = SwByteArray(valueData, valueBytes);
        return true;
    }
    out.inlineValue = false;
    out.value.clear();
    out.blobRef.valid = true;
    unsigned int length = 0;
    unsigned int crc32 = 0;
    unsigned int flags = 0;
    return readU64LE_(cur, end, out.blobRef.fileId) &&
           readU64LE_(cur, end, out.blobRef.offset) &&
           readU32LE_(cur, end, length) &&
           readU32LE_(cur, end, crc32) &&
           readU32LE_(cur, end, flags) &&
           (out.blobRef.length = length, true) &&
           (out.blobRef.crc32 = crc32, true) &&
           (out.blobRef.flags = flags, true) &&
           cur == end;
}

inline bool decodePrimaryPayload_(const SwByteArray& payload, PrimaryRecord_& out) {
    return decodePrimaryPayload_(payload.constData(), payload.size(), out);
}

inline void encodeTableRecord_(const TableRecord_& record, SwByteArray& out) {
    appendBytes_(out, record.userKey);
    appendU64LE_(out, record.sequence);
    appendU32LE_(out, record.flags);
    appendBytes_(out, record.payload);
}

inline bool decodeTableRecord_(const char*& cur, const char* end, TableRecord_& out) {
    return readBytes_(cur, end, out.userKey) &&
           readU64LE_(cur, end, out.sequence) &&
           readU32LE_(cur, end, out.flags) &&
           readBytes_(cur, end, out.payload);
}

inline bool decodeTableRecordView_(const char*& cur, const char* end, TableRecordView_& out) {
    return readBytesView_(cur, end, out.userKeyData, out.userKeyBytes) &&
           readU64LE_(cur, end, out.sequence) &&
           readU32LE_(cur, end, out.flags) &&
           readBytesView_(cur, end, out.payloadData, out.payloadBytes);
}

inline unsigned long long parseNumericSuffix_(const SwString& fileName,
                                              const SwString& prefix,
                                              const SwString& suffix) {
    if (!fileName.startsWith(prefix) || !fileName.endsWith(suffix)) {
        return 0;
    }
    return static_cast<unsigned long long>(
        fileName.mid(static_cast<int>(prefix.size()),
                     static_cast<int>(fileName.size() - prefix.size() - suffix.size())).toLongLong());
}

inline SwString blobFileName_(unsigned long long fileId) {
    return SwString("BLOB-") + SwString::number(fileId) + ".dat";
}

inline SwString walFileName_(unsigned long long fileId) {
    return SwString("WAL-") + SwString::number(fileId) + ".log";
}

inline SwString manifestFileName_(unsigned long long fileId) {
    return SwString("MANIFEST-") + SwString::number(fileId) + ".dbm";
}

inline bool compareTableMetaNewestFirst_(const TableMeta_& lhs, const TableMeta_& rhs) {
    if (lhs.level != rhs.level) {
        return lhs.level < rhs.level;
    }
    return lhs.fileId > rhs.fileId;
}

inline bool compareTableMetaOldestFirst_(const TableMeta_& lhs, const TableMeta_& rhs) {
    if (lhs.level != rhs.level) {
        return lhs.level > rhs.level;
    }
    return lhs.fileId < rhs.fileId;
}

