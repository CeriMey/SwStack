#pragma once

#include "SwEmbeddedDb.h"
#include "SwMutex.h"
#include "auth/SwHttpAuthTypes.h"

#include <algorithm>
#include <cctype>
#include <vector>

struct SwHttpAuthRecoveryCodeStatus {
    int totalCodes = 0;
    int remainingCodes = 0;
    SwString generatedAt;
};

struct SwHttpAuthRecoveryCodeGeneration {
    SwList<SwString> codes;
    SwHttpAuthRecoveryCodeStatus status;
};

class SwHttpAuthRecoveryCodeStore {
public:
    SwHttpAuthRecoveryCodeStore() = default;

    void setConfig(const SwHttpAuthConfig& config) {
        SwMutexLocker locker(&m_mutex);
        m_config = config;
    }

    SwDbStatus open() {
        SwMutexLocker locker(&m_mutex);
        if (m_opened) {
            return SwDbStatus::success();
        }

        SwEmbeddedDbOptions options = m_config.dbOptions;
        options.dbPath = m_config.storageDir.trimmed().isEmpty()
                             ? SwString("auth/recovery-codes/db")
                             : (m_config.storageDir.trimmed() + "/recovery-codes/db");
        const SwDbStatus status = m_db.open(options);
        if (!status.ok()) {
            return status;
        }
        m_opened = true;
        return SwDbStatus::success();
    }

    void close() {
        SwMutexLocker locker(&m_mutex);
        if (!m_opened) {
            return;
        }
        m_db.close();
        m_opened = false;
    }

    bool isOpen() const {
        return m_opened;
    }

    SwDbStatus statusForAccount(const SwString& accountId, SwHttpAuthRecoveryCodeStatus* outStatus) {
        SwMutexLocker locker(&m_mutex);
        if (!m_opened) {
            return SwDbStatus(SwDbStatus::NotOpen, "Recovery code store not open");
        }
        if (!outStatus) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing recovery code status output");
        }
        *outStatus = statusForAccountLocked_(accountId.trimmed());
        return SwDbStatus::success();
    }

    SwDbStatus replaceCodes(const SwString& accountId,
                            int requestedCount,
                            SwHttpAuthRecoveryCodeGeneration* outGeneration) {
        SwMutexLocker locker(&m_mutex);
        if (!m_opened) {
            return SwDbStatus(SwDbStatus::NotOpen, "Recovery code store not open");
        }
        const SwString normalizedAccountId = accountId.trimmed();
        if (normalizedAccountId.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Account id is required");
        }
        if (!outGeneration) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing recovery code output");
        }

        const int count = std::max(1, std::min(requestedCount, 24));
        SwDbWriteBatch batch;
        appendAccountDeletesLocked_(normalizedAccountId, batch);

        SwHttpAuthRecoveryCodeGeneration generation;
        const SwString generatedAt = swHttpAuthDetail::currentIsoTimestamp();
        for (int i = 0; i < count; ++i) {
            SwString rawCode;
            SwString normalizedCode;
            do {
                rawCode = generateCode_();
                normalizedCode = normalizeCode_(rawCode);
            } while (rawCode.isEmpty() || containsCode_(generation.codes, rawCode));

            RecoveryCodeRecord_ record;
            record.accountId = normalizedAccountId;
            record.codeId = swHttpAuthDetail::generateId("recovery-code");
            record.codeHash = hashCode_(normalizedAccountId, normalizedCode);
            record.createdAt = generatedAt;
            record.updatedAt = generatedAt;
            generation.codes.append(rawCode);
            batch.put(primaryKey_(record), swHttpAuthDetail::jsonToBytes(recordToJson_(record)), secondaryKeys_(record));
        }

        const SwDbStatus status = m_db.write(batch);
        if (!status.ok()) {
            return status;
        }
        generation.status = statusForAccountLocked_(normalizedAccountId);
        *outGeneration = generation;
        return SwDbStatus::success();
    }

    SwDbStatus consumeCode(const SwString& accountId, const SwString& code) {
        SwMutexLocker locker(&m_mutex);
        if (!m_opened) {
            return SwDbStatus(SwDbStatus::NotOpen, "Recovery code store not open");
        }
        const SwString normalizedAccountId = accountId.trimmed();
        const SwString normalizedCode = normalizeCode_(code);
        if (normalizedAccountId.isEmpty() || normalizedCode.isEmpty()) {
            return SwDbStatus(SwDbStatus::NotFound, "Recovery code not found");
        }

        const SwString expectedHash = hashCode_(normalizedAccountId, normalizedCode);
        for (SwDbIterator it = scanAccountLocked_(normalizedAccountId); it.isValid(); it.next()) {
            RecoveryCodeRecord_ record;
            if (!parseRecord_(it.current().value, record) || !record.usedAt.trimmed().isEmpty()) {
                continue;
            }
            if (!swHttpAuthDetail::constantTimeEquals(SwByteArray(record.codeHash.toUtf8()),
                                                      SwByteArray(expectedHash.toUtf8()))) {
                continue;
            }
            record.usedAt = swHttpAuthDetail::currentIsoTimestamp();
            record.updatedAt = record.usedAt;
            SwDbWriteBatch batch;
            batch.put(primaryKey_(record), swHttpAuthDetail::jsonToBytes(recordToJson_(record)), secondaryKeys_(record));
            return m_db.write(batch);
        }
        return SwDbStatus(SwDbStatus::NotFound, "Recovery code not found");
    }

    SwDbStatus removeCodesForAccount(const SwString& accountId) {
        SwMutexLocker locker(&m_mutex);
        if (!m_opened) {
            return SwDbStatus(SwDbStatus::NotOpen, "Recovery code store not open");
        }
        SwDbWriteBatch batch;
        appendAccountDeletesLocked_(accountId.trimmed(), batch);
        return m_db.write(batch);
    }

