#pragma once

#include "SwDebug.h"
#include "SwEmbeddedDb.h"
#include "SwMailCommon.h"
#include "SwMailSrs.h"

#define SW_MAIL_RETURN_IF_NOT_OPEN_() \
    if (!m_opened) { \
        return SwDbStatus(SwDbStatus::NotOpen, "Mail store not open"); \
    }

class SwMailStore {
public:
    SwMailStore() = default;

    void setConfig(const SwMailConfig& config) {
        SwMutexLocker locker(&m_mutex);
        m_config = config;
        m_config.domain = swMailDetail::normalizeDomain(m_config.domain);
        m_config.mailHost = swMailDetail::normalizeMailHost(m_config.mailHost, m_config.domain);
    }

    const SwMailConfig& config() const {
        return m_config;
    }

    SwDbStatus open() {
        SwMutexLocker locker(&m_mutex);
        if (m_opened) {
            return SwDbStatus::success();
        }
        SwEmbeddedDbOptions options = m_config.dbOptions;
        if (options.dbPath.trimmed().isEmpty()) {
            options.dbPath = m_config.storageDir.trimmed().isEmpty() ? SwString("mail/db")
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

    SwDbStatus createAccount(const SwString& address,
                             const SwString& password,
                             SwMailAccount* createdOut = nullptr) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(address, localPart, domain)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid account address");
        }
        SwMailAccount existing;
        const SwDbStatus existingStatus = loadAccountLocked_(swMailDetail::canonicalAddress(address), existing);
        if (existingStatus.ok()) {
            return SwDbStatus(SwDbStatus::Busy, "Account already exists");
        }
        if (password.size() < 4) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Password too short");
        }

