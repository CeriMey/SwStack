#include "SwCoreApplication.h"
#include "SwLaunchCrash.h"
#include "SwDebug.h"
#include "SwDir.h"
#include "SwEventLoop.h"
#include "SwFile.h"
#include "SwHttpContext.h"
#include "SwHttpServer.h"
#include "SwJsonDocument.h"
#include "SwLaunchDeploySupport.h"
#include "SwLaunchTraceConfig.h"
#include "SwLaunchVersion.h"
#include "SwProcess.h"
#include "SwSharedMemorySignal.h"
#include "SwStandardLocation.h"
#include "SwTimer.h"

#include <cctype>
#include <iostream>
#include <memory>
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

static bool hasCrashTestArg_(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const SwString arg = argv[i];
        if (arg == "-crash_test" || arg == "--crash-test" || arg == "--crash-handler-smoke") {
            return true;
        }
    }
    return false;
}

static SwString swLaunchHelpTopics_() {
    return "config, nodes, containers, supervision, control_api, logs, state_api, deploy, checksum, rollback, paths";
}

static SwList<SwString> swLaunchHelpTopicList_() {
    SwList<SwString> topics;
    const SwList<SwString> rawParts = swLaunchHelpTopics_().split(',');
    for (size_t i = 0; i < rawParts.size(); ++i) {
        const SwString topic = rawParts[i].trimmed();
        if (!topic.isEmpty()) {
            topics.append(topic);
        }
    }
    return topics;
}

static SwString swLaunchVersion_() {
    return SWLAUNCH_VERSION_STRING;
}

static SwString swLaunchGitRevision_() {
    return SWLAUNCH_GIT_REVISION;
}

static SwString swLaunchBuildStamp_() {
    return SwString(__DATE__) + " " + SwString(__TIME__);
}

static SwString swLaunchVersionLine_() {
    SwString line = swLaunchVersion_();
    const SwString gitRevision = swLaunchGitRevision_().trimmed();
    if (!gitRevision.isEmpty() && gitRevision != "unknown") {
        line += " (git ";
        line += gitRevision;
        line += ")";
    }
    return line;
}

static SwString normalizeHelpTopic_(SwString topic) {
    topic = topic.trimmed().toLower();
    topic.replace("-", "_");
    topic.replace(" ", "_");

    if (topic == "control" || topic == "api" || topic == "http" || topic == "controlapi") {
        return "control_api";
    }
    if (topic == "log" || topic == "stream" || topic == "log_stream" || topic == "sse") {
        return "logs";
    }
    if (topic == "state" || topic == "state_put" || topic == "put_state") {
        return "state_api";
    }
    if (topic == "checksums" || topic == "sha256") {
        return "checksum";
    }
    if (topic == "rollbacks") {
        return "rollback";
    }
    if (topic == "path" || topic == "files") {
        return "paths";
    }
    return topic;
}

