#include "SwApiIpcInspector.h"

#include "SwJsonDocument.h"
#include "SwSharedMemorySignal.h"

SwJsonArray SwApiIpcInspector::appsSnapshot() const { return sw::ipc::shmAppsSnapshot(); }

SwJsonArray SwApiIpcInspector::registrySnapshot(const SwString& domain) const { return sw::ipc::shmRegistrySnapshot(domain); }

SwJsonArray SwApiIpcInspector::subscribersSnapshot(const SwString& domain) const { return sw::ipc::shmSubscribersSnapshot(domain); }

SwStringList SwApiIpcInspector::domains() const {
    SwStringList out;
    const SwJsonArray apps = appsSnapshot();
    out.reserve(apps.size());

    for (size_t i = 0; i < apps.size(); ++i) {
        const SwJsonValue v = apps[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        const SwString domain = SwString(o["domain"].toString());
        if (!domain.isEmpty()) out.append(domain);
    }

    out.removeDuplicates();
    return out;
}

static SwString normalizeSlashes(SwString x) {
    x.replace("\\", "/");
    while (x.contains("//")) x.replace("//", "/");
    while (x.startsWith("/")) x = x.mid(1);
    while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
    return x;
}

bool SwApiIpcInspector::parseTarget(const SwString& input,
                                   const SwString& defaultDomain,
                                   Target& out,
                                   SwString& err) const {
    err.clear();
    out = Target{};

    SwString x = normalizeSlashes(input.trimmed());
    if (x.isEmpty()) {
        err = "empty target";
        return false;
    }

    size_t slashCount = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (x.at(static_cast<int>(i)) == '/') slashCount++;
    }

    if (slashCount >= 2) {
        const int slash = x.indexOf("/");
        if (slash <= 0 || (static_cast<size_t>(slash + 1) >= x.size())) {
            err = "invalid target format";
            return false;
        }
        out.domain = x.left(slash);
        out.object = x.mid(slash + 1);
        return !out.domain.isEmpty() && !out.object.isEmpty();
    }

    if (defaultDomain.isEmpty()) {
        err = "missing domain (use --domain <sys>)";
        return false;
    }

    out.domain = defaultDomain;
    out.object = x;
    return !out.object.isEmpty();
}

bool SwApiIpcInspector::activePidsForDomain_(const SwString& domain, SwMap<uint32_t, bool>& out) {
    out.clear();
    bool found = false;

    SwJsonArray apps = sw::ipc::shmAppsSnapshot();
    for (size_t i = 0; i < apps.size(); ++i) {
        const SwJsonValue v = apps[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["domain"].toString()) != domain) continue;
        found = true;

        const SwJsonValue pidsVal = o["pids"];
        if (!pidsVal.isArray()) break;
        const SwJsonArray pids = pidsVal.toArray();
        for (size_t k = 0; k < pids.size(); ++k) {
            const SwJsonValue pv = pids[k];
            if (!pv.isObject()) continue;
            const SwJsonObject po(pv.toObject());
            const int pid = po["pid"].toInt();
            if (pid > 0) out.insert(static_cast<uint32_t>(pid), true);
        }
        break; // domain unique
    }

    return found;
}

