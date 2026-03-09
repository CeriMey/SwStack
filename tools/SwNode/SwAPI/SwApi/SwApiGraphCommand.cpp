#include "SwApiGraphCommand.h"

#include "SwApiJson.h"

#include <iostream>

SwApiGraphCommand::SwApiGraphCommand(const SwApiCli& cli,
                                     SwApiIpcInspector& inspector,
                                     const SwStringList& args,
                                     SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiGraphCommand::~SwApiGraphCommand() = default;

void SwApiGraphCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi graph connections [--domain <sys>] [--ns <prefix>] [--kind <all|topic|rpc|config|internal>] [--resolve] [--json] [--pretty]\n";
}

static SwString normalizePrefix(SwString x) {
    x.replace("\\", "/");
    while (x.startsWith("/")) x = x.mid(1);
    while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
    return x;
}

static bool matchesPrefix(const SwString& s, const SwString& prefix) {
    if (prefix.isEmpty()) return true;
    if (s == prefix) return true;
    if (s.size() > prefix.size() && s.startsWith(prefix + "/")) return true;
    return false;
}

static bool startsWith(const SwString& s, const SwString& prefix) { return s.startsWith(prefix); }

static bool matchesKind(const SwString& signal, const SwString& kindRaw) {
    const SwString kind = kindRaw.toLower();

    if (kind.isEmpty() || kind == "all" || kind == "*") return true;

    const bool internal = startsWith(signal, "__");
    const bool rpc = startsWith(signal, "__rpc__|") || startsWith(signal, "__rpc_ret__|");
    const bool cfg = startsWith(signal, "__config__|") || startsWith(signal, "__cfg__|");

    if (kind == "topic" || kind == "topics") return !internal;
    if (kind == "internal" || kind == "internals") return internal;
    if (kind == "rpc" || kind == "service" || kind == "services") return rpc;
    if (kind == "config" || kind == "param" || kind == "params") return cfg;
    return false;
}

static bool isKnownKind(const SwString& kindRaw) {
    if (kindRaw.isEmpty()) return true;
    const SwString kind = kindRaw.toLower();
    return kind == "all" || kind == "*" || kind == "topic" || kind == "topics" || kind == "internal" || kind == "internals" || kind == "rpc" ||
           kind == "service" || kind == "services" || kind == "config" || kind == "param" || kind == "params";
}

static SwMap<int, SwStringList> pidToNodeTargets(const SwJsonArray& nodes) {
    SwMap<int, SwStringList> map;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const SwJsonValue v = nodes[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        const SwString target = SwString(o["target"].toString());

        const SwJsonValue pidsVal = o["pids"];
        if (!pidsVal.isArray()) continue;
        const SwJsonArray pids = pidsVal.toArray();
        for (size_t k = 0; k < pids.size(); ++k) {
            const SwJsonValue pv = pids[k];
            const int pid = pv.toInt();
            if (pid <= 0) continue;
            map[pid].append(target);
        }
    }
    return map;
}

struct SwApiGraphConnectionGroup {
    SwString domain;
    SwString object;
    SwString signal;
    SwString pubTarget;
    SwMap<SwString, bool> subTargets;

    void initFrom(const SwString& d, const SwString& obj, const SwString& sig) {
        if (!domain.isEmpty()) return;
        domain = d;
        object = obj;
        signal = sig;
        pubTarget = d + "/" + obj;
    }
};

static SwStringList filterTargetsByNs(const SwStringList& targets, const SwString& domain, const SwString& nsPrefix) {
    if (targets.isEmpty()) return SwStringList{};
    if (nsPrefix.isEmpty()) return targets;

    const SwString domPrefix = domain + "/";

    SwStringList out;
    out.reserve(targets.size());
    for (size_t i = 0; i < targets.size(); ++i) {
        const SwString& t = targets[i];
        if (!t.startsWith(domPrefix)) continue;

        const SwString obj = normalizePrefix(t.mid(static_cast<int>(domPrefix.size())));
        if (matchesPrefix(obj, nsPrefix)) out.append(t);
    }
    return out;
}

