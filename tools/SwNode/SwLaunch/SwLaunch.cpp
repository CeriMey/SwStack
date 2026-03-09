#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwJsonDocument.h"
#include "SwLaunchTraceConfig.h"
#include "SwProcess.h"
#include "SwSharedMemorySignal.h"
#include "SwStandardLocation.h"
#include "SwTimer.h"

#include <cctype>
#if !defined(_WIN32)
#include <unistd.h>
#endif

static uint64_t nowMonotonicMs_() {
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetTickCount64());
#else
    struct timespec ts;
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
#endif
}

static bool loadJsonObject_(const SwCoreApplication& app,
                            SwJsonObject& out,
                            SwString& errOut,
                            SwString& configFileUsedOut) {
    errOut = SwString();
    configFileUsedOut = SwString();

    const SwString configFile = app.getArgument("config_file", SwString());
    const SwString configJson = app.getArgument("config_json", SwString());

    SwString raw;
    if (!configFile.isEmpty()) {
        configFileUsedOut = configFile;
        if (!SwFile::isFile(configFile)) {
            errOut = SwString("config_file not found: ") + configFile;
            return false;
        }
        SwFile f(configFile);
        if (!f.open(SwFile::Read)) {
            errOut = SwString("failed to open config_file: ") + configFile;
            return false;
        }
        raw = f.readAll();
    } else if (!configJson.isEmpty()) {
        raw = configJson;
    } else {
        errOut = SwString("missing --config_file or --config_json");
        return false;
    }

    SwJsonDocument doc;
    SwString parseErr;
    if (!doc.loadFromJson(raw, parseErr)) {
        errOut = SwString("json parse error: ") + parseErr;
        return false;
    }
    if (!doc.isObject()) {
        errOut = SwString("json root must be an object");
        return false;
    }

    out = doc.object();
    return true;
}

static SwJsonObject getObjectOrEmpty_(const SwJsonValue& v) {
    if (v.isObject()) {
        return v.toObject();
    }
    return SwJsonObject();
}

static SwJsonArray getArrayOrEmpty_(const SwJsonValue& v) {
    if (v.isArray()) {
        return v.toArray();
    }
    return SwJsonArray();
}

static bool isAbsPath_(const SwString& p) {
    if (p.isEmpty()) return false;
    if (p.startsWith("/") || p.startsWith("\\")) return true;
    return (p.size() >= 2 && p[1] == ':');
}

static SwString joinPath_(const SwString& base, const SwString& rel) {
    if (base.isEmpty()) return rel;
    SwString b = base;
    b.replace("\\", "/");
    SwString r = rel;
    r.replace("\\", "/");
    while (b.endsWith("/")) b = b.left(static_cast<int>(b.size()) - 1);
    while (r.startsWith("/")) r = r.mid(1);
    return b + "/" + r;
}

static SwString resolvePath_(const SwString& baseDir, const SwString& maybeRel) {
    if (maybeRel.isEmpty()) return SwString();
    const SwString combined = isAbsPath_(maybeRel) ? maybeRel : joinPath_(baseDir, maybeRel);
    return swDirPlatform().absolutePath(combined);
}

static SwString sanitizeFileLeaf_(SwString leaf) {
    leaf.replace("\\", "_");
    leaf.replace("/", "_");
    leaf.replace(":", "_");
    leaf.replace(" ", "_");
    leaf.replace("\"", "_");
    leaf.replace("'", "_");
    return leaf;
}

static bool hasLeafSuffix_(const SwString& p) {
    const std::string s = p.toStdString();
    const size_t dot = s.find_last_of('.');
    const size_t slash = s.find_last_of("/\\");
    return (dot != std::string::npos) && (slash == std::string::npos || dot > slash);
}

static bool isWindowsExePath_(const SwString& exePath) {
    return exePath.toLower().endsWith(".exe");
}

static bool isRunnableExeFile_(const SwString& absPath) {
    if (!SwFile::isFile(absPath)) return false;
#if defined(_WIN32)
    return true;
#else
    if (isWindowsExePath_(absPath)) return true; // WSL interop
    return ::access(absPath.toStdString().c_str(), X_OK) == 0;
#endif
}

