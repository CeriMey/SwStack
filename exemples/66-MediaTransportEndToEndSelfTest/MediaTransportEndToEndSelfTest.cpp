#include "SwCoreApplication.h"
#include "media/SwMediaSourceFactory.h"
#include "media/server/SwMediaServer.h"
#include "media/server/SwMediaServerConfig.h"
#include "media/server/SwMediaServerFactory.h"
#include "media/source/SwVtpVideoSource.h"
#include "media/swvtp/SwVtpServerTransport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
const NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
const NativeSocket kInvalidSocket = -1;
#endif

void closeNativeSocket(NativeSocket socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool ensureSocketsInitialized() {
#if defined(_WIN32)
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, []() {
        WSADATA data;
        ok = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
    });
    return ok;
#else
    return true;
#endif
}

uint64_t nowMs() {
    const std::chrono::steady_clock::duration elapsed =
        std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[MediaTransportEndToEndSelfTest] FAIL " << message << "\n";
        return false;
    }
    return true;
}

void pumpFor(SwCoreApplication& app, int durationMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(durationMs);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

template <typename Predicate>
bool pumpUntil(SwCoreApplication& app, Predicate predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        (void)app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

SwByteArray makeH264AnnexBIdr(uint8_t salt) {
    const char header[] = {
        0x00, 0x00, 0x00, 0x01,
        static_cast<char>(0x65),
        static_cast<char>(0x88),
        static_cast<char>(0x84),
        static_cast<char>(0x21),
        static_cast<char>(0xA0),
        static_cast<char>(salt)
    };
    SwByteArray payload(header, sizeof(header));
    for (int i = 0; i < 260; ++i) {
        payload.append(static_cast<char>((i * 11 + salt) & 0xFF));
    }
    return payload;
}

SwByteArray makeH265AnnexBIdr(uint8_t salt) {
    const char header[] = {
        0x00, 0x00, 0x00, 0x01,
        static_cast<char>(0x26),
        static_cast<char>(0x01),
        static_cast<char>(0xAF),
        static_cast<char>(0x10),
        static_cast<char>(salt)
    };
    SwByteArray payload(header, sizeof(header));
    for (int i = 0; i < 260; ++i) {
        payload.append(static_cast<char>((i * 13 + salt) & 0xFF));
    }
    return payload;
}

SwByteArray makeH264RtpIdrNal(uint8_t salt) {
    const char bytes[] = {
        static_cast<char>(0x65),
        static_cast<char>(0x88),
        static_cast<char>(0x84),
        static_cast<char>(0x21),
        static_cast<char>(0xA0),
        static_cast<char>(salt)
    };
    return SwByteArray(bytes, sizeof(bytes));
}

SwByteArray makeAv1Frame(uint8_t salt) {
    SwByteArray payload;
    payload.append(static_cast<char>(0x12));
    payload.append(static_cast<char>(0x00));
    payload.append(static_cast<char>(0x40));
    payload.append(static_cast<char>(0x41));
    payload.append(static_cast<char>(salt));
    return payload;
}

SwByteArray makeLargeAv1Frame(uint8_t salt, int bodyBytes) {
    SwByteArray payload = makeAv1Frame(salt);
    for (int i = 0; i < bodyBytes; ++i) {
        payload.append(static_cast<char>((i * 17 + salt) & 0xFF));
    }
    return payload;
}

SwByteArray makeSwVtpPayloadForCodec(SwVideoPacket::Codec codec, uint8_t salt) {
    switch (codec) {
    case SwVideoPacket::Codec::H264:
        return makeH264AnnexBIdr(salt);
    case SwVideoPacket::Codec::H265:
        return makeH265AnnexBIdr(salt);
    case SwVideoPacket::Codec::AV1:
        return makeAv1Frame(salt);
    default:
        break;
    }
    return SwByteArray();
}

SwString codecName(SwVideoPacket::Codec codec) {
    switch (codec) {
    case SwVideoPacket::Codec::H264:
        return "h264";
    case SwVideoPacket::Codec::H265:
        return "h265";
    case SwVideoPacket::Codec::AV1:
        return "av1";
    default:
        break;
    }
    return "unknown";
}

SwVideoPacket makePacket(SwVideoPacket::Codec codec,
                         const SwByteArray& payload,
                         uint64_t frameIndex,
                         bool keyFrame) {
    const std::int64_t pts = static_cast<std::int64_t>(frameIndex * 33333ULL);
    SwVideoPacket packet(codec, payload, pts, pts, keyFrame);
    if (frameIndex == 0U) {
        packet.setDiscontinuity(true);
    }
    return packet;
}

SwVideoPublishStream makeStream(SwVideoPacket::Codec codec) {
    SwVideoPublishStream stream;
    stream.id = "main";
    stream.trackId = "video";
    stream.codec = codec;
    stream.width = 1920;
    stream.height = 1080;
    stream.fpsNumerator = 30;
    stream.fpsDenominator = 1;
    stream.startBitrateKbps = 6000;
    stream.minBitrateKbps = 800;
    stream.maxBitrateKbps = 12000;
    stream.latencyBudgetMs = 90;
    return stream;
}

SwMediaServerConfig makeServerConfig(SwMediaTransportProtocol protocol,
                                     const SwString& host,
                                     uint16_t port) {
    SwMediaServerConfig config;
    config.name = "MediaTransportEndToEndSelfTest";
    config.endpoint.protocol = protocol;
    config.endpoint.host = host;
    config.endpoint.bindAddress = "0.0.0.0";
    config.endpoint.port = port;
    config.endpoint.deliveryMode = SwMediaTransportDeliveryMode::Unicast;
    config.lowLatency = true;
    config.defaultLatencyBudgetMs = 90;
    config.maxClients = 4;
    config.mtuBytes = 1200;
    return config;
}

class HeadlessPlayerProbe {
public:
    bool start(const SwString& url) {
        source = SwMediaSourceFactory::createVideoSource(url);
        if (!source) {
            error = "factory returned no video source";
            return false;
        }
        source->setPacketCallback([this](const SwVideoPacket& packet) {
            packets.fetch_add(1);
            bytes.fetch_add(static_cast<uint64_t>(packet.payload().size()));
            if (packet.isKeyFrame()) {
                keyFrames.fetch_add(1);
            }
            lastCodec.store(static_cast<int>(packet.codec()));
        });
        source->setStatusCallback([this](const SwMediaSource::StreamStatus& status) {
            std::lock_guard<std::mutex> lock(statusMutex);
            lastStatus = status.reason.toStdString();
        });
        source->start();
        return true;
    }

    void stop() {
        if (source) {
            source->stop();
            source->setPacketCallback(SwVideoSource::PacketCallback());
            source.reset();
        }
    }

    uint64_t packetCount() const { return packets.load(); }
    uint64_t byteCount() const { return bytes.load(); }
    uint64_t keyFrameCount() const { return keyFrames.load(); }
    SwVideoPacket::Codec codec() const {
        return static_cast<SwVideoPacket::Codec>(lastCodec.load());
    }

    std::shared_ptr<SwVideoSource> videoSource() const {
        return source;
    }

    std::shared_ptr<SwVtpVideoSource> swvtpSource() const {
        return std::dynamic_pointer_cast<SwVtpVideoSource>(source);
    }

    SwVtpVideoSourceMetrics swvtpMetrics() const {
        std::shared_ptr<SwVtpVideoSource> vtp = swvtpSource();
        return vtp ? vtp->metrics() : SwVtpVideoSourceMetrics();
    }

    SwString videoTrackCodec() const {
        if (!source) {
            return SwString();
        }
        SwList<SwMediaTrack> sourceTracks = source->tracks();
        for (auto it = sourceTracks.begin(); it != sourceTracks.end(); ++it) {
            if (it->type == SwMediaTrack::Type::Video) {
                return it->codec;
            }
        }
        return SwString();
    }

    std::string status() const {
        std::lock_guard<std::mutex> lock(statusMutex);
        return lastStatus;
    }

    std::string error{};

private:
    std::shared_ptr<SwVideoSource> source{};
    std::atomic<uint64_t> packets{0};
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> keyFrames{0};
    std::atomic<int> lastCodec{static_cast<int>(SwVideoPacket::Codec::Unknown)};
    mutable std::mutex statusMutex;
    std::string lastStatus{};
};

class DropFirstSwVtpVideoFragmentTransport : public SwVtpServerTransport {
public:
    uint32_t droppedFragments() const {
        return m_droppedFragments.load();
    }

protected:
    bool writeDatagram_(const SwByteArray& datagram) override {
        SwVtpDatagram parsed;
        if (!m_dropped.load() &&
            swVtpParseDatagram(datagram, parsed) &&
            parsed.header.messageType == SwVtpMessageType::FrameFragment &&
            parsed.header.trackType == SwVtpTrackType::Video &&
            parsed.header.fragmentCount > 2U &&
            parsed.header.fragmentIndex == 1U) {
            m_dropped.store(true);
            m_droppedFragments.fetch_add(1);
            return true;
        }
        return SwVtpServerTransport::writeDatagram_(datagram);
    }

private:
    std::atomic<bool> m_dropped{false};
    std::atomic<uint32_t> m_droppedFragments{0};
};

bool sendAll(NativeSocket socket, const std::string& data) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0U) {
#if defined(_WIN32)
        const int sent = send(socket, cursor, static_cast<int>(remaining), 0);
#else
        const ssize_t sent = send(socket, cursor, remaining, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int parseCSeq(const std::string& request) {
    std::istringstream stream(request);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string lower = lowerCopy(line);
        const std::string key = "cseq:";
        if (lower.find(key) == 0U) {
            return std::atoi(line.substr(key.size()).c_str());
        }
    }
    return 1;
}

std::string methodFromRequest(const std::string& request) {
    const std::size_t space = request.find(' ');
    return space == std::string::npos ? std::string() : request.substr(0, space);
}

uint16_t parseClientRtpPort(const std::string& request) {
    const std::string lower = lowerCopy(request);
    const std::string key = "client_port=";
    const std::size_t pos = lower.find(key);
    if (pos == std::string::npos) {
        return 0;
    }
    return static_cast<uint16_t>(std::atoi(lower.substr(pos + key.size()).c_str()));
}

SwByteArray makeRtpDatagram(uint16_t sequence,
                            uint32_t timestamp,
                            const SwByteArray& payload) {
    char header[12] = {};
    header[0] = static_cast<char>(0x80U);
    header[1] = static_cast<char>(0x80U | 96U);
    header[2] = static_cast<char>((sequence >> 8U) & 0xFFU);
    header[3] = static_cast<char>(sequence & 0xFFU);
    header[4] = static_cast<char>((timestamp >> 24U) & 0xFFU);
    header[5] = static_cast<char>((timestamp >> 16U) & 0xFFU);
    header[6] = static_cast<char>((timestamp >> 8U) & 0xFFU);
    header[7] = static_cast<char>(timestamp & 0xFFU);
    header[8] = static_cast<char>(0x53);
    header[9] = static_cast<char>(0x57);
    header[10] = static_cast<char>(0x52);
    header[11] = static_cast<char>(0x54);

    SwByteArray datagram;
    datagram.append(header, sizeof(header));
    datagram.append(payload);
    return datagram;
}

bool sendUdpDatagramToLocalhost(uint16_t port, const SwByteArray& datagram) {
    if (!ensureSocketsInitialized()) {
        return false;
    }
    NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocket) {
        return false;
    }

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7F000001UL);

#if defined(_WIN32)
    const int sent = sendto(socket,
                            datagram.constData(),
                            static_cast<int>(datagram.size()),
                            0,
                            reinterpret_cast<sockaddr*>(&address),
                            sizeof(address));
#else
    const ssize_t sent = sendto(socket,
                                datagram.constData(),
                                datagram.size(),
                                0,
                                reinterpret_cast<sockaddr*>(&address),
                                sizeof(address));
#endif
    closeNativeSocket(socket);
    return sent == static_cast<decltype(sent)>(datagram.size());
}

