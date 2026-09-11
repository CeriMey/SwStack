#include "SwCoreApplication.h"

#include "SwBridgeApp.h"
#include <iostream>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (SwString(argv[i]) == "--help" || SwString(argv[i]) == "-h") {
            std::cout << "Usage: SwBridge [port] [--api-key KEY] [--rosbridge-port PORT]\n"
                         "                [--rosbridge-domain DOMAIN] [--rosbridge-map FILE]\n"
                         "                [--sys DOMAIN] [--duration_ms MS]\n"
                         "Defaults: HTTP 8088, native WebSocket HTTP+1, rosbridge 5010, domain visionmax.\n"
                         "Use --rosbridge-port 0 to disable rosbridge.\n"
                         "SwLaunch identity/config arguments are accepted. --sys sets the default IPC domain.\n";
            return 0;
        }
    }
    SwCoreApplication app(argc, argv);
    SwBridgeApp mainObj(argc, argv);
    if (!mainObj.started()) return 1;
    return app.exec();
}
