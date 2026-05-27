#include "media/swvtp/SwVtpAv1.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void appendByte(SwByteArray& payload, uint8_t value) {
    payload.append(static_cast<char>(value));
}

void appendLeb128(SwByteArray& payload, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0U) {
            byte |= 0x80U;
        }
        appendByte(payload, byte);
    } while (value != 0U);
}

SwByteArray makeObu(SwVtpAv1ObuType type,
                    const SwByteArray& body,
                    bool withExtension = false,
                    uint8_t temporalLayer = 0) {
    SwByteArray obu;
    uint8_t header = static_cast<uint8_t>((static_cast<uint8_t>(type) & 0x0FU) << 3U);
    if (withExtension) {
        header |= 0x04U;
    }
    header |= 0x02U;
    appendByte(obu, header);
    if (withExtension) {
        appendByte(obu, static_cast<uint8_t>((temporalLayer & 0x07U) << 5U));
    }
    appendLeb128(obu, body.size());
    obu.append(body);
    return obu;
}

SwByteArray makeBody(std::size_t bytes, uint8_t seed) {
    SwByteArray body;
    body.reserve(bytes);
    for (std::size_t i = 0; i < bytes; ++i) {
        appendByte(body, static_cast<uint8_t>(seed + (i & 0x3FU)));
    }
    return body;
}

SwByteArray makeAv1AccessUnit() {
    SwByteArray payload;
    payload.append(makeObu(SwVtpAv1ObuType::TemporalDelimiter, makeBody(1, 0x01U)));
    payload.append(makeObu(SwVtpAv1ObuType::SequenceHeader, makeBody(12, 0x10U)));
    payload.append(makeObu(SwVtpAv1ObuType::Frame, makeBody(2600, 0x40U), true, 1));
    return payload;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[SwVtpAv1SelfTest] FAIL " << message << "\n";
        return false;
    }
    return true;
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

SwByteArray withoutLastByte(const SwByteArray& bytes) {
    if (bytes.isEmpty()) {
        return bytes;
    }
    return bytes.mid(0, static_cast<int>(bytes.size() - 1U));
}

SwVtpDatagram makeValidationDatagram() {
    SwVtpDatagram datagram;
    datagram.header.version = kSwVtpVersion1;
    datagram.header.messageType = SwVtpMessageType::FrameFragment;
    datagram.header.streamId = 9;
    datagram.header.trackId = 1;
    datagram.header.trackType = SwVtpTrackType::Video;
    datagram.header.codec = SwVtpCodec::AV1;
    datagram.header.frameId = 100;
    datagram.header.fragmentIndex = 0;
    datagram.header.fragmentCount = 1;
    datagram.header.ptsUs = 10000;
    datagram.header.captureTimeUs = 9900;
    datagram.header.deadlineUs = 20000;
    datagram.header.sendTimeUs = 10100;
    datagram.payload = makeBody(8, 0x50U);
    datagram.header.payloadBytes = static_cast<uint16_t>(datagram.payload.size());
    return datagram;
}

