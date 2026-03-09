#include "SwApiPingCommand.h"

#include "SwApiJson.h"

#include "SwSharedMemorySignal.h"

#include <iostream>

SwApiPingCommand::SwApiPingCommand(const SwApiCli& cli,
                                   SwApiIpcInspector& inspector,
                                   const SwStringList& args,
                                   SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiPingCommand::~SwApiPingCommand() = default;

void SwApiPingCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi ping <target> <n> <s> [--domain <sys>] [--json] [--pretty]\n";
}

void SwApiPingCommand::start() {
    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");
    const SwString defaultDomain = cli().value("domain", cli().value("sys", SwString()));

    if (args().size() < 3) {
        printUsage_();
        finish(2);
        return;
    }

    SwApiIpcInspector::Target target;
    SwString err;
    if (!inspector().parseTarget(args()[0], defaultDomain, target, err)) {
        std::cerr << "swapi ping: " << err.toStdString() << "\n";
        finish(2);
        return;
    }

    bool okN = false;
    const int n = args()[1].toInt(&okN);
    if (!okN) {
        std::cerr << "swapi ping: invalid <n>\n";
        finish(2);
        return;
    }
    const SwString s = args()[2];

    bool ok = false;
    try {
        sw::ipc::Registry reg(target.domain, target.object);
        sw::ipc::Signal<int, SwString> sig(reg, "ping");
        ok = sig.publish(n, s);
    } catch (...) {
        ok = false;
    }

    if (json) {
        SwJsonObject o;
        o["ok"] = SwJsonValue(ok);
        o["target"] = SwJsonValue(target.toString().toStdString());
        o["n"] = SwJsonValue(n);
        o["s"] = SwJsonValue(s.toStdString());
        std::cout << SwApiJson::toJson(o, pretty).toStdString() << "\n";
    } else {
        std::cout << (ok ? "ok" : "failed") << "\n";
    }

    finish(ok ? 0 : 3);
}