SwJsonArray SwApiIpcInspector::nodesForDomain(const SwString& domain, bool includeStale) const {
    SwJsonArray out;
    if (domain.isEmpty()) return out;

    const SwJsonArray all = sw::ipc::shmRegistrySnapshot(domain);
    SwMap<uint32_t, bool> activePids;
    const bool haveDomain = activePidsForDomain_(domain, activePids);
    if (!haveDomain && !includeStale) return out;

    struct Agg {
        uint64_t lastSeenMs{0};
        SwMap<uint32_t, bool> pids;
        SwStringList configIds;
        SwMap<SwString, bool> configIdsSet;
    };
    SwMap<SwString, Agg> agg;

    const int kConfigPrefixLen = static_cast<int>(SwString("__config__|").size());

    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());

        const SwString object = SwString(o["object"].toString());
        if (object.isEmpty()) continue;

        const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
        if (pid != 0 && haveDomain && !includeStale && !activePids.contains(pid)) {
            continue;
        }

        Agg& a = agg[object];
        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t > a.lastSeenMs) a.lastSeenMs = t;
        if (pid != 0) a.pids.insert(pid, true);

        const SwString sig = SwString(o["signal"].toString());
        if (sig.startsWith("__config__|")) {
            const SwString cfgId = sig.mid(kConfigPrefixLen);
            if (!cfgId.isEmpty() && !a.configIdsSet.contains(cfgId)) {
                a.configIdsSet.insert(cfgId, true);
                a.configIds.append(cfgId);
            }
        }
    }

    for (SwMap<SwString, Agg>::const_iterator it = agg.cbegin(); it != agg.cend(); ++it) {
        const SwString objectFqn = it.key();
        const Agg& a = it.value();
        if (a.configIds.isEmpty()) continue; // only SwRemoteObject nodes

        SwString nsPart;
        SwString objName = objectFqn;
        const size_t slash = objectFqn.lastIndexOf('/');
        if (slash != static_cast<size_t>(-1)) {
            nsPart = objectFqn.left(static_cast<int>(slash));
            objName = objectFqn.mid(static_cast<int>(slash + 1));
        }
        if (objName.isEmpty()) continue;

        SwJsonObject d;
        d["target"] = SwJsonValue((domain + "/" + objectFqn).toStdString());
        d["domain"] = SwJsonValue(domain.toStdString());
        d["nameSpace"] = SwJsonValue(nsPart.toStdString());
        d["objectName"] = SwJsonValue(objName.toStdString());
        d["object"] = SwJsonValue(objectFqn.toStdString());
        d["lastSeenMs"] = SwJsonValue(static_cast<double>(a.lastSeenMs));
        d["alive"] = SwJsonValue(a.pids.size() != 0);

        SwJsonArray pidArr;
        const SwList<uint32_t> pidKeys = a.pids.keys();
        for (size_t k = 0; k < pidKeys.size(); ++k) {
            pidArr.append(SwJsonValue(static_cast<int>(pidKeys[k])));
        }
        d["pids"] = SwJsonValue(pidArr);

        SwJsonArray cfgIds;
        for (size_t k = 0; k < a.configIds.size(); ++k) {
            cfgIds.append(SwJsonValue(a.configIds[k].toStdString()));
        }
        d["configIds"] = SwJsonValue(cfgIds);

        out.append(SwJsonValue(d));
    }

    return out;
}

SwJsonArray SwApiIpcInspector::nodesAllDomains(bool includeStale) const {
    SwJsonArray out;
    const SwStringList doms = domains();
    for (size_t i = 0; i < doms.size(); ++i) {
        SwJsonArray part = nodesForDomain(doms[i], includeStale);
        for (size_t k = 0; k < part.size(); ++k) {
            out.append(part[k]);
        }
    }
    return out;
}

SwString SwApiIpcInspector::kindForSignal_(const SwString& signal) {
    if (signal.startsWith("__config__|")) return "configDoc";
    if (signal.startsWith("__cfg__|")) return "configValue";
    if (signal.startsWith("__rpc__|")) return "rpcRequest";
    if (signal.startsWith("__rpc_ret__|")) return "rpcResponse";
    return "signal";
}

SwStringList SwApiIpcInspector::parseTypeArgs_(const SwString& typeName) {
    SwStringList out;
    const int lt = typeName.indexOf("<");
    const int gt = (lt < 0) ? -1 : typeName.indexOf(">", static_cast<size_t>(lt + 1));
    if (lt < 0 || gt < 0 || gt <= lt + 1) return out;

    SwString inside = typeName.mid(lt + 1, gt - lt - 1);
    const int insideLen = static_cast<int>(inside.size());

    int start = 0;
    while (start < insideLen) {
        const int comma = inside.indexOf(",", static_cast<size_t>(start));
        const int end = (comma < 0) ? insideLen : comma;

        SwString token = inside.mid(start, end - start).trimmed();
        if (token.startsWith("class ")) token = token.mid(6);
        if (token.startsWith("struct ")) token = token.mid(7);
        token = token.trimmed();
        if (!token.isEmpty()) out.append(token);

        if (comma < 0) break;
        start = comma + 1;
    }
    return out;
}