struct IvfFrame {
    uint64_t timestamp{0};
    SwByteArray payload{};
};

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
    const uint16_t headerBytes = readLe16(header + 6);
    if (headerBytes < 32U) {
        error = "invalid IVF header size";
        return false;
    }
    if (std::memcmp(header + 8, "AV01", 4) != 0 &&
        std::memcmp(header + 8, "av01", 4) != 0) {
        error = "IVF file is not AV1";
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

bool runHeaderRoundTrip() {
    SwVtpDatagram datagram;
    datagram.header.version = kSwVtpVersion1;
    datagram.header.messageType = SwVtpMessageType::FrameFragment;
    datagram.header.flags = SwVtpFlag_KeyFrame | SwVtpFlag_FirstFragment;
    datagram.header.streamId = 7;
    datagram.header.trackId = 3;
    datagram.header.trackType = SwVtpTrackType::Video;
    datagram.header.codec = SwVtpCodec::AV1;
    datagram.header.temporalLayer = 1;
    datagram.header.spatialLayer = 0;
    datagram.header.frameId = 99;
    datagram.header.fragmentIndex = 2;
    datagram.header.fragmentCount = 9;
    datagram.header.ptsUs = 123456;
    datagram.header.captureTimeUs = 123000;
    datagram.header.deadlineUs = 123556;
    datagram.header.sendTimeUs = 123400;
    datagram.payload = makeBody(24, 0x22U);

    const SwByteArray encoded = swVtpSerializeDatagram(datagram);
    SwVtpDatagram parsed;
    bool ok = expect(swVtpParseDatagram(encoded, parsed), "parse serialized datagram");
    ok = expect(encoded.size() == kSwVtpHeaderBytes + datagram.payload.size(),
                "serialized datagram size") && ok;
    ok = expect(parsed.header.headerBytes == kSwVtpHeaderBytes,
                "header byte count") && ok;
    ok = expect(parsed.header.streamId == 7, "stream id roundtrip") && ok;
    ok = expect(parsed.header.trackId == 3, "track id roundtrip") && ok;
    ok = expect(parsed.header.frameId == 99, "frame id roundtrip") && ok;
    ok = expect(parsed.header.fragmentIndex == 2, "fragment index roundtrip") && ok;
    ok = expect(parsed.header.fragmentCount == 9, "fragment count roundtrip") && ok;
    ok = expect(parsed.header.payloadBytes == 24, "payload bytes roundtrip") && ok;
    ok = expect(parsed.header.captureTimeUs == 123000, "capture timestamp roundtrip") && ok;
    ok = expect(parsed.payload == datagram.payload, "payload roundtrip") && ok;
    if (ok) {
        std::cout << "[SwVTP header roundtrip] PASS\n";
    }
    return ok;
}

bool runProtocolValidationScenario() {
    const SwVtpDatagram datagram = makeValidationDatagram();
    const SwByteArray encoded = swVtpSerializeDatagram(datagram);
    SwVtpDatagram parsedDatagram;
    SwVtpHeader parsedHeader;

    bool ok = expect(swVtpParseDatagram(encoded, parsedDatagram),
                     "valid datagram parses before negative checks");
    ok = expect(!swVtpParseDatagram(withoutLastByte(encoded), parsedDatagram),
                "truncated datagram is rejected") && ok;

    SwByteArray invalidMagic = encoded;
    invalidMagic[0] = '\0';
    ok = expect(!swVtpParseDatagram(invalidMagic, parsedDatagram),
                "invalid magic is rejected") && ok;

    SwByteArray invalidVersion = encoded;
    invalidVersion[4] = static_cast<char>(kSwVtpVersion1 + 1U);
    ok = expect(!swVtpParseDatagram(invalidVersion, parsedDatagram),
                "invalid version is rejected") && ok;

    SwByteArray invalidMessage = encoded;
    invalidMessage[5] = static_cast<char>(SwVtpMessageType::Invalid);
    ok = expect(!swVtpParseDatagram(invalidMessage, parsedDatagram),
                "invalid message type is rejected") && ok;

    SwByteArray invalidHeaderBytes = encoded;
    invalidHeaderBytes[7] = static_cast<char>(kSwVtpHeaderBytes - 1U);
    ok = expect(!swVtpParseDatagram(invalidHeaderBytes, parsedDatagram),
                "invalid header size is rejected") && ok;

    SwByteArray oversizedPayload = encoded;
    oversizedPayload[28] = '\x01';
    oversizedPayload[29] = '\0';
    ok = expect(!swVtpParseHeader(oversizedPayload, parsedHeader),
                "header claiming missing payload is rejected") && ok;

    SwByteArray trailingBytes = encoded;
    trailingBytes.append('x');
    ok = expect(swVtpParseHeader(trailingBytes, parsedHeader),
                "header parser accepts datagram prefix") && ok;
    ok = expect(!swVtpParseDatagram(trailingBytes, parsedDatagram),
                "datagram parser rejects trailing bytes") && ok;

    if (ok) {
        std::cout << "[SwVTP protocol validation] PASS\n";
    }
    return ok;
}

bool runDeliveryModeScenario() {
    uint32_t anyMask = 0;
    uint32_t mask192 = 0;
    uint32_t mask192168 = 0;
    uint32_t exactClient = 0;
    uint32_t client192168 = 0;
    uint32_t client192167 = 0;
    uint32_t client10 = 0;
    uint32_t invalidMask = 0;
    uint32_t multicast239 = 0;
    uint32_t multicast238 = 0;
    uint32_t broadcast = 0;

    bool ok = expect(swVtpParseIpv4Address("0.0.0.0", anyMask),
                     "parse unicast any mask");
    ok = expect(swVtpParseIpv4Address("192.0.0.0", mask192),
                "parse unicast /8 style mask") && ok;
    ok = expect(swVtpParseIpv4Address("192.168.0.0", mask192168),
                "parse unicast /16 style mask") && ok;
    ok = expect(swVtpParseIpv4Address("192.168.1.20", exactClient),
                "parse exact unicast address") && ok;
    ok = expect(swVtpParseIpv4Address("192.168.55.77", client192168),
                "parse allowed 192.168 client") && ok;
    ok = expect(swVtpParseIpv4Address("192.167.55.77", client192167),
                "parse denied 192.167 client") && ok;
    ok = expect(swVtpParseIpv4Address("10.1.2.3", client10),
                "parse unicast client") && ok;
    ok = expect(swVtpParseIpv4Address("192.0.1.0", invalidMask),
                "parse invalid sparse unicast mask") && ok;
    ok = expect(swVtpParseIpv4Address("239.10.20.30", multicast239),
                "parse 239 multicast address") && ok;
    ok = expect(swVtpParseIpv4Address("238.10.20.30", multicast238),
                "parse non-239 multicast address") && ok;
    ok = expect(swVtpParseIpv4Address("255.255.255.255", broadcast),
                "parse broadcast address") && ok;
    ok = expect(!swVtpParseIpv4Address("192.168.1", client10),
                "reject incomplete IPv4 address") && ok;
    ok = expect(!swVtpParseIpv4Address("300.1.2.3", client10),
                "reject IPv4 octet over 255") && ok;

    SwVtpUdpEndpoint anyEndpoint = swVtpMakeUnicastEndpoint(anyMask, 5000);
    SwVtpUdpEndpoint endpoint192 = swVtpMakeUnicastEndpoint(mask192, 5000);
    SwVtpUdpEndpoint endpoint192168 = swVtpMakeUnicastEndpoint(mask192168, 5000);
    SwVtpUdpEndpoint exactEndpoint = swVtpMakeUnicastEndpoint(exactClient, 5000);
    ok = expect(anyEndpoint.isValid(), "unicast any-client mask is valid") && ok;
    ok = expect(endpoint192.isValid(), "unicast 192.x mask is valid") && ok;
    ok = expect(endpoint192168.isValid(), "unicast 192.168.x mask is valid") && ok;
    ok = expect(exactEndpoint.isValid(), "unicast exact client is valid") && ok;
    ok = expect(swVtpUnicastMaskAllowsClient(anyMask, client10),
                "0.0.0.0 mask accepts any unicast client") && ok;
    ok = expect(swVtpUnicastMaskAllowsClient(mask192, client192168),
                "192.0.0.0 mask accepts 192.x client") && ok;
    ok = expect(!swVtpUnicastMaskAllowsClient(mask192, client10),
                "192.0.0.0 mask rejects non-192 client") && ok;
    ok = expect(swVtpUnicastMaskAllowsClient(mask192168, client192168),
                "192.168.0.0 mask accepts 192.168 client") && ok;
    ok = expect(!swVtpUnicastMaskAllowsClient(mask192168, client192167),
                "192.168.0.0 mask rejects adjacent subnet") && ok;
    ok = expect(swVtpUnicastMaskAllowsClient(exactClient, exactClient),
                "exact unicast mask accepts exact client") && ok;
    ok = expect(!swVtpUnicastMaskAllowsClient(exactClient, client192168),
                "exact unicast mask rejects other client") && ok;
    ok = expect(!swVtpMakeUnicastEndpoint(invalidMask, 5000).isValid(),
                "unicast sparse mask is invalid") && ok;
    ok = expect(!swVtpMakeUnicastEndpoint(multicast239, 5000).isValid(),
                "unicast mask rejects multicast range") && ok;

    SwVtpClientAnnouncement client;
    client.streamId = 12;
    client.receivePort = 6000;
    client.clientIpv4 = client192168;
    ok = expect(client.isValid(), "client announcement is valid") && ok;
    ok = expect(swVtpEndpointAcceptsClient(endpoint192, client),
                "unicast endpoint accepts announced matching client") && ok;
    ok = expect(!swVtpEndpointAcceptsClient(swVtpMakeUnicastEndpoint(client10, 5000),
                                            client),
                "unicast endpoint rejects announced non-matching client") && ok;

    SwVtpStreamConfig config;
    config.streamId = 12;
    config.trackId = 1;
    config.trackType = SwVtpTrackType::Video;
    config.codec = SwVtpCodec::AV1;
    config.endpoint = endpoint192;
    ok = expect(config.isValid(), "stream config with unicast mask is valid") && ok;
    ok = expect(swVtpStreamConfigAcceptsClient(config, client),
                "stream config accepts matching client announcement") && ok;
    client.streamId = 13;
    ok = expect(!swVtpStreamConfigAcceptsClient(config, client),
                "stream config rejects wrong stream announcement") && ok;
    client.streamId = 12;

    SwVtpStreamConfig parsedConfig;
    ok = expect(swVtpParseStreamConfigPayload(swVtpSerializeStreamConfig(config),
                                              parsedConfig),
                "stream config payload roundtrip") && ok;
    ok = expect(parsedConfig.endpoint.deliveryMode == SwVtpDeliveryMode::Unicast &&
                    parsedConfig.endpoint.ipv4 == mask192,
                "stream config keeps unicast mask") && ok;

    SwVtpClientAnnouncement parsedClient;
    ok = expect(swVtpParseClientAnnouncementPayload(
                    swVtpSerializeClientAnnouncement(client),
                    parsedClient),
                "client announcement payload roundtrip") && ok;
    ok = expect(parsedClient.clientIpv4 == client192168 &&
                    parsedClient.receivePort == 6000,
                "client announcement keeps address and port") && ok;

    SwVtpClientAnnouncement invalidClient = client;
    invalidClient.clientIpv4 = kSwVtpIpv4Any;
    ok = expect(!swVtpParseClientAnnouncementPayload(
                    swVtpSerializeClientAnnouncement(invalidClient),
                    parsedClient),
                "client announcement rejects 0.0.0.0 client address") && ok;

    SwVtpUdpEndpoint broadcastEndpoint = swVtpMakeBroadcastEndpoint(5000);
    ok = expect(broadcastEndpoint.isValid(), "broadcast endpoint is valid") && ok;
    broadcastEndpoint.ipv4 = client10;
    ok = expect(!broadcastEndpoint.isValid(),
                "broadcast endpoint rejects non-broadcast address") && ok;

    SwVtpUdpEndpoint multicastEndpoint =
        swVtpMakeMulticast239Endpoint(multicast239, 5000, 4, true);
    ok = expect(multicastEndpoint.isValid(),
                "239.x multicast endpoint is valid") && ok;
    ok = expect(!swVtpMakeMulticast239Endpoint(multicast238, 5000).isValid(),
                "non-239 multicast endpoint is rejected") && ok;
    ok = expect(!swVtpMakeMulticast239Endpoint(multicast239, 5000, 0).isValid(),
                "multicast endpoint rejects ttl zero") && ok;

    SwVtpUdpEndpoint parsedEndpoint;
    ok = expect(swVtpParseUdpEndpointPayload(swVtpSerializeUdpEndpoint(multicastEndpoint),
                                             parsedEndpoint),
                "multicast endpoint payload roundtrip") && ok;
    ok = expect(parsedEndpoint.deliveryMode == SwVtpDeliveryMode::Multicast239 &&
                    parsedEndpoint.ipv4 == multicast239 &&
                    parsedEndpoint.ttl == 4 &&
                    (parsedEndpoint.flags & SwVtpEndpointFlag_MulticastLoopback) != 0U,
                "multicast endpoint keeps group ttl and loopback") && ok;

    SwByteArray endpointPayload = swVtpSerializeUdpEndpoint(multicastEndpoint);
    endpointPayload[9] = '\x01';
    ok = expect(!swVtpParseUdpEndpointPayload(endpointPayload, parsedEndpoint),
                "endpoint payload rejects non-zero reserved byte") && ok;

    if (ok) {
        std::cout << "[SwVTP delivery modes] PASS\n";
    }
    return ok;
}

SwVtpAv1PacketizerResult makePacketizedFrame() {
    const SwByteArray payload = makeAv1AccessUnit();
    SwVideoPacket packet(SwVideoPacket::Codec::AV1, payload, 10000, 10000, true);
    packet.setDiscontinuity(true);

    SwVtpAv1PacketizerOptions options;
    options.streamId = 2;
    options.trackId = 1;
    options.frameId = 42;
    options.nowUs = 1000;
    options.captureTimeUs = 9000;
    options.latencyBudgetUs = 90000;
    options.maxDatagramBytes = 400;
    return SwVtpAv1Packetizer::packetize(packet, options);
}

bool runPacketizerScenario() {
    const SwVtpAv1PacketizerResult result = makePacketizedFrame();
    bool ok = expect(result.ok, "packetizer result");
    ok = expect(result.containsSequenceHeader, "sequence header detected") && ok;
    ok = expect(result.keyFrame, "keyframe detected") && ok;
    ok = expect(result.datagrams.size() > 3U, "fragmented into multiple datagrams") && ok;

    for (std::size_t i = 0; i < result.serializedDatagrams.size(); ++i) {
        ok = expect(result.serializedDatagrams[i].size() <= 400U, "datagram under MTU") && ok;
    }

    const SwVtpHeader& first = result.datagrams[0].header;
    ok = expect(first.messageType == SwVtpMessageType::FrameFragment, "frame fragment type") && ok;
    ok = expect(first.codec == SwVtpCodec::AV1, "AV1 codec marker") && ok;
    ok = expect(first.hasFlag(SwVtpFlag_KeyFrame), "keyframe flag") && ok;
    ok = expect(first.hasFlag(SwVtpFlag_Discontinuity), "discontinuity flag") && ok;
    ok = expect(first.hasFlag(SwVtpFlag_FirstFragment), "first fragment flag") && ok;
    ok = expect(first.hasFlag(SwVtpFlag_CodecConfig), "codec config flag") && ok;
    ok = expect(first.fragmentCount == result.datagrams.size(), "fragment count") && ok;
    ok = expect(first.captureTimeUs == 9000, "capture timestamp") && ok;
    ok = expect(first.deadlineUs == 91000, "deadline") && ok;

    const SwVtpHeader& last = result.datagrams[result.datagrams.size() - 1U].header;
    ok = expect(last.hasFlag(SwVtpFlag_LastFragment), "last fragment flag") && ok;
    if (ok) {
        std::cout << "[SwVTP AV1 packetizer] PASS\n";
    }
    return ok;
}

bool runPacketizerValidationScenario() {
    const SwByteArray payload = makeAv1AccessUnit();
    SwVtpAv1PacketizerOptions options;
    options.streamId = 4;
    options.trackId = 2;
    options.frameId = 77;
    options.nowUs = 12345;
    options.latencyBudgetUs = 1000;
    options.maxDatagramBytes = 2000;

    SwVideoPacket h264Packet(SwVideoPacket::Codec::H264, payload, 0, 0, true);
    bool ok = expect(!SwVtpAv1Packetizer::packetize(h264Packet, options).ok,
                     "packetizer rejects non-AV1 packets");

    SwVideoPacket emptyPacket(SwVideoPacket::Codec::AV1, SwByteArray(), 0, 0, true);
    ok = expect(!SwVtpAv1Packetizer::packetize(emptyPacket, options).ok,
                "packetizer rejects empty payloads") && ok;

    SwVideoPacket av1Packet(SwVideoPacket::Codec::AV1, payload, -1, -1, false);
    SwVtpAv1PacketizerOptions tooSmall = options;
    tooSmall.maxDatagramBytes = kSwVtpHeaderBytes;
    ok = expect(!SwVtpAv1Packetizer::packetize(av1Packet, tooSmall).ok,
                "packetizer rejects MTU smaller than header") && ok;

    const SwVtpAv1PacketizerResult fallback =
        SwVtpAv1Packetizer::packetize(av1Packet, options);
    ok = expect(fallback.ok, "packetizer accepts valid fallback timestamp frame") && ok;
    ok = expect(fallback.datagrams[0].header.ptsUs == options.nowUs,
                "packetizer falls back PTS to send time") && ok;
    ok = expect(fallback.datagrams[0].header.captureTimeUs == options.nowUs,
                "packetizer falls back capture time to PTS") && ok;
    ok = expect(fallback.datagrams[0].header.deadlineUs ==
                    options.nowUs + options.latencyBudgetUs,
                "packetizer computes deadline from latency budget") && ok;

    if (ok) {
        std::cout << "[SwVTP AV1 packetizer validation] PASS\n";
    }
    return ok;
}

bool runReassemblerScenario() {
    const SwVtpAv1PacketizerResult result = makePacketizedFrame();
    SwVtpAv1Reassembler reassembler;
    SwVideoPacket completedPacket;
    bool completed = false;
    bool ok = expect(result.ok, "packetizer available for reassembler");

    for (std::size_t i = result.serializedDatagrams.size(); i > 0U; --i) {
        const std::size_t index = i - 1U;
        SwVtpAv1Reassembler::PushResult push =
            reassembler.pushSerializedDatagram(result.serializedDatagrams[index], 2000);
        if (push.completed()) {
            completed = true;
            completedPacket = push.packet;
        }
    }

    ok = expect(completed, "out-of-order completion") && ok;
    ok = expect(completedPacket.codec() == SwVideoPacket::Codec::AV1, "completed codec") && ok;
    ok = expect(completedPacket.payload() == makeAv1AccessUnit(), "completed payload") && ok;
    ok = expect(completedPacket.isKeyFrame(), "completed keyframe") && ok;
    ok = expect(completedPacket.isDiscontinuity(), "completed discontinuity") && ok;

    SwVtpAv1Reassembler::PushResult duplicate =
        reassembler.pushSerializedDatagram(result.serializedDatagrams[0], 3000);
    ok = expect(duplicate.status == SwVtpAv1Reassembler::PushStatus::Accepted,
                "fragment accepted after completed frame was cleared") && ok;

    if (ok) {
        std::cout << "[SwVTP AV1 reassembler] PASS\n";
    }
    return ok;
}

bool runReassemblerValidationScenario() {
    const SwVtpAv1PacketizerResult result = makePacketizedFrame();
    bool ok = expect(result.ok && result.datagrams.size() > 2U,
                     "packetized frame available for reassembler validation");

    SwVtpAv1Reassembler invalidSerialized;
    SwVtpAv1Reassembler::PushResult badSerialized =
        invalidSerialized.pushSerializedDatagram(makeBody(12, 0x44U), 2000);
    ok = expect(badSerialized.status == SwVtpAv1Reassembler::PushStatus::Invalid,
                "reassembler rejects malformed serialized datagram") && ok;

    SwVtpDatagram wrongMessage = result.datagrams[0];
    wrongMessage.header.messageType = SwVtpMessageType::Nack;
    SwVtpAv1Reassembler wrongMessageReassembler;
    ok = expect(wrongMessageReassembler.pushDatagram(wrongMessage, 2000).status ==
                    SwVtpAv1Reassembler::PushStatus::Invalid,
                "reassembler rejects non-frame message") && ok;

    SwVtpDatagram wrongIndex = result.datagrams[0];
    wrongIndex.header.fragmentIndex = wrongIndex.header.fragmentCount;
    SwVtpAv1Reassembler wrongIndexReassembler;
    ok = expect(wrongIndexReassembler.pushDatagram(wrongIndex, 2000).status ==
                    SwVtpAv1Reassembler::PushStatus::Invalid,
                "reassembler rejects out-of-range fragment index") && ok;

    SwVtpDatagram payloadMismatch = result.datagrams[0];
    payloadMismatch.payload = withoutLastByte(payloadMismatch.payload);
    SwVtpAv1Reassembler mismatchReassembler;
    ok = expect(mismatchReassembler.pushDatagram(payloadMismatch, 2000).status ==
                    SwVtpAv1Reassembler::PushStatus::Invalid,
                "reassembler rejects payload/header size mismatch") && ok;

    SwVtpDatagram stale = result.datagrams[0];
    SwVtpAv1Reassembler staleReassembler;
    ok = expect(staleReassembler.pushDatagram(stale, stale.header.deadlineUs + 1U).status ==
                    SwVtpAv1Reassembler::PushStatus::Stale,
                "reassembler rejects stale fragment") && ok;

    SwVtpAv1Reassembler duplicateReassembler;
    ok = expect(duplicateReassembler.pushDatagram(result.datagrams[0], 2000).status ==
                    SwVtpAv1Reassembler::PushStatus::Accepted,
                "reassembler accepts first incomplete fragment") && ok;
    ok = expect(duplicateReassembler.pushDatagram(result.datagrams[0], 3000).status ==
                    SwVtpAv1Reassembler::PushStatus::Duplicate,
                "reassembler detects duplicate incomplete fragment") && ok;
    ok = expect(duplicateReassembler.snapshot().duplicateFragments == 1U,
                "reassembler duplicate counter") && ok;

    SwVtpAv1Reassembler limitedReassembler;
    limitedReassembler.setLimits(64, 1);
    ok = expect(limitedReassembler.pushDatagram(result.datagrams[0], 2000).status ==
                    SwVtpAv1Reassembler::PushStatus::Invalid,
                "reassembler drops frames over max byte limit") && ok;
    ok = expect(limitedReassembler.snapshot().droppedFrames == 1U,
                "reassembler max byte drop counter") && ok;

    SwVtpAv1Reassembler conflictReassembler;
    ok = expect(conflictReassembler.pushDatagram(result.datagrams[0], 2000).status ==
                    SwVtpAv1Reassembler::PushStatus::Accepted,
                "reassembler accepts frame before conflict") && ok;
    SwVtpDatagram conflictingFragment = result.datagrams[1];
    conflictingFragment.header.fragmentCount =
        static_cast<uint16_t>(conflictingFragment.header.fragmentCount + 1U);
    ok = expect(conflictReassembler.pushDatagram(conflictingFragment, 3000).status ==
                    SwVtpAv1Reassembler::PushStatus::Invalid,
                "reassembler rejects inconsistent fragment count") && ok;
    ok = expect(conflictReassembler.snapshot().droppedFrames == 1U,
                "reassembler conflict drop counter") && ok;

    if (ok) {
        std::cout << "[SwVTP AV1 reassembler validation] PASS\n";
    }
    return ok;
}

bool runNackAndDeadlineScenario() {
    const SwVtpAv1PacketizerResult result = makePacketizedFrame();
    SwVtpAv1Reassembler reassembler;
    bool ok = expect(result.ok && result.serializedDatagrams.size() > 4U,
                     "packetized frame available for nack");

    reassembler.pushSerializedDatagram(result.serializedDatagrams[0], 2000);
    reassembler.pushSerializedDatagram(result.serializedDatagrams[2], 2000);

    SwVtpNackRequest request;
    ok = expect(reassembler.makeNackRequest(2, 1, 42, 3000, 1000, request),
                "early NACK request") && ok;
    ok = expect(request.missingFragments.size() ==
                    result.serializedDatagrams.size() - 2U,
                "NACK missing count") && ok;

    SwVtpNackRequest lateRequest;
    ok = expect(!reassembler.makeNackRequest(2, 1, 42, 90500, 1000, lateRequest),
                "late NACK suppressed by deadline") && ok;

    const std::size_t expired = reassembler.expire(92000);
    ok = expect(expired == 1U, "deadline expiration") && ok;
    const SwVtpAv1Reassembler::Snapshot snapshot = reassembler.snapshot();
    ok = expect(snapshot.droppedFrames == 1U, "dropped frame stat") && ok;

    SwVtpAv1Reassembler::PushResult stale =
        reassembler.pushSerializedDatagram(result.serializedDatagrams[3], 92000);
    ok = expect(stale.status == SwVtpAv1Reassembler::PushStatus::Stale,
                "stale fragment rejected") && ok;

    if (ok) {
        std::cout << "[SwVTP AV1 NACK/deadline] PASS\n";
    }
    return ok;
}

bool runControlPayloadValidationScenario() {
    SwVtpNackRequest parsedNack;
    SwVtpReceiverStats parsedStats;
    SwVtpClockSyncPing parsedPing;
    SwVtpClockSyncPong parsedPong;

    bool ok = expect(!swVtpParseNackPayload(1, 1, SwByteArray(), parsedNack),
                     "NACK parser rejects empty payload");

    SwVtpNackRequest zeroFrameNack;
    zeroFrameNack.streamId = 1;
    zeroFrameNack.trackId = 1;
    zeroFrameNack.frameId = 0;
    zeroFrameNack.missingFragments.append(1);
    ok = expect(!swVtpParseNackPayload(1,
                                       1,
                                       swVtpSerializeNack(zeroFrameNack),
                                       parsedNack),
                "NACK parser rejects frame id zero") && ok;

    SwByteArray truncatedNack;
    swVtpAppendU32(truncatedNack, 42);
    swVtpAppendU16(truncatedNack, 2);
    swVtpAppendU16(truncatedNack, 1);
    ok = expect(!swVtpParseNackPayload(1, 1, truncatedNack, parsedNack),
                "NACK parser rejects truncated fragment list") && ok;

    SwVtpReceiverStats stats;
    stats.streamId = 1;
    stats.trackId = 1;
    stats.lastFrameId = 8;
    const SwByteArray statsPayload = swVtpSerializeReceiverStats(stats);
    ok = expect(!swVtpParseReceiverStatsPayload(withoutLastByte(statsPayload),
                                                parsedStats),
                "receiver stats parser rejects truncated payload") && ok;

    SwVtpClockSyncPing zeroPing;
    zeroPing.syncId = 0;
    zeroPing.clientSendTimeUs = 1000;
    ok = expect(!swVtpParseClockSyncPing(swVtpSerializeClockSyncPing(zeroPing),
                                         parsedPing),
                "clock ping parser rejects sync id zero") && ok;
    ok = expect(!swVtpParseClockSyncPing(withoutLastByte(
                                             swVtpSerializeClockSyncPing(zeroPing)),
                                         parsedPing),
                "clock ping parser rejects truncated payload") && ok;

    SwVtpClockSyncPong invalidPong;
    invalidPong.syncId = 5;
    invalidPong.clientSendTimeUs = 1000;
    invalidPong.serverReceiveTimeUs = 3000;
    invalidPong.serverSendTimeUs = 2000;
    ok = expect(!swVtpParseClockSyncPong(swVtpSerializeClockSyncPong(invalidPong),
                                         parsedPong),
                "clock pong parser rejects negative server processing") && ok;
    ok = expect(!swVtpParseClockSyncPong(withoutLastByte(
                                             swVtpSerializeClockSyncPong(invalidPong)),
                                         parsedPong),
                "clock pong parser rejects truncated payload") && ok;

    if (ok) {
        std::cout << "[SwVTP control payload validation] PASS\n";
    }
    return ok;
}

bool runNackPayloadRoundTrip() {
    SwVtpNackRequest request;
    request.streamId = 2;
    request.trackId = 1;
    request.frameId = 42;
    request.missingFragments.append(1);
    request.missingFragments.append(7);
    request.missingFragments.append(8);

    const SwByteArray payload = swVtpSerializeNack(request);
    SwVtpNackRequest parsed;
    bool ok = expect(swVtpParseNackPayload(2, 1, payload, parsed), "parse NACK payload");
    ok = expect(parsed.frameId == 42, "NACK frame id") && ok;
    ok = expect(parsed.missingFragments.size() == 3U, "NACK count") && ok;
    ok = expect(parsed.missingFragments[0] == 1 &&
                    parsed.missingFragments[1] == 7 &&
                    parsed.missingFragments[2] == 8,
                "NACK fragment indexes") && ok;
    if (ok) {
        std::cout << "[SwVTP NACK payload] PASS\n";
    }
    return ok;
}

bool runReceiverStatsPayloadRoundTrip() {
    SwVtpReceiverStats stats;
    stats.streamId = 2;
    stats.trackId = 1;
    stats.lastFrameId = 42;
    stats.estimatedBandwidthKbps = 7500;
    stats.rttMs = 28;
    stats.jitterMs = 3;
    stats.lossPermille = 4;
    stats.nackPermille = 12;
    stats.receiveQueueMs = 8;
    stats.decodeQueueMs = 5;
    stats.renderQueueMs = 9;
    stats.transferLatencyMs = 18;
    stats.captureLatencyMs = 47;
    stats.clockUncertaintyMs = 4;
    stats.droppedFrames = 3;

    SwVtpReceiverStats parsed;
    bool ok = expect(swVtpParseReceiverStatsPayload(swVtpSerializeReceiverStats(stats),
                                                    parsed),
                     "parse receiver stats payload");
    ok = expect(parsed.streamId == stats.streamId, "receiver stats stream id") && ok;
    ok = expect(parsed.trackId == stats.trackId, "receiver stats track id") && ok;
    ok = expect(parsed.lastFrameId == stats.lastFrameId, "receiver stats frame id") && ok;
    ok = expect(parsed.estimatedBandwidthKbps == stats.estimatedBandwidthKbps,
                "receiver stats bandwidth") && ok;
    ok = expect(parsed.rttMs == stats.rttMs, "receiver stats RTT") && ok;
    ok = expect(parsed.jitterMs == stats.jitterMs, "receiver stats jitter") && ok;
    ok = expect(parsed.lossPermille == stats.lossPermille, "receiver stats loss") && ok;
    ok = expect(parsed.nackPermille == stats.nackPermille, "receiver stats nack") && ok;
    ok = expect(parsed.receiveQueueMs == stats.receiveQueueMs,
                "receiver stats receive queue") && ok;
    ok = expect(parsed.decodeQueueMs == stats.decodeQueueMs,
                "receiver stats decode queue") && ok;
    ok = expect(parsed.renderQueueMs == stats.renderQueueMs,
                "receiver stats render queue") && ok;
    ok = expect(parsed.transferLatencyMs == stats.transferLatencyMs,
                "receiver stats transfer latency") && ok;
    ok = expect(parsed.captureLatencyMs == stats.captureLatencyMs,
                "receiver stats capture latency") && ok;
    ok = expect(parsed.clockUncertaintyMs == stats.clockUncertaintyMs,
                "receiver stats clock uncertainty") && ok;
    ok = expect(parsed.droppedFrames == stats.droppedFrames,
                "receiver stats dropped frames") && ok;

    if (ok) {
        std::cout << "[SwVTP receiver stats payload] PASS\n";
    }
    return ok;
}

bool runBitrateControlPayloadRoundTrip() {
    SwVtpBitrateControl control;
    control.streamId = 2;
    control.trackId = 1;
    control.targetBitrateKbps = 4200;
    control.encoderBitrateKbps = 4000;
    control.estimatedBandwidthKbps = 6200;
    control.reason = static_cast<uint8_t>(SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure);
    control.flags = 1;

    SwVtpBitrateControl parsed;
    bool ok = expect(swVtpParseBitrateControlPayload(swVtpSerializeBitrateControl(control),
                                                     parsed),
                     "parse bitrate control payload");
    ok = expect(parsed.streamId == control.streamId, "bitrate control stream id") && ok;
    ok = expect(parsed.trackId == control.trackId, "bitrate control track id") && ok;
    ok = expect(parsed.targetBitrateKbps == control.targetBitrateKbps,
                "bitrate control target") && ok;
    ok = expect(parsed.encoderBitrateKbps == control.encoderBitrateKbps,
                "bitrate control encoder bitrate") && ok;
    ok = expect(parsed.estimatedBandwidthKbps == control.estimatedBandwidthKbps,
                "bitrate control bandwidth") && ok;
    ok = expect(parsed.reason == control.reason, "bitrate control reason") && ok;
    ok = expect(parsed.flags == control.flags, "bitrate control flags") && ok;

    SwByteArray invalid = swVtpSerializeBitrateControl(control);
    invalid[19] = 1;
    ok = expect(!swVtpParseBitrateControlPayload(invalid, parsed),
                "bitrate control rejects reserved bits") && ok;

    if (ok) {
        std::cout << "[SwVTP bitrate control payload] PASS\n";
    }
    return ok;
}

bool runLatencyValidationScenario() {
    SwVtpClockSyncSample invalidSync;
    invalidSync.syncId = 0;
    invalidSync.clientSendTimeUs = 1000;
    invalidSync.serverReceiveTimeUs = 2000;
    invalidSync.serverSendTimeUs = 2100;
    invalidSync.clientReceiveTimeUs = 3000;
    bool ok = expect(!swVtpEstimateClock(invalidSync).valid,
                     "clock estimate rejects sync id zero");

    SwVtpClockSyncSample reversedClient = invalidSync;
    reversedClient.syncId = 1;
    reversedClient.clientSendTimeUs = 3000;
    reversedClient.clientReceiveTimeUs = 2000;
    ok = expect(!swVtpEstimateClock(reversedClient).valid,
                "clock estimate rejects reversed client timestamps") && ok;

    SwVtpClockSyncSample reversedServer = invalidSync;
    reversedServer.syncId = 2;
    reversedServer.serverReceiveTimeUs = 3000;
    reversedServer.serverSendTimeUs = 2000;
    ok = expect(!swVtpEstimateClock(reversedServer).valid,
                "clock estimate rejects reversed server timestamps") && ok;

    SwVtpClockSyncSample impossibleProcessing = invalidSync;
    impossibleProcessing.syncId = 3;
    impossibleProcessing.clientSendTimeUs = 1000;
    impossibleProcessing.clientReceiveTimeUs = 1100;
    impossibleProcessing.serverReceiveTimeUs = 2000;
    impossibleProcessing.serverSendTimeUs = 2300;
    ok = expect(!swVtpEstimateClock(impossibleProcessing).valid,
                "clock estimate rejects server processing above RTT") && ok;

    SwVtpClockEstimate invalidEstimate;
    SwVtpHeader header;
    header.sendTimeUs = 1000;
    ok = expect(!swVtpMeasureFrameLatency(header, invalidEstimate, 2000).valid,
                "latency sample rejects invalid clock estimate") && ok;

    SwVtpClockEstimate validEstimate;
    validEstimate.valid = true;
    validEstimate.serverToClientOffsetUs = 1000;
    validEstimate.oneWayUncertaintyUs = 4000;
    validEstimate.confidencePercent = 96;
    header.sendTimeUs = 2000;
    header.captureTimeUs = 0;
    const SwVtpFrameLatencySample noCapture =
        swVtpMeasureFrameLatency(header, validEstimate, 3500);
    ok = expect(noCapture.valid, "latency sample accepts send timestamp") && ok;
    ok = expect(noCapture.transferLatencyUs == 500,
                "latency transfer uses corrected server send time") && ok;
    ok = expect(!noCapture.captureLatencyValid,
                "latency capture sample is invalid without capture timestamp") && ok;

    uint64_t converted = 0;
    ok = expect(swVtpServerTimeToClientTime(2500, validEstimate, converted) &&
                    converted == 3500,
                "server time conversion applies positive offset") && ok;

    SwVtpClockEstimate negativeOffset = validEstimate;
    negativeOffset.serverToClientOffsetUs = -1000;
    ok = expect(swVtpServerTimeToClientTime(2500, negativeOffset, converted) &&
                    converted == 1500,
                "server time conversion applies negative offset") && ok;
    ok = expect(!swVtpServerTimeToClientTime(500, negativeOffset, converted),
                "server time conversion rejects underflow") && ok;

    SwVtpClockEstimate overflowOffset = validEstimate;
    overflowOffset.serverToClientOffsetUs = std::numeric_limits<int64_t>::max();
    ok = expect(!swVtpServerTimeToClientTime(std::numeric_limits<uint64_t>::max(),
                                            overflowOffset,
                                            converted),
                "server time conversion rejects overflow") && ok;
    ok = expect(swVtpClockConfidencePercent(200000) == 10,
                "clock confidence keeps low-confidence floor") && ok;

    if (ok) {
        std::cout << "[SwVTP latency validation] PASS\n";
    }
    return ok;
}

bool runClockSyncPayloadRoundTrip() {
    SwVtpClockSyncPing ping;
    ping.syncId = 77;
    ping.clientSendTimeUs = 1000000;

    SwVtpClockSyncPing parsedPing;
    bool ok = expect(swVtpParseClockSyncPing(swVtpSerializeClockSyncPing(ping),
                                             parsedPing),
                     "parse clock sync ping");
    ok = expect(parsedPing.syncId == 77, "clock sync ping id") && ok;
    ok = expect(parsedPing.clientSendTimeUs == 1000000,
                "clock sync ping client send time") && ok;

    SwVtpClockSyncPong pong;
    pong.syncId = 77;
    pong.clientSendTimeUs = 1000000;
    pong.serverReceiveTimeUs = 20000;
    pong.serverSendTimeUs = 25000;

    SwVtpClockSyncPong parsedPong;
    ok = expect(swVtpParseClockSyncPong(swVtpSerializeClockSyncPong(pong),
                                        parsedPong),
                "parse clock sync pong") && ok;
    ok = expect(parsedPong.syncId == 77, "clock sync pong id") && ok;
    ok = expect(parsedPong.clientSendTimeUs == 1000000,
                "clock sync pong client send time") && ok;
    ok = expect(parsedPong.serverReceiveTimeUs == 20000,
                "clock sync pong server receive time") && ok;
    ok = expect(parsedPong.serverSendTimeUs == 25000,
                "clock sync pong server send time") && ok;

    if (ok) {
        std::cout << "[SwVTP clock sync payload] PASS\n";
    }
    return ok;
}

bool runLatencySymmetricClockScenario() {
    SwVtpClockSyncSample sync;
    sync.syncId = 1;
    sync.clientSendTimeUs = 1000000;
    sync.serverReceiveTimeUs = 20000;
    sync.serverSendTimeUs = 25000;
    sync.clientReceiveTimeUs = 1045000;

    const SwVtpClockEstimate estimate = swVtpEstimateClock(sync);
    bool ok = expect(estimate.valid, "symmetric clock estimate valid");
    ok = expect(estimate.serverToClientOffsetUs == 1000000,
                "symmetric clock offset exact") && ok;
    ok = expect(estimate.rttUs == 45000, "symmetric total RTT") && ok;
    ok = expect(estimate.serverProcessingUs == 5000,
                "symmetric server processing") && ok;
    ok = expect(estimate.networkRttUs == 40000, "symmetric network RTT") && ok;
    ok = expect(estimate.oneWayUncertaintyUs == 20000,
                "symmetric uncertainty") && ok;

    SwVtpHeader header;
    header.version = kSwVtpVersion1;
    header.messageType = SwVtpMessageType::FrameFragment;
    header.sendTimeUs = 50000;
    header.captureTimeUs = 30000;

    const SwVtpFrameLatencySample latency =
        swVtpMeasureFrameLatency(header, estimate, 1070000);
    ok = expect(latency.valid, "symmetric transfer latency valid") && ok;
    ok = expect(latency.transferLatencyUs == 20000,
                "symmetric transfer latency exact") && ok;
    ok = expect(latency.captureLatencyValid,
                "symmetric capture latency valid") && ok;
    ok = expect(latency.captureToReceiveUs == 40000,
                "symmetric capture latency exact") && ok;

    if (ok) {
        std::cout << "[SwVTP latency symmetric clocks] PASS\n";
    }
    return ok;
}

bool runLatencyUnsyncedAsymmetricClockScenario() {
    SwVtpClockSyncSample sync;
    sync.syncId = 2;
    sync.clientSendTimeUs = 1000000;
    sync.serverReceiveTimeUs = 20000;
    sync.serverSendTimeUs = 25000;
    sync.clientReceiveTimeUs = 1055000;

    const SwVtpClockEstimate estimate = swVtpEstimateClock(sync);
    bool ok = expect(estimate.valid, "asymmetric clock estimate valid");
    ok = expect(estimate.serverToClientOffsetUs == 1005000,
                "asymmetric clock offset midpoint") && ok;
    ok = expect(estimate.networkRttUs == 50000,
                "asymmetric network RTT") && ok;
    ok = expect(estimate.oneWayUncertaintyUs == 25000,
                "asymmetric uncertainty") && ok;
    ok = expect(estimate.confidencePercent > 0 && estimate.confidencePercent <= 100,
                "asymmetric confidence range") && ok;

    SwVtpHeader header;
    header.version = kSwVtpVersion1;
    header.messageType = SwVtpMessageType::FrameFragment;
    header.sendTimeUs = 40000;
    header.captureTimeUs = 10000;

    const SwVtpFrameLatencySample latency =
        swVtpMeasureFrameLatency(header, estimate, 1070000);
    ok = expect(latency.valid, "asymmetric transfer latency valid") && ok;
    ok = expect(latency.transferLatencyUs == 25000,
                "asymmetric estimated transfer latency") && ok;
    ok = expect(latency.captureLatencyValid,
                "asymmetric capture latency valid") && ok;
    ok = expect(latency.captureToReceiveUs == 55000,
                "asymmetric estimated capture latency") && ok;
    ok = expect(latency.oneWayUncertaintyUs == 25000,
                "asymmetric latency uncertainty propagated") && ok;

    if (ok) {
        std::cout << "[SwVTP latency unsynced asymmetric clocks] PASS\n";
    }
    return ok;
}

bool runAdaptiveBitrateScenario() {
    SwVtpAdaptiveBitratePolicy policy;
    policy.minBitrateKbps = 1000;
    policy.maxBitrateKbps = 10000;
    policy.startBitrateKbps = 6000;
    policy.upshiftCooldownMs = 1000;

    SwVtpAdaptiveBitrateController controller(policy);
    SwVtpReceiverStats stable;
    stable.estimatedBandwidthKbps = 12000;

    SwVtpAdaptiveBitrateDecision up =
        controller.update(stable, 2000);
    bool ok = expect(up.reason == SwVtpAdaptiveBitrateDecision::Reason::UpshiftProbe,
                     "ABR stable upshift") &&
              expect(up.targetBitrateKbps > policy.startBitrateKbps,
                     "ABR target increased");

    SwVtpReceiverStats damaged;
    damaged.estimatedBandwidthKbps = 9000;
    damaged.lossPermille = 100;
    damaged.rttMs = 140;
    SwVtpAdaptiveBitrateDecision down =
        controller.update(damaged, 2100);
    ok = expect(down.reason == SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure,
                "ABR network pressure") && ok;
    ok = expect(down.targetBitrateKbps < up.targetBitrateKbps,
                "ABR target decreased") && ok;
    ok = expect(down.requestKeyFrame, "ABR keyframe request on hard network damage") && ok;
    ok = expect(down.preferBaseTemporalLayer, "ABR temporal base preference") && ok;

    if (ok) {
        std::cout << "[SwVTP adaptive bitrate] PASS\n";
    }
    return ok;
}

bool runAdaptiveBandwidthEstimateScenario() {
    SwVtpAdaptiveBitratePolicy policy;
    policy.minBitrateKbps = 1000;
    policy.maxBitrateKbps = 10000;
    policy.startBitrateKbps = 8000;
    policy.bandwidthSafetyPercent = 90;
    policy.upshiftCooldownMs = 1000;

    SwVtpAdaptiveBitrateController controller(policy);
    SwVtpReceiverStats bandwidthDrop;
    bandwidthDrop.estimatedBandwidthKbps = 4000;

    SwVtpAdaptiveBitrateDecision down = controller.update(bandwidthDrop, 0);
    bool ok = expect(down.reason == SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure,
                     "ABR bandwidth estimate pressure reason");
    ok = expect(down.targetBitrateKbps == 3600,
                "ABR target clamped to safe bandwidth ceiling") && ok;
    ok = expect(!down.requestKeyFrame,
                "ABR bandwidth-only pressure does not force keyframe") && ok;
    ok = expect(down.preferBaseTemporalLayer,
                "ABR bandwidth pressure prefers base temporal layer") && ok;

    SwVtpReceiverStats recovered;
    recovered.estimatedBandwidthKbps = 12000;
    SwVtpAdaptiveBitrateDecision cooldown = controller.update(recovered, 500);
    ok = expect(cooldown.reason == SwVtpAdaptiveBitrateDecision::Reason::Stable,
                "ABR cooldown blocks immediate upshift") && ok;
    ok = expect(cooldown.targetBitrateKbps == 3600,
                "ABR target held during cooldown") && ok;

    SwVtpAdaptiveBitrateDecision up = controller.update(recovered, 1200);
    ok = expect(up.reason == SwVtpAdaptiveBitrateDecision::Reason::UpshiftProbe,
                "ABR upshifts after pressure cooldown") && ok;
    ok = expect(up.targetBitrateKbps == 3960,
                "ABR upshift is gradual") && ok;

    if (ok) {
        std::cout << "[SwVTP adaptive bandwidth estimate] PASS\n";
    }
    return ok;
}

bool runAdaptiveBandwidthHeadroomScenario() {
    SwVtpAdaptiveBitratePolicy policy;
    policy.minBitrateKbps = 1000;
    policy.maxBitrateKbps = 12000;
    policy.startBitrateKbps = 8000;
    policy.upshiftPercent = 110;
    policy.upshiftBandwidthHeadroomPercent = 85;

    SwVtpAdaptiveBitrateController controller(policy);
    SwVtpReceiverStats limitedHeadroom;
    limitedHeadroom.estimatedBandwidthKbps = 9000;

    SwVtpAdaptiveBitrateDecision hold = controller.update(limitedHeadroom, 5000);
    bool ok = expect(hold.reason == SwVtpAdaptiveBitrateDecision::Reason::Stable,
                     "ABR does not upshift without bandwidth headroom");
    ok = expect(hold.targetBitrateKbps == 8000,
                "ABR keeps target when below ceiling but headroom is insufficient") && ok;

    SwVtpReceiverStats wideHeadroom;
    wideHeadroom.estimatedBandwidthKbps = 11000;
    SwVtpAdaptiveBitrateDecision up = controller.update(wideHeadroom, 9000);
    ok = expect(up.reason == SwVtpAdaptiveBitrateDecision::Reason::UpshiftProbe,
                "ABR upshifts with enough bandwidth headroom") && ok;
    ok = expect(up.targetBitrateKbps == 8800,
                "ABR uses configured upshift percent") && ok;

    if (ok) {
        std::cout << "[SwVTP adaptive bandwidth headroom] PASS\n";
    }
    return ok;
}

bool runAdaptiveQueueAndDecoderPressureScenario() {
    SwVtpAdaptiveBitratePolicy policy;
    policy.minBitrateKbps = 1000;
    policy.maxBitrateKbps = 10000;
    policy.startBitrateKbps = 6000;
    policy.hardQueueMs = 80;
    policy.fastDownshiftPercent = 65;

    SwVtpAdaptiveBitrateController queueController(policy);
    SwVtpReceiverStats renderBacklog;
    renderBacklog.estimatedBandwidthKbps = 20000;
    renderBacklog.renderQueueMs = 100;
    SwVtpAdaptiveBitrateDecision queueDown =
        queueController.update(renderBacklog, 1000);
    bool ok = expect(queueDown.reason ==
                         SwVtpAdaptiveBitrateDecision::Reason::ClientQueuePressure,
                     "ABR render queue pressure reason");
    ok = expect(queueDown.targetBitrateKbps == 3900,
                "ABR render queue fast downshift") && ok;
    ok = expect(!queueDown.requestKeyFrame,
                "ABR render queue pressure does not force keyframe") && ok;
    ok = expect(queueDown.preferBaseTemporalLayer,
                "ABR render queue prefers base temporal layer") && ok;

    SwVtpAdaptiveBitrateController decoderController(policy);
    SwVtpReceiverStats decoderBacklog;
    decoderBacklog.estimatedBandwidthKbps = 20000;
    decoderBacklog.decodeQueueMs = 100;
    SwVtpAdaptiveBitrateDecision decoderDown =
        decoderController.update(decoderBacklog, 1000);
    ok = expect(decoderDown.reason ==
                    SwVtpAdaptiveBitrateDecision::Reason::DecoderPressure,
                "ABR decoder pressure reason") && ok;
    ok = expect(decoderDown.targetBitrateKbps == 3900,
                "ABR decoder fast downshift") && ok;
    ok = expect(decoderDown.requestKeyFrame,
                "ABR decoder pressure requests keyframe") && ok;

    if (ok) {
        std::cout << "[SwVTP adaptive queue/decoder pressure] PASS\n";
    }
    return ok;
}

bool runAdaptiveMinMaxClampScenario() {
    SwVtpAdaptiveBitratePolicy policy;
    policy.minBitrateKbps = 1200;
    policy.maxBitrateKbps = 5000;
    policy.startBitrateKbps = 8000;
    policy.bandwidthSafetyPercent = 90;

    SwVtpAdaptiveBitrateController controller(policy);
    bool ok = expect(controller.targetBitrateKbps() == 5000,
                     "ABR start target clamped to max");

    SwVtpReceiverStats lowBandwidth;
    lowBandwidth.estimatedBandwidthKbps = 500;
    SwVtpAdaptiveBitrateDecision minClamp = controller.update(lowBandwidth, 1000);
    ok = expect(minClamp.reason == SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure,
                "ABR low bandwidth reason") && ok;
    ok = expect(minClamp.targetBitrateKbps == 1200,
                "ABR low bandwidth clamped to min") && ok;

    if (ok) {
        std::cout << "[SwVTP adaptive min/max clamp] PASS\n";
    }
    return ok;
}

bool runIvfAv1StreamValidation(const char* path) {
    std::vector<IvfFrame> frames;
    std::string error;
    bool ok = expect(loadIvfAv1Frames(path, frames, error),
                     "load real AV1 IVF stream");
    if (!ok) {
        std::cerr << "[SwVtpAv1SelfTest] IVF error: " << error << "\n";
        return false;
    }

    SwVtpAv1Reassembler reassembler;
    std::size_t totalBytes = 0;
    std::size_t totalDatagrams = 0;
    std::size_t framesWithParsedObus = 0;

    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const IvfFrame& frame = frames[frameIndex];
        const SwList<SwVtpAv1Obu> obus = SwVtpAv1Parser::parseObus(frame.payload);
        if (!obus.isEmpty()) {
            ++framesWithParsedObus;
        }

        SwVideoPacket packet(SwVideoPacket::Codec::AV1,
                             frame.payload,
                             static_cast<std::int64_t>(frame.timestamp),
                             static_cast<std::int64_t>(frame.timestamp),
                             frameIndex == 0U);
        SwVtpAv1PacketizerOptions options;
        options.streamId = 11;
        options.trackId = 1;
        options.frameId = static_cast<uint32_t>(1000U + frameIndex);
        options.nowUs = 5000000ULL + static_cast<uint64_t>(frameIndex) * 33333ULL;
        options.captureTimeUs = options.nowUs - 4000ULL;
        options.latencyBudgetUs = 90000;
        options.maxDatagramBytes = 256;

        const SwVtpAv1PacketizerResult packetized =
            SwVtpAv1Packetizer::packetize(packet, options);
        ok = expect(packetized.ok, "real AV1 frame packetized") && ok;
        ok = expect(!packetized.serializedDatagrams.isEmpty(),
                    "real AV1 frame produced datagrams") && ok;

        for (std::size_t i = 0; i < packetized.serializedDatagrams.size(); ++i) {
            ok = expect(packetized.serializedDatagrams[i].size() <= options.maxDatagramBytes,
                        "real AV1 datagram stays under MTU") && ok;
        }

        bool completed = false;
        SwVideoPacket completedPacket;
        for (std::size_t i = packetized.serializedDatagrams.size(); i > 0U; --i) {
            const std::size_t datagramIndex = i - 1U;
            SwVtpAv1Reassembler::PushResult push =
                reassembler.pushSerializedDatagram(packetized.serializedDatagrams[datagramIndex],
                                                   options.nowUs + 1000ULL);
            if (push.completed()) {
                completed = true;
                completedPacket = push.packet;
            }
        }

        ok = expect(completed, "real AV1 frame reassembled") && ok;
        ok = expect(completedPacket.codec() == SwVideoPacket::Codec::AV1,
                    "real AV1 reassembled codec") && ok;
        ok = expect(completedPacket.payload() == frame.payload,
                    "real AV1 payload survives SwVTP bit-exact") && ok;
        ok = expect(completedPacket.pts() == static_cast<std::int64_t>(frame.timestamp),
                    "real AV1 timestamp survives SwVTP") && ok;

        totalBytes += frame.payload.size();
        totalDatagrams += packetized.serializedDatagrams.size();
    }

    ok = expect(framesWithParsedObus > 0U,
                "real AV1 stream exposes parseable OBU payloads") && ok;
    ok = expect(reassembler.snapshot().completedFrames == frames.size(),
                "real AV1 stream completed every frame") && ok;

    if (ok) {
        std::cout << "[SwVTP real AV1 IVF stream] PASS frames=" << frames.size()
                  << " bytes=" << totalBytes
                  << " datagrams=" << totalDatagrams << "\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    bool ok = true;
    ok = runHeaderRoundTrip() && ok;
    ok = runProtocolValidationScenario() && ok;
    ok = runDeliveryModeScenario() && ok;
    ok = runPacketizerScenario() && ok;
    ok = runPacketizerValidationScenario() && ok;
    ok = runReassemblerScenario() && ok;
    ok = runReassemblerValidationScenario() && ok;
    ok = runNackAndDeadlineScenario() && ok;
    ok = runControlPayloadValidationScenario() && ok;
    ok = runNackPayloadRoundTrip() && ok;
    ok = runReceiverStatsPayloadRoundTrip() && ok;
    ok = runBitrateControlPayloadRoundTrip() && ok;
    ok = runLatencyValidationScenario() && ok;
    ok = runClockSyncPayloadRoundTrip() && ok;
    ok = runLatencySymmetricClockScenario() && ok;
    ok = runLatencyUnsyncedAsymmetricClockScenario() && ok;
    ok = runAdaptiveBitrateScenario() && ok;
    ok = runAdaptiveBandwidthEstimateScenario() && ok;
    ok = runAdaptiveBandwidthHeadroomScenario() && ok;
    ok = runAdaptiveQueueAndDecoderPressureScenario() && ok;
    ok = runAdaptiveMinMaxClampScenario() && ok;
    if (argc > 1) {
        ok = runIvfAv1StreamValidation(argv[1]) && ok;
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