class MinimalRtspH264Server {
public:
    explicit MinimalRtspH264Server(uint16_t port)
        : m_port(port) {}

    ~MinimalRtspH264Server() {
        stop();
    }

    bool start() {
        if (!ensureSocketsInitialized()) {
            return false;
        }
        m_stop.store(false);
        m_thread = std::thread([this]() { threadMain_(); });
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(1000);
        while (!m_ready.load() && !m_failed.load() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return m_ready.load() && !m_failed.load();
    }

    void stop() {
        m_stop.store(true);
        if (m_listenSocket != kInvalidSocket) {
            closeNativeSocket(m_listenSocket);
            m_listenSocket = kInvalidSocket;
        }
        poke_();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    uint64_t rtpPacketsSent() const {
        return m_rtpPacketsSent.load();
    }

private:
    void poke_() {
        NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == kInvalidSocket) {
            return;
        }
        sockaddr_in address;
        std::memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(m_port);
        address.sin_addr.s_addr = htonl(0x7F000001UL);
        (void)::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        closeNativeSocket(socket);
    }

    bool readOneRequest_(NativeSocket client,
                         std::string& buffer,
                         std::string& request) {
        while (buffer.find("\r\n\r\n") == std::string::npos) {
            char chunk[2048] = {};
#if defined(_WIN32)
            const int got = recv(client, chunk, sizeof(chunk), 0);
#else
            const ssize_t got = recv(client, chunk, sizeof(chunk), 0);
#endif
            if (got <= 0) {
                return false;
            }
            buffer.append(chunk, static_cast<std::size_t>(got));
        }
        const std::size_t end = buffer.find("\r\n\r\n") + 4U;
        request = buffer.substr(0, end);
        buffer.erase(0, end);
        return true;
    }

    bool sendResponse_(NativeSocket client,
                       int cseq,
                       const std::string& headers,
                       const std::string& body = std::string()) {
        std::ostringstream response;
        response << "RTSP/1.0 200 OK\r\n";
        response << "CSeq: " << cseq << "\r\n";
        response << "Server: SwStack-E2E\r\n";
        response << headers;
        if (!body.empty()) {
            response << "Content-Type: application/sdp\r\n";
            response << "Content-Length: " << body.size() << "\r\n";
        }
        response << "\r\n";
        response << body;
        return sendAll(client, response.str());
    }

    void sendRtpBurst_(uint16_t clientRtpPort) {
        for (uint16_t i = 0; i < 6U; ++i) {
            SwByteArray datagram = makeRtpDatagram(static_cast<uint16_t>(100 + i),
                                                   90000U + 3000U * i,
                                                   makeH264RtpIdrNal(static_cast<uint8_t>(i)));
            if (sendUdpDatagramToLocalhost(clientRtpPort, datagram)) {
                m_rtpPacketsSent.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void handleClient_(NativeSocket client) {
        std::string buffer;
        uint16_t clientRtpPort = 0;
        const std::string sdp =
            "v=0\r\n"
            "o=- 0 0 IN IP4 127.0.0.1\r\n"
            "s=SwStack E2E\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "t=0 0\r\n"
            "a=control:*\r\n"
            "m=video 0 RTP/AVP 96\r\n"
            "a=rtpmap:96 H264/90000\r\n"
            "a=control:trackID=0\r\n";

        while (!m_stop.load()) {
            std::string request;
            if (!readOneRequest_(client, buffer, request)) {
                break;
            }
            const int cseq = parseCSeq(request);
            const std::string method = methodFromRequest(request);
            if (method == "OPTIONS") {
                sendResponse_(client, cseq, "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n");
            } else if (method == "DESCRIBE") {
                std::ostringstream headers;
                headers << "Content-Base: rtsp://127.0.0.1:" << m_port << "/stream/\r\n";
                sendResponse_(client, cseq, headers.str(), sdp);
            } else if (method == "SETUP") {
                clientRtpPort = parseClientRtpPort(request);
                std::ostringstream headers;
                headers << "Transport: RTP/AVP;unicast;client_port="
                        << clientRtpPort << "-" << static_cast<uint16_t>(clientRtpPort + 1)
                        << ";server_port=" << static_cast<uint16_t>(m_port + 100)
                        << "-" << static_cast<uint16_t>(m_port + 101)
                        << ";ssrc=53575254\r\n";
                headers << "Session: swstack-e2e;timeout=10\r\n";
                sendResponse_(client, cseq, headers.str());
            } else if (method == "PLAY") {
                sendResponse_(client, cseq, "Session: swstack-e2e\r\nRTP-Info: url=trackID=0;seq=100;rtptime=90000\r\n");
                if (clientRtpPort != 0U) {
                    sendRtpBurst_(clientRtpPort);
                }
            } else if (method == "TEARDOWN") {
                sendResponse_(client, cseq, "Session: swstack-e2e\r\n");
                break;
            } else {
                sendResponse_(client, cseq, std::string());
            }
        }
    }

    void threadMain_() {
        NativeSocket listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == kInvalidSocket) {
            m_failed.store(true);
            return;
        }
        m_listenSocket = listenSocket;
        int reuse = 1;
        setsockopt(listenSocket,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse),
                   sizeof(reuse));

        sockaddr_in address;
        std::memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(m_port);
        address.sin_addr.s_addr = htonl(0x7F000001UL);
        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listenSocket, 1) != 0) {
            m_failed.store(true);
            closeNativeSocket(listenSocket);
            m_listenSocket = kInvalidSocket;
            return;
        }
        m_ready.store(true);

        while (!m_stop.load()) {
            sockaddr_in clientAddress;
            std::memset(&clientAddress, 0, sizeof(clientAddress));
#if defined(_WIN32)
            int clientLength = sizeof(clientAddress);
#else
            socklen_t clientLength = sizeof(clientAddress);
#endif
            NativeSocket client = accept(listenSocket,
                                         reinterpret_cast<sockaddr*>(&clientAddress),
                                         &clientLength);
            if (client == kInvalidSocket) {
                if (m_stop.load()) {
                    break;
                }
                continue;
            }
            handleClient_(client);
            closeNativeSocket(client);
        }

        closeNativeSocket(listenSocket);
        m_listenSocket = kInvalidSocket;
    }

