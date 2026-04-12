#include "SwCoreApplication.h"
#include "media/SwRtspUdpSource.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

struct RtspDumpFileCloser {
    void operator()(FILE* file) const {
        if (file) {
            std::fclose(file);
        }
    }
};

struct ClientOptions {
    std::string url{"rtsp://172.16.40.81:5004/video"};
    std::string dumpPath{"rtsp_dump.bin"};
    std::string localBind{};
    uint16_t rtpPort{0};
    uint16_t rtcpPort{0};
    uint64_t maxPackets{0};
    int timeoutMs{0};
    std::string transport{};
    std::string trustedCaFile{};
    bool showHelp{false};
};

static void printUsage() {
    std::cout
        << "Usage: RtspUdpClient [url] [dumpPath] [localBind] [rtpPort] [options]\n"
        << "Options:\n"
        << "  --url <rtsp-url>\n"
        << "  --dump <file>\n"
        << "  --bind <local-address>\n"
        << "  --rtp-port <port>\n"
        << "  --max-packets <count>\n"
        << "  --timeout-ms <milliseconds>\n"
        << "  --transport <udp|tcp>\n"
        << "  --trusted-ca <pem-file>\n"
        << "  --help\n";
}

static bool readNextArg(int argc,
                        char** argv,
                        int& index,
                        const char* option,
                        std::string& outValue,
                        std::string& error) {
    if (++index >= argc || !argv[index]) {
        error = std::string("Missing value for ") + option;
        return false;
    }
    outValue = argv[index];
    return true;
}

