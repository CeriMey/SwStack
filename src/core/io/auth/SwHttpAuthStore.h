#pragma once

#include "SwDebug.h"
#include "SwEmbeddedDb.h"
#include "auth/SwHttpAuthTypes.h"

#define SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_() \
    if (!m_opened) { \
        return SwDbStatus(SwDbStatus::NotOpen, "Auth store not open"); \
    }

class SwHttpAuthStore {
public:
    SwHttpAuthStore() = default;

    void setConfig(const SwHttpAuthConfig& config) {
        SwMutexLocker locker(&m_mutex);
        m_config = config;
        m_config.routePrefix = swHttpAuthDetail::normalizeRoutePrefix(m_config.routePrefix);
        m_config.publicBaseUrl = swHttpAuthDetail::normalizeBaseUrl(m_config.publicBaseUrl);
        if (m_config.sessionCookieName.trimmed().isEmpty()) {
            m_config.sessionCookieName = "sw_auth";
        }
    }

    const SwHttpAuthConfig& config() const {
        return m_config;
    }

    SwDbStatus open() {
        SwMutexLocker locker(&m_mutex);
        if (m_opened) {
            return SwDbStatus::success();
        }
        SwEmbeddedDbOptions options = m_config.dbOptions;
        if (options.dbPath.trimmed().isEmpty()) {
            options.dbPath = m_config.storageDir.trimmed().isEmpty() ? SwString("auth/db")
                                                                    : (m_config.storageDir.trimmed() + "/db");
        }
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

    SwDbStatus createAccount(const SwString& email,
                             const SwString& password,
                             const SwString& subjectId,
                             SwHttpAuthAccount* createdOut = nullptr) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwString localPart;
        SwString domain;
        if (!swHttpAuthDetail::splitEmail(email, localPart, domain)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid email address");
        }
        if (password.size() < static_cast<std::size_t>(std::max(1, m_config.passwordMinLength))) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Password too short");
        }

        SwHttpAuthAccount existing;
        if (loadAccountByEmailLocked_(swHttpAuthDetail::normalizeEmail(email), existing).ok()) {
            return SwDbStatus(SwDbStatus::Busy, "Account already exists");
        }