    uint16_t m_port{0};
    std::thread m_thread{};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_failed{false};
    std::atomic<uint64_t> m_rtpPacketsSent{0};
    NativeSocket m_listenSocket{kInvalidSocket};
};

bool publishUntilReceived(SwCoreApplication& app,
                          SwMediaServer& server,
                          const SwString& streamId,
                          const std::vector<SwVideoPacket>& packets,
                          HeadlessPlayerProbe& player,
                          uint64_t requiredPackets,
                          int timeoutMs) {
    const uint64_t start = nowMs();
    uint64_t index = 0;
    while (player.packetCount() < requiredPackets && nowMs() - start < static_cast<uint64_t>(timeoutMs)) {
        const SwVideoPacket& packet = packets[static_cast<std::size_t>(index % packets.size())];
        (void)server.publishVideoPacket(streamId, packet);
        ++index;
        pumpFor(app, 40);
    }
    return player.packetCount() >= requiredPackets;
}

bool runUdpCase(SwCoreApplication& app) {
    std::cout << "[MediaTransportEndToEndSelfTest] udp begin" << std::endl;
    const uint16_t port = 56410;
    SwMediaServerConfig config =
        makeServerConfig(SwMediaTransportProtocol::Udp, "127.0.0.1", port);
    SwMediaServer server(config);
    server.setTransport(SwMediaServerFactory::createTransport(config));
    if (!expect(server.addVideoStream(makeStream(SwVideoPacket::Codec::H264)), "udp add stream")) {
        return false;
    }
    if (!expect(server.start(), "udp server start")) {
        return false;
    }

    HeadlessPlayerProbe player;
    if (!expect(player.start(SwString("udp://127.0.0.1:") +
                             SwString::number(port) +
                             "?format=h264&codec=h264&bind=0.0.0.0"),
                "udp player start")) {
        server.stop();
        return false;
    }

    std::vector<SwVideoPacket> packets;
    for (uint8_t i = 0; i < 6U; ++i) {
        packets.push_back(makePacket(SwVideoPacket::Codec::H264, makeH264AnnexBIdr(i), i, true));
    }
    const bool ok = publishUntilReceived(app, server, "main", packets, player, 3, 2500);
    SwVideoServerMetrics metrics = server.metrics();
    std::cout << "[MediaTransportEndToEndSelfTest] udp packets=" << player.packetCount()
              << " bytes=" << player.byteCount()
              << " sent=" << metrics.framesSent << std::endl;
    player.stop();
    server.stop();
    return expect(ok, "udp player received video") &&
           expect(player.codec() == SwVideoPacket::Codec::H264, "udp codec h264");
}

