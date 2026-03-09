#include "SwBuildController.h"

#include "SwBuildDependencyResolver.h"
#include "SwBuildInstaller.h"
#include "SwBuildProjectScanner.h"
#include "SwBuildUtils.h"

#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwProcess.h"

#include <iostream>

SwBuildController::SwBuildController(const SwBuildOptions& options, SwObject* parent)
    : SwObject(parent), options_(options) {}

void SwBuildController::start() {
    SwString err;
    if (!prepareWorkspace_(err)) {
        failAndExit_(3, err);
        return;
    }

    if (!discoverProjects_(err)) {
        failAndExit_(4, err);
        return;
    }

    if (projects_.isEmpty()) {
        swWarning() << "[SwBuild] no CMakeLists.txt found under scan root:" << options_.scanDirAbs();
        SwCoreApplication::instance()->exit(0);
        return;
    }

    currentIndex_ = 0;
    stage_ = options_.buildOnly() ? Stage::Build : Stage::Configure;
    startCurrentStage_();
}

bool SwBuildController::prepareWorkspace_(SwString& errOut) {
    errOut.clear();

    if (options_.rootDirAbs().isEmpty()) {
        errOut = "rootDir is empty";
        return false;
    }

    if (!SwDir::exists(options_.rootDirAbs())) {
        errOut = SwString("rootDir does not exist: ") + options_.rootDirAbs();
        return false;
    }

    if (options_.clean()) {
        if (SwDir::exists(options_.buildRootDir())) {
            if (!SwDir::removeRecursively(options_.buildRootDir())) {
                errOut = SwString("failed to clean build dir: ") + options_.buildRootDir();
                return false;
            }
        }
        if (SwDir::exists(options_.logRootDir())) {
            if (!SwDir::removeRecursively(options_.logRootDir())) {
                errOut = SwString("failed to clean log dir: ") + options_.logRootDir();
                return false;
            }
        }
        if (SwDir::exists(options_.installRootDir())) {
            if (!SwDir::removeRecursively(options_.installRootDir())) {
                errOut = SwString("failed to clean install dir: ") + options_.installRootDir();
                return false;
            }
        }
    }

    if (!SwDir::mkpathAbsolute(options_.buildRootDir(), false)) {
        errOut = SwString("failed to create build dir: ") + options_.buildRootDir();
        return false;
    }
    if (!SwDir::mkpathAbsolute(options_.logRootDir(), false)) {
        errOut = SwString("failed to create log dir: ") + options_.logRootDir();
        return false;
    }
    if (!SwDir::mkpathAbsolute(options_.installRootDir(), false)) {
        errOut = SwString("failed to create install dir: ") + options_.installRootDir();
        return false;
    }

    // Pre-create standard install layout.
    (void)SwDir::mkpathAbsolute(swbuild::joinPath(options_.installRootDir(), "bin"), false);
    (void)SwDir::mkpathAbsolute(swbuild::joinPath(options_.installRootDir(), "lib"), false);
    (void)SwDir::mkpathAbsolute(swbuild::joinPath(options_.installRootDir(), "plugins"), false);
    return true;
}

bool SwBuildController::discoverProjects_(SwString& errOut) {
    errOut.clear();

    SwBuildProjectScanner scanner;
    projects_ = scanner.scan(options_, errOut);
    if (!errOut.isEmpty()) return false;

    SwBuildDependencyResolver resolver;
    if (!resolver.sort(projects_, errOut)) {
        return false;
    }

    return true;
}

