#pragma once

#include "SwMap.h"
#include "SwObject.h"
#include "SwString.h"

class SwOidcClientCredential : public SwObject {
    SW_OBJECT(SwOidcClientCredential, SwObject)

public:
    explicit SwOidcClientCredential(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwOidcClientCredential() override = default;

    virtual SwString clientId() const = 0;
    virtual bool applyTokenAuthentication(SwMap<SwString, SwString>& fields,
                                          SwMap<SwString, SwString>& headers,
                                          SwString* errorOut = nullptr) const = 0;
};