static SwString swLaunchHelpOverview_() {
    return SwString("SwLaunch\n\nVersion:\n  ") + swLaunchVersionLine_() + SwString(
R"(

SwLaunch is a local orchestrator for nodes and containers.
It can:
- load a desired state from JSON,
- start and supervise child processes,
- expose a protected control API,
- apply desired-state changes at runtime,
- deploy binaries and payload files with SHA-256 verification,
- persist the resulting launch.json,
- rollback files and runtime if a deployment fails.

Basic usage:
  SwLaunch --config_file=<path>
  SwLaunch --config_json=<json>
  SwLaunch --version
  SwLaunch -h

Global arguments:
  -v, --version              Print the product version and exit.
  --config_file=<path>       Read launch state from disk.
  --config_json=<json>       Read launch state from inline JSON.
  --sys=<domain>             Override default system id.
  --duration_ms=<ms>         Stop all children after this delay. 0 = run forever.

Control API arguments:
  --control_port=<port>      Enable the HTTP control API on this port.
  --control_bind=<ipv4>      Bind address. Default: 127.0.0.1
  --control_token=<secret>   Required bearer token for /api/* routes.

Control API routes:
  GET  /api/launch/help
  GET  /api/launch/help/:topic
  GET  /api/launch/state
  GET  /api/launch/logs/stream
  PUT  /api/launch/state
  POST /api/launch/deploy
  GET  /api/launch/deploy/:jobId

Important behavior:
  - Runtime mutations require startup with --config_file.
  - The control token is never persisted into launch.json.
  - Deployments are serialized. A concurrent mutation gets HTTP 409.
  - File replacement is limited to the target unit root:
      workingDirectory if defined, otherwise executable directory.
  - A file already identical by SHA-256 is skipped and does not trigger a restart by itself.
  - Node/container spec changes trigger a targeted restart of that owner unit.
  - Container internal composition changes are normalized as a restart of the owning container.

Detailed help:
  SwLaunch --help=config
  SwLaunch -h deploy
  SwLaunch --help=deploy
  SwLaunch --help=control_api
  SwLaunch --help=logs
  SwLaunch --help=rollback

Accepted help topics:
  )") + swLaunchHelpTopics_() + "\n";
}

static SwString swLaunchHelpForTopic_(const SwString& rawTopic) {
    const SwString topic = normalizeHelpTopic_(rawTopic);

    if (topic.isEmpty() || topic == "overview" || topic == "general") {
        return swLaunchHelpOverview_();
    }

    if (topic == "config") {
        return SwString(
R"(SwLaunch help: config

Goal:
  Start SwLaunch from a reproducible desired state.

What SwLaunch expects:
  - --config_file=<path> or --config_json=<json>
  - root JSON must be an object
  - root may contain sys, duration_ms, control_api, nodes, containers

Key rules:
  - baseDir = directory of --config_file when present, otherwise current directory
  - executable, workingDirectory and config_file child paths are resolved from baseDir
  - desired state is full-state oriented, not patch oriented

What to verify:
  - JSON parses cleanly
  - child executables resolve to real files
  - workingDirectory is correct for plugins and relative payload files
)");
    }

    if (topic == "nodes") {
        return SwString(
R"(SwLaunch help: nodes

Goal:
  Launch a standalone executable under SwLaunch supervision.

Required fields per node:
  - ns
  - name
  - executable

Useful optional fields:
  - workingDirectory
  - duration_ms
  - config_file
  - config_root
  - params
  - options

What SwLaunch does:
  - resolves the executable path
  - generates a temporary child config file when params/options/config_root are provided
  - starts the process with --sys --ns --name and child config arguments
  - supervises restart and disconnect behavior if enabled
)");
    }

    if (topic == "containers") {
        return SwString(
R"(SwLaunch help: containers

Goal:
  Launch a process such as SwComponentContainer and manage it like a single owner unit.

Required fields per container:
  - ns
  - name
  - executable

Useful optional fields:
  - workingDirectory
  - duration_ms
  - config_file
  - composition
  - options

What SwLaunch does:
  - resolves the executable path
  - generates a temporary config_file when composition/options are provided
  - starts the container process
  - treats internal composition changes as a restart of the owning container in deploy V1

Operational note:
  Plugin loading depends on the container process workingDirectory.
)");
    }

    if (topic == "supervision") {
        return SwString(
R"(SwLaunch help: supervision

Crash restart:
  - enable options.reloadOnCrash=true
  - if exitCode != 0, SwLaunch restarts the unit after restartDelayMs

Disconnect restart:
  - enable options.reloadOnDisconnect=true
  - SwLaunch scans the SHM registry for the expected object presence marker
  - if the object stays absent longer than disconnectTimeoutMs, the unit is restarted

Important:
  - clean exitCode 0 does not count as a crash restart
  - disconnect supervision only makes sense if the child publishes presence normally
)");
    }

    if (topic == "control_api") {
        return SwString(
R"(SwLaunch help: control_api

Goal:
  Control the launcher at runtime through a protected local HTTP API.

How to enable it:
  --control_port=<port>
  --control_bind=<ipv4>      default: 127.0.0.1
  --control_token=<secret>

Auth model:
  Authorization: Bearer <token>

Routes:
  GET  /api/launch/help
  GET  /api/launch/help/:topic
  GET  /api/launch/state
  GET  /api/launch/logs/stream
  PUT  /api/launch/state
  POST /api/launch/deploy
  GET  /api/launch/deploy/:jobId

Important:
  - help routes expose version, capabilities and help topics in JSON
  - mutations are refused unless SwLaunch was started with --config_file
  - the bearer token is not returned by the API and is not written into launch.json
)");
    }

    if (topic == "logs") {
        return SwString(
R"(SwLaunch help: logs

Goal:
  Follow launcher and child logs in real time over a long-lived HTTP stream.

Route:
  GET /api/launch/logs/stream

Protocol:
  - Server-Sent Events (text/event-stream)
  - same bearer token as the rest of /api/*
  - long-lived HTTP connection with periodic keepalive comments

Behavior:
  - streams SwLaunch logs
  - streams child stdout/stderr after they are forwarded by SwLaunch
  - sends a short backlog on connect
  - query parameter backlog=<0..200> controls backlog size, default 32

Quick test:
  curl -N -H "Authorization: Bearer <token>" http://<ip>:7777/api/launch/logs/stream
)");
    }

    if (topic == "state_api") {
        return SwString(
R"(SwLaunch help: state_api

GET /api/launch/state
  Returns:
  - current desiredState
  - runtime summary of managed units
  - control API bind/port summary

PUT /api/launch/state
  Accepts:
  - a full desired state object

Behavior:
  - removed unit: stop it
  - added unit: start it
  - changed unit spec: restart that unit
  - unchanged unit: keep it running

Important:
  - this is a full-state replacement flow, not a partial patch flow
  - successful mutation persists launch.json when --config_file is in use
)");
    }

    if (topic == "deploy") {
        return SwString(
R"(SwLaunch help: deploy

Goal:
  Replace binaries and payload files at runtime in a controlled, rollback-safe flow.

Request format:
  POST /api/launch/deploy
  Content-Type: multipart/form-data

Parts:
  - manifest
  - one file part for each declared artifact

Manifest fields:
  - formatVersion
  - deploymentId (optional)
  - desiredState
  - artifacts[]

Per artifact:
  - partName
  - ownerKey            node:<ns>/<name> or container:<ns>/<name>
  - relativePath
  - sha256

Apply flow:
  1. authenticate request
  2. parse multipart into staging
  3. verify artifact SHA-256
  4. compare against current deployed files
  5. compute impacted owner units
  6. stop only impacted units
  7. replace only non-identical files
  8. persist launch.json
  9. restart target state
 10. rollback files and runtime if a failure occurs
)");
    }

    if (topic == "checksum") {
        return SwString(
R"(SwLaunch help: checksum

SwLaunch uses SHA-256 for two reasons:
  1. validate uploaded artifact integrity
  2. avoid replacing a file that is already identical on disk

Outcomes:
  - upload SHA-256 != manifest SHA-256:
      deployment is rejected before stopping units
  - upload SHA-256 == current deployed file SHA-256:
      artifact is skipped

Practical effect:
  - skippedFiles contains already-identical artifacts
  - a skipped artifact does not trigger a restart by itself
  - replaying the same bundle is idempotent from the file-replacement point of view
)");
    }

    if (topic == "rollback") {
        return SwString(
R"(SwLaunch help: rollback

Goal:
  Avoid leaving the launcher in a half-applied state.

Rollback scope:
  - replaced files
  - persisted launch.json
  - runtime state of managed units as far as possible

When rollback is triggered:
  - artifact replacement failure
  - config persistence failure
  - runtime restart/apply failure after file replacement

Operational expectation:
  - a failed job should converge back toward the previous state
  - deployment jobs expose errors, replacedFiles and skippedFiles for diagnosis
)");
    }

    if (topic == "paths") {
        return SwString(
R"(SwLaunch help: paths

Resolution rules:
  - baseDir = config_file directory, or current directory if using config_json
  - child executable path is resolved from baseDir
  - child workingDirectory is resolved from baseDir
  - if workingDirectory is absent, executable directory is used

Deployment write root:
  - workingDirectory if defined
  - otherwise executable directory

Security rules for deployment artifacts:
  - relativePath must stay relative
  - absolute paths are rejected
  - paths containing .. are rejected

Practical consequence:
  SwLaunch can replace files owned by a managed unit, but it is not a generic remote file writer.
)");
    }

    if (topic == "all") {
        return swLaunchHelpOverview_() +
               "\n" + swLaunchHelpForTopic_("config") +
               "\n" + swLaunchHelpForTopic_("nodes") +
               "\n" + swLaunchHelpForTopic_("containers") +
               "\n" + swLaunchHelpForTopic_("supervision") +
               "\n" + swLaunchHelpForTopic_("control_api") +
               "\n" + swLaunchHelpForTopic_("logs") +
               "\n" + swLaunchHelpForTopic_("state_api") +
               "\n" + swLaunchHelpForTopic_("deploy") +
               "\n" + swLaunchHelpForTopic_("checksum") +
               "\n" + swLaunchHelpForTopic_("rollback") +
               "\n" + swLaunchHelpForTopic_("paths");
    }

    return SwString();
}

static bool tryPrintHelp_(const SwCoreApplication& app, int& exitCodeOut) {
    exitCodeOut = 0;

    SwString topic;
    if (app.hasArgument("h")) {
        topic = app.getArgument("h", SwString());
    } else if (app.hasArgument("?")) {
        topic = app.getArgument("?", SwString());
    } else if (app.hasArgument("help_topic")) {
        topic = app.getArgument("help_topic", SwString());
    } else if (app.hasArgument("help_feature")) {
        topic = app.getArgument("help_feature", SwString());
    } else if (app.hasArgument("help-topic")) {
        topic = app.getArgument("help-topic", SwString());
    } else if (app.hasArgument("help-feature")) {
        topic = app.getArgument("help-feature", SwString());
    } else if (app.hasArgument("help")) {
        topic = app.getArgument("help", SwString());
    } else {
        return false;
    }

    const SwString helpText = swLaunchHelpForTopic_(topic);
    if (!helpText.isEmpty()) {
        std::cout << helpText.toStdString();
        if (!helpText.endsWith("\n")) {
            std::cout << std::endl;
        }
        exitCodeOut = 0;
        return true;
    }

    std::cout << "Unknown SwLaunch help topic: " << topic.toStdString() << "\n\n";
    std::cout << swLaunchHelpOverview_().toStdString();
    exitCodeOut = 2;
    return true;
}

static bool tryPrintVersion_(const SwCoreApplication& app, int& exitCodeOut) {
    exitCodeOut = 0;
    if (!app.hasArgument("v") && !app.hasArgument("version")) {
        return false;
    }

    std::cout << "SwLaunch " << swLaunchVersionLine_().toStdString() << std::endl;
    return true;
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
    const bool runAsAdmin = opts.contains("runAsAdmin") ? opts["runAsAdmin"].toBool() : false;

    if (runAsAdmin) flags |= ProcessFlags::RunAsAdmin;
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

static bool hasProcessFlag_(ProcessFlags flags, ProcessFlags bit) {
#if defined(_WIN32)
    return (static_cast<DWORD>(flags) & static_cast<DWORD>(bit)) != 0;
#else
    return (static_cast<int>(flags) & static_cast<int>(bit)) != 0;
#endif
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

#if defined(_WIN32)
static HANDLE createLauncherLifetimeJob_() {
    HANDLE job = ::CreateJobObjectW(NULL, NULL);
    if (!job) {
        swError() << "[launcher] CreateJobObjectW failed: " << ::GetLastError();
        return NULL;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                            JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if (!::SetInformationJobObject(job,
                                   JobObjectExtendedLimitInformation,
                                   &info,
                                   static_cast<DWORD>(sizeof(info)))) {
        swError() << "[launcher] SetInformationJobObject failed: " << ::GetLastError();
        ::CloseHandle(job);
        return NULL;
    }

    BOOL inJob = FALSE;
    if (::IsProcessInJob(::GetCurrentProcess(), NULL, &inJob) && inJob) {
        swWarning() << "[launcher] current process is already inside a job; attached-child ownership may be limited";
        return job;
    }

    if (!::AssignProcessToJobObject(job, ::GetCurrentProcess())) {
        swError() << "[launcher] AssignProcessToJobObject(self) failed: " << ::GetLastError();
        ::CloseHandle(job);
        return NULL;
    }

    return job;
}
#endif

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
            SwTimer::singleShot(0, [this]() {
                drainPendingOutput_();
            });
        }
        return ok;
    }

    void stop() {
        stopping_ = true;
        if (onlineTimer_) onlineTimer_->stop();
        if (process_ && process_->isOpen()) {
            if (hasProcessFlag_(processFlags_, ProcessFlags::Detached)) {
                process_->release();
            } else {
                process_->terminate();
            }
        }
    }

    void forceKill() {
        stopping_ = true;
        if (onlineTimer_) onlineTimer_->stop();
        if (process_ && process_->isOpen()) {
            process_->kill();
        }
    }

    void setShutdownOnCleanExitHandler(const std::function<void(const SwString&, int)>& handler) {
        shutdownOnCleanExitHandler_ = handler;
    }

    bool isRunning() const {
        return process_ && process_->isOpen();
    }

    SwString runtimeId() const {
        return id_;
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
        shutdownLauncherOnCleanExit_ =
            opts.contains("shutdownLauncherOnCleanExit") ? opts["shutdownLauncherOnCleanExit"].toBool() : false;
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
        if (exitCode == 0 && shutdownLauncherOnCleanExit_) {
            if (shutdownOnCleanExitHandler_) {
                shutdownOnCleanExitHandler_(id_, exitCode);
            }
            return;
        }
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
    bool shutdownLauncherOnCleanExit_{false};
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
    std::function<void(const SwString&, int)> shutdownOnCleanExitHandler_{};

    void drainPendingOutput_() {
        if (!process_) return;
        forwardChildChunk_(id_, /*fromStdErr=*/false, process_->read());
        forwardChildChunk_(id_, /*fromStdErr=*/true, process_->readStdErr());
    }
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
            SwTimer::singleShot(0, [this]() {
                drainPendingOutput_();
            });
        }
        return ok;
    }

    void stop() {
        stopping_ = true;
        if (onlineTimer_) onlineTimer_->stop();
        if (process_ && process_->isOpen()) {
            if (hasProcessFlag_(processFlags_, ProcessFlags::Detached)) {
                process_->release();
            } else {
                process_->terminate();
            }
        }
    }

    void forceKill() {
        stopping_ = true;
        if (onlineTimer_) onlineTimer_->stop();
        if (process_ && process_->isOpen()) {
            process_->kill();
        }
    }

    void setShutdownOnCleanExitHandler(const std::function<void(const SwString&, int)>& handler) {
        shutdownOnCleanExitHandler_ = handler;
    }

    bool isRunning() const {
        return process_ && process_->isOpen();
    }

    SwString runtimeId() const {
        return id_;
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
        shutdownLauncherOnCleanExit_ =
            opts.contains("shutdownLauncherOnCleanExit") ? opts["shutdownLauncherOnCleanExit"].toBool() : false;
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
        if (exitCode == 0 && shutdownLauncherOnCleanExit_) {
            if (shutdownOnCleanExitHandler_) {
                shutdownOnCleanExitHandler_(id_, exitCode);
            }
            return;
        }
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
    bool shutdownLauncherOnCleanExit_{false};
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
    std::function<void(const SwString&, int)> shutdownOnCleanExitHandler_{};

    void drainPendingOutput_() {
        if (!process_) return;
        forwardChildChunk_(id_, /*fromStdErr=*/false, process_->read());
        forwardChildChunk_(id_, /*fromStdErr=*/true, process_->readStdErr());
    }
};

