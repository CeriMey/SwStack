#include "SwCoreApplication.h"

#include "DemoSubscriber.h"

#include <iostream>

static void usage() {
    std::cout
        << "Usage:\n"
        << "  ConfigurableObjectDemo.exe <sys>/<namespace> [objectName]\n"
        << "  ConfigurableObjectDemo.exe <sys>/<namespace>/<objectName>\n"
        << "  ConfigurableObjectDemo.exe <sys> <namespace> [objectName]\n"
        << "\n"
        << "Examples:\n"
        << "  ConfigurableObjectDemo.exe demo/device1 DemoDevice\n"
        << "  ConfigurableObjectDemo.exe demo/device1/DemoDevice\n"
        << "  ConfigurableObjectDemo.exe demo device1 DemoDevice\n"
        << "\n"
        << "Notes:\n"
        << "  - This is a debug subscriber: it listens forever (Ctrl+C to exit).\n"
        << "  - It acks \"ping\" with \"pong\" and remote config updates with \"configAck\".\n";
}

static bool splitTarget(const SwString& target, SwString& outDomain, SwString& outObject) {
    const std::string s = target.toStdString();
    const size_t slash = s.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= s.size()) return false;
    outDomain = SwString(s.substr(0, slash));
    outObject = SwString(s.substr(slash + 1));
    return true;
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    SwString sysName;
    SwString nameSpace;
    SwString objectName("DemoDevice");

    int argi = 1;
    if (argc >= 2 && SwString(argv[1]) == "sub") argi = 2; // backward-compat

    const int remaining = argc - argi;
    if (remaining < 1) {
        usage();
        return 1;
    }

    const SwString first(argv[argi]);
    if (splitTarget(first, sysName, nameSpace)) {
        // Allow <sys>/<namespace>/<objectName> in a single argument (namespace may contain '/').
        {
            SwString maybeSys;
            SwString rest;
            if (splitTarget(first, maybeSys, rest)) {
                SwString r = rest;
                r.replace("\\", "/");
                SwList<SwString> partsRaw = r.split('/');
                SwList<SwString> parts;
                for (size_t i = 0; i < partsRaw.size(); ++i) {
                    if (!partsRaw[i].isEmpty()) parts.append(partsRaw[i]);
                }
                if (parts.size() >= 2) {
                    objectName = parts[parts.size() - 1];
                    SwString ns;
                    for (size_t i = 0; i + 1 < parts.size(); ++i) {
                        if (!ns.isEmpty()) ns += "/";
                        ns += parts[i];
                    }
                    nameSpace = ns;
                } else {
                    // Only <sys>/<namespace>, keep default objectName unless provided as next arg.
                    if (remaining >= 2) objectName = SwString(argv[argi + 1]);
                }
            }
        }
    } else {
        if (remaining < 2) {
            usage();
            return 1;
        }
        sysName = SwString(argv[argi + 0]);
        nameSpace = SwString(argv[argi + 1]);
        if (remaining >= 3) objectName = SwString(argv[argi + 2]);
    }

    if (sysName.isEmpty() || nameSpace.isEmpty() || objectName.isEmpty()) {
        usage();
        return 1;
    }

    DemoSubscriber sub(sysName, nameSpace, objectName);
    sub.start();

    return app.exec();
}