        SwHttpAuthAccount account;
        account.accountId = swHttpAuthDetail::generateId("account");
        account.subjectId = subjectId.trimmed();
        account.email = swHttpAuthDetail::normalizeEmail(email);
        account.passwordHash = swHttpAuthDetail::makePasswordHash(password);
        if (account.passwordHash.isEmpty()) {
            return SwDbStatus(SwDbStatus::IoError, "Unable to hash password");
        }
        account.emailVerifiedAt.clear();
        account.passwordResetRequired = false;
        account.suspended = false;
        account.createdAt = swHttpAuthDetail::currentIsoTimestamp();
        account.updatedAt = account.createdAt;

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(accountPrimaryKey_(account.accountId)),
                  swHttpAuthDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));
        const SwDbStatus status = m_db.write(batch);
        if (status.ok() && createdOut) {
            *createdOut = account;
        }
        return status;
    }

    SwDbStatus createImportedAccount(const SwHttpAuthAccount& importedAccount,
                                     SwHttpAuthAccount* createdOut = nullptr) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwString localPart;
        SwString domain;
        const SwString normalizedEmail = swHttpAuthDetail::normalizeEmail(importedAccount.email);
        if (!swHttpAuthDetail::splitEmail(normalizedEmail, localPart, domain)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid email address");
        }
        if (importedAccount.passwordHash.trimmed().isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Imported password hash is required");
        }

        SwHttpAuthAccount existing;
        if (loadAccountByEmailLocked_(normalizedEmail, existing).ok()) {
            return SwDbStatus(SwDbStatus::Busy, "Account already exists");
        }

        SwHttpAuthAccount account = importedAccount;
        account.accountId = account.accountId.trimmed().isEmpty()
                                ? swHttpAuthDetail::generateId("account")
                                : importedAccount.accountId.trimmed();
        account.subjectId = importedAccount.subjectId.trimmed();
        account.email = normalizedEmail;
        account.passwordHash = importedAccount.passwordHash.trimmed();
        account.emailVerifiedAt = importedAccount.emailVerifiedAt.trimmed();
        account.passwordResetRequired = importedAccount.passwordResetRequired;
        account.createdAt = importedAccount.createdAt.trimmed().isEmpty()
                                ? swHttpAuthDetail::currentIsoTimestamp()
                                : importedAccount.createdAt.trimmed();
        account.updatedAt = importedAccount.updatedAt.trimmed().isEmpty()
                                ? account.createdAt
                                : importedAccount.updatedAt.trimmed();

        const SwDbStatus status = writeAccountLocked_(account);
        if (status.ok() && createdOut) {
            *createdOut = account;
        }
        return status;
    }

    SwDbStatus removeAccount(const SwString& accountId) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        SwDbWriteBatch batch;
        batch.erase(toPrimaryKey_(accountPrimaryKey_(accountId.trimmed())));
        return m_db.write(batch);
    }

    SwDbStatus getAccountById(const SwString& accountId, SwHttpAuthAccount* outAccount) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        if (!outAccount) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output account");
        }
        return loadAccountByIdLocked_(accountId.trimmed(), *outAccount);
    }

    SwDbStatus getAccountByEmail(const SwString& email, SwHttpAuthAccount* outAccount) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        if (!outAccount) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output account");
        }
        return loadAccountByEmailLocked_(swHttpAuthDetail::normalizeEmail(email), *outAccount);
    }

    SwDbStatus setAccountSubjectId(const SwString& accountId, const SwString& subjectId) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthAccount account;
        const SwDbStatus status = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!status.ok()) {
            return status;
        }
        account.subjectId = subjectId.trimmed();
        account.updatedAt = swHttpAuthDetail::currentIsoTimestamp();
        return writeAccountLocked_(account);
    }

    SwDbStatus setAccountPassword(const SwString& accountId, const SwString& password) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        if (password.size() < static_cast<std::size_t>(std::max(1, m_config.passwordMinLength))) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Password too short");
        }

        SwHttpAuthAccount account;
        const SwDbStatus status = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!status.ok()) {
            return status;
        }
        account.passwordHash = swHttpAuthDetail::makePasswordHash(password);
        if (account.passwordHash.isEmpty()) {
            return SwDbStatus(SwDbStatus::IoError, "Unable to hash password");
        }
        account.passwordResetRequired = false;
        account.updatedAt = swHttpAuthDetail::currentIsoTimestamp();
        return writeAccountLocked_(account);
    }

    SwDbStatus setAccountPasswordResetRequired(const SwString& accountId, bool passwordResetRequired) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthAccount account;
        const SwDbStatus status = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!status.ok()) {
            return status;
        }
        account.passwordResetRequired = passwordResetRequired;
        account.updatedAt = swHttpAuthDetail::currentIsoTimestamp();
        return writeAccountLocked_(account);
    }

    SwDbStatus setAccountEmailVerifiedAt(const SwString& accountId, const SwString& verifiedAt) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthAccount account;
        const SwDbStatus status = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!status.ok()) {
            return status;
        }
        account.emailVerifiedAt = verifiedAt.trimmed();
        account.updatedAt = swHttpAuthDetail::currentIsoTimestamp();
        return writeAccountLocked_(account);
    }

    SwDbStatus setAccountSuspended(const SwString& accountId, bool suspended) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthAccount account;
        const SwDbStatus status = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!status.ok()) {
            return status;
        }
        account.suspended = suspended;
        account.updatedAt = swHttpAuthDetail::currentIsoTimestamp();
        return writeAccountLocked_(account);
    }

    SwList<SwHttpAuthAccount> listAccounts() {
        SwMutexLocker locker(&m_mutex);
        SwList<SwHttpAuthAccount> accounts;
        if (!m_opened) {
            return accounts;
        }
        for (SwDbIterator it = m_db.scanPrimary(toPrimaryKey_("auth/account/"), toPrimaryKey_("auth/account/\xff"));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            accounts.append(accountFromJson_(object));
        }
        return accounts;
    }

    bool verifyPassword(const SwHttpAuthAccount& account, const SwString& password) const {
        return swHttpAuthDetail::verifyPasswordHash(account.passwordHash, password);
    }

    SwDbStatus createSession(const SwString& accountId,
                             const SwString& userAgent,
                             bool viaTls,
                             unsigned long long ttlMs,
                             SwString* outRawToken,
                             SwHttpAuthSession* outSession) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthAccount account;
        const SwDbStatus accountStatus = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!accountStatus.ok()) {
            return accountStatus;
        }

        const SwString rawToken = swHttpAuthDetail::randomHexToken(32);
        if (rawToken.isEmpty()) {
            return SwDbStatus(SwDbStatus::IoError, "Unable to generate session token");
        }

        SwHttpAuthSession session;
        session.sessionId = swHttpAuthDetail::generateId("session");
        session.accountId = account.accountId;
        session.tokenHash = swHttpAuthDetail::hashSha256(rawToken);
        session.expiresAtMs = swHttpAuthDetail::currentEpochMs() + static_cast<long long>(ttlMs);
        session.userAgent = userAgent.left(512);
        session.viaTls = viaTls;
        session.createdAt = swHttpAuthDetail::currentIsoTimestamp();
        session.updatedAt = session.createdAt;

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(sessionPrimaryKey_(session.tokenHash)),
                  swHttpAuthDetail::jsonToBytes(sessionToJson_(session)),
                  secondaryKeysForSession_(session));
        const SwDbStatus status = m_db.write(batch);
        if (!status.ok()) {
            return status;
        }
        if (outRawToken) {
            *outRawToken = rawToken;
        }
        if (outSession) {
            *outSession = session;
        }
        return SwDbStatus::success();
    }

    SwDbStatus getSessionByToken(const SwString& rawToken, SwHttpAuthSession* outSession) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        if (!outSession) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output session");
        }
        return loadSessionByTokenHashLocked_(swHttpAuthDetail::hashSha256(rawToken.trimmed()), *outSession);
    }

    SwList<SwHttpAuthSession> listSessionsByAccount(const SwString& accountId) {
        SwMutexLocker locker(&m_mutex);
        SwList<SwHttpAuthSession> sessions;
        if (!m_opened) {
            return sessions;
        }
        const SwString start = swHttpAuthDetail::sessionsByAccountSecondaryKey(accountId.trimmed(), SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.sessionsByAccount", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            sessions.append(sessionFromJson_(object));
        }
        return sessions;
    }

    SwDbStatus removeSessionByToken(const SwString& rawToken) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        SwDbWriteBatch batch;
        batch.erase(toPrimaryKey_(sessionPrimaryKey_(swHttpAuthDetail::hashSha256(rawToken.trimmed()))));
        if (batch.isEmpty()) {
            return SwDbStatus::success();
        }
        return m_db.write(batch);
    }

    SwDbStatus removeSessionsForAccount(const SwString& accountId, const SwString& keepSessionId = SwString()) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        const SwList<SwHttpAuthSession> sessions = listSessionsByAccountUnlocked_(accountId.trimmed());
        SwDbWriteBatch batch;
        for (std::size_t i = 0; i < sessions.size(); ++i) {
            if (!keepSessionId.isEmpty() && sessions[i].sessionId == keepSessionId) {
                continue;
            }
            batch.erase(toPrimaryKey_(sessionPrimaryKey_(sessions[i].tokenHash)));
        }
        return m_db.write(batch);
    }

    SwDbStatus removeChallengesForAccount(const SwString& accountId) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        const SwString normalizedAccountId = accountId.trimmed();
        if (normalizedAccountId.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Account id is required");
        }

        SwDbWriteBatch batch;
        const SwString start = normalizedAccountId + "\x1f";
        const SwString end = normalizedAccountId + "\x1f\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.challengesByAccountPurpose",
                                              toSecondaryKey_(start),
                                              toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            const SwHttpAuthChallenge challenge = challengeFromJson_(object);
            batch.erase(toPrimaryKey_(challengePrimaryKey_(challenge.challengeId)));
        }
        return m_db.write(batch);
    }

    SwDbStatus createChallenge(const SwString& purpose,
                               const SwString& accountId,
                               unsigned long long ttlMs,
                               SwString* outRawToken,
                               SwHttpAuthChallenge* outChallenge) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthAccount account;
        const SwDbStatus accountStatus = loadAccountByIdLocked_(accountId.trimmed(), account);
        if (!accountStatus.ok()) {
            return accountStatus;
        }

        const SwString normalizedPurpose = purpose.trimmed().toLower();
        if (normalizedPurpose.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Challenge purpose is required");
        }

        const SwList<SwHttpAuthChallenge> existing =
            listChallengesByAccountPurposeUnlocked_(account.accountId, normalizedPurpose);
        SwDbWriteBatch batch;
        for (std::size_t i = 0; i < existing.size(); ++i) {
            batch.erase(toPrimaryKey_(challengePrimaryKey_(existing[i].challengeId)));
        }

        const SwString rawToken = swHttpAuthDetail::randomHexToken(32);
        const SwString code = swHttpAuthDetail::generateChallengeCode(8);
        if (rawToken.isEmpty() || code.isEmpty()) {
            return SwDbStatus(SwDbStatus::IoError, "Unable to generate challenge");
        }

        SwHttpAuthChallenge challenge;
        challenge.challengeId = swHttpAuthDetail::generateId("challenge");
        challenge.purpose = normalizedPurpose;
        challenge.accountId = account.accountId;
        challenge.code = code;
        challenge.tokenHash = swHttpAuthDetail::hashSha256(rawToken);
        challenge.expiresAtMs = swHttpAuthDetail::currentEpochMs() + static_cast<long long>(ttlMs);
        challenge.consumedAt.clear();
        challenge.createdAt = swHttpAuthDetail::currentIsoTimestamp();
        challenge.updatedAt = challenge.createdAt;

        batch.put(toPrimaryKey_(challengePrimaryKey_(challenge.challengeId)),
                  swHttpAuthDetail::jsonToBytes(challengeToJson_(challenge)),
                  secondaryKeysForChallenge_(challenge));
        const SwDbStatus status = m_db.write(batch);
        if (!status.ok()) {
            return status;
        }
        if (outRawToken) {
            *outRawToken = rawToken;
        }
        if (outChallenge) {
            *outChallenge = challenge;
        }
        return SwDbStatus::success();
    }

    SwDbStatus getChallengeByToken(const SwString& rawToken, SwHttpAuthChallenge* outChallenge) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        if (!outChallenge) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output challenge");
        }
        const SwString tokenHash = swHttpAuthDetail::hashSha256(rawToken.trimmed());
        const SwString start = swHttpAuthDetail::challengeByTokenSecondaryKey(tokenHash);
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.challengesByToken", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            *outChallenge = challengeFromJson_(object);
            return SwDbStatus::success();
        }
        return SwDbStatus(SwDbStatus::NotFound, "Challenge not found");
    }

    SwDbStatus getChallengeByCode(const SwString& purpose,
                                  const SwString& code,
                                  SwHttpAuthChallenge* outChallenge) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        if (!outChallenge) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output challenge");
        }
        const SwString start =
            swHttpAuthDetail::challengeByCodeSecondaryKey(purpose.trimmed().toLower(), code.trimmed().toUpper(), SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.challengesByCode", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            *outChallenge = challengeFromJson_(object);
            return SwDbStatus::success();
        }
        return SwDbStatus(SwDbStatus::NotFound, "Challenge not found");
    }

    SwList<SwHttpAuthChallenge> listChallenges() {
        SwMutexLocker locker(&m_mutex);
        SwList<SwHttpAuthChallenge> challenges;
        if (!m_opened) {
            return challenges;
        }
        for (SwDbIterator it = m_db.scanPrimary(toPrimaryKey_("auth/challenge/"), toPrimaryKey_("auth/challenge/\xff"));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            challenges.append(challengeFromJson_(object));
        }
        return challenges;
    }

    SwDbStatus consumeChallenge(const SwString& challengeId) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();

        SwHttpAuthChallenge challenge;
        const SwDbStatus status = loadChallengeByIdLocked_(challengeId.trimmed(), challenge);
        if (!status.ok()) {
            return status;
        }
        challenge.consumedAt = swHttpAuthDetail::currentIsoTimestamp();
        challenge.updatedAt = challenge.consumedAt;
        return writeChallengeLocked_(challenge);
    }

    SwDbStatus removeChallenge(const SwString& challengeId) {
        SwMutexLocker locker(&m_mutex);
        SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_();
        SwDbWriteBatch batch;
        batch.erase(toPrimaryKey_(challengePrimaryKey_(challengeId.trimmed())));
        return m_db.write(batch);
    }

