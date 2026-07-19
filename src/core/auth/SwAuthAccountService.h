#pragma once

#include "SwObject.h"
#include "auth/SwHttpAuthStore.h"

class SwAuthAccountService final : public SwObject {
    SW_OBJECT(SwAuthAccountService, SwObject)

public:
    explicit SwAuthAccountService(SwHttpAuthStore& store, SwObject* parent = nullptr);

    SwDbStatus findById(const SwString& accountId, SwHttpAuthAccount& outAccount);
    SwDbStatus findByEmail(const SwString& email, SwHttpAuthAccount& outAccount);
    SwDbStatus createWithoutPassword(const SwString& email,
                                     const SwString& subjectId,
                                     SwHttpAuthAccount& outAccount);
    SwDbStatus setSubject(const SwString& accountId, const SwString& subjectId);
    SwDbStatus markEmailVerified(const SwString& accountId);
    SwDbStatus remove(const SwString& accountId);

signals:
    DECLARE_SIGNAL(accountCreated, const SwHttpAuthAccount&)
    DECLARE_SIGNAL(accountUpdated, const SwHttpAuthAccount&)
    DECLARE_SIGNAL(accountRemoved, const SwString&)

private:
    SwHttpAuthStore& store_;
};
