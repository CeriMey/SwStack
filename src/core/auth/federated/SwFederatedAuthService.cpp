#include "auth/federated/SwFederatedAuthService.h"

SwFederatedAuthService::SwFederatedAuthService(SwObject* parent)
    : SwObject(parent),
      providers_(this) {
}

SwFederatedAuthProviderRegistry& SwFederatedAuthService::providers() {
    return providers_;
}

const SwFederatedAuthProviderRegistry& SwFederatedAuthService::providers() const {
    return providers_;
}

void SwFederatedAuthService::setStorageDir(const SwString& storageDir) {
    if (!started_) {
        storageDir_ = storageDir.trimmed();
    }
}

bool SwFederatedAuthService::start(SwString* errorOut) {
    if (errorOut) {
        errorOut->clear();
    }
    if (started_) {
        return true;
    }
    transactions_.setStorageDir(storageDir_);
    const SwDbStatus status = transactions_.open();
    if (!status.ok()) {
        if (errorOut) {
            *errorOut = status.message();
        }
        return false;
    }
    started_ = true;
    return true;
}

void SwFederatedAuthService::stop() {
    transactions_.close();
    started_ = false;
}

bool SwFederatedAuthService::isStarted() const {
    return started_;
}

SwDbStatus SwFederatedAuthService::beginAuthorization(
    const SwString& providerKey,
    const SwString& redirectUri,
    const SwString& continuationId,
    bool forceAccountSelection,
    SwString& outAuthorizationUrl,
    SwString* errorOut) {
    outAuthorizationUrl.clear();
    if (errorOut) {
        errorOut->clear();
    }
    if (!started_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated auth service not started");
    }
    SwFederatedAuthProvider* provider = providers_.provider(providerKey);
    if (!provider) {
        return SwDbStatus(SwDbStatus::NotFound, "Federated provider not found");
    }
    if (!provider->isConfigured()) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Federated provider is not configured");
    }

    SwString state;
    SwFederatedAuthTransaction transaction;
    SwDbStatus status = transactions_.create(provider->definition().key,
                                              redirectUri,
                                              continuationId,
                                              kTransactionTtlMs_,
                                              state,
                                              &transaction);
    if (!status.ok()) {
        if (errorOut) {
            *errorOut = status.message();
        }
        return status;
    }

    SwFederatedAuthorizationRequest request;
    request.redirectUri = redirectUri;
    request.state = state;
    request.nonce = transaction.nonce;
    request.forceAccountSelection = forceAccountSelection;
    outAuthorizationUrl = provider->authorizationUrl(request, errorOut);
    if (outAuthorizationUrl.isEmpty()) {
        return SwDbStatus(SwDbStatus::InvalidArgument,
                          errorOut && !errorOut->isEmpty()
                              ? *errorOut
                              : SwString("Unable to build authorization URL"));
    }
    emit authorizationStarted(provider->definition().key);
    return SwDbStatus::success();
}

SwFederatedLoginFlow* SwFederatedAuthService::createLoginFlow(
    const SwString& providerKey,
    const SwString& state,
    const SwString& code,
    const SwJsonObject& callbackUser,
    SwFederatedAuthTransaction* outTransaction,
    SwObject* parent,
    SwString* errorOut) {
    if (errorOut) {
        errorOut->clear();
    }
    if (outTransaction) {
        *outTransaction = SwFederatedAuthTransaction();
    }
    if (!started_) {
        if (errorOut) {
            *errorOut = "Federated auth service not started";
        }
        return nullptr;
    }
    SwFederatedAuthProvider* provider = providers_.provider(providerKey);
    if (!provider) {
        if (errorOut) {
            *errorOut = "Federated provider not found";
        }
        return nullptr;
    }
    if (code.trimmed().isEmpty()) {
        if (errorOut) {
            *errorOut = "Federated authorization code is missing";
        }
        return nullptr;
    }

    SwFederatedAuthTransaction transaction;
    const SwDbStatus status = consumeTransaction(provider->definition().key, state, transaction);
    if (!status.ok()) {
        if (errorOut) {
            *errorOut = status.message();
        }
        return nullptr;
    }
    if (outTransaction) {
        *outTransaction = transaction;
    }
    return provider->createLoginFlow(code,
                                     transaction.redirectUri,
                                     transaction.nonce,
                                     callbackUser,
                                     parent);
}

SwDbStatus SwFederatedAuthService::consumeTransaction(
    const SwString& providerKey,
    const SwString& state,
    SwFederatedAuthTransaction& outTransaction) {
    outTransaction = SwFederatedAuthTransaction();
    if (!started_) {
        return SwDbStatus(SwDbStatus::NotOpen, "Federated auth service not started");
    }
    SwFederatedAuthProvider* provider = providers_.provider(providerKey);
    if (!provider) {
        return SwDbStatus(SwDbStatus::NotFound, "Federated provider not found");
    }
    return transactions_.consume(provider->definition().key, state, &outTransaction);
}
