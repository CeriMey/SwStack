#include "media/swvtp/SwVtpAv1.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SwVtpSocketHandle;
static const SwVtpSocketHandle kInvalidSocketHandle = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SwVtpSocketHandle;
static const SwVtpSocketHandle kInvalidSocketHandle = -1;
#endif

namespace {

struct SocketRuntime {
    SocketRuntime() {
#if defined(_WIN32)
        WSADATA data;
        ok = (::WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
    }
    ~SocketRuntime() {
#if defined(_WIN32)
        if (ok) {
            ::WSACleanup();
        }
#endif
    }
    bool ok{true};
};

struct IvfFrame {
    uint64_t timestamp{0};
    SwByteArray payload{};
};

struct UdpPacket {
    SwByteArray bytes{};
    uint32_t senderIpv4{0};
    uint16_t senderPort{0};
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

void closeSocket(SwVtpSocketHandle socketHandle) {
    if (socketHandle == kInvalidSocketHandle) {
        return;
    }
#if defined(_WIN32)
    ::closesocket(socketHandle);
#else
    ::close(socketHandle);
#endif
}

sockaddr_in makeSockaddr(uint32_t ipv4, uint16_t port) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(ipv4);
    return addr;
}

uint32_t sockaddrIpv4(const sockaddr_in& addr) {
    return ntohl(addr.sin_addr.s_addr);
}

uint16_t sockaddrPort(const sockaddr_in& addr) {
    return ntohs(addr.sin_port);
}

SwVtpSocketHandle createUdpSocket() {
    return ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

bool bindUdpSocket(SwVtpSocketHandle socketHandle, uint32_t ipv4, uint16_t port) {
    const int yes = 1;
    ::setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr = makeSockaddr(ipv4, port);
    return ::bind(socketHandle,
                  reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) == 0;
}

bool localPort(SwVtpSocketHandle socketHandle, uint16_t& outPort) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
#if defined(_WIN32)
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (::getsockname(socketHandle, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return false;
    }
    outPort = sockaddrPort(addr);
    return true;
}

bool waitReadable(SwVtpSocketHandle socketHandle, int timeoutMs) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socketHandle, &readSet);
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    const int result = ::select(static_cast<int>(socketHandle + 1),
                                &readSet,
                                nullptr,
                                nullptr,
                                &timeout);
    return result > 0 && FD_ISSET(socketHandle, &readSet);
}

bool recvUdp(SwVtpSocketHandle socketHandle, int timeoutMs, UdpPacket& outPacket) {
    if (!waitReadable(socketHandle, timeoutMs)) {
        return false;
    }
    std::vector<char> buffer(65536);
    sockaddr_in sender;
    std::memset(&sender, 0, sizeof(sender));
#if defined(_WIN32)
    int senderLen = sizeof(sender);
    const int received = ::recvfrom(socketHandle,
                                    buffer.data(),
                                    static_cast<int>(buffer.size()),
                                    0,
                                    reinterpret_cast<sockaddr*>(&sender),
                                    &senderLen);
#else
    socklen_t senderLen = sizeof(sender);
    const ssize_t received = ::recvfrom(socketHandle,
                                        buffer.data(),
                                        buffer.size(),
                                        0,
                                        reinterpret_cast<sockaddr*>(&sender),
                                        &senderLen);
#endif
    if (received <= 0) {
        return false;
    }
    outPacket.bytes = SwByteArray();
    outPacket.bytes.append(buffer.data(), static_cast<std::size_t>(received));
    outPacket.senderIpv4 = sockaddrIpv4(sender);
    outPacket.senderPort = sockaddrPort(sender);
    return true;
}

bool sendUdp(SwVtpSocketHandle socketHandle,
             const SwByteArray& bytes,
             uint32_t ipv4,
             uint16_t port) {
    sockaddr_in dest = makeSockaddr(ipv4, port);
    const char* data = bytes.constData();
    if (!data || bytes.isEmpty()) {
        return false;
    }
#if defined(_WIN32)
    const int sent = ::sendto(socketHandle,
                              data,
                              static_cast<int>(bytes.size()),
                              0,
                              reinterpret_cast<const sockaddr*>(&dest),
                              sizeof(dest));
    return sent == static_cast<int>(bytes.size());
#else
    const ssize_t sent = ::sendto(socketHandle,
                                  data,
                                  bytes.size(),
                                  0,
                                  reinterpret_cast<const sockaddr*>(&dest),
                                  sizeof(dest));
    return sent == static_cast<ssize_t>(bytes.size());
#endif
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

    SwVtpStreamConfig config;
    config.streamId = 1;
    config.trackId = 1;
    config.trackType = SwVtpTrackType::Video;
    config.codec = SwVtpCodec::AV1;
    config.endpoint = swVtpMakeUnicastEndpoint(unicastMask, bindPort);
    if (!config.isValid()) {
        std::cerr << "[SwVTP server] invalid stream config\n";
        return EXIT_FAILURE;
    }

    SwVtpSocketHandle socketHandle = createUdpSocket();
    if (socketHandle == kInvalidSocketHandle ||
        !bindUdpSocket(socketHandle, bindIp, bindPort)) {
        std::cerr << "[SwVTP server] failed to bind "
                  << ipv4ToString(bindIp) << ":" << bindPort << "\n";
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    std::cout << "[SwVTP server] listening=" << ipv4ToString(bindIp) << ":" << bindPort
              << " mode=unicast mask=" << ipv4ToString(unicastMask)
              << " frames=" << frameLimit << " mtu=" << mtu << "\n";

    SwVtpClientAnnouncement acceptedClient;
    uint32_t clientIp = 0;
    uint16_t clientPort = 0;
    const uint64_t waitStart = nowUs();
    while (nowUs() - waitStart < 10000000ULL) {
        UdpPacket packet;
        if (!recvUdp(socketHandle, 500, packet)) {
            continue;
        }

        SwVtpDatagram datagram;
        if (!swVtpParseDatagram(packet.bytes, datagram)) {
            continue;
        }

        if (datagram.header.messageType == SwVtpMessageType::Ping) {
            SwVtpClockSyncPing ping;
            if (swVtpParseClockSyncPing(datagram.payload, ping)) {
                SwVtpClockSyncPong pong;
                pong.syncId = ping.syncId;
                pong.clientSendTimeUs = ping.clientSendTimeUs;
                pong.serverReceiveTimeUs = nowUs();
                pong.serverSendTimeUs = nowUs();
                sendUdp(socketHandle,
                        makeControlDatagram(SwVtpMessageType::Pong,
                                            swVtpSerializeClockSyncPong(pong)),
                        packet.senderIpv4,
                        packet.senderPort);
            }
        } else if (datagram.header.messageType == SwVtpMessageType::Hello) {
            SwVtpClientAnnouncement client;
            if (swVtpParseClientAnnouncementPayload(datagram.payload, client) &&
                swVtpStreamConfigAcceptsClient(config, client)) {
                acceptedClient = client;
                clientIp = client.clientIpv4;
                clientPort = client.receivePort;
                sendUdp(socketHandle,
                        makeControlDatagram(SwVtpMessageType::Accept,
                                            swVtpSerializeStreamConfig(config)),
                        packet.senderIpv4,
                        packet.senderPort);
                break;
            }
        }
    }

    if (!acceptedClient.isValid()) {
        std::cerr << "[SwVTP server] no valid client announcement received\n";
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    std::cout << "[SwVTP server] acceptedClient=" << ipv4ToString(clientIp)
              << ":" << clientPort << "\n";

    uint64_t datagramsSent = 0;
    uint64_t bytesSent = 0;
    uint64_t videoBytes = 0;
    const uint64_t sendStart = nowUs();
    for (std::size_t i = 0; i < frameLimit; ++i) {
        const IvfFrame& frame = frames[i % frames.size()];
        SwVideoPacket packet(SwVideoPacket::Codec::AV1,
                             frame.payload,
                             static_cast<std::int64_t>(i * 33333ULL),
                             static_cast<std::int64_t>(i * 33333ULL),
                             i == 0U);

        SwVtpAv1PacketizerOptions options;
        options.streamId = config.streamId;
        options.trackId = config.trackId;
        options.frameId = static_cast<uint32_t>(i + 1U);
        options.nowUs = nowUs();
        options.captureTimeUs = options.nowUs - 4000ULL;
        options.latencyBudgetUs = 90000;
        options.maxDatagramBytes = mtu;

        const SwVtpAv1PacketizerResult packetized =
            SwVtpAv1Packetizer::packetize(packet, options);
        if (!packetized.ok) {
            std::cerr << "[SwVTP server] packetizer failed at frame " << i << "\n";
            closeSocket(socketHandle);
            return EXIT_FAILURE;
        }

        for (std::size_t n = 0; n < packetized.serializedDatagrams.size(); ++n) {
            if (!sendUdp(socketHandle, packetized.serializedDatagrams[n], clientIp, clientPort)) {
                std::cerr << "[SwVTP server] send failed at frame " << i << "\n";
                closeSocket(socketHandle);
                return EXIT_FAILURE;
            }
            ++datagramsSent;
            bytesSent += packetized.serializedDatagrams[n].size();
        }
        videoBytes += frame.payload.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    const uint64_t sendElapsedUs = std::max<uint64_t>(1U, nowUs() - sendStart);
    const double networkKbps =
        static_cast<double>(bytesSent * 8ULL) / static_cast<double>(sendElapsedUs) * 1000.0;
    const double videoKbps =
        static_cast<double>(videoBytes * 8ULL) / static_cast<double>(sendElapsedUs) * 1000.0;

    std::cout << "[SwVTP server metrics] framesSent=" << frameLimit
              << " datagramsSent=" << datagramsSent
              << " videoBytes=" << videoBytes
              << " udpPayloadBytes=" << bytesSent
              << " elapsedMs=" << (sendElapsedUs / 1000.0)
              << " videoKbps=" << videoKbps
              << " udpPayloadKbps=" << networkKbps << "\n";

    closeSocket(socketHandle);
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

    SwVtpSocketHandle socketHandle = createUdpSocket();
    if (socketHandle == kInvalidSocketHandle ||
        !bindUdpSocket(socketHandle, kSwVtpIpv4Any, clientPort)) {
        std::cerr << "[SwVTP client] failed to bind receive socket\n";
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }
    if (!localPort(socketHandle, clientPort)) {
        std::cerr << "[SwVTP client] failed to read bound port\n";
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    SwVtpClockEstimate clockEstimate;
    SwVtpClockSyncPing ping;
    ping.syncId = 1;
    ping.clientSendTimeUs = nowUs();
    if (!sendUdp(socketHandle,
                 makeControlDatagram(SwVtpMessageType::Ping,
                                     swVtpSerializeClockSyncPing(ping)),
                 serverIp,
                 serverPort)) {
        std::cerr << "[SwVTP client] failed to send clock ping\n";
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    const uint64_t clockWaitStart = nowUs();
    while (nowUs() - clockWaitStart < 2000000ULL) {
        UdpPacket packet;
        if (!recvUdp(socketHandle, 200, packet)) {
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
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    SwVtpClientAnnouncement client;
    client.streamId = 1;
    client.receivePort = clientPort;
    client.clientIpv4 = clientIp;
    if (!sendUdp(socketHandle,
                 makeControlDatagram(SwVtpMessageType::Hello,
                                     swVtpSerializeClientAnnouncement(client)),
                 serverIp,
                 serverPort)) {
        std::cerr << "[SwVTP client] failed to send announcement\n";
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    SwVtpStreamConfig config;
    const uint64_t acceptWaitStart = nowUs();
    while (nowUs() - acceptWaitStart < 2000000ULL) {
        UdpPacket packet;
        if (!recvUdp(socketHandle, 200, packet)) {
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
        closeSocket(socketHandle);
        return EXIT_FAILURE;
    }

    std::cout << "[SwVTP client] local=" << ipv4ToString(clientIp) << ":" << clientPort
              << " server=" << ipv4ToString(serverIp) << ":" << serverPort
              << " clockRttUs=" << clockEstimate.rttUs
              << " clockUncertaintyUs=" << clockEstimate.oneWayUncertaintyUs
              << " clockConfidence=" << static_cast<int>(clockEstimate.confidencePercent)
              << "\n";

    SwVtpAv1Reassembler reassembler;
    UsStats transferLatency;
    UsStats captureLatency;
    uint64_t datagramsReceived = 0;
    uint64_t bytesReceived = 0;
    uint64_t videoBytes = 0;
    std::size_t framesCompleted = 0;
    const uint64_t receiveStart = nowUs();
    uint64_t lastPacketUs = receiveStart;

    while (framesCompleted < expectedFrames && nowUs() - lastPacketUs < 3000000ULL) {
        UdpPacket packet;
        if (!recvUdp(socketHandle, 500, packet)) {
            continue;
        }
        lastPacketUs = nowUs();

        SwVtpDatagram datagram;
        if (!swVtpParseDatagram(packet.bytes, datagram) ||
            datagram.header.messageType != SwVtpMessageType::FrameFragment) {
            continue;
        }

        ++datagramsReceived;
        bytesReceived += packet.bytes.size();

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
    const double videoKbps =
        static_cast<double>(videoBytes * 8ULL) / static_cast<double>(receiveElapsedUs) * 1000.0;
    const double udpPayloadKbps =
        static_cast<double>(bytesReceived * 8ULL) / static_cast<double>(receiveElapsedUs) * 1000.0;

    std::cout << "[SwVTP client metrics] framesCompleted=" << framesCompleted
              << " expectedFrames=" << expectedFrames
              << " datagramsReceived=" << datagramsReceived
              << " udpPayloadBytes=" << bytesReceived
              << " videoBytes=" << videoBytes
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
              << " dropped=" << snapshot.droppedFrames << "\n";

    closeSocket(socketHandle);
    return framesCompleted == expectedFrames ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
    SocketRuntime runtime;
    if (!runtime.ok) {
        std::cerr << "[SwVTP UDP] socket runtime init failed\n";
        return EXIT_FAILURE;
    }
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
