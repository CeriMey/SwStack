#include "SwLaunchRemoteControl.h"

#include <utility>

SwLaunchRemoteControl::SwLaunchRemoteControl(const SwString& sysName,
                                             const SwString& nameSpace,
                                             const SwString& objectName,
                                             ShutdownHandler shutdownHandler,
                                             SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent)
    , shutdownHandler_(std::move(shutdownHandler)) {
    ipcExposeRpc(requestShutdown, this, &SwLaunchRemoteControl::requestShutdown);
}

bool SwLaunchRemoteControl::requestShutdown() {
    return shutdownHandler_ ? shutdownHandler_() : false;
}