        SwMailAccount account;
        account.address = swMailDetail::canonicalAddress(address);
        account.domain = domain;
        account.localPart = localPart;
        account.passwordSalt = swMailDetail::makePasswordSalt();
        account.passwordHash = swMailDetail::hashPassword(account.passwordSalt, password);
        account.active = true;
        account.canReceive = true;
        account.canSend = true;
        account.suspended = false;
        account.quotaBytes = m_config.accountDefaultQuotaBytes;
        account.usedBytes = 0;
        account.createdAt = swMailDetail::currentIsoTimestamp();
        account.updatedAt = account.createdAt;

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::accountKey(account.address)),
                  swMailDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));

        const SwDbStatus writeStatus = m_db.write(batch);
        if (!writeStatus.ok()) {
            return writeStatus;
        }

        const SwDbStatus mailboxStatus = ensureDefaultMailboxesLocked_(account.address);
        if (!mailboxStatus.ok()) {
            return mailboxStatus;
        }

        if (createdOut) {
            *createdOut = account;
        }
        return SwDbStatus::success();
    }

    SwDbStatus updateAccount(const SwMailAccount& account) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwMailAccount normalized = account;
        normalized.address = swMailDetail::canonicalAddress(account.address);
        normalized.domain = swMailDetail::normalizeDomain(account.domain);
        normalized.localPart = account.localPart.trimmed().toLower();
        normalized.updatedAt = swMailDetail::currentIsoTimestamp();

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::accountKey(normalized.address)),
                  swMailDetail::jsonToBytes(accountToJson_(normalized)),
                  secondaryKeysForAccount_(normalized));
        return m_db.write(batch);
    }

    SwDbStatus setAccountPassword(const SwString& address, const SwString& password) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwMailAccount account;
        const SwDbStatus status = loadAccountLocked_(swMailDetail::canonicalAddress(address), account);
        if (!status.ok()) {
            return status;
        }
        account.passwordSalt = swMailDetail::makePasswordSalt();
        account.passwordHash = swMailDetail::hashPassword(account.passwordSalt, password);
        account.updatedAt = swMailDetail::currentIsoTimestamp();

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::accountKey(account.address)),
                  swMailDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));
        return m_db.write(batch);
    }

    SwDbStatus setAccountSuspended(const SwString& address, bool suspended) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwMailAccount account;
        const SwDbStatus status = loadAccountLocked_(swMailDetail::canonicalAddress(address), account);
        if (!status.ok()) {
            return status;
        }
        account.suspended = suspended;
        account.active = !suspended;
        account.updatedAt = swMailDetail::currentIsoTimestamp();

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::accountKey(account.address)),
                  swMailDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));
        return m_db.write(batch);
    }

    SwDbStatus getAccount(const SwString& address, SwMailAccount* outAccount) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        if (!outAccount) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output account");
        }
        return loadAccountLocked_(swMailDetail::canonicalAddress(address), *outAccount);
    }

    bool authenticate(const SwString& address,
                      const SwString& password,
                      SwMailAccount* outAccount = nullptr,
                      SwString* outError = nullptr) {
        SwMutexLocker locker(&m_mutex);
        if (outError) {
            outError->clear();
        }
        if (!m_opened) {
            if (outError) {
                *outError = "Store not open";
            }
            return false;
        }

        SwMailAccount account;
        const SwDbStatus status = loadAccountLocked_(swMailDetail::canonicalAddress(address), account);
        if (!status.ok()) {
            if (outError) {
                *outError = status.message();
            }
            return false;
        }
        if (account.suspended || !account.active) {
            if (outError) {
                *outError = "Account suspended";
            }
            return false;
        }
        const SwString expected = swMailDetail::hashPassword(account.passwordSalt, password);
        if (expected != account.passwordHash) {
            if (outError) {
                *outError = "Invalid credentials";
            }
            return false;
        }
        if (outAccount) {
            *outAccount = account;
        }
        return true;
    }

    SwList<SwMailAccount> listAccounts() {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailAccount> accounts;
        if (!m_opened) {
            return accounts;
        }
        const SwString start = swMailDetail::accountsSecondaryKey(m_config.domain, SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("mail.accounts", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            accounts.append(accountFromJson_(object));
        }
        return accounts;
    }

    SwDbStatus upsertAlias(const SwMailAlias& alias) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwMailAlias normalized = alias;
        normalized.domain = swMailDetail::normalizeDomain(alias.domain);
        normalized.localPart = alias.localPart.trimmed().toLower();
        normalized.address = normalized.localPart + "@" + normalized.domain;
        normalized.targets = swMailDetail::normalizeRecipients(alias.targets);
        if (normalized.localPart.isEmpty() || normalized.domain.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid alias address");
        }
        if (normalized.createdAt.isEmpty()) {
            normalized.createdAt = swMailDetail::currentIsoTimestamp();
        }
        normalized.updatedAt = swMailDetail::currentIsoTimestamp();

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::aliasKey(normalized.domain, normalized.localPart)),
                  swMailDetail::jsonToBytes(aliasToJson_(normalized)),
                  secondaryKeysForAlias_(normalized));
        return m_db.write(batch);
    }

    SwList<SwMailAlias> listAliases() {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailAlias> aliases;
        if (!m_opened) {
            return aliases;
        }
        // Scan primaire : les alias peuvent appartenir au domaine géré comme aux
        // domaines custom hébergés, l'index secondaire est scopé par domaine.
        for (SwDbIterator it = m_db.scanPrimary(SwByteArray("alias/"), SwByteArray("alias/\xff"));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            aliases.append(aliasFromJson_(object));
        }
        return aliases;
    }

    SwDbStatus getAlias(const SwString& address, SwMailAlias* outAlias) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        if (!outAlias) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output alias");
        }
        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(address, localPart, domain)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid alias address");
        }
        SwByteArray value;
        const SwDbStatus status =
            m_db.get(toPrimaryKey_(swMailDetail::aliasKey(domain, localPart)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swMailDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Corrupt alias record");
        }
        *outAlias = aliasFromJson_(object);
        return SwDbStatus::success();
    }

    SwDbStatus deleteAlias(const SwString& address) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(address, localPart, domain)) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid alias address");
        }
        SwByteArray value;
        const SwDbStatus status =
            m_db.get(toPrimaryKey_(swMailDetail::aliasKey(domain, localPart)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwDbWriteBatch batch;
        batch.erase(toPrimaryKey_(swMailDetail::aliasKey(domain, localPart)));
        return m_db.write(batch);
    }

    SwDbStatus ensureSrsSecret(SwString* outSecret) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        if (!outSecret) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output secret");
        }
        outSecret->clear();
        SwByteArray value;
        const SwDbStatus status = m_db.get(toPrimaryKey_(SwString("srs/secret")), &value, nullptr);
        if (status.ok()) {
            SwJsonObject object;
            if (swMailDetail::parseJsonObject(value, object)) {
                *outSecret = object.value("secret").toString();
                if (!outSecret->isEmpty()) {
                    return SwDbStatus::success();
                }
            }
        } else if (status.code() != SwDbStatus::NotFound) {
            return status;
        }

        const SwString secret = SwMailSrs::generateSecret();
        if (secret.isEmpty()) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Unable to generate SRS secret");
        }
        SwJsonObject object;
        object["secret"] = secret;
        object["createdAt"] = swMailDetail::currentIsoTimestamp();
        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(SwString("srs/secret")), swMailDetail::jsonToBytes(object));
        const SwDbStatus writeStatus = m_db.write(batch);
        if (!writeStatus.ok()) {
            return writeStatus;
        }
        *outSecret = secret;
        return SwDbStatus::success();
    }

    SwDbStatus ensureDefaultMailboxes(const SwString& accountAddress) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        return ensureDefaultMailboxesLocked_(swMailDetail::canonicalAddress(accountAddress));
    }

    SwList<SwMailMailbox> listMailboxes(const SwString& accountAddress) {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailMailbox> mailboxes;
        if (!m_opened) {
            return mailboxes;
        }
        const SwString normalizedAddress = swMailDetail::canonicalAddress(accountAddress);
        const SwString start = swMailDetail::mailboxesSecondaryKey(normalizedAddress, SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("mail.mailboxes", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            mailboxes.append(mailboxFromJson_(object));
        }
        return mailboxes;
    }

    SwDbStatus appendMessage(const SwString& accountAddress,
                             const SwString& mailboxName,
                             const SwByteArray& rawMessage,
                             const SwList<SwString>& flags,
                             const SwString& internalDate = SwString(),
                             SwMailMessageEntry* createdOut = nullptr) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        if (rawMessage.size() == 0) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Message body is empty");
        }
        if (static_cast<unsigned long long>(rawMessage.size()) > m_config.maxMessageBytes) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Message exceeds maximum size");
        }

        const SwString normalizedAddress = swMailDetail::canonicalAddress(accountAddress);
        SwMailAccount account;
        SwDbStatus accountStatus = loadAccountLocked_(normalizedAddress, account);
        if (!accountStatus.ok()) {
            return accountStatus;
        }
        if (account.suspended || !account.canReceive) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Account cannot receive mail");
        }

        SwMailMailbox mailbox;
        const SwDbStatus mailboxStatus = ensureMailboxLocked_(normalizedAddress,
                                                              swMailDetail::normalizeMailboxName(mailboxName),
                                                              &mailbox);
        if (!mailboxStatus.ok()) {
            return mailboxStatus;
        }

        SwMailMessageEntry entry;
        entry.accountAddress = normalizedAddress;
        entry.mailboxName = mailbox.name;
        entry.uid = mailbox.uidNext;
        entry.flags = flags;
        entry.internalDate = internalDate.isEmpty() ? swMailDetail::currentIsoTimestamp() : internalDate;
        entry.rawMessage = rawMessage;
        entry.sizeBytes = static_cast<unsigned long long>(rawMessage.size());
        const SwMap<SwString, SwString> headers = swMailDetail::parseHeaders(rawMessage);
        entry.subject = headers.value("subject");
        entry.from = swMailDetail::canonicalAddress(headers.value("from"));
        entry.to = swMailDetail::normalizeRecipients(swMailDetail::parseAddressListHeader(headers.value("to")));
        entry.cc = swMailDetail::normalizeRecipients(swMailDetail::parseAddressListHeader(headers.value("cc")));
        entry.bcc = swMailDetail::normalizeRecipients(swMailDetail::parseAddressListHeader(headers.value("bcc")));
        entry.messageId = headers.value("message-id");
        if (entry.messageId.isEmpty()) {
            entry.messageId = swMailDetail::generateMessageId(m_config);
        }
        entry.authResults = headers.value("authentication-results");  // Lot 2 : re-dérivé de l'en-tête

        mailbox.uidNext += 1;
        mailbox.totalCount += 1;
        if (!hasSeenFlag_(entry.flags)) {
            mailbox.unseenCount += 1;
        }
        mailbox.updatedAt = swMailDetail::currentIsoTimestamp();

        account.usedBytes += entry.sizeBytes;
        account.updatedAt = mailbox.updatedAt;

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::mailboxKey(normalizedAddress, mailbox.name)),
                  swMailDetail::jsonToBytes(mailboxToJson_(mailbox)),
                  secondaryKeysForMailbox_(mailbox));
        batch.put(toPrimaryKey_(swMailDetail::messageKey(normalizedAddress, mailbox.name, entry.uid)),
                  swMailDetail::jsonToBytes(messageToJson_(entry)),
                  secondaryKeysForMessage_(entry));
        batch.put(toPrimaryKey_(swMailDetail::accountKey(account.address)),
                  swMailDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));

        const SwDbStatus writeStatus = m_db.write(batch);
        if (!writeStatus.ok()) {
            return writeStatus;
        }
        if (createdOut) {
            *createdOut = entry;
        }
        return SwDbStatus::success();
    }

    SwList<SwMailMessageEntry> listMessages(const SwString& accountAddress,
                                            const SwString& mailboxName) {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailMessageEntry> messages;
        if (!m_opened) {
            return messages;
        }
        const SwString normalizedAddress = swMailDetail::canonicalAddress(accountAddress);
        const SwString normalizedMailbox = swMailDetail::normalizeMailboxName(mailboxName);
        const SwString start = swMailDetail::messagesSecondaryKey(normalizedAddress, normalizedMailbox, 0);
        const SwString end = normalizedAddress + "\x1f" + normalizedMailbox + "\x1f" + "\xff";
        for (SwDbIterator it = m_db.scanIndex("mail.messages", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            messages.append(messageFromJson_(object));
        }
        return messages;
    }

    SwDbStatus getMessage(const SwString& accountAddress,
                          const SwString& mailboxName,
                          unsigned long long uid,
                          SwMailMessageEntry* outMessage) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        if (!outMessage) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output message");
        }
        SwByteArray value;
        const SwDbStatus status =
            m_db.get(toPrimaryKey_(swMailDetail::messageKey(accountAddress, mailboxName, uid)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swMailDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid stored message");
        }
        *outMessage = messageFromJson_(object);
        return SwDbStatus::success();
    }

    SwDbStatus setMessageFlags(const SwString& accountAddress,
                               const SwString& mailboxName,
                               unsigned long long uid,
                               const SwList<SwString>& flags) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        const SwString normalizedAddress = swMailDetail::canonicalAddress(accountAddress);
        const SwString normalizedMailbox = swMailDetail::normalizeMailboxName(mailboxName);
        SwMailMessageEntry entry;
        SwDbStatus status = getMessageUnlocked_(normalizedAddress, normalizedMailbox, uid, entry);
        if (!status.ok()) {
            return status;
        }

        SwMailMailbox mailbox;
        status = ensureMailboxLocked_(normalizedAddress, normalizedMailbox, &mailbox);
        if (!status.ok()) {
            return status;
        }

        const bool previouslySeen = hasSeenFlag_(entry.flags);
        entry.flags = flags;
        const bool nowSeen = hasSeenFlag_(entry.flags);
        if (previouslySeen != nowSeen) {
            if (nowSeen) {
                mailbox.unseenCount = mailbox.unseenCount > 0 ? mailbox.unseenCount - 1 : 0;
            } else {
                mailbox.unseenCount += 1;
            }
        }
        mailbox.updatedAt = swMailDetail::currentIsoTimestamp();

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::mailboxKey(normalizedAddress, normalizedMailbox)),
                  swMailDetail::jsonToBytes(mailboxToJson_(mailbox)),
                  secondaryKeysForMailbox_(mailbox));
        batch.put(toPrimaryKey_(swMailDetail::messageKey(normalizedAddress, normalizedMailbox, uid)),
                  swMailDetail::jsonToBytes(messageToJson_(entry)),
                  secondaryKeysForMessage_(entry));
        return m_db.write(batch);
    }

    SwDbStatus copyMessage(const SwString& accountAddress,
                           const SwString& sourceMailbox,
                           unsigned long long uid,
                           const SwString& targetMailbox) {
        SwMailMessageEntry entry;
        {
            SwMutexLocker locker(&m_mutex);
            SW_MAIL_RETURN_IF_NOT_OPEN_();
            const SwDbStatus status =
                getMessageUnlocked_(swMailDetail::canonicalAddress(accountAddress),
                                    swMailDetail::normalizeMailboxName(sourceMailbox),
                                    uid,
                                    entry);
            if (!status.ok()) {
                return status;
            }
        }
        return appendMessage(accountAddress, targetMailbox, entry.rawMessage, entry.flags, entry.internalDate, nullptr);
    }

    SwDbStatus expungeMailbox(const SwString& accountAddress,
                              const SwString& mailboxName,
                              unsigned long long* removedCountOut = nullptr) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        const SwString normalizedAddress = swMailDetail::canonicalAddress(accountAddress);
        const SwString normalizedMailbox = swMailDetail::normalizeMailboxName(mailboxName);
        SwMailAccount account;
        SwDbStatus status = loadAccountLocked_(normalizedAddress, account);
        if (!status.ok()) {
            return status;
        }
        SwMailMailbox mailbox;
        status = ensureMailboxLocked_(normalizedAddress, normalizedMailbox, &mailbox);
        if (!status.ok()) {
            return status;
        }

        const SwList<SwMailMessageEntry> messages = listMessagesUnlocked_(normalizedAddress, normalizedMailbox);
        SwDbWriteBatch batch;
        unsigned long long removedCount = 0;
        unsigned long long removedBytes = 0;
        unsigned long long removedUnseen = 0;
        for (std::size_t i = 0; i < messages.size(); ++i) {
            const SwMailMessageEntry& entry = messages[i];
            bool deleted = false;
            for (std::size_t j = 0; j < entry.flags.size(); ++j) {
                if (entry.flags[j].toLower() == "\\deleted") {
                    deleted = true;
                    break;
                }
            }
            if (!deleted) {
                continue;
            }
            batch.erase(toPrimaryKey_(swMailDetail::messageKey(normalizedAddress, normalizedMailbox, entry.uid)));
            removedCount += 1;
            removedBytes += entry.sizeBytes;
            if (!hasSeenFlag_(entry.flags)) {
                removedUnseen += 1;
            }
        }
        if (removedCount == 0) {
            if (removedCountOut) {
                *removedCountOut = 0;
            }
            return SwDbStatus::success();
        }

        mailbox.totalCount = mailbox.totalCount > removedCount ? mailbox.totalCount - removedCount : 0;
        mailbox.unseenCount = mailbox.unseenCount > removedUnseen ? mailbox.unseenCount - removedUnseen : 0;
        mailbox.updatedAt = swMailDetail::currentIsoTimestamp();
        account.usedBytes = account.usedBytes > removedBytes ? account.usedBytes - removedBytes : 0;
        account.updatedAt = mailbox.updatedAt;

        batch.put(toPrimaryKey_(swMailDetail::mailboxKey(normalizedAddress, normalizedMailbox)),
                  swMailDetail::jsonToBytes(mailboxToJson_(mailbox)),
                  secondaryKeysForMailbox_(mailbox));
        batch.put(toPrimaryKey_(swMailDetail::accountKey(account.address)),
                  swMailDetail::jsonToBytes(accountToJson_(account)),
                  secondaryKeysForAccount_(account));

        status = m_db.write(batch);
        if (status.ok() && removedCountOut) {
            *removedCountOut = removedCount;
        }
        return status;
    }

    SwDbStatus storeQueueItem(const SwMailQueueItem& item) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwMailQueueItem normalized = item;
        if (normalized.id.isEmpty()) {
            normalized.id = swMailDetail::generateId("queue");
        }
        if (normalized.createdAtMs <= 0) {
            normalized.createdAtMs = swMailDetail::currentEpochMs();
        }
        if (normalized.updatedAtMs <= 0) {
            normalized.updatedAtMs = normalized.createdAtMs;
        }
        if (normalized.nextAttemptAtMs <= 0) {
            normalized.nextAttemptAtMs = normalized.createdAtMs;
        }
        if (normalized.expireAtMs <= 0) {
            normalized.expireAtMs = normalized.createdAtMs + static_cast<long long>(m_config.queueMaxAgeMs);
        }

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::queueKey(normalized.id)),
                  swMailDetail::jsonToBytes(queueItemToJson_(normalized)),
                  secondaryKeysForQueueItem_(normalized));
        return m_db.write(batch);
    }

    SwList<SwMailQueueItem> listQueueItems() {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailQueueItem> items;
        if (!m_opened) {
            return items;
        }
        for (SwDbIterator it = m_db.scanPrimary(SwByteArray("queue/"), SwByteArray("queue/\xff"));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            items.append(queueItemFromJson_(object));
        }
        return items;
    }

    SwDbStatus getQueueItem(const SwString& id, SwMailQueueItem* outItem) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        if (!outItem) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output queue item");
        }
        SwByteArray value;
        const SwDbStatus status = m_db.get(toPrimaryKey_(swMailDetail::queueKey(id)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swMailDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid queue item");
        }
        *outItem = queueItemFromJson_(object);
        return SwDbStatus::success();
    }

    SwList<SwMailQueueItem> listDueQueueItems(long long nowEpochMs, std::size_t maxItems = 32) {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailQueueItem> items;
        if (!m_opened) {
            return items;
        }
        const SwString start = swMailDetail::queueDueSecondaryKey(0, SwString());
        const SwString end =
            swMailDetail::queueDueSecondaryKey(nowEpochMs, SwString("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));
        for (SwDbIterator it = m_db.scanIndex("mail.queueDue", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            items.append(queueItemFromJson_(object));
            if (items.size() >= maxItems) {
                break;
            }
        }
        return items;
    }

    SwDbStatus removeQueueItem(const SwString& id) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        SwDbWriteBatch batch;
        batch.erase(toPrimaryKey_(swMailDetail::queueKey(id)));
        return m_db.write(batch);
    }

    SwDbStatus upsertDkimRecord(const SwMailDkimRecord& record) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();

        SwMailDkimRecord normalized = record;
        normalized.domain = swMailDetail::normalizeDomain(record.domain);
        normalized.selector = record.selector.trimmed().toLower();
        if (normalized.createdAt.isEmpty()) {
            normalized.createdAt = swMailDetail::currentIsoTimestamp();
        }
        normalized.updatedAt = swMailDetail::currentIsoTimestamp();

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::dkimKey(normalized.domain, normalized.selector)),
                  swMailDetail::jsonToBytes(dkimToJson_(normalized)),
                  secondaryKeysForDkim_(normalized));
        return m_db.write(batch);
    }

    SwDbStatus getDkimRecord(const SwString& domain,
                             const SwString& selector,
                             SwMailDkimRecord* outRecord) {
        SwMutexLocker locker(&m_mutex);
        SW_MAIL_RETURN_IF_NOT_OPEN_();
        if (!outRecord) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output DKIM record");
        }
        SwByteArray value;
        const SwDbStatus status =
            m_db.get(toPrimaryKey_(swMailDetail::dkimKey(domain, selector)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swMailDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid DKIM record");
        }
        *outRecord = dkimFromJson_(object);
        return SwDbStatus::success();
    }

    SwList<SwMailDkimRecord> listDkimRecords() {
        SwMutexLocker locker(&m_mutex);
        SwList<SwMailDkimRecord> records;
        if (!m_opened) {
            return records;
        }
        const SwString start = swMailDetail::dkimSecondaryKey(m_config.domain, SwString());
        const SwString end = start + "\xff";
        for (SwDbIterator it = m_db.scanIndex("mail.dkim", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            records.append(dkimFromJson_(object));
        }
        return records;
    }

    // Résolution trois-voies : comptes locaux d'un côté, cibles externes issues
    // d'alias de l'autre. L'expansion d'alias est récursive (profondeur bornée,
    // cycles ignorés). Retourne NotFound si un destinataire de premier niveau
    // n'est ni un compte ni un alias actif du domaine.
    SwDbStatus resolveRecipients(const SwList<SwString>& recipients,
                                 SwList<SwString>* outLocalTargets,
                                 SwList<SwMailResolvedForward>* outForwardTargets);

    SwDbStatus resolveLocalRecipients(const SwList<SwString>& recipients, SwList<SwString>* outTargets);

private:
    SwEmbeddedDb m_db;
    SwMailConfig m_config;
    mutable SwMutex m_mutex;
    bool m_opened = false;

    static SwByteArray toPrimaryKey_(const SwString& key) {
        return SwByteArray(key.toUtf8());
    }

    static SwByteArray toSecondaryKey_(const SwString& key) {
        return SwByteArray(key.toUtf8());
    }

    SwDbStatus ensureDefaultMailboxesLocked_(const SwString& accountAddress) {
        const SwList<SwString> names = swMailDetail::defaultMailboxNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            SwMailMailbox mailbox;
            const SwDbStatus status = ensureMailboxLocked_(accountAddress, names[i], &mailbox);
            if (!status.ok()) {
                return status;
            }
        }
        return SwDbStatus::success();
    }

    SwDbStatus ensureMailboxLocked_(const SwString& accountAddress,
                                    const SwString& mailboxName,
                                    SwMailMailbox* outMailbox) {
        const SwString normalizedAddress = swMailDetail::canonicalAddress(accountAddress);
        const SwString normalizedMailbox = swMailDetail::normalizeMailboxName(mailboxName);
        SwByteArray value;
        const SwDbStatus getStatus =
            m_db.get(toPrimaryKey_(swMailDetail::mailboxKey(normalizedAddress, normalizedMailbox)), &value, nullptr);
        if (getStatus.ok()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(value, object)) {
                return SwDbStatus(SwDbStatus::Corruption, "Invalid mailbox record");
            }
            if (outMailbox) {
                *outMailbox = mailboxFromJson_(object);
            }
            return SwDbStatus::success();
        }
        if (getStatus.code() != SwDbStatus::NotFound) {
            return getStatus;
        }

        SwMailMailbox mailbox;
        mailbox.accountAddress = normalizedAddress;
        mailbox.name = normalizedMailbox;
        mailbox.uidNext = 1;
        mailbox.totalCount = 0;
        mailbox.unseenCount = 0;
        mailbox.createdAt = swMailDetail::currentIsoTimestamp();
        mailbox.updatedAt = mailbox.createdAt;

        SwDbWriteBatch batch;
        batch.put(toPrimaryKey_(swMailDetail::mailboxKey(normalizedAddress, normalizedMailbox)),
                  swMailDetail::jsonToBytes(mailboxToJson_(mailbox)),
                  secondaryKeysForMailbox_(mailbox));
        const SwDbStatus writeStatus = m_db.write(batch);
        if (writeStatus.ok() && outMailbox) {
            *outMailbox = mailbox;
        }
        return writeStatus;
    }

    SwDbStatus loadAccountLocked_(const SwString& canonicalAddress, SwMailAccount& outAccount) {
        SwByteArray value;
        const SwDbStatus status = m_db.get(toPrimaryKey_(swMailDetail::accountKey(canonicalAddress)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swMailDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid account record");
        }
        outAccount = accountFromJson_(object);
        return SwDbStatus::success();
    }

    static constexpr int kMaxAliasDepth_ = 8;

    SwDbStatus resolveRecipientLocked_(const SwString& address,
                                       const SwString& rootRecipient,
                                       int depth,
                                       SwList<SwString>& visitedAliases,
                                       SwList<SwString>* outLocalTargets,
                                       SwList<SwMailResolvedForward>* outForwardTargets) {
        if (depth > kMaxAliasDepth_) {
            return SwDbStatus(SwDbStatus::InvalidArgument, "Alias expansion too deep: " + rootRecipient);
        }

        SwMailAccount account;
        if (loadAccountLocked_(address, account).ok()) {
            outLocalTargets->append(account.address);
            return SwDbStatus::success();
        }

        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(address, localPart, domain)) {
            if (depth == 0) {
                return SwDbStatus(SwDbStatus::NotFound, "Recipient not local: " + address);
            }
            return SwDbStatus::success();
        }

        // L'alias est cherché quel que soit le domaine : le domaine géré comme
        // les domaines custom hébergés peuvent porter des alias.
        SwByteArray value;
        const SwDbStatus aliasStatus =
            m_db.get(toPrimaryKey_(swMailDetail::aliasKey(domain, localPart)), &value, nullptr);
        if (!aliasStatus.ok()) {
            if (domain != m_config.domain) {
                if (depth == 0) {
                    return SwDbStatus(SwDbStatus::NotFound, "Recipient not local: " + address);
                }
                SwMailResolvedForward forward;
                forward.aliasAddress = rootRecipient;
                forward.target = address;
                outForwardTargets->append(forward);
                return SwDbStatus::success();
            }
            if (depth == 0) {
                return SwDbStatus(SwDbStatus::NotFound, "Recipient not found: " + address);
            }
            // Cible d'alias locale inexistante : on l'ignore plutôt que d'échouer
            // toute la livraison.
            return SwDbStatus::success();
        }
        SwJsonObject aliasObject;
        if (!swMailDetail::parseJsonObject(value, aliasObject)) {
            return SwDbStatus(SwDbStatus::Corruption, "Corrupt alias record");
        }
        const SwMailAlias alias = aliasFromJson_(aliasObject);
        if (!alias.active || alias.targets.isEmpty()) {
            if (depth == 0) {
                return SwDbStatus(SwDbStatus::NotFound, "Recipient disabled: " + address);
            }
            return SwDbStatus::success();
        }

        for (std::size_t i = 0; i < visitedAliases.size(); ++i) {
            if (visitedAliases[i] == alias.address) {
                return SwDbStatus::success();
            }
        }
        visitedAliases.append(alias.address);

        for (std::size_t i = 0; i < alias.targets.size(); ++i) {
            const SwString canonical = swMailDetail::canonicalAddress(alias.targets[i]);
            if (canonical.isEmpty()) {
                continue;
            }
            const SwDbStatus status = resolveRecipientLocked_(canonical,
                                                              rootRecipient,
                                                              depth + 1,
                                                              visitedAliases,
                                                              outLocalTargets,
                                                              outForwardTargets);
            if (!status.ok()) {
                return status;
            }
        }
        return SwDbStatus::success();
    }

    SwDbStatus getMessageUnlocked_(const SwString& accountAddress,
                                   const SwString& mailboxName,
                                   unsigned long long uid,
                                   SwMailMessageEntry& outMessage) {
        SwByteArray value;
        const SwDbStatus status =
            m_db.get(toPrimaryKey_(swMailDetail::messageKey(accountAddress, mailboxName, uid)), &value, nullptr);
        if (!status.ok()) {
            return status;
        }
        SwJsonObject object;
        if (!swMailDetail::parseJsonObject(value, object)) {
            return SwDbStatus(SwDbStatus::Corruption, "Invalid stored message");
        }
        outMessage = messageFromJson_(object);
        return SwDbStatus::success();
    }

    SwList<SwMailMessageEntry> listMessagesUnlocked_(const SwString& accountAddress,
                                                     const SwString& mailboxName) {
        SwList<SwMailMessageEntry> messages;
        const SwString start = swMailDetail::messagesSecondaryKey(accountAddress, mailboxName, 0);
        const SwString end = accountAddress + "\x1f" + mailboxName + "\x1f" + "\xff";
        for (SwDbIterator it = m_db.scanIndex("mail.messages", toSecondaryKey_(start), toSecondaryKey_(end));
             it.isValid();
             it.next()) {
            SwJsonObject object;
            if (!swMailDetail::parseJsonObject(it.current().value, object)) {
                continue;
            }
            messages.append(messageFromJson_(object));
        }
        return messages;
    }

    static bool hasSeenFlag_(const SwList<SwString>& flags) {
        for (std::size_t i = 0; i < flags.size(); ++i) {
            if (flags[i].toLower() == "\\seen") {
                return true;
            }
        }
        return false;
    }

    static SwJsonObject accountToJson_(const SwMailAccount& account) {
        SwJsonObject object;
        object["address"] = account.address;
        object["domain"] = account.domain;
        object["localPart"] = account.localPart;
        object["passwordSalt"] = account.passwordSalt;
        object["passwordHash"] = account.passwordHash;
        object["active"] = account.active;
        object["canReceive"] = account.canReceive;
        object["canSend"] = account.canSend;
        object["suspended"] = account.suspended;
        object["quotaBytes"] = static_cast<long long>(account.quotaBytes);
        object["usedBytes"] = static_cast<long long>(account.usedBytes);
        object["createdAt"] = account.createdAt;
        object["updatedAt"] = account.updatedAt;
        return object;
    }

    static SwMailAccount accountFromJson_(const SwJsonObject& object) {
        SwMailAccount account;
        account.address = object.value("address").toString();
        account.domain = object.value("domain").toString();
        account.localPart = object.value("localPart").toString();
        account.passwordSalt = object.value("passwordSalt").toString();
        account.passwordHash = object.value("passwordHash").toString();
        account.active = object.value("active").toBool(true);
        account.canReceive = object.value("canReceive").toBool(true);
        account.canSend = object.value("canSend").toBool(true);
        account.suspended = object.value("suspended").toBool(false);
        account.quotaBytes = static_cast<unsigned long long>(object.value("quotaBytes").toInteger(0));
        account.usedBytes = static_cast<unsigned long long>(object.value("usedBytes").toInteger(0));
        account.createdAt = object.value("createdAt").toString();
        account.updatedAt = object.value("updatedAt").toString();
        return account;
    }

    static SwJsonObject aliasToJson_(const SwMailAlias& alias) {
        SwJsonObject object;
        object["address"] = alias.address;
        object["domain"] = alias.domain;
        object["localPart"] = alias.localPart;
        object["targets"] = swMailDetail::toJsonArray(alias.targets);
        object["active"] = alias.active;
        object["createdAt"] = alias.createdAt;
        object["updatedAt"] = alias.updatedAt;
        return object;
    }

    static SwMailAlias aliasFromJson_(const SwJsonObject& object) {
        SwMailAlias alias;
        alias.address = object.value("address").toString();
        alias.domain = object.value("domain").toString();
        alias.localPart = object.value("localPart").toString();
        alias.targets = swMailDetail::fromJsonStringArray(object.value("targets"));
        alias.active = object.value("active").toBool(true);
        alias.createdAt = object.value("createdAt").toString();
        alias.updatedAt = object.value("updatedAt").toString();
        return alias;
    }

    static SwJsonObject mailboxToJson_(const SwMailMailbox& mailbox) {
        SwJsonObject object;
        object["accountAddress"] = mailbox.accountAddress;
        object["name"] = mailbox.name;
        object["uidNext"] = static_cast<long long>(mailbox.uidNext);
        object["totalCount"] = static_cast<long long>(mailbox.totalCount);
        object["unseenCount"] = static_cast<long long>(mailbox.unseenCount);
        object["createdAt"] = mailbox.createdAt;
        object["updatedAt"] = mailbox.updatedAt;
        return object;
    }

    static SwMailMailbox mailboxFromJson_(const SwJsonObject& object) {
        SwMailMailbox mailbox;
        mailbox.accountAddress = object.value("accountAddress").toString();
        mailbox.name = object.value("name").toString();
        mailbox.uidNext = static_cast<unsigned long long>(object.value("uidNext").toInteger(1));
        mailbox.totalCount = static_cast<unsigned long long>(object.value("totalCount").toInteger(0));
        mailbox.unseenCount = static_cast<unsigned long long>(object.value("unseenCount").toInteger(0));
        mailbox.createdAt = object.value("createdAt").toString();
        mailbox.updatedAt = object.value("updatedAt").toString();
        return mailbox;
    }

    static SwJsonObject messageToJson_(const SwMailMessageEntry& entry) {
        SwJsonObject object;
        object["accountAddress"] = entry.accountAddress;
        object["mailboxName"] = entry.mailboxName;
        object["uid"] = static_cast<long long>(entry.uid);
        object["flags"] = swMailDetail::toJsonArray(entry.flags);
        object["internalDate"] = entry.internalDate;
        object["subject"] = entry.subject;
        object["from"] = entry.from;
        object["to"] = swMailDetail::toJsonArray(entry.to);
        object["cc"] = swMailDetail::toJsonArray(entry.cc);
        object["bcc"] = swMailDetail::toJsonArray(entry.bcc);
        object["messageId"] = entry.messageId;
        object["authResults"] = entry.authResults;
        object["sizeBytes"] = static_cast<long long>(entry.sizeBytes);
        object["raw"] = SwString(entry.rawMessage.toBase64());
        return object;
    }

    static SwMailMessageEntry messageFromJson_(const SwJsonObject& object) {
        SwMailMessageEntry entry;
        entry.accountAddress = object.value("accountAddress").toString();
        entry.mailboxName = object.value("mailboxName").toString();
        entry.uid = static_cast<unsigned long long>(object.value("uid").toInteger(0));
        entry.flags = swMailDetail::fromJsonStringArray(object.value("flags"));
        entry.internalDate = object.value("internalDate").toString();
        entry.subject = object.value("subject").toString();
        entry.from = object.value("from").toString();
        entry.to = swMailDetail::fromJsonStringArray(object.value("to"));
        entry.cc = swMailDetail::fromJsonStringArray(object.value("cc"));
        entry.bcc = swMailDetail::fromJsonStringArray(object.value("bcc"));
        entry.messageId = object.value("messageId").toString();
        entry.authResults = object.value("authResults").toString();  // legacy → vide (compat)
        entry.sizeBytes = static_cast<unsigned long long>(object.value("sizeBytes").toInteger(0));
        entry.rawMessage = SwByteArray::fromBase64(SwByteArray(object.value("raw").toString()));
        return entry;
    }

    static SwJsonObject queueItemToJson_(const SwMailQueueItem& item) {
        SwJsonObject object;
        object["id"] = item.id;
        object["mailFrom"] = item.envelope.mailFrom;
        object["rcptTo"] = swMailDetail::toJsonArray(item.envelope.rcptTo);
        object["raw"] = SwString(item.rawMessage.toBase64());
        object["attemptCount"] = item.attemptCount;
        object["createdAtMs"] = item.createdAtMs;
        object["updatedAtMs"] = item.updatedAtMs;
        object["nextAttemptAtMs"] = item.nextAttemptAtMs;
        object["expireAtMs"] = item.expireAtMs;
        object["lastError"] = item.lastError;
        object["dkimDomain"] = item.dkimDomain;
        object["dkimSelector"] = item.dkimSelector;
        object["signedMessage"] = item.signedMessage;
        return object;
    }

    static SwMailQueueItem queueItemFromJson_(const SwJsonObject& object) {
        SwMailQueueItem item;
        item.id = object.value("id").toString();
        item.envelope.mailFrom = object.value("mailFrom").toString();
        item.envelope.rcptTo = swMailDetail::fromJsonStringArray(object.value("rcptTo"));
        item.rawMessage = SwByteArray::fromBase64(SwByteArray(object.value("raw").toString()));
        item.attemptCount = object.value("attemptCount").toInt(0);
        item.createdAtMs = static_cast<long long>(object.value("createdAtMs").toInteger(0));
        item.updatedAtMs = static_cast<long long>(object.value("updatedAtMs").toInteger(0));
        item.nextAttemptAtMs = static_cast<long long>(object.value("nextAttemptAtMs").toInteger(0));
        item.expireAtMs = static_cast<long long>(object.value("expireAtMs").toInteger(0));
        item.lastError = object.value("lastError").toString();
        item.dkimDomain = object.value("dkimDomain").toString();
        item.dkimSelector = object.value("dkimSelector").toString();
        item.signedMessage = object.value("signedMessage").toBool(false);
        return item;
    }

    static SwJsonObject dkimToJson_(const SwMailDkimRecord& record) {
        SwJsonObject object;
        object["domain"] = record.domain;
        object["selector"] = record.selector;
        object["privateKeyPem"] = record.privateKeyPem;
        object["publicKeyTxt"] = record.publicKeyTxt;
        object["createdAt"] = record.createdAt;
        object["updatedAt"] = record.updatedAt;
        return object;
    }

    static SwMailDkimRecord dkimFromJson_(const SwJsonObject& object) {
        SwMailDkimRecord record;
        record.domain = object.value("domain").toString();
        record.selector = object.value("selector").toString();
        record.privateKeyPem = object.value("privateKeyPem").toString();
        record.publicKeyTxt = object.value("publicKeyTxt").toString();
        record.createdAt = object.value("createdAt").toString();
        record.updatedAt = object.value("updatedAt").toString();
        return record;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForAccount_(const SwMailAccount& account) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> values;
        values.append(SwByteArray(swMailDetail::accountsSecondaryKey(account.domain, account.address).toUtf8()));
        secondary["mail.accounts"] = values;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForAlias_(const SwMailAlias& alias) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> values;
        values.append(SwByteArray(swMailDetail::aliasesSecondaryKey(alias.domain, alias.localPart).toUtf8()));
        secondary["mail.aliases"] = values;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForMailbox_(const SwMailMailbox& mailbox) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> values;
        values.append(SwByteArray(swMailDetail::mailboxesSecondaryKey(mailbox.accountAddress, mailbox.name).toUtf8()));
        secondary["mail.mailboxes"] = values;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForMessage_(const SwMailMessageEntry& entry) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> values;
        values.append(
            SwByteArray(swMailDetail::messagesSecondaryKey(entry.accountAddress, entry.mailboxName, entry.uid).toUtf8()));
        secondary["mail.messages"] = values;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForQueueItem_(const SwMailQueueItem& item) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> dueValues;
        dueValues.append(SwByteArray(swMailDetail::queueDueSecondaryKey(item.nextAttemptAtMs, item.id).toUtf8()));
        secondary["mail.queueDue"] = dueValues;
        return secondary;
    }

    static SwMap<SwString, SwList<SwByteArray>> secondaryKeysForDkim_(const SwMailDkimRecord& record) {
        SwMap<SwString, SwList<SwByteArray>> secondary;
        SwList<SwByteArray> values;
        values.append(SwByteArray(swMailDetail::dkimSecondaryKey(record.domain, record.selector).toUtf8()));
        secondary["mail.dkim"] = values;
        return secondary;
    }
};

