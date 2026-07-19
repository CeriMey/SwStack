#include "auth/federated/SwOidcProvider.h"

#include "auth/SwHttpAuthTypes.h"
#include "auth/federated/SwOidcLoginFlow.h"

SwOidcProvider::SwOidcProvider(const SwFederatedProviderDefinition& definition,
                               SwOidcClientCredential* credential,
                               SwObject* parent)
    : SwFederatedAuthProvider(parent),
      definition_(definition),
      credential_(credential) {
    definition_.key = definition_.key.trimmed().toLower();
    if (credential_) {
        credential_->setParent(this);
    }
}

const SwFederatedProviderDefinition& SwOidcProvider::definition() const {
    return definition_;
}

SwString SwOidcProvider::clientId() const {
    return credential_ ? credential_->clientId() : SwString();
}

bool SwOidcProvider::isConfigured() const {
    return credential_ &&
           !credential_->clientId().trimmed().isEmpty() &&
           !definition_.authorizationEndpoint.trimmed().isEmpty() &&
           !definition_.tokenEndpoint.trimmed().isEmpty();
}

SwString SwOidcProvider::authorizationUrl(const SwFederatedAuthorizationRequest& request,
                                          SwString* errorOut) const {
    if (errorOut) {
        errorOut->clear();
    }
    if (!isConfigured()) {
        if (errorOut) {
            *errorOut = "Federated provider is not configured";
        }
        return SwString();
    }
    if (request.redirectUri.trimmed().isEmpty() || request.state.trimmed().isEmpty()) {
        if (errorOut) {
            *errorOut = "Federated authorization request is incomplete";
        }
        return SwString();
    }

    SwString url = definition_.authorizationEndpoint;
    url = appendQuery_(url, "client_id", credential_->clientId());
    url = appendQuery_(url, "redirect_uri", request.redirectUri.trimmed());
    url = appendQuery_(url, "response_type", "code");
    url = appendQuery_(url, "scope", definition_.scope);
    url = appendQuery_(url, "state", request.state);
    if (definition_.requiresNonce) {
        if (request.nonce.trimmed().isEmpty()) {
            if (errorOut) {
                *errorOut = "OIDC nonce is required";
            }
            return SwString();
        }
        url = appendQuery_(url, "nonce", request.nonce);
    }
    if (definition_.responseMode == SwFederatedResponseMode::FormPost) {
        url = appendQuery_(url, "response_mode", "form_post");
    }
    if (request.forceAccountSelection && !definition_.accountSelectionPrompt.trimmed().isEmpty()) {
        url = appendQuery_(url, "prompt", definition_.accountSelectionPrompt.trimmed());
    }
    return url;
}

bool SwOidcProvider::tokenRequest(const SwString& code,
                                  const SwString& redirectUri,
                                  SwFederatedTokenRequest& outRequest,
                                  SwString* errorOut) const {
    outRequest = SwFederatedTokenRequest();
    if (errorOut) {
        errorOut->clear();
    }
    if (!isConfigured() || code.trimmed().isEmpty() || redirectUri.trimmed().isEmpty()) {
        if (errorOut) {
            *errorOut = "OIDC token request is incomplete";
        }
        return false;
    }

    SwMap<SwString, SwString> fields;
    fields["grant_type"] = "authorization_code";
    fields["code"] = code.trimmed();
    fields["redirect_uri"] = redirectUri.trimmed();
    if (!credential_->applyTokenAuthentication(fields, outRequest.headers, errorOut)) {
        return false;
    }
    outRequest.url = definition_.tokenEndpoint;
    outRequest.body = formBody_(fields);
    return true;
}

SwFederatedLoginFlow* SwOidcProvider::createLoginFlow(
    const SwString& code,
    const SwString& redirectUri,
    const SwString& expectedNonce,
    const SwJsonObject& callbackUser,
    SwObject* parent) {
    return new SwOidcLoginFlow(*this,
                               code,
                               redirectUri,
                               expectedNonce,
                               callbackUser,
                               parent);
}

SwString SwOidcProvider::appendQuery_(const SwString& url,
                                      const SwString& key,
                                      const SwString& value) {
    SwString result = url;
    result += result.contains("?") ? "&" : "?";
    result += swHttpAuthDetail::urlEncodeComponent(key);
    result += "=";
    result += swHttpAuthDetail::urlEncodeComponent(value);
    return result;
}

SwByteArray SwOidcProvider::formBody_(const SwMap<SwString, SwString>& fields) {
    SwString body;
    for (SwMap<SwString, SwString>::const_iterator it = fields.begin(); it != fields.end(); ++it) {
        if (!body.isEmpty()) {
            body += "&";
        }
        body += swHttpAuthDetail::urlEncodeComponent(it.key());
        body += "=";
        body += swHttpAuthDetail::urlEncodeComponent(it.value());
    }
    return SwByteArray(body.toStdString());
}
