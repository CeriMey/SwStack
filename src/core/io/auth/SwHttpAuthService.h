#pragma once

#include "SwDebug.h"
#include "SwMailService.h"
#include "SwMutex.h"
#include "SwObject.h"
#include "SwString.h"
#include "auth/SwHttpAuthStore.h"
#include "auth/SwHttpAuthTemplateRenderer.h"
#include "http/SwHttpContext.h"

class SwHttpAuthService : public SwObject {
    SW_OBJECT(SwHttpAuthService, SwObject)

public:
    explicit SwHttpAuthService(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwHttpAuthService() override {
        stop();
    }

    void setConfig(const SwHttpAuthConfig& config);
    const SwHttpAuthConfig& config() const;

    void setHooks(const SwHttpAuthHooks& hooks);
    const SwHttpAuthHooks& hooks() const;

    void setMailService(SwMailService* mailService);
    SwMailService* mailService();
    const SwMailService* mailService() const;

    SwHttpAuthStore& store();
    const SwHttpAuthStore& store() const;

    bool start(SwString* outError = nullptr);
    void stop();
    bool isStarted() const;

    SwString extractRequestToken(const SwString& cookieHeader, const SwString& authorizationHeader) const;
    void applyIdentityToContext(SwHttpContext& ctx, const SwHttpAuthIdentity& identity) const;
    bool resolveIdentityFromToken(const SwString& rawToken,
                                  SwHttpAuthIdentity* outIdentity,
                                  SwString* outError = nullptr);

    SwDbStatus registerAccount(const SwString& email,
                               const SwString& password,
                               const SwJsonValue& payload,
                               SwHttpAuthAccount* outAccount,
                               SwJsonValue* outSubject,
                               bool* outPendingEmailVerification,
                               SwString* outError = nullptr);
    SwDbStatus login(const SwString& email,
                     const SwString& password,
                     const SwString& userAgent,
                     bool viaTls,
                     SwString* outRawToken,
                     SwHttpAuthIdentity* outIdentity,
                     SwString* outPasswordResetToken = nullptr,
                     SwString* outError = nullptr);
    SwDbStatus logout(const SwString& rawToken);
    SwDbStatus requestEmailVerification(const SwString& email, SwString* outError = nullptr);
    SwDbStatus verifyEmail(const SwString& code,
                           const SwString& token,
                           SwHttpAuthAccount* outAccount,
                           SwJsonValue* outSubject,
                           SwString* outError = nullptr);
    SwDbStatus requestPasswordReset(const SwString& email, SwString* outError = nullptr);
    SwDbStatus resetPassword(const SwString& code,
                             const SwString& token,
                             const SwString& newPassword,
                             SwString* outError = nullptr);
    SwDbStatus changePassword(const SwString& rawToken,
                              const SwString& currentPassword,
                              const SwString& newPassword,
                              SwHttpAuthIdentity* outIdentity = nullptr,
                              SwString* outError = nullptr);
    bool loadSubjectView(const SwString& subjectId,
                         SwJsonValue* outSubject,
                         SwString* outError = nullptr) const;

private:
    struct ThrottleState_ {
        int failures = 0;
        long long windowStartMs = 0;
    };

    bool validateConfig_(SwString& outError) const;
    bool validateMailConfig_(SwString& outError) const;
    static bool validateMailTemplate_(const SwHttpAuthMailTemplate& mailTemplate);
    bool hasInternalMailDelivery_() const;
    bool throttleAllows_(const SwString& key);
    SwDbStatus lookupChallenge_(const SwString& purpose,
                                const SwString& code,
                                const SwString& token,
                                SwHttpAuthChallenge* outChallenge);
    static SwDbStatus validateChallenge_(const SwHttpAuthChallenge& challenge);
    SwDbStatus sendChallengeForAccount_(const SwHttpAuthAccount& account,
                                        const SwString& purpose,
                                        bool swallowDeliveryErrors,
                                        SwString* outError);
    SwString buildChallengeUrl_(const SwString& purpose,
                                const SwString& rawToken,
                                const SwString& configuredTemplate) const;
    bool sendMail_(const SwHttpAuthOutgoingMail& mail, SwString* outError);
    bool sendViaMailService_(const SwHttpAuthOutgoingMail& mail, SwString* outError);
    static SwByteArray buildMimeMessage_(const SwMailConfig& mailConfig, const SwHttpAuthOutgoingMail& mail);

    SwHttpAuthConfig m_config;
    SwHttpAuthHooks m_hooks;
    SwHttpAuthStore m_store;
    SwMailService* m_mailService = nullptr;
    mutable SwMutex m_mutex;
    SwMap<SwString, ThrottleState_> m_throttle;
    bool m_started = false;
};

namespace swHttpAuthServiceDetail {

inline SwString safeHeaderText_(const SwString& value) {
    std::string text = value.toStdString();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' || text[i] == '\n') {
            text[i] = ' ';
        }
    }
    return SwString(text);
}