bool runRtpCase(SwCoreApplication& app) {
    std::cout << "[MediaTransportEndToEndSelfTest] rtp begin" << std::endl;
    const uint16_t port = 56420;
    SwMediaServerConfig config =
        makeServerConfig(SwMediaTransportProtocol::Rtp, "127.0.0.1", port);
    SwMediaServer server(config);
    server.setTransport(SwMediaServerFactory::createTransport(config));
    if (!expect(server.addVideoStream(makeStream(SwVideoPacket::Codec::H264)), "rtp add stream")) {
        return false;
    }
    if (!expect(server.start(), "rtp server start")) {
        return false;
    }

    HeadlessPlayerProbe player;
    if (!expect(player.start(SwString("rtp://127.0.0.1:") +
                             SwString::number(port) +
                             "?codec=h264&pt=96&bind=0.0.0.0"),
                "rtp player start")) {
        server.stop();
        return false;
    }

    std::vector<SwVideoPacket> packets;
    for (uint8_t i = 0; i < 6U; ++i) {
        packets.push_back(makePacket(SwVideoPacket::Codec::H264, makeH264RtpIdrNal(i), i, true));
    }
    const bool ok = publishUntilReceived(app, server, "main", packets, player, 3, 2500);
    SwVideoServerMetrics metrics = server.metrics();
    std::cout << "[MediaTransportEndToEndSelfTest] rtp packets=" << player.packetCount()
              << " bytes=" << player.byteCount()
              << " sent=" << metrics.framesSent << std::endl;
    player.stop();
    server.stop();
    return expect(ok, "rtp player received video") &&
           expect(player.codec() == SwVideoPacket::Codec::H264, "rtp codec h264");
}

