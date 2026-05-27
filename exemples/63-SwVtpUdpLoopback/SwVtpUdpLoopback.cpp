#include "media/swvtp/SwVtpAv1.h"
#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"
#include "media/server/SwMediaServer.h"
#include "media/swvtp/SwVtpKlv.h"
#include "media/swvtp/SwVtpServerTransport.h"
#include "media/swvtp/SwVtpUdpTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct IvfFrame {
    uint64_t timestamp{0};
    SwByteArray payload{};
};

struct UsStats {
    uint64_t count{0};
    uint64_t totalUs{0};
    uint64_t minUs{std::numeric_limits<uint64_t>::max()};
    uint64_t maxUs{0};

    void add(uint64_t valueUs) {
        ++count;
        totalUs += valueUs;
        minUs = std::min(minUs, valueUs);
        maxUs = std::max(maxUs, valueUs);
    }

    double avgMs() const {
        return count == 0U ? 0.0 : static_cast<double>(totalUs) / static_cast<double>(count) / 1000.0;
    }
    double minMs() const {
        return count == 0U ? 0.0 : static_cast<double>(minUs) / 1000.0;
    }
    double maxMs() const {
        return count == 0U ? 0.0 : static_cast<double>(maxUs) / 1000.0;
    }
};

uint64_t nowUs() {
    const std::chrono::steady_clock::duration elapsed =
        std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8U));
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

uint64_t readLe64(const uint8_t* data) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8U) | static_cast<uint64_t>(data[i]);
    }
    return value;
}

void appendBe64(SwByteArray& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.append(static_cast<char>((value >> static_cast<unsigned int>(shift)) & 0xFFU));
    }
}

std::string ipv4ToString(uint32_t ipv4) {
    return std::to_string(swVtpIpv4Octet(ipv4, 0)) + "." +
           std::to_string(swVtpIpv4Octet(ipv4, 1)) + "." +
           std::to_string(swVtpIpv4Octet(ipv4, 2)) + "." +
           std::to_string(swVtpIpv4Octet(ipv4, 3));
}

bool parsePort(const char* text, uint16_t& outPort) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (!end || *end != '\0' || value == 0UL || value > 65535UL) {
        return false;
    }
    outPort = static_cast<uint16_t>(value);
    return true;
}

bool parsePositiveSize(const char* text, std::size_t& outValue) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (!end || *end != '\0' || value == 0UL) {
        return false;
    }
    outValue = static_cast<std::size_t>(value);
    return true;
}

bool loadIvfAv1Frames(const char* path, std::vector<IvfFrame>& frames, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open IVF file";
        return false;
    }

    uint8_t header[32] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        error = "truncated IVF header";
        return false;
    }
    if (std::memcmp(header, "DKIF", 4) != 0) {
        error = "missing IVF magic";
        return false;
    }
    if (std::memcmp(header + 8, "AV01", 4) != 0 &&
        std::memcmp(header + 8, "av01", 4) != 0) {
        error = "IVF file is not AV1";
        return false;
    }

    const uint16_t headerBytes = readLe16(header + 6);
    if (headerBytes < 32U) {
        error = "invalid IVF header size";
        return false;
    }
    if (headerBytes > 32U) {
        file.seekg(static_cast<std::streamoff>(headerBytes - 32U), std::ios::cur);
        if (!file) {
            error = "cannot skip extended IVF header";
            return false;
        }
    }

    while (true) {
        uint8_t frameHeader[12] = {};
        file.read(reinterpret_cast<char*>(frameHeader), sizeof(frameHeader));
        const std::streamsize headerRead = file.gcount();
        if (headerRead == 0 && file.eof()) {
            break;
        }
        if (headerRead != static_cast<std::streamsize>(sizeof(frameHeader))) {
            error = "truncated IVF frame header";
            return false;
        }

        const uint32_t frameBytes = readLe32(frameHeader);
        if (frameBytes == 0U) {
            error = "empty IVF frame";
            return false;
        }

        std::vector<char> buffer(frameBytes);
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (file.gcount() != static_cast<std::streamsize>(buffer.size())) {
            error = "truncated IVF frame payload";
            return false;
        }

        IvfFrame frame;
        frame.timestamp = readLe64(frameHeader + 4);
        frame.payload.append(buffer.data(), buffer.size());
        frames.push_back(frame);
    }

    if (frames.empty()) {
        error = "IVF file contains no frames";
        return false;
    }
    return true;
}

