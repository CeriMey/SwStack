#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwDir.h"
#include "SwEventLoop.h"
#include "SwFile.h"
#include "SwHttpClient.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwLaunchDeploySupport.h"
#include "SwProcess.h"
#include "SwStandardLocation.h"
#include "SwTimer.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static SwString joinTestPath_(const SwString& base, const SwString& leaf) {
    if (base.isEmpty()) return leaf;
    SwString normalizedBase = base;
    normalizedBase.replace("\\", "/");
    while (normalizedBase.endsWith("/")) normalizedBase.chop(1);
    SwString normalizedLeaf = leaf;
    normalizedLeaf.replace("\\", "/");
    while (normalizedLeaf.startsWith("/")) normalizedLeaf = normalizedLeaf.mid(1);
    return normalizedBase + "/" + normalizedLeaf;
}

static SwString absoluteCandidate_(const SwString& path) {
    return swDirPlatform().absolutePath(path);
}

static SwString uniqueToken_() {
    const long long now = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    return SwString("swlaunch_deploy_it_") + SwString::number(now);
}

static uint16_t findFreeLocalPort_() {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 19080;
    }

    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return 19080;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (::bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(sock);
        return 19080;
    }

    int len = sizeof(addr);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::closesocket(sock);
        return 19080;
    }

    const uint16_t port = ntohs(addr.sin_port);
    ::closesocket(sock);
    return port == 0 ? 19080 : port;
#else
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 19080;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (::bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return 19080;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(sock);
        return 19080;
    }

    const uint16_t port = ntohs(addr.sin_port);
    ::close(sock);
    return port == 0 ? 19080 : port;
#endif
}

static bool readBinaryFile_(const SwString& path, SwByteArray& out, SwString& errOut) {
    out.clear();
    errOut.clear();

    std::ifstream file(path.toStdString().c_str(), std::ios::binary);
    if (!file.is_open()) {
        errOut = SwString("failed to open binary file: ") + path;
        return false;
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!bytes.empty()) {
        out.append(bytes.data(), bytes.size());
    }
    return true;
}

static bool readTextFile_(const SwString& path, SwString& out, SwString& errOut) {
    out.clear();
    errOut.clear();
    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        errOut = SwString("failed to open text file: ") + path;
        return false;
    }
    out = file.readAll();
    file.close();
    return true;
}

static SwList<SwString> readNonEmptyLines_(const SwString& path) {
    SwList<SwString> out;
    if (!SwFile::isFile(path)) return out;

    SwFile file(path);
    if (!file.open(SwFile::Read)) return out;

    const SwString text = file.readAll();
    file.close();

    const SwList<SwString> lines = text.split('\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        SwString line = lines[i].trimmed();
        if (!line.isEmpty()) out.append(line);
    }
    return out;
}

class SwLaunchDeployIntegrationRunner : public SwObject {
    SW_OBJECT(SwLaunchDeployIntegrationRunner, SwObject)

public:
    SwLaunchDeployIntegrationRunner(SwCoreApplication* app, const SwString& selfPath, SwObject* parent = nullptr)
        : SwObject(parent)
        , app_(app)
        , selfPath_(absoluteCandidate_(selfPath))
        , controlToken_("integration-secret-token")
        , ownerKey_("node:tests/fixture") {
        bindLauncherProcess_();
    }

    ~SwLaunchDeployIntegrationRunner() override {
        cleanup_(false);
    }

