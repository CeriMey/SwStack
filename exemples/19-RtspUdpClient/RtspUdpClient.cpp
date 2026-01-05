#include "SwCoreApplication.h"
#include "media/SwRtspUdpSource.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    std::string url = "rtsp://172.16.40.81:5004/video";
    std::string dumpPath = "rtsp_dump.h264";
    std::string localBind;
    uint16_t rtpPort = 0;
    uint16_t rtcpPort = 0;
    if (argc > 1 && argv[1]) {
        url = argv[1];
    }
    if (argc > 2 && argv[2]) {
        dumpPath = argv[2];
    }
    if (argc > 3 && argv[3]) {
        localBind = argv[3];
    }
    if (argc > 4 && argv[4]) {
        rtpPort = static_cast<uint16_t>(std::atoi(argv[4]));
        rtcpPort = static_cast<uint16_t>(rtpPort + 1);
    }

    std::cout << "[RtspUdpClient] Connecting to " << url << std::endl;
    std::cout << "[RtspUdpClient] Writing raw H264 to " << dumpPath << std::endl;

    FILE* raw = std::fopen(dumpPath.c_str(), "wb");
    auto outFile = std::unique_ptr<FILE, decltype(&std::fclose)>(raw, &std::fclose);
    if (!raw) {
        std::cerr << "[RtspUdpClient] Warning: failed to open output file, dump disabled." << std::endl;
    }

    std::atomic<uint64_t> packetCount{0};
    std::atomic<uint64_t> byteCount{0};

    auto source = std::make_shared<SwRtspUdpSource>(SwString(url.c_str()), nullptr);
    if (!localBind.empty()) {
        source->setLocalAddress(SwString(localBind.c_str()));
    }
    if (rtpPort != 0) {
        source->forceLocalBind(SwString(localBind.empty() ? "0.0.0.0" : localBind.c_str()), rtpPort, rtcpPort);
    }
    source->setPacketCallback([&](const SwVideoPacket& packet) {
        if (packet.codec() != SwVideoPacket::Codec::H264) {
            return;
        }
        ++packetCount;
        byteCount += static_cast<uint64_t>(packet.payload().size());
        if (outFile) {
            std::fwrite(packet.payload().constData(), 1, static_cast<size_t>(packet.payload().size()), outFile.get());
        }
        if ((packetCount % 100) == 0) {
            std::cout << "[RtspUdpClient] packets=" << packetCount.load()
                      << " bytes=" << byteCount.load() << std::endl;
        }
    });

    source->start();

    // Ctrl+C to quit; otherwise runs until the RTSP source stops.
    int code = app.exec();
    source->stop();
    std::cout << "[RtspUdpClient] Stopped. packets=" << packetCount.load()
              << " bytes=" << byteCount.load() << std::endl;
    return code;
}
