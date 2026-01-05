#include "SwCoreApplication.h"
#include "SwRegularExpression.h"
#include "SwString.h"

#include "IpcRingBufferCameraPublisher.h"

#include <algorithm>
#include <iostream>

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    IpcRingBufferCameraPublisher::Config cfg;
    const SwString target = app.getArgument("target", "demo/camera").trimmed();
    const SwString stream = app.getArgument("stream", "video").trimmed();
    cfg.capacity = static_cast<uint32_t>((std::max)(1, app.getArgument("cap", "100").toInt()));
    cfg.maxBytes = static_cast<uint32_t>((std::max)(1, app.getArgument("max", "10485760").toInt()));
    cfg.deviceIndex = static_cast<uint32_t>((std::max)(0, app.getArgument("device", "0").toInt()));
    cfg.verbose = (app.getArgument("quiet", "0").toInt() == 0);

    SwString normalized = target;
    normalized.replace('\\', '/');
    normalized.remove(SwRegularExpression("^/+|/+$"));
    while (normalized.contains("//")) normalized.replace("//", "/");
    const int slash = normalized.indexOf('/');
    if (slash <= 0 || slash >= normalized.size() - 1) {
        std::cerr << "[IpcRingBufferCamera] invalid --target (expected domain/object)\n";
        return 2;
    }
    cfg.domain = normalized.left(slash);
    cfg.object = normalized.mid(slash + 1);
    cfg.stream = stream.isEmpty() ? SwString("video") : stream;

    IpcRingBufferCameraPublisher publisher(cfg);
    if (!publisher.start()) {
        return 2;
    }

    std::cout << "[IpcRingBufferCamera] Running. Ctrl+C to stop.\n";
    const int code = app.exec();
    publisher.stop();
    return code;
}