static SwString resolveExecutablePath_(const SwString& baseDir, const SwString& maybeRelExe, bool preferWindowsExe) {
    if (maybeRelExe.isEmpty()) return SwString();

    const auto trySpec = [&](const SwString& spec) -> SwString {
        if (spec.isEmpty()) return SwString();
        const SwString resolved = resolvePath_(baseDir, spec);
        return isRunnableExeFile_(resolved) ? resolved : SwString();
    };

    const SwString lower = maybeRelExe.toLower();
    const bool hasSuffix = hasLeafSuffix_(maybeRelExe);

    if (hasSuffix) {
        SwString found = trySpec(maybeRelExe);
        if (!found.isEmpty()) return found;

        // If an explicit ".exe" doesn't resolve, try without it (cross-platform JSON convenience).
        if (lower.endsWith(".exe") && maybeRelExe.size() >= 4) {
            found = trySpec(maybeRelExe.left(static_cast<int>(maybeRelExe.size() - 4)));
            if (!found.isEmpty()) return found;
        }

        return SwString();
    }

    // Suffix omitted => resolve based on target preference.
    if (preferWindowsExe) {
        SwString found = trySpec(maybeRelExe + ".exe");
        if (!found.isEmpty()) return found;
        found = trySpec(maybeRelExe);
        if (!found.isEmpty()) return found;
        return SwString();
    }

    SwString found = trySpec(maybeRelExe);
    if (!found.isEmpty()) return found;
    found = trySpec(maybeRelExe + ".exe");
    if (!found.isEmpty()) return found;
    return SwString();
}

static bool isWslDrvFsPath_(const SwString& absPath) {
    SwString p = absPath;
    p.replace("\\", "/");
    if (!p.startsWith("/mnt/")) return false;
    if (p.size() < 6) return false;
    const char drive = p[5];
    if (!std::isalpha(static_cast<unsigned char>(drive))) return false;
    return (p.size() == 6 || p[6] == '/');
}

static SwString wslDrvFsToWindowsPath_(const SwString& absPath) {
    SwString p = absPath;
    p.replace("\\", "/");
    if (!isWslDrvFsPath_(p)) return absPath;

    const char drive = p[5];
    SwString rest = (p.size() > 6) ? p.mid(6) : SwString("/");
    rest.replace("/", "\\");

    SwString out(1, static_cast<char>(std::toupper(static_cast<unsigned char>(drive))));
    out += ":";
    out += rest;
    return out;
}

static SwString pathForChildProcess_(const SwString& childExePath, const SwString& hostPath) {
#if defined(_WIN32)
    (void)childExePath;
    return hostPath;
#else
    if (!isWindowsExePath_(childExePath)) return hostPath;
    if (hostPath.size() >= 2 && hostPath[1] == ':') return hostPath; // already Windows style
    return wslDrvFsToWindowsPath_(hostPath);
#endif
}

static SwString tempDirForChildConfig_(const SwString& childExePath, const SwString& baseDir) {
    SwString tempDir = SwStandardLocation::standardLocation(SwStandardLocationId::Temp);
#if !defined(_WIN32)
    if (isWindowsExePath_(childExePath) && !baseDir.isEmpty()) {
        const SwString sharedTmp = joinPath_(baseDir, "_runlogs/_tmp");
        if (SwDir::mkpathAbsolute(sharedTmp)) {
            tempDir = sharedTmp;
        }
    }
#endif
    return tempDir;
}

static SwString stripKnownLibrarySuffix_(SwString path) {
    const SwString lower = path.toLower();
    if (lower.endsWith(".dll") && path.size() >= 4) {
        return path.left(static_cast<int>(path.size() - 4));
    }
    if (lower.endsWith(".so") && path.size() >= 3) {
        return path.left(static_cast<int>(path.size() - 3));
    }
    if (lower.endsWith(".dylib") && path.size() >= 6) {
        return path.left(static_cast<int>(path.size() - 6));
    }
    return path;
}

static int executableTargetHint_(const SwString& baseDir, const SwString& exeSpec) {
    if (exeSpec.isEmpty()) return 0;
    const SwString lower = exeSpec.toLower();
    if (lower.endsWith(".exe")) return 1;
    if (hasLeafSuffix_(exeSpec)) return 0; // explicit non-.exe suffix => don't guess

    const SwString nativePath = resolvePath_(baseDir, exeSpec);
    const SwString exePath = resolvePath_(baseDir, exeSpec + ".exe");

    const bool nativeOk = isRunnableExeFile_(nativePath);
    const bool exeOk = isRunnableExeFile_(exePath);

    if (exeOk && !nativeOk) return 1;
    if (nativeOk && !exeOk) return -1;
    return 0;
}

