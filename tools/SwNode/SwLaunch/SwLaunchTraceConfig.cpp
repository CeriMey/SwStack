#include "SwLaunchTraceConfig.h"

#include "SwDebug.h"
#include "SwDir.h"

static SwString joinPath_(const SwString& base, const SwString& rel) {
    if (base.isEmpty()) return rel;
    SwString b = base;
    b.replace("\\", "/");
    SwString r = rel;
    r.replace("\\", "/");
    while (b.endsWith("/")) b.chop(1);
    while (r.startsWith("/")) r = r.mid(1);
    return b + "/" + r;
}

SwLaunchTraceConfig::SwLaunchTraceConfig(const SwString& sysName,
                                         const SwString& configRootDir,
                                         const SwString& baseDir,
                                         SwObject* parent)
    : SwRemoteObject(sysName, SwString("swlaunch"), SwString("trace"), parent), baseDir_(baseDir) {
    if (!configRootDir.isEmpty()) {
        setConfigRootDirectory(configRootDir);
    }

    const SwString defaultFilePath =
        swDirPlatform().absolutePath(joinPath_(baseDir_, SwString("log/SwLaunch.log")));

    ipcRegisterConfig(bool, toConsole_, "trace/to_console", true, [this](const bool&) { apply_(); });
    ipcRegisterConfig(bool, toFile_, "trace/to_file", false, [this](const bool&) { apply_(); });
    ipcRegisterConfig(SwString, filePath_, "trace/file_path", defaultFilePath, [this](const SwString&) { apply_(); });
    ipcRegisterConfig(SwString, filterRegex_, "trace/filter_regex", SwString(),
                      [this](const SwString&) { apply_(); });

    apply_();
}

void SwLaunchTraceConfig::apply_() {
    SwDebug::setConsoleEnabled(toConsole_);
    SwDebug::setFilePath(filePath_);
    SwDebug::setFileEnabled(toFile_ && !filePath_.isEmpty());
    SwDebug::setFilterRegex(filterRegex_);
}

