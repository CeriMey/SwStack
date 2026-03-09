#include "SwApiContainerCommand.h"

#include "SwApiJson.h"

#include "SwIpcRpc.h"

#include <iostream>

SwApiContainerCommand::SwApiContainerCommand(const SwApiCli& cli,
                                             SwApiIpcInspector& inspector,
                                             const SwStringList& args,
                                             SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiContainerCommand::~SwApiContainerCommand() = default;

void SwApiContainerCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi container status <containerTarget> [--domain <sys>] [--timeout_ms <ms>] [--pretty] [--json]\n"
        << "  swapi container plugins list <containerTarget> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container plugins info <containerTarget> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container plugin load <containerTarget> <path> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container plugin stop <containerTarget> <query> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container plugin restart <containerTarget> <query> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container components list <containerTarget> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container component load <containerTarget> <type> <ns> <name> [--params <json>] [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container component unload <containerTarget> <targetObject> [--domain <sys>] [--timeout_ms <ms>]\n"
        << "  swapi container component restart <containerTarget> <targetObject> [--domain <sys>] [--timeout_ms <ms>]\n";
}

static bool parseContainerTarget(SwApiIpcInspector& inspector,
                                const SwApiCli& cli,
                                const SwString& raw,
                                SwApiIpcInspector::Target& out,
                                SwString& err) {
    const SwString defaultDomain = cli.value("domain", cli.value("sys", SwString()));
    return inspector.parseTarget(raw, defaultDomain, out, err);
}

static void printJsonStringMaybePretty(const SwString& rawJson, bool pretty) {
    SwJsonDocument doc;
    SwString err;
    if (!doc.loadFromJson(rawJson.trimmed().toStdString(), err)) {
        std::cout << rawJson.toStdString() << "\n";
        return;
    }
    std::cout << doc.toJson(pretty ? SwJsonDocument::JsonFormat::Pretty : SwJsonDocument::JsonFormat::Compact).toStdString() << "\n";
}

int SwApiContainerCommand::cmdStatus_() {
    const bool pretty = cli().hasFlag("pretty");
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 2) {
        std::cerr << "swapi container status: missing <containerTarget>\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!parseContainerTarget(inspector(), cli(), args()[1], target, err)) {
        std::cerr << "swapi container status: " << err.toStdString() << "\n";
        return 2;
    }

    sw::ipc::RpcMethodClient<SwString> rpc(target.domain, target.object, "status", "swapi");
    const SwString resp = rpc.call(timeoutMs);
    if (resp.isEmpty()) {
        std::cerr << "swapi container status: rpc failed: " << rpc.lastError().toStdString() << "\n";
        return 3;
    }

    printJsonStringMaybePretty(resp, pretty);
    return 0;
}

