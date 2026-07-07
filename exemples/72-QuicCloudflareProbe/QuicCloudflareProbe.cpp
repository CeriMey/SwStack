#include "core/io/SwUdpSocket.h"
#include "core/io/quic/SwQuicClientInitialBuilder.h"
#include "core/io/quic/SwQuicConnectionId.h"
#include "core/io/quic/SwQuicFrameCodec.h"
#include "core/io/quic/SwQuicInitialSecrets.h"
#include "core/io/quic/SwQuicPacketProtector.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const uint16_t kDefaultPort_ = 443;
const int kDefaultTimeoutMs_ = 3000;

uint32_t readU32_(const SwByteArray& bytes, std::size_t offset) {
    if (bytes.size() < offset + 4) {
        return 0;
    }
    return (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3]));
}

std::string hexPreview_(const SwByteArray& bytes, std::size_t limit) {
    std::ostringstream out;
    const std::size_t count = std::min<std::size_t>(bytes.size(), limit);
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(static_cast<unsigned char>(bytes[i]));
    }
    if (bytes.size() > limit) {
        out << " ...";
    }
    return out.str();
}

bool makeConnectionId_(const char* prefix,
                       uint64_t seed,
                       SwQuicConnectionId& outConnectionId,
                       SwString* error) {
    char bytes[8] = {};
    bytes[0] = prefix[0];
    bytes[1] = prefix[1];
    bytes[2] = prefix[2];
    bytes[3] = prefix[3];
    bytes[4] = static_cast<char>((seed >> 24) & 0xffU);
    bytes[5] = static_cast<char>((seed >> 16) & 0xffU);
    bytes[6] = static_cast<char>((seed >> 8) & 0xffU);
    bytes[7] = static_cast<char>(seed & 0xffU);
    return SwQuicConnectionId::fromBytes(SwByteArray(bytes, sizeof(bytes)),
                                         outConnectionId,
                                         error);
}

void printResponse_(const SwByteArray& response,
                    const SwString& sender,
                    uint16_t senderPort) {
    std::cout << "received=" << response.size()
              << " from=" << sender.toStdString() << ":" << senderPort
              << std::endl;

    if (!response.isEmpty()) {
        const unsigned char first = static_cast<unsigned char>(response[0]);
        std::cout << "first_byte=0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(first) << std::dec << std::setfill(' ')
                  << " long_header=" << (((first & 0x80U) != 0) ? "yes" : "no")
                  << std::endl;
    }
    if (response.size() >= 5 && (static_cast<unsigned char>(response[0]) & 0x80U) != 0) {
        std::cout << "version=0x" << std::hex << std::setw(8) << std::setfill('0')
                  << readU32_(response, 1) << std::dec << std::setfill(' ')
                  << std::endl;
    }
    std::cout << "preview=" << hexPreview_(response, 48) << std::endl;
}

const char* frameName_(SwQuicFrame::Type type) {
    switch (type) {
    case SwQuicFrame::Type::Padding:
        return "PADDING";
    case SwQuicFrame::Type::Ping:
        return "PING";
    case SwQuicFrame::Type::Ack:
        return "ACK";
    case SwQuicFrame::Type::ResetStream:
        return "RESET_STREAM";
    case SwQuicFrame::Type::StopSending:
        return "STOP_SENDING";
    case SwQuicFrame::Type::Crypto:
        return "CRYPTO";
    case SwQuicFrame::Type::NewToken:
        return "NEW_TOKEN";
    case SwQuicFrame::Type::Stream:
        return "STREAM";
    case SwQuicFrame::Type::MaxData:
        return "MAX_DATA";
    case SwQuicFrame::Type::MaxStreamData:
        return "MAX_STREAM_DATA";
    case SwQuicFrame::Type::MaxStreams:
        return "MAX_STREAMS";
    case SwQuicFrame::Type::DataBlocked:
        return "DATA_BLOCKED";
    case SwQuicFrame::Type::StreamDataBlocked:
        return "STREAM_DATA_BLOCKED";
    case SwQuicFrame::Type::StreamsBlocked:
        return "STREAMS_BLOCKED";
    case SwQuicFrame::Type::NewConnectionId:
        return "NEW_CONNECTION_ID";
    case SwQuicFrame::Type::RetireConnectionId:
        return "RETIRE_CONNECTION_ID";
    case SwQuicFrame::Type::PathChallenge:
        return "PATH_CHALLENGE";
    case SwQuicFrame::Type::PathResponse:
        return "PATH_RESPONSE";
    case SwQuicFrame::Type::ConnectionClose:
        return "CONNECTION_CLOSE";
    case SwQuicFrame::Type::HandshakeDone:
        return "HANDSHAKE_DONE";
    case SwQuicFrame::Type::Datagram:
        return "DATAGRAM";
    }
    return "UNKNOWN";
}

