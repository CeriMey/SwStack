#pragma once

#include "SwRemoteObject.h"

#include <functional>

class SwLaunchRemoteControl : public SwRemoteObject {
    SW_OBJECT(SwLaunchRemoteControl, SwRemoteObject)

public:
    using ShutdownHandler = std::function<bool()>;

    SwLaunchRemoteControl(const SwString& sysName,
                          const SwString& nameSpace,
                          const SwString& objectName,
                          ShutdownHandler shutdownHandler,
                          SwObject* parent = nullptr);

    bool requestShutdown();

private:
    ShutdownHandler shutdownHandler_;
};