bool runSwVtpCodecCase(SwCoreApplication& app,
                       SwVideoPacket::Codec codec,
                       uint16_t serverPort,
                       uint16_t clientPort,
                       bool validateAutomaticBitrate) {
    const SwString codecLabel = codecName(codec);
    const std::string codecText = codecLabel.toStdString();
    std::cout << "[MediaTransportEndToEndSelfTest] swvtp " << codecText << " begin" << std::endl;
    SwMediaServerConfig config =
        makeServerConfig(SwMediaTransportProtocol::SwVtp, "127.0.0.0", serverPort);
    config.endpoint.bindAddress = "127.0.0.1";
    config.mtuBytes = codec == SwVideoPacket::Codec::AV1 ? 1200U : 96U;
    std::shared_ptr<SwVtpServerTransport> transport(new SwVtpServerTransport());
    SwMediaServer server(config);
    server.setTransport(transport);
    std::atomic<uint32_t> feedbackCount{0};
    std::atomic<uint32_t> feedbackTargetKbps{0};
    std::atomic<uint32_t> feedbackReceiveQueueMs{0};
    server.setClientFeedbackCallback([&](const SwVideoServerClientFeedback& feedback) {
        feedbackCount.fetch_add(1);
        feedbackTargetKbps.store(feedback.targetBitrateKbps);
        feedbackReceiveQueueMs.store(feedback.receiveQueueMs);
    });

    SwVideoPublishStream stream = makeStream(codec);
    if (!expect(server.addVideoStream(stream),
                std::string("swvtp add stream ") + codecText)) {
        return false;
    }
    if (!expect(server.start(), std::string("swvtp server start ") + codecText)) {
        return false;
    }

    HeadlessPlayerProbe player;
    if (!expect(player.start(SwString("swvtp://127.0.0.1:") +
                             SwString::number(serverPort) +
                             "?localport=" +
                             SwString::number(clientPort)),
                std::string("swvtp player start ") + codecText)) {
        server.stop();
        return false;
    }
    if (!expect(static_cast<bool>(player.swvtpSource()),
                std::string("swvtp source type ") + codecText)) {
        player.stop();
        server.stop();
        return false;
    }

    const bool sessionReady = pumpUntil(app,
                                        [&player, transport, &stream]() {
                                            const SwVtpVideoSourceMetrics metrics =
                                                player.swvtpMetrics();
                                            return transport->activeClientCount() > 0U &&
                                                   metrics.accepted &&
                                                   metrics.clockSynced &&
                                                   metrics.negotiatedTargetBitrateKbps > 0U;
                                        },
                                        2500);
    if (!expect(sessionReady,
                std::string("swvtp session negotiated ") + codecText)) {
        player.stop();
        server.stop();
        return false;
    }

    bool abrOk = true;
    if (validateAutomaticBitrate) {
        SwVideoSource::ConsumerPressure pressure;
        pressure.queuedPackets = 128;
        pressure.queuedBytes = 4 * 1024 * 1024;
        pressure.softPressure = true;
        pressure.hardPressure = true;
        if (player.videoSource()) {
            player.videoSource()->setConsumerPressure(pressure);
        }
        abrOk = pumpUntil(app,
                          [&player, &server, &stream, &feedbackCount, &feedbackReceiveQueueMs]() {
                              const SwVideoServerMetrics serverMetrics = server.metrics();
                              const SwVtpVideoSourceMetrics clientMetrics = player.swvtpMetrics();
                              return feedbackCount.load() > 0U &&
                                     feedbackReceiveQueueMs.load() >= 80U &&
                                     serverMetrics.targetBitrateKbps > 0U &&
                                     serverMetrics.targetBitrateKbps < stream.startBitrateKbps &&
                                     clientMetrics.negotiatedTargetBitrateKbps ==
                                         serverMetrics.targetBitrateKbps;
                          },
                          3000);
        if (player.videoSource()) {
            player.videoSource()->setConsumerPressure(SwVideoSource::ConsumerPressure());
        }
    }

    std::vector<SwVideoPacket> packets;
    for (uint8_t i = 0; i < 6U; ++i) {
        packets.push_back(makePacket(codec,
                                     makeSwVtpPayloadForCodec(codec, i),
                                     i,
                                     true));
    }
    const bool ok = publishUntilReceived(app, server, "main", packets, player, 3, 2500);
    const bool latencyOk = pumpUntil(app,
                                     [&player]() {
                                         const SwVtpVideoSourceMetrics metrics =
                                             player.swvtpMetrics();
                                         return metrics.transferLatencySamples > 0U &&
                                                metrics.captureLatencySamples > 0U;
                                     },
                                     1000);
    SwVideoServerMetrics metrics = server.metrics();
    const SwVtpVideoSourceMetrics clientMetrics = player.swvtpMetrics();
    const SwVideoPacket::Codec receivedCodec = player.codec();
    const SwString receivedTrackCodec = player.videoTrackCodec();
    const uint64_t receivedPackets = player.packetCount();
    const uint64_t receivedBytes = player.byteCount();
    const uint64_t receivedKeyFrames = player.keyFrameCount();
    std::cout << "[MediaTransportEndToEndSelfTest] swvtp " << codecText
              << " packets=" << player.packetCount()
              << " bytes=" << player.byteCount()
              << " sent=" << metrics.framesSent
              << " clients=" << metrics.transport.activeClients
              << " targetKbps=" << metrics.targetBitrateKbps
              << " feedback=" << feedbackCount.load()
              << " fragments=" << clientMetrics.acceptedFragments
              << " stale=" << clientMetrics.staleFragments
              << " dropped=" << clientMetrics.droppedFrames
              << " nacks=" << clientMetrics.nackRequestsSent
              << " transferMs=" << clientMetrics.averageTransferLatencyMs()
              << " captureMs=" << clientMetrics.averageCaptureLatencyMs()
              << std::endl;
    player.stop();
    server.stop();
    return expect(ok, std::string("swvtp player received video ") + codecText) &&
           expect(receivedCodec == codec,
                  std::string("swvtp packet codec ") + codecText) &&
           expect(receivedTrackCodec == codecLabel,
                  std::string("swvtp track codec ") + codecText) &&
           expect(receivedKeyFrames >= 1U,
                  std::string("swvtp keyframe ") + codecText) &&
           expect(receivedBytes >= static_cast<uint64_t>(packets.front().payload().size()) * 3ULL,
                  std::string("swvtp full payload reassembled ") + codecText) &&
           expect(latencyOk,
                  std::string("swvtp latency metrics ") + codecText) &&
           expect(clientMetrics.maxTransferLatencyMs() < 100.0,
                  std::string("swvtp transfer latency sub100 ") + codecText) &&
           expect(clientMetrics.maxCaptureLatencyMs() < 100.0,
                  std::string("swvtp capture latency sub100 ") + codecText) &&
           expect(!validateAutomaticBitrate || abrOk,
                  std::string("swvtp automatic bitrate feedback ") + codecText) &&
           expect(!validateAutomaticBitrate ||
                      (feedbackTargetKbps.load() == metrics.targetBitrateKbps &&
                       feedbackReceiveQueueMs.load() >= 80U),
                  "swvtp automatic bitrate server feedback fields");
}

