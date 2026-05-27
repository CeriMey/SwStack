#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"
#include "media/encoder/SwAv1LiveEncoder.h"
#include "media/encoder/SwVideoEncoder.h"
#include "media/server/SwMediaServer.h"
#include "media/server/SwMediaServerFactory.h"
#include "media/swvtp/SwVtpFeedbackController.h"
#include "media/swvtp/SwVtpKlv.h"
#include "media/swvtp/SwVtpProtocol.h"
#include "media/swvtp/SwVtpServerTransport.h"
#include "media/swvtp/SwVtpUdpTransport.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {

class FakeAv1Backend : public SwAv1LiveEncoderBackend {
public:
    SwString name() const override {
        return "FakeAv1Backend";
    }

    bool configure(const SwVideoEncoderConfig& config) override {
        configured = config.codec == SwVideoPacket::Codec::AV1 && config.isValid();
        return configured;
    }

    bool encodeFrame(const SwVideoFrame& frame, SwVideoPacket& outPacket) override {
        if (!configured || !frame.isValid()) {
            return false;
        }
        SwByteArray payload;
        payload.append(static_cast<char>(0x12));
        payload.append(static_cast<char>(0x00));
        payload.append(static_cast<char>(0x01));
        outPacket = SwVideoPacket(SwVideoPacket::Codec::AV1,
                                  payload,
                                  frame.timestamp(),
                                  frame.timestamp(),
                                  keyFrameRequests != 0U);
        return true;
    }

    bool setTargetBitrateKbps(uint32_t bitrateKbps) override {
        targetBitrateKbps = bitrateKbps;
        return true;
    }

    void requestKeyFrame() override {
        ++keyFrameRequests;
    }

    bool configured{false};
    uint32_t targetBitrateKbps{0};
    uint32_t keyFrameRequests{0};
};

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[MediaServerArchitectureSelfTest] FAIL " << message << "\n";
        return false;
    }
    return true;
}

SwVideoPacket makeAv1Packet() {
    SwByteArray payload;
    payload.append(static_cast<char>(0x12));
    payload.append(static_cast<char>(0x00));
    payload.append(static_cast<char>(0x40));
    payload.append(static_cast<char>(0x41));
    payload.append(static_cast<char>(0x42));
    return SwVideoPacket(SwVideoPacket::Codec::AV1, payload, 1000, 1000, true);
}

SwVideoPacket makeSecondAv1Packet() {
    SwByteArray payload;
    payload.append(static_cast<char>(0x12));
    payload.append(static_cast<char>(0x00));
    payload.append(static_cast<char>(0x50));
    payload.append(static_cast<char>(0x51));
    payload.append(static_cast<char>(0x52));
    payload.append(static_cast<char>(0x53));
    return SwVideoPacket(SwVideoPacket::Codec::AV1, payload, 34000, 34000, false);
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

SwMediaPacket makeKlvPacket() {
    SwByteArray payload;
    payload.reserve(1800);
    for (int i = 0; i < 1800; ++i) {
        payload.append(static_cast<char>((i * 17 + 3) & 0xFF));
    }

    SwMediaPacket packet;
    packet.setType(SwMediaPacket::Type::Metadata);
    packet.setTrackId("klv");
    packet.setCodec("klv");
    packet.setClockRate(90000);
    packet.setPayload(payload);
    packet.setPts(55000);
    packet.setDts(55000);
    return packet;
}

bool byteArraysEqual(const SwByteArray& left, const SwByteArray& right) {
    if (left.size() != right.size()) {
        return false;
    }
    if (left.isEmpty()) {
        return true;
    }
    return left.constData() && right.constData() &&
           std::memcmp(left.constData(), right.constData(), left.size()) == 0;
}

uint16_t readBe16(const unsigned char* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) |
                                 static_cast<uint16_t>(data[1]));
}

uint32_t readBe32(const unsigned char* data) {
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           static_cast<uint32_t>(data[3]);
}

SwVideoPublishStream makeStream() {
    SwVideoPublishStream stream;
    stream.id = "main";
    stream.trackId = "video";
    stream.codec = SwVideoPacket::Codec::AV1;
    stream.width = 1920;
    stream.height = 1080;
    stream.startBitrateKbps = 4000;
    stream.minBitrateKbps = 800;
    stream.maxBitrateKbps = 12000;
    return stream;
}