SwJsonArray SwApiIpcInspector::argTypesToJson_(const SwStringList& types) {
    SwJsonArray arr;
    for (size_t i = 0; i < types.size(); ++i) {
        arr.append(SwJsonValue(types[i].toStdString()));
    }
    return arr;
}

SwJsonArray SwApiIpcInspector::signalsForTarget(const Target& target, bool includeStale) const {
    SwJsonArray out;
    if (target.domain.isEmpty() || target.object.isEmpty()) return out;

    const SwJsonArray all = sw::ipc::shmRegistrySnapshot(target.domain);
    SwMap<uint32_t, bool> activePids;
    const bool haveDomain = activePidsForDomain_(target.domain, activePids);
    if (!haveDomain && !includeStale) return out;

    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["object"].toString()) != target.object) continue;

        const uint32_t pid = static_cast<uint32_t>(o["pid"].toInt());
        if (pid != 0 && haveDomain && !includeStale && !activePids.contains(pid)) {
            continue;
        }

        const SwString sig = SwString(o["signal"].toString());
        SwJsonObject item;
        item["signal"] = SwJsonValue(sig.toStdString());
        item["kind"] = SwJsonValue(kindForSignal_(sig).toStdString());
        item["pid"] = o["pid"];
        item["lastSeenMs"] = o["lastSeenMs"];
        item["typeId"] = o["typeId"];
        item["typeName"] = o["typeName"];

        const SwString typeName = SwString(o["typeName"].toString());
        const SwStringList args = parseTypeArgs_(typeName);
        item["args"] = SwJsonValue(argTypesToJson_(args));

        out.append(SwJsonValue(item));
    }

    return out;
}

SwJsonArray SwApiIpcInspector::rpcsForTarget(const Target& target, bool includeStale) const {
    SwJsonArray out;
    const SwJsonArray sigs = signalsForTarget(target, includeStale);

    const int kRpcPrefixLen = static_cast<int>(SwString("__rpc__|").size());

    for (size_t i = 0; i < sigs.size(); ++i) {
        const SwJsonValue v = sigs[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());

        const SwString sig = SwString(o["signal"].toString());
        if (!sig.startsWith("__rpc__|")) continue;

        const SwString method = sig.mid(kRpcPrefixLen);
        const SwString typeName = SwString(o["typeName"].toString());
        SwStringList args = parseTypeArgs_(typeName);

        // Skip internal RPC envelope fields: callId, clientPid, clientInfo.
        if (args.size() >= 3) {
            SwStringList userArgs;
            for (size_t k = 3; k < args.size(); ++k) userArgs.append(args[k]);
            args = userArgs;
        }

        SwJsonObject item;
        item["method"] = SwJsonValue(method.toStdString());
        item["requestSignal"] = SwJsonValue(sig.toStdString());
        item["requestTypeId"] = o["typeId"];
        item["requestTypeName"] = SwJsonValue(typeName.toStdString());
        item["args"] = SwJsonValue(argTypesToJson_(args));
        out.append(SwJsonValue(item));
    }

    return out;
}

bool SwApiIpcInspector::findConfigDocSignal(const Target& target, SwString& outSignalName) const {
    outSignalName.clear();
    if (target.domain.isEmpty() || target.object.isEmpty()) return false;

    const SwJsonArray all = sw::ipc::shmRegistrySnapshot(target.domain);
    uint64_t bestSeen = 0;
    SwString best;

    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        if (SwString(o["object"].toString()) != target.object) continue;

        const SwString sig = SwString(o["signal"].toString());
        if (!sig.startsWith("__config__|")) continue;

        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t >= bestSeen) {
            bestSeen = t;
            best = sig;
        }
    }

    if (best.isEmpty()) return false;
    outSignalName = best;
    return true;
}

bool SwApiIpcInspector::readConfigDocJson(const Target& target, SwString& outJson, uint64_t& outPubId, SwString& err) const {
    outJson.clear();
    outPubId = 0;
    err.clear();

    SwString cfgSig;
    if (!findConfigDocSignal(target, cfgSig)) {
        err = "target does not expose a __config__|* signal (not a SwRemoteObject?)";
        return false;
    }

    sw::ipc::Registry reg(target.domain, target.object);
    sw::ipc::Signal<uint64_t, SwString> sig(reg, cfgSig);

    uint64_t pubId = 0;
    SwString json;
    if (!sig.readLatest(pubId, json)) {
        err = "failed to read latest config snapshot";
        return false;
    }

    outPubId = pubId;
    outJson = json;
    return true;
}

