#pragma once

#include "SwHttpClient.h"
#include "SwJsonObject.h"
#include "auth/federated/SwFederatedAuthProvider.h"
#include "auth/federated/SwFederatedLoginFlow.h"
#include "auth/federated/SwOidcJwtValidator.h"

class SwOidcLoginFlow final : public SwFederatedLoginFlow {
    SW_OBJECT(SwOidcLoginFlow, SwFederatedLoginFlow)

public:
    SwOidcLoginFlow(SwFederatedAuthProvider& provider,
                    const SwString& code,
                    const SwString& redirectUri,
                    const SwString& expectedNonce,
                    const SwJsonObject& callbackUser,
                    SwObject* parent = nullptr);

public slots:
    void start() override;
    void cancel() override;

private slots:
    void onTokenResponse_(const SwByteArray& body);
    void onTokenError_(int error);
    void onJwksResponse_(const SwByteArray& body);
    void onJwksError_(int error);

private:
    void fail_(const SwString& code, const SwString& message);
    void finishIdentity_(SwFederatedIdentity identity);

    SwFederatedAuthProvider& provider_;
    SwString code_;
    SwString redirectUri_;
    SwString expectedNonce_;
    SwJsonObject callbackUser_;
    SwHttpClient* tokenClient_ = nullptr;
    SwHttpClient* jwksClient_ = nullptr;
    SwString idToken_;
    bool started_ = false;
    bool done_ = false;
    SwOidcJwtValidator validator_;
};
