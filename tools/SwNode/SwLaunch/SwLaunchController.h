#pragma once

struct SwLaunchManagedUnitPlan {
    SwString kind;
    SwString ownerKey;
    SwString runtimeId;
    SwJsonObject spec;
    SwString executablePath;
    SwString workingDirectory;
    SwString artifactRoot;
};

struct SwLaunchManagedUnitRuntime {
    SwLaunchManagedUnitPlan plan;
    LaunchContainerProcess* containerProcess{nullptr};
    LaunchNodeProcess* nodeProcess{nullptr};

    bool isRunning() const {
        if (containerProcess) return containerProcess->isRunning();
        if (nodeProcess) return nodeProcess->isRunning();
        return false;
    }

    void stop() {
        if (containerProcess) containerProcess->stop();
        if (nodeProcess) nodeProcess->stop();
    }

    void forceKill() {
        if (containerProcess) containerProcess->forceKill();
        if (nodeProcess) nodeProcess->forceKill();
    }

    void deleteLater() {
        if (containerProcess) containerProcess->deleteLater();
        if (nodeProcess) nodeProcess->deleteLater();
    }
};

struct SwLaunchArtifactPlan {
    SwString partName;
    SwString ownerKey;
    SwString relativePath;
    SwString sha256;
    SwString stagingPath;
    SwString destinationPath;
    SwString currentSha256;
    bool skipped{false};
};

struct SwLaunchMutationSummary {
    SwList<SwString> impactedUnits;
    SwList<SwString> replacedFiles;
    SwList<SwString> skippedFiles;
};

struct SwLaunchDeployJob {
    SwString jobId;
    SwString deploymentId;
    SwString status;
    SwString phase;
    SwString stagingRoot;
    SwJsonObject desiredState;
    SwList<SwLaunchArtifactPlan> artifacts;
    SwList<SwString> errors;
    SwList<SwString> impactedUnits;
    SwList<SwString> replacedFiles;
    SwList<SwString> skippedFiles;
};

struct SwLaunchRollbackFileAction {
    SwString destinationPath;
    SwString backupPath;
    bool destinationExisted{false};
};

struct SwLaunchLogEntry {
    long long seq{0};
    SwString line;
};

struct SwLaunchLogStreamClient {
    SwAbstractSocket* socket{nullptr};
    SwTimer* heartbeatTimer{nullptr};
};

static void swLaunchAppendUnique_(SwList<SwString>& values, const SwString& value) {
    if (value.isEmpty()) return;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] == value) return;
    }
    values.append(value);
}

static SwJsonArray swLaunchStringListToJsonArray_(const SwList<SwString>& values) {
    SwJsonArray out;
    for (size_t i = 0; i < values.size(); ++i) {
        out.append(SwJsonValue(values[i]));
    }
    return out;
}

static SwJsonObject swLaunchSanitizeStateForApi_(SwJsonObject root) {
    root.remove("control_token");
    if (root.contains("control_api")) {
        if (root["control_api"].isObject()) {
            SwJsonObject controlApi(root["control_api"].toObject());
            controlApi.remove("token");
            controlApi.remove("control_token");
            root["control_api"] = SwJsonValue(controlApi);
        } else {
            root.remove("control_api");
        }
    }
    return root;
}

static SwJsonObject swLaunchErrorPayload_(const SwString& errorText) {
    SwJsonObject out;
    out["ok"] = SwJsonValue(false);
    out["error"] = SwJsonValue(errorText);
    return out;
}

static SwJsonObject swLaunchOkPayload_() {
    SwJsonObject out;
    out["ok"] = SwJsonValue(true);
    return out;
}

static bool swLaunchReadTextFile_(const SwString& path, SwString& out, SwString& errOut) {
    out.clear();
    errOut.clear();

    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        errOut = SwString("failed to open file: ") + path;
        return false;
    }

    out = file.readAll();
    file.close();
    return true;
}

static SwString swLaunchOwnerKey_(const SwString& kind, const SwString& ns, const SwString& name) {
    return kind + ":" + ns + "/" + name;
}

static bool swLaunchResolveManagedUnitPlan_(const SwString& kind,
                                            const SwJsonObject& spec,
                                            const SwString& baseDir,
                                            const SwString& defaultSys,
                                            bool preferWindowsExe,
                                            SwLaunchManagedUnitPlan& out,
                                            SwString& errOut) {
    errOut.clear();

    if (kind != "container" && kind != "node") {
        errOut = SwString("unsupported unit kind: ") + kind;
        return false;
    }

    SwString sys = defaultSys;
    if (spec.contains("sys")) sys = SwString(spec["sys"].toString());
    if (!sys.isEmpty() && sys != defaultSys) {
        errOut = SwString("unit sys mismatch for kind=") + kind;
        return false;
    }

    SwString ns;
    if (spec.contains("ns")) ns = SwString(spec["ns"].toString());
    if (ns.isEmpty() && spec.contains("namespace")) ns = SwString(spec["namespace"].toString());

    SwString name;
    if (spec.contains("name")) name = SwString(spec["name"].toString());
    if (name.isEmpty() && spec.contains("object")) name = SwString(spec["object"].toString());

    if (ns.isEmpty() || name.isEmpty()) {
        errOut = SwString("unit identity requires ns/name for kind=") + kind;
        return false;
    }

    const SwString exeSpec = spec.contains("executable") ? SwString(spec["executable"].toString()) : SwString();
    if (exeSpec.isEmpty()) {
        errOut = SwString("missing executable for ") + swLaunchOwnerKey_(kind, ns, name);
        return false;
    }

    const SwString exePath = resolveExecutablePath_(baseDir, exeSpec, preferWindowsExe);
    if (exePath.isEmpty()) {
        errOut = SwString("failed to resolve executable for ") + swLaunchOwnerKey_(kind, ns, name);
        return false;
    }

    SwString workingDir;
    if (spec.contains("workingDirectory")) {
        workingDir = resolvePath_(baseDir, SwString(spec["workingDirectory"].toString()));
    }
    if (workingDir.isEmpty()) {
        SwFile exeFile(exePath);
        workingDir = exeFile.getDirectory();
    }
    if (!workingDir.isEmpty()) {
        workingDir = swDirPlatform().absolutePath(workingDir);
    }

    SwFile exeFile(exePath);
    const SwString fallbackRoot = exeFile.getDirectory();

    out.kind = kind;
    out.ownerKey = swLaunchOwnerKey_(kind, ns, name);
    out.runtimeId = ns + "/" + name;
    out.spec = spec;
    out.executablePath = exePath;
    out.workingDirectory = workingDir;
    out.artifactRoot = !workingDir.isEmpty() ? workingDir : fallbackRoot;
    return true;
}