private:
    SwEmbeddedDb m_db;
    SwHttpAuthConfig m_config;
    mutable SwMutex m_mutex;
    bool m_opened = false;

    static SwByteArray toPrimaryKey_(const SwString& key) {
        return SwByteArray(key.toUtf8());
    }

    static SwByteArray toSecondaryKey_(const SwString& key) {
        return SwByteArray(key.toUtf8());
    }

    static SwString accountPrimaryKey_(const SwString& accountId) {
        return "auth/account/" + accountId;
    }

    static SwString sessionPrimaryKey_(const SwString& tokenHash) {
        return "auth/session/" + tokenHash;
    }

    static SwString challengePrimaryKey_(const SwString& challengeId) {
        return "auth/challenge/" + challengeId;
    }

    SwDbStatus writeAccountLocked_(const SwHttpAuthAccount& account) {
        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(accountPrimaryKey_(account.accountId)),
                  swHttpAuthDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));
        return m_db.write(batch);
    }

    SwDbStatus writeChallengeLocked_(const SwHttpAuthChallenge& challenge) {
        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(challengePrimaryKey_(challenge.challengeId)),
                  swHttpAuthDetail::jsonToBytes(challengeToJson_(challenge)),
                  secondaryKeysForChallenge_(challenge));
        return m_db.write(batch);
    }

    SwDbStatus loadAccountByIdLocked_(const SwString& accountId, SwHttpAuthAccount& outAccount) {
        SwByteArray value;
        const SwDbStatus status = m_db.get(toPrimaryKey_(accountPrimaryKey_(accountId)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swHttpAuthDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid account record");
        }
        outAccount = accountFromJson_(object);
        return SwDbStatus::success();
    }

    SwDbStatus loadAccountByEmailLocked_(const SwString& normalizedEmail, SwHttpAuthAccount& outAccount) {
        const SwString start = swHttpAuthDetail::accountsByEmailSecondaryKey(normalizedEmail);
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.accountsByEmail", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            outAccount = accountFromJson_(object);
            return SwDbStatus::success();
        }
        return SwDbStatus(SwDbStatus::NotFound, "Account not found");
    }

    SwDbStatus loadSessionByTokenHashLocked_(const SwString& tokenHash, SwHttpAuthSession& outSession) {
        SwByteArray value;
        const SwDbStatus status = m_db.get(toPrimaryKey_(sessionPrimaryKey_(tokenHash)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swHttpAuthDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid session record");
        }
        outSession = sessionFromJson_(object);
        return SwDbStatus::success();
    }

    SwDbStatus loadChallengeByIdLocked_(const SwString& challengeId, SwHttpAuthChallenge& outChallenge) {
        SwByteArray value;
        const SwDbStatus status = m_db.get(toPrimaryKey_(challengePrimaryKey_(challengeId)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swHttpAuthDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid challenge record");
        }
        outChallenge = challengeFromJson_(object);
        return SwDbStatus::success();
    }

    SwList<SwHttpAuthSession> listSessionsByAccountUnlocked_(const SwString& accountId) {
        SwList<SwHttpAuthSession> sessions;
        const SwString start = swHttpAuthDetail::sessionsByAccountSecondaryKey(accountId, SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.sessionsByAccount", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            sessions.append(sessionFromJson_(object));
        }
        return sessions;
    }

    SwList<SwHttpAuthChallenge> listChallengesByAccountPurposeUnlocked_(const SwString& accountId,
                                                                         const SwString& purpose) {
        SwList<SwHttpAuthChallenge> challenges;
        const SwString start =
            swHttpAuthDetail::challengeByAccountPurposeSecondaryKey(accountId, purpose, SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("auth.challengesByAccountPurpose",
                                              toSecondaryKey_(start),
                                              toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swHttpAuthDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            challenges.append(challengeFromJson_(object));
        }
        return challenges;
    }

    static SwJsonObject accountToJson_(const SwHttpAuthAccount& account) {
        SwJsonObject object;
        object["accountId"] = account.accountId.toStdString();
        object["subjectId"] = account.subjectId.toStdString();
        object["email"] = account.email.toStdString();
        object["passwordHash"] = account.passwordHash.toStdString();
        object["emailVerifiedAt"] = account.emailVerifiedAt.toStdString();
        object["passwordResetRequired"] = account.passwordResetRequired;
        object["suspended"] = account.suspended;
        object["createdAt"] = account.createdAt.toStdString();
        object["updatedAt"] = account.updatedAt.toStdString();
        return object;
    }

    static SwHttpAuthAccount accountFromJson_(const SwJsonObject& object) {
        SwHttpAuthAccount account;
        account.accountId = object.value("accountId").toString().c_str();
        account.subjectId = object.value("subjectId").toString().c_str();
        account.email = object.value("email").toString().c_str();
        account.passwordHash = object.value("passwordHash").toString().c_str();
        account.emailVerifiedAt = object.value("emailVerifiedAt").toString().c_str();
        account.passwordResetRequired = object.value("passwordResetRequired").toBool(false);
        account.suspended = object.value("suspended").toBool(false);
        account.createdAt = object.value("createdAt").toString().c_str();
        account.updatedAt = object.value("updatedAt").toString().c_str();
        return account;
    }

    static SwJsonObject sessionToJson_(const SwHttpAuthSession& session) {
        SwJsonObject object;
        object["sessionId"] = session.sessionId.toStdString();
        object["accountId"] = session.accountId.toStdString();
        object["tokenHash"] = session.tokenHash.toStdString();
        object["expiresAtMs"] = session.expiresAtMs;
        object["userAgent"] = session.userAgent.toStdString();
        object["viaTls"] = session.viaTls;
        object["createdAt"] = session.createdAt.toStdString();
        object["updatedAt"] = session.updatedAt.toStdString();
        return object;
    }

    static SwHttpAuthSession sessionFromJson_(const SwJsonObject& object) {
        SwHttpAuthSession session;
        session.sessionId = object.value("sessionId").toString().c_str();
        session.accountId = object.value("accountId").toString().c_str();
        session.tokenHash = object.value("tokenHash").toString().c_str();
        session.expiresAtMs = static_cast<long long>(object.value("expiresAtMs").toInteger(0));
        session.userAgent = object.value("userAgent").toString().c_str();
        session.viaTls = object.value("viaTls").toBool(false);
        session.createdAt = object.value("createdAt").toString().c_str();
        session.updatedAt = object.value("updatedAt").toString().c_str();
        return session;
    }

    static SwJsonObject challengeToJson_(const SwHttpAuthChallenge& challenge) {
        SwJsonObject object;
        object["challengeId"] = challenge.challengeId.toStdString();
        object["purpose"] = challenge.purpose.toStdString();
        object["accountId"] = challenge.accountId.toStdString();
        object["code"] = challenge.code.toStdString();
        object["tokenHash"] = challenge.tokenHash.toStdString();
        object["expiresAtMs"] = challenge.expiresAtMs;
        object["consumedAt"] = challenge.consumedAt.toStdString();
        object["createdAt"] = challenge.createdAt.toStdString();
        object["updatedAt"] = challenge.updatedAt.toStdString();
        return object;
    }

    static SwHttpAuthChallenge challengeFromJson_(const SwJsonObject& object) {
        SwHttpAuthChallenge challenge;
        challenge.challengeId = object.value("challengeId").toString().c_str();
        challenge.purpose = object.value("purpose").toString().c_str();
        challenge.accountId = object.value("accountId").toString().c_str();
        challenge.code = object.value("code").toString().c_str();
        challenge.tokenHash = object.value("tokenHash").toString().c_str();
        challenge.expiresAtMs = static_cast<long long>(object.value("expiresAtMs").toInteger(0));
        challenge.consumedAt = object.value("consumedAt").toString().c_str();
        challenge.createdAt = object.value("createdAt").toString().c_str();
        challenge.updatedAt = object.value("updatedAt").toString().c_str();
        return challenge;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForAccount_(const SwHttpAuthAccount& account) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> emailKeys;
        emailKeys.append(SwByteArray(swHttpAuthDetail::accountsByEmailSecondaryKey(account.email).toUtf8()));
        secondary["auth.accountsByEmail"] = emailKeys;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForSession_(const SwHttpAuthSession& session) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> accountKeys;
        accountKeys.append(
            SwByteArray(swHttpAuthDetail::sessionsByAccountSecondaryKey(session.accountId, session.sessionId).toUtf8()));
        secondary["auth.sessionsByAccount"] = accountKeys;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForChallenge_(const SwHttpAuthChallenge& challenge) {
        SwMap<SwString, SwList<SwByteArray>> secondary;

        SwList<SwByteArray> accountPurposeKeys;
        accountPurposeKeys.append(
            SwByteArray(swHttpAuthDetail::challengeByAccountPurposeSecondaryKey(challenge.accountId,
                                                                                challenge.purpose,
                                                                                challenge.challengeId)
                            .toUtf8()));
        secondary["auth.challengesByAccountPurpose"] = accountPurposeKeys;

        SwList<SwByteArray> tokenKeys;
        tokenKeys.append(SwByteArray(swHttpAuthDetail::challengeByTokenSecondaryKey(challenge.tokenHash).toUtf8()));
        secondary["auth.challengesByToken"] = tokenKeys;

        SwList<SwByteArray> codeKeys;
        codeKeys.append(
            SwByteArray(swHttpAuthDetail::challengeByCodeSecondaryKey(challenge.purpose,
                                                                      challenge.code.toUpper(),
                                                                      challenge.challengeId)
                            .toUtf8()));
        secondary["auth.challengesByCode"] = codeKeys;

        return secondary;
    }
};

#undef SW_HTTP_AUTH_RETURN_IF_NOT_OPEN_