bool SwApiIpcInspector::publishConfigValue(const Target& target,
                                          const SwString& configPath,
                                          const SwString& value,
                                          SwString& err) const {
    err.clear();
    if (target.domain.isEmpty() || target.object.isEmpty()) {
        err = "invalid target";
        return false;
    }
    if (configPath.isEmpty()) {
        err = "empty config path";
        return false;
    }

    sw::ipc::Registry reg(target.domain, target.object);
    sw::ipc::Signal<uint64_t, SwString> sig(reg, SwString("__cfg__|") + configPath);
    const bool ok = sig.publish(0, value);
    if (!ok) err = "publish failed (queue full or mapping error)";
    return ok;
}

bool SwApiIpcInspector::nodeInfo(const Target& target, SwJsonObject& out, SwString& err, bool includeStale) const {
    out = SwJsonObject{};
    err.clear();

    if (target.domain.isEmpty() || target.object.isEmpty()) {
        err = "invalid target";
        return false;
    }

    const SwJsonArray sigs = signalsForTarget(target, includeStale);
    if (sigs.isEmpty() && !includeStale) {
        err = "target not found in registry";
        return false;
    }

    uint64_t lastSeen = 0;
    SwMap<uint32_t, bool> pids;
    SwStringList configIds;
    SwMap<SwString, bool> cfgSet;
    size_t rpcCount = 0;

    const int kConfigPrefixLen = static_cast<int>(SwString("__config__|").size());

    for (size_t i = 0; i < sigs.size(); ++i) {
        const SwJsonValue v = sigs[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());

        const uint64_t t = static_cast<uint64_t>(o["lastSeenMs"].toDouble());
        if (t > lastSeen) lastSeen = t;

        const int pid = o["pid"].toInt();
        if (pid > 0) pids.insert(static_cast<uint32_t>(pid), true);

        const SwString sig = SwString(o["signal"].toString());
        if (sig.startsWith("__config__|")) {
            const SwString cfgId = sig.mid(kConfigPrefixLen);
            if (!cfgId.isEmpty() && !cfgSet.contains(cfgId)) {
                cfgSet.insert(cfgId, true);
                configIds.append(cfgId);
            }
        }
        if (sig.startsWith("__rpc__|")) rpcCount++;
    }

    SwString nsPart;
    SwString objName = target.object;
    const size_t slash = target.object.lastIndexOf('/');
    if (slash != static_cast<size_t>(-1)) {
        nsPart = target.object.left(static_cast<int>(slash));
        objName = target.object.mid(static_cast<int>(slash + 1));
    }

    out["target"] = SwJsonValue(target.toString().toStdString());
    out["domain"] = SwJsonValue(target.domain.toStdString());
    out["object"] = SwJsonValue(target.object.toStdString());
    out["nameSpace"] = SwJsonValue(nsPart.toStdString());
    out["objectName"] = SwJsonValue(objName.toStdString());
    out["lastSeenMs"] = SwJsonValue(static_cast<double>(lastSeen));
    out["alive"] = SwJsonValue(pids.size() != 0);
    out["signalCount"] = SwJsonValue(static_cast<int>(sigs.size()));
    out["rpcCount"] = SwJsonValue(static_cast<int>(rpcCount));

    SwJsonArray pidArr;
    const SwList<uint32_t> pidKeys = pids.keys();
    for (size_t i = 0; i < pidKeys.size(); ++i) {
        pidArr.append(SwJsonValue(static_cast<int>(pidKeys[i])));
    }
    out["pids"] = SwJsonValue(pidArr);

    SwJsonArray cfgArr;
    for (size_t i = 0; i < configIds.size(); ++i) {
        cfgArr.append(SwJsonValue(configIds[i].toStdString()));
    }
    out["configIds"] = SwJsonValue(cfgArr);

    return true;
}
