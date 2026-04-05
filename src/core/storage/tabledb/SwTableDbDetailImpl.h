#pragma once

#include "SwByteArray.h"
#include "SwJsonDocument.h"

#include <atomic>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace swTableDbDetail {

inline SwString normalizedType_(const SwString& type) {
    return type.trimmed().toLower();
}

inline SwString isoTimestampFromUnixSeconds_(std::time_t utcSeconds) {
    std::tm utcTime {};
#if defined(_WIN32)
    gmtime_s(&utcTime, &utcSeconds);
#else
    gmtime_r(&utcSeconds, &utcTime);
#endif
    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(4) << (utcTime.tm_year + 1900)
           << "-" << std::setw(2) << (utcTime.tm_mon + 1)
           << "-" << std::setw(2) << utcTime.tm_mday
           << "T" << std::setw(2) << utcTime.tm_hour
           << ":" << std::setw(2) << utcTime.tm_min
           << ":" << std::setw(2) << utcTime.tm_sec
           << "Z";
    return SwString(stream.str());
}

inline SwByteArray hexWidth64_(std::uint64_t value) {
    static const char* digits = "0123456789abcdef";
    SwByteArray encoded(16, '0');
    for (int i = 15; i >= 0; --i) {
        encoded[static_cast<std::size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
    }
    return encoded;
}

inline SwString indexNameForField_(const SwTableSchema& schema, const SwString& columnId) {
    if (columnId == "createdAt" || columnId == "updatedAt") {
        return "swtable." + schema.tableId + ".builtin." + columnId;
    }
    for (std::size_t i = 0; i < schema.indexes.size(); ++i) {
        if (schema.indexes[i].columnId == columnId) {
            const SwString suffix = schema.indexes[i].indexId.trimmed().isEmpty()
                                        ? schema.indexes[i].columnId
                                        : schema.indexes[i].indexId;
            return "swtable." + schema.tableId + ".idx." + suffix;
        }
    }
    return SwString();
}

inline SwByteArray rowSuffix_(const SwString& rowId) {
    return SwByteArray(rowId.toUtf8()).toHex();
}

inline bool parseLongLongString_(const SwString& value, long long& outValue) {
    bool ok = false;
    outValue = value.trimmed().toLongLong(&ok);
    return ok;
}

inline bool parseDoubleString_(const SwString& value, double& outValue) {
    bool ok = false;
    outValue = value.trimmed().toDouble(&ok);
    return ok;
}

inline SwString toFieldString_(const SwJsonValue& value) {
    return SwString(value.toString());
}

} // namespace swTableDbDetail

inline void SwTableDb::setStorageDir(const SwString& storageDir) {
    SwMutexLocker locker(&mutex_);
    storageDir_ = storageDir.trimmed().isEmpty() ? SwString("tabledb") : storageDir.trimmed();
}

inline const SwString& SwTableDb::storageDir() const {
    return storageDir_;
}

inline SwDbStatus SwTableDb::open(const SwEmbeddedDbOptions& options) {
    SwMutexLocker locker(&mutex_);
    if (opened_) {
        return SwDbStatus::success();
    }
    SwEmbeddedDbOptions effective = options;
    if (effective.dbPath.trimmed().isEmpty()) {
        effective.dbPath = storageDir_.trimmed().isEmpty() ? SwString("tabledb/db") : (storageDir_ + "/db");
    }
    const SwDbStatus status = db_.open(effective);
    if (!status.ok()) {
        return status;
    }
    opened_ = true;
    return SwDbStatus::success();
}

inline void SwTableDb::close() {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return;
    }
    db_.close();
    opened_ = false;
}

inline bool SwTableDb::isOpen() const {
    return opened_;
}

inline SwDbStatus SwTableDb::sync() {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Table database not open");
    }
    return db_.sync();
}

inline SwDbStatus SwTableDb::validateSchema(const SwTableSchema& schema) {
    return validateSchema_(schema);
}

inline SwByteArray SwTableDb::rowPrefix_(const SwString& tableId) {
    return SwByteArray(("table/" + tableId + "/row/").toUtf8());
}

inline SwByteArray SwTableDb::rowPrimaryKey_(const SwString& tableId, const SwString& rowId) {
    return rowPrefix_(tableId) + SwByteArray(rowId.toUtf8());
}

inline SwByteArray SwTableDb::rowPrimaryStart_(const SwString& tableId, const SwString& rowPrefixValue) {
    return rowPrimaryKey_(tableId, rowPrefixValue);
}

