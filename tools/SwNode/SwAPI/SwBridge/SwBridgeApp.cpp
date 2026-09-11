#include "SwBridgeApp.h"
#include "SwBridgeHttpServer.h"
#include "SwBridgeRpcClient.h"
#include "rosbridge/SwRosBridgeServer.h"
#include "SwTimer.h"
#include <iostream>
#include <limits>

namespace {
uint16_t portNumber(const SwString& text, bool allowZero = false) {
    if (!text.isInt()) throw std::runtime_error("port must be an integer");
    const int port = text.toInt();
    if (port < (allowZero ? 0 : 1) || port > 65535) throw std::runtime_error("port out of range");
    return static_cast<uint16_t>(port);
}
}

SwBridgeApp::SwBridgeApp(int argc, char** argv, SwObject* parent) : SwObject(parent) {
    try {
        uint16_t port = 8088, rosPort = 5010;
        SwString apiKey, domain("visionmax"), mapping;
        bool havePort = false, explicitDomain = false;
        int durationMs = 0;
        for (int i = 1; i < argc; ++i) {
            SwString option(argv[i]);
            if (!option.startsWith("--") && !havePort) { port = portNumber(option); havePort = true; continue; }
            const std::string argument = option.toStdString();
            const auto equals = argument.find('=');
            SwString value;
            if (equals != std::string::npos) {
                option = SwString(argument.substr(0, equals));
                value = SwString(argument.substr(equals + 1));
            } else {
                if (i + 1 >= argc) throw std::runtime_error("missing option value");
                value = SwString(argv[++i]);
            }
            if (option == "--api-key") apiKey = value;
            else if (option == "--rosbridge-port") rosPort = portNumber(value, true);
            else if (option == "--rosbridge-domain") { domain = value; explicitDomain = true; }
            else if (option == "--rosbridge-map") mapping = value;
            else if (option == "--sys") { if (!explicitDomain) domain = value; }
            else if (option == "--duration_ms") {
                size_t consumed = 0;
                const auto duration = std::stoll(value.toStdString(), &consumed);
                if (consumed != value.size() || duration < 0 || duration > std::numeric_limits<int>::max())
                    throw std::runtime_error("duration_ms must be a nonnegative 32-bit integer");
                durationMs = static_cast<int>(duration);
            } else if (option == "--ns" || option == "--name" || option == "--config_file" || option == "--config_root") {
                // SwLaunch supplies process identity and supervisor settings.
                // This gateway uses --sys for discovery; its ROS aliases remain in --rosbridge-map.
            }
            else throw std::runtime_error(("unknown option: " + option).toStdString());
        }
        if (port == 65535 || (rosPort && (rosPort == port || rosPort == port + 1)))
            throw std::runtime_error("HTTP, native WebSocket and rosbridge ports must be distinct");
        if (domain.isEmpty() || domain.contains('/')) throw std::runtime_error("invalid rosbridge IPC domain");
        rpc_.reset(new SwBridgeRpcClient());
        server_.reset(new SwBridgeHttpServer(port, *rpc_));
        server_->setApiKey(apiKey);
        if (rosPort) {
            rosbridge_.reset(new swros::Server(domain, mapping, apiKey, *rpc_));
            if (!rosbridge_->listen(rosPort)) throw std::runtime_error("cannot listen on rosbridge port");
        }
        if (!server_->start()) throw std::runtime_error("cannot listen on HTTP/native WebSocket ports");
        std::cout << "[SwBridge] http://localhost:" << port << "/\n";
        if (rosPort) std::cout << "[SwBridge] rosbridge ws://localhost:" << rosPort << " (domain " << domain << ")\n";
        started_ = true;
        if (durationMs > 0) SwTimer::singleShot(durationMs, [] { SwCoreApplication::instance()->quit(); });
    } catch (const std::exception& error) {
        std::cerr << "[SwBridge] " << error.what() << "\n";
    }
}

SwBridgeApp::~SwBridgeApp() = default;