inline SwDbStatus SwMailStore::resolveRecipients(const SwList<SwString>& recipients,
                                                 SwList<SwString>* outLocalTargets,
                                                 SwList<SwMailResolvedForward>* outForwardTargets) {
    SwMutexLocker locker(&m_mutex);
    SW_MAIL_RETURN_IF_NOT_OPEN_();
    if (!outLocalTargets || !outForwardTargets) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output targets");
    }
    outLocalTargets->clear();
    outForwardTargets->clear();
    const SwList<SwString> normalizedRecipients = swMailDetail::normalizeRecipients(recipients);
    for (std::size_t i = 0; i < normalizedRecipients.size(); ++i) {
        SwList<SwString> visitedAliases;
        const SwDbStatus status = resolveRecipientLocked_(normalizedRecipients[i],
                                                          normalizedRecipients[i],
                                                          0,
                                                          visitedAliases,
                                                          outLocalTargets,
                                                          outForwardTargets);
        if (!status.ok()) {
            return status;
        }
    }
    *outLocalTargets = swMailDetail::normalizeRecipients(*outLocalTargets);

    SwList<SwMailResolvedForward> dedupedForwards;
    for (std::size_t i = 0; i < outForwardTargets->size(); ++i) {
        const SwMailResolvedForward& candidate = (*outForwardTargets)[i];
        bool exists = false;
        for (std::size_t j = 0; j < dedupedForwards.size(); ++j) {
            if (dedupedForwards[j].aliasAddress == candidate.aliasAddress &&
                dedupedForwards[j].target == candidate.target) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            dedupedForwards.append(candidate);
        }
    }
    *outForwardTargets = dedupedForwards;
    return SwDbStatus::success();
}

inline SwDbStatus SwMailStore::resolveLocalRecipients(const SwList<SwString>& recipients,
                                                      SwList<SwString>* outTargets) {
    if (!outTargets) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output targets");
    }
    SwList<SwMailResolvedForward> forwards;
    return resolveRecipients(recipients, outTargets, &forwards);
}

#undef SW_MAIL_RETURN_IF_NOT_OPEN_
