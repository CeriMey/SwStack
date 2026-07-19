#pragma once

#include "SwObject.h"
#include "auth/federated/SwFederatedAuthTypes.h"

class SwFederatedLoginFlow;

class SwFederatedAuthProvider : public SwObject {
    SW_OBJECT(SwFederatedAuthProvider, SwObject)

public:
    explicit SwFederatedAuthProvider(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwFederatedAuthProvider() override = default;

    virtual const SwFederatedProviderDefinition& definition() const = 0;
    virtual SwString clientId() const = 0;
    virtual bool isConfigured() const = 0;
    virtual SwString authorizationUrl(const SwFederatedAuthorizationRequest& request,
                                      SwString* errorOut = nullptr) const = 0;
    virtual bool tokenRequest(const SwString& code,
                              const SwString& redirectUri,
                              SwFederatedTokenRequest& outRequest,
                              SwString* errorOut = nullptr) const = 0;
    virtual SwFederatedLoginFlow* createLoginFlow(const SwString& code,
                                                  const SwString& redirectUri,
                                                  const SwString& expectedNonce,
                                                  const SwJsonObject& callbackUser,
                                                  SwObject* parent = nullptr) = 0;
};
