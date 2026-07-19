#pragma once

#include "SwObject.h"
#include "auth/SwHttpAuthStore.h"

class SwAuthMfaService final : public SwObject {
    SW_OBJECT(SwAuthMfaService, SwObject)

public:
    SwAuthMfaService(SwHttpAuthStore& store,
                     const SwHttpAuthConfig& config,
                     SwObject* parent = nullptr);

    bool isRequired(const SwHttpAuthAccount& account) const;
    SwDbStatus createLoginChallenge(const SwHttpAuthAccount& account,
                                    SwHttpAuthMfaLoginChallenge& outChallenge);

signals:
    DECLARE_SIGNAL(challengeCreated,
                   const SwHttpAuthAccount&,
                   const SwHttpAuthMfaLoginChallenge&)

private:
    SwHttpAuthStore& store_;
    const SwHttpAuthConfig& config_;
};