    void start() {
        SwString err;
        const bool ok = run_(err);
        cleanup_(ok);

        if (!ok) {
            std::cout << "[SwLaunchDeployIntegrationTest] FAIL: " << err.toStdString() << std::endl;
            if (!workspaceRoot_.isEmpty()) {
                std::cout << "[SwLaunchDeployIntegrationTest] Preserved workspace: "
                          << workspaceRoot_.toStdString() << std::endl;
            }
            app_->exit(1);
            return;
        }

        std::cout << "[SwLaunchDeployIntegrationTest] PASS" << std::endl;
        app_->exit(0);
    }

private:
    bool run_(SwString& errOut) {
        std::cout << "[SwLaunchDeployIntegrationTest] resolve tools" << std::endl;
        if (!resolveToolPaths_(errOut)) return false;
        std::cout << "[SwLaunchDeployIntegrationTest] prepare workspace" << std::endl;
        if (!prepareWorkspace_(errOut)) return false;
        std::cout << "[SwLaunchDeployIntegrationTest] start launcher" << std::endl;
        if (!startLauncher_(errOut)) return false;
        std::cout << "[SwLaunchDeployIntegrationTest] wait control API" << std::endl;
        if (!waitForControlApi_(errOut)) return false;
        std::cout << "[SwLaunchDeployIntegrationTest] wait initial fixture" << std::endl;
        if (!waitForLogLineCount_(1, 10000, errOut)) return false;

        SwList<SwString> lines = readNonEmptyLines_(eventLogPath_);
        if (lines.size() != 1 || !lines[0].contains("version=V1;payload=alpha")) {
            errOut = SwString("unexpected initial fixture log: ") + (lines.isEmpty() ? SwString("<empty>") : lines[0]);
            return false;
        }

        SwJsonObject state;
        if (!getState_(state, errOut)) return false;
        if (!assertFixtureRunning_(state, errOut)) return false;

        SwJsonObject firstJob;
        std::cout << "[SwLaunchDeployIntegrationTest] first deploy" << std::endl;
        if (!postDeployAndWait_(fixtureV2Path_, "bravo", firstJob, errOut)) return false;
        if (!waitForLogLineCount_(2, 10000, errOut)) return false;

        lines = readNonEmptyLines_(eventLogPath_);
        if (lines.size() != 2 || !lines[1].contains("version=V2;payload=bravo")) {
            errOut = SwString("unexpected post-deploy fixture log: ") + (lines.size() > 1 ? lines[1] : SwString("<missing>"));
            return false;
        }

        if (getArraySize_(firstJob, "replacedFiles") != 2) {
            errOut = "first deploy should replace 2 files";
            return false;
        }
        if (getArraySize_(firstJob, "skippedFiles") != 0) {
            errOut = "first deploy should not skip files";
            return false;
        }

        SwString configText;
        if (!readTextFile_(launchConfigPath_, configText, errOut)) return false;
        if (configText.contains(controlToken_)) {
            errOut = "control token leaked into persisted launch config";
            return false;
        }

        if (!getState_(state, errOut)) return false;
        if (!assertFixtureRunning_(state, errOut)) return false;

        SwJsonObject secondJob;
        std::cout << "[SwLaunchDeployIntegrationTest] second deploy skip" << std::endl;
        if (!postDeployAndWait_(fixtureV2Path_, "bravo", secondJob, errOut)) return false;
        SwEventLoop::swsleep(1200);
        lines = readNonEmptyLines_(eventLogPath_);
        if (lines.size() != 2) {
            errOut = "second deploy should not restart the fixture";
            return false;
        }
        if (getArraySize_(secondJob, "replacedFiles") != 0) {
            errOut = "second deploy should not replace files";
            return false;
        }
        if (getArraySize_(secondJob, "skippedFiles") != 2) {
            errOut = "second deploy should skip 2 files";
            return false;
        }

        return true;
    }