static bool guessPreferWindowsExe_(const SwJsonObject& root, const SwString& baseDir) {
    const auto scan = [&](const SwJsonArray& arr) -> int {
        for (size_t i = 0; i < arr.size(); ++i) {
            const SwJsonValue v = arr[i];
            if (!v.isObject()) continue;
            const SwJsonObject spec(v.toObject());
            const SwString exe = spec.contains("executable") ? SwString(spec["executable"].toString()) : SwString();
            const int hint = executableTargetHint_(baseDir, exe);
            if (hint != 0) return hint;
        }
        return 0;
    };

    int hint = scan(getArrayOrEmpty_(root["containers"]));
    if (hint == 0) hint = scan(getArrayOrEmpty_(root["nodes"]));
    if (hint != 0) return hint > 0;

#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

static ProcessFlags processFlagsFromOptions_(const SwJsonObject& opts) {
    ProcessFlags flags = ProcessFlags::NoFlag;

#if defined(_WIN32)
    const bool createNoWindow = opts.contains("createNoWindow") ? opts["createNoWindow"].toBool() : false;
    const bool createNewConsole = opts.contains("createNewConsole") ? opts["createNewConsole"].toBool() : false;
    const bool detached = opts.contains("detached") ? opts["detached"].toBool() : false;
    const bool suspended = opts.contains("suspended") ? opts["suspended"].toBool() : false;

    if (suspended) flags |= ProcessFlags::Suspended;

    if (detached) {
        flags |= ProcessFlags::Detached;
        if (createNewConsole || createNoWindow) {
            swWarning() << "[launcher] process options: 'detached' overrides 'createNewConsole/createNoWindow'";
        }
    } else if (createNewConsole) {
        flags |= ProcessFlags::CreateNewConsole;
        if (createNoWindow) {
            swWarning() << "[launcher] process options: 'createNewConsole' overrides 'createNoWindow'";
        }
    } else if (createNoWindow) {
        flags |= ProcessFlags::CreateNoWindow;
    }
#else
    (void)opts;
#endif

    return flags;
}

static uint64_t remoteObjectLastSeenMs_(const SwString& domain, const SwString& objectFqn) {
    uint64_t best = 0;
    if (domain.isEmpty() || objectFqn.isEmpty()) return best;

    SwJsonArray all = sw::ipc::shmRegistrySnapshot(domain);
    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["object"].toString()) != objectFqn) continue;

        const SwString sig = SwString(o["signal"].toString());
        if (!sig.startsWith("__config__|")) continue; // presence marker for SwRemoteObject

        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t > best) best = t;
    }
    return best;
}

enum class ChildLogLevel_ {
    Unknown = 0,
    Debug = 1,
    Warning = 2,
    Error = 3
};

static ChildLogLevel_ stripLeadingLogPrefix_(SwString& line) {
    SwString s = line;
    while (!s.isEmpty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '\n')) {
        s = s.mid(1);
    }

    auto looksLikeTimePrefixRaw = [](const SwString& v) -> bool {
        // Expected: "HHMMSS.UUUUUU" (6 digits '.' 6 digits)
        if (v.size() < 13) return false;
        for (size_t i = 0; i < 6; ++i) {
            const char c = v[i];
            if (c < '0' || c > '9') return false;
        }
        if (v[6] != '.') return false;
        for (size_t i = 7; i < 13; ++i) {
            const char c = v[i];
            if (c < '0' || c > '9') return false;
        }
        return true;
    };

    auto looksLikeTimePrefixBracketed = [&](const SwString& v) -> bool {
        // Expected: "[HHMMSS.UUUUUU]"
        if (v.size() < 15) return false;
        if (v[0] != '[') return false;
        if (v[14] != ']') return false;
        return looksLikeTimePrefixRaw(v.mid(1, 13));
    };

    if (looksLikeTimePrefixBracketed(s)) {
        s = s.mid(15);
        while (!s.isEmpty() && (s[0] == ' ' || s[0] == '\t')) {
            s = s.mid(1);
        }
    } else if (looksLikeTimePrefixRaw(s)) {
        s = s.mid(13);
        while (!s.isEmpty() && (s[0] == ' ' || s[0] == '\t')) {
            s = s.mid(1);
        }
    }

    const SwString tags[] = {SwString("[DEBUG]"), SwString("[WARNING]"), SwString("[ERROR]")};
    for (size_t i = 0; i < 3; ++i) {
        if (!s.startsWith(tags[i])) continue;

        s = s.mid(static_cast<int>(tags[i].size()));
        while (!s.isEmpty() && (s[0] == ' ' || s[0] == '\t')) {
            s = s.mid(1);
        }

        line = s;
        if (i == 0) return ChildLogLevel_::Debug;
        if (i == 1) return ChildLogLevel_::Warning;
        return ChildLogLevel_::Error;
    }

    line = s;
    return ChildLogLevel_::Unknown;
}

static void forwardChildChunk_(const SwString& id, bool fromStdErr, const SwString& chunk) {
    if (chunk.isEmpty()) return;

    const SwStringList lines = chunk.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        SwString line = lines[i];
        if (!line.isEmpty() && line[line.size() - 1] == '\r') {
            line.chop(1);
        }
        if (line.trimmed().isEmpty()) continue;

        const ChildLogLevel_ lvl = stripLeadingLogPrefix_(line);

        if (lvl == ChildLogLevel_::Debug) {
            swDebug() << "[" << id << "] " << line;
            continue;
        }
        if (lvl == ChildLogLevel_::Warning) {
            swWarning() << "[" << id << "] " << line;
            continue;
        }
        if (lvl == ChildLogLevel_::Error) {
            swError() << "[" << id << "] " << line;
            continue;
        }

        if (fromStdErr) {
            swWarning() << "[" << id << "][stderr] " << line;
        } else {
            swDebug() << "[" << id << "] " << line;
        }
    }
}

