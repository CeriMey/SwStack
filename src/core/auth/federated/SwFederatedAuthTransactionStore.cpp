#include "auth/federated/SwFederatedAuthTransactionStore.h"

#include "SwJsonDocument.h"
#include "auth/SwHttpAuthTypes.h"

void SwFederatedAuthTransactionStore::setStorageDir(const SwString& storageDir) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        storageDir_ = storageDir.trimmed();
    }
}
SwDbStatus SwFederatedAuthTransactionStore::open() {
    SwMutexLocker locker(&mutex_);
    if (opened_) {
        return SwDbStatus::success();
    }
    SwEmbeddedDbOptions options;
    options.dbPath = (storageDir_.isEmpty() ? SwString("federated-auth-transactions") : storageDir_) + "/db";
    const SwDbStatus status = database_.open(options);
    if (status.ok()) {
        opened_ = true;
    }
    return status;
}

void SwFederatedAuthTransactionStore::close() {
    SwMutexLocker locker(&mutex_);
    if (opened_) {
        database_.close();
        opened_ = false;
    }
}

bool SwFederatedAuthTransactionStore::isOpen() const {
    SwMutexLocker locker(&mutex_);
    return opened_;
}

SwDbStatus SwFederatedAuthTransactionStore::create(
    const SwString& provider,
    const SwString& redirectUri,
    const SwString& continuationId,
    unsigned long long ttlMs,
    SwString& outState,
    SwFederatedAuthTransaction* outTransaction) {
    SwMutexLocker locker(&mutex_);
    outState.clear();
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated transaction store not open");
    }
    const SwString normalizedProvider = provider.trimmed().toLower();
    if (normalizedProvider.isEmpty() || redirectUri.trimmed().isEmpty()) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Federated transaction is incomplete");
    }

    outState = swHttpAuthDetail::randomHexToken(32);
    const SwString nonce = swHttpAuthDetail::randomHexToken(32);
    if (outState.isEmpty() || nonce.isEmpty()) {
        outState.clear();
        return SwDbStatus(SwDbStatus::IoError, "Unable to generate federated transaction");
    }

    SwFederatedAuthTransaction transaction;
    transaction.stateHash = stateHash_(outState);
    transaction.provider = normalizedProvider;
    transaction.nonce = nonce;
    transaction.redirectUri = redirectUri.trimmed();
    transaction.continuationId = continuationId.trimmed();
    transaction.expiresAtMs = swHttpAuthDetail::currentEpochMs() +
                              static_cast<long long>(ttlMs == 0 ? 10ull * 60ull * 1000ull : ttlMs);
    transaction.createdAt = swHttpAuthDetail::currentIsoTimestamp();

    SwDbWriteBatch batch;
    batch.put(primaryKey_(transaction.stateHash),
              SwByteArray(SwJsonDocument(toJson_(transaction)).toJson(SwJsonDocument::JsonFormat::Compact).toUtf8()));
    const SwDbStatus status = database_.write(batch);
    if (status.ok() && outTransaction) {
        *outTransaction = transaction;
    }
    if (!status.ok()) {
        outState.clear();
    }
    return status;
}

SwDbStatus SwFederatedAuthTransactionStore::consume(
    const SwString& provider,
    const SwString& state,
    SwFederatedAuthTransaction* outTransaction) {
    SwMutexLocker locker(&mutex_);
    if (!opened_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated transaction store not open");
    }
    if (!outTransaction || state.trimmed().isEmpty()) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Federated state is missing");
    }

    const SwByteArray key = primaryKey_(stateHash_(state));
    SwByteArray bytes;
    SwDbStatus status = database_.get(key, &bytes, nullptr);
    if (!status.ok()) {
        return status;
    }
    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(bytes.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        return SwDbStatus(SwDbStatus::Corruption, "Invalid federated transaction");
    }
    SwFederatedAuthTransaction transaction = fromJson_(document.object());
    if (transaction.provider != provider.trimmed().toLower()) {
        return SwDbStatus(SwDbStatus::NotFound, "Federated transaction not found");
    }
    if (!transaction.consumedAt.trimmed().isEmpty()) {
        return SwDbStatus(SwDbStatus::Busy, "Federated transaction already consumed");
    }
    if (transaction.expiresAtMs <= swHttpAuthDetail::currentEpochMs()) {
        SwDbWriteBatch expiredBatch;
        expiredBatch.erase(key);
        (void)database_.write(expiredBatch);
        return SwDbStatus(SwDbStatus::Busy, "Federated transaction expired");
    }

    transaction.consumedAt = swHttpAuthDetail::currentIsoTimestamp();
    SwDbWriteBatch batch;
    batch.put(key,
              SwByteArray(SwJsonDocument(toJson_(transaction)).toJson(SwJsonDocument::JsonFormat::Compact).toUtf8()));
    status = database_.write(batch);
    if (status.ok()) {
        *outTransaction = transaction;
    }
    return status;
}

SwString SwFederatedAuthTransactionStore::stateHash_(const SwString& state) {
    return swHttpAuthDetail::hashSha256(state.trimmed());
}

SwByteArray SwFederatedAuthTransactionStore::primaryKey_(const SwString& stateHash) {
    return SwByteArray((SwString("federated/transaction/") + stateHash).toUtf8());
}

SwJsonObject SwFederatedAuthTransactionStore::toJson_(const SwFederatedAuthTransaction& transaction) {
    SwJsonObject object;
    object["stateHash"] = transaction.stateHash;
    object["provider"] = transaction.provider;
    object["nonce"] = transaction.nonce;
    object["redirectUri"] = transaction.redirectUri;
    object["continuationId"] = transaction.continuationId;
    object["expiresAtMs"] = transaction.expiresAtMs;
    object["createdAt"] = transaction.createdAt;
    object["consumedAt"] = transaction.consumedAt;
    return object;
}

SwFederatedAuthTransaction SwFederatedAuthTransactionStore::fromJson_(const SwJsonObject& object) {
    SwFederatedAuthTransaction transaction;
    transaction.stateHash = object.value("stateHash").toString();
    transaction.provider = object.value("provider").toString();
    transaction.nonce = object.value("nonce").toString();
    transaction.redirectUri = object.value("redirectUri").toString();
    transaction.continuationId = object.value("continuationId").toString();
    transaction.expiresAtMs = object.value("expiresAtMs").toInteger(0);
    transaction.createdAt = object.value("createdAt").toString();
    transaction.consumedAt = object.value("consumedAt").toString();
    return transaction;
}