#include "SwLaunchController.h"

int main(int argc, char** argv) {
    swLaunchInstallCrashHandling();
    if (hasCrashTestArg_(argc, argv)) {
        swLaunchTriggerCrashTest();
    }

    SwCoreApplication app(argc, argv);
    SwDebug::setAppName("SwLaunch");
    SwDebug::setVersion(swLaunchVersion_());

    int versionExitCode = 0;
    if (tryPrintVersion_(app, versionExitCode)) {
        return versionExitCode;
    }

    int helpExitCode = 0;
    if (tryPrintHelp_(app, helpExitCode)) {
        return helpExitCode;
    }

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

    if (root.contains("control_api") && !root["control_api"].isObject()) {
        swError() << "field 'control_api' must be an object when present";
        return 2;
    }

    const SwJsonObject controlApi = getObjectOrEmpty_(root["control_api"]);
    SwString controlBind = controlApi.contains("bind") ? SwString(controlApi["bind"].toString()) : SwString("127.0.0.1");
    if (controlBind.isEmpty()) {
        controlBind = "127.0.0.1";
    }
    if (app.hasArgument("control_bind")) {
        controlBind = app.getArgument("control_bind", controlBind);
    }

    int controlPort = controlApi.contains("port") ? controlApi["port"].toInt() : 0;
    if (app.hasArgument("control_port")) {
        controlPort = app.getArgument("control_port", SwString::number(controlPort)).toInt();
    }
    if (controlPort < 0 || controlPort > 65535) {
        swError() << "control_port must be between 0 and 65535";
        return 2;
    }
    const SwString controlToken = app.getArgument("control_token", SwString());

    const bool preferWindowsExe = guessPreferWindowsExe_(root, baseDir);

#if defined(_WIN32)
    HANDLE launcherLifetimeJob = createLauncherLifetimeJob_();
#endif

    SwObject manager;
    auto* traceConfig = new SwLaunchTraceConfig(sys, resolvePath_(baseDir, "systemConfig"), baseDir, &manager);
    (void)traceConfig;

    auto* controller = new SwLaunchController(app,
                                              root,
                                              baseDir,
                                              configFileUsed,
                                              sys,
                                              durationMs,
                                              preferWindowsExe,
                                              controlBind,
                                              static_cast<uint16_t>(controlPort),
                                              controlToken,
                                              &manager);

    if (!controller->start(err)) {
        swError() << err;
        return 3;
    }

    if (durationMs > 0) {
        SwTimer::singleShot(durationMs, [controller, &app]() {
            swWarning() << "[launcher] stopping all processes";
            controller->stopAll();
            app.quit();
        });
    }

    const int exitCode = app.exec();

#if defined(_WIN32)
    if (launcherLifetimeJob) {
        ::CloseHandle(launcherLifetimeJob);
        launcherLifetimeJob = NULL;
    }
#endif

    return exitCode;
}