class LaunchContainerProcess : public SwObject {
 public:
    LaunchContainerProcess(const SwJsonObject& spec,
                           const SwString& baseDir,
                           const SwString& defaultSys,
                           bool preferWindowsExe,
                           SwObject* parent = nullptr)
        : SwObject(parent)
        , spec_(spec)
        , baseDir_(baseDir)
        , defaultSys_(defaultSys)
        , preferWindowsExe_(preferWindowsExe) {
        process_ = new SwProcess(this);

        SwObject::connect(process_, SIGNAL(readyReadStdOut), std::function<void()>([this]() {
            forwardChildChunk_(id_, /*fromStdErr=*/false, process_->read());
        }));

        SwObject::connect(process_, SIGNAL(readyReadStdErr), std::function<void()>([this]() {
            forwardChildChunk_(id_, /*fromStdErr=*/true, process_->readStdErr());
        }));

        SwObject::connect(process_, SIGNAL(processTerminated), std::function<void(int)>([this](int exitCode) {
            onTerminated_(exitCode);
        }));

        onlineTimer_ = new SwTimer(this);
        SwObject::connect(onlineTimer_, &SwTimer::timeout, [this]() { checkOnline_(); });
    }

    bool start() {
        if (!parseSpec_()) {
            return false;
        }
        if (!ensureConfigFile_()) {
            return false;
        }

        startMs_ = nowMonotonicMs_();
        lastSeenMs_ = 0;
        everOnline_ = false;

        SwStringList args;
        args.append(SwString("--sys=%1").arg(sys_));
        args.append(SwString("--ns=%1").arg(containerNs_));
        args.append(SwString("--name=%1").arg(containerName_));
        const SwString childConfigPath = pathForChildProcess_(exePath_, configFilePath_);
        args.append(SwString("--config_file=%1").arg(childConfigPath));
        args.append(SwString("--duration_ms=%1").arg(SwString::number(durationMs_)));

        swDebug() << "[launcher] start container=" << id_
                  << " exe=" << exePath_
                  << " wd=" << workingDir_
                  << " config=" << childConfigPath;

        const bool ok = process_->start(exePath_, args, processFlags_, workingDir_);
        if (!ok) {
            swError() << "[launcher] failed to start container=" << id_;
            if (onlineTimer_) onlineTimer_->stop();
        } else {
            startOnlineMonitoring_();
        }
        return ok;
    }

    void stop() {
        stopping_ = true;
        if (onlineTimer_) onlineTimer_->stop();
        if (process_ && process_->isOpen()) {
            process_->terminate();
        }
    }

 private:
    bool parseSpec_() {
        SwString sys = defaultSys_;
        if (spec_.contains("sys")) sys = SwString(spec_["sys"].toString());

        SwString ns;
        if (spec_.contains("ns")) ns = SwString(spec_["ns"].toString());
        if (ns.isEmpty() && spec_.contains("namespace")) ns = SwString(spec_["namespace"].toString());

        SwString name;
        if (spec_.contains("name")) name = SwString(spec_["name"].toString());
        if (name.isEmpty() && spec_.contains("object")) name = SwString(spec_["object"].toString());

        const SwString exe = spec_.contains("executable") ? SwString(spec_["executable"].toString()) : SwString();
        if (exe.isEmpty()) {
            swError() << "[launcher] container spec missing 'executable'";
            return false;
        }

        sys_ = sys;
        containerNs_ = ns;
        containerName_ = name;
        id_ = containerNs_ + "/" + containerName_;
        exePath_ = resolveExecutablePath_(baseDir_, exe, preferWindowsExe_);

        if (containerNs_.isEmpty() || containerName_.isEmpty()) {
            swError() << "[launcher] invalid container identity (need ns+name)";
            return false;
        }
        if (exePath_.isEmpty()) {
            swError() << "[launcher] invalid executable path";
            return false;
        }

        if (spec_.contains("workingDirectory")) {
            workingDir_ = resolvePath_(baseDir_, SwString(spec_["workingDirectory"].toString()));
        }
        if (workingDir_.isEmpty()) {
            SwFile f(exePath_);
            workingDir_ = f.getDirectory();
        }
        if (!workingDir_.isEmpty()) {
            workingDir_ = swDirPlatform().absolutePath(workingDir_);
        }

        const SwJsonObject opts = spec_.contains("options") ? getObjectOrEmpty_(spec_["options"]) : SwJsonObject();
        reloadOnCrash_ = opts.contains("reloadOnCrash") ? opts["reloadOnCrash"].toBool() : false;
        restartDelayMs_ = opts.contains("restartDelayMs") ? opts["restartDelayMs"].toInt() : 1000;
        reloadOnDisconnect_ = opts.contains("reloadOnDisconnect") ? opts["reloadOnDisconnect"].toBool() : false;
        disconnectTimeoutMs_ = opts.contains("disconnectTimeoutMs") ? opts["disconnectTimeoutMs"].toInt() : 5000;
        if (disconnectTimeoutMs_ <= 0) disconnectTimeoutMs_ = 5000;
        disconnectCheckMs_ = opts.contains("disconnectCheckMs") ? opts["disconnectCheckMs"].toInt() : 1000;
        if (disconnectCheckMs_ <= 0) disconnectCheckMs_ = 1000;
        durationMs_ = spec_.contains("duration_ms") ? spec_["duration_ms"].toInt() : 0;
        if (durationMs_ <= 0) durationMs_ = 0; // run until launcher stops it

        processFlags_ = processFlagsFromOptions_(opts);

        return true;
    }