bool runSwVtpNackRecoveryCase(SwCoreApplication& app) {
    std::cout << "[MediaTransportEndToEndSelfTest] swvtp av1 nack recovery begin" << std::endl;
    const uint16_t serverPort = 56446;
    const uint16_t clientPort = 56447;
    SwMediaServerConfig config =
        makeServerConfig(SwMediaTransportProtocol::SwVtp, "127.0.0.0", serverPort);
    config.endpoint.bindAddress = "127.0.0.1";
    config.mtuBytes = 96U;

    std::shared_ptr<DropFirstSwVtpVideoFragmentTransport> transport(
        new DropFirstSwVtpVideoFragmentTransport());
    SwMediaServer server(config);
    server.setTransport(transport);

    SwVideoPublishStream stream = makeStream(SwVideoPacket::Codec::AV1);
    stream.latencyBudgetMs = 90;
    if (!expect(server.addVideoStream(stream), "swvtp nack add stream")) {
        return false;
    }
    if (!expect(server.start(), "swvtp nack server start")) {
        return false;
    }

    HeadlessPlayerProbe player;
    if (!expect(player.start(SwString("swvtp://127.0.0.1:") +
                             SwString::number(serverPort) +
                             "?localport=" +
                             SwString::number(clientPort)),
                "swvtp nack player start")) {
        server.stop();
        return false;
    }

    const bool sessionReady = pumpUntil(app,
                                        [&player, transport]() {
                                            const SwVtpVideoSourceMetrics metrics =
                                                player.swvtpMetrics();
                                            return transport->activeClientCount() > 0U &&
                                                   metrics.accepted &&
                                                   metrics.negotiatedTargetBitrateKbps > 0U;
                                        },
                                        2500);
    if (!expect(sessionReady, "swvtp nack session negotiated")) {
        player.stop();
        server.stop();
        return false;
    }

    const SwByteArray payload = makeLargeAv1Frame(9, 900);
    const SwVideoPacket packet = makePacket(SwVideoPacket::Codec::AV1, payload, 0, true);
    const bool published = server.publishVideoPacket("main", packet);
    const bool recovered = pumpUntil(app,
                                     [&player]() {
                                         const SwVtpVideoSourceMetrics metrics =
                                             player.swvtpMetrics();
                                         return player.packetCount() >= 1U &&
                                                metrics.nackRequestsSent > 0U;
                                     },
                                     2500);

    const SwVtpVideoSourceMetrics clientMetrics = player.swvtpMetrics();
    const uint64_t receivedPackets = player.packetCount();
    const uint64_t receivedBytes = player.byteCount();
    const uint32_t droppedFragments = transport->droppedFragments();
    std::cout << "[MediaTransportEndToEndSelfTest] swvtp av1 nack recovery"
              << " packets=" << receivedPackets
              << " bytes=" << receivedBytes
              << " droppedFragments=" << droppedFragments
              << " nacks=" << clientMetrics.nackRequestsSent
              << " requestedFragments=" << clientMetrics.nackFragmentsRequested
              << " transferMs=" << clientMetrics.averageTransferLatencyMs()
              << std::endl;

    player.stop();
    server.stop();
    return expect(published, "swvtp nack packet published") &&
           expect(droppedFragments == 1U, "swvtp nack test dropped one fragment") &&
           expect(recovered, "swvtp nack recovered missing fragment") &&
           expect(receivedPackets >= 1U, "swvtp nack player received video") &&
           expect(receivedBytes == static_cast<uint64_t>(payload.size()),
                  "swvtp nack full payload reassembled") &&
           expect(clientMetrics.nackRequestsSent > 0U, "swvtp nack request emitted") &&
           expect(clientMetrics.nackFragmentsRequested > 0U,
                  "swvtp nack fragment request emitted") &&
           expect(clientMetrics.maxTransferLatencyMs() < 100.0,
                  "swvtp nack transfer latency sub100");
}

