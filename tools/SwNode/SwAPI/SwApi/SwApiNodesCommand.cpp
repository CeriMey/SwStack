#include "SwApiNodesCommand.h"

#include "SwApiJson.h"

#include "SwIpcRpc.h"

#include <iostream>

SwApiNodesCommand::SwApiNodesCommand(const SwApiCli& cli,
                                     SwApiIpcInspector& inspector,
                                     const SwStringList& args,
                                     SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiNodesCommand::~SwApiNodesCommand() = default;

void SwApiNodesCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi node list [--domain <sys>] [--ns <prefix>] [--include-stale] [--json] [--pretty]\n"
        << "  swapi node info <target> [--domain <sys>] [--json] [--pretty]\n"
        << "  swapi node save-as-factory <target> [--domain <sys>] [--timeout_ms <ms>] [--json]\n"
        << "  swapi node reset-factory <target> [--domain <sys>] [--timeout_ms <ms>] [--json]\n";
}

static SwString normalizePrefix(SwString x) {
    x.replace("\\", "/");
    while (x.startsWith("/")) x = x.mid(1);
    while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
    return x;
}

static bool matchesNamespacePrefix(const SwString& nodeNs, const SwString& nsPrefix) {
    if (nsPrefix.isEmpty()) return true;
    if (nodeNs == nsPrefix) return true;
    if (nodeNs.startsWith(nsPrefix + "/")) return true;
    return false;
}

int SwApiNodesCommand::cmdList_() {
    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const bool includeStale = cli().hasFlag("include-stale") || cli().hasFlag("stale");
    const SwString domain = cli().value("domain", cli().value("sys", SwString()));
    const SwString nsPrefix = normalizePrefix(cli().value("ns", cli().value("namespace", SwString())));

    SwJsonArray nodes = domain.isEmpty() ? inspector().nodesAllDomains(includeStale) : inspector().nodesForDomain(domain, includeStale);
    if (!nsPrefix.isEmpty()) {
        SwJsonArray filtered;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const SwJsonValue v = nodes[i];
            if (!v.isObject()) continue;
            const SwJsonObject o(v.toObject());
            const SwString nodeNs = normalizePrefix(SwString(o["nameSpace"].toString()));
            if (!matchesNamespacePrefix(nodeNs, nsPrefix)) continue;
            filtered.append(v);
        }
        nodes = filtered;
    }
    if (json) {
        std::cout << SwApiJson::toJson(nodes, pretty).toStdString() << "\n";
        return 0;
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        const SwJsonValue v = nodes[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        std::cout << SwString(o["target"].toString()).toStdString() << "\n";
    }
    return 0;
}

int SwApiNodesCommand::cmdInfo_() {
    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 2) {
        std::cerr << "swapi node info: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi node info: " << err.toStdString() << "\n";
        return 2;
    }

    SwJsonObject info;
    if (!inspector().nodeInfo(target, info, err, /*includeStale=*/false)) {
        std::cerr << "swapi node info: " << err.toStdString() << "\n";
        return 3;
    }

    if (json) {
        std::cout << SwApiJson::toJson(info, pretty).toStdString() << "\n";
        return 0;
    }

    std::cout
        << "target: " << SwString(info["target"].toString()).toStdString() << "\n"
        << "alive: " << (info["alive"].toBool() ? "true" : "false") << "\n"
        << "lastSeenMs: " << static_cast<uint64_t>(info["lastSeenMs"].toDouble()) << "\n"
        << "signals: " << info["signalCount"].toInt() << "\n"
        << "rpcs: " << info["rpcCount"].toInt() << "\n";
    return 0;
}

int SwApiNodesCommand::cmdSaveAsFactory_() {
    const bool json = cli().hasFlag("json");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 2) {
        std::cerr << "swapi node save-as-factory: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi node save-as-factory: " << err.toStdString() << "\n";
        return 2;
    }

    sw::ipc::RpcMethodClient<bool> rpc(target.domain, target.object, "system/saveAsFactory", "swapi");
    const bool ok = rpc.call(timeoutMs);

    if (json) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["target"] = SwJsonValue(target.toString().toStdString());
        std::cout << SwApiJson::toJson(o, cli().hasFlag("pretty")).toStdString() << "\n";
    } else {
        std::cout << (ok ? "ok" : "failed") << "\n";
    }
    return ok ? 0 : 3;
}

int SwApiNodesCommand::cmdResetFactory_() {
    const bool json = cli().hasFlag("json");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 2) {
        std::cerr << "swapi node reset-factory: missing <target>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[1], defaultDomain, target, err)) {
        std::cerr << "swapi node reset-factory: " << err.toStdString() << "\n";
        return 2;
    }

    sw::ipc::RpcMethodClient<bool> rpc(target.domain, target.object, "system/resetFactory", "swapi");
    const bool ok = rpc.call(timeoutMs);

    if (json) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["target"] = SwJsonValue(target.toString().toStdString());
        std::cout << SwApiJson::toJson(o, cli().hasFlag("pretty")).toStdString() << "\n";
    } else {
        std::cout << (ok ? "ok" : "failed") << "\n";
    }
    return ok ? 0 : 3;
}

void SwApiNodesCommand::start() {
    const SwStringList& a = args();
    if (a.isEmpty()) {
        printUsage_();
        finish(2);
        return;
    }

    const SwString sub = a[0];
    int code = 2;
    if (sub == "list") code = cmdList_();
    else if (sub == "info") code = cmdInfo_();
    else if (sub == "save-as-factory") code = cmdSaveAsFactory_();
    else if (sub == "reset-factory") code = cmdResetFactory_();
    else {
        printUsage_();
        finish(2);
        return;
    }

    finish(code);
}
