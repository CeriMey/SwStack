#include "SwGuiApplication.h"
#include "core/runtime/SwCrashHandler.h"
#include "core/types/SwDebug.h"

#include "WhatsAppWindow.h"

#include <cstdlib>

static void ensureCrashDumpsEnabled_() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "SW_CRASH_DUMPS") == 0 && value) {
        std::free(value);
        return;
    }
    _putenv_s("SW_CRASH_DUMPS", "1");
#else
    const char* value = std::getenv("SW_CRASH_DUMPS");
    if (value) {
        return;
    }
    (void)setenv("SW_CRASH_DUMPS", "1", 0);
#endif
}

static void applyFireBdArgs_(int argc, char** argv) {
    if (argc <= 1 || !argv) {
        return;
    }

    auto setEnvFromArg = [&](const char* flag, const char* envName) {
        if (!flag || !envName) {
            return;
        }
        for (int i = 1; i + 1 < argc; ++i) {
            if (!argv[i] || !argv[i + 1]) {
                continue;
            }
            if (SwString(argv[i]) != SwString(flag)) {
                continue;
            }
#if defined(_WIN32)
            _putenv_s(envName, argv[i + 1]);
#else
            (void)setenv(envName, argv[i + 1], 1);
#endif
        }
    };

    setEnvFromArg("--firebd-url", "SW_FIREBD_URL");
    setEnvFromArg("--firebd-uid", "SW_FIREBD_UID");
    setEnvFromArg("--firebd-auth", "SW_FIREBD_AUTH");
    setEnvFromArg("--firebd-poll-ms", "SW_FIREBD_POLL_MS");
    setEnvFromArg("--profile", "SW_WA_PROFILE");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    applyFireBdArgs_(argc, argv);

    ensureCrashDumpsEnabled_();
    SwCrashHandler::install("WhatsApp");

    const SwString crashDir = SwCrashHandler::crashDirectory();
    if (!crashDir.isEmpty()) {
        SwString runtimeLog = crashDir;
        if (!runtimeLog.endsWith("/") && !runtimeLog.endsWith("\\")) {
            runtimeLog += "/";
        }
        runtimeLog += "WhatsApp_runtime.log";
        SwDebug::setAppName("WhatsApp");
        SwDebug::setFilePath(runtimeLog);
        SwDebug::setFileEnabled(true);
    }

    SwGuiApplication app;

    // Snapshot mode (keeps the old dev workflow for pixel-level regression checks).
    // Usage: WhatsApp --snapshot d:/out/
    if (argc >= 3 && argv[1] && argv[2] && SwString(argv[1]) == "--snapshot") {
        WhatsAppWindow window;
        return window.saveSnapshot(SwString(argv[2])) ? 0 : 1;
    }

    SwCrashReport report;
    if (SwCrashHandler::takeLastCrashReport(report)) {
        swCError("exemples.32.whatsapp") << "[CrashReport] dir=" << report.crashDir << " log=" << report.logPath
                                        << " dump=" << report.dumpPath;
    }

    WhatsAppWindow window;
    window.show();
    return app.exec();
}
