#include "auth/SwAuthSessionService.h"

SwAuthSessionService::SwAuthSessionService(SwHttpAuthStore& store,
                                           const SwHttpAuthConfig& config,
                                           SwObject* parent)
    : SwObject(parent),
      store_(store),
      config_(config) {
}
SwDbStatus SwAuthSessionService::create(const SwHttpAuthAccount& account,
                                        const SwJsonValue& subject,
                                        const SwString& userAgent,
                                        bool viaTls,
                                        SwString& outRawToken,
                                        SwHttpAuthSession& outSession) {
    outRawToken.clear();
    outSession = SwHttpAuthSession();
    const SwDbStatus status = store_.createSession(account.accountId,
                                                   userAgent,
                                                   viaTls,
                                                   config_.sessionTtlMs,
                                                   &outRawToken,
                                                   &outSession);
    if (status.ok()) {
        emit sessionCreated(account, outSession, subject);
    }
    return status;
}

SwDbStatus SwAuthSessionService::revoke(const SwString& rawToken) {
    const SwString token = rawToken.trimmed();
    if (token.isEmpty()) {
        return SwDbStatus::success();
    }
    const SwDbStatus status = store_.removeSessionByToken(token);
    if (status.ok() || status.code() == SwDbStatus::NotFound) {
        emit sessionRevoked(token);
        return SwDbStatus::success();
    }
    return status;
}