static bool swLaunchBuildPlanSet_(const SwJsonObject& root,
                                  const SwString& baseDir,
                                  const SwString& defaultSys,
                                  bool preferWindowsExe,
                                  SwMap<SwString, SwLaunchManagedUnitPlan>& outPlans,
                                  SwList<SwString>& outOrder,
                                  SwString& errOut) {
    outPlans.clear();
    outOrder.clear();
    errOut.clear();

    if (root.contains("containers") && !root["containers"].isArray()) {
        errOut = "field 'containers' must be an array";
        return false;
    }
    if (root.contains("nodes") && !root["nodes"].isArray()) {
        errOut = "field 'nodes' must be an array";
        return false;
    }

    SwMap<SwString, bool> runtimeIds;

    const auto appendPlans = [&](const SwString& kind, const SwJsonArray& array) -> bool {
        for (size_t i = 0; i < array.size(); ++i) {
            if (!array[i].isObject()) {
                errOut = SwString("every ") + kind + " entry must be an object";
                return false;
            }

            SwLaunchManagedUnitPlan plan;
            if (!swLaunchResolveManagedUnitPlan_(kind,
                                                 array[i].toObject(),
                                                 baseDir,
                                                 defaultSys,
                                                 preferWindowsExe,
                                                 plan,
                                                 errOut)) {
                return false;
            }

            if (outPlans.contains(plan.ownerKey)) {
                errOut = SwString("duplicate managed unit key: ") + plan.ownerKey;
                return false;
            }
            if (runtimeIds.contains(plan.runtimeId)) {
                errOut = SwString("duplicate runtime id: ") + plan.runtimeId;
                return false;
            }

            runtimeIds[plan.runtimeId] = true;
            outPlans[plan.ownerKey] = plan;
            outOrder.append(plan.ownerKey);
        }
        return true;
    };

    if (!appendPlans("container", getArrayOrEmpty_(root["containers"]))) return false;
    if (!appendPlans("node", getArrayOrEmpty_(root["nodes"]))) return false;
    return true;
}

class SwLaunchController : public SwObject {
    SW_OBJECT(SwLaunchController, SwObject)

public:
    SwLaunchController(SwCoreApplication& app,
                       const SwJsonObject& desiredRoot,
                       const SwString& baseDir,
                       const SwString& configFilePath,
                       const SwString& sys,
                       int durationMs,
                       bool preferWindowsExe,
                       const SwString& controlBind,
                       uint16_t controlPort,
                       const SwString& controlToken,
                       SwObject* parent = nullptr)
        : SwObject(parent)
        , app_(app)
        , desiredRoot_(swLaunchSanitizeStateForApi_(desiredRoot))
        , baseDir_(baseDir)
        , configFilePath_(configFilePath)
        , systemId_(sys)
        , durationMs_(durationMs)
        , preferWindowsExe_(preferWindowsExe)
        , controlBind_(controlBind.isEmpty() ? SwString("127.0.0.1") : controlBind)
        , controlPort_(controlPort)
        , controlToken_(controlToken)
        , controlServer_(this) {
        if (controlPort_ > 0) {
            installLogListener_();
        }
        configureControlApi_();
    }

    ~SwLaunchController() override {
        cleanupLogListener_();
        closeAllLogStreams_();
    }

    bool start(SwString& errOut) {
        errOut.clear();

        SwMap<SwString, SwLaunchManagedUnitPlan> plans;
        SwList<SwString> order;
        if (!swLaunchBuildPlanSet_(desiredRoot_, baseDir_, systemId_, preferWindowsExe_, plans, order, errOut)) {
            return false;
        }

        if (!startUnits_(plans, order, order, errOut)) {
            stopAll();
            return false;
        }

        if (!startControlListener_(errOut)) {
            stopAll();
            return false;
        }

        return true;
    }

    void stopAll() {
        const SwList<SwString> keys = activeUnits_.keys();
        for (size_t i = 0; i < keys.size(); ++i) {
            if (activeUnits_.contains(keys[i])) {
                activeUnits_[keys[i]].stop();
            }
        }
    }

private:
    using SwLaunchContextHandler = std::function<void(SwHttpContext&)>;