bool printFrameSummary_(const std::vector<SwQuicFrame>& frames) {
    std::size_t paddingCount = 0;
    bool sawCrypto = false;
    std::cout << "frames=" << frames.size() << std::endl;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const SwQuicFrame& frame = frames[i];
        if (frame.type() == SwQuicFrame::Type::Padding) {
            ++paddingCount;
            continue;
        }

        std::cout << "frame[" << i << "]=" << frameName_(frame.type());
        if (frame.type() == SwQuicFrame::Type::Ack) {
            std::cout << " largest=" << frame.largestAcknowledged()
                      << " delay=" << frame.ackDelay()
                      << " first_range=" << frame.firstAckRange()
                      << " extra_ranges=" << frame.ackRanges().size();
        } else if (frame.type() == SwQuicFrame::Type::Crypto) {
            sawCrypto = true;
            std::cout << " offset=" << frame.offset()
                      << " bytes=" << frame.data().size();
        } else if (frame.type() == SwQuicFrame::Type::ConnectionClose) {
            std::cout << " error=" << frame.errorCode()
                      << " reason=" << frame.reasonPhrase().toStdString();
        }
        std::cout << std::endl;
    }
    if (paddingCount > 0) {
        std::cout << "padding_frames=" << paddingCount << std::endl;
    }
    return sawCrypto;
}

bool unprotectServerInitialPackets_(const SwByteArray& response,
                                    const SwQuicConnectionId& originalDestinationConnectionId,
                                    bool* outSawCrypto,
                                    SwString* error) {
    if (outSawCrypto) {
        *outSawCrypto = false;
    }

    SwQuicInitialKeys clientKeys;
    SwQuicInitialKeys serverKeys;
    if (!SwQuicInitialSecrets::deriveV1(originalDestinationConnectionId,
                                        clientKeys,
                                        serverKeys,
                                        error)) {
        return false;
    }

    std::size_t offset = 0;
    std::size_t initialCount = 0;
    while (offset < response.size()) {
        const unsigned char first = static_cast<unsigned char>(response[offset]);
        if ((first & 0x80U) == 0) {
            std::cout << "coalesced_remaining=" << (response.size() - offset)
                      << " next_header=short" << std::endl;
            break;
        }
        if ((first & 0x30U) != 0) {
            std::cout << "coalesced_remaining=" << (response.size() - offset)
                      << " next_long_packet_type=0x" << std::hex
                      << static_cast<int>((first & 0x30U) >> 4)
                      << std::dec << std::endl;
            break;
        }

        const SwByteArray remaining =
            response.mid(static_cast<int>(offset), static_cast<int>(response.size() - offset));
        SwQuicPacketHeader header;
        SwByteArray plaintext;
        std::size_t consumed = 0;
        if (!SwQuicPacketProtector::unprotectInitial(serverKeys,
                                                     remaining,
                                                     header,
                                                     plaintext,
                                                     &consumed,
                                                     error)) {
            return false;
        }
        if (consumed == 0) {
            *error = SwString("QUIC Initial unprotection consumed zero bytes");
            return false;
        }

        std::vector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(plaintext, frames, error)) {
            return false;
        }

        std::cout << "unprotected_initial[" << initialCount << "]=yes"
                  << " version=0x" << std::hex << std::setw(8) << std::setfill('0')
                  << header.version() << std::dec << std::setfill(' ')
                  << " packet_number=" << header.packetNumber()
                  << " pn_length=" << static_cast<int>(header.packetNumberLength())
                  << " payload_bytes=" << plaintext.size()
                  << " consumed_bytes=" << consumed
                  << std::endl;
        std::cout << "server_scid=" << header.sourceConnectionId().bytes().toHex().toStdString()
                  << " server_dcid=" << header.destinationConnectionId().bytes().toHex().toStdString()
                  << std::endl;
        const bool sawCrypto = printFrameSummary_(frames);
        if (sawCrypto && outSawCrypto) {
            *outSawCrypto = true;
        }

        ++initialCount;
        offset += consumed;
    }

    return initialCount > 0;
}

