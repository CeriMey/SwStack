#pragma once

#include "SwObject.h"
#include "auth/SwAuthLoginResult.h"
#include "auth/SwAuthMfaService.h"
#include "auth/SwAuthSessionService.h"

class SwAuthLoginCompletionService final : public SwObject {
    SW_OBJECT(SwAuthLoginCompletionService, SwObject)

public:
    SwAuthLoginCompletionService(SwAuthMfaService& mfaService,
                                 SwAuthSessionService& sessionService,
                                 SwObject* parent = nullptr);

    SwDbStatus complete(const SwHttpAuthAccount& account,
                        const SwJsonValue& subject,
                        const SwString& userAgent,
                        bool viaTls,
                        SwAuthLoginResult& outResult);

signals:
    DECLARE_SIGNAL(completed, const SwAuthLoginResult&)

private:
    SwAuthMfaService& mfaService_;
    SwAuthSessionService& sessionService_;
};