inline SwJsonObject accountToJson_(const SwHttpAuthAccount& account) {
    SwJsonObject object;
    object["accountId"] = account.accountId.toStdString();
    object["subjectId"] = account.subjectId.toStdString();
    object["email"] = account.email.toStdString();
    object["emailVerified"] = !account.emailVerifiedAt.trimmed().isEmpty();
    object["emailVerifiedAt"] = account.emailVerifiedAt.toStdString();
    object["passwordResetRequired"] = account.passwordResetRequired;
    object["suspended"] = account.suspended;
    object["createdAt"] = account.createdAt.toStdString();
    object["updatedAt"] = account.updatedAt.toStdString();
    return object;
}

}

inline void SwHttpAuthService::setConfig(const SwHttpAuthConfig& config) {
    SwHttpAuthConfig normalized = config;
    normalized.routePrefix = swHttpAuthDetail::normalizeRoutePrefix(normalized.routePrefix);
    normalized.publicBaseUrl = swHttpAuthDetail::normalizeBaseUrl(normalized.publicBaseUrl);
    if (normalized.sessionCookieName.trimmed().isEmpty()) {
        normalized.sessionCookieName = "sw_auth";
    }
    if (normalized.passwordMinLength < 1) {
        normalized.passwordMinLength = 1;
    }
    m_config = normalized;
    m_store.setConfig(m_config);
}

inline const SwHttpAuthConfig& SwHttpAuthService::config() const {
    return m_config;
}

inline void SwHttpAuthService::setHooks(const SwHttpAuthHooks& hooks) {
    m_hooks = hooks;
}

inline const SwHttpAuthHooks& SwHttpAuthService::hooks() const {
    return m_hooks;
}

inline void SwHttpAuthService::setMailService(SwMailService* mailService) {
    m_mailService = mailService;
}

inline SwMailService* SwHttpAuthService::mailService() {
    return m_mailService;
}

inline const SwMailService* SwHttpAuthService::mailService() const {
    return m_mailService;
}

inline SwHttpAuthStore& SwHttpAuthService::store() {
    return m_store;
}

inline const SwHttpAuthStore& SwHttpAuthService::store() const {
    return m_store;
}

inline bool SwHttpAuthService::start(SwString* outError) {
    if (outError) {
        outError->clear();
    }

    SwString error;
    if (!validateConfig_(error)) {
        if (outError) {
            *outError = error;
        }
        return false;
    }

    m_store.setConfig(m_config);
    const SwDbStatus status = m_store.open();
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return false;
    }

    SwMutexLocker locker(&m_mutex);
    m_started = true;
    return true;
}

inline void SwHttpAuthService::stop() {
    {
        SwMutexLocker locker(&m_mutex);
        m_started = false;
        m_throttle.clear();
    }
    m_store.close();
}

inline bool SwHttpAuthService::isStarted() const {
    SwMutexLocker locker(&m_mutex);
    return m_started;
}

inline SwString SwHttpAuthService::extractRequestToken(const SwString& cookieHeader,
                                                       const SwString& authorizationHeader) const {
    const SwString bearer = swHttpAuthDetail::extractBearerToken(authorizationHeader);
    if (!bearer.isEmpty()) {
        return bearer;
    }
    return swHttpAuthDetail::parseCookieValue(cookieHeader, m_config.sessionCookieName);
}

inline void SwHttpAuthService::applyIdentityToContext(SwHttpContext& ctx, const SwHttpAuthIdentity& identity) const {
    if (!identity.authenticated) {
        return;
    }
    ctx.setLocal("auth/accountId", identity.account.accountId);
    ctx.setLocal("auth/subjectId", identity.account.subjectId);
    ctx.setLocal("auth/email", identity.account.email);
    ctx.setLocal("auth/emailVerified", identity.emailVerified ? "true" : "false");
    ctx.setLocal("auth/sessionId", identity.session.sessionId);
}

