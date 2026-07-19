#pragma once

#include "SwEmbeddedDb.h"
#include "SwMutex.h"
#include "auth/federated/SwFederatedAuthTypes.h"

class SwFederatedIdentityStore {
public:
    SwFederatedIdentityStore() = default;

    void setStorageDir(const SwString& storageDir);
    SwDbStatus open();
    void close();
    bool isOpen() const;

    SwDbStatus get(const SwString& provider,
                   const SwString& providerSubject,
                   SwFederatedIdentityLink* outLink);
    SwDbStatus upsert(const SwFederatedIdentityLink& link,
                      SwFederatedIdentityLink* outLink = nullptr);
    SwDbStatus remove(const SwString& provider, const SwString& providerSubject);

private:
    static SwByteArray primaryKey_(const SwString& provider, const SwString& providerSubject);
    static SwJsonObject toJson_(const SwFederatedIdentityLink& link);
    static SwFederatedIdentityLink fromJson_(const SwJsonObject& object);

    SwEmbeddedDb database_;
    SwString storageDir_ = "federated-identities";
    mutable SwMutex mutex_;
    bool opened_ = false;
};
