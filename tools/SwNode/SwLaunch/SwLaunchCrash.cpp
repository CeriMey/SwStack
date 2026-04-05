#include "SwLaunchCrash.h"

#include "SwDebug.h"
#include "core/runtime/SwCrashHandler.h"

#include <csignal>
#include <cstdlib>
#include <exception>

namespace {

[[noreturn]] void triggerCrashTrap_() {
    volatile int* p = nullptr;
    *p = 0x534c;
    std::abort();
}

SwString currentExceptionMessage_() {
    try {
        const std::exception_ptr ep = std::current_exception();
        if (ep) {
            std::rethrow_exception(ep);
        }
    } catch (const std::exception& ex) {
        return SwString(ex.what());
    } catch (...) {
        return "Unknown C++ exception";
    }
    return "Unhandled termination";
}

void signalAbortHandler_(int) {
    swCError("sw.tools.swlaunch.crash") << "Abort signal raised in SwLaunch";
    triggerCrashTrap_();
}

void terminateHandler_() {
    swCError("sw.tools.swlaunch.crash") << "Unhandled termination in SwLaunch:"
                                        << currentExceptionMessage_();
    triggerCrashTrap_();
}

}

void swLaunchInstallCrashHandling() {
    SwCrashHandler::forceEnable();
    SwCrashHandler::install("SwLaunch");

    const SwString crashDir = SwCrashHandler::crashDirectory();
    if (crashDir.isEmpty()) {
        return;
    }

    SwString runtimeLog = crashDir;
    if (!runtimeLog.endsWith("/") && !runtimeLog.endsWith("\\")) {
        runtimeLog += "/";
    }
    runtimeLog += "SwLaunch_runtime.log";

    SwDebug::setAppName("SwLaunch");
    SwDebug::setFilePath(runtimeLog);
    SwDebug::setFileEnabled(true);
    std::set_terminate(&terminateHandler_);
    std::signal(SIGABRT, &signalAbortHandler_);
}

void swLaunchTriggerCrashTest() {
    triggerCrashTrap_();
}