SwByteArray makeControlDatagram(SwVtpMessageType type, const SwByteArray& payload) {
    SwVtpDatagram datagram;
    datagram.header.version = kSwVtpVersion1;
    datagram.header.messageType = type;
    datagram.header.trackType = SwVtpTrackType::Control;
    datagram.header.codec = SwVtpCodec::Unknown;
    datagram.header.sendTimeUs = nowUs();
    datagram.payload = payload;
    return swVtpSerializeDatagram(datagram);
}

SwMediaTrack makeKlvTrack() {
    SwMediaTrack track;
    track.id = "klv";
    track.type = SwMediaTrack::Type::Metadata;
    track.codec = "klv";
    track.clockRate = 90000;
    track.selected = true;
    track.availability = SwMediaTrack::Availability::Available;
    return track;
}

SwMediaPacket makeKlvPacket(uint64_t frameIndex, uint64_t captureUs) {
    static const unsigned char key[16] = {
        0x06, 0x0E, 0x2B, 0x34, 0x02, 0x0B, 0x01, 0x01,
        0x0E, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00
    };

    SwByteArray payload;
    payload.append(reinterpret_cast<const char*>(key), sizeof(key));
    payload.append(static_cast<char>(16));
    appendBe64(payload, frameIndex);
    appendBe64(payload, captureUs);

    SwMediaPacket packet;
    packet.setType(SwMediaPacket::Type::Metadata);
    packet.setTrackId("klv");
    packet.setCodec("klv");
    packet.setClockRate(90000);
    packet.setPayload(payload);
    packet.setPts(static_cast<std::int64_t>(captureUs));
    packet.setDts(static_cast<std::int64_t>(captureUs));
    return packet;
}

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  SwVtpUdpLoopback server <bindIp> <port> <ivfPath> [unicastMask] [frames] [mtu]\n"
        << "  SwVtpUdpLoopback client <serverIp> <serverPort> [clientIp] [clientPort] [expectedFrames]\n";
}

