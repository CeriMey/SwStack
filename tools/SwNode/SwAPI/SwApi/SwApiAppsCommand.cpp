#include "SwApiAppsCommand.h"

#include "SwApiJson.h"

#include <iostream>

SwApiAppsCommand::SwApiAppsCommand(const SwApiCli& cli,
                                   SwApiIpcInspector& inspector,
                                   const SwStringList& args,
                                   SwObject* parent)
    : SwApiCommand(cli, inspector, args, parent) {}

SwApiAppsCommand::~SwApiAppsCommand() = default;

void SwApiAppsCommand::printUsage_() const {
    std::cerr
        << "Usage:\n"
        << "  swapi app list [--json] [--pretty]\n";
}

void SwApiAppsCommand::start() {
    const SwStringList& a = args();
    const SwString sub = a.isEmpty() ? SwString("list") : a[0];
    if (sub != "list") {
        printUsage_();
        finish(2);
        return;
    }

    const bool json = cli().hasFlag("json");
    const bool pretty = cli().hasFlag("pretty");

    const SwJsonArray apps = inspector().appsSnapshot();
    if (json) {
        std::cout << SwApiJson::toJson(apps, pretty).toStdString() << "\n";
        finish(0);
        return;
    }

    for (size_t i = 0; i < apps.size(); ++i) {
        const SwJsonValue v = apps[i];
        if (!v.isObject()) continue;
        const SwJsonObject o(v.toObject());
        const SwString domain = SwString(o["domain"].toString());
        const int clientCount = o.contains("clientCount") ? o["clientCount"].toInt() : 0;
        std::cout << domain.toStdString() << " (clients=" << clientCount << ")\n";
    }

    finish(0);
}
