#include "auth/SwAuthAccountService.h"

#include "auth/SwHttpAuthTypes.h"

SwAuthAccountService::SwAuthAccountService(SwHttpAuthStore& store, SwObject* parent)
    : SwObject(parent),
      store_(store) {
}

SwDbStatus SwAuthAccountService::findById(const SwString& accountId,
                                          SwHttpAuthAccount& outAccount) {
    return store_.getAccountById(accountId, &outAccount);
}

SwDbStatus SwAuthAccountService::findByEmail(const SwString& email,
                                             SwHttpAuthAccount& outAccount) {
    return store_.getAccountByEmail(email, &outAccount);
}

SwDbStatus SwAuthAccountService::createWithoutPassword(const SwString& email,
                                                       const SwString& subjectId,
                                                       SwHttpAuthAccount& outAccount) {
    const SwDbStatus status = store_.createAccountWithoutPassword(email, subjectId, &outAccount);
    if (status.ok()) {
        emit accountCreated(outAccount);
    }
    return status;
}

SwDbStatus SwAuthAccountService::setSubject(const SwString& accountId,
                                            const SwString& subjectId) {
    SwDbStatus status = store_.setAccountSubjectId(accountId, subjectId);
    SwHttpAuthAccount account;
    if (status.ok() && store_.getAccountById(accountId, &account).ok()) {
        emit accountUpdated(account);
    }
    return status;
}

SwDbStatus SwAuthAccountService::markEmailVerified(const SwString& accountId) {
    SwDbStatus status = store_.setAccountEmailVerifiedAt(accountId,
                                                          swHttpAuthDetail::currentIsoTimestamp());
    SwHttpAuthAccount account;
    if (status.ok() && store_.getAccountById(accountId, &account).ok()) {
        emit accountUpdated(account);
    }
    return status;
}

SwDbStatus SwAuthAccountService::remove(const SwString& accountId) {
    const SwDbStatus status = store_.removeAccount(accountId);
    if (status.ok()) {
        emit accountRemoved(accountId);
    }
    return status;
}