    bool resolveToolPaths_(SwString& errOut) {
        errOut.clear();

        SwFile selfFile(selfPath_);
        const SwString selfDir = selfFile.getDirectory();
        const SwString exeSuffix =
#if defined(_WIN32)
            ".exe";
#else
            "";
#endif

        const auto firstExisting = [](const SwList<SwString>& candidates) -> SwString {
            for (size_t i = 0; i < candidates.size(); ++i) {
                const SwString absolute = absoluteCandidate_(candidates[i]);
                if (SwFile::isFile(absolute)) return absolute;
            }
            return SwString();
        };

        launchExePath_ = firstExisting({
            joinTestPath_(selfDir, SwString("../../Debug/SwLaunch") + exeSuffix),
            joinTestPath_(selfDir, SwString("../../SwLaunch") + exeSuffix),
            joinTestPath_(selfDir, SwString("../Debug/SwLaunch") + exeSuffix),
            joinTestPath_(selfDir, SwString("../SwLaunch") + exeSuffix)
        });

        fixtureV1Path_ = firstExisting({
            joinTestPath_(selfDir, SwString("SwLaunchDeployFixtureNodeV1") + exeSuffix),
            joinTestPath_(selfDir, SwString("../SwLaunchDeployFixtureNodeV1") + exeSuffix)
        });

        fixtureV2Path_ = firstExisting({
            joinTestPath_(selfDir, SwString("SwLaunchDeployFixtureNodeV2") + exeSuffix),
            joinTestPath_(selfDir, SwString("../SwLaunchDeployFixtureNodeV2") + exeSuffix)
        });

        if (launchExePath_.isEmpty()) {
            errOut = "failed to locate SwLaunch executable";
            return false;
        }
        if (fixtureV1Path_.isEmpty() || fixtureV2Path_.isEmpty()) {
            errOut = "failed to locate fixture node executables";
            return false;
        }
        return true;
    }

    bool prepareWorkspace_(SwString& errOut) {
        errOut.clear();

        workspaceRoot_ = joinTestPath_(SwStandardLocation::standardLocation(SwStandardLocationId::Temp), uniqueToken_());
        runtimeDir_ = joinTestPath_(workspaceRoot_, "runtime");
        launchConfigPath_ = joinTestPath_(workspaceRoot_, "launch.json");
        runtimeExePath_ = joinTestPath_(runtimeDir_, fixtureRuntimeExeName_());
        payloadPath_ = joinTestPath_(runtimeDir_, "payload.txt");
        eventLogPath_ = joinTestPath_(runtimeDir_, "event_log.txt");
        readyPath_ = joinTestPath_(runtimeDir_, "ready.txt");
        uploadPayloadPath_ = joinTestPath_(workspaceRoot_, "upload_payload.txt");
        controlPort_ = findFreeLocalPort_();

        if (!SwDir::mkpathAbsolute(runtimeDir_, false)) {
            errOut = SwString("failed to create runtime directory: ") + runtimeDir_;
            return false;
        }
        if (!SwFile::copy(fixtureV1Path_, runtimeExePath_, true)) {
            errOut = SwString("failed to copy V1 fixture into runtime: ") + runtimeExePath_;
            return false;
        }
        if (!swLaunchWriteTextFile_(payloadPath_, "alpha", &errOut)) return false;

        SwJsonObject params;
        params["event_log"] = SwJsonValue("event_log.txt");
        params["payload_file"] = SwJsonValue("payload.txt");
        params["ready_file"] = SwJsonValue("ready.txt");

        SwJsonObject node;
        node["executable"] = SwJsonValue(SwString("runtime/") + fixtureRuntimeExeName_());
        node["ns"] = SwJsonValue("tests");
        node["name"] = SwJsonValue("fixture");
        node["workingDirectory"] = SwJsonValue("runtime");
        node["params"] = SwJsonValue(params);

        SwJsonArray nodes;
        nodes.append(SwJsonValue(node));

        SwJsonObject root;
        root["sys"] = SwJsonValue("demo");
        root["nodes"] = SwJsonValue(nodes);
        root["containers"] = SwJsonValue(SwJsonArray());

        if (!swLaunchWriteJsonFileAtomic_(launchConfigPath_, root, &errOut)) {
            return false;
        }
        return true;
    }

