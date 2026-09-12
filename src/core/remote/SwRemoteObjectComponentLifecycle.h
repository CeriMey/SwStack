#pragma once
#include "SwRemoteObject.h"
#include <stdexcept>

// Optional lifecycle for registry-created objects. Constructors expose identity;
// configuration must precede business work. stop is idempotent and drains work.
// A stopped instance is destroyed; restart creates a fresh instance.
class SwRemoteObjectComponentLifecycle {
public:
    virtual ~SwRemoteObjectComponentLifecycle() = default;
    virtual void configureComponent(const SwJsonObject& params) = 0;
    virtual void startComponent() = 0;
    virtual void stopComponent() noexcept = 0;
    virtual bool componentReady() const noexcept = 0;
    static void applyParameters(SwRemoteObject& object, const SwJsonObject& params) {
        for (const auto& value : params.data())
            if (!object.setConfigValue(SwString(value.first), value.second, false, true))
                throw std::invalid_argument("Component rejected configuration: " + value.first);
    }
};