    void startOnlineMonitoring_() {
        if (!onlineTimer_) return;
        if (!reloadOnDisconnect_) {
            onlineTimer_->stop();
            return;
        }
        onlineTimer_->stop();
        onlineTimer_->start(disconnectCheckMs_);
    }

    void checkOnline_() {
        if (stopping_) return;
        if (!reloadOnDisconnect_) return;
        if (!process_ || !process_->isOpen()) return;

        const uint64_t now = nowMonotonicMs_();
        const uint64_t seen = remoteObjectLastSeenMs_(sys_, id_);
        if (seen != 0) {
            everOnline_ = true;
            lastSeenMs_ = seen;
            return;
        }

        const uint64_t ref = everOnline_ ? lastSeenMs_ : startMs_;
        if (ref == 0) return;
        if (now < ref) return;

        if ((now - ref) < static_cast<uint64_t>(disconnectTimeoutMs_)) return;

        swWarning() << "[launcher] container offline (registry heartbeat) => restart id=" << id_;
        restartDueToDisconnect_();
    }

    void restartDueToDisconnect_() {
        if (stopping_) return;
        if (!process_ || !process_->isOpen()) return;
        if (!reloadOnDisconnect_) return;
        if (onlineTimer_) onlineTimer_->stop();

        process_->terminate();

        const int delay = (restartDelayMs_ > 0) ? restartDelayMs_ : 1000;
        SwTimer::singleShot(delay, [this]() {
            if (stopping_) return;
            swWarning() << "[launcher] restarting container (disconnect) id=" << id_;
            (void)start();
        });
    }

    bool ensureConfigFile_() {
        if (!configFilePath_.isEmpty()) return true;

        if (spec_.contains("config_file")) {
            configFilePath_ = resolvePath_(baseDir_, SwString(spec_["config_file"].toString()));
            return !configFilePath_.isEmpty();
        }

        SwJsonObject composition = spec_.contains("composition") ? getObjectOrEmpty_(spec_["composition"]) : SwJsonObject();
        if (composition.size() == 0) {
            composition = spec_;
        }

        SwJsonObject compOut;
        if (composition.contains("threading")) compOut["threading"] = composition["threading"];
        if (composition.contains("plugins")) {
            const SwJsonValue pv = composition["plugins"];
            if (pv.isArray()) {
                const SwJsonArray in(pv.toArray());
                SwJsonArray out;
                for (size_t i = 0; i < in.size(); ++i) {
                    const SwJsonValue v = in[i];
                    if (!v.isString()) {
                        out.append(v);
                        continue;
                    }
                    const SwString stripped = stripKnownLibrarySuffix_(SwString(v.toString()));
                    out.append(SwJsonValue(stripped.toStdString()));
                }
                compOut["plugins"] = SwJsonValue(out);
            } else {
                compOut["plugins"] = pv;
            }
        }
        if (composition.contains("components")) compOut["components"] = composition["components"];
        if (!compOut.contains("components") && composition.contains("nodes")) compOut["components"] = composition["nodes"];

        SwJsonObject cfgOut;
        cfgOut["composition"] = SwJsonValue(compOut);
        if (spec_.contains("options")) {
            cfgOut["options"] = spec_["options"];
        }

        const SwString tempDir = tempDirForChildConfig_(exePath_, baseDir_);
        SwString leaf = SwString("sw_launch_") + sanitizeFileLeaf_(id_) + SwString(".json");
        configFilePath_ = joinPath_(tempDir, leaf);

        SwFile f(configFilePath_);
        if (!f.open(SwFile::Write)) {
            swError() << "[launcher] failed to write temp config: " << configFilePath_;
            return false;
        }

        SwJsonDocument d(cfgOut);
        f.write(d.toJson(SwJsonDocument::JsonFormat::Pretty));
        return true;
    }

    void onTerminated_(int exitCode) {
        swWarning() << "[launcher] container terminated=" << id_ << " exitCode=" << exitCode;
        if (process_ && process_->isOpen()) process_->close();
        if (stopping_) return;
        if (!reloadOnCrash_) return;
        if (exitCode == 0) return;

        const int delay = (restartDelayMs_ > 0) ? restartDelayMs_ : 1000;
        SwTimer::singleShot(delay, [this]() {
            if (stopping_) return;
            swWarning() << "[launcher] restarting container=" << id_;
            (void)start();
        });
    }

