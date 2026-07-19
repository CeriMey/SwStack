#pragma once

#include "SwEmbeddedDb.h"
#include "SwMutex.h"
#include "auth/federated/SwFederatedAuthTypes.h"

class SwFederatedAuthTransactionStore {
public:
    SwFederatedAuthTransactionStore() = default;

    void setStorageDir(const SwString& storageDir);
    SwDbStatus open();
    void close();
    bool isOpen() const;

    SwDbStatus create(const SwString& provider,
                      const SwString& redirectUri,
                      const SwString& continuationId,
                      unsigned long long ttlMs,
                      SwString& outState,
                      SwFederatedAuthTransaction* outTransaction = nullptr);
    SwDbStatus consume(const SwString& provider,
                       const SwString& state,
                       SwFederatedAuthTransaction* outTransaction);

private:
    static SwString stateHash_(const SwString& state);
    static SwByteArray primaryKey_(const SwString& stateHash);
    static SwJsonObject toJson_(const SwFederatedAuthTransaction& transaction);
    static SwFederatedAuthTransaction fromJson_(const SwJsonObject& object);

    SwEmbeddedDb database_;
    SwString storageDir_ = "federated-auth-transactions";
    mutable SwMutex mutex_;
    bool opened_ = false;
};