int SwApiGraphCommand::cmdConnections_() {
    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const bool resolve = cli().hasFlag("resolve");
    const SwString domain = cli().value("domain", cli().value("sys", SwString()));
    const SwString nsPrefix = normalizePrefix(cli().value("ns", cli().value("namespace", SwString())));
    const SwString kind = cli().value("kind", "all");

    if (!isKnownKind(kind)) {
        std::cerr << "swapi graph connections: unknown --kind '" << kind.toStdString() << "'\n";
        return 2;
    }

    SwJsonArray out;
    SwStringList domains;
    if (!domain.isEmpty()) {
        domains.append(domain);
    } else {
        domains = inspector().domains();
    }

    for (size_t di = 0; di < domains.size(); ++di) {
        const SwString dom = domains[di];
        const SwJsonArray subs = inspector().subscribersSnapshot(dom);
        const SwMap<int, SwStringList> pidMap = resolve ? pidToNodeTargets(inspector().nodesForDomain(dom, /*includeStale=*/false))
                                                        : SwMap<int, SwStringList>();

        SwMap<SwString, SwApiGraphConnectionGroup> groups;

        for (size_t i = 0; i < subs.size(); ++i) {
            const SwJsonValue v = subs[i];
            if (!v.isObject()) continue;
            const SwJsonObject o(v.toObject());
            const SwString obj = normalizePrefix(SwString(o["object"].toString()));
            const SwString sig = SwString(o["signal"].toString());
            const SwString subObj = o.contains("subObject") ? normalizePrefix(SwString(o["subObject"].toString())) : SwString();
            const SwString subTarget = o.contains("subTarget") ? SwString(o["subTarget"].toString()) : SwString();
            
            const bool objHasNs = obj.contains("/");
            const SwString objWithNs = (!nsPrefix.isEmpty() && !objHasNs) ? normalizePrefix(nsPrefix + "/" + obj) : obj;
            const bool subObjHasNs = (!subObj.isEmpty() && subObj.contains("/"));
            const SwString subObjWithNs =
                (!nsPrefix.isEmpty() && !subObj.isEmpty() && !subObjHasNs) ? normalizePrefix(nsPrefix + "/" + subObj) : subObj;

            if (!matchesPrefix(objWithNs, nsPrefix)) continue;
            if (!subObjWithNs.isEmpty() && !matchesPrefix(subObjWithNs, nsPrefix)) continue;
            if (!matchesKind(sig, kind)) continue;

            if (!resolve) {
                out.append(v);
                continue;
            }

            const SwString pubTarget = dom + "/" + objWithNs;
            const SwString key = pubTarget + "\n" + sig;

            SwApiGraphConnectionGroup& g = groups[key];
            g.initFrom(dom, objWithNs, sig);

            const int subPid = o["subPid"].toInt();
            if (!subTarget.isEmpty()) {
                g.subTargets.insert(subTarget, true);
            } else {
                SwMap<int, SwStringList>::const_iterator it = pidMap.find(subPid);
                if (it != pidMap.end()) {
                    const SwStringList filtered = filterTargetsByNs(it.value(), dom, nsPrefix);
                    for (size_t k = 0; k < filtered.size(); ++k) {
                        if (filtered[k] == pubTarget) continue;
                        g.subTargets.insert(filtered[k], true);
                    }
                }
            }
        }

        for (SwMap<SwString, SwApiGraphConnectionGroup>::const_iterator it = groups.cbegin(); it != groups.cend(); ++it) {
            const SwApiGraphConnectionGroup& g = it.value();
            SwJsonObject x;
            x["domain"] = SwJsonValue(g.domain.toStdString());
            x["object"] = SwJsonValue(g.object.toStdString());
            x["signal"] = SwJsonValue(g.signal.toStdString());
            x["pubTarget"] = SwJsonValue(g.pubTarget.toStdString());

            SwJsonArray subArr;
            for (SwMap<SwString, bool>::const_iterator sit = g.subTargets.begin(); sit != g.subTargets.end(); ++sit) {
                subArr.append(SwJsonValue(sit.key().toStdString()));
            }
            x["subTargets"] = SwJsonValue(subArr);
            out.append(SwJsonValue(x));
        }
    }

    if (json) {
        std::cout << SwApiJson::toJson(out, pretty).toStdString() << "\n";
        return 0;
    }

    for (size_t i = 0; i < out.size(); ++i) {
        const SwJsonValue v = out[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        std::cout << "domain=" << SwString(o["domain"].toString()).toStdString()
                  << " object=" << SwString(o["object"].toString()).toStdString()
                  << " signal=" << SwString(o["signal"].toString()).toStdString() << "\n";
    }
    return 0;
}

void SwApiGraphCommand::start() {
    const SwStringList& a = args();
    const SwString sub = a.isEmpty() ? SwString("connections") : a[0];
    if (sub != "connections") {
        printUsage_();
        finish(2);
        return;
    }

    const int code = cmdConnections_();
    finish(code);
}