    void configureControlApi_() {
        SwHttpLimits limits = controlServer_.limits();
        limits.maxBodyBytes = 512u * 1024u * 1024u;
        limits.maxMultipartFieldBytes = 8u * 1024u * 1024u;
        limits.maxMultipartParts = 128;
        limits.enableMultipartFileStreaming = true;
        limits.multipartTempDirectory = joinPath_(baseDir_, "_runlogs/_http_multipart");
        controlServer_.setLimits(limits);

        controlServer_.addRoute("GET", "/api/launch/help", [this](const SwHttpRequest& request) {
            return handleRoute_(request, [this](SwHttpContext& ctx) {
                ctx.json(SwJsonValue(buildHelpPayload_(SwString())));
            });
        });

        controlServer_.addRoute("GET", "/api/launch/help/:topic", [this](const SwHttpRequest& request) {
            return handleRoute_(request, [this](SwHttpContext& ctx) {
                const SwString topic = ctx.pathValue("topic");
                if (swLaunchHelpForTopic_(topic).isEmpty()) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_(SwString("unknown help topic: ") + topic)), 404);
                    return;
                }
                ctx.json(SwJsonValue(buildHelpPayload_(topic)));
            });
        });

        controlServer_.addRoute("GET", "/api/launch/state", [this](const SwHttpRequest& request) {
            return handleAuthorizedRoute_(request, [this](SwHttpContext& ctx) {
                ctx.json(SwJsonValue(buildStatePayload_()));
            });
        });

        controlServer_.addRoute("GET", "/api/launch/logs/stream", [this](const SwHttpRequest& request) {
            return handleAuthorizedRoute_(request, [this](SwHttpContext& ctx) {
                openLogStream_(ctx);
            });
        });

        controlServer_.addRoute("PUT", "/api/launch/state", [this](const SwHttpRequest& request) {
            return handleAuthorizedRoute_(request, [this](SwHttpContext& ctx) {
                if (!ensureMutationAllowed_(ctx)) return;
                if (mutationInProgress_) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_("another mutation is already running")), 409);
                    return;
                }

                SwJsonDocument document;
                SwString parseError;
                if (!ctx.parseJsonBody(document, parseError) || !document.isObject()) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_(SwString("invalid JSON object: ") + parseError)), 400);
                    return;
                }

                mutationInProgress_ = true;
                SwLaunchMutationSummary summary;
                SwString err;
                int statusCode = 500;
                const bool ok = applyDesiredState_(document.object(), SwList<SwLaunchArtifactPlan>(), summary, err, statusCode);
                mutationInProgress_ = false;

                if (!ok) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_(err)), statusCode);
                    return;
                }

                SwJsonObject out = swLaunchOkPayload_();
                out["impactedUnits"] = SwJsonValue(swLaunchStringListToJsonArray_(summary.impactedUnits));
                out["replacedFiles"] = SwJsonValue(swLaunchStringListToJsonArray_(summary.replacedFiles));
                out["skippedFiles"] = SwJsonValue(swLaunchStringListToJsonArray_(summary.skippedFiles));
                out["desiredState"] = SwJsonValue(swLaunchSanitizeStateForApi_(desiredRoot_));
                ctx.json(SwJsonValue(out));
            });
        });

        controlServer_.addRoute("POST", "/api/launch/deploy", [this](const SwHttpRequest& request) {
            return handleAuthorizedRoute_(request, [this](SwHttpContext& ctx) {
                if (!ensureMutationAllowed_(ctx)) return;
                if (mutationInProgress_) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_("another mutation is already running")), 409);
                    return;
                }
                if (!ctx.request().isMultipartFormData) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_("multipart/form-data is required")), 400);
                    return;
                }

                SwLaunchDeployJob job;
                SwString err;
                if (!prepareDeployJob_(ctx, job, err)) {
                    cleanupJobStaging_(job);
                    ctx.json(SwJsonValue(swLaunchErrorPayload_(err)), 400);
                    return;
                }

                mutationInProgress_ = true;
                deployJobs_[job.jobId] = job;

                SwJsonObject out = swLaunchOkPayload_();
                out["jobId"] = SwJsonValue(job.jobId);
                out["status"] = SwJsonValue(job.status);
                out["phase"] = SwJsonValue(job.phase);
                ctx.json(SwJsonValue(out), 202);

                const SwString jobId = job.jobId;
                SwTimer::singleShot(0, [this, jobId]() {
                    runDeployJob_(jobId);
                });
            });
        });

        controlServer_.addRoute("GET", "/api/launch/deploy/:jobId", [this](const SwHttpRequest& request) {
            return handleAuthorizedRoute_(request, [this](SwHttpContext& ctx) {
                const SwString jobId = ctx.pathValue("jobId");
                if (jobId.isEmpty() || !deployJobs_.contains(jobId)) {
                    ctx.json(SwJsonValue(swLaunchErrorPayload_("deploy job not found")), 404);
                    return;
                }
                ctx.json(SwJsonValue(jobToJson_(deployJobs_[jobId])));
            });
        });
    }

    SwHttpResponse handleRoute_(const SwHttpRequest& request, const SwLaunchContextHandler& handler) {
        SwHttpContext ctx(request);
        if (handler) {
            handler(ctx);
        }
        if (!ctx.handled()) {
            ctx.json(SwJsonValue(swLaunchErrorPayload_("route handler produced no response")), 500);
        }
        return ctx.takeResponse();
    }

    SwHttpResponse handleAuthorizedRoute_(const SwHttpRequest& request, const SwLaunchContextHandler& handler) {
        SwHttpContext ctx(request);
        if (!authorizeRequest_(ctx)) {
            return ctx.takeResponse();
        }
        if (handler) {
            handler(ctx);
        }
        if (!ctx.handled()) {
            ctx.json(SwJsonValue(swLaunchErrorPayload_("route handler produced no response")), 500);
        }
        return ctx.takeResponse();
    }

    bool authorizeRequest_(SwHttpContext& ctx) const {
        if (controlToken_.isEmpty()) {
            ctx.json(SwJsonValue(swLaunchErrorPayload_("control token not configured")), 500);
            return false;
        }

        const SwString authorization = ctx.headerValue("authorization").trimmed();
        const SwString authorizationLower = authorization.toLower();
        if (!authorizationLower.startsWith("bearer ")) {
            ctx.response().headers["www-authenticate"] = "Bearer";
            ctx.json(SwJsonValue(swLaunchErrorPayload_("missing bearer token")), 401);
            return false;
        }

        const SwString providedToken = authorization.mid(7).trimmed();
        if (providedToken != controlToken_) {
            ctx.response().headers["www-authenticate"] = "Bearer";
            ctx.json(SwJsonValue(swLaunchErrorPayload_("invalid bearer token")), 401);
            return false;
        }

        return true;
    }

    static SwString normalizeLogLine_(SwString line) {
        while (line.endsWith("\n") || line.endsWith("\r")) {
            line.chop(1);
        }
        return line;
    }

    int parseRequestedBacklog_(const SwHttpContext& ctx) const {
        int backlog = 32;
        const SwString requested = ctx.queryValue("backlog").trimmed();
        if (!requested.isEmpty()) {
            backlog = requested.toInt();
        }
        if (backlog < 0) backlog = 0;
        if (backlog > maxLogBacklogEntries_) backlog = maxLogBacklogEntries_;
        return backlog;
    }

    SwString buildSseEvent_(const SwString& eventName, const SwString& payload, long long eventId = 0) const {
        SwString out;
        if (eventId > 0) {
            out += "id: " + SwString::number(eventId) + "\n";
        }
        if (!eventName.isEmpty()) {
            out += "event: " + eventName + "\n";
        }

        const SwList<SwString> lines = payload.split('\n');
        if (lines.isEmpty()) {
            out += "data:\n\n";
            return out;
        }

        for (size_t i = 0; i < lines.size(); ++i) {
            out += "data: " + lines[i] + "\n";
        }
        out += "\n";
        return out;
    }

    void sendLogEntryToSocket_(SwAbstractSocket* socket, const SwLaunchLogEntry& entry) {
        if (!socket) return;

        SwJsonObject payload;
        payload["seq"] = SwJsonValue(SwString::number(entry.seq));
        payload["line"] = SwJsonValue(entry.line);
        const SwString eventText = buildSseEvent_("log",
                                                  SwJsonDocument(payload).toJson(SwJsonDocument::JsonFormat::Compact),
                                                  entry.seq);
        if (!socket->write(eventText)) {
            removeLogStreamSocket_(socket);
        }
    }

    void broadcastLogEntry_(const SwLaunchLogEntry& entry) {
        const SwList<SwLaunchLogStreamClient> clients = logStreamClients_;
        for (size_t i = 0; i < clients.size(); ++i) {
            if (!clients[i].socket) continue;
            sendLogEntryToSocket_(clients[i].socket, entry);
        }
    }

    void appendLogLine_(SwString line) {
        line = normalizeLogLine_(line);
        if (line.isEmpty()) return;

        SwLaunchLogEntry entry;
        entry.seq = nextLogSequence_++;
        entry.line = line;
        recentLogEntries_.append(entry);
        while (static_cast<int>(recentLogEntries_.size()) > maxLogBacklogEntries_) {
            recentLogEntries_.removeFirst();
        }

        if (!logStreamClients_.isEmpty()) {
            broadcastLogEntry_(entry);
        }
    }

    void installLogListener_() {
        if (debugLineListenerId_ > 0) {
            return;
        }

        SwPointer<SwLaunchController> self(this);
        debugLineListenerId_ = SwDebug::addLineListener([self](const std::string& stdLine) {
            if (!self) return;

            const SwString line(stdLine);
            self->app_.postEvent([self, line]() {
                if (!self) return;
                self->appendLogLine_(line);
            });
        });
    }

    void cleanupLogListener_() {
        if (debugLineListenerId_ > 0) {
            SwDebug::removeLineListener(debugLineListenerId_);
            debugLineListenerId_ = 0;
        }
    }

    bool hasLogStreamSocket_(SwAbstractSocket* socket) const {
        if (!socket) return false;
        for (size_t i = 0; i < logStreamClients_.size(); ++i) {
            if (logStreamClients_[i].socket == socket) {
                return true;
            }
        }
        return false;
    }

    void removeLogStreamSocket_(SwAbstractSocket* socket, bool closeSocket = true) {
        if (!socket) return;

        for (size_t i = 0; i < logStreamClients_.size(); ++i) {
            if (logStreamClients_[i].socket != socket) continue;

            SwLaunchLogStreamClient client = logStreamClients_[i];
            logStreamClients_.removeAt(i);

            if (client.heartbeatTimer) {
                client.heartbeatTimer->stop();
            }

            socket->disconnectAllSlots();
            if (closeSocket && socket->isOpen()) {
                socket->close();
            }
            socket->deleteLater();
            return;
        }
    }

    void closeAllLogStreams_() {
        while (!logStreamClients_.isEmpty()) {
            SwAbstractSocket* socket = logStreamClients_.first().socket;
            removeLogStreamSocket_(socket);
        }
    }

    void attachLogStreamSocket_(SwAbstractSocket* socket, int backlogCount) {
        if (!socket) {
            return;
        }

        socket->setParent(this);
        SwObject::connect(socket, &SwAbstractSocket::disconnected, this, [this, socket]() {
            removeLogStreamSocket_(socket, false);
        });
        SwObject::connect(socket, &SwAbstractSocket::errorOccurred, this, [this, socket](int) {
            removeLogStreamSocket_(socket, false);
        });
        SwObject::connect(socket, &SwIODevice::readyRead, this, [socket]() {
            while (true) {
                const SwString ignored = socket->read();
                if (ignored.isEmpty()) break;
            }
        });

        SwLaunchLogStreamClient client;
        client.socket = socket;
        client.heartbeatTimer = new SwTimer(logHeartbeatIntervalMs_, socket);
        SwObject::connect(client.heartbeatTimer, &SwTimer::timeout, this, [this, socket]() {
            if (!hasLogStreamSocket_(socket)) {
                return;
            }
            if (!socket->write(": keepalive\n\n")) {
                removeLogStreamSocket_(socket);
            }
        });
        client.heartbeatTimer->start();
        logStreamClients_.append(client);

        SwString responseText;
        responseText += "HTTP/1.1 200 OK\r\n";
        responseText += "content-type: text/event-stream; charset=utf-8\r\n";
        responseText += "cache-control: no-cache\r\n";
        responseText += "connection: keep-alive\r\n";
        responseText += "x-accel-buffering: no\r\n";
        responseText += "\r\n";
        if (!socket->write(responseText)) {
            removeLogStreamSocket_(socket);
            return;
        }

        SwJsonObject readyPayload;
        readyPayload["mode"] = SwJsonValue("sse");
        readyPayload["heartbeatMs"] = SwJsonValue(logHeartbeatIntervalMs_);
        readyPayload["backlogCount"] = SwJsonValue(backlogCount);
        const SwString readyEvent = buildSseEvent_("ready",
                                                   SwJsonDocument(readyPayload).toJson(SwJsonDocument::JsonFormat::Compact));
        if (!socket->write(readyEvent)) {
            removeLogStreamSocket_(socket);
            return;
        }

        int startIndex = static_cast<int>(recentLogEntries_.size()) - backlogCount;
        if (startIndex < 0) startIndex = 0;
        for (int i = startIndex; i < static_cast<int>(recentLogEntries_.size()); ++i) {
            if (!hasLogStreamSocket_(socket)) {
                return;
            }
            sendLogEntryToSocket_(socket, recentLogEntries_[static_cast<size_t>(i)]);
        }
    }

    void openLogStream_(SwHttpContext& ctx) {
        const int backlogCount = parseRequestedBacklog_(ctx);
        ctx.switchToRawSocket([this, backlogCount](SwAbstractSocket* socket) {
            attachLogStreamSocket_(socket, backlogCount);
        }, false);
    }

    bool startControlListener_(SwString& errOut) {
        errOut.clear();
        if (controlPort_ == 0) {
            return true;
        }
        if (controlToken_.isEmpty()) {
            errOut = "control API requires --control_token";
            return false;
        }
        if (!controlServer_.listen(controlBind_, controlPort_)) {
            errOut = SwString("failed to start control API on ") + controlBind_ + ":" + SwString::number(controlPort_);
            return false;
        }
        swDebug() << "[launcher] control API listening on" << controlBind_ << ":" << controlServer_.httpPort();
        return true;
    }

    bool ensureMutationAllowed_(SwHttpContext& ctx) const {
        if (!configFilePath_.isEmpty()) {
            return true;
        }
        ctx.json(SwJsonValue(swLaunchErrorPayload_("mutations require startup with --config_file")), 403);
        return false;
    }

    SwJsonArray buildHelpTopicsJson_() const {
        return swLaunchStringListToJsonArray_(swLaunchHelpTopicList_());
    }

    SwJsonArray buildCapabilitiesJson_() const {
        SwJsonArray capabilities;
        capabilities.append(SwJsonValue("help.http"));
        capabilities.append(SwJsonValue("auth.bearer_token"));
        capabilities.append(SwJsonValue("state.read"));
        capabilities.append(SwJsonValue("state.full_replace_write"));
        capabilities.append(SwJsonValue("deploy.multipart_manifest_bundle"));
        capabilities.append(SwJsonValue("deploy.sha256_verification"));
        capabilities.append(SwJsonValue("deploy.skip_identical_artifacts"));
        capabilities.append(SwJsonValue("deploy.rollback_files_and_runtime"));
        capabilities.append(SwJsonValue("deploy.serialized_jobs"));
        capabilities.append(SwJsonValue("logs.sse_stream"));
        capabilities.append(SwJsonValue("logs.backlog"));
        capabilities.append(SwJsonValue("logs.heartbeat"));
        capabilities.append(SwJsonValue("runtime.targeted_restart"));
        return capabilities;
    }

    SwJsonArray buildRouteHelpJson_() const {
        SwJsonArray routes;

        const auto appendRoute = [&routes](const SwString& method,
                                           const SwString& path,
                                           bool authRequired,
                                           const SwString& summary) {
            SwJsonObject route;
            route["method"] = SwJsonValue(method);
            route["path"] = SwJsonValue(path);
            route["authRequired"] = SwJsonValue(authRequired);
            route["summary"] = SwJsonValue(summary);
            routes.append(SwJsonValue(route));
        };

        appendRoute("GET", "/api/launch/help", false, "HTTP help overview with version, routes and capabilities");
        appendRoute("GET", "/api/launch/help/:topic", false, "Detailed help for one topic");
        appendRoute("GET", "/api/launch/state", true, "Read desired state and runtime summary");
        appendRoute("GET", "/api/launch/logs/stream", true, "Follow launcher and child logs in SSE");
        appendRoute("PUT", "/api/launch/state", true, "Replace desired state and reconcile runtime");
        appendRoute("POST", "/api/launch/deploy", true, "Deploy files plus desired state with rollback");
        appendRoute("GET", "/api/launch/deploy/:jobId", true, "Inspect one deploy job");
        return routes;
    }

    SwJsonObject buildHelpPayload_(const SwString& rawTopic) const {
        const SwString normalizedTopic = normalizeHelpTopic_(rawTopic);
        const SwString resolvedTopic = normalizedTopic.isEmpty() ? SwString("overview") : normalizedTopic;
        const SwString helpText = swLaunchHelpForTopic_(rawTopic);

        SwJsonObject payload;
        payload["ok"] = SwJsonValue(true);
        payload["product"] = SwJsonValue("SwLaunch");
        payload["version"] = SwJsonValue(swLaunchVersion_());
        payload["gitRevision"] = SwJsonValue(swLaunchGitRevision_());
        payload["apiVersion"] = SwJsonValue("v1");
        payload["buildStamp"] = SwJsonValue(swLaunchBuildStamp_());
        payload["topic"] = SwJsonValue(resolvedTopic);
        payload["text"] = SwJsonValue(helpText.isEmpty() ? swLaunchHelpOverview_() : helpText);
        payload["helpTopics"] = SwJsonValue(buildHelpTopicsJson_());
        payload["capabilities"] = SwJsonValue(buildCapabilitiesJson_());

        SwJsonObject auth;
        auth["scheme"] = SwJsonValue("Bearer");
        auth["apiRoutesRequireToken"] = SwJsonValue(true);
        auth["helpRoutesRequireToken"] = SwJsonValue(false);
        payload["auth"] = SwJsonValue(auth);

        SwJsonObject control;
        control["enabled"] = SwJsonValue(controlPort_ > 0);
        control["bind"] = SwJsonValue(controlBind_);
        control["port"] = SwJsonValue(static_cast<int>(controlServer_.httpPort()));
        payload["controlApi"] = SwJsonValue(control);

        SwJsonObject features;
        features["logStreamProtocol"] = SwJsonValue("sse");
        features["logStreamPath"] = SwJsonValue("/api/launch/logs/stream");
        features["logHeartbeatMs"] = SwJsonValue(logHeartbeatIntervalMs_);
        features["maxLogBacklogEntries"] = SwJsonValue(maxLogBacklogEntries_);
        features["mutationsRequireConfigFile"] = SwJsonValue(true);
        features["artifactIntegrity"] = SwJsonValue("sha256");
        features["serializedMutations"] = SwJsonValue(true);
        payload["features"] = SwJsonValue(features);

        if (rawTopic.trimmed().isEmpty()) {
            payload["routes"] = SwJsonValue(buildRouteHelpJson_());
        }

        return payload;
    }

    SwJsonObject buildStatePayload_() const {
        SwJsonObject root;
        root["ok"] = SwJsonValue(true);
        root["desiredState"] = SwJsonValue(swLaunchSanitizeStateForApi_(desiredRoot_));
        root["runtime"] = SwJsonValue(buildRuntimePayload_());
        return root;
    }

    SwJsonObject buildRuntimePayload_() const {
        SwJsonObject runtime;
        SwJsonObject control;
        control["enabled"] = SwJsonValue(controlPort_ > 0);
        control["bind"] = SwJsonValue(controlBind_);
        control["port"] = SwJsonValue(static_cast<int>(controlServer_.httpPort()));
        control["helpPath"] = SwJsonValue("/api/launch/help");
        control["logStreamPath"] = SwJsonValue("/api/launch/logs/stream");
        control["logStreamProtocol"] = SwJsonValue("sse");
        runtime["controlApi"] = SwJsonValue(control);

        SwJsonArray units;
        SwMap<SwString, SwLaunchManagedUnitPlan> plans;
        SwList<SwString> order;
        SwString err;
        if (swLaunchBuildPlanSet_(desiredRoot_, baseDir_, systemId_, preferWindowsExe_, plans, order, err)) {
            for (size_t i = 0; i < order.size(); ++i) {
                const SwString key = order[i];
                if (!plans.contains(key)) continue;

                SwJsonObject entry;
                entry["ownerKey"] = SwJsonValue(key);
                entry["kind"] = SwJsonValue(plans[key].kind);
                entry["runtimeId"] = SwJsonValue(plans[key].runtimeId);
                entry["running"] = SwJsonValue(activeUnits_.contains(key) && activeUnits_[key].isRunning());
                entry["artifactRoot"] = SwJsonValue(plans[key].artifactRoot);
                units.append(SwJsonValue(entry));
            }
        }

        runtime["units"] = SwJsonValue(units);
        return runtime;
    }

    SwJsonObject jobToJson_(const SwLaunchDeployJob& job) const {
        SwJsonObject out;
        out["jobId"] = SwJsonValue(job.jobId);
        out["deploymentId"] = SwJsonValue(job.deploymentId);
        out["status"] = SwJsonValue(job.status);
        out["phase"] = SwJsonValue(job.phase);
        out["errors"] = SwJsonValue(swLaunchStringListToJsonArray_(job.errors));
        out["impactedUnits"] = SwJsonValue(swLaunchStringListToJsonArray_(job.impactedUnits));
        out["replacedFiles"] = SwJsonValue(swLaunchStringListToJsonArray_(job.replacedFiles));
        out["skippedFiles"] = SwJsonValue(swLaunchStringListToJsonArray_(job.skippedFiles));
        return out;
    }

    void updateJobPhase_(const SwString& jobId, const SwString& phase, const SwString& status = SwString()) {
        if (jobId.isEmpty() || !deployJobs_.contains(jobId)) return;
        SwLaunchDeployJob job = deployJobs_[jobId];
        job.phase = phase;
        if (!status.isEmpty()) job.status = status;
        deployJobs_[jobId] = job;
    }

    SwString nextJobId_() {
        return SwString("deploy-") + SwString::number(static_cast<long long>(nowMonotonicMs_())) +
               "-" + SwString::number(nextJobCounter_++);
    }

    bool prepareDeployJob_(SwHttpContext& ctx, SwLaunchDeployJob& jobOut, SwString& errOut) {
        errOut.clear();
        jobOut = SwLaunchDeployJob();
        jobOut.jobId = nextJobId_();
        jobOut.status = "queued";
        jobOut.phase = "queued";
        jobOut.stagingRoot = joinPath_(baseDir_, SwString("_runlogs/_deploy/") + sanitizeFileLeaf_(jobOut.jobId));

        const SwHttpRequest& request = ctx.request();
        const SwHttpRequest::MultipartPart* manifestPart = nullptr;
        for (size_t i = 0; i < request.multipartParts.size(); ++i) {
            if (request.multipartParts[i].name == "manifest") {
                manifestPart = &request.multipartParts[i];
                break;
            }
        }
        if (!manifestPart) {
            errOut = "missing multipart part: manifest";
            return false;
        }

        SwString manifestText;
        if (manifestPart->storedOnDisk) {
            if (!swLaunchReadTextFile_(manifestPart->tempFilePath, manifestText, errOut)) return false;
        } else {
            manifestText = SwString(manifestPart->data.toStdString());
        }

        SwJsonDocument document;
        SwString parseError;
        if (!document.loadFromJson(manifestText, parseError) || !document.isObject()) {
            errOut = SwString("invalid manifest JSON: ") + parseError;
            return false;
        }

        const SwJsonObject manifest = document.object();
        const SwString formatVersion = manifest.contains("formatVersion")
                                           ? SwString(manifest["formatVersion"].toString())
                                           : SwString();
        if (formatVersion.isEmpty() || (formatVersion != "1" && formatVersion != "v1")) {
            errOut = "manifest formatVersion must be 1";
            return false;
        }
        if (!manifest.contains("desiredState") || !manifest["desiredState"].isObject()) {
            errOut = "manifest missing desiredState object";
            return false;
        }
        if (manifest.contains("artifacts") && !manifest["artifacts"].isArray()) {
            errOut = "manifest field 'artifacts' must be an array";
            return false;
        }

        jobOut.deploymentId = manifest.contains("deploymentId") ? SwString(manifest["deploymentId"].toString()) : SwString();
        jobOut.desiredState = manifest["desiredState"].toObject();

        if (!SwDir::mkpathAbsolute(jobOut.stagingRoot, false)) {
            errOut = SwString("failed to create deploy staging directory: ") + jobOut.stagingRoot;
            return false;
        }

        const SwJsonArray artifacts = getArrayOrEmpty_(manifest["artifacts"]);
        for (size_t i = 0; i < artifacts.size(); ++i) {
            if (!artifacts[i].isObject()) {
                errOut = "every manifest artifact must be an object";
                return false;
            }

            const SwJsonObject artifactObject = artifacts[i].toObject();
            SwLaunchArtifactPlan artifact;
            artifact.partName = artifactObject.contains("partName") ? SwString(artifactObject["partName"].toString()) : SwString();
            artifact.ownerKey = artifactObject.contains("ownerKey") ? SwString(artifactObject["ownerKey"].toString()) : SwString();
            artifact.relativePath = artifactObject.contains("relativePath") ? SwString(artifactObject["relativePath"].toString()) : SwString();
            artifact.sha256 = artifactObject.contains("sha256") ? SwString(artifactObject["sha256"].toString()) : SwString();

            if (artifact.partName.isEmpty() || artifact.ownerKey.isEmpty() || artifact.relativePath.isEmpty() || artifact.sha256.isEmpty()) {
                errOut = "artifact entries require partName, ownerKey, relativePath and sha256";
                return false;
            }
            if (!swLaunchIsSha256Hex_(artifact.sha256)) {
                errOut = SwString("invalid artifact sha256 for part ") + artifact.partName;
                return false;
            }
            if (!swLaunchIsSafeRelativePath_(artifact.relativePath)) {
                errOut = SwString("unsafe artifact relativePath: ") + artifact.relativePath;
                return false;
            }

            const SwHttpRequest::MultipartPart* uploadPart = nullptr;
            for (size_t p = 0; p < request.multipartParts.size(); ++p) {
                if (request.multipartParts[p].name == artifact.partName) {
                    uploadPart = &request.multipartParts[p];
                    break;
                }
            }
            if (!uploadPart) {
                errOut = SwString("missing multipart part referenced by manifest: ") + artifact.partName;
                return false;
            }

            const SwString stageLeaf = SwString::number(static_cast<long long>(i)) + "_" +
                                       sanitizeFileLeaf_(!uploadPart->fileName.isEmpty() ? uploadPart->fileName : artifact.partName);
            artifact.stagingPath = joinPath_(jobOut.stagingRoot, SwString("uploads/") + stageLeaf);

            SwString writeError;
            if (uploadPart->storedOnDisk) {
                if (!swLaunchCopyFile_(uploadPart->tempFilePath, artifact.stagingPath, &writeError)) {
                    errOut = writeError;
                    return false;
                }
            } else {
                if (!swLaunchWriteBytesFile_(artifact.stagingPath, uploadPart->data, &writeError)) {
                    errOut = writeError;
                    return false;
                }
            }

            const SwString stagedChecksum = swLaunchChecksumForFile_(artifact.stagingPath);
            if (stagedChecksum.isEmpty()) {
                errOut = SwString("failed to checksum staged artifact: ") + artifact.partName;
                return false;
            }
            if (stagedChecksum.toLower() != artifact.sha256.toLower()) {
                errOut = SwString("sha256 mismatch for part: ") + artifact.partName;
                return false;
            }

            jobOut.artifacts.append(artifact);
        }

        return true;
    }

    void cleanupJobStaging_(const SwLaunchDeployJob& job) const {
        if (job.stagingRoot.isEmpty()) return;
        (void)SwDir::removeRecursively(job.stagingRoot);
    }

    bool normalizeDesiredState_(const SwJsonObject& requested,
                                SwJsonObject& normalized,
                                SwString& errOut) const {
        errOut.clear();
        normalized = requested;

        if (normalized.contains("control_token")) {
            errOut = "control_token is CLI-only and cannot be mutated through the API";
            return false;
        }

        if (normalized.contains("control_api") && normalized["control_api"].isObject()) {
            const SwJsonObject incomingControlApi(normalized["control_api"].toObject());
            if (incomingControlApi.contains("token") || incomingControlApi.contains("control_token")) {
                errOut = "control_api token fields are CLI-only";
                return false;
            }

            const SwJsonObject currentControlApi = getObjectOrEmpty_(desiredRoot_["control_api"]);
            if (swLaunchJsonCompact_(SwJsonValue(incomingControlApi)) != swLaunchJsonCompact_(SwJsonValue(currentControlApi))) {
                errOut = "control_api settings are immutable after startup";
                return false;
            }
        }

        if (normalized.contains("sys") && SwString(normalized["sys"].toString()) != systemId_) {
            errOut = "field 'sys' is immutable after startup";
            return false;
        }
        if (normalized.contains("duration_ms") && normalized["duration_ms"].toInt() != durationMs_) {
            errOut = "field 'duration_ms' is immutable after startup";
            return false;
        }

        normalized["sys"] = SwJsonValue(systemId_);
        if (desiredRoot_.contains("duration_ms")) {
            normalized["duration_ms"] = SwJsonValue(durationMs_);
        } else {
            normalized.remove("duration_ms");
        }

        if (desiredRoot_.contains("control_api")) {
            normalized["control_api"] = desiredRoot_["control_api"];
        } else {
            normalized.remove("control_api");
        }

        if (!normalized.contains("containers")) normalized["containers"] = SwJsonValue(SwJsonArray());
        if (!normalized.contains("nodes")) normalized["nodes"] = SwJsonValue(SwJsonArray());

        const SwList<SwString> currentKeys = desiredRoot_.keys();
        for (size_t i = 0; i < currentKeys.size(); ++i) {
            const SwString key = currentKeys[i];
            if (key == "containers" || key == "nodes" || key == "sys" || key == "duration_ms" || key == "control_api") {
                continue;
            }
            if (!normalized.contains(key)) {
                normalized[key] = desiredRoot_[key];
            }
        }

        normalized = swLaunchSanitizeStateForApi_(normalized);
        return true;
    }

    bool prepareArtifactsForApply_(SwList<SwLaunchArtifactPlan>& artifacts,
                                   const SwMap<SwString, SwLaunchManagedUnitPlan>& targetPlans,
                                   SwLaunchMutationSummary& summary,
                                   SwString& errOut) const {
        errOut.clear();

        for (size_t i = 0; i < artifacts.size(); ++i) {
            SwLaunchArtifactPlan& artifact = artifacts[i];
            if (!targetPlans.contains(artifact.ownerKey)) {
                errOut = SwString("artifact owner is unknown in desiredState: ") + artifact.ownerKey;
                return false;
            }
            if (!swLaunchIsSafeRelativePath_(artifact.relativePath)) {
                errOut = SwString("unsafe artifact relativePath: ") + artifact.relativePath;
                return false;
            }

            const SwLaunchManagedUnitPlan plan = targetPlans.value(artifact.ownerKey);
            artifact.destinationPath = swLaunchJoinRootAndRelative_(plan.artifactRoot, artifact.relativePath);
            if (artifact.destinationPath.isEmpty()) {
                errOut = SwString("failed to resolve artifact destination for ") + artifact.ownerKey;
                return false;
            }

            artifact.currentSha256 = SwFile::isFile(artifact.destinationPath)
                                         ? swLaunchChecksumForFile_(artifact.destinationPath)
                                         : SwString();
            artifact.skipped = !artifact.currentSha256.isEmpty() &&
                               artifact.currentSha256.toLower() == artifact.sha256.toLower();
            if (artifact.skipped) {
                summary.skippedFiles.append(artifact.destinationPath);
            }
        }

        return true;
    }

    SwList<SwString> collectImpactedUnits_(const SwMap<SwString, SwLaunchManagedUnitPlan>& currentPlans,
                                           const SwMap<SwString, SwLaunchManagedUnitPlan>& targetPlans,
                                           const SwList<SwLaunchArtifactPlan>& artifacts) const {
        SwList<SwString> impacted;

        const SwList<SwString> currentKeys = currentPlans.keys();
        for (size_t i = 0; i < currentKeys.size(); ++i) {
            const SwString key = currentKeys[i];
            if (!targetPlans.contains(key)) {
                swLaunchAppendUnique_(impacted, key);
                continue;
            }

            const SwString currentSpec = swLaunchJsonCompact_(SwJsonValue(currentPlans.value(key).spec));
            const SwString targetSpec = swLaunchJsonCompact_(SwJsonValue(targetPlans.value(key).spec));
            if (currentSpec != targetSpec) {
                swLaunchAppendUnique_(impacted, key);
            }
        }

        const SwList<SwString> targetKeys = targetPlans.keys();
        for (size_t i = 0; i < targetKeys.size(); ++i) {
            if (!currentPlans.contains(targetKeys[i])) {
                swLaunchAppendUnique_(impacted, targetKeys[i]);
            }
        }

        for (size_t i = 0; i < artifacts.size(); ++i) {
            if (!artifacts[i].skipped) {
                swLaunchAppendUnique_(impacted, artifacts[i].ownerKey);
            }
        }

        return impacted;
    }

    bool waitUnitStopped_(const SwLaunchManagedUnitRuntime& runtime, int timeoutMs) const {
        const int stepMs = 50;
        int elapsedMs = 0;
        while (runtime.isRunning() && elapsedMs < timeoutMs) {
            SwEventLoop::swsleep(stepMs);
            elapsedMs += stepMs;
        }
        return !runtime.isRunning();
    }

    bool discardUnits_(const SwList<SwString>& keys, SwString& errOut) {
        errOut.clear();
        for (size_t i = 0; i < keys.size(); ++i) {
            const SwString key = keys[i];
            if (!activeUnits_.contains(key)) continue;

            SwLaunchManagedUnitRuntime runtime = activeUnits_[key];
            runtime.stop();
            if (!waitUnitStopped_(runtime, 3000)) {
                runtime.forceKill();
                if (!waitUnitStopped_(runtime, 2000)) {
                    errOut = SwString("failed to stop managed unit: ") + key;
                    return false;
                }
            }

            runtime.deleteLater();
            activeUnits_.remove(key);
        }
        return true;
    }

    void discardUnitsBestEffort_(const SwList<SwString>& keys) {
        SwString ignored;
        (void)discardUnits_(keys, ignored);
    }

    bool startUnits_(const SwMap<SwString, SwLaunchManagedUnitPlan>& plans,
                     const SwList<SwString>& order,
                     const SwList<SwString>& selectedKeys,
                     SwString& errOut) {
        errOut.clear();

        for (size_t i = 0; i < order.size(); ++i) {
            const SwString key = order[i];
            bool selected = false;
            for (size_t s = 0; s < selectedKeys.size(); ++s) {
                if (selectedKeys[s] == key) {
                    selected = true;
                    break;
                }
            }
            if (!selected) continue;
            if (!plans.contains(key)) continue;
            if (activeUnits_.contains(key)) continue;

            const SwLaunchManagedUnitPlan plan = plans.value(key);
            SwLaunchManagedUnitRuntime runtime;
            runtime.plan = plan;

            if (plan.kind == "container") {
                LaunchContainerProcess* process = new LaunchContainerProcess(plan.spec, baseDir_, systemId_, preferWindowsExe_, this);
                process->setShutdownOnCleanExitHandler([this](const SwString& id, int exitCode) {
                    requestGlobalShutdown_(id, exitCode);
                });
                if (!process->start()) {
                    process->deleteLater();
                    errOut = SwString("failed to start managed container: ") + plan.ownerKey;
                    return false;
                }
                runtime.containerProcess = process;
            } else {
                LaunchNodeProcess* process = new LaunchNodeProcess(plan.spec, baseDir_, systemId_, preferWindowsExe_, this);
                process->setShutdownOnCleanExitHandler([this](const SwString& id, int exitCode) {
                    requestGlobalShutdown_(id, exitCode);
                });
                if (!process->start()) {
                    process->deleteLater();
                    errOut = SwString("failed to start managed node: ") + plan.ownerKey;
                    return false;
                }
                runtime.nodeProcess = process;
            }

            activeUnits_[plan.ownerKey] = runtime;
        }

        return true;
    }

    void requestGlobalShutdown_(const SwString& id, int exitCode) {
        if (shutdownRequested_) return;
        shutdownRequested_ = true;
        const SwString triggerId = id;
        SwTimer::singleShot(0, [this, triggerId, exitCode]() {
            swWarning() << "[launcher] clean exit requested global shutdown id=" << triggerId
                        << " exitCode=" << exitCode;
            stopAll();
            app_.quit();
        });
    }

    bool persistDesiredState_(const SwJsonObject& state, SwString& errOut) const {
        errOut.clear();
        if (configFilePath_.isEmpty()) {
            errOut = "mutations require startup with --config_file";
            return false;
        }

        SwJsonObject persisted = swLaunchSanitizeStateForApi_(state);
        persisted.remove("control_token");
        if (persisted.contains("control_api") && persisted["control_api"].isObject()) {
            SwJsonObject controlApi(persisted["control_api"].toObject());
            controlApi.remove("token");
            controlApi.remove("control_token");
            persisted["control_api"] = SwJsonValue(controlApi);
        }

        return swLaunchWriteJsonFileAtomic_(configFilePath_, persisted, &errOut);
    }

    bool replaceArtifacts_(const SwList<SwLaunchArtifactPlan>& artifacts,
                           SwList<SwLaunchRollbackFileAction>& rollbackActions,
                           SwLaunchMutationSummary& summary,
                           SwString& errOut,
                           const SwString& jobId = SwString()) {
        errOut.clear();

        for (size_t i = 0; i < artifacts.size(); ++i) {
            const SwLaunchArtifactPlan artifact = artifacts[i];
            if (artifact.skipped) continue;

            if (!jobId.isEmpty()) updateJobPhase_(jobId, "replacing_artifacts", "running");

            SwLaunchRollbackFileAction action;
            action.destinationPath = artifact.destinationPath;
            action.destinationExisted = SwFile::isFile(artifact.destinationPath);

            if (action.destinationExisted) {
                action.backupPath = artifact.stagingPath + ".rollback.bak";
                if (!swLaunchCopyFile_(artifact.destinationPath, action.backupPath, &errOut)) {
                    return false;
                }
            }

            const SwString tempReplacement = artifact.destinationPath + ".swlaunch.tmp";
            if (!swLaunchCopyFile_(artifact.stagingPath, tempReplacement, &errOut)) {
                (void)swLaunchRemoveFileQuiet_(tempReplacement);
                return false;
            }
            if (!swLaunchReplaceFile_(tempReplacement, artifact.destinationPath, &errOut)) {
                (void)swLaunchRemoveFileQuiet_(tempReplacement);
                return false;
            }

            rollbackActions.append(action);
            summary.replacedFiles.append(artifact.destinationPath);
        }

        return true;
    }

    void rollbackArtifacts_(const SwList<SwLaunchRollbackFileAction>& rollbackActions) const {
        for (int i = static_cast<int>(rollbackActions.size()) - 1; i >= 0; --i) {
            const SwLaunchRollbackFileAction action = rollbackActions[static_cast<size_t>(i)];
            if (action.destinationExisted) {
                SwString err;
                const SwString tempPath = action.destinationPath + ".swlaunch.rollback";
                if (swLaunchCopyFile_(action.backupPath, tempPath, &err)) {
                    (void)swLaunchReplaceFile_(tempPath, action.destinationPath, &err);
                } else {
                    swWarning() << "[launcher] rollback failed for" << action.destinationPath << ":" << err;
                }
            } else {
                (void)swLaunchRemoveFileQuiet_(action.destinationPath);
            }
            if (!action.backupPath.isEmpty()) {
                (void)swLaunchRemoveFileQuiet_(action.backupPath);
            }
        }
    }

    bool applyDesiredState_(const SwJsonObject& requestedRoot,
                            const SwList<SwLaunchArtifactPlan>& requestedArtifacts,
                            SwLaunchMutationSummary& summary,
                            SwString& errOut,
                            int& statusCode,
                            const SwString& jobId = SwString()) {
        summary = SwLaunchMutationSummary();
        errOut.clear();
        statusCode = 500;

        if (configFilePath_.isEmpty()) {
            statusCode = 403;
            errOut = "mutations require startup with --config_file";
            return false;
        }

        SwJsonObject normalizedRoot;
        if (!normalizeDesiredState_(requestedRoot, normalizedRoot, errOut)) {
            statusCode = 400;
            return false;
        }

        updateJobPhase_(jobId, "validating", "running");

        SwMap<SwString, SwLaunchManagedUnitPlan> currentPlans;
        SwList<SwString> currentOrder;
        if (!swLaunchBuildPlanSet_(desiredRoot_, baseDir_, systemId_, preferWindowsExe_, currentPlans, currentOrder, errOut)) {
            statusCode = 500;
            return false;
        }

        SwMap<SwString, SwLaunchManagedUnitPlan> targetPlans;
        SwList<SwString> targetOrder;
        if (!swLaunchBuildPlanSet_(normalizedRoot, baseDir_, systemId_, preferWindowsExe_, targetPlans, targetOrder, errOut)) {
            statusCode = 400;
            return false;
        }

        SwList<SwLaunchArtifactPlan> artifacts = requestedArtifacts;
        if (!prepareArtifactsForApply_(artifacts, targetPlans, summary, errOut)) {
            statusCode = 400;
            return false;
        }

        summary.impactedUnits = collectImpactedUnits_(currentPlans, targetPlans, artifacts);
        const bool rootChanged = swLaunchJsonCompact_(SwJsonValue(desiredRoot_)) !=
                                 swLaunchJsonCompact_(SwJsonValue(normalizedRoot));

        if (summary.impactedUnits.isEmpty()) {
            if (!rootChanged) {
                return true;
            }

            updateJobPhase_(jobId, "persisting_state", "running");
            if (!persistDesiredState_(normalizedRoot, errOut)) {
                statusCode = 500;
                return false;
            }
            desiredRoot_ = normalizedRoot;
            return true;
        }

        const SwJsonObject previousRoot = desiredRoot_;
        const SwList<SwString> impactedUnits = summary.impactedUnits;
        SwList<SwLaunchRollbackFileAction> rollbackActions;
        bool configPersisted = false;

        updateJobPhase_(jobId, "stopping_units", "running");
        if (!discardUnits_(impactedUnits, errOut)) {
            statusCode = 500;
            return false;
        }

        if (!replaceArtifacts_(artifacts, rollbackActions, summary, errOut, jobId)) {
            statusCode = 500;
            rollbackArtifacts_(rollbackActions);
            SwString restoreErr;
            (void)startUnits_(currentPlans, currentOrder, impactedUnits, restoreErr);
            return false;
        }

        if (rootChanged) {
            updateJobPhase_(jobId, "persisting_state", "running");
            if (!persistDesiredState_(normalizedRoot, errOut)) {
                statusCode = 500;
                rollbackArtifacts_(rollbackActions);
                SwString restoreErr;
                (void)startUnits_(currentPlans, currentOrder, impactedUnits, restoreErr);
                return false;
            }
            configPersisted = true;
        }

        updateJobPhase_(jobId, "reconciling_runtime", "running");
        if (!startUnits_(targetPlans, targetOrder, impactedUnits, errOut)) {
            statusCode = 500;
            discardUnitsBestEffort_(impactedUnits);
            if (configPersisted) {
                SwString rollbackConfigErr;
                if (!persistDesiredState_(previousRoot, rollbackConfigErr)) {
                    swWarning() << "[launcher] failed to rollback config file:" << rollbackConfigErr;
                }
            }
            rollbackArtifacts_(rollbackActions);
            SwString restoreErr;
            if (!startUnits_(currentPlans, currentOrder, impactedUnits, restoreErr)) {
                swWarning() << "[launcher] failed to restore previous runtime:" << restoreErr;
            }
            return false;
        }

        desiredRoot_ = normalizedRoot;
        return true;
    }

    void runDeployJob_(const SwString& jobId) {
        if (!deployJobs_.contains(jobId)) {
            mutationInProgress_ = false;
            return;
        }

        SwLaunchDeployJob job = deployJobs_[jobId];
        job.status = "running";
        job.phase = "starting";
        deployJobs_[jobId] = job;

        SwLaunchMutationSummary summary;
        SwString err;
        int statusCode = 500;
        const bool ok = applyDesiredState_(job.desiredState, job.artifacts, summary, err, statusCode, jobId);

        job = deployJobs_.contains(jobId) ? deployJobs_[jobId] : job;
        job.impactedUnits = summary.impactedUnits;
        job.replacedFiles = summary.replacedFiles;
        job.skippedFiles = summary.skippedFiles;

        if (ok) {
            job.status = "succeeded";
            job.phase = "completed";
        } else {
            if (!err.isEmpty()) job.errors.append(err);
            job.status = "failed";
            job.phase = "failed";
        }

        deployJobs_[jobId] = job;
        cleanupJobStaging_(job);
        mutationInProgress_ = false;
    }

private:
    SwCoreApplication& app_;
    SwJsonObject desiredRoot_;
    SwString baseDir_;
    SwString configFilePath_;
    SwString systemId_;
    int durationMs_{0};
    bool preferWindowsExe_{false};
    SwString controlBind_;
    uint16_t controlPort_{0};
    SwString controlToken_;
    SwHttpServer controlServer_;
    SwList<SwLaunchLogEntry> recentLogEntries_;
    SwList<SwLaunchLogStreamClient> logStreamClients_;
    long long nextLogSequence_{1};
    long long debugLineListenerId_{0};
    int logHeartbeatIntervalMs_{15000};
    int maxLogBacklogEntries_{200};
    SwMap<SwString, SwLaunchManagedUnitRuntime> activeUnits_;
    SwMap<SwString, SwLaunchDeployJob> deployJobs_;
    bool mutationInProgress_{false};
    bool shutdownRequested_{false};
    long long nextJobCounter_{1};
};