private:
    struct RecoveryCodeRecord_ {
        SwString accountId;
        SwString codeId;
        SwString codeHash;
        SwString usedAt;
        SwString createdAt;
        SwString updatedAt;
    };

    static SwString normalizeCode_(const SwString& code) {
        std::string normalized;
        const std::string input = code.toStdString();
        normalized.reserve(input.size());
        for (std::size_t i = 0; i < input.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(input[i]);
            if (std::isalnum(c)) {
                normalized.push_back(static_cast<char>(std::toupper(c)));
            }
        }
        return SwString(normalized);
    }

    static SwString hashCode_(const SwString& accountId, const SwString& normalizedCode) {
        return swHttpAuthDetail::hashSha256(accountId.trimmed() + ":" + normalizedCode.trimmed().toUpper());
    }

    static SwString generateCode_() {
        static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        std::vector<unsigned char> bytes;
        if (!swHttpAuthDetail::fillRandomBytes(bytes, 12)) {
            return SwString();
        }

        std::string code;
        code.reserve(14);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (i > 0 && i % 4 == 0) {
                code.push_back('-');
            }
            code.push_back(alphabet[bytes[i] % 32]);
        }
        return SwString(code);
    }

    static bool containsCode_(const SwList<SwString>& codes, const SwString& code) {
        for (std::size_t i = 0; i < codes.size(); ++i) {
            if (codes[i] == code) {
                return true;
            }
        }
        return false;
    }

    static SwByteArray primaryKey_(const RecoveryCodeRecord_& record) {
        return SwByteArray((SwString("auth/recovery-code/") + record.accountId + "\x1f" + record.codeId).toUtf8());
    }

    static SwByteArray accountIndexKey_(const SwString& accountId, const SwString& codeId) {
        return SwByteArray((accountId + "\x1f" + codeId).toUtf8());
    }

    SwDbIterator scanAccountLocked_(const SwString& accountId) {
        const SwString start = accountId.trimmed() + "\x1f";
        return m_db.scanIndex("auth.recoveryCodesByAccount",
                              accountIndexKey_(accountId.trimmed(), SwString()),
                              SwByteArray((start + "\xff").toUtf8()));
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeys_(const RecoveryCodeRecord_& record) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> accountKeys;
        accountKeys.append(accountIndexKey_(record.accountId, record.codeId));
        secondary["auth.recoveryCodesByAccount"] = accountKeys;
        return secondary;
    }

    static SwJsonObject recordToJson_(const RecoveryCodeRecord_& record) {
        SwJsonObject object;
        object["accountId"] = record.accountId;
        object["codeId"] = record.codeId;
        object["codeHash"] = record.codeHash;
        object["usedAt"] = record.usedAt;
        object["createdAt"] = record.createdAt;
        object["updatedAt"] = record.updatedAt;
        return object;
    }

    static RecoveryCodeRecord_ recordFromJson_(const SwJsonObject& object) {
        RecoveryCodeRecord_ record;
        record.accountId = object.value("accountId").toString();
        record.codeId = object.value("codeId").toString();
        record.codeHash = object.value("codeHash").toString();
        record.usedAt = object.value("usedAt").toString();
        record.createdAt = object.value("createdAt").toString();
        record.updatedAt = object.value("updatedAt").toString();
        return record;
    }

    static bool parseRecord_(const SwByteArray& bytes, RecoveryCodeRecord_& outRecord) {
        SwJsonObject object;
        if (!swHttpAuthDetail::parseJsonObject(bytes, object)) {
            return false;
        }
        outRecord = recordFromJson_(object);
        return !outRecord.accountId.trimmed().isEmpty() && !outRecord.codeId.trimmed().isEmpty();
    }

    void appendAccountDeletesLocked_(const SwString& accountId, SwDbWriteBatch& batch) {
        if (accountId.trimmed().isEmpty()) {
            return;
        }
        for (SwDbIterator it = scanAccountLocked_(accountId.trimmed()); it.isValid(); it.next()) {
            RecoveryCodeRecord_ record;
            if (parseRecord_(it.current().value, record)) {
                batch.erase(primaryKey_(record));
            }
        }
    }

    SwHttpAuthRecoveryCodeStatus statusForAccountLocked_(const SwString& accountId) {
        SwHttpAuthRecoveryCodeStatus status;
        if (accountId.trimmed().isEmpty()) {
            return status;
        }
        for (SwDbIterator it = scanAccountLocked_(accountId.trimmed()); it.isValid(); it.next()) {
            RecoveryCodeRecord_ record;
            if (!parseRecord_(it.current().value, record)) {
                continue;
            }
            ++status.totalCodes;
            if (record.usedAt.trimmed().isEmpty()) {
                ++status.remainingCodes;
            }
            if (status.generatedAt.isEmpty() || record.createdAt < status.generatedAt) {
                status.generatedAt = record.createdAt;
            }
        }
        return status;
    }

    SwHttpAuthConfig m_config;
    SwEmbeddedDb m_db;
    mutable SwMutex m_mutex;
    bool m_opened = false;
};
