#pragma once

#include "SwJsonObject.h"
#include "auth/federated/SwFederatedAuthTypes.h"

class SwOidcJwtValidator {
public:
    bool validate(const SwString& idToken,
                  const SwJsonObject& jwks,
                  const SwFederatedProviderDefinition& provider,
                  const SwString& clientId,
                  const SwString& expectedNonce,
                  SwFederatedIdentity& outIdentity,
                  SwString* errorOut = nullptr) const;

private:
    static bool splitJwt_(const SwString& token,
                          SwString& headerB64,
                          SwString& payloadB64,
                          SwString& signatureB64);
    static bool decodeJson_(const SwString& encoded, SwJsonObject& outObject);
    static bool verifyRs256_(const SwString& signingInput,
                             const SwString& signatureB64,
                             const SwJsonObject& jwks,
                             const SwJsonObject& header,
                             SwString* errorOut);
    static bool issuerMatches_(const SwString& issuer,
                               const SwFederatedProviderDefinition& provider);
    static bool audienceMatches_(const SwJsonValue& audience, const SwString& clientId);
    static bool truthy_(const SwJsonValue& value);
    static SwByteArray base64UrlDecode_(SwString encoded, bool* okOut = nullptr);
    static bool fail_(SwString* errorOut, const SwString& message);
};
