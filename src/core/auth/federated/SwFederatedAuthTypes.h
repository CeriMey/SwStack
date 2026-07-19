#pragma once

#include "SwByteArray.h"
#include "SwJsonValue.h"
#include "SwMap.h"
#include "SwString.h"

#include <vector>

enum class SwFederatedProtocol {
    OpenIdConnect,
    OAuth2UserInfo
};

enum class SwFederatedResponseMode {
    Query,
    FormPost
};

enum class SwFederatedIdentitySource {
    IdToken,
    UserInfo,
    IdTokenAndCallback
};

struct SwFederatedProviderDefinition {
    SwString key;
    SwString displayName;
    SwFederatedProtocol protocol = SwFederatedProtocol::OpenIdConnect;
    SwString issuer;
    std::vector<SwString> acceptedIssuers;
    SwString authorizationEndpoint;
    SwString tokenEndpoint;
    SwString jwksEndpoint;
    SwString userInfoEndpoint;
    SwString scope = "openid profile email";
    SwFederatedResponseMode responseMode = SwFederatedResponseMode::Query;
    SwFederatedIdentitySource identitySource = SwFederatedIdentitySource::IdToken;
    SwString accountSelectionPrompt;
    bool requiresNonce = true;
};

struct SwFederatedAuthorizationRequest {
    SwString redirectUri;
    SwString state;
    SwString nonce;
    bool forceAccountSelection = false;
};

struct SwFederatedTokenRequest {
    SwString url;
    SwByteArray body;
    SwString contentType = "application/x-www-form-urlencoded";
    SwMap<SwString, SwString> headers;
};

struct SwFederatedIdentity {
    SwString provider;
    SwString providerSubject;
    SwString email;
    bool emailVerified = false;
    SwString displayName;
    SwString pictureUrl;
    SwJsonValue rawClaims;
};

struct SwFederatedAuthTransaction {
    SwString stateHash;
    SwString provider;
    SwString nonce;
    SwString redirectUri;
    SwString continuationId;
    long long expiresAtMs = 0;
    SwString createdAt;
    SwString consumedAt;
};

struct SwFederatedIdentityLink {
    SwString provider;
    SwString providerSubject;
    SwString accountId;
    SwString email;
    SwString displayName;
    SwString createdAt;
    SwString updatedAt;
};

struct SwFederatedFlowError {
    SwString code;
    SwString message;
};
