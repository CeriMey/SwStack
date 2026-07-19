#include "auth/federated/SwFederatedIdentityStore.h"

#include "SwJsonDocument.h"
#include "auth/SwHttpAuthTypes.h"

void SwFederatedIdentityStore::setStorageDir(const SwString& storageDir) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        storageDir_ = storageDir.trimmed();
    }
}
SwDbStatus SwFederatedIdentityStore::open() {
    SwMutexLocker locker(&mutex_);
    if (opened_) {
        return SwDbStatus::success();
    }
    SwEmbeddedDbOptions options;
    options.dbPath = (storageDir_.isEmpty() ? SwString("federated-identities") : storageDir_) + "/db";
    const SwDbStatus status = database_.open(options);
    if (status.ok()) {
        opened_ = true;
    }
    return status;
}

void SwFederatedIdentityStore::close() {
    SwMutexLocker locker(&mutex_);
    if (opened_) {
        database_.close();
        opened_ = false;
    }
}

bool SwFederatedIdentityStore::isOpen() const {
    SwMutexLocker locker(&mutex_);
    return opened_;
}

SwDbStatus SwFederatedIdentityStore::get(const SwString& provider,
                                         const SwString& providerSubject,
                                         SwFederatedIdentityLink* outLink) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated identity store not open");
    }
    if (!outLink) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Federated identity output is missing");
    }
    SwByteArray bytes;
    const SwDbStatus status = database_.get(primaryKey_(provider, providerSubject), &bytes, nullptr);
    if (!status.ok()) {
        return status;
    }
    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(bytes.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        return SwDbStatus(SwDbStatus::Corruption, "Invalid federated identity record");
    }
    *outLink = fromJson_(document.object());
    return SwDbStatus::success();
}

SwDbStatus SwFederatedIdentityStore::upsert(const SwFederatedIdentityLink& input,
                                            SwFederatedIdentityLink* outLink) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated identity store not open");
    }
    SwFederatedIdentityLink link = input;
    link.provider = link.provider.trimmed().toLower();
    link.providerSubject = link.providerSubject.trimmed();
    link.accountId = link.accountId.trimmed();
    link.email = swHttpAuthDetail::normalizeEmail(link.email);
    if (link.provider.isEmpty() || link.providerSubject.isEmpty() || link.accountId.isEmpty()) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Federated identity is incomplete");
    }
    if (link.createdAt.trimmed().isEmpty()) {
        link.createdAt = swHttpAuthDetail::currentIsoTimestamp();
    }
    link.updatedAt = swHttpAuthDetail::currentIsoTimestamp();
    SwDbWriteBatch batch;
    batch.put(primaryKey_(link.provider, link.providerSubject),
              SwByteArray(SwJsonDocument(toJson_(link)).toJson(SwJsonDocument::JsonFormat::Compact).toUtf8()));
    const SwDbStatus status = database_.write(batch);
    if (status.ok() && outLink) {
        *outLink = link;
    }
    return status;
}

SwDbStatus SwFederatedIdentityStore::remove(const SwString& provider,
                                            const SwString& providerSubject) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated identity store not open");
    }
    SwDbWriteBatch batch;
    batch.erase(primaryKey_(provider, providerSubject));
    return database_.write(batch);
}

SwByteArray SwFederatedIdentityStore::primaryKey_(const SwString& provider,
                                                  const SwString& providerSubject) {
    const SwString material = provider.trimmed().toLower() + ":" + providerSubject.trimmed();
    return SwByteArray((SwString("federated/identity/") + swHttpAuthDetail::hashSha256(material)).toUtf8());
}

SwJsonObject SwFederatedIdentityStore::toJson_(const SwFederatedIdentityLink& link) {
    SwJsonObject object;
    object["provider"] = link.provider;
    object["providerSubject"] = link.providerSubject;
    object["accountId"] = link.accountId;
    object["email"] = link.email;
    object["displayName"] = link.displayName;
    object["createdAt"] = link.createdAt;
    object["updatedAt"] = link.updatedAt;
    return object;
}

SwFederatedIdentityLink SwFederatedIdentityStore::fromJson_(const SwJsonObject& object) {
    SwFederatedIdentityLink link;
    link.provider = object.value("provider").toString();
    link.providerSubject = object.value("providerSubject").toString();
    link.accountId = object.value("accountId").toString();
    link.email = object.value("email").toString();
    link.displayName = object.value("displayName").toString();
    link.createdAt = object.value("createdAt").toString();
    link.updatedAt = object.value("updatedAt").toString();
    return link;
}
