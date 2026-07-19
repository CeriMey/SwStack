#include "auth/SwAuthLoginCompletionService.h"

SwAuthLoginCompletionService::SwAuthLoginCompletionService(
    SwAuthMfaService& mfaService,
    SwAuthSessionService& sessionService,
    SwObject* parent)
    : SwObject(parent),
      mfaService_(mfaService),
      sessionService_(sessionService) {
}
SwDbStatus SwAuthLoginCompletionService::complete(
    const SwHttpAuthAccount& account,
    const SwJsonValue& subject,
    const SwString& userAgent,
    bool viaTls,
    SwAuthLoginResult& outResult) {
    outResult = SwAuthLoginResult();
    outResult.account = account;
    outResult.subject = subject;
    if (account.suspended) {
        outResult.error = "Account suspended";
        emit completed(outResult);
        return SwDbStatus(SwDbStatus::Busy, outResult.error);
    }
    if (mfaService_.isRequired(account)) {
        const SwDbStatus status = mfaService_.createLoginChallenge(account, outResult.mfaChallenge);
        if (!status.ok()) {
            outResult.error = status.message();
            emit completed(outResult);
            return status;
        }
        outResult.disposition = SwAuthLoginDisposition::MfaRequired;
        emit completed(outResult);
        return SwDbStatus::success();
    }

    const SwDbStatus status = sessionService_.create(account,
                                                      subject,
                                                      userAgent,
                                                      viaTls,
                                                      outResult.rawToken,
                                                      outResult.session);
    if (!status.ok()) {
        outResult.error = status.message();
        emit completed(outResult);
        return status;
    }
    outResult.disposition = SwAuthLoginDisposition::Authenticated;
    emit completed(outResult);
    return SwDbStatus::success();
}
