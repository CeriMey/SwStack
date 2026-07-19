#pragma once

#include "SwObject.h"
#include "auth/SwHttpAuthStore.h"

class SwAuthSessionService final : public SwObject {
    SW_OBJECT(SwAuthSessionService, SwObject)

public:
    SwAuthSessionService(SwHttpAuthStore& store,
                         const SwHttpAuthConfig& config,
                         SwObject* parent = nullptr);

    SwDbStatus create(const SwHttpAuthAccount& account,
                      const SwJsonValue& subject,
                      const SwString& userAgent,
                      bool viaTls,
                      SwString& outRawToken,
                      SwHttpAuthSession& outSession);
    SwDbStatus revoke(const SwString& rawToken);

signals:
    DECLARE_SIGNAL(sessionCreated,
                   const SwHttpAuthAccount&,
                   const SwHttpAuthSession&,
                   const SwJsonValue&)
    DECLARE_SIGNAL(sessionRevoked, const SwString&)

private:
    SwHttpAuthStore& store_;
    const SwHttpAuthConfig& config_;
};