int runServer(int argc, char** argv) {
    if (argc < 5) {
        printUsage();
        return EXIT_FAILURE;
    }

    uint32_t bindIp = 0;
    uint16_t bindPort = 0;
    if (!swVtpParseIpv4Address(argv[2], bindIp) || !parsePort(argv[3], bindPort)) {
        std::cerr << "[SwVTP server] invalid bind endpoint\n";
        return EXIT_FAILURE;
    }

    std::vector<IvfFrame> frames;
    std::string error;
    if (!loadIvfAv1Frames(argv[4], frames, error)) {
        std::cerr << "[SwVTP server] IVF error: " << error << "\n";
        return EXIT_FAILURE;
    }

    uint32_t unicastMask = kSwVtpIpv4Any;
    if (argc > 5 && !swVtpParseIpv4Address(argv[5], unicastMask)) {
        std::cerr << "[SwVTP server] invalid unicast mask\n";
        return EXIT_FAILURE;
    }
    std::size_t frameLimit = frames.size();
    if (argc > 6 && !parsePositiveSize(argv[6], frameLimit)) {
        std::cerr << "[SwVTP server] invalid frame limit\n";
        return EXIT_FAILURE;
    }

    std::size_t mtu = 512;
    if (argc > 7 && !parsePositiveSize(argv[7], mtu)) {
        std::cerr << "[SwVTP server] invalid MTU\n";
        return EXIT_FAILURE;
    }

    SwMediaServerConfig serverConfig;
    serverConfig.endpoint.protocol = SwMediaTransportProtocol::SwVtp;
    serverConfig.endpoint.deliveryMode = SwMediaTransportDeliveryMode::Unicast;
    serverConfig.endpoint.bindAddress = ipv4ToString(bindIp).c_str();
    serverConfig.endpoint.host = ipv4ToString(unicastMask).c_str();
    serverConfig.endpoint.port = bindPort;
    serverConfig.mtuBytes = static_cast<uint16_t>(std::min<std::size_t>(mtu, 65535U));
    serverConfig.defaultLatencyBudgetMs = 90;
    serverConfig.maxClients = 1;

    SwVideoPublishStream stream;
    stream.id = "main";
    stream.trackId = "video";
    stream.codec = SwVideoPacket::Codec::AV1;
    stream.width = 1920;
    stream.height = 1080;
    stream.fpsNumerator = 30;
    stream.fpsDenominator = 1;
    stream.startBitrateKbps = 4000;
    stream.minBitrateKbps = 800;
    stream.maxBitrateKbps = 12000;
    stream.latencyBudgetMs = 90;

    auto transport = std::make_shared<SwVtpServerTransport>();
    SwMediaServer server(serverConfig);
    server.setTransport(transport);
    if (!server.addVideoStream(stream) ||
        !server.addMetadataTrack(makeKlvTrack()) ||
        !server.start()) {
        std::cerr << "[SwVTP server] failed to start SwMediaServer on "
                  << ipv4ToString(bindIp) << ":" << bindPort << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[SwVTP server] listening=" << ipv4ToString(bindIp) << ":" << bindPort
              << " mode=unicast mask=" << ipv4ToString(unicastMask)
              << " frames=" << frameLimit << " mtu=" << mtu << "\n";

    const uint64_t waitStart = nowUs();
    while (transport->activeClientCount() == 0U && nowUs() - waitStart < 10000000ULL) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (transport->activeClientCount() == 0U) {
        std::cerr << "[SwVTP server] no valid client announcement received\n";
        server.stop();
        return EXIT_FAILURE;
    }

    std::cout << "[SwVTP server] acceptedClients=" << transport->activeClientCount() << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint64_t videoBytes = 0;
    uint64_t klvPackets = 0;
    uint64_t klvBytes = 0;
    const uint64_t sendStart = nowUs();
    for (std::size_t i = 0; i < frameLimit; ++i) {
        const IvfFrame& frame = frames[i % frames.size()];
        const uint64_t captureUs = nowUs() - 4000ULL;
        SwVideoPacket packet(SwVideoPacket::Codec::AV1,
                             frame.payload,
                             static_cast<std::int64_t>(captureUs),
                             static_cast<std::int64_t>(captureUs),
                             i == 0U);

        if (!server.publishVideoPacket("main", packet)) {
            std::cerr << "[SwVTP server] publish failed at frame " << i << "\n";
            server.stop();
            return EXIT_FAILURE;
        }
        videoBytes += frame.payload.size();

        const SwMediaPacket klv = makeKlvPacket(static_cast<uint64_t>(i), captureUs);
        if (server.publishMediaPacket("klv", klv)) {
            ++klvPackets;
            klvBytes += klv.payload().size();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    const uint64_t sendElapsedUs = std::max<uint64_t>(1U, nowUs() - sendStart);
    const double videoKbps =
        static_cast<double>(videoBytes * 8ULL) / static_cast<double>(sendElapsedUs) * 1000.0;
    const SwVideoServerMetrics metrics = server.metrics();

    std::cout << "[SwVTP server metrics] framesSent=" << metrics.framesSent
              << " datagramsSent=" << metrics.transport.datagramsSent
              << " videoBytes=" << videoBytes
              << " klvPackets=" << klvPackets
              << " klvBytes=" << klvBytes
              << " udpPayloadBytes=" << metrics.transport.bytesSent
              << " elapsedMs=" << (sendElapsedUs / 1000.0)
              << " videoKbps=" << videoKbps
              << " udpPayloadKbps="
              << (static_cast<double>(metrics.transport.bytesSent * 8ULL) /
                  static_cast<double>(sendElapsedUs) * 1000.0)
              << " targetKbps=" << metrics.targetBitrateKbps
              << " encoderKbps=" << metrics.encoderBitrateKbps << "\n";

    server.stop();
    return EXIT_SUCCESS;
}

int runClient(int argc, char** argv) {
    if (argc < 4) {
        printUsage();
        return EXIT_FAILURE;
    }

    uint32_t serverIp = 0;
    uint16_t serverPort = 0;
    if (!swVtpParseIpv4Address(argv[2], serverIp) || !parsePort(argv[3], serverPort)) {
        std::cerr << "[SwVTP client] invalid server endpoint\n";
        return EXIT_FAILURE;
    }

    uint32_t clientIp = 0;
    if (!swVtpParseIpv4Address(argc > 4 ? argv[4] : "127.0.0.1", clientIp)) {
        std::cerr << "[SwVTP client] invalid client announcement IP\n";
        return EXIT_FAILURE;
    }
    uint16_t clientPort = 0;
    if (argc > 5 && !parsePort(argv[5], clientPort)) {
        std::cerr << "[SwVTP client] invalid client port\n";
        return EXIT_FAILURE;
    }
    std::size_t expectedFrames = 30;
    if (argc > 6 && !parsePositiveSize(argv[6], expectedFrames)) {
        std::cerr << "[SwVTP client] invalid expected frame count\n";
        return EXIT_FAILURE;
    }

    SwVtpUdpTransport udp;
    if (!udp.open(kSwVtpIpv4Any, clientPort)) {
        std::cerr << "[SwVTP client] failed to bind receive socket\n";
        return EXIT_FAILURE;
    }
    clientPort = udp.localPort();

    SwVtpClockEstimate clockEstimate;
    SwVtpClockSyncPing ping;
    ping.syncId = 1;
    uint64_t lastPingUs = 0;
    const uint64_t clockWaitStart = nowUs();
    while (nowUs() - clockWaitStart < 2000000ULL) {
        const uint64_t currentUs = nowUs();
        if (lastPingUs == 0U || currentUs - lastPingUs >= 200000ULL) {
            ping.clientSendTimeUs = currentUs;
            if (!udp.send(makeControlDatagram(SwVtpMessageType::Ping,
                                              swVtpSerializeClockSyncPing(ping)),
                          serverIp,
                          serverPort)) {
                std::cerr << "[SwVTP client] failed to send clock ping\n";
                return EXIT_FAILURE;
            }
            lastPingUs = currentUs;
        }

        SwVtpUdpPacket packet;
        if (!udp.receive(50, packet)) {
            continue;
        }
        SwVtpDatagram datagram;
        SwVtpClockSyncPong pong;
        if (swVtpParseDatagram(packet.bytes, datagram) &&
            datagram.header.messageType == SwVtpMessageType::Pong &&
            swVtpParseClockSyncPong(datagram.payload, pong)) {
            SwVtpClockSyncSample sample;
            sample.syncId = pong.syncId;
            sample.clientSendTimeUs = pong.clientSendTimeUs;
            sample.serverReceiveTimeUs = pong.serverReceiveTimeUs;
            sample.serverSendTimeUs = pong.serverSendTimeUs;
            sample.clientReceiveTimeUs = nowUs();
            clockEstimate = swVtpEstimateClock(sample);
            break;
        }
    }

    if (!clockEstimate.valid) {
        std::cerr << "[SwVTP client] failed to estimate clock\n";
        return EXIT_FAILURE;
    }

    SwVtpClientAnnouncement client;
    client.streamId = 1;
    client.receivePort = clientPort;
    client.clientIpv4 = clientIp;

    SwVtpStreamConfig config;
    uint64_t lastHelloUs = 0;
    const uint64_t acceptWaitStart = nowUs();
    while (nowUs() - acceptWaitStart < 2000000ULL) {
        const uint64_t currentUs = nowUs();
        if (lastHelloUs == 0U || currentUs - lastHelloUs >= 200000ULL) {
            if (!udp.send(makeControlDatagram(SwVtpMessageType::Hello,
                                              swVtpSerializeClientAnnouncement(client)),
                          serverIp,
                          serverPort)) {
                std::cerr << "[SwVTP client] failed to send announcement\n";
                return EXIT_FAILURE;
            }
            lastHelloUs = currentUs;
        }

        SwVtpUdpPacket packet;
        if (!udp.receive(50, packet)) {
            continue;
        }
        SwVtpDatagram datagram;
        if (swVtpParseDatagram(packet.bytes, datagram) &&
            datagram.header.messageType == SwVtpMessageType::Accept &&
            swVtpParseStreamConfigPayload(datagram.payload, config)) {
            break;
        }
    }

    if (!config.isValid()) {
        std::cerr << "[SwVTP client] server did not accept stream\n";
        return EXIT_FAILURE;
    }

    std::cout << "[SwVTP client] local=" << ipv4ToString(clientIp) << ":" << clientPort
              << " server=" << ipv4ToString(serverIp) << ":" << serverPort
              << " clockRttUs=" << clockEstimate.rttUs
              << " clockUncertaintyUs=" << clockEstimate.oneWayUncertaintyUs
              << " clockConfidence=" << static_cast<int>(clockEstimate.confidencePercent)
              << "\n";

    SwVtpAv1Reassembler reassembler;
    SwVtpKlvReassembler klvReassembler;
    klvReassembler.setTrackId("klv");
    UsStats transferLatency;
    UsStats captureLatency;
    uint64_t datagramsReceived = 0;
    uint64_t klvDatagramsReceived = 0;
    uint64_t bytesReceived = 0;
    uint64_t videoBytes = 0;
    uint64_t klvBytes = 0;
    uint64_t klvPackets = 0;
    std::size_t framesCompleted = 0;
    const uint64_t receiveStart = nowUs();
    uint64_t lastPacketUs = receiveStart;

    while ((framesCompleted < expectedFrames || klvPackets < expectedFrames) &&
           nowUs() - lastPacketUs < 3000000ULL) {
        SwVtpUdpPacket packet;
        if (!udp.receive(500, packet)) {
            continue;
        }
        lastPacketUs = nowUs();

        SwVtpDatagram datagram;
        if (!swVtpParseDatagram(packet.bytes, datagram)) {
            continue;
        }
        bytesReceived += packet.bytes.size();

        if (datagram.header.messageType == SwVtpMessageType::KlvFragment) {
            ++klvDatagramsReceived;
            SwVtpKlvReassembler::PushResult push =
                klvReassembler.pushDatagram(datagram, lastPacketUs);
            if (push.completed()) {
                ++klvPackets;
                klvBytes += push.packet.payload().size();
            }
            continue;
        }

        if (datagram.header.messageType != SwVtpMessageType::FrameFragment) {
            continue;
        }

        ++datagramsReceived;

        const SwVtpFrameLatencySample latency =
            swVtpMeasureFrameLatency(datagram.header, clockEstimate, lastPacketUs);
        if (latency.valid) {
            transferLatency.add(latency.transferLatencyUs);
            if (latency.captureLatencyValid) {
                captureLatency.add(latency.captureToReceiveUs);
            }
        }

        SwVtpAv1Reassembler::PushResult push =
            reassembler.pushDatagram(datagram, lastPacketUs);
        if (push.completed()) {
            ++framesCompleted;
            videoBytes += push.packet.payload().size();
        }
    }

    const uint64_t receiveElapsedUs = std::max<uint64_t>(1U, nowUs() - receiveStart);
    const SwVtpAv1Reassembler::Snapshot snapshot = reassembler.snapshot();
    const SwVtpKlvReassembler::Snapshot klvSnapshot = klvReassembler.snapshot();
    const double videoKbps =
        static_cast<double>(videoBytes * 8ULL) / static_cast<double>(receiveElapsedUs) * 1000.0;
    const double udpPayloadKbps =
        static_cast<double>(bytesReceived * 8ULL) / static_cast<double>(receiveElapsedUs) * 1000.0;

    std::cout << "[SwVTP client metrics] framesCompleted=" << framesCompleted
              << " expectedFrames=" << expectedFrames
              << " videoDatagrams=" << datagramsReceived
              << " klvDatagrams=" << klvDatagramsReceived
              << " udpPayloadBytes=" << bytesReceived
              << " videoBytes=" << videoBytes
              << " klvPackets=" << klvPackets
              << " klvBytes=" << klvBytes
              << " elapsedMs=" << (receiveElapsedUs / 1000.0)
              << " videoKbps=" << videoKbps
              << " udpPayloadKbps=" << udpPayloadKbps
              << " transferLatencyMs(avg/min/max)="
              << transferLatency.avgMs() << "/" << transferLatency.minMs() << "/"
              << transferLatency.maxMs()
              << " captureLatencyMs(avg/min/max)="
              << captureLatency.avgMs() << "/" << captureLatency.minMs() << "/"
              << captureLatency.maxMs()
              << " duplicates=" << snapshot.duplicateFragments
              << " stale=" << snapshot.staleFragments
              << " dropped=" << snapshot.droppedFrames
              << " klvDuplicates=" << klvSnapshot.duplicateFragments
              << " klvStale=" << klvSnapshot.staleFragments
              << " klvDropped=" << klvSnapshot.droppedSamples << "\n";

    return framesCompleted == expectedFrames && klvPackets >= expectedFrames
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return EXIT_FAILURE;
    }
    const std::string mode = argv[1] ? argv[1] : "";
    if (mode == "server") {
        return runServer(argc, argv);
    }
    if (mode == "client") {
        return runClient(argc, argv);
    }
    printUsage();
    return EXIT_FAILURE;
}
