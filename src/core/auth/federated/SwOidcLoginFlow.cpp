#include "auth/federated/SwOidcLoginFlow.h"

#include "SwJsonDocument.h"

SwOidcLoginFlow::SwOidcLoginFlow(SwFederatedAuthProvider& provider,
                                 const SwString& code,
                                 const SwString& redirectUri,
                                 const SwString& expectedNonce,
                                 const SwJsonObject& callbackUser,
                                 SwObject* parent)
    : SwFederatedLoginFlow(parent),
      provider_(provider),
      code_(code.trimmed()),
      redirectUri_(redirectUri.trimmed()),
      expectedNonce_(expectedNonce.trimmed()),
      callbackUser_(callbackUser) {
}

void SwOidcLoginFlow::start() {
    if (started_ || done_) {
        return;
    }
    started_ = true;

    SwFederatedTokenRequest request;
    SwString error;
    if (!provider_.tokenRequest(code_, redirectUri_, request, &error)) {
        fail_("token_request_invalid",
              error.isEmpty() ? SwString("Unable to build provider token request") : error);
        return;
    }

    tokenClient_ = new SwHttpClient(this);
    for (SwMap<SwString, SwString>::const_iterator it = request.headers.begin();
         it != request.headers.end();
         ++it) {
        tokenClient_->setRawHeader(it.key(), it.value());
    }
    connect(tokenClient_, &SwHttpClient::finished, this, &SwOidcLoginFlow::onTokenResponse_);
    connect(tokenClient_, &SwHttpClient::errorOccurred, this, &SwOidcLoginFlow::onTokenError_);
    if (!tokenClient_->post(request.url, request.body, request.contentType)) {
        fail_("token_endpoint_unavailable", "Provider token endpoint unavailable");
    }
}

void SwOidcLoginFlow::cancel() {
    if (done_) {
        return;
    }
    if (tokenClient_) {
        tokenClient_->abort();
    }
    if (jwksClient_) {
        jwksClient_->abort();
    }
    fail_("cancelled", "Federated login cancelled");
}

void SwOidcLoginFlow::onTokenResponse_(const SwByteArray& body) {
    if (done_) {
        return;
    }
    if (!tokenClient_ || tokenClient_->statusCode() < 200 || tokenClient_->statusCode() >= 300) {
        fail_("token_exchange_rejected", "Provider rejected the authorization code");
        return;
    }

    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(body.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        fail_("token_response_invalid", "Provider token response is invalid");
        return;
    }
    idToken_ = document.object().value("id_token").toString().trimmed();
    if (idToken_.isEmpty()) {
        fail_("id_token_missing", "Provider token response is missing id_token");
        return;
    }

    const SwString jwksEndpoint = provider_.definition().jwksEndpoint.trimmed();
    if (jwksEndpoint.isEmpty()) {
        fail_("jwks_endpoint_missing", "Provider JWKS endpoint is missing");
        return;
    }
    jwksClient_ = new SwHttpClient(this);
    connect(jwksClient_, &SwHttpClient::finished, this, &SwOidcLoginFlow::onJwksResponse_);
    connect(jwksClient_, &SwHttpClient::errorOccurred, this, &SwOidcLoginFlow::onJwksError_);
    if (!jwksClient_->get(jwksEndpoint)) {
        fail_("jwks_endpoint_unavailable", "Provider JWKS endpoint unavailable");
    }
}

void SwOidcLoginFlow::onTokenError_(int error) {
    (void)error;
    fail_("token_endpoint_unavailable", "Provider token endpoint unavailable");
}

void SwOidcLoginFlow::onJwksResponse_(const SwByteArray& body) {
    if (done_) {
        return;
    }
    if (!jwksClient_ || jwksClient_->statusCode() < 200 || jwksClient_->statusCode() >= 300) {
        fail_("jwks_response_rejected", "Provider JWKS endpoint returned an error");
        return;
    }

    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(body.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        fail_("jwks_response_invalid", "Provider JWKS response is invalid");
        return;
    }

    SwFederatedIdentity identity;
    if (!validator_.validate(idToken_,
                             document.object(),
                             provider_.definition(),
                             provider_.clientId(),
                             expectedNonce_,
                             identity,
                             &error)) {
        fail_("identity_validation_failed", error);
        return;
    }
    finishIdentity_(identity);
}

void SwOidcLoginFlow::onJwksError_(int error) {
    (void)error;
    fail_("jwks_endpoint_unavailable", "Provider JWKS endpoint unavailable");
}

void SwOidcLoginFlow::fail_(const SwString& code, const SwString& message) {
    if (done_) {
        return;
    }
    done_ = true;
    SwFederatedFlowError error;
    error.code = code;
    error.message = message.trimmed().isEmpty() ? SwString("Federated login failed") : message.trimmed();
    emit failed(error);
}

void SwOidcLoginFlow::finishIdentity_(SwFederatedIdentity identity) {
    if (done_) {
        return;
    }
    if (identity.displayName.trimmed().isEmpty() && !callbackUser_.isEmpty()) {
        const SwJsonObject name = callbackUser_.value("name").toObject();
        SwString displayName = callbackUser_.value("name").toString().trimmed();
        if (displayName.isEmpty()) {
            displayName = (name.value("firstName").toString() + " " +
                           name.value("lastName").toString()).trimmed();
        }
        identity.displayName = displayName;
    }
    done_ = true;
    emit identityVerified(identity);
}