inline bool SwHttpAuthService::resolveIdentityFromToken(const SwString& rawToken,
                                                        SwHttpAuthIdentity* outIdentity,
                                                        SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (outIdentity) {
        *outIdentity = SwHttpAuthIdentity();
    }

    const SwString token = rawToken.trimmed();
    if (token.isEmpty()) {
        if (outError) {
            *outError = "Missing session token";
        }
        return false;
    }

    SwHttpAuthSession session;
    const SwDbStatus sessionStatus = m_store.getSessionByToken(token, &session);
    if (!sessionStatus.ok()) {
        if (outError) {
            *outError = sessionStatus.message();
        }
        return false;
    }

    if (session.expiresAtMs > 0 && session.expiresAtMs <= swHttpAuthDetail::currentEpochMs()) {
        (void)m_store.removeSessionByToken(token);
        if (outError) {
            *outError = "Session expired";
        }
        return false;
    }

    SwHttpAuthAccount account;
    const SwDbStatus accountStatus = m_store.getAccountById(session.accountId, &account);
    if (!accountStatus.ok()) {
        if (outError) {
            *outError = accountStatus.message();
        }
        return false;
    }
    if (account.suspended) {
        if (outError) {
            *outError = "Account suspended";
        }
        return false;
    }

    SwHttpAuthIdentity identity;
    identity.authenticated = true;
    identity.emailVerified = !account.emailVerifiedAt.trimmed().isEmpty();
    identity.account = account;
    identity.session = session;
    if (!account.subjectId.trimmed().isEmpty()) {
        SwString subjectError;
        (void)loadSubjectView(account.subjectId, &identity.subject, &subjectError);
    }

    if (outIdentity) {
        *outIdentity = identity;
    }
    return true;
}

