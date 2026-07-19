#pragma once

#include "SwObject.h"
#include "auth/federated/SwFederatedAuthTypes.h"

class SwFederatedLoginFlow : public SwObject {
    SW_OBJECT(SwFederatedLoginFlow, SwObject)

public:
    explicit SwFederatedLoginFlow(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

public slots:
    virtual void start() = 0;
    virtual void cancel() = 0;

signals:
    DECLARE_SIGNAL(identityVerified, const SwFederatedIdentity&)
    DECLARE_SIGNAL(failed, const SwFederatedFlowError&)
};
