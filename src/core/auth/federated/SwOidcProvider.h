#pragma once

#include "auth/federated/SwFederatedAuthProvider.h"
#include "auth/federated/SwOidcClientCredential.h"

class SwOidcProvider final : public SwFederatedAuthProvider {
    SW_OBJECT(SwOidcProvider, SwFederatedAuthProvider)

public:
    SwOidcProvider(const SwFederatedProviderDefinition& definition,
                   SwOidcClientCredential* credential,
                   SwObject* parent = nullptr);

    const SwFederatedProviderDefinition& definition() const override;
    SwString clientId() const override;
    bool isConfigured() const override;
    SwString authorizationUrl(const SwFederatedAuthorizationRequest& request,
                              SwString* errorOut = nullptr) const override;
    bool tokenRequest(const SwString& code,
                      const SwString& redirectUri,
                      SwFederatedTokenRequest& outRequest,
                      SwString* errorOut = nullptr) const override;
    SwFederatedLoginFlow* createLoginFlow(const SwString& code,
                                          const SwString& redirectUri,
                                          const SwString& expectedNonce,
                                          const SwJsonObject& callbackUser,
                                          SwObject* parent = nullptr) override;

private:
    static SwString appendQuery_(const SwString& url, const SwString& key, const SwString& value);
    static SwByteArray formBody_(const SwMap<SwString, SwString>& fields);

    SwFederatedProviderDefinition definition_;
    SwOidcClientCredential* credential_ = nullptr;
};
