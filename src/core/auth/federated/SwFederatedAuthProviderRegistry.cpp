#include "auth/federated/SwFederatedAuthProviderRegistry.h"

SwFederatedAuthProviderRegistry::SwFederatedAuthProviderRegistry(SwObject* parent)
    : SwObject(parent) {
}
bool SwFederatedAuthProviderRegistry::registerProvider(SwFederatedAuthProvider* provider,
                                                       SwString* errorOut) {
    if (errorOut) {
        errorOut->clear();
    }
    if (!provider) {
        if (errorOut) {
            *errorOut = "Federated provider is missing";
        }
        return false;
    }
    const SwString key = provider->definition().key.trimmed().toLower();
    if (key.isEmpty()) {
        if (errorOut) {
            *errorOut = "Federated provider key is missing";
        }
        return false;
    }
    if (providers_.contains(key)) {
        if (errorOut) {
            *errorOut = "Federated provider already registered";
        }
        return false;
    }
    provider->setParent(this);
    providers_.insert(key, provider);
    emit providerRegistered(key);
    return true;
}

SwFederatedAuthProvider* SwFederatedAuthProviderRegistry::provider(const SwString& key) const {
    return providers_.value(key.trimmed().toLower(), nullptr);
}

bool SwFederatedAuthProviderRegistry::contains(const SwString& key) const {
    return providers_.contains(key.trimmed().toLower());
}

SwList<SwString> SwFederatedAuthProviderRegistry::providerKeys() const {
    return providers_.keys();
}
