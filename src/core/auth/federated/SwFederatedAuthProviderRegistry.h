#pragma once

#include "SwMap.h"
#include "SwObject.h"
#include "auth/federated/SwFederatedAuthProvider.h"

class SwFederatedAuthProviderRegistry final : public SwObject {
    SW_OBJECT(SwFederatedAuthProviderRegistry, SwObject)

public:
    explicit SwFederatedAuthProviderRegistry(SwObject* parent = nullptr);

    bool registerProvider(SwFederatedAuthProvider* provider, SwString* errorOut = nullptr);
    SwFederatedAuthProvider* provider(const SwString& key) const;
    bool contains(const SwString& key) const;
    SwList<SwString> providerKeys() const;

signals:
    DECLARE_SIGNAL(providerRegistered, const SwString&)

private:
    SwMap<SwString, SwFederatedAuthProvider*> providers_;
};