    bool startLauncher_(SwString& errOut) {
        errOut.clear();
        launchProcessExited_ = false;
        launchProcessExitCode_ = 0;
        launchStdOut_.clear();
        launchStdErr_.clear();

        SwStringList args;
        args.append(SwString("--config_file=%1").arg(launchConfigPath_));
        args.append(SwString("--control_bind=127.0.0.1"));
        args.append(SwString("--control_port=%1").arg(SwString::number(controlPort_)));
        args.append(SwString("--control_token=%1").arg(controlToken_));
        args.append("--duration_ms=30000");

        ProcessFlags flags = ProcessFlags::NoFlag;
#if defined(_WIN32)
        flags |= ProcessFlags::CreateNoWindow;
#endif

        if (!launchProcess_.start(launchExePath_, args, flags, workspaceRoot_)) {
            errOut = SwString("failed to start SwLaunch: ") + launchExePath_;
            return false;
        }
        return true;
    }

    bool stopLauncher_(SwString& errOut) {
        errOut.clear();
        if (!launchProcess_.isOpen()) return true;

        launchProcess_.terminate();
        if (waitUntil_([this]() { return !launchProcess_.isOpen(); }, 4000)) {
            return true;
        }

        launchProcess_.kill();
        if (waitUntil_([this]() { return !launchProcess_.isOpen(); }, 3000)) {
            return true;
        }

        errOut = "failed to stop SwLaunch process";
        return false;
    }