bool runSwVtpCase(SwCoreApplication& app) {
    bool ok = true;
    ok = runSwVtpCodecCase(app, SwVideoPacket::Codec::AV1, 56440, 56441, true) && ok;
    ok = runSwVtpCodecCase(app, SwVideoPacket::Codec::H264, 56442, 56443, false) && ok;
    ok = runSwVtpCodecCase(app, SwVideoPacket::Codec::H265, 56444, 56445, false) && ok;
    ok = runSwVtpNackRecoveryCase(app) && ok;
    return ok;
}

bool runRtspCase(SwCoreApplication& app) {
    std::cout << "[MediaTransportEndToEndSelfTest] rtsp begin" << std::endl;
    const uint16_t rtspPort = 56460;
    const uint16_t localRtpPort = 56462;
    MinimalRtspH264Server server(rtspPort);
    if (!expect(server.start(), "rtsp control server start")) {
        return false;
    }

    HeadlessPlayerProbe player;
    if (!expect(player.start(SwString("rtsp://127.0.0.1:") +
                             SwString::number(rtspPort) +
                             "/stream?transport=udp&local_rtp=" +
                             SwString::number(localRtpPort) +
                             "&local_rtcp=" +
                             SwString::number(static_cast<int>(localRtpPort + 1)) +
                             "&bind=0.0.0.0"),
                "rtsp player start")) {
        server.stop();
        return false;
    }

    const bool ok = pumpUntil(app, [&player]() { return player.packetCount() >= 3U; }, 3500);
    std::cout << "[MediaTransportEndToEndSelfTest] rtsp packets=" << player.packetCount()
              << " bytes=" << player.byteCount()
              << " rtpSent=" << server.rtpPacketsSent() << std::endl;
    player.stop();
    server.stop();
    return expect(ok, "rtsp player received video") &&
           expect(player.codec() == SwVideoPacket::Codec::H264, "rtsp codec h264");
}

} // namespace

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    bool ok = true;
    ok = runUdpCase(app) && ok;
    ok = runRtpCase(app) && ok;
    ok = runRtspCase(app) && ok;
    ok = runSwVtpCase(app) && ok;

    if (!ok) {
        return 1;
    }
    std::cout << "[MediaTransportEndToEndSelfTest] PASS" << std::endl;
    return 0;
}
