#include "auth/SwAuthMfaService.h"

SwAuthMfaService::SwAuthMfaService(SwHttpAuthStore& store,
                                   const SwHttpAuthConfig& config,
                                   SwObject* parent)
    : SwObject(parent),
      store_(store),
      config_(config) {
}
bool SwAuthMfaService::isRequired(const SwHttpAuthAccount& account) const {
    return account.mfaTotpEnabled && !account.mfaTotpSecret.trimmed().isEmpty();
}

SwDbStatus SwAuthMfaService::createLoginChallenge(
    const SwHttpAuthAccount& account,
    SwHttpAuthMfaLoginChallenge& outChallenge) {
    outChallenge = SwHttpAuthMfaLoginChallenge();
    SwString rawToken;
    SwHttpAuthChallenge record;
    const SwDbStatus status = store_.createChallenge("login_mfa",
                                                     account.accountId,
                                                     config_.mfaChallengeTtlMs,
                                                     &rawToken,
                                                     &record);
    if (!status.ok()) {
        return status;
    }
    outChallenge.challengeToken = rawToken;
    outChallenge.email = account.email;
    outChallenge.expiresAtMs = record.expiresAtMs;
    emit challengeCreated(account, outChallenge);
    return SwDbStatus::success();
}
