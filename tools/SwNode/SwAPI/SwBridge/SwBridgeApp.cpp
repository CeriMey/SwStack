#include "SwBridgeApp.h"

#include <iostream>

#include "SwBridgeHttpServer.h"

static SwString usageText() {
    return SwString(
        "Usage:\n"
        "  SwBridge.exe [port] [--api-key <key>]\n"
        "\n"
        "Defaults:\n"
        "  port=8088  (WebSocket on port+1)\n"
        "\n"
        "Open:\n"
        "  http://localhost:<port>/\n");
}

uint16_t SwBridgeApp::parsePort_(int argc, char** argv) {
    if (argc < 2) return 8088;
    try {
        int p = std::atoi(argv[1]);
        if (p <= 0 || p > 65535) return 8088;
        return static_cast<uint16_t>(p);
    } catch (...) {
        return 8088;
    }
}

SwBridgeApp::SwBridgeApp(int argc, char** argv, SwObject* parent)
    : SwObject(parent) {
    if (argc >= 2 && (SwString(argv[1]) == "-h" || SwString(argv[1]) == "--help")) {
        std::cout << usageText().toStdString();
    }

    const uint16_t port = parsePort_(argc, argv);
    server_ = new SwBridgeHttpServer(port, this);

    // Parse --api-key
    for (int i = 1; i < argc - 1; ++i) {
        if (SwString(argv[i]) == "--api-key") {
            server_->setApiKey(SwString(argv[i + 1]));
            break;
        }
    }

    if (!server_->start()) {
        std::cerr << "[SwBridge] failed to listen on port " << port << "\n";
    } else {
        std::cout << "[SwBridge] http://localhost:" << port << "/\n";
    }
}

SwBridgeApp::~SwBridgeApp() = default;
