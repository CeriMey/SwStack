#include "SwBuildOptions.h"

#include "SwBuildUtils.h"

#include "SwDir.h"

#include <cstdlib>

#if defined(_WIN32)
static SwString readEnv_(const char* name) {
    if (!name) return SwString();
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) {
        if (value) {
            std::free(value);
        }
        return SwString();
    }
    SwString out(value);
    std::free(value);
    return out;
}

static SwString findCMakeOnWindows_() {
    const SwString pf = readEnv_("ProgramFiles");
    const SwString pf86 = readEnv_("ProgramFiles(x86)");

    SwList<SwString> candidates;
    if (!pf.isEmpty()) {
        candidates.append(pf + "\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe");
    }
    if (!pf86.isEmpty()) {
        candidates.append(pf86 + "\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe");
    }

    for (int i = 0; i < candidates.size(); ++i) {
        if (swFilePlatform().isFile(candidates[i])) {
            return candidates[i];
        }
    }

    return SwString();
}
#endif

SwBuildOptions::SwBuildOptions() = default;

bool SwBuildOptions::fromApplication(const SwCoreApplication& app, SwBuildOptions& out, SwString& errOut) {
    errOut.clear();
    out = SwBuildOptions();

    out.showHelp_ = app.hasArgument("help") || app.hasArgument("h");

    const SwString rootArg = app.getArgument("root", SwDir::currentPath());
    out.rootDirAbs_ = swbuild::normalizePath(swbuild::absPath(rootArg));
    if (out.rootDirAbs_.endsWith("/")) {
        out.rootDirAbs_.chop(1);
    }

    const SwString scanArg = app.getArgument("scan", "src");
    out.scanDirAbs_ = swbuild::resolveAgainst(out.rootDirAbs_, scanArg);
    out.scanDirAbs_ = swbuild::normalizePath(out.scanDirAbs_);

    out.buildType_ = app.getArgument("build_type", "Release");
    out.cmakeBin_ = app.getArgument("cmake", "cmake");
#if defined(_WIN32)
    if (out.cmakeBin_ == "cmake") {
        const SwString cmakeVs = findCMakeOnWindows_();
        if (!cmakeVs.isEmpty()) {
            out.cmakeBin_ = cmakeVs;
        }
    }
#endif
    out.generator_ = app.getArgument("generator", "");

    out.clean_ = app.hasArgument("clean");
    out.configureOnly_ = app.hasArgument("configure_only");
    out.buildOnly_ = app.hasArgument("build_only");
    out.installEnabled_ = !app.hasArgument("no_install");
    out.includeNested_ = !app.hasArgument("exclude_nested") && !app.hasArgument("no_nested");
    if (app.hasArgument("include_nested")) {
        out.includeNested_ = true;
    }
    out.dryRun_ = app.hasArgument("dry_run");
    out.verbose_ = app.hasArgument("verbose");

    if (out.configureOnly_ && out.buildOnly_) {
        errOut = "invalid options: --configure_only and --build_only are mutually exclusive";
        return false;
    }

    const SwString buildRootArg = app.getArgument("build_root", "");
    out.buildRootDir_ = buildRootArg.isEmpty() ? swbuild::joinPath(out.rootDirAbs_, "build")
                                               : swbuild::resolveAgainst(out.rootDirAbs_, buildRootArg);
    out.buildRootDir_ = swbuild::normalizePath(out.buildRootDir_);
    while (out.buildRootDir_.endsWith("/")) out.buildRootDir_.chop(1);

    out.logRootDir_ = swbuild::joinPath(out.rootDirAbs_, "log");
    out.installRootDir_ = swbuild::joinPath(out.rootDirAbs_, "install");

    return true;
}