    SwJsonObject spec_;
    SwString baseDir_;
    SwString defaultSys_;
    bool preferWindowsExe_{false};

    SwProcess* process_{nullptr};

    SwString sys_{};
    SwString containerNs_{};
    SwString containerName_{};
    SwString id_{};
    SwString exePath_{};
    SwString workingDir_{};
    SwString configFilePath_{};
    bool reloadOnCrash_{false};
    int restartDelayMs_{1000};
    bool reloadOnDisconnect_{false};
    int disconnectTimeoutMs_{5000};
    int disconnectCheckMs_{1000};
    int durationMs_{0};
    ProcessFlags processFlags_{ProcessFlags::NoFlag};
    bool stopping_{false};
    SwTimer* onlineTimer_{nullptr};
    uint64_t startMs_{0};
    uint64_t lastSeenMs_{0};
    bool everOnline_{false};
};

class LaunchNodeProcess : public SwObject {
 public:
    LaunchNodeProcess(const SwJsonObject& spec,
                      const SwString& baseDir,
                      const SwString& defaultSys,
                      bool preferWindowsExe,
                      SwObject* parent = nullptr)
        : SwObject(parent)
        , spec_(spec)
        , baseDir_(baseDir)
        , defaultSys_(defaultSys)
        , preferWindowsExe_(preferWindowsExe) {
        process_ = new SwProcess(this);

        SwObject::connect(process_, SIGNAL(readyReadStdOut), std::function<void()>([this]() {
            forwardChildChunk_(id_, /*fromStdErr=*/false, process_->read());
        }));

        SwObject::connect(process_, SIGNAL(readyReadStdErr), std::function<void()>([this]() {
            forwardChildChunk_(id_, /*fromStdErr=*/true, process_->readStdErr());
        }));

        SwObject::connect(process_, SIGNAL(processTerminated), std::function<void(int)>([this](int exitCode) {
            onTerminated_(exitCode);
        }));

        onlineTimer_ = new SwTimer(this);
        SwObject::connect(onlineTimer_, &SwTimer::timeout, [this]() { checkOnline_(); });
    }

    bool start() {
        if (!parseSpec_()) {
            return false;
        }
        if (!ensureConfigFile_()) {
            return false;
        }

        startMs_ = nowMonotonicMs_();
        lastSeenMs_ = 0;
        everOnline_ = false;

        SwStringList args;
        args.append(SwString("--sys=%1").arg(sys_));
        args.append(SwString("--ns=%1").arg(nodeNs_));
        args.append(SwString("--name=%1").arg(nodeName_));
        if (!configFilePath_.isEmpty()) {
            args.append(SwString("--config_file=%1").arg(pathForChildProcess_(exePath_, configFilePath_)));
        }
        if (!configRoot_.isEmpty()) {
            const SwString childRoot = (isAbsPath_(configRoot_) ? pathForChildProcess_(exePath_, configRoot_) : configRoot_);
            args.append(SwString("--config_root=%1").arg(childRoot));
        }
        args.append(SwString("--duration_ms=%1").arg(SwString::number(durationMs_)));

        swDebug() << "[launcher] start node=" << id_
                  << " exe=" << exePath_
                  << " wd=" << workingDir_
                  << " config=" << pathForChildProcess_(exePath_, configFilePath_);

        const bool ok = process_->start(exePath_, args, processFlags_, workingDir_);
        if (!ok) {
            swError() << "[launcher] failed to start node=" << id_;
            if (onlineTimer_) onlineTimer_->stop();
        } else {
            startOnlineMonitoring_();
        }
        return ok;
    }

    void stop() {
        stopping_ = true;
        if (onlineTimer_) onlineTimer_->stop();
        if (process_ && process_->isOpen()) {
            process_->terminate();
        }
    }