int parseTimeoutMs_(int argc, char** argv) {
    if (argc < 4) {
        return kDefaultTimeoutMs_;
    }
    const int parsed = std::atoi(argv[3]);
    return parsed > 0 ? parsed : kDefaultTimeoutMs_;
}

}

int main(int argc, char** argv) {
    const SwString host(argc >= 2 ? argv[1] : "cloudflare-quic.com");
    const SwString serverName(argc >= 3 ? argv[2] : host.toStdString().c_str());
    const int timeoutMs = parseTimeoutMs_(argc, argv);

    const uint64_t seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    SwString error;
    SwQuicConnectionId destinationConnectionId;
    SwQuicConnectionId sourceConnectionId;
    if (!makeConnectionId_("swcf", seed, destinationConnectionId, &error) ||
        !makeConnectionId_("swcl", seed ^ 0xa5a55a5aULL, sourceConnectionId, &error)) {
        std::cerr << "connection ID build failed: " << error.toStdString() << std::endl;
        return 1;
    }

    SwQuicClientInitialBuilder::Options options;
    options.serverName = serverName;
    options.destinationConnectionId = destinationConnectionId;
    options.sourceConnectionId = sourceConnectionId;
    options.packetNumber = 0;

    SwByteArray packet;
    if (!SwQuicClientInitialBuilder::buildHttp3Initial(options, packet, &error)) {
        std::cerr << "initial build failed: " << error.toStdString() << std::endl;
        return 1;
    }

    std::cout << "target=" << host.toStdString() << ":" << kDefaultPort_
              << " sni=" << serverName.toStdString()
              << " timeout_ms=" << timeoutMs
              << " initial_bytes=" << packet.size()
              << std::endl;

    SwUdpSocket socket;
    socket.setMaxPendingDatagrams(8);

    const int64_t sent = socket.writeDatagram(packet.constData(),
                                              static_cast<int64_t>(packet.size()),
                                              host,
                                              kDefaultPort_);
    if (sent != static_cast<int64_t>(packet.size())) {
        std::cerr << "send failed: sent=" << sent
                  << " error=" << socket.errorString().toStdString()
                  << " system_error=" << socket.systemError()
                  << std::endl;
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    bool receivedAnyResponse = false;
    bool decryptedAnyInitial = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const int sliceMs = 100;
        socket.pollPendingDatagrams(sliceMs);
        if (!socket.hasPendingDatagrams()) {
            continue;
        }

        SwString sender;
        uint16_t senderPort = 0;
        const SwByteArray response = socket.receiveDatagram(&sender, &senderPort);
        receivedAnyResponse = true;
        printResponse_(response, sender, senderPort);
        bool sawCrypto = false;
        if (!unprotectServerInitialPackets_(response, destinationConnectionId, &sawCrypto, &error)) {
            std::cerr << "server initial unprotect failed: " << error.toStdString() << std::endl;
            return 3;
        }
        decryptedAnyInitial = true;
        if (sawCrypto) {
            std::cout << "server_crypto=yes" << std::endl;
            return 0;
        }
    }

    if (!receivedAnyResponse) {
        std::cerr << "timeout: no UDP response received from " << host.toStdString()
                  << ":" << kDefaultPort_ << std::endl;
        return 2;
    }
    if (decryptedAnyInitial) {
        std::cerr << "timeout: server Initial was decrypted, but no CRYPTO frame was received"
                  << std::endl;
        return 4;
    }
    return 3;
}
