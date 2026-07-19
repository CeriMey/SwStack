#pragma once

#include "auth/federated/SwOidcClientCredential.h"

class SwOidcStaticClientCredential final : public SwOidcClientCredential {
    SW_OBJECT(SwOidcStaticClientCredential, SwOidcClientCredential)

public:
    SwOidcStaticClientCredential(const SwString& clientId,
                                 const SwString& clientSecret,
                                 SwObject* parent = nullptr);

    SwString clientId() const override;
    bool applyTokenAuthentication(SwMap<SwString, SwString>& fields,
                                  SwMap<SwString, SwString>& headers,
                                  SwString* errorOut = nullptr) const override;

private:
    SwString clientId_;
    SwString clientSecret_;
};
