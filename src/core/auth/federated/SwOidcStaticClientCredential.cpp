#include "auth/federated/SwOidcStaticClientCredential.h"

SwOidcStaticClientCredential::SwOidcStaticClientCredential(const SwString& clientId,
                                                           const SwString& clientSecret,
                                                           SwObject* parent)
    : SwOidcClientCredential(parent),
      clientId_(clientId.trimmed()),
      clientSecret_(clientSecret.trimmed()) {
}
SwString SwOidcStaticClientCredential::clientId() const {
    return clientId_;
}

bool SwOidcStaticClientCredential::applyTokenAuthentication(
    SwMap<SwString, SwString>& fields,
    SwMap<SwString, SwString>& headers,
    SwString* errorOut) const {
    (void)headers;
    if (errorOut) {
        errorOut->clear();
    }
    if (clientId_.isEmpty() || clientSecret_.isEmpty()) {
        if (errorOut) {
            *errorOut = "OIDC client credentials are not configured";
        }
        return false;
    }
    fields["client_id"] = clientId_;
    fields["client_secret"] = clientSecret_;
    return true;
}