uint64_t nowUs() {
    const std::chrono::steady_clock::duration elapsed =
        std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
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

bool runTransportFactoryChecks() {
    bool ok = true;
    std::shared_ptr<SwVideoTransportServer> swvtp =
        SwMediaServerFactory::createTransport("swvtp://0.0.0.0:55245");
    std::shared_ptr<SwVideoTransportServer> rtp =
        SwMediaServerFactory::createTransport("rtp://0.0.0.0:5004");
    std::shared_ptr<SwVideoTransportServer> rtsp =
        SwMediaServerFactory::createTransport("rtsp://0.0.0.0:8554");
    std::shared_ptr<SwVideoTransportServer> udp =
        SwMediaServerFactory::createTransport("udp://0.0.0.0:5000");

    ok = expect(swvtp && swvtp->protocolName() == "swvtp", "SwVTP transport factory") && ok;
    ok = expect(rtp && rtp->protocolName() == "rtp", "RTP transport factory") && ok;
    ok = expect(rtsp && rtsp->protocolName() == "rtsp", "RTSP transport factory") && ok;
    ok = expect(udp && udp->protocolName() == "udp", "UDP transport factory") && ok;
    return ok;
}

bool runServerPublishCheck() {
    bool ok = true;
    SwMediaServerConfig config;
    config.endpoint.protocol = SwMediaTransportProtocol::SwVtp;
    config.endpoint.host = "0.0.0.0";
    config.endpoint.port = 55246;
    config.mtuBytes = 512;

    std::shared_ptr<SwVideoTransportServer> transport =
        SwMediaServerFactory::createTransport(config);
    std::shared_ptr<SwVtpServerTransport> swvtpTransport =
        std::dynamic_pointer_cast<SwVtpServerTransport>(transport);
    std::shared_ptr<FakeAv1Backend> backend = std::make_shared<FakeAv1Backend>();
    std::shared_ptr<SwAv1LiveEncoder> encoder = std::make_shared<SwAv1LiveEncoder>(backend);
    SwVideoEncoderConfig encoderConfig;
    encoderConfig.codec = SwVideoPacket::Codec::AV1;
    encoderConfig.width = 1920;
    encoderConfig.height = 1080;
    encoderConfig.startBitrateKbps = 4000;
    encoderConfig.minBitrateKbps = 800;
    encoderConfig.maxBitrateKbps = 12000;
    ok = expect(encoder->configure(encoderConfig), "AV1 encoder configure") && ok;

    SwMediaServer server(config);
    server.setTransport(transport);
    server.setVideoEncoder(encoder);
    ok = expect(server.addVideoStream(makeStream()), "server add stream") && ok;
    ok = expect(server.addMetadataTrack(makeKlvTrack()), "server add KLV metadata track") && ok;
    ok = expect(server.start(), "server start") && ok;
    ok = expect(static_cast<bool>(swvtpTransport), "server SwVTP transport type") && ok;

    SwVtpUdpTransport client;
    ok = expect(client.open(SwString("127.0.0.1"), 0), "client SwUdpSocket bind") && ok;
    SwVtpClientAnnouncement announcement;
    announcement.streamId = 1;
    swVtpParseIpv4Address("127.0.0.1", announcement.clientIpv4);
    announcement.receivePort = client.localPort();
    ok = expect(client.send(makeControlDatagram(SwVtpMessageType::Hello,
                                                swVtpSerializeClientAnnouncement(announcement)),
                            0x7F000001U,
                            config.endpoint.port),
                "client hello send") && ok;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (swvtpTransport && swvtpTransport->activeClientCount() == 0U &&
           std::chrono::steady_clock::now() < deadline) {
        SwVtpUdpPacket ignored;
        client.receive(10, ignored);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ok = expect(swvtpTransport && swvtpTransport->activeClientCount() == 1U,
                "server accepted SwVTP client") && ok;

    ok = expect(server.publishVideoPacket("main", makeAv1Packet()), "server publish packet") && ok;
    const SwMediaPacket klvSent = makeKlvPacket();
    ok = expect(server.publishMediaPacket("klv", klvSent), "server publish KLV metadata") && ok;

    SwVtpKlvReassembler klvReassembler;
    klvReassembler.setTrackId("klv");
    SwMediaPacket klvReceived;
    bool gotKlv = false;
    const auto klvDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!gotKlv && std::chrono::steady_clock::now() < klvDeadline) {
        SwVtpUdpPacket packet;
        if (!client.receive(50, packet)) {
            continue;
        }
        SwVtpDatagram datagram;
        if (!swVtpParseDatagram(packet.bytes, datagram) ||
            datagram.header.messageType != SwVtpMessageType::KlvFragment) {
            continue;
        }
        SwVtpKlvReassembler::PushResult push =
            klvReassembler.pushDatagram(datagram, nowUs());
        if (push.completed()) {
            klvReceived = push.packet;
            gotKlv = true;
        }
    }
    ok = expect(gotKlv, "client reassembled KLV metadata") && ok;
    ok = expect(klvReceived.type() == SwMediaPacket::Type::Metadata,
                "KLV packet type preserved") && ok;
    ok = expect(klvReceived.trackId() == "klv", "KLV track id preserved") && ok;
    ok = expect(klvReceived.codec() == "klv", "KLV codec preserved") && ok;
    ok = expect(klvReceived.pts() == klvSent.pts(), "KLV PTS preserved") && ok;
    ok = expect(byteArraysEqual(klvReceived.payload(), klvSent.payload()),
                "KLV payload preserved") && ok;
    ok = expect(klvReassembler.snapshot().completedSamples == 1U,
                "KLV reassembler completed one sample") && ok;

    SwVtpReceiverStats receiverStats;
    receiverStats.streamId = 1;
    receiverStats.trackId = 1;
    receiverStats.lastFrameId = 1;
    receiverStats.estimatedBandwidthKbps = 1000;
    receiverStats.lossPermille = 100;
    ok = expect(client.send(makeControlDatagram(SwVtpMessageType::ReceiverStats,
                                                swVtpSerializeReceiverStats(receiverStats)),
                            0x7F000001U,
                            config.endpoint.port),
                "client receiver stats send") && ok;

    const auto abrDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (encoder->targetBitrateKbps() == 4000U &&
           std::chrono::steady_clock::now() < abrDeadline) {
        SwVtpUdpPacket ignored;
        client.receive(10, ignored);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const SwVideoServerMetrics metrics = server.metrics();
    ok = expect(metrics.framesSent == 1U, "server sent one frame") && ok;
    ok = expect(metrics.transport.datagramsSent > 0U, "server emitted transport datagrams") && ok;
    ok = expect(metrics.targetBitrateKbps < 4000U, "server applied receiver ABR target") && ok;
    ok = expect(encoder->targetBitrateKbps() == metrics.targetBitrateKbps,
                "media server applied ABR target to encoder") && ok;
    server.stop();
    return ok;
}

bool runFeedbackCheck() {
    SwVtpFeedbackController controller;
    SwVtpReceiverStats stats;
    stats.streamId = 1;
    stats.trackId = 1;
    stats.estimatedBandwidthKbps = 6000;
    stats.rttMs = 10;
    const SwVtpAdaptiveBitrateDecision decision = controller.update(stats, 1000);
    return expect(decision.targetBitrateKbps > 0U, "SwVTP feedback produces bitrate decision");
}

bool runAv1EncoderCheck() {
    bool ok = true;
    std::shared_ptr<FakeAv1Backend> backend = std::make_shared<FakeAv1Backend>();
    SwAv1LiveEncoder encoder(backend);

    SwVideoEncoderConfig config;
    config.codec = SwVideoPacket::Codec::AV1;
    config.width = 64;
    config.height = 64;
    config.startBitrateKbps = 2000;
    config.minBitrateKbps = 800;
    config.maxBitrateKbps = 6000;

    ok = expect(encoder.configure(config), "AV1 live encoder configures") && ok;
    ok = expect(encoder.setTargetBitrateKbps(200), "AV1 live encoder accepts bitrate") && ok;
    ok = expect(encoder.targetBitrateKbps() == 800U, "AV1 live encoder clamps bitrate") && ok;
    ok = expect(backend->targetBitrateKbps == 800U, "AV1 backend receives bitrate") && ok;

    SwVideoFrame frame = SwVideoFrame::allocate(64, 64, SwVideoPixelFormat::RGB24);
    frame.setTimestamp(12345);
    encoder.requestKeyFrame();
    SwVideoPacket packet;
    ok = expect(encoder.encodeFrame(frame, packet), "AV1 live encoder encodes via backend") && ok;
    ok = expect(packet.codec() == SwVideoPacket::Codec::AV1, "AV1 live encoder packet codec") && ok;
    ok = expect(packet.isKeyFrame(), "AV1 live encoder keyframe request") && ok;
    return ok;
}

bool runDatagramTransportCheck(SwMediaTransportProtocol protocol,
                               const char* label,
                               bool expectRtpHeader) {
    bool ok = true;
    SwVtpUdpTransport receiver;
    ok = expect(receiver.open(SwString("127.0.0.1"), 0), "datagram receiver bind") && ok;

    SwMediaServerConfig config;
    config.endpoint.protocol = protocol;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.bindAddress = "0.0.0.0";
    config.endpoint.port = receiver.localPort();
    config.mtuBytes = 1200;

    std::shared_ptr<SwVideoTransportServer> transport =
        SwMediaServerFactory::createTransport(config);
    SwMediaServer server(config);
    server.setTransport(transport);
    ok = expect(server.addVideoStream(makeStream()), label) && ok;
    ok = expect(server.start(), label) && ok;
    const SwVideoPacket sent = makeAv1Packet();
    const SwVideoPacket second = makeSecondAv1Packet();
    ok = expect(server.publishVideoPacket("main", sent), label) && ok;
    ok = expect(server.publishVideoPacket("main", second), label) && ok;

    SwVtpUdpPacket firstPacket;
    SwVtpUdpPacket secondPacket;
    ok = expect(receiver.receive(500, firstPacket), label) && ok;
    ok = expect(receiver.receive(500, secondPacket), label) && ok;
    ok = expect(firstPacket.senderIpv4 == 0x7F000001U, label) && ok;
    ok = expect(firstPacket.senderPort != 0U, label) && ok;
    if (ok && expectRtpHeader) {
        const unsigned char* firstData =
            reinterpret_cast<const unsigned char*>(firstPacket.bytes.constData());
        const unsigned char* secondData =
            reinterpret_cast<const unsigned char*>(secondPacket.bytes.constData());
        ok = expect(firstPacket.bytes.size() == sent.payload().size() + 12U, label) && ok;
        ok = expect(secondPacket.bytes.size() == second.payload().size() + 12U, label) && ok;
        ok = expect(firstData && secondData, label) && ok;
        if (!firstData || !secondData) {
            server.stop();
            return false;
        }
        ok = expect(((firstData[0] >> 6U) == 2U) && ((secondData[0] >> 6U) == 2U),
                    label) && ok;
        ok = expect((firstData[0] & 0x0FU) == 0U, label) && ok;
        ok = expect((firstData[1] & 0x80U) != 0U, label) && ok;
        ok = expect((firstData[1] & 0x7FU) == 97U, label) && ok;
        ok = expect((secondData[1] & 0x7FU) == 97U, label) && ok;
        ok = expect(readBe16(firstData + 2) == 1U, label) && ok;
        ok = expect(readBe16(secondData + 2) == 2U, label) && ok;
        ok = expect(readBe32(firstData + 4) == 90U, label) && ok;
        ok = expect(readBe32(secondData + 4) == 3060U, label) && ok;
        ok = expect(readBe32(firstData + 8) == 0x53575254U, label) && ok;
        ok = expect(std::memcmp(firstData + 12,
                                sent.payload().constData(),
                                sent.payload().size()) == 0,
                    label) && ok;
        ok = expect(std::memcmp(secondData + 12,
                                second.payload().constData(),
                                second.payload().size()) == 0,
                    label) && ok;
    } else if (ok) {
        ok = expect(byteArraysEqual(firstPacket.bytes, sent.payload()), label) && ok;
        ok = expect(byteArraysEqual(secondPacket.bytes, second.payload()), label) && ok;
    }

    const SwVideoServerMetrics metrics = server.metrics();
    ok = expect(metrics.framesAccepted == 2U, label) && ok;
    ok = expect(metrics.framesSent == 2U, label) && ok;
    ok = expect(metrics.transport.datagramsSent == 2U, label) && ok;
    ok = expect(metrics.transport.bytesSent ==
                    firstPacket.bytes.size() + secondPacket.bytes.size(),
                label) && ok;
    server.stop();
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok = runTransportFactoryChecks() && ok;
    ok = runServerPublishCheck() && ok;
    ok = runFeedbackCheck() && ok;
    ok = runAv1EncoderCheck() && ok;
    ok = runDatagramTransportCheck(SwMediaTransportProtocol::Udp,
                                   "UDP server emits datagram",
                                   false) && ok;
    ok = runDatagramTransportCheck(SwMediaTransportProtocol::Rtp,
                                   "RTP server emits RTP datagram",
                                   true) && ok;
    ok = runDatagramTransportCheck(SwMediaTransportProtocol::Rtsp,
                                   "RTSP server emits RTP media datagram",
                                   true) && ok;
    if (!ok) {
        return EXIT_FAILURE;
    }
    std::cout << "[MediaServerArchitectureSelfTest] PASS\n";
    return EXIT_SUCCESS;
}