void SwBuildController::startCurrentStage_() {
    if (currentIndex_ >= projects_.size()) {
        swDebug() << "[SwBuild] done";
        SwCoreApplication::instance()->exit(0);
        return;
    }

    SwString err;
    const SwBuildProject project = projects_[static_cast<size_t>(currentIndex_)];

    if (stage_ == Stage::Configure) {
        if (options_.dryRun()) {
            swDebug() << "[SwBuild][dry_run] configure:" << project.sourceDirAbs();
            if (options_.configureOnly()) {
                ++currentIndex_;
                stage_ = Stage::Configure;
            } else {
                stage_ = Stage::Build;
            }
            startCurrentStage_();
            return;
        }

        if (!startConfigure_(project, err)) {
            failAndExit_(5, err);
            return;
        }
        return;
    }

    if (stage_ == Stage::Build) {
        if (options_.dryRun()) {
            swDebug() << "[SwBuild][dry_run] build:" << project.buildDirAbs();
            if (options_.installEnabled()) {
                swDebug() << "[SwBuild][dry_run] install/copy from:" << project.buildDirAbs();
            }
            ++currentIndex_;
            stage_ = options_.buildOnly() ? Stage::Build : Stage::Configure;
            startCurrentStage_();
            return;
        }

        if (!startBuild_(project, err)) {
            failAndExit_(6, err);
            return;
        }
        return;
    }
}

bool SwBuildController::startConfigure_(const SwBuildProject& project, SwString& errOut) {
    errOut.clear();

    if (options_.clean()) {
        if (SwDir::exists(project.buildDirAbs())) {
            if (!SwDir::removeRecursively(project.buildDirAbs())) {
                errOut = SwString("failed to clean build dir: ") + project.buildDirAbs();
                return false;
            }
        }
    }

    if (!SwDir::mkpathAbsolute(project.buildDirAbs(), false)) {
        errOut = SwString("failed to create build dir: ") + project.buildDirAbs();
        return false;
    }

    const SwString logDir = swbuild::joinPath(options_.logRootDir(), project.relativeSourceDir());
    if (!SwDir::mkpathAbsolute(logDir, false)) {
        errOut = SwString("failed to create log dir: ") + logDir;
        return false;
    }

    const SwString logPath = swbuild::joinPath(logDir, "configure.log");

    SwStringList args;
    args.append("-S");
    args.append(project.sourceDirAbs());
    args.append("-B");
    args.append(project.buildDirAbs());
    args.append(SwString("-DCMAKE_BUILD_TYPE=") + options_.buildType());
    args.append(SwString("-DCMAKE_INSTALL_PREFIX=") + options_.installRootDir());
    args.append(SwString("-DCMAKE_PREFIX_PATH=") + options_.installRootDir());

    if (!options_.generator().isEmpty()) {
        args.append("-G");
        args.append(options_.generator());
    }

    return startProcess_(SwString("configure ") + project.relativeSourceDir(),
                         options_.cmakeBin(),
                         args,
                         options_.rootDirAbs(),
                         logPath,
                         errOut);
}

bool SwBuildController::startBuild_(const SwBuildProject& project, SwString& errOut) {
    errOut.clear();

    const SwString logDir = swbuild::joinPath(options_.logRootDir(), project.relativeSourceDir());
    if (!SwDir::mkpathAbsolute(logDir, false)) {
        errOut = SwString("failed to create log dir: ") + logDir;
        return false;
    }

    const SwString logPath = swbuild::joinPath(logDir, "build.log");

    SwStringList args;
    args.append("--build");
    args.append(project.buildDirAbs());
    args.append("--config");
    args.append(options_.buildType());

    return startProcess_(SwString("build ") + project.relativeSourceDir(),
                         options_.cmakeBin(),
                         args,
                         options_.rootDirAbs(),
                         logPath,
                         errOut);
}