 private:
    bool parseSpec_() {
        SwString sys = defaultSys_;
        if (spec_.contains("sys")) sys = SwString(spec_["sys"].toString());

        SwString ns;
        if (spec_.contains("ns")) ns = SwString(spec_["ns"].toString());
        if (ns.isEmpty() && spec_.contains("namespace")) ns = SwString(spec_["namespace"].toString());

        SwString name;
        if (spec_.contains("name")) name = SwString(spec_["name"].toString());
        if (name.isEmpty() && spec_.contains("object")) name = SwString(spec_["object"].toString());

        const SwString exe = spec_.contains("executable") ? SwString(spec_["executable"].toString()) : SwString();
        if (exe.isEmpty()) {
            swError() << "[launcher] node spec missing 'executable'";
            return false;
        }

        sys_ = sys;
        nodeNs_ = ns;
        nodeName_ = name;
        id_ = nodeNs_ + "/" + nodeName_;
        exePath_ = resolveExecutablePath_(baseDir_, exe, preferWindowsExe_);
        configRoot_ = spec_.contains("config_root") ? SwString(spec_["config_root"].toString()) : SwString();

        if (nodeNs_.isEmpty() || nodeName_.isEmpty()) {
            swError() << "[launcher] invalid node identity (need ns+name)";
            return false;
        }
        if (exePath_.isEmpty()) {
            swError() << "[launcher] invalid executable path";
            return false;
        }

        if (spec_.contains("workingDirectory")) {
            workingDir_ = resolvePath_(baseDir_, SwString(spec_["workingDirectory"].toString()));
        }
        if (workingDir_.isEmpty()) {
            SwFile f(exePath_);
            workingDir_ = f.getDirectory();
        }
        if (!workingDir_.isEmpty()) {
            workingDir_ = swDirPlatform().absolutePath(workingDir_);
        }

        const SwJsonObject opts = spec_.contains("options") ? getObjectOrEmpty_(spec_["options"]) : SwJsonObject();
        reloadOnCrash_ = opts.contains("reloadOnCrash") ? opts["reloadOnCrash"].toBool() : false;
        restartDelayMs_ = opts.contains("restartDelayMs") ? opts["restartDelayMs"].toInt() : 1000;
        reloadOnDisconnect_ = opts.contains("reloadOnDisconnect") ? opts["reloadOnDisconnect"].toBool() : false;
        disconnectTimeoutMs_ = opts.contains("disconnectTimeoutMs") ? opts["disconnectTimeoutMs"].toInt() : 5000;
        if (disconnectTimeoutMs_ <= 0) disconnectTimeoutMs_ = 5000;
        disconnectCheckMs_ = opts.contains("disconnectCheckMs") ? opts["disconnectCheckMs"].toInt() : 1000;
        if (disconnectCheckMs_ <= 0) disconnectCheckMs_ = 1000;
        durationMs_ = spec_.contains("duration_ms") ? spec_["duration_ms"].toInt() : 0;
        if (durationMs_ <= 0) durationMs_ = 0; // run until launcher stops it

        processFlags_ = processFlagsFromOptions_(opts);

        return true;
    }

    void startOnlineMonitoring_() {
        if (!onlineTimer_) return;
        if (!reloadOnDisconnect_) {
            onlineTimer_->stop();
            return;
        }
        onlineTimer_->stop();
        onlineTimer_->start(disconnectCheckMs_);
    }

    void checkOnline_() {
        if (stopping_) return;
        if (!reloadOnDisconnect_) return;
        if (!process_ || !process_->isOpen()) return;

        const uint64_t now = nowMonotonicMs_();
        const uint64_t seen = remoteObjectLastSeenMs_(sys_, id_);
        if (seen != 0) {
            everOnline_ = true;
            lastSeenMs_ = seen;
            return;
        }

        const uint64_t ref = everOnline_ ? lastSeenMs_ : startMs_;
        if (ref == 0) return;
        if (now < ref) return;

        if ((now - ref) < static_cast<uint64_t>(disconnectTimeoutMs_)) return;

        swWarning() << "[launcher] node offline (registry heartbeat) => restart id=" << id_;
        restartDueToDisconnect_();
    }

    void restartDueToDisconnect_() {
        if (stopping_) return;
        if (!process_ || !process_->isOpen()) return;
        if (!reloadOnDisconnect_) return;
        if (onlineTimer_) onlineTimer_->stop();

        process_->terminate();

        const int delay = (restartDelayMs_ > 0) ? restartDelayMs_ : 1000;
        SwTimer::singleShot(delay, [this]() {
            if (stopping_) return;
            swWarning() << "[launcher] restarting node (disconnect) id=" << id_;
            (void)start();
        });
    }

    bool ensureConfigFile_() {
        if (!configFilePath_.isEmpty()) return true;

        if (spec_.contains("config_file")) {
            configFilePath_ = resolvePath_(baseDir_, SwString(spec_["config_file"].toString()));
            return !configFilePath_.isEmpty();
        }

        const bool hasParams = spec_.contains("params") && spec_["params"].isObject();
        const bool hasOptions = spec_.contains("options") && spec_["options"].isObject();
        const bool hasRoot = spec_.contains("config_root") && !SwString(spec_["config_root"].toString()).isEmpty();
        if (!hasParams && !hasOptions && !hasRoot) {
            return true; // no config to pass
        }

        SwJsonObject cfgOut;
        if (hasParams) cfgOut["params"] = spec_["params"];
        if (hasOptions) cfgOut["options"] = spec_["options"];
        if (hasRoot) cfgOut["config_root"] = spec_["config_root"];

        const SwString tempDir = tempDirForChildConfig_(exePath_, baseDir_);
        SwString leaf = SwString("sw_node_") + sanitizeFileLeaf_(id_) + SwString(".json");
        configFilePath_ = joinPath_(tempDir, leaf);

        SwFile f(configFilePath_);
        if (!f.open(SwFile::Write)) {
            swError() << "[launcher] failed to write temp config: " << configFilePath_;
            return false;
        }

        SwJsonDocument d(cfgOut);
        f.write(d.toJson(SwJsonDocument::JsonFormat::Pretty));
        return true;
    }