    template <typename Predicate>
    bool waitUntil_(Predicate predicate, int timeoutMs, int stepMs = 100) {
        const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        while (true) {
            if (predicate()) return true;
            const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - startedAt)
                                            .count();
            if (elapsedMs >= timeoutMs) break;
            SwEventLoop::swsleep(stepMs);
        }
        return predicate();
    }

    bool waitForControlApi_(SwString& errOut) {
        errOut.clear();
        const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        while (true) {
            drainLauncherOutput_();
            if (launchProcessExited_) {
                errOut = SwString("SwLaunch exited before control API was ready: ") + launcherOutput_();
                return false;
            }

            SwJsonObject response;
            SwString requestError;
            if (requestJson_("GET", "/api/launch/state", SwByteArray(), SwString(), 200, response, requestError)) {
                return true;
            }

            const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - startedAt)
                                            .count();
            if (elapsedMs >= 10000) {
                errOut = requestError.isEmpty() ? SwString("timeout while waiting for control API") : requestError;
                return false;
            }
            SwEventLoop::swsleep(150);
        }
    }

    bool waitForLogLineCount_(size_t expectedCount, int timeoutMs, SwString& errOut) {
        errOut.clear();
        const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        while (true) {
            if (readNonEmptyLines_(eventLogPath_).size() >= expectedCount) {
                return true;
            }
            drainLauncherOutput_();
            if (launchProcessExited_) {
                errOut = SwString("SwLaunch exited while waiting for fixture log: ") + launcherOutput_();
                return false;
            }

            const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - startedAt)
                                            .count();
            if (elapsedMs >= timeoutMs) {
                break;
            }
            SwEventLoop::swsleep(100);
        }

        errOut = SwString("timeout while waiting for fixture log line count=") + SwString::number(static_cast<long long>(expectedCount));
        return false;
    }

    bool requestJson_(const SwString& method,
                      const SwString& path,
                      const SwByteArray& body,
                      const SwString& contentType,
                      int expectedStatus,
                      SwJsonObject& outObject,
                      SwString& errOut) {
        outObject = SwJsonObject();
        errOut.clear();

        SwHttpClient client;
        client.setRawHeader("Authorization", SwString("Bearer ") + controlToken_);

        bool finished = false;
        bool failed = false;
        int errorCode = 0;
        SwObject::connect(&client, &SwHttpClient::finished, [&](const SwByteArray&) {
            finished = true;
        });
        SwObject::connect(&client, &SwHttpClient::errorOccurred, [&](int code) {
            failed = true;
            errorCode = code;
        });

        const SwString url = baseUrl_() + path;
        bool requestStarted = false;
        if (method == "GET") {
            requestStarted = client.get(url);
        } else if (method == "POST") {
            requestStarted = client.post(url, body, contentType);
        } else if (method == "PUT") {
            requestStarted = client.put(url, body, contentType);
        } else {
            errOut = SwString("unsupported HTTP method in test: ") + method;
            return false;
        }

        if (!requestStarted) {
            errOut = SwString("failed to start HTTP request: ") + method + " " + path;
            return false;
        }

        const bool completed = waitUntil_([&finished, &failed]() {
            return finished || failed;
        }, 5000, 25);
        if (!completed) {
            client.abort();
            errOut = SwString("HTTP request timeout: ") + method + " " + path;
            return false;
        }
        if (failed) {
            errOut = SwString("HTTP client error: ") + SwString::number(errorCode);
            return false;
        }
        if (client.statusCode() != expectedStatus) {
            errOut = SwString("unexpected HTTP status ") + SwString::number(client.statusCode()) +
                     SwString(" for ") + method + " " + path + ": " + client.responseBodyAsString();
            return false;
        }

        SwString parseError;
        SwJsonDocument document;
        if (!document.loadFromJson(client.responseBodyAsString(), parseError) || !document.isObject()) {
            errOut = SwString("invalid JSON response for ") + method + " " + path + ": " + parseError;
            return false;
        }
        outObject = document.object();
        return true;
    }

    bool getState_(SwJsonObject& outObject, SwString& errOut) {
        return requestJson_("GET", "/api/launch/state", SwByteArray(), SwString(), 200, outObject, errOut);
    }

    bool assertFixtureRunning_(const SwJsonObject& stateObject, SwString& errOut) const {
        errOut.clear();
        if (!stateObject.contains("runtime") || !stateObject["runtime"].isObject()) {
            errOut = "state payload missing runtime";
            return false;
        }

        const SwJsonObject runtime = stateObject["runtime"].toObject();
        if (!runtime.contains("units") || !runtime["units"].isArray()) {
            errOut = "state payload missing runtime.units";
            return false;
        }

        const SwJsonArray units = runtime["units"].toArray();
        for (size_t i = 0; i < units.size(); ++i) {
            if (!units[i].isObject()) continue;
            const SwJsonObject unit = units[i].toObject();
            if (SwString(unit["ownerKey"].toString()) == ownerKey_) {
                if (!unit.contains("running") || !unit["running"].toBool()) {
                    errOut = "fixture unit is not running";
                    return false;
                }
                return true;
            }
        }

        errOut = "fixture unit not found in state payload";
        return false;
    }

    int getArraySize_(const SwJsonObject& object, const SwString& key) const {
        if (!object.contains(key) || !object[key].isArray()) return 0;
        return static_cast<int>(object[key].toArray().size());
    }

    bool postDeployAndWait_(const SwString& binaryUploadPath,
                            const SwString& payloadText,
                            SwJsonObject& outJob,
                            SwString& errOut) {
        outJob = SwJsonObject();
        errOut.clear();

        if (!swLaunchWriteTextFile_(uploadPayloadPath_, payloadText, &errOut)) {
            return false;
        }

        const SwString binarySha = swLaunchChecksumForFile_(binaryUploadPath);
        const SwString payloadSha = swLaunchChecksumForFile_(uploadPayloadPath_);
        if (binarySha.isEmpty() || payloadSha.isEmpty()) {
            errOut = "failed to calculate upload checksums";
            return false;
        }

        SwJsonObject exeArtifact;
        exeArtifact["partName"] = SwJsonValue("binary");
        exeArtifact["ownerKey"] = SwJsonValue(ownerKey_);
        exeArtifact["relativePath"] = SwJsonValue(fixtureRuntimeExeName_());
        exeArtifact["sha256"] = SwJsonValue(binarySha);

        SwJsonObject payloadArtifact;
        payloadArtifact["partName"] = SwJsonValue("payload");
        payloadArtifact["ownerKey"] = SwJsonValue(ownerKey_);
        payloadArtifact["relativePath"] = SwJsonValue("payload.txt");
        payloadArtifact["sha256"] = SwJsonValue(payloadSha);

        SwJsonArray artifacts;
        artifacts.append(SwJsonValue(exeArtifact));
        artifacts.append(SwJsonValue(payloadArtifact));

        SwFile configFile(launchConfigPath_);
        SwString configText;
        if (!readTextFile_(launchConfigPath_, configText, errOut)) return false;
        SwJsonDocument currentConfig;
        if (!currentConfig.loadFromJson(configText, errOut) || !currentConfig.isObject()) {
            if (errOut.isEmpty()) errOut = "failed to parse current launch config";
            return false;
        }

        SwJsonObject manifest;
        manifest["formatVersion"] = SwJsonValue("1");
        manifest["desiredState"] = SwJsonValue(currentConfig.object());
        manifest["artifacts"] = SwJsonValue(artifacts);

        const SwString boundary = SwString("----swlaunchdeploy") + uniqueToken_();
        SwByteArray body;
        if (!appendMultipartFilePart_(body, boundary, "manifest", "manifest.json", "application/json",
                                      SwByteArray(SwJsonDocument(manifest).toJson(SwJsonDocument::JsonFormat::Compact).toStdString()), errOut)) {
            return false;
        }

        SwByteArray binaryBytes;
        if (!readBinaryFile_(binaryUploadPath, binaryBytes, errOut)) return false;
        if (!appendMultipartFilePart_(body, boundary, "binary", "fixture-node.bin", "application/octet-stream", binaryBytes, errOut)) {
            return false;
        }

        SwByteArray payloadBytes;
        if (!readBinaryFile_(uploadPayloadPath_, payloadBytes, errOut)) return false;
        if (!appendMultipartFilePart_(body, boundary, "payload", "payload.txt", "text/plain", payloadBytes, errOut)) {
            return false;
        }

        body.append("--");
        body.append(boundary.toStdString());
        body.append("--\r\n");

        SwJsonObject response;
        if (!requestJson_("POST",
                          "/api/launch/deploy",
                          body,
                          SwString("multipart/form-data; boundary=") + boundary,
                          202,
                          response,
                          errOut)) {
            return false;
        }

        const SwString jobId = response.contains("jobId") ? SwString(response["jobId"].toString()) : SwString();
        if (jobId.isEmpty()) {
            errOut = "deploy response missing jobId";
            return false;
        }

        return waitForJob_(jobId, outJob, errOut);
    }

    bool appendMultipartFilePart_(SwByteArray& body,
                                  const SwString& boundary,
                                  const SwString& partName,
                                  const SwString& fileName,
                                  const SwString& contentType,
                                  const SwByteArray& bytes,
                                  SwString& errOut) const {
        (void)errOut;
        body.append("--");
        body.append(boundary.toStdString());
        body.append("\r\n");
        body.append("Content-Disposition: form-data; name=\"");
        body.append(partName.toStdString());
        body.append("\"; filename=\"");
        body.append(fileName.toStdString());
        body.append("\"\r\n");
        body.append("Content-Type: ");
        body.append(contentType.toStdString());
        body.append("\r\n\r\n");
        body.append(bytes);
        body.append("\r\n");
        return true;
    }

    bool waitForJob_(const SwString& jobId, SwJsonObject& outJob, SwString& errOut) {
        errOut.clear();
        outJob = SwJsonObject();

        const bool ok = waitUntil_([this, &jobId, &outJob, &errOut]() {
            drainLauncherOutput_();
            if (launchProcessExited_) {
                errOut = SwString("SwLaunch exited while waiting for deploy job: ") + launcherOutput_();
                return true;
            }

            SwJsonObject job;
            SwString requestError;
            if (!requestJson_("GET",
                              SwString("/api/launch/deploy/") + jobId,
                              SwByteArray(),
                              SwString(),
                              200,
                              job,
                              requestError)) {
                errOut = requestError;
                return false;
            }

            const SwString status = job.contains("status") ? SwString(job["status"].toString()) : SwString();
            if (status == "failed") {
                errOut = SwString("deploy job failed: ") + jobToString_(job);
                return false;
            }
            if (status == "succeeded") {
                outJob = job;
                return true;
            }
            return false;
        }, 15000, 150);

        if (!ok && outJob.isEmpty() && errOut.isEmpty()) {
            errOut = SwString("timeout while waiting for deploy job: ") + jobId;
        }
        return !outJob.isEmpty();
    }

    void bindLauncherProcess_() {
        if (launcherBindingsInstalled_) return;
        launcherBindingsInstalled_ = true;

        SwObject::connect(&launchProcess_, SIGNAL(readyReadStdOut), std::function<void()>([this]() {
            drainLauncherOutput_();
        }));
        SwObject::connect(&launchProcess_, SIGNAL(readyReadStdErr), std::function<void()>([this]() {
            drainLauncherOutput_();
        }));
        SwObject::connect(&launchProcess_, SIGNAL(processTerminated), std::function<void(int)>([this](int exitCode) {
            drainLauncherOutput_();
            launchProcessExited_ = true;
            launchProcessExitCode_ = exitCode;
            if (launchProcess_.isOpen()) {
                launchProcess_.close();
            }
        }));
    }

    void drainLauncherOutput_() {
        const SwString stdoutChunk = launchProcess_.read();
        if (!stdoutChunk.isEmpty()) {
            launchStdOut_ += stdoutChunk;
        }

        const SwString stderrChunk = launchProcess_.readStdErr();
        if (!stderrChunk.isEmpty()) {
            launchStdErr_ += stderrChunk;
        }
    }

    SwString jobToString_(const SwJsonObject& job) const {
        return SwJsonDocument(job).toJson(SwJsonDocument::JsonFormat::Compact);
    }

    SwString baseUrl_() const {
        return SwString("http://127.0.0.1:") + SwString::number(controlPort_);
    }

    SwString launcherOutput_() {
        drainLauncherOutput_();
        return SwString("exitCode=") + SwString::number(launchProcessExitCode_) +
               SwString(" stdout=") + launchStdOut_ +
               SwString(" stderr=") + launchStdErr_;
    }

    SwString fixtureRuntimeExeName_() const {
#if defined(_WIN32)
        return "fixture-node.exe";
#else
        return "fixture-node";
#endif
    }

    void cleanup_(bool success) {
        SwString ignored;
        (void)stopLauncher_(ignored);
        if (success && !workspaceRoot_.isEmpty()) {
            (void)SwDir::removeRecursively(workspaceRoot_);
            workspaceRoot_.clear();
        }
    }

private:
    SwCoreApplication* app_{nullptr};
    SwString selfPath_;
    SwString workspaceRoot_;
    SwString runtimeDir_;
    SwString launchConfigPath_;
    SwString runtimeExePath_;
    SwString payloadPath_;
    SwString eventLogPath_;
    SwString readyPath_;
    SwString uploadPayloadPath_;
    SwString launchExePath_;
    SwString fixtureV1Path_;
    SwString fixtureV2Path_;
    SwString controlToken_;
    SwString ownerKey_;
    uint16_t controlPort_{0};
    SwProcess launchProcess_;
    bool launcherBindingsInstalled_{false};
    bool launchProcessExited_{false};
    int launchProcessExitCode_{0};
    SwString launchStdOut_;
    SwString launchStdErr_;
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    const SwString selfPath = (argc > 0 && argv && argv[0]) ? SwString(argv[0]) : SwString("SwLaunchDeployIntegrationTest");
    SwLaunchDeployIntegrationRunner runner(&app, selfPath);
    SwTimer::singleShot(0, [&runner]() { runner.start(); });
    return app.exec();
}