int SwApiContainerCommand::cmdPlugins_() {
    const bool pretty = cli().hasFlag("pretty");
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 3) {
        std::cerr << "swapi container plugins: missing <list|info> <containerTarget>\n";
        return 2;
    }

    const SwString action = args()[1];
    const SwString rawTarget = args()[2];

    SwApiIpcInspector::Target target;
    SwString err;
    if (!parseContainerTarget(inspector(), cli(), rawTarget, target, err)) {
        std::cerr << "swapi container plugins: " << err.toStdString() << "\n";
        return 2;
    }

    if (action == "list") {
        sw::ipc::RpcMethodClient<SwString> rpc(target.domain, target.object, "listPlugins", "swapi");
        const SwString resp = rpc.call(timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container plugins list: rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    if (action == "info") {
        sw::ipc::RpcMethodClient<SwString> rpc(target.domain, target.object, "listPluginsInfo", "swapi");
        const SwString resp = rpc.call(timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container plugins info: rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    std::cerr << "swapi container plugins: unknown action\n";
    return 2;
}

int SwApiContainerCommand::cmdPlugin_() {
    const bool pretty = cli().hasFlag("pretty");
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 4) {
        std::cerr << "swapi container plugin: missing <load|stop|restart> <containerTarget> <arg>\n";
        return 2;
    }

    const SwString action = args()[1];
    const SwString rawTarget = args()[2];
    const SwString arg = args()[3];

    SwApiIpcInspector::Target target;
    SwString err;
    if (!parseContainerTarget(inspector(), cli(), rawTarget, target, err)) {
        std::cerr << "swapi container plugin: " << err.toStdString() << "\n";
        return 2;
    }

    if (action == "load") {
        sw::ipc::RpcMethodClient<SwString, SwString> rpc(target.domain, target.object, "loadPlugin", "swapi");
        const SwString resp = rpc.call(arg, timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container plugin load: rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    if (action == "stop") {
        sw::ipc::RpcMethodClient<SwString, SwString> rpc(target.domain, target.object, "stopPlugin", "swapi");
        const SwString resp = rpc.call(arg, timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container plugin stop: rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    if (action == "restart") {
        sw::ipc::RpcMethodClient<SwString, SwString> rpc(target.domain, target.object, "restartPlugin", "swapi");
        const SwString resp = rpc.call(arg, timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container plugin restart: rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    std::cerr << "swapi container plugin: unknown action\n";
    return 2;
}

int SwApiContainerCommand::cmdComponents_() {
    const bool pretty = cli().hasFlag("pretty");
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 3) {
        std::cerr << "swapi container components: missing list <containerTarget>\n";
        return 2;
    }

    const SwString action = args()[1];
    const SwString rawTarget = args()[2];

    if (action != "list") {
        std::cerr << "swapi container components: unknown action\n";
        return 2;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!parseContainerTarget(inspector(), cli(), rawTarget, target, err)) {
        std::cerr << "swapi container components: " << err.toStdString() << "\n";
        return 2;
    }

    sw::ipc::RpcMethodClient<SwString> rpc(target.domain, target.object, "listComponents", "swapi");
    const SwString resp = rpc.call(timeoutMs);
    if (resp.isEmpty()) {
        std::cerr << "swapi container components list: rpc failed: " << rpc.lastError().toStdString() << "\n";
        return 3;
    }

    printJsonStringMaybePretty(resp, pretty);
    return 0;
}

int SwApiContainerCommand::cmdComponent_() {
    const bool pretty = cli().hasFlag("pretty");
    const int timeoutMs = cli().intValue("timeout_ms", 2000);

    if (args().size() < 4) {
        std::cerr << "swapi container component: missing <load|unload|restart> <containerTarget> ...\n";
        return 2;
    }

    const SwString action = args()[1];
    const SwString rawTarget = args()[2];

    SwApiIpcInspector::Target target;
    SwString err;
    if (!parseContainerTarget(inspector(), cli(), rawTarget, target, err)) {
        std::cerr << "swapi container component: " << err.toStdString() << "\n";
        return 2;
    }

    if (action == "load") {
        if (args().size() < 6) {
            std::cerr << "swapi container component load: missing <type> <ns> <name>\n";
            return 2;
        }
        const SwString typeName = args()[3];
        const SwString ns = args()[4];
        const SwString name = args()[5];
        const SwString paramsJson = cli().value("params", "{}");

        sw::ipc::RpcMethodClient<SwString, SwString, SwString, SwString, SwString> rpc(
            target.domain, target.object, "loadComponent", "swapi");
        const SwString resp = rpc.call(typeName, ns, name, paramsJson, timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container component load: rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    if (action == "unload" || action == "restart") {
        const SwString obj = args()[3];
        const SwString method = (action == "unload") ? SwString("unloadComponent") : SwString("restartComponent");
        sw::ipc::RpcMethodClient<SwString, SwString> rpc(target.domain, target.object, method, "swapi");
        const SwString resp = rpc.call(obj, timeoutMs);
        if (resp.isEmpty()) {
            std::cerr << "swapi container component " << action.toStdString() << ": rpc failed: " << rpc.lastError().toStdString() << "\n";
            return 3;
        }
        printJsonStringMaybePretty(resp, pretty);
        return 0;
    }

    std::cerr << "swapi container component: unknown action\n";
    return 2;
}

void SwApiContainerCommand::start() {
    if (args().isEmpty()) {
        printUsage_();
        finish(2);
        return;
    }

    const SwString sub = args()[0];
    int code = 2;

    if (sub == "status") code = cmdStatus_();
    else if (sub == "plugins") code = cmdPlugins_();
    else if (sub == "plugin") code = cmdPlugin_();
    else if (sub == "components") code = cmdComponents_();
    else if (sub == "component") code = cmdComponent_();
    else {
        printUsage_();
        finish(2);
        return;
    }

    finish(code);
}