inline SwDbStatus SwHttpAuthService::registerAccount(const SwString& email,
                                                     const SwString& password,
                                                     const SwJsonValue& payload,
                                                     SwHttpAuthAccount* outAccount,
                                                     SwJsonValue* outSubject,
                                                     bool* outPendingEmailVerification,
                                                     SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (outSubject) {
        *outSubject = SwJsonValue();
    }
    if (outPendingEmailVerification) {
        *outPendingEmailVerification = false;
    }

    if (!isStarted()) {
        return SwDbStatus(SwDbStatus::NotOpen, "Auth service not started");
    }
    if (!m_hooks.registerSubject) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "registerSubject hook missing");
    }

    SwHttpAuthAccount account;
    SwDbStatus status = m_store.createAccount(email, password, SwString(), &account);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwString subjectId;
    SwJsonValue subjectView;
    SwString registerError;
    const bool registered = m_hooks.registerSubject(account.email, payload, subjectId, subjectView, registerError);
    if (!registered || subjectId.trimmed().isEmpty()) {
        (void)m_store.removeAccount(account.accountId);
        const SwString message = registerError.isEmpty() ? SwString("Subject registration failed") : registerError;
        if (outError) {
            *outError = message;
        }
        return SwDbStatus(SwDbStatus::InvalidArgument, message);
    }

    status = m_store.setAccountSubjectId(account.accountId, subjectId.trimmed());
    if (!status.ok()) {
        (void)m_store.removeAccount(account.accountId);
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    if (m_config.verificationRequired) {
        status = sendChallengeForAccount_(account, "verify_email", false, outError);
        if (!status.ok()) {
            (void)m_store.removeAccount(account.accountId);
            return status;
        }
        if (outPendingEmailVerification) {
            *outPendingEmailVerification = true;
        }
    } else {
        status = m_store.setAccountEmailVerifiedAt(account.accountId, swHttpAuthDetail::currentIsoTimestamp());
        if (!status.ok()) {
            (void)m_store.removeAccount(account.accountId);
            if (outError) {
                *outError = status.message();
            }
            return status;
        }
    }

    status = m_store.getAccountById(account.accountId, &account);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    if (outAccount) {
        *outAccount = account;
    }
    if (outSubject) {
        *outSubject = subjectView;
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwHttpAuthService::login(const SwString& email,
                                           const SwString& password,
                                           const SwString& userAgent,
                                           bool viaTls,
                                           SwString* outRawToken,
                                           SwHttpAuthIdentity* outIdentity,
                                           SwString* outPasswordResetToken,
                                           SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (outRawToken) {
        outRawToken->clear();
    }
    if (outIdentity) {
        *outIdentity = SwHttpAuthIdentity();
    }
    if (outPasswordResetToken) {
        outPasswordResetToken->clear();
    }
    if (!isStarted()) {
        return SwDbStatus(SwDbStatus::NotOpen, "Auth service not started");
    }

    SwHttpAuthAccount account;
    SwDbStatus status = m_store.getAccountByEmail(email, &account);
    if (!status.ok()) {
        if (outError) {
            *outError = "Invalid credentials";
        }
        return SwDbStatus(SwDbStatus::NotFound, "Invalid credentials");
    }
    if (account.suspended) {
        if (outError) {
            *outError = "Account suspended";
        }
        return SwDbStatus(SwDbStatus::Busy, "Account suspended");
    }
    if (account.passwordResetRequired) {
        if (!password.trimmed().isEmpty()) {
            if (outError) {
                *outError = "Invalid credentials";
            }
            return SwDbStatus(SwDbStatus::NotFound, "Invalid credentials");
        }

        SwString resetToken;
        const SwDbStatus challengeStatus =
            m_store.createChallenge("reset_password", account.accountId, m_config.resetCodeTtlMs, &resetToken, nullptr);
        if (!challengeStatus.ok()) {
            if (outError) {
                *outError = challengeStatus.message();
            }
            return challengeStatus;
        }

        if (outPasswordResetToken) {
            *outPasswordResetToken = resetToken;
        }
        if (outError) {
            *outError = "Password reset required";
        }
        return SwDbStatus(SwDbStatus::Busy, "Password reset required");
    }
    if (!m_store.verifyPassword(account, password)) {
        if (outError) {
            *outError = "Invalid credentials";
        }
        return SwDbStatus(SwDbStatus::NotFound, "Invalid credentials");
    }
    if (!swHttpAuthDetail::isModernPasswordHash(account.passwordHash)) {
        const SwDbStatus upgradeStatus = m_store.setAccountPassword(account.accountId, password);
        if (!upgradeStatus.ok()) {
            swWarningM(kSwLogCategory_SwHttpAuth)
                << "unable to upgrade legacy password hash for" << account.email << upgradeStatus.message();
        } else {
            SwHttpAuthAccount refreshedAccount;
            if (m_store.getAccountById(account.accountId, &refreshedAccount).ok()) {
                account = refreshedAccount;
            }
        }
    }
    if (m_config.requireVerifiedEmailForLogin && account.emailVerifiedAt.trimmed().isEmpty()) {
        if (outError) {
            *outError = "Email not verified";
        }
        return SwDbStatus(SwDbStatus::Busy, "Email not verified");
    }

    SwHttpAuthSession session;
    status = m_store.createSession(account.accountId,
                                   userAgent,
                                   viaTls,
                                   m_config.sessionTtlMs,
                                   outRawToken,
                                   &session);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwHttpAuthIdentity identity;
    identity.authenticated = true;
    identity.emailVerified = !account.emailVerifiedAt.trimmed().isEmpty();
    identity.account = account;
    identity.session = session;
    if (!account.subjectId.trimmed().isEmpty()) {
        SwString subjectError;
        (void)loadSubjectView(account.subjectId, &identity.subject, &subjectError);
    }
    if (outIdentity) {
        *outIdentity = identity;
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwHttpAuthService::logout(const SwString& rawToken) {
    const SwString token = rawToken.trimmed();
    if (token.isEmpty()) {
        return SwDbStatus::success();
    }
    const SwDbStatus status = m_store.removeSessionByToken(token);
    if (status.code() == SwDbStatus::NotFound) {
        return SwDbStatus::success();
    }
    return status;
}

inline SwDbStatus SwHttpAuthService::requestEmailVerification(const SwString& email, SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (!isStarted()) {
        return SwDbStatus(SwDbStatus::NotOpen, "Auth service not started");
    }
    if (!m_config.verificationRequired) {
        return SwDbStatus::success();
    }

    SwHttpAuthAccount account;
    const SwDbStatus status = m_store.getAccountByEmail(email, &account);
    if (!status.ok()) {
        return SwDbStatus::success();
    }
    if (account.suspended || !account.emailVerifiedAt.trimmed().isEmpty()) {
        return SwDbStatus::success();
    }
    if (!throttleAllows_("verify_email:" + account.email)) {
        return SwDbStatus::success();
    }
    return sendChallengeForAccount_(account, "verify_email", true, outError);
}

inline SwDbStatus SwHttpAuthService::verifyEmail(const SwString& code,
                                                 const SwString& token,
                                                 SwHttpAuthAccount* outAccount,
                                                 SwJsonValue* outSubject,
                                                 SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (outSubject) {
        *outSubject = SwJsonValue();
    }

    SwHttpAuthChallenge challenge;
    SwDbStatus status = lookupChallenge_("verify_email", code, token, &challenge);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }
    status = validateChallenge_(challenge);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwHttpAuthAccount account;
    status = m_store.getAccountById(challenge.accountId, &account);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    if (account.emailVerifiedAt.trimmed().isEmpty()) {
        status = m_store.setAccountEmailVerifiedAt(account.accountId, swHttpAuthDetail::currentIsoTimestamp());
        if (!status.ok()) {
            if (outError) {
                *outError = status.message();
            }
            return status;
        }
    }

    status = m_store.consumeChallenge(challenge.challengeId);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    status = m_store.getAccountById(account.accountId, &account);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwJsonValue subjectView;
    if (!account.subjectId.trimmed().isEmpty()) {
        SwString subjectError;
        (void)loadSubjectView(account.subjectId, &subjectView, &subjectError);
    }

    if (m_hooks.onEmailVerified) {
        m_hooks.onEmailVerified(account, subjectView);
    }
    if (outAccount) {
        *outAccount = account;
    }
    if (outSubject) {
        *outSubject = subjectView;
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwHttpAuthService::requestPasswordReset(const SwString& email, SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (!isStarted()) {
        return SwDbStatus(SwDbStatus::NotOpen, "Auth service not started");
    }

    SwHttpAuthAccount account;
    const SwDbStatus status = m_store.getAccountByEmail(email, &account);
    if (!status.ok() || account.suspended) {
        return SwDbStatus::success();
    }
    if (!throttleAllows_("reset_password:" + account.email)) {
        return SwDbStatus::success();
    }
    return sendChallengeForAccount_(account, "reset_password", true, outError);
}

inline SwDbStatus SwHttpAuthService::resetPassword(const SwString& code,
                                                   const SwString& token,
                                                   const SwString& newPassword,
                                                   SwString* outError) {
    if (outError) {
        outError->clear();
    }

    SwHttpAuthChallenge challenge;
    SwDbStatus status = lookupChallenge_("reset_password", code, token, &challenge);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }
    status = validateChallenge_(challenge);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwHttpAuthAccount account;
    status = m_store.getAccountById(challenge.accountId, &account);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    status = m_store.setAccountPassword(account.accountId, newPassword);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    status = m_store.consumeChallenge(challenge.challengeId);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    status = m_store.removeSessionsForAccount(account.accountId);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    status = m_store.getAccountById(account.accountId, &account);
    if (status.ok() && m_hooks.onPasswordChanged) {
        SwJsonValue subjectView;
        SwString subjectError;
        (void)loadSubjectView(account.subjectId, &subjectView, &subjectError);
        m_hooks.onPasswordChanged(account, subjectView);
    }
    return status.ok() ? SwDbStatus::success() : status;
}

inline SwDbStatus SwHttpAuthService::changePassword(const SwString& rawToken,
                                                    const SwString& currentPassword,
                                                    const SwString& newPassword,
                                                    SwHttpAuthIdentity* outIdentity,
                                                    SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (outIdentity) {
        *outIdentity = SwHttpAuthIdentity();
    }

    SwHttpAuthIdentity identity;
    if (!resolveIdentityFromToken(rawToken, &identity, outError)) {
        return SwDbStatus(SwDbStatus::NotFound, outError ? *outError : SwString("Invalid session"));
    }
    if (!m_store.verifyPassword(identity.account, currentPassword)) {
        if (outError) {
            *outError = "Invalid credentials";
        }
        return SwDbStatus(SwDbStatus::NotFound, "Invalid credentials");
    }

    SwDbStatus status = m_store.setAccountPassword(identity.account.accountId, newPassword);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    status = m_store.removeSessionsForAccount(identity.account.accountId, identity.session.sessionId);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwHttpAuthAccount updatedAccount;
    status = m_store.getAccountById(identity.account.accountId, &updatedAccount);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    SwHttpAuthIdentity refreshed;
    refreshed.authenticated = true;
    refreshed.emailVerified = !updatedAccount.emailVerifiedAt.trimmed().isEmpty();
    refreshed.account = updatedAccount;
    refreshed.session = identity.session;
    if (!updatedAccount.subjectId.trimmed().isEmpty()) {
        SwString subjectError;
        (void)loadSubjectView(updatedAccount.subjectId, &refreshed.subject, &subjectError);
    }
    if (outIdentity) {
        *outIdentity = refreshed;
    }
    if (m_hooks.onPasswordChanged) {
        m_hooks.onPasswordChanged(updatedAccount, refreshed.subject);
    }
    return SwDbStatus::success();
}

inline bool SwHttpAuthService::loadSubjectView(const SwString& subjectId,
                                               SwJsonValue* outSubject,
                                               SwString* outError) const {
    if (outError) {
        outError->clear();
    }
    if (outSubject) {
        *outSubject = SwJsonValue();
    }
    if (!m_hooks.loadSubject || subjectId.trimmed().isEmpty()) {
        return false;
    }
    SwJsonValue subject;
    SwString error;
    if (!m_hooks.loadSubject(subjectId.trimmed(), subject, error)) {
        if (outError) {
            *outError = error.isEmpty() ? SwString("Unable to load subject") : error;
        }
        return false;
    }
    if (outSubject) {
        *outSubject = subject;
    }
    return true;
}

inline bool SwHttpAuthService::validateConfig_(SwString& outError) const {
    outError.clear();
    if (m_config.routePrefix.trimmed().isEmpty()) {
        outError = "Auth routePrefix is required";
        return false;
    }
    if (m_config.sessionCookieName.trimmed().isEmpty()) {
        outError = "Auth sessionCookieName is required";
        return false;
    }
    if (m_config.passwordMinLength < 1) {
        outError = "Auth passwordMinLength must be >= 1";
        return false;
    }
    return validateMailConfig_(outError);
}

inline bool SwHttpAuthService::validateMailConfig_(SwString& outError) const {
    outError.clear();
    const bool verificationEnabled = m_config.verificationRequired;
    const bool resetEnabled = true;
    if (!verificationEnabled && !resetEnabled) {
        return true;
    }
    if (m_config.mail.fromAddress.trimmed().isEmpty()) {
        outError = "Auth mail.fromAddress is required";
        return false;
    }
    if (verificationEnabled) {
        if (!validateMailTemplate_(m_config.mail.verificationTemplate)) {
            outError = "Auth verification template is required";
            return false;
        }
        if (m_config.mail.verificationUrlTemplate.trimmed().isEmpty() &&
            m_config.publicBaseUrl.trimmed().isEmpty()) {
            outError = "Auth verification URL template or publicBaseUrl is required";
            return false;
        }
    }
    if (!validateMailTemplate_(m_config.mail.resetPasswordTemplate)) {
        outError = "Auth reset password template is required";
        return false;
    }
    if (m_config.mail.resetPasswordUrlTemplate.trimmed().isEmpty() &&
        m_config.publicBaseUrl.trimmed().isEmpty()) {
        outError = "Auth reset password URL template or publicBaseUrl is required";
        return false;
    }
    if (!hasInternalMailDelivery_() && !m_hooks.deliverMail) {
        outError = "Auth mail delivery requires SwMailService or deliverMail hook";
        return false;
    }
    return true;
}

inline bool SwHttpAuthService::validateMailTemplate_(const SwHttpAuthMailTemplate& mailTemplate) {
    return !mailTemplate.subject.trimmed().isEmpty() &&
           (!mailTemplate.textBody.trimmed().isEmpty() || !mailTemplate.htmlBody.trimmed().isEmpty());
}

inline bool SwHttpAuthService::hasInternalMailDelivery_() const {
    return m_mailService && m_mailService->isStarted() && !m_config.mail.fromAddress.trimmed().isEmpty();
}

inline bool SwHttpAuthService::throttleAllows_(const SwString& key) {
    const long long now = swHttpAuthDetail::currentEpochMs();
    const long long windowMs = 10ll * 60ll * 1000ll;
    const int maxAttempts = 5;

    SwMutexLocker locker(&m_mutex);
    ThrottleState_ state = m_throttle.value(key, ThrottleState_());
    if (state.windowStartMs <= 0 || now - state.windowStartMs >= windowMs) {
        state.windowStartMs = now;
        state.failures = 0;
    }
    if (state.failures >= maxAttempts) {
        m_throttle[key] = state;
        return false;
    }
    ++state.failures;
    m_throttle[key] = state;
    return true;
}

inline SwDbStatus SwHttpAuthService::lookupChallenge_(const SwString& purpose,
                                                      const SwString& code,
                                                      const SwString& token,
                                                      SwHttpAuthChallenge* outChallenge) {
    if (!outChallenge) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Missing output challenge");
    }
    const SwString normalizedPurpose = purpose.trimmed().toLower();
    if (!code.trimmed().isEmpty()) {
        return m_store.getChallengeByCode(normalizedPurpose, code.trimmed().toUpper(), outChallenge);
    }
    if (token.trimmed().isEmpty()) {
        return SwDbStatus(SwDbStatus::InvalidArgument, "Challenge code or token required");
    }
    SwDbStatus status = m_store.getChallengeByToken(token.trimmed(), outChallenge);
    if (!status.ok()) {
        return status;
    }
    if (outChallenge->purpose != normalizedPurpose) {
        return SwDbStatus(SwDbStatus::NotFound, "Challenge not found");
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwHttpAuthService::validateChallenge_(const SwHttpAuthChallenge& challenge) {
    if (!challenge.consumedAt.trimmed().isEmpty()) {
        return SwDbStatus(SwDbStatus::Busy, "Challenge already consumed");
    }
    if (challenge.expiresAtMs > 0 && challenge.expiresAtMs <= swHttpAuthDetail::currentEpochMs()) {
        return SwDbStatus(SwDbStatus::Busy, "Challenge expired");
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwHttpAuthService::sendChallengeForAccount_(const SwHttpAuthAccount& account,
                                                              const SwString& purpose,
                                                              bool swallowDeliveryErrors,
                                                              SwString* outError) {
    if (outError) {
        outError->clear();
    }
    const SwString normalizedPurpose = purpose.trimmed().toLower();
    const unsigned long long ttlMs =
        normalizedPurpose == "verify_email" ? m_config.verificationCodeTtlMs : m_config.resetCodeTtlMs;

    SwString rawToken;
    SwHttpAuthChallenge challenge;
    SwDbStatus status = m_store.createChallenge(normalizedPurpose, account.accountId, ttlMs, &rawToken, &challenge);
    if (!status.ok()) {
        if (outError) {
            *outError = status.message();
        }
        return status;
    }

    const SwHttpAuthMailTemplate& mailTemplate =
        normalizedPurpose == "verify_email" ? m_config.mail.verificationTemplate : m_config.mail.resetPasswordTemplate;
    const SwString configuredTemplate =
        normalizedPurpose == "verify_email" ? m_config.mail.verificationUrlTemplate : m_config.mail.resetPasswordUrlTemplate;
    const SwString url = buildChallengeUrl_(normalizedPurpose, rawToken, configuredTemplate);
    const SwHttpAuthRenderedTemplate rendered =
        SwHttpAuthTemplateRenderer::render(mailTemplate, challenge.code, url);

    SwHttpAuthOutgoingMail mail;
    mail.purpose = normalizedPurpose;
    mail.accountId = account.accountId;
    mail.email = account.email;
    mail.fromAddress = m_config.mail.fromAddress.trimmed();
    mail.to.append(account.email);
    mail.subject = rendered.subject;
    mail.textBody = rendered.textBody;
    mail.htmlBody = rendered.htmlBody;
    mail.code = challenge.code;
    mail.url = url;

    SwString mailError;
    if (!sendMail_(mail, &mailError)) {
        swWarningM(kSwLogCategory_SwHttpAuth) << "auth mail delivery failed" << normalizedPurpose << account.email
                                              << mailError;
        if (!swallowDeliveryErrors) {
            (void)m_store.removeChallenge(challenge.challengeId);
            if (outError) {
                *outError = mailError;
            }
            return SwDbStatus(SwDbStatus::IoError,
                              mailError.isEmpty() ? SwString("Unable to deliver auth email") : mailError);
        }
        if (outError) {
            *outError = mailError;
        }
    }
    return SwDbStatus::success();
}

inline SwString SwHttpAuthService::buildChallengeUrl_(const SwString& purpose,
                                                      const SwString& rawToken,
                                                      const SwString& configuredTemplate) const {
    if (!configuredTemplate.trimmed().isEmpty()) {
        return SwHttpAuthTemplateRenderer::renderUrl(configuredTemplate.trimmed(), rawToken);
    }
    const SwString baseUrl = swHttpAuthDetail::normalizeBaseUrl(m_config.publicBaseUrl);
    if (baseUrl.isEmpty()) {
        return SwString();
    }
    if (purpose.trimmed().toLower() == "verify_email") {
        return baseUrl + m_config.routePrefix + "/email/verify?token=" + rawToken;
    }
    return baseUrl + m_config.routePrefix + "/password/reset?token=" + rawToken;
}

inline bool SwHttpAuthService::sendMail_(const SwHttpAuthOutgoingMail& mail, SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (hasInternalMailDelivery_()) {
        SwString mailError;
        if (sendViaMailService_(mail, &mailError)) {
            return true;
        }
        if (m_hooks.deliverMail) {
            SwString hookError;
            if (m_hooks.deliverMail(mail, hookError)) {
                return true;
            }
            if (outError) {
                *outError = hookError.isEmpty() ? mailError : hookError;
            }
            return false;
        }
        if (outError) {
            *outError = mailError;
        }
        return false;
    }
    if (!m_hooks.deliverMail) {
        if (outError) {
            *outError = "No auth mail delivery transport configured";
        }
        return false;
    }
    SwString hookError;
    const bool ok = m_hooks.deliverMail(mail, hookError);
    if (!ok && outError) {
        *outError = hookError.isEmpty() ? SwString("Unable to deliver auth email") : hookError;
    }
    return ok;
}

inline bool SwHttpAuthService::sendViaMailService_(const SwHttpAuthOutgoingMail& mail, SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (!m_mailService) {
        if (outError) {
            *outError = "Mail service unavailable";
        }
        return false;
    }

    const SwMailConfig mailConfig = m_mailService->config();
    const SwByteArray message = buildMimeMessage_(mailConfig, mail);
    const SwString localDomain = swMailDetail::normalizeDomain(mailConfig.domain);

    SwList<SwString> localRecipients;
    SwList<SwString> remoteRecipients;
    for (std::size_t i = 0; i < mail.to.size(); ++i) {
        const SwString canonical = swMailDetail::canonicalAddress(mail.to[i]);
        SwString localPart;
        SwString domain;
        if (swMailDetail::splitAddress(canonical, localPart, domain) &&
            swMailDetail::normalizeDomain(domain) == localDomain) {
            localRecipients.append(canonical);
        } else {
            remoteRecipients.append(canonical);
        }
    }

    if (!localRecipients.isEmpty()) {
        SwString localError;
        const SwDbStatus status = m_mailService->deliverLocalMessage(mail.fromAddress, localRecipients, message, nullptr, &localError);
        if (!status.ok()) {
            if (outError) {
                *outError = localError.isEmpty() ? status.message() : localError;
            }
            return false;
        }
    }

    if (!remoteRecipients.isEmpty()) {
        SwString remoteError;
        const SwDbStatus status = m_mailService->enqueueRemoteMessage(mail.fromAddress, remoteRecipients, message, &remoteError);
        if (!status.ok()) {
            if (outError) {
                *outError = remoteError.isEmpty() ? status.message() : remoteError;
            }
            return false;
        }
    }
    return true;
}

inline SwByteArray SwHttpAuthService::buildMimeMessage_(const SwMailConfig& mailConfig,
                                                        const SwHttpAuthOutgoingMail& mail) {
    const SwString boundary = "swauth-" + swHttpAuthDetail::randomHexToken(12);
    const SwString subject = swHttpAuthServiceDetail::safeHeaderText_(mail.subject);

    SwString raw;
    raw += "Subject: " + subject + "\r\n";
    raw += "MIME-Version: 1.0\r\n";
    if (mail.htmlBody.trimmed().isEmpty()) {
        raw += "Content-Type: text/plain; charset=utf-8\r\n";
        raw += "Content-Transfer-Encoding: 8bit\r\n\r\n";
        raw += mail.textBody;
        if (!mail.textBody.endsWith("\r\n")) {
            raw += "\r\n";
        }
    } else {
        raw += "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n\r\n";
        raw += "--" + boundary + "\r\n";
        raw += "Content-Type: text/plain; charset=utf-8\r\n";
        raw += "Content-Transfer-Encoding: 8bit\r\n\r\n";
        raw += mail.textBody;
        if (!mail.textBody.endsWith("\r\n")) {
            raw += "\r\n";
        }
        raw += "--" + boundary + "\r\n";
        raw += "Content-Type: text/html; charset=utf-8\r\n";
        raw += "Content-Transfer-Encoding: 8bit\r\n\r\n";
        raw += mail.htmlBody;
        if (!mail.htmlBody.endsWith("\r\n")) {
            raw += "\r\n";
        }
        raw += "--" + boundary + "--\r\n";
    }

    return swMailDetail::ensureMessageEnvelopeHeaders(mailConfig,
                                                      SwByteArray(raw.toStdString()),
                                                      mail.fromAddress,
                                                      mail.to);
}