    void onTerminated_(int exitCode) {
        swWarning() << "[launcher] node terminated=" << id_ << " exitCode=" << exitCode;
        if (process_ && process_->isOpen()) process_->close();
        if (stopping_) return;
        if (!reloadOnCrash_) return;
        if (exitCode == 0) return;

        const int delay = (restartDelayMs_ > 0) ? restartDelayMs_ : 1000;
        SwTimer::singleShot(delay, [this]() {
            if (stopping_) return;
            swWarning() << "[launcher] restarting node=" << id_;
            (void)start();
        });
    }

    SwJsonObject spec_;
    SwString baseDir_;
    SwString defaultSys_;
    bool preferWindowsExe_{false};

    SwProcess* process_{nullptr};

    SwString sys_{};
    SwString nodeNs_{};
    SwString nodeName_{};
    SwString id_{};
    SwString exePath_{};
    SwString workingDir_{};
    SwString configFilePath_{};
    SwString configRoot_{};
    bool reloadOnCrash_{false};
    int restartDelayMs_{1000};
    bool reloadOnDisconnect_{false};
    int disconnectTimeoutMs_{5000};
    int disconnectCheckMs_{1000};
    int durationMs_{0};
    ProcessFlags processFlags_{ProcessFlags::NoFlag};
    bool stopping_{false};
    SwTimer* onlineTimer_{nullptr};
    uint64_t startMs_{0};
    uint64_t lastSeenMs_{0};
    bool everOnline_{false};
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    SwJsonObject root;
    SwString err;
    SwString configFileUsed;
    if (!loadJsonObject_(app, root, err, configFileUsed)) {
        swError() << err;
        return 2;
    }

    SwString baseDir;
    if (!configFileUsed.isEmpty()) {
        SwFile f(configFileUsed);
        baseDir = f.getDirectory();
    }
    if (baseDir.isEmpty()) {
        baseDir = SwDir::currentPath();
    }

    SwString sys = root.contains("sys") ? SwString(root["sys"].toString()) : SwString("demo");
    if (app.hasArgument("sys")) {
        sys = app.getArgument("sys", sys);
    }
    int durationMs = root.contains("duration_ms") ? root["duration_ms"].toInt() : 0;
    if (app.hasArgument("duration_ms")) {
        durationMs = app.getArgument("duration_ms", "0").toInt();
    }

    const bool preferWindowsExe = guessPreferWindowsExe_(root, baseDir);

    SwObject manager;
    auto* traceConfig = new SwLaunchTraceConfig(sys, resolvePath_(baseDir, "systemConfig"), baseDir, &manager);
    (void)traceConfig;
    SwList<LaunchContainerProcess*> containers;
    SwList<LaunchNodeProcess*> nodes;

    const SwJsonArray containersArr = getArrayOrEmpty_(root["containers"]);
    const SwJsonArray nodesArr = getArrayOrEmpty_(root["nodes"]);
    if (containersArr.size() == 0 && nodesArr.size() == 0) {
        swError() << "launch json must contain 'containers' and/or 'nodes' arrays";
        return 2;
    }

    for (size_t i = 0; i < containersArr.size(); ++i) {
        const SwJsonValue v = containersArr[i];
        if (!v.isObject()) continue;
        const SwJsonObject spec(v.toObject());
        auto* c = new LaunchContainerProcess(spec, baseDir, sys, preferWindowsExe, &manager);
        containers.append(c);
        if (!c->start()) {
            swError() << "[launcher] failed to start a container, stopping";
            for (int k = 0; k < containers.size(); ++k) containers[k]->stop();
            for (int k = 0; k < nodes.size(); ++k) nodes[k]->stop();
            return 3;
        }
    }

    for (size_t i = 0; i < nodesArr.size(); ++i) {
        const SwJsonValue v = nodesArr[i];
        if (!v.isObject()) continue;
        const SwJsonObject spec(v.toObject());
        auto* n = new LaunchNodeProcess(spec, baseDir, sys, preferWindowsExe, &manager);
        nodes.append(n);
        if (!n->start()) {
            swError() << "[launcher] failed to start a node, stopping";
            for (int k = 0; k < containers.size(); ++k) containers[k]->stop();
            for (int k = 0; k < nodes.size(); ++k) nodes[k]->stop();
            return 3;
        }
    }

    if (durationMs > 0) {
        SwTimer::singleShot(durationMs, [&]() {
            swWarning() << "[launcher] stopping all processes";
            for (int i = 0; i < containers.size(); ++i) containers[i]->stop();
            for (int i = 0; i < nodes.size(); ++i) nodes[i]->stop();
            app.quit();
        });
    }

    return app.exec();
}
