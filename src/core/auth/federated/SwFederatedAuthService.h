#pragma once

#include "SwObject.h"
#include "auth/federated/SwFederatedAuthProviderRegistry.h"
#include "auth/federated/SwFederatedAuthTransactionStore.h"
#include "auth/federated/SwFederatedLoginFlow.h"

class SwFederatedAuthService final : public SwObject {
    SW_OBJECT(SwFederatedAuthService, SwObject)

public:
    explicit SwFederatedAuthService(SwObject* parent = nullptr);

    SwFederatedAuthProviderRegistry& providers();
    const SwFederatedAuthProviderRegistry& providers() const;

    void setStorageDir(const SwString& storageDir);
    bool start(SwString* errorOut = nullptr);
    void stop();
    bool isStarted() const;

    SwDbStatus beginAuthorization(const SwString& providerKey,
                                  const SwString& redirectUri,
                                  const SwString& continuationId,
                                  bool forceAccountSelection,
                                  SwString& outAuthorizationUrl,
                                  SwString* errorOut = nullptr);

    SwFederatedLoginFlow* createLoginFlow(const SwString& providerKey,
                                          const SwString& state,
                                          const SwString& code,
                                          const SwJsonObject& callbackUser,
                                          SwFederatedAuthTransaction* outTransaction,
                                          SwObject* parent,
                                          SwString* errorOut = nullptr);

    SwDbStatus consumeTransaction(const SwString& providerKey,
                                  const SwString& state,
                                  SwFederatedAuthTransaction& outTransaction);

signals:
    DECLARE_SIGNAL(authorizationStarted, const SwString&)

private:
    static constexpr unsigned long long kTransactionTtlMs_ = 30ull * 60ull * 1000ull;

    SwFederatedAuthProviderRegistry providers_;
    SwFederatedAuthTransactionStore transactions_;
    SwString storageDir_ = "federated-auth-transactions";
    bool started_ = false;
};