bool SwBuildController::startProcess_(const SwString& label,
                                      const SwString& program,
                                      const SwStringList& args,
                                      const SwString& workingDir,
                                      const SwString& logFilePath,
                                      SwString& errOut) {
    errOut.clear();

    if (process_ || logFile_) {
        errOut = "internal error: process already running";
        return false;
    }

    currentLabel_ = label;
    currentLogPath_ = logFilePath;

    // SwProcess quotes/escapes args on Windows; keep args here unquoted.
    const SwString programRun = program;
    const SwStringList argsRun = args;

    logFile_ = new SwFile(logFilePath, this);
    if (!logFile_->open(SwFile::Write)) {
        SwObject::safeDelete(logFile_);
        errOut = SwString("failed to open log file: ") + logFilePath;
        return false;
    }

    {
        SwString header;
        header += SwString("[SwBuild] ") + label + "\n";
        header += SwString("[SwBuild] program: ") + swbuild::quoteArgIfNeeded(programRun) + "\n";
        header += SwString("[SwBuild] workingDir: ") + workingDir + "\n";
        header += SwString("[SwBuild] args:");
        for (int i = 0; i < argsRun.size(); ++i) {
            header += SwString(" ") + swbuild::quoteArgIfNeeded(argsRun[i]);
        }
        header += "\n\n";
        (void)logFile_->write(header);
    }

    process_ = new SwProcess(this);

    SwObject::connect(process_, SIGNAL(readyReadStdOut), std::function<void()>([this]() {
        handleProcessStdOut_();
    }));

    SwObject::connect(process_, SIGNAL(readyReadStdErr), std::function<void()>([this]() {
        handleProcessStdErr_();
    }));

    SwObject::connect(process_, SIGNAL(processTerminated), std::function<void(int)>([this](int code) {
        handleProcessTerminated_(code);
    }));

    if (!process_->start(programRun, argsRun, ProcessFlags::NoFlag, workingDir)) {
        SwObject::safeDelete(process_);
        logFile_->close();
        SwObject::safeDelete(logFile_);
        errOut = SwString("failed to start process: ") + programRun;
        return false;
    }

    swDebug() << "[SwBuild] running:" << label;
    return true;
}

void SwBuildController::handleProcessStdOut_() {
    if (!process_ || !logFile_) return;
    for (;;) {
        const SwString out = process_->read();
        if (out.isEmpty()) break;
        (void)logFile_->write(out);
        if (options_.verbose()) {
            std::cout << out.toStdString();
            std::cout.flush();
        }
    }
}

void SwBuildController::handleProcessStdErr_() {
    if (!process_ || !logFile_) return;
    for (;;) {
        const SwString err = process_->readStdErr();
        if (err.isEmpty()) break;
        (void)logFile_->write(err);
        if (options_.verbose()) {
            std::cerr << err.toStdString();
            std::cerr.flush();
        }
    }
}

void SwBuildController::handleProcessTerminated_(int exitCode) {
    if (!process_ || !logFile_) {
        failAndExit_(7, "internal error: processTerminated without active process");
        return;
    }

    // Flush remaining buffers.
    handleProcessStdOut_();
    handleProcessStdErr_();

    const SwString doneLine = SwString("\n[SwBuild] exitCode=") + SwString::number(exitCode) + "\n";
    (void)logFile_->write(doneLine);

    logFile_->close();
    SwFile* logFileToDelete = logFile_;
    logFile_ = nullptr;

    SwProcess* processToDelete = process_;
    process_ = nullptr;

    // Ensure the process is fully closed before moving on. Defer deletion to avoid
    // destroying objects from inside timer callbacks / signal dispatch.
    processToDelete->close();
    processToDelete->deleteLater();
    logFileToDelete->deleteLater();

    if (exitCode != 0) {
        failAndExit_(exitCode, SwString("command failed (see log): ") + currentLogPath_);
        return;
    }

    const SwBuildProject project = projects_[static_cast<size_t>(currentIndex_)];

    if (stage_ == Stage::Configure) {
        if (options_.configureOnly()) {
            ++currentIndex_;
            stage_ = Stage::Configure;
        } else {
            stage_ = Stage::Build;
        }
        SwCoreApplication::instance()->postEvent([this]() { startCurrentStage_(); });
        return;
    }

    if (stage_ == Stage::Build) {
        SwString err;
        if (!finalizeBuildProject_(project, err)) {
            failAndExit_(8, err);
            return;
        }

        ++currentIndex_;
        stage_ = options_.buildOnly() ? Stage::Build : Stage::Configure;
        SwCoreApplication::instance()->postEvent([this]() { startCurrentStage_(); });
        return;
    }
}

bool SwBuildController::finalizeBuildProject_(const SwBuildProject& project, SwString& errOut) {
    errOut.clear();
    if (!options_.installEnabled()) return true;

    SwBuildInstaller installer;
    return installer.install(project, options_, errOut);
}

void SwBuildController::failAndExit_(int exitCode, const SwString& message) {
    if (!message.isEmpty()) {
        swError() << "[SwBuild]" << message;
    }
    SwCoreApplication::instance()->exit(exitCode);
}
