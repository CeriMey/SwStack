#include "SwCoreApplication.h"
#include "media/SwRtspUdpSource.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

struct RtspDumpFileCloser {
    void operator()(FILE* file) const {
        if (file) {
            std::fclose(file);
        }
    }
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    std::string url = "rtsp://172.16.40.81:5004/video";
    std::string dumpPath = "rtsp_dump.bin";
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
    std::cout << "[RtspUdpClient] Writing raw video elementary stream to " << dumpPath << std::endl;

    FILE* raw = nullptr;
#if defined(_WIN32)
    if (fopen_s(&raw, dumpPath.c_str(), "wb") != 0) {
        raw = nullptr;
    }
#else
    raw = std::fopen(dumpPath.c_str(), "wb");
#endif
    std::unique_ptr<FILE, RtspDumpFileCloser> outFile(raw);
    if (!raw) {
        std::cerr << "[RtspUdpClient] Warning: failed to open output file, dump disabled." << std::endl;
    }

    std::atomic<uint64_t> packetCount{0};
    std::atomic<uint64_t> byteCount{0};
    std::atomic<int> firstCodec{-1};

    auto source = std::make_shared<SwRtspUdpSource>(SwString(url.c_str()), nullptr);
    if (!localBind.empty()) {
        source->setLocalAddress(SwString(localBind.c_str()));
    }
    if (rtpPort != 0) {
        source->forceLocalBind(SwString(localBind.empty() ? "0.0.0.0" : localBind.c_str()), rtpPort, rtcpPort);
    }
    source->setPacketCallback([&](const SwVideoPacket& packet) {
        if (packet.payload().isEmpty() || packet.carriesRawFrame()) {
            return;
        }
        const int codecValue = static_cast<int>(packet.codec());
        int expectedCodec = -1;
        if (firstCodec.compare_exchange_strong(expectedCodec, codecValue)) {
            std::cout << "[RtspUdpClient] first codec=" << codecValue
                      << " key=" << (packet.isKeyFrame() ? 1 : 0)
                      << " bytes=" << packet.payload().size() << std::endl;
        }
        ++packetCount;
        byteCount += static_cast<uint64_t>(packet.payload().size());
        if (outFile) {
            std::fwrite(packet.payload().constData(), 1, static_cast<size_t>(packet.payload().size()), outFile.get());
        }
        if ((packetCount % 100) == 0) {
            std::cout << "[RtspUdpClient] packets=" << packetCount.load()
                      << " bytes=" << byteCount.load()
                      << " codec=" << codecValue << std::endl;
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
