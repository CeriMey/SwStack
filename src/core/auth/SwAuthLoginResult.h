#pragma once

#include "SwJsonValue.h"
#include "SwString.h"
#include "auth/SwHttpAuthTypes.h"

enum class SwAuthLoginDisposition {
    Authenticated,
    MfaRequired,
    Denied
};
struct SwAuthLoginResult {
    SwAuthLoginDisposition disposition = SwAuthLoginDisposition::Denied;
    SwString error;
    SwString rawToken;
    SwHttpAuthAccount account;
    SwHttpAuthSession session;
    SwJsonValue subject;
    SwHttpAuthMfaLoginChallenge mfaChallenge;

    bool authenticated() const {
        return disposition == SwAuthLoginDisposition::Authenticated;
    }
};