inline SwByteArray SwTableDb::rowPrimaryEnd_(const SwString& tableId, const SwString& rowPrefixValue) {
    SwByteArray end = rowPrimaryKey_(tableId, rowPrefixValue);
    end.append('\xff');
    return end;
}

inline SwString SwTableDb::nowIso_() {
    return swTableDbDetail::isoTimestampFromUnixSeconds_(std::time(nullptr));
}

inline SwString SwTableDb::nextRowId_() {
    static std::atomic<unsigned long long> counter(0);
    const unsigned long long seq = ++counter;
    return "row-" + SwString::number(static_cast<long long>(std::time(nullptr))) + "-" + SwString::number(seq);
}

inline SwByteArray SwTableDb::jsonBytes_(const SwJsonObject& object) {
    return SwByteArray(SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact).toStdString());
}

inline bool SwTableDb::parseJsonObject_(const SwByteArray& bytes, SwJsonObject& outObject) {
    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(bytes.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        return false;
    }
    outObject = document.object();
    return true;
}

inline bool SwTableDb::isSupportedType_(const SwString& type) {
    const SwString normalized = swTableDbDetail::normalizedType_(type);
    return normalized == "string" || normalized == "integer" || normalized == "number" ||
           normalized == "boolean" || normalized == "datetime" || normalized == "json";
}

inline bool SwTableDb::isEmptyValue_(const SwJsonValue& value) {
    if (value.isNull()) {
        return true;
    }
    if (value.isString()) {
        return SwString(value.toString()).trimmed().isEmpty();
    }
    if (value.isObject()) {
        return value.toObject().isEmpty();
    }
    if (value.isArray()) {
        return value.toArray().isEmpty();
    }
    return false;
}

inline bool SwTableDb::isBuiltInField_(const SwString& columnId) {
    return columnId == "rowId" || columnId == "createdAt" || columnId == "updatedAt";
}

inline bool SwTableDb::requiresStringPrefixSupport_(const SwString& columnId, const SwString& type) {
    return columnId == "rowId" || columnId == "createdAt" || columnId == "updatedAt" ||
           swTableDbDetail::normalizedType_(type) == "string" ||
           swTableDbDetail::normalizedType_(type) == "datetime";
}

inline const SwTableColumn* SwTableDb::findColumn_(const SwTableSchema& schema, const SwString& columnId) {
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        if (schema.columns[i].columnId == columnId) {
            return &schema.columns[i];
        }
    }
    return nullptr;
}

inline const SwTableIndex* SwTableDb::findIndexByColumn_(const SwTableSchema& schema, const SwString& columnId) {
    for (std::size_t i = 0; i < schema.indexes.size(); ++i) {
        if (schema.indexes[i].columnId == columnId) {
            return &schema.indexes[i];
        }
    }
    return nullptr;
}

inline bool SwTableDb::hasDuplicateColumns_(const SwTableSchema& schema) {
    SwMap<SwString, bool> seen;
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        if (schema.columns[i].columnId.trimmed().isEmpty() || seen.contains(schema.columns[i].columnId)) {
            return true;
        }
        seen.insert(schema.columns[i].columnId, true);
    }
    return false;
}

inline bool SwTableDb::hasDuplicateIndexes_(const SwTableSchema& schema) {
    SwMap<SwString, bool> seen;
    for (std::size_t i = 0; i < schema.indexes.size(); ++i) {
        const SwString key = schema.indexes[i].indexId.trimmed().isEmpty() ? schema.indexes[i].columnId
                                                                            : schema.indexes[i].indexId;
        if (key.trimmed().isEmpty() || seen.contains(key)) {
            return true;
        }
        seen.insert(key, true);
    }
    return false;
}

inline SwDbStatus SwTableDb::validateSchema_(const SwTableSchema& schema) {
    if (schema.tableId.trimmed().isEmpty()) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "tableId is required");
    }
    if (hasDuplicateColumns_(schema)) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Duplicate column ids are not allowed");
    }
    if (hasDuplicateIndexes_(schema)) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Duplicate index ids are not allowed");
    }
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        if (!isSupportedType_(schema.columns[i].type)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Unsupported column type");
        }
    }
    for (std::size_t i = 0; i < schema.indexes.size(); ++i) {
        const SwTableColumn* column = findColumn_(schema, schema.indexes[i].columnId);
        if (!column) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Index references unknown column");
        }
        if (swTableDbDetail::normalizedType_(column->type) == "json") {
            return SwDbStatus(SwDbStatus::InvalidArgument, "JSON columns cannot be indexed");
        }
    }
    return SwDbStatus::success();
}
