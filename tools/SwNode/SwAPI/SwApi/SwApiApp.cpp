#include "SwApiApp.h"

#include "SwApiAppsCommand.h"
#include "SwApiCommand.h"
#include "SwApiConfigCommand.h"
#include "SwApiContainerCommand.h"
#include "SwApiGraphCommand.h"
#include "SwApiNodesCommand.h"
#include "SwApiPingCommand.h"
#include "SwApiRpcsCommand.h"
#include "SwApiSignalsCommand.h"

#include "SwCoreApplication.h"
#include "SwTimer.h"

#include <iostream>

SwApiApp::SwApiApp(SwCoreApplication& app, int argc, char** argv, SwObject* parent)
    : SwObject(parent), app_(app), cli_(argc, argv) {
    SwTimer::singleShot(0, this, &SwApiApp::run_);
}

SwApiApp::~SwApiApp() = default;

void SwApiApp::printUsage_() const {
    const SwString exe = cli_.exe().isEmpty() ? SwString("swapi") : cli_.exe();
    std::cerr
        << "Usage:\n"
        << "  " << exe.toStdString() << " <group> <command> [args...] [--json] [--pretty]\n"
        << "\n"
        << "Groups:\n"
        << "  app        Introspect running domains (apps registry)\n"
        << "  node       List nodes / factory RPC helpers\n"
        << "  config     Read/write SwRemoteObject config (__config__|* / __cfg__|*)\n"
        << "  param      Alias for 'config'\n"
        << "  signal     List/echo SHM signals\n"
        << "  rpc        List RPC methods (__rpc__|*)\n"
        << "  ping       Send ping signal (debug)\n"
        << "  container  SwComponentContainer management RPCs\n"
        << "  graph      Inspect subscribers registry\n"
        << "\n"
        << "Examples:\n"
        << "  " << exe.toStdString() << " app list\n"
        << "  " << exe.toStdString() << " node list --domain demo\n"
        << "  " << exe.toStdString() << " node info demo/demo/container\n"
        << "  " << exe.toStdString() << " config dump demo/demo/container --pretty\n"
        << "  " << exe.toStdString() << " config get demo/demo/container composition/threading\n"
        << "  " << exe.toStdString() << " config set demo/demo/container composition/threading thread_per_plugin\n"
        << "  " << exe.toStdString() << " rpc list demo/demo/container\n"
        << "  " << exe.toStdString() << " rpc call demo/demo/container status --args []\n"
        << "  " << exe.toStdString() << " container status demo/demo/container --pretty\n"
        << "  " << exe.toStdString() << " graph connections --domain demo\n"
        << "  " << exe.toStdString() << " ping demo/demo/container 1 hello\n"
        << "\n"
        << "Notes:\n"
        << "  Targets are usually 'sys/ns/name' (domain + objectFqn).\n"
        << "  For 2-segment targets like 'ns/name', pass --domain <sys>.\n";
}

SwApiCommand* SwApiApp::makeCommand_() {
    const SwStringList& pos = cli_.positionals();
    if (pos.isEmpty()) return nullptr;

    const SwString root = pos[0];
    SwStringList args;
    for (size_t i = 1; i < pos.size(); ++i) {
        args.append(pos[i]);
    }

    if (root == "app") return new SwApiAppsCommand(cli_, inspector_, args, this);
    if (root == "node") return new SwApiNodesCommand(cli_, inspector_, args, this);
    if (root == "config" || root == "param") return new SwApiConfigCommand(cli_, inspector_, args, this);
    if (root == "signal") return new SwApiSignalsCommand(cli_, inspector_, args, this);
    if (root == "rpc") return new SwApiRpcsCommand(cli_, inspector_, args, this);
    if (root == "ping") return new SwApiPingCommand(cli_, inspector_, args, this);
    if (root == "container") return new SwApiContainerCommand(cli_, inspector_, args, this);
    if (root == "graph") return new SwApiGraphCommand(cli_, inspector_, args, this);

    return nullptr;
}

void SwApiApp::run_() {
    if (cli_.hasFlag("help") || cli_.positionals().isEmpty() || cli_.positionals()[0] == "help") {
        printUsage_();
        app_.exit(0);
        return;
    }

    cmd_ = makeCommand_();
    if (!cmd_) {
        std::cerr << "swapi: unknown command\n\n";
        printUsage_();
        app_.exit(2);
        return;
    }

    SwObject::connect(cmd_, &SwApiCommand::finished, [this](int code) { app_.exit(code); });
    cmd_->start();
}