static bool parseArguments(int argc, char** argv, ClientOptions& options, std::string& error) {
    int positionalIndex = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
            return true;
        }
        if (arg == "--url") {
            if (!readNextArg(argc, argv, i, "--url", options.url, error)) {
                return false;
            }
            continue;
        }
        if (arg == "--dump") {
            if (!readNextArg(argc, argv, i, "--dump", options.dumpPath, error)) {
                return false;
            }
            continue;
        }
        if (arg == "--bind") {
            if (!readNextArg(argc, argv, i, "--bind", options.localBind, error)) {
                return false;
            }
            continue;
        }
        if (arg == "--rtp-port") {
            std::string value;
            if (!readNextArg(argc, argv, i, "--rtp-port", value, error)) {
                return false;
            }
            options.rtpPort = static_cast<uint16_t>(std::atoi(value.c_str()));
            options.rtcpPort = static_cast<uint16_t>(options.rtpPort + 1);
            continue;
        }
        if (arg == "--max-packets") {
            std::string value;
            if (!readNextArg(argc, argv, i, "--max-packets", value, error)) {
                return false;
            }
            options.maxPackets = static_cast<uint64_t>(std::strtoull(value.c_str(), nullptr, 10));
            continue;
        }
        if (arg == "--timeout-ms") {
            std::string value;
            if (!readNextArg(argc, argv, i, "--timeout-ms", value, error)) {
                return false;
            }
            options.timeoutMs = std::atoi(value.c_str());
            continue;
        }
        if (arg == "--transport") {
            if (!readNextArg(argc, argv, i, "--transport", options.transport, error)) {
                return false;
            }
            continue;
        }
        if (arg == "--trusted-ca") {
            if (!readNextArg(argc, argv, i, "--trusted-ca", options.trustedCaFile, error)) {
                return false;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            error = std::string("Unknown option: ") + arg;
            return false;
        }

        switch (positionalIndex++) {
        case 0:
            options.url = arg;
            break;
        case 1:
            options.dumpPath = arg;
            break;
        case 2:
            options.localBind = arg;
            break;
        case 3:
            options.rtpPort = static_cast<uint16_t>(std::atoi(arg.c_str()));
            options.rtcpPort = static_cast<uint16_t>(options.rtpPort + 1);
            break;
        default:
            error = std::string("Unexpected positional argument: ") + arg;
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    ClientOptions options;
    std::string argumentError;
    if (!parseArguments(argc, argv, options, argumentError)) {
        std::cerr << "[RtspUdpClient] " << argumentError << std::endl;
        printUsage();
        return 2;
    }
    if (options.showHelp) {
        printUsage();
        return 0;
    }

    std::cout << "[RtspUdpClient] Connecting to " << options.url << std::endl;
    std::cout << "[RtspUdpClient] Writing raw video elementary stream to " << options.dumpPath << std::endl;
    if (options.maxPackets > 0) {
        std::cout << "[RtspUdpClient] Will stop after " << options.maxPackets << " packets" << std::endl;
    }
    if (options.timeoutMs > 0) {
        std::cout << "[RtspUdpClient] Timeout set to " << options.timeoutMs << " ms" << std::endl;
    }

    FILE* raw = nullptr;
#if defined(_WIN32)
    if (fopen_s(&raw, options.dumpPath.c_str(), "wb") != 0) {
        raw = nullptr;
    }
#else
    raw = std::fopen(options.dumpPath.c_str(), "wb");
#endif
    std::unique_ptr<FILE, RtspDumpFileCloser> outFile(raw);
    if (!raw) {
        std::cerr << "[RtspUdpClient] Warning: failed to open output file, dump disabled." << std::endl;
    }

    std::atomic<uint64_t> packetCount{0};
    std::atomic<uint64_t> byteCount{0};
    std::atomic<int> firstCodec{-1};
    std::atomic<bool> sawFirstPacket{false};
    std::atomic<bool> exitRequested{false};

    auto source = std::make_shared<SwRtspUdpSource>(SwString(options.url.c_str()), nullptr);
    if (!options.localBind.empty()) {
        source->setLocalAddress(SwString(options.localBind.c_str()));
    }
    if (!options.transport.empty()) {
        source->setUseTcpTransport(options.transport == "tcp" || options.transport == "TCP");
    }
    if (!options.trustedCaFile.empty()) {
        source->setTrustedCaFile(SwString(options.trustedCaFile.c_str()));
    }
    if (options.rtpPort != 0) {
        source->forceLocalBind(SwString(options.localBind.empty() ? "0.0.0.0" : options.localBind.c_str()),
                               options.rtpPort,
                               options.rtcpPort);
    }
    source->setPacketCallback([&](const SwVideoPacket& packet) {
        if (packet.payload().isEmpty() || packet.carriesRawFrame()) {
            return;
        }
        sawFirstPacket.store(true, std::memory_order_release);
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
            std::fwrite(packet.payload().constData(),
                        1,
                        static_cast<size_t>(packet.payload().size()),
                        outFile.get());
        }
        if ((packetCount % 100) == 0) {
            std::cout << "[RtspUdpClient] packets=" << packetCount.load()
                      << " bytes=" << byteCount.load()
                      << " codec=" << codecValue << std::endl;
        }
        if (options.maxPackets > 0 &&
            packetCount.load(std::memory_order_relaxed) >= options.maxPackets &&
            !exitRequested.exchange(true, std::memory_order_acq_rel)) {
            app.exit(0);
        }
    });

    const auto startedAt = std::chrono::steady_clock::now();
    std::thread watchdog;
    if (options.timeoutMs > 0) {
        watchdog = std::thread([&]() {
            while (!exitRequested.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - startedAt)
                                           .count();
                if (elapsedMs < options.timeoutMs) {
                    continue;
                }
                if (!exitRequested.exchange(true, std::memory_order_acq_rel)) {
                    if (!sawFirstPacket.load(std::memory_order_acquire)) {
                        std::cerr << "[RtspUdpClient] Timeout waiting for first packet." << std::endl;
                        app.exit(3);
                    } else if (options.maxPackets > 0 &&
                               packetCount.load(std::memory_order_relaxed) < options.maxPackets) {
                        std::cerr << "[RtspUdpClient] Timeout before reaching requested packet count. current="
                                  << packetCount.load(std::memory_order_relaxed)
                                  << " expected=" << options.maxPackets << std::endl;
                        app.exit(4);
                    }
                }
                return;
            }
        });
    }

    source->start();

    const int code = app.exec();
    exitRequested.store(true, std::memory_order_release);
    if (watchdog.joinable()) {
        watchdog.join();
    }
    source->stop();
    std::cout << "[RtspUdpClient] Stopped. packets=" << packetCount.load()
              << " bytes=" << byteCount.load() << std::endl;
    return code;
}
