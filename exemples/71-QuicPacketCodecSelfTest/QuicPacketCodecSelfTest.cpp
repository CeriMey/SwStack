#include "core/io/quic/SwQuicAckTracker.h"
#include "core/io/quic/SwQuicClientHelloBuilder.h"
#include "core/io/quic/SwQuicClientInitialBuilder.h"
#include "core/io/quic/SwQuicConnection.h"
#include "core/io/quic/SwQuicConnectionId.h"
#include "core/io/quic/SwQuicFrameCodec.h"
#include "core/io/quic/SwQuicInitialSecrets.h"
#include "core/io/quic/SwQuicPacketCodec.h"
#include "core/io/quic/SwQuicPacketProtector.h"
#include "core/io/quic/SwQuicServer.h"
#include "core/io/quic/SwQuicStream.h"
#include "core/io/quic/SwQuicStreamMap.h"
#include "core/io/quic/SwQuicVarIntCodec.h"
#include "core/types/SwVector.h"

#include <cstdint>
#include <iostream>

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool checkVarIntRoundTrip(std::uint64_t value, std::size_t expectedSize) {
    SwByteArray encoded;
    SwString error;
    if (!requireTrue(SwQuicVarIntCodec::encode(value, encoded, &error),
                     "varint encode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    if (!requireTrue(encoded.size() == expectedSize, "unexpected varint encoded size")) {
        return false;
    }

    std::size_t offset = 0;
    std::uint64_t decoded = 0;
    if (!requireTrue(SwQuicVarIntCodec::decode(encoded, offset, decoded, &error),
                     "varint decode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(decoded == value, "varint value mismatch") &&
           requireTrue(offset == encoded.size(), "varint offset mismatch");
}

bool testVarIntCodec() {
    return checkVarIntRoundTrip(0, 1) &&
           checkVarIntRoundTrip(63, 1) &&
           checkVarIntRoundTrip(64, 2) &&
           checkVarIntRoundTrip(15293, 2) &&
           checkVarIntRoundTrip(16384, 4) &&
           checkVarIntRoundTrip(0x3fffffffULL, 4) &&
           checkVarIntRoundTrip(0x40000000ULL, 8) &&
           checkVarIntRoundTrip(SwQuicVarIntCodec::maxValue(), 8);
}

bool testConnectionIdValidation() {
    SwString error;
    SwByteArray maxId(20, 'a');
    SwQuicConnectionId id;
    if (!requireTrue(SwQuicConnectionId::fromBytes(maxId, id, &error),
                     "20-byte connection ID should be accepted")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray oversized(21, 'b');
    return requireTrue(!SwQuicConnectionId::fromBytes(oversized, id, &error),
                       "21-byte connection ID should be rejected");
}

bool testInitialPacketRoundTrip() {
    SwString error;
    SwQuicConnectionId dcid;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("client01"), dcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("server01"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(dcid, scid);
    header.setVersion(1);
    header.setToken(SwByteArray("tok"));
    if (!header.setPacketNumberLength(2, &error) ||
        !header.setPacketNumber(0x1234, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray payload("hello-quic");
    SwByteArray encoded;
    if (!requireTrue(SwQuicPacketCodec::encodeInitialPacket(header, payload, encoded, &error),
                     "Initial packet encode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    if (!requireTrue(encoded.size() > payload.size(), "Initial packet did not add a header")) {
        return false;
    }
    if (!requireTrue(static_cast<unsigned char>(encoded[0]) == 0xc1U,
                     "Initial packet first byte mismatch")) {
        return false;
    }

    SwQuicPacketHeader decodedHeader;
    SwByteArray decodedPayload;
    if (!requireTrue(SwQuicPacketCodec::decodeInitialPacket(encoded,
                                                            decodedHeader,
                                                            decodedPayload,
                                                            &error),
                     "Initial packet decode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(decodedHeader.form() == SwQuicPacketHeader::Form::Long,
                       "decoded form mismatch") &&
           requireTrue(decodedHeader.longPacketType() == SwQuicPacketHeader::LongPacketType::Initial,
                       "decoded packet type mismatch") &&
           requireTrue(decodedHeader.version() == 1, "decoded version mismatch") &&
           requireTrue(decodedHeader.destinationConnectionId() == dcid,
                       "decoded destination connection ID mismatch") &&
           requireTrue(decodedHeader.sourceConnectionId() == scid,
                       "decoded source connection ID mismatch") &&
           requireTrue(decodedHeader.token() == SwByteArray("tok"), "decoded token mismatch") &&
           requireTrue(decodedHeader.packetNumberLength() == 2,
                       "decoded packet number length mismatch") &&
           requireTrue(decodedHeader.packetNumber() == 0x1234,
                       "decoded packet number mismatch") &&
           requireTrue(decodedHeader.payloadLength() == payload.size(),
                       "decoded payload length mismatch") &&
           requireTrue(decodedPayload == payload, "decoded payload mismatch");
}

bool testFrameRoundTrip() {
    SwVector<SwQuicFrame::AckRange> ackRanges;
    SwQuicFrame::AckRange range = {1, 3};
    ackRanges.push_back(range);

    SwVector<SwQuicFrame> frames;
    frames.push_back(SwQuicFrame::ping());
    frames.push_back(SwQuicFrame::ack(12, 4, 2, ackRanges));
    frames.push_back(SwQuicFrame::crypto(0, SwByteArray("client-hello")));
    frames.push_back(SwQuicFrame::stream(0, 5, SwByteArray("GET /"), true));
    frames.push_back(SwQuicFrame::datagram(SwByteArray("probe")));
    frames.push_back(SwQuicFrame::connectionClose(0x10, 0x06, SwString("done")));

    SwString error;
    SwByteArray payload;
    if (!requireTrue(SwQuicFrameCodec::encodeFrames(frames, payload, &error),
                     "frame encode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> decoded;
    if (!requireTrue(SwQuicFrameCodec::decodeFrames(payload, decoded, &error),
                     "frame decode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(decoded.size() == frames.size(), "decoded frame count mismatch") &&
           requireTrue(decoded[0].type() == SwQuicFrame::Type::Ping, "decoded ping mismatch") &&
           requireTrue(decoded[1].type() == SwQuicFrame::Type::Ack, "decoded ack type mismatch") &&
           requireTrue(decoded[1].largestAcknowledged() == 12, "decoded ack largest mismatch") &&
           requireTrue(decoded[1].ackDelay() == 4, "decoded ack delay mismatch") &&
           requireTrue(decoded[1].firstAckRange() == 2, "decoded ack first range mismatch") &&
           requireTrue(decoded[1].ackRanges().size() == 1, "decoded ack range count mismatch") &&
           requireTrue(decoded[1].ackRanges()[0].gap == 1, "decoded ack gap mismatch") &&
           requireTrue(decoded[1].ackRanges()[0].rangeLength == 3,
                       "decoded ack range length mismatch") &&
           requireTrue(decoded[2].type() == SwQuicFrame::Type::Crypto,
                       "decoded crypto type mismatch") &&
           requireTrue(decoded[2].offset() == 0, "decoded crypto offset mismatch") &&
           requireTrue(decoded[2].data() == SwByteArray("client-hello"),
                       "decoded crypto data mismatch") &&
           requireTrue(decoded[3].type() == SwQuicFrame::Type::Stream,
                       "decoded stream type mismatch") &&
           requireTrue(decoded[3].streamId() == 0, "decoded stream id mismatch") &&
           requireTrue(decoded[3].offset() == 5, "decoded stream offset mismatch") &&
           requireTrue(decoded[3].data() == SwByteArray("GET /"),
                       "decoded stream data mismatch") &&
           requireTrue(decoded[3].fin(), "decoded stream fin mismatch") &&
           requireTrue(decoded[4].type() == SwQuicFrame::Type::Datagram,
                       "decoded datagram type mismatch") &&
           requireTrue(decoded[4].data() == SwByteArray("probe"),
                       "decoded datagram data mismatch") &&
           requireTrue(decoded[5].type() == SwQuicFrame::Type::ConnectionClose,
                       "decoded close type mismatch") &&
           requireTrue(decoded[5].errorCode() == 0x10, "decoded close error mismatch") &&
           requireTrue(decoded[5].errorFrameType() == 0x06,
                       "decoded close frame type mismatch") &&
           requireTrue(decoded[5].reasonPhrase() == SwString("done"),
                       "decoded close reason mismatch");
}

bool testInitialPacketWithFramesRoundTrip() {
    SwString error;
    SwQuicConnectionId dcid;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("client02"), dcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("server02"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> frames;
    frames.push_back(SwQuicFrame::crypto(0, SwByteArray("tls-handshake-bytes")));
    frames.push_back(SwQuicFrame::ping());

    SwByteArray payload;
    if (!SwQuicFrameCodec::encodeFrames(frames, payload, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(dcid, scid);
    if (!header.setPacketNumberLength(1, &error) ||
        !header.setPacketNumber(7, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray packet;
    if (!requireTrue(SwQuicPacketCodec::encodeInitialPacket(header, payload, packet, &error),
                     "Initial packet with frames encode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicPacketHeader decodedHeader;
    SwByteArray decodedPayload;
    if (!requireTrue(SwQuicPacketCodec::decodeInitialPacket(packet,
                                                            decodedHeader,
                                                            decodedPayload,
                                                            &error),
                     "Initial packet with frames decode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> decodedFrames;
    if (!requireTrue(SwQuicFrameCodec::decodeFrames(decodedPayload, decodedFrames, &error),
                     "Initial packet decoded frames failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(decodedHeader.packetNumber() == 7,
                       "decoded packet number with frames mismatch") &&
           requireTrue(decodedFrames.size() == 2, "decoded packet frame count mismatch") &&
           requireTrue(decodedFrames[0].type() == SwQuicFrame::Type::Crypto,
                       "decoded packet crypto type mismatch") &&
           requireTrue(decodedFrames[0].data() == SwByteArray("tls-handshake-bytes"),
                       "decoded packet crypto data mismatch") &&
           requireTrue(decodedFrames[1].type() == SwQuicFrame::Type::Ping,
                       "decoded packet ping mismatch");
}

bool testAckTrackerBuildsRanges() {
    SwQuicAckTracker tracker;
    tracker.recordReceivedPacket(12);
    tracker.recordReceivedPacket(11);
    tracker.recordReceivedPacket(10);
    tracker.recordReceivedPacket(7);
    tracker.recordReceivedPacket(6);

    SwString error;
    SwQuicFrame ack = SwQuicFrame::ping();
    if (!requireTrue(tracker.buildAckFrame(ack, 5, &error), "ACK frame build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(ack.type() == SwQuicFrame::Type::Ack, "ACK tracker type mismatch") &&
           requireTrue(ack.largestAcknowledged() == 12, "ACK tracker largest mismatch") &&
           requireTrue(ack.ackDelay() == 5, "ACK tracker delay mismatch") &&
           requireTrue(ack.firstAckRange() == 2, "ACK tracker first range mismatch") &&
           requireTrue(ack.ackRanges().size() == 1, "ACK tracker range count mismatch") &&
           requireTrue(ack.ackRanges()[0].gap == 1, "ACK tracker gap mismatch") &&
           requireTrue(ack.ackRanges()[0].rangeLength == 1,
                       "ACK tracker range length mismatch");
}

bool testStreamReassembly() {
    SwString error;
    SwQuicStream stream(4);

    if (!requireTrue(stream.receiveFrame(SwQuicFrame::stream(4, 6, SwByteArray("world"), true),
                                         &error),
                     "stream receive tail failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(!stream.hasReadableData(), "stream should wait for offset zero")) {
        return false;
    }

    if (!requireTrue(stream.receiveFrame(SwQuicFrame::stream(4, 0, SwByteArray("hello "), false),
                                         &error),
                     "stream receive head failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray firstRead = stream.readContiguous();
    if (!requireTrue(firstRead == SwByteArray("hello world"),
                     "stream first read mismatch")) {
        return false;
    }
    if (!requireTrue(stream.isReceiveComplete(), "stream should be complete after FIN read")) {
        return false;
    }

    if (!requireTrue(stream.receiveFrame(SwQuicFrame::stream(4, 3, SwByteArray("lo "), false),
                                         &error),
                     "stream duplicate receive failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(stream.readContiguous().size() == 0,
                     "stream duplicate data should not be readable")) {
        return false;
    }

    SwQuicStream other(8);
    return requireTrue(!other.receiveFrame(SwQuicFrame::stream(9, 0, SwByteArray("x"), false),
                                           &error),
                       "stream should reject another stream id");
}

bool testStreamMapMultiplexing() {
    SwString error;
    SwQuicStreamMap streams;

    if (!requireTrue(streams.receiveFrame(SwQuicFrame::stream(8, 4, SwByteArray("-b"), true),
                                          &error),
                     "stream map receive stream 8 tail failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(streams.receiveFrame(SwQuicFrame::stream(4, 0, SwByteArray("alpha"), true),
                                          &error),
                     "stream map receive stream 4 failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(streams.receiveFrame(SwQuicFrame::stream(8, 0, SwByteArray("beta"), false),
                                          &error),
                     "stream map receive stream 8 head failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    const SwByteArray stream4 = streams.readContiguous(4);
    const SwByteArray stream8 = streams.readContiguous(8);
    const SwQuicStream* s4 = streams.stream(4);
    const SwQuicStream* s8 = streams.stream(8);

    return requireTrue(streams.streamCount() == 2, "stream map count mismatch") &&
           requireTrue(streams.hasStream(4), "stream map missing stream 4") &&
           requireTrue(streams.hasStream(8), "stream map missing stream 8") &&
           requireTrue(stream4 == SwByteArray("alpha"), "stream map stream 4 data mismatch") &&
           requireTrue(stream8 == SwByteArray("beta-b"), "stream map stream 8 data mismatch") &&
           requireTrue(s4 && s4->isReceiveComplete(), "stream map stream 4 completion mismatch") &&
           requireTrue(s8 && s8->isReceiveComplete(), "stream map stream 8 completion mismatch");
}

bool testConnectionReceivesInitialPacket() {
    SwString error;
    SwQuicConnectionId dcid;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("client03"), dcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("server03"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> frames;
    frames.push_back(SwQuicFrame::crypto(0, SwByteArray("crypto")));
    frames.push_back(SwQuicFrame::ping());
    frames.push_back(SwQuicFrame::stream(0, 0, SwByteArray("hello"), true));
    frames.push_back(SwQuicFrame::datagram(SwByteArray("dgram")));

    SwByteArray payload;
    if (!SwQuicFrameCodec::encodeFrames(frames, payload, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(dcid, scid);
    if (!header.setPacketNumberLength(1, &error) ||
        !header.setPacketNumber(9, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray packet;
    if (!SwQuicPacketCodec::encodeInitialPacket(header, payload, packet, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicConnection connection;
    if (!requireTrue(connection.receiveInitialPacket(packet, &error),
                     "connection receive Initial packet failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicFrame ack = SwQuicFrame::ping();
    if (!requireTrue(connection.buildAckFrame(ack, 0, &error),
                     "connection ACK frame build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(connection.state() == SwQuicConnection::State::Open,
                       "connection state mismatch") &&
           requireTrue(connection.lastInitialHeader().packetNumber() == 9,
                       "connection last packet number mismatch") &&
           requireTrue(connection.receivedPingCount() == 1,
                       "connection ping count mismatch") &&
           requireTrue(connection.cryptoData() == SwByteArray("crypto"),
                       "connection crypto data mismatch") &&
           requireTrue(connection.streams().hasStream(0), "connection stream missing") &&
           requireTrue(connection.readStream(0) == SwByteArray("hello"),
                       "connection stream data mismatch") &&
           requireTrue(connection.pendingDatagramCount() == 1,
                       "connection datagram count mismatch") &&
           requireTrue(connection.takeDatagram() == SwByteArray("dgram"),
                       "connection datagram data mismatch") &&
           requireTrue(connection.pendingDatagramCount() == 0,
                       "connection datagram drain mismatch") &&
           requireTrue(ack.type() == SwQuicFrame::Type::Ack,
                       "connection ACK type mismatch") &&
           requireTrue(ack.largestAcknowledged() == 9,
                       "connection ACK largest mismatch");
}

bool testUdpLoopbackServerReceivesInitialPacket() {
    SwString error;
    SwQuicServer server;
    SwQuicServer client;

    if (!requireTrue(server.listen(SwString("127.0.0.1"), 0, &error),
                     "loopback server listen failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(client.listen(SwString("127.0.0.1"), 0, &error),
                     "loopback client listen failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicConnectionId dcid;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("udpclient"), dcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("udpserver"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(dcid, scid);
    if (!header.setPacketNumberLength(1, &error) ||
        !header.setPacketNumber(3, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> frames;
    frames.push_back(SwQuicFrame::crypto(0, SwByteArray("udp-crypto")));
    frames.push_back(SwQuicFrame::stream(0, 0, SwByteArray("udp-stream"), true));
    frames.push_back(SwQuicFrame::datagram(SwByteArray("udp-datagram")));

    if (!requireTrue(client.sendInitialPacket(SwString("127.0.0.1"),
                                              server.localPort(),
                                              header,
                                              frames,
                                              &error),
                     "loopback client send failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    int processed = 0;
    for (int attempt = 0; attempt < 50 && processed == 0; ++attempt) {
        processed = server.poll(10, &error);
        if (processed < 0) {
            std::cerr << error << std::endl;
            return false;
        }
    }

    SwQuicConnection* connection = server.connection(SwString("127.0.0.1"), client.localPort());
    if (!requireTrue(processed == 1, "loopback server did not process packet") ||
        !requireTrue(connection != nullptr, "loopback connection missing")) {
        return false;
    }

    SwQuicFrame ack = SwQuicFrame::ping();
    if (!requireTrue(connection->buildAckFrame(ack, 0, &error),
                     "loopback ACK build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(server.connectionCount() == 1, "loopback connection count mismatch") &&
           requireTrue(connection->lastInitialHeader().packetNumber() == 3,
                       "loopback packet number mismatch") &&
           requireTrue(connection->cryptoData() == SwByteArray("udp-crypto"),
                       "loopback crypto data mismatch") &&
           requireTrue(connection->readStream(0) == SwByteArray("udp-stream"),
                       "loopback stream data mismatch") &&
           requireTrue(connection->takeDatagram() == SwByteArray("udp-datagram"),
                       "loopback datagram data mismatch") &&
           requireTrue(ack.type() == SwQuicFrame::Type::Ack,
                       "loopback ACK type mismatch") &&
           requireTrue(ack.largestAcknowledged() == 3,
                       "loopback ACK largest mismatch");
}

bool testInitialSecretDerivationMatchesRfc9001() {
    SwString error;
    SwQuicConnectionId dcid;
    if (!SwQuicConnectionId::fromBytes(
            SwByteArray::fromHex(SwByteArray("8394c8f03e515708")),
            dcid,
            &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!requireTrue(SwQuicInitialSecrets::deriveV1(dcid, clientKeys, serverKeys, &error),
                     "initial secret derivation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(clientKeys.secret.toHex() ==
                           SwByteArray("c00cf151ca5be075ed0ebfb5c80323c4"
                                       "2d6b7db67881289af4008f1f6c357aea"),
                       "client initial secret mismatch") &&
           requireTrue(clientKeys.key.toHex() ==
                           SwByteArray("1f369613dd76d5467730efcbe3b1a22d"),
                       "client initial key mismatch") &&
           requireTrue(clientKeys.iv.toHex() ==
                           SwByteArray("fa044b2f42a3fd3b46fb255c"),
                       "client initial iv mismatch") &&
           requireTrue(clientKeys.headerProtectionKey.toHex() ==
                           SwByteArray("9f50449e04a0e810283a1e9933adedd2"),
                       "client initial hp mismatch") &&
           requireTrue(serverKeys.secret.toHex() ==
                           SwByteArray("3c199828fd139efd216c155ad844cc81"
                                       "fb82fa8d7446fa7d78be803acdda951b"),
                       "server initial secret mismatch") &&
           requireTrue(serverKeys.key.toHex() ==
                           SwByteArray("cf3a5331653c364c88f0f379b6067e37"),
                       "server initial key mismatch") &&
           requireTrue(serverKeys.iv.toHex() ==
                           SwByteArray("0ac1493ca1905853b0bba03e"),
                       "server initial iv mismatch") &&
           requireTrue(serverKeys.headerProtectionKey.toHex() ==
                           SwByteArray("c206b8d9b9f0f37644430b490eeaa314"),
                       "server initial hp mismatch");
}

bool testInitialPacketProtectionMatchesRfc9001() {
    SwString error;
    SwQuicConnectionId dcid;
    if (!SwQuicConnectionId::fromBytes(
            SwByteArray::fromHex(SwByteArray("8394c8f03e515708")),
            dcid,
            &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!requireTrue(SwQuicInitialSecrets::deriveV1(dcid, clientKeys, serverKeys, &error),
                     "initial secret derivation failed for protection test")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray plaintext = SwByteArray::fromHex(SwByteArray(
        "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e868"
        "04fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578"
        "616d706c652e636f6dff01000100000a00080006001d00170018001000070005"
        "04616c706e000500050100000000003300260024001d00209370b2c9caa47fba"
        "baf4559fedba753de171fa71f50f1ce15d43e994ec74d748002b000302030400"
        "0d0010000e0403050306030203080408050806002d00020101001c0002400100"
        "3900320408ffffffffffffffff05048000ffff07048000ffff08011001048000"
        "75300901100f088394c8f03e51570806048000ffff"));
    if (!requireTrue(plaintext.size() <= 1162, "RFC 9001 plaintext is too large")) {
        return false;
    }
    plaintext.append(SwByteArray(1162 - plaintext.size(), '\0'));

    const SwByteArray header = SwByteArray::fromHex(
        SwByteArray("c300000001088394c8f03e5157080000449e00000002"));

    SwByteArray protectedPacket;
    if (!requireTrue(SwQuicPacketProtector::protectClientInitial(clientKeys,
                                                                 2,
                                                                 4,
                                                                 header,
                                                                 plaintext,
                                                                 protectedPacket,
                                                                 &error),
                     "RFC 9001 client Initial protection failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    const SwByteArray expectedPrefix = SwByteArray::fromHex(SwByteArray(
        "c000000001088394c8f03e5157080000449e7b9aec34"
        "d1b1c98dd7689fb8ec11d242b123dc9b"));

    return requireTrue(protectedPacket.size() == 1200,
                       "RFC 9001 protected packet size mismatch") &&
           requireTrue(protectedPacket.mid(0, static_cast<int>(expectedPrefix.size())) ==
                           expectedPrefix,
                       "RFC 9001 protected packet prefix mismatch");
}

bool testServerInitialUnprotectionMatchesRfc9001() {
    SwString error;
    SwQuicConnectionId dcid;
    if (!SwQuicConnectionId::fromBytes(
            SwByteArray::fromHex(SwByteArray("8394c8f03e515708")),
            dcid,
            &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!requireTrue(SwQuicInitialSecrets::deriveV1(dcid, clientKeys, serverKeys, &error),
                     "initial secret derivation failed for server unprotection test")) {
        std::cerr << error << std::endl;
        return false;
    }

    const SwByteArray packet = SwByteArray::fromHex(SwByteArray(
        "cf000000010008f067a5502a4262b5004075c0d95a482cd0991cd25b0aac406a"
        "5816b6394100f37a1c69797554780bb38cc5a99f5ede4cf73c3ec2493a1839b3"
        "dbcba3f6ea46c5b7684df3548e7ddeb9c3bf9c73cc3f3bded74b562bfb19fb84"
        "022f8ef4cdd93795d77d06edbb7aaf2f58891850abbdca3d20398c276456cbc4"
        "2158407dd074ee"));
    const SwByteArray expectedPlaintext = SwByteArray::fromHex(SwByteArray(
        "02000000000600405a020000560303eefce7f7b37ba1d1632e96677825ddf739"
        "88cfc79825df566dc5430b9a045a1200130100002e00330024001d00209d3c94"
        "0d89690b84d08a60993c144eca684d1081287c834d5311bcf32bb9da1a002b00"
        "020304"));

    SwQuicPacketHeader header;
    SwByteArray plaintext;
    std::size_t consumed = 0;
    if (!requireTrue(SwQuicPacketProtector::unprotectInitial(serverKeys,
                                                             packet,
                                                             header,
                                                             plaintext,
                                                             &consumed,
                                                             &error),
                     "RFC 9001 server Initial unprotection failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> frames;
    if (!requireTrue(SwQuicFrameCodec::decodeFrames(plaintext, frames, &error),
                     "RFC 9001 server Initial frames decode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(consumed == packet.size(), "RFC 9001 consumed length mismatch") &&
           requireTrue(header.version() == 1, "RFC 9001 server Initial version mismatch") &&
           requireTrue(header.destinationConnectionId().isEmpty(),
                       "RFC 9001 server Initial DCID mismatch") &&
           requireTrue(header.sourceConnectionId().bytes().toHex() ==
                           SwByteArray("f067a5502a4262b5"),
                       "RFC 9001 server Initial SCID mismatch") &&
           requireTrue(header.packetNumberLength() == 2,
                       "RFC 9001 server Initial packet number length mismatch") &&
           requireTrue(header.packetNumber() == 1,
                       "RFC 9001 server Initial packet number mismatch") &&
           requireTrue(plaintext == expectedPlaintext,
                       "RFC 9001 server Initial plaintext mismatch") &&
           requireTrue(frames.size() == 2,
                       "RFC 9001 server Initial frame count mismatch") &&
           requireTrue(frames[0].type() == SwQuicFrame::Type::Ack,
                       "RFC 9001 server Initial first frame mismatch") &&
           requireTrue(frames[1].type() == SwQuicFrame::Type::Crypto,
                       "RFC 9001 server Initial second frame mismatch") &&
           requireTrue(frames[1].data().size() == 90,
                       "RFC 9001 server Initial crypto length mismatch");
}

bool testClientHelloBuilderForHttp3() {
    SwString error;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("swclnt01"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray clientHello;
    if (!requireTrue(SwQuicClientHelloBuilder::buildForHttp3(SwString("cloudflare-quic.com"),
                                                             scid,
                                                             clientHello,
                                                             &error),
                     "ClientHello build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    const SwByteArray x25519KeyShare = SwByteArray::fromHex(
        SwByteArray("001d00208520f0098930a754748b7ddcb43ef75a"
                    "0dbf3a0d26381af4eba4a98eaa9b4e6a"));

    return requireTrue(clientHello.size() > 100, "ClientHello too small") &&
           requireTrue(static_cast<unsigned char>(clientHello[0]) == 0x01U,
                       "ClientHello handshake type mismatch") &&
           requireTrue(clientHello.contains(x25519KeyShare),
                       "ClientHello X25519 key share missing");
}

bool testProtectedClientInitialBuilder() {
    SwString error;
    SwQuicConnectionId dcid;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("swcf0001"), dcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("swclnt01"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicClientInitialBuilder::Options options;
    options.serverName = SwString("cloudflare-quic.com");
    options.destinationConnectionId = dcid;
    options.sourceConnectionId = scid;
    options.packetNumber = 0;

    SwByteArray packet;
    if (!requireTrue(SwQuicClientInitialBuilder::buildHttp3Initial(options, packet, &error),
                     "protected Initial build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(packet.size() >= 1200, "protected Initial below minimum size") &&
           requireTrue((static_cast<unsigned char>(packet[0]) & 0x80U) != 0,
                       "protected Initial long header bit missing") &&
           requireTrue(packet.mid(1, 4) == SwByteArray("\0\0\0\1", 4),
                       "protected Initial version mismatch");
}

bool testMalformedPacketRejection() {
    SwString error;
    SwQuicPacketHeader header;
    SwByteArray payload;
    SwByteArray shortPacket;
    shortPacket.append(static_cast<char>(0xc0));
    return requireTrue(!SwQuicPacketCodec::decodeInitialPacket(shortPacket, header, payload, &error),
                       "truncated Initial packet should be rejected");
}

bool testAllFrameTypesRoundTrip() {
    SwString error;

    SwVector<SwQuicFrame::AckRange> ackRanges;
    SwQuicFrame::AckRange range = {2, 5};
    ackRanges.push_back(range);

    SwByteArray connectionId("newcid01");
    SwByteArray resetToken(16, 'r');
    SwByteArray pathData("pathpath");

    SwVector<SwQuicFrame> frames;
    frames.push_back(SwQuicFrame::padding());
    frames.push_back(SwQuicFrame::ping());
    frames.push_back(SwQuicFrame::ackWithEcn(20, 3, 4, ackRanges, 11, 12, 13));
    frames.push_back(SwQuicFrame::resetStream(4, 0x101, 4096));
    frames.push_back(SwQuicFrame::stopSending(8, 0x202));
    frames.push_back(SwQuicFrame::crypto(0, SwByteArray("crypto-bytes")));
    frames.push_back(SwQuicFrame::newToken(SwByteArray("opaque-token")));
    frames.push_back(SwQuicFrame::stream(12, 100, SwByteArray("stream-body"), true));
    frames.push_back(SwQuicFrame::maxData(1048576));
    frames.push_back(SwQuicFrame::maxStreamData(4, 262144));
    frames.push_back(SwQuicFrame::maxStreams(true, 100));
    frames.push_back(SwQuicFrame::maxStreams(false, 3));
    frames.push_back(SwQuicFrame::dataBlocked(999));
    frames.push_back(SwQuicFrame::streamDataBlocked(4, 777));
    frames.push_back(SwQuicFrame::streamsBlocked(false, 5));
    frames.push_back(SwQuicFrame::newConnectionId(7, 3, connectionId, resetToken));
    frames.push_back(SwQuicFrame::retireConnectionId(2));
    frames.push_back(SwQuicFrame::pathChallenge(pathData));
    frames.push_back(SwQuicFrame::pathResponse(pathData));
    frames.push_back(SwQuicFrame::handshakeDone());
    frames.push_back(SwQuicFrame::datagram(SwByteArray("dgram-body")));

    SwByteArray payload;
    if (!requireTrue(SwQuicFrameCodec::encodeFrames(frames, payload, &error),
                     "all-frame encode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwQuicFrame> decoded;
    if (!requireTrue(SwQuicFrameCodec::decodeFrames(payload, decoded, &error),
                     "all-frame decode failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    if (!requireTrue(decoded.size() == frames.size(), "all-frame count mismatch")) {
        return false;
    }

    return requireTrue(decoded[2].type() == SwQuicFrame::Type::Ack &&
                           decoded[2].hasEcn() &&
                           decoded[2].largestAcknowledged() == 20 &&
                           decoded[2].firstAckRange() == 4 &&
                           decoded[2].ackRanges().size() == 1 &&
                           decoded[2].ackRanges()[0].gap == 2 &&
                           decoded[2].ackRanges()[0].rangeLength == 5 &&
                           decoded[2].ect0Count() == 11 &&
                           decoded[2].ect1Count() == 12 &&
                           decoded[2].ecnCeCount() == 13,
                       "ACK-ECN round trip mismatch") &&
           requireTrue(decoded[3].type() == SwQuicFrame::Type::ResetStream &&
                           decoded[3].streamId() == 4 &&
                           decoded[3].errorCode() == 0x101 &&
                           decoded[3].finalSize() == 4096,
                       "RESET_STREAM round trip mismatch") &&
           requireTrue(decoded[4].type() == SwQuicFrame::Type::StopSending &&
                           decoded[4].streamId() == 8 &&
                           decoded[4].errorCode() == 0x202,
                       "STOP_SENDING round trip mismatch") &&
           requireTrue(decoded[6].type() == SwQuicFrame::Type::NewToken &&
                           decoded[6].data() == SwByteArray("opaque-token"),
                       "NEW_TOKEN round trip mismatch") &&
           requireTrue(decoded[8].type() == SwQuicFrame::Type::MaxData &&
                           decoded[8].maximum() == 1048576,
                       "MAX_DATA round trip mismatch") &&
           requireTrue(decoded[9].type() == SwQuicFrame::Type::MaxStreamData &&
                           decoded[9].streamId() == 4 &&
                           decoded[9].maximum() == 262144,
                       "MAX_STREAM_DATA round trip mismatch") &&
           requireTrue(decoded[10].type() == SwQuicFrame::Type::MaxStreams &&
                           decoded[10].isBidirectional() &&
                           decoded[10].maximum() == 100,
                       "MAX_STREAMS bidi round trip mismatch") &&
           requireTrue(decoded[11].type() == SwQuicFrame::Type::MaxStreams &&
                           !decoded[11].isBidirectional() &&
                           decoded[11].maximum() == 3,
                       "MAX_STREAMS uni round trip mismatch") &&
           requireTrue(decoded[12].type() == SwQuicFrame::Type::DataBlocked &&
                           decoded[12].maximum() == 999,
                       "DATA_BLOCKED round trip mismatch") &&
           requireTrue(decoded[13].type() == SwQuicFrame::Type::StreamDataBlocked &&
                           decoded[13].streamId() == 4 &&
                           decoded[13].maximum() == 777,
                       "STREAM_DATA_BLOCKED round trip mismatch") &&
           requireTrue(decoded[14].type() == SwQuicFrame::Type::StreamsBlocked &&
                           !decoded[14].isBidirectional() &&
                           decoded[14].maximum() == 5,
                       "STREAMS_BLOCKED round trip mismatch") &&
           requireTrue(decoded[15].type() == SwQuicFrame::Type::NewConnectionId &&
                           decoded[15].sequenceNumber() == 7 &&
                           decoded[15].retirePriorTo() == 3 &&
                           decoded[15].connectionId() == connectionId &&
                           decoded[15].statelessResetToken() == resetToken,
                       "NEW_CONNECTION_ID round trip mismatch") &&
           requireTrue(decoded[16].type() == SwQuicFrame::Type::RetireConnectionId &&
                           decoded[16].sequenceNumber() == 2,
                       "RETIRE_CONNECTION_ID round trip mismatch") &&
           requireTrue(decoded[17].type() == SwQuicFrame::Type::PathChallenge &&
                           decoded[17].data() == pathData,
                       "PATH_CHALLENGE round trip mismatch") &&
           requireTrue(decoded[18].type() == SwQuicFrame::Type::PathResponse &&
                           decoded[18].data() == pathData,
                       "PATH_RESPONSE round trip mismatch") &&
           requireTrue(decoded[19].type() == SwQuicFrame::Type::HandshakeDone,
                       "HANDSHAKE_DONE round trip mismatch") &&
           requireTrue(decoded[20].type() == SwQuicFrame::Type::Datagram &&
                           decoded[20].data() == SwByteArray("dgram-body"),
                       "DATAGRAM round trip mismatch");
}

bool testUnknownFrameTypeRejected() {
    SwString error;
    SwByteArray payload;
    payload.append(static_cast<char>(0x40));  // 0x40 is not an assigned frame type.
    SwVector<SwQuicFrame> frames;
    return requireTrue(!SwQuicFrameCodec::decodeFrames(payload, frames, &error),
                       "unknown frame type should be rejected");
}

bool testShortHeader1RttRoundTrip() {
    SwString error;
    SwQuicConnectionId dcid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("8394c8f03e515708"), dcid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    // Reuse the RFC 9001 Initial derivation only to obtain a valid AES-128-GCM
    // key set of the right sizes; the round trip validates the 1-RTT framing,
    // header protection and AEAD wiring.
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!requireTrue(SwQuicInitialSecrets::deriveV1(dcid, clientKeys, serverKeys, &error),
                     "1-RTT key derivation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicConnectionId connectionId;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("rtt-dcid"), connectionId, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    const SwByteArray plaintext("http3-1rtt-application-payload");
    SwByteArray protectedPacket;
    if (!requireTrue(SwQuicPacketProtector::protectShortHeader1Rtt(clientKeys,
                                                                  connectionId,
                                                                  0x1234,
                                                                  2,
                                                                  false,
                                                                  true,
                                                                  plaintext,
                                                                  protectedPacket,
                                                                  &error),
                     "short header protection failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    if (!requireTrue((static_cast<unsigned char>(protectedPacket[0]) & 0x80U) == 0,
                     "short header must clear the long-header bit")) {
        return false;
    }

    std::uint64_t packetNumber = 0;
    bool keyPhase = false;
    SwByteArray recovered;
    if (!requireTrue(SwQuicPacketProtector::unprotectShortHeader1Rtt(clientKeys,
                                                                    protectedPacket,
                                                                    connectionId.size(),
                                                                    packetNumber,
                                                                    keyPhase,
                                                                    recovered,
                                                                    &error),
                     "short header unprotection failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(packetNumber == 0x1234, "short header packet number mismatch") &&
           requireTrue(keyPhase, "short header key phase mismatch") &&
           requireTrue(recovered == plaintext, "short header payload mismatch");
}

bool testCoalescedInitialPackets() {
    SwString error;
    SwQuicConnectionId dcid;
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("coalesc1"), dcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("coalesc2"), scid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwByteArray datagram;
    for (int i = 0; i < 2; ++i) {
        SwVector<SwQuicFrame> frames;
        frames.push_back(SwQuicFrame::crypto(0, SwByteArray(i == 0 ? "first" : "second")));

        SwByteArray payload;
        if (!SwQuicFrameCodec::encodeFrames(frames, payload, &error)) {
            std::cerr << error << std::endl;
            return false;
        }

        SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(dcid, scid);
        if (!header.setPacketNumberLength(1, &error) ||
            !header.setPacketNumber(static_cast<std::uint64_t>(i), &error)) {
            std::cerr << error << std::endl;
            return false;
        }

        SwByteArray packet;
        if (!SwQuicPacketCodec::encodeInitialPacket(header, payload, packet, &error)) {
            std::cerr << error << std::endl;
            return false;
        }
        datagram.append(packet);
    }

    std::size_t offset = 0;
    int decodedCount = 0;
    std::uint64_t lastPacketNumber = 0;
    while (offset < datagram.size()) {
        const SwByteArray remaining =
            datagram.mid(static_cast<int>(offset), static_cast<int>(datagram.size() - offset));
        SwQuicPacketHeader header;
        SwByteArray payload;
        std::size_t consumed = 0;
        if (!requireTrue(SwQuicPacketCodec::decodeInitialPacket(remaining, header, payload,
                                                               &consumed, &error),
                         "coalesced Initial decode failed")) {
            std::cerr << error << std::endl;
            return false;
        }
        if (!requireTrue(consumed > 0, "coalesced decode consumed zero bytes")) {
            return false;
        }
        lastPacketNumber = header.packetNumber();
        offset += consumed;
        ++decodedCount;
    }

    return requireTrue(decodedCount == 2, "coalesced Initial packet count mismatch") &&
           requireTrue(lastPacketNumber == 1, "coalesced Initial last packet number mismatch");
}

bool testPacketNumberExpansion() {
    // RFC 9000 appendix A.3 worked example: largest received 0xa82f30ea,
    // 2-byte truncated number 0x9b32 expands to 0xa82f9b32.
    return requireTrue(SwQuicPacketCodec::expandPacketNumber(0xa82f30eaULL, true, 0x9b32, 2) ==
                           0xa82f9b32ULL,
                       "RFC 9000 A.3 packet number expansion mismatch") &&
           requireTrue(SwQuicPacketCodec::expandPacketNumber(0, false, 0x42, 1) == 0x42,
                       "first packet number must decode verbatim") &&
           requireTrue(SwQuicPacketCodec::expandPacketNumber(0xff, true, 0x00, 1) == 0x100,
                       "wrap-forward packet number expansion mismatch") &&
           requireTrue(SwQuicPacketCodec::expandPacketNumber(0x100, true, 0xff, 1) == 0xff,
                       "backward packet number expansion mismatch");
}

// End-to-end wiring of the sans-IO connection: two endpoints exchanging
// protected 1-RTT packets, with ACKs feeding loss recovery and stream data
// running through flow control.
bool testSansIoConnectionLoopback() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("cli-cid1"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("srv-cid1"), serverCid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    // Both directions use Initial-style key material derived from a shared
    // CID: good enough to exercise protection and the full receive path.
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    std::uint64_t now = 1000;

    // Client sends stream data and an unreliable datagram.
    if (!requireTrue(client.sendStreamData(0, SwByteArray("hello quic"), true, &error),
                     "sans-io sendStreamData failed") ||
        !requireTrue(client.queueDatagramFrame(SwByteArray("dgram"), &error),
                     "sans-io queueDatagramFrame failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwVector<SwByteArray> wire;
    if (!requireTrue(client.buildDatagrams(now, wire, &error),
                     "sans-io client buildDatagrams failed") ||
        !requireTrue(!wire.empty(), "sans-io client produced no datagrams")) {
        std::cerr << error << std::endl;
        return false;
    }

    for (std::size_t i = 0; i < wire.size(); ++i) {
        if (!requireTrue(server.receiveDatagram(wire[i], now + 5, &error),
                         "sans-io server receiveDatagram failed")) {
            std::cerr << error << std::endl;
            return false;
        }
    }

    if (!requireTrue(server.readStream(0) == SwByteArray("hello quic"),
                     "sans-io server stream data mismatch") ||
        !requireTrue(server.takeDatagram() == SwByteArray("dgram"),
                     "sans-io server datagram mismatch")) {
        return false;
    }

    // Client has ack-eliciting data in flight: a PTO timer must be armed.
    if (!requireTrue(client.nextTimeoutMs(now) >= 0, "sans-io client PTO timer not armed")) {
        return false;
    }

    // Server flushes its delayed ACK (application space max_ack_delay).
    now += 100;
    SwVector<SwByteArray> ackWire;
    if (!requireTrue(server.buildDatagrams(now, ackWire, &error),
                     "sans-io server buildDatagrams failed") ||
        !requireTrue(!ackWire.empty(), "sans-io server produced no ACK datagram")) {
        std::cerr << error << std::endl;
        return false;
    }

    for (std::size_t i = 0; i < ackWire.size(); ++i) {
        if (!requireTrue(client.receiveDatagram(ackWire[i], now + 5, &error),
                         "sans-io client receiveDatagram(ACK) failed")) {
            std::cerr << error << std::endl;
            return false;
        }
    }

    // The ACK must have cleared the client's in-flight packets and produced
    // an RTT sample.
    typedef SwQuicConnection::Level Level;
    if (!requireTrue(client.lossRecovery(Level::Application).inFlightCount() == 0,
                     "sans-io ACK did not clear in-flight packets") ||
        !requireTrue(client.lossRecovery(Level::Application).hasRttSample(),
                     "sans-io ACK did not produce an RTT sample")) {
        return false;
    }

    // Graceful close: client sends CONNECTION_CLOSE, server drains.
    client.close(0, SwString("bye"));
    SwVector<SwByteArray> closeWire;
    if (!requireTrue(client.buildDatagrams(now + 10, closeWire, &error),
                     "sans-io client close buildDatagrams failed") ||
        !requireTrue(closeWire.size() == 1, "sans-io close datagram count mismatch")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(server.receiveDatagram(closeWire[0], now + 15, &error),
                     "sans-io server receive close failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(client.state() == SwQuicConnection::State::Closing,
                       "sans-io client state should be Closing") &&
           requireTrue(server.state() == SwQuicConnection::State::Draining,
                       "sans-io server state should be Draining");
}

// Connection migration (RFC 9000 section 9): after the peer's address changes
// (Wi-Fi -> cellular), the endpoint validates the new path with
// PATH_CHALLENGE/PATH_RESPONSE, holds sending under the 3x anti-amplification
// limit until it validates, and resets its congestion controller afterwards.
bool testConnectionMigrationPathValidation() {
    SwString error;
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("migclint"), clientCid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("migsrvr1"), serverCid, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);

    std::uint64_t now = 2000;

    // Establish traffic on the original path.
    if (!requireTrue(client.sendStreamData(0, SwByteArray("on wifi"), false, &error),
                     "migration initial send failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    SwVector<SwByteArray> wire;
    if (!requireTrue(client.buildDatagrams(now, wire, &error), "migration build 1 failed")) {
        return false;
    }
    for (std::size_t i = 0; i < wire.size(); ++i) {
        if (!requireTrue(server.receiveDatagram(wire[i], now, &error),
                         "migration server recv 1 failed")) {
            return false;
        }
    }
    if (!requireTrue(server.pathValidated(), "path should start validated")) {
        return false;
    }

    // The peer's address changes: the driver signals it to the server.
    now += 50;
    if (!requireTrue(server.onPeerAddressChanged(now, 1200, &error),
                     "onPeerAddressChanged failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(!server.pathValidated(), "path must be unvalidated after migration") ||
        !requireTrue(server.awaitingPathResponse(),
                     "server must await a PATH_RESPONSE")) {
        return false;
    }

    // The server emits a PATH_CHALLENGE toward the new path.
    SwVector<SwByteArray> challengeWire;
    if (!requireTrue(server.buildDatagrams(now, challengeWire, &error),
                     "migration challenge build failed") ||
        !requireTrue(!challengeWire.empty(), "server did not emit a PATH_CHALLENGE")) {
        return false;
    }

    // The client receives the challenge and answers with PATH_RESPONSE.
    for (std::size_t i = 0; i < challengeWire.size(); ++i) {
        if (!requireTrue(client.receiveDatagram(challengeWire[i], now, &error),
                         "migration client recv challenge failed")) {
            return false;
        }
    }
    SwVector<SwByteArray> responseWire;
    if (!requireTrue(client.buildDatagrams(now, responseWire, &error),
                     "migration response build failed") ||
        !requireTrue(!responseWire.empty(), "client did not emit a PATH_RESPONSE")) {
        return false;
    }

    // The server validates the path on the matching PATH_RESPONSE.
    for (std::size_t i = 0; i < responseWire.size(); ++i) {
        if (!requireTrue(server.receiveDatagram(responseWire[i], now, &error),
                         "migration server recv response failed")) {
            return false;
        }
    }

    return requireTrue(server.pathValidated(),
                       "path must be validated after matching PATH_RESPONSE") &&
           requireTrue(!server.awaitingPathResponse(),
                       "server should no longer await a PATH_RESPONSE") &&
           requireTrue(server.congestionControl().bytesInFlight() == 0,
                       "congestion controller should be reset on migration");
}

// Wire up two sans-IO connections sharing 1-RTT application keys.
bool setupAppPair(SwQuicConnection& client, SwQuicConnection& server,
                  const SwByteArray& clientCidBytes, const SwByteArray& serverCidBytes,
                  SwString* error) {
    SwQuicConnectionId clientCid;
    SwQuicConnectionId serverCid;
    if (!SwQuicConnectionId::fromBytes(clientCidBytes, clientCid, error) ||
        !SwQuicConnectionId::fromBytes(serverCidBytes, serverCid, error)) {
        return false;
    }
    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(serverCid, clientKeys, serverKeys, error)) {
        return false;
    }
    client.setLocalConnectionId(clientCid);
    client.setPeerConnectionId(serverCid);
    server.setLocalConnectionId(serverCid);
    server.setPeerConnectionId(clientCid);
    client.setLevelKeys(SwQuicConnection::Level::Application, serverKeys, clientKeys);
    server.setLevelKeys(SwQuicConnection::Level::Application, clientKeys, serverKeys);
    return true;
}

bool pumpTo(SwQuicConnection& from, SwQuicConnection& to, std::uint64_t nowMs, SwString* error) {
    SwVector<SwByteArray> wire;
    if (!from.buildDatagrams(nowMs, wire, error)) {
        return false;
    }
    for (std::size_t i = 0; i < wire.size(); ++i) {
        if (!to.receiveDatagram(wire[i], nowMs, error)) {
            return false;
        }
    }
    return true;
}

// RFC 9000 4.5: a received RESET_STREAM's Final Size is accounted in connection
// flow control, and a Final Size below already-received data is a FINAL_SIZE_ERROR.
bool testResetStreamFinalSizeAccounting() {
    SwString error;

    // (1) A RESET_STREAM whose final size exceeds the delivered bytes credits
    //     the extra bytes to connection flow control.
    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    if (!setupAppPair(client, server, SwByteArray("rstclin1"), SwByteArray("rstsrvr1"), &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    const std::uint64_t now = 3000;
    if (!requireTrue(client.sendStreamData(0, SwByteArray("hello"), false, &error),
                     "reset test stream send failed") ||
        !requireTrue(pumpTo(client, server, now, &error), "reset test stream deliver failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    client.queueFrame(SwQuicConnection::Level::Application,
                      SwQuicFrame::resetStream(0, 0x0, 10)); // final size 10 > 5 delivered
    if (!requireTrue(pumpTo(client, server, now, &error), "reset frame deliver failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    const bool accounted = server.connectionFlowControl().bytesReceived() >= 10;

    // (2) A RESET_STREAM whose final size is below the received data is rejected.
    SwQuicConnection client2(SwQuicConnection::Role::Client);
    SwQuicConnection server2(SwQuicConnection::Role::Server);
    if (!setupAppPair(client2, server2, SwByteArray("rstclin2"), SwByteArray("rstsrvr2"), &error)) {
        return false;
    }
    if (!requireTrue(client2.sendStreamData(0, SwByteArray("hello"), false, &error),
                     "reset test 2 send failed") ||
        !requireTrue(pumpTo(client2, server2, now, &error), "reset test 2 deliver failed")) {
        return false;
    }
    client2.queueFrame(SwQuicConnection::Level::Application,
                       SwQuicFrame::resetStream(0, 0x0, 3)); // final size 3 < 5 received
    SwVector<SwByteArray> wire;
    if (!requireTrue(client2.buildDatagrams(now, wire, &error), "reset test 2 build failed")) {
        return false;
    }
    bool rejected = false;
    for (std::size_t i = 0; i < wire.size(); ++i) {
        SwString e2;
        if (!server2.receiveDatagram(wire[i], now, &e2)) {
            rejected = true;
        }
    }

    return requireTrue(accounted, "RESET_STREAM final size not accounted in flow control") &&
           requireTrue(rejected, "decreasing RESET_STREAM final size must be rejected");
}

// RFC 9002 7.6.2: on persistent congestion the sender collapses its congestion
// window to the minimum (2 * max_datagram_size). This drives a client through a
// long loss span and asserts the connection actually collapses the window.
bool testPersistentCongestionCollapsesWindow() {
    SwString error;
    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    if (!setupAppPair(client, server, SwByteArray("pcclint1"), SwByteArray("pcsrvr01"), &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    // 1) Establish an RTT sample: send on stream 0, get it acked ~10 ms later.
    std::uint64_t now = 1000;
    if (!requireTrue(client.sendStreamData(0, SwByteArray("x"), false, &error),
                     "pc rtt send failed") ||
        !requireTrue(pumpTo(client, server, now, &error), "pc rtt deliver failed")) {
        return false;
    }
    SwVector<SwByteArray> ack;
    if (!requireTrue(server.buildDatagrams(now + 10, ack, &error), "pc server ack build failed")) {
        return false;
    }
    for (std::size_t i = 0; i < ack.size(); ++i) {
        if (!requireTrue(client.receiveDatagram(ack[i], now + 10, &error),
                         "pc client recv ack failed")) {
            return false;
        }
    }

    // 2) Send three ack-eliciting packets spanning > the persistent-congestion
    //    duration; DROP the first two, deliver only the last.
    auto sendDropped = [&](std::uint64_t streamId, std::uint64_t at) -> bool {
        SwVector<SwByteArray> wire;
        return client.sendStreamData(streamId, SwByteArray("d"), false, &error) &&
               client.buildDatagrams(at, wire, &error); // built (advances pn) but not delivered
    };
    if (!requireTrue(sendDropped(4, 2000), "pc send1 failed") ||
        !requireTrue(sendDropped(8, 2200), "pc send2 failed")) {
        return false;
    }
    // Deliver the third packet so the server acks it, declaring the first two lost.
    if (!requireTrue(client.sendStreamData(12, SwByteArray("d"), false, &error),
                     "pc send3 failed")) {
        return false;
    }
    SwVector<SwByteArray> third;
    if (!requireTrue(client.buildDatagrams(2400, third, &error), "pc build3 failed")) {
        return false;
    }
    for (std::size_t i = 0; i < third.size(); ++i) {
        if (!requireTrue(server.receiveDatagram(third[i], 2400, &error), "pc deliver3 failed")) {
            return false;
        }
    }
    SwVector<SwByteArray> ack3;
    if (!requireTrue(server.buildDatagrams(2400, ack3, &error), "pc ack3 build failed")) {
        return false;
    }
    for (std::size_t i = 0; i < ack3.size(); ++i) {
        if (!requireTrue(client.receiveDatagram(ack3[i], 2400, &error), "pc recv ack3 failed")) {
            return false;
        }
    }

    // The window must have collapsed to the minimum (2 * max_datagram_size).
    const std::uint64_t minWindow = 2 * client.congestionControl().maxDatagramSize();
    return requireTrue(client.congestionControl().congestionWindow() == minWindow,
                       "persistent congestion did not collapse the congestion window");
}

// RFC 9002 / RFC 9000 8.1: a datagram withheld by anti-amplification on an
// unvalidated path must leave NO phantom bytes in flight (packet number not
// burned, congestion accounting untouched) and must not lose its frames.
bool testAmplificationWithheldNoPhantomBytes() {
    SwString error;
    SwQuicConnection client(SwQuicConnection::Role::Client);
    SwQuicConnection server(SwQuicConnection::Role::Server);
    if (!setupAppPair(client, server, SwByteArray("phantcl1"), SwByteArray("phantsv1"), &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    // Queue far more stream data than the anti-amplification budget can carry.
    SwByteArray blob;
    for (int i = 0; i < 12000; ++i) {
        blob.append(static_cast<char>('a' + (i % 26)));
    }
    if (!requireTrue(client.sendStreamData(0, blob, false, &error), "phantom send failed")) {
        return false;
    }

    // Simulate a migration: the path becomes unvalidated with a 3x1200 budget.
    const std::uint64_t now = 5000;
    if (!requireTrue(client.onPeerAddressChanged(now, 1200, &error), "phantom migrate failed")) {
        return false;
    }

    const std::uint64_t before = client.congestionControl().bytesInFlight();
    SwVector<SwByteArray> out;
    if (!requireTrue(client.buildDatagrams(now, out, &error), "phantom build failed")) {
        return false;
    }

    std::uint64_t sent = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        sent += static_cast<std::uint64_t>(out[i].size());
    }
    const std::uint64_t after = client.congestionControl().bytesInFlight();

    // No phantom: bytes counted in flight equal exactly the bytes actually sent.
    if (!requireTrue(after - before == sent,
                     "phantom bytes counted in flight for a withheld datagram")) {
        return false;
    }
    // A withhold must actually have happened (budget < queued data).
    if (!requireTrue(sent <= 3 * 1200 + 1300,
                     "anti-amplification budget was not enforced")) {
        return false;
    }

    // The withheld stream frames must not be lost: validate the path and confirm
    // the remainder now flows.
    if (out.empty() || !requireTrue(server.receiveDatagram(out[0], now, &error),
                                    "phantom deliver challenge failed")) {
        return false;
    }
    SwVector<SwByteArray> resp;
    if (!requireTrue(server.buildDatagrams(now, resp, &error), "phantom resp build failed")) {
        return false;
    }
    for (std::size_t i = 0; i < resp.size(); ++i) {
        if (!requireTrue(client.receiveDatagram(resp[i], now, &error), "phantom resp recv failed")) {
            return false;
        }
    }
    if (!requireTrue(client.pathValidated(), "path not validated after PATH_RESPONSE")) {
        return false;
    }
    SwVector<SwByteArray> rest;
    if (!requireTrue(client.buildDatagrams(now, rest, &error), "phantom rest build failed")) {
        return false;
    }
    return requireTrue(!rest.empty(), "withheld stream frames were lost, not resent");
}

}

int main() {
    if (!testVarIntCodec() ||
        !testConnectionIdValidation() ||
        !testInitialPacketRoundTrip() ||
        !testFrameRoundTrip() ||
        !testInitialPacketWithFramesRoundTrip() ||
        !testAckTrackerBuildsRanges() ||
        !testStreamReassembly() ||
        !testStreamMapMultiplexing() ||
        !testConnectionReceivesInitialPacket() ||
        !testUdpLoopbackServerReceivesInitialPacket() ||
        !testInitialSecretDerivationMatchesRfc9001() ||
        !testInitialPacketProtectionMatchesRfc9001() ||
        !testServerInitialUnprotectionMatchesRfc9001() ||
        !testClientHelloBuilderForHttp3() ||
        !testProtectedClientInitialBuilder() ||
        !testMalformedPacketRejection() ||
        !testAllFrameTypesRoundTrip() ||
        !testUnknownFrameTypeRejected() ||
        !testShortHeader1RttRoundTrip() ||
        !testCoalescedInitialPackets() ||
        !testPacketNumberExpansion() ||
        !testSansIoConnectionLoopback() ||
        !testConnectionMigrationPathValidation() ||
        !testResetStreamFinalSizeAccounting() ||
        !testPersistentCongestionCollapsesWindow() ||
        !testAmplificationWithheldNoPhantomBytes()) {
        return 1;
    }

    std::cout << "QuicPacketCodecSelfTest passed" << std::endl;
    return 0;
}
