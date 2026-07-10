#ifndef SWQUICCLIENTINITIALBUILDER_H
#define SWQUICCLIENTINITIALBUILDER_H

#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicClientHelloBuilder.h"
#include "quic/SwQuicConnectionId.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicFrameCodec.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicPacketProtector.h"
#include "quic/SwQuicVarIntCodec.h"

#include <algorithm>
#include <cstdint>
#include <vector>

class SwQuicClientInitialBuilder {
public:
    struct Options {
        SwString serverName;
        SwQuicConnectionId destinationConnectionId;
        SwQuicConnectionId sourceConnectionId;
        // Empty for the first Initial. After a validated Retry this is the
        // exact opaque token carried by the Retry packet (RFC 9000 17.2.5).
        SwByteArray token;
        std::uint64_t packetNumber = 0;
    };

    static bool buildHttp3Initial(const Options& options,
                                  SwByteArray& outPacket,
                                  SwString* error = nullptr) {
        if (options.serverName.isEmpty()) {
            setError_(error, "QUIC Initial server name is missing");
            return false;
        }
        if (options.destinationConnectionId.isEmpty() || options.sourceConnectionId.isEmpty()) {
            setError_(error, "QUIC Initial connection IDs are missing");
            return false;
        }

        SwByteArray clientHello;
        if (!SwQuicClientHelloBuilder::buildForHttp3(options.serverName,
                                                     options.sourceConnectionId,
                                                     clientHello,
                                                     error)) {
            return false;
        }

        return buildFromClientHello(options, clientHello, outPacket, error);
    }

    // Wraps a caller-provided TLS ClientHello (e.g. one built with a real
    // ephemeral X25519 key) into a padded, protected QUIC Initial packet. Used by
    // the live handshake client; buildHttp3Initial is the deterministic probe path.
    static bool buildFromClientHello(const Options& options,
                                     const SwByteArray& clientHello,
                                     SwByteArray& outPacket,
                                     SwString* error = nullptr) {
        if (options.destinationConnectionId.isEmpty() || options.sourceConnectionId.isEmpty()) {
            setError_(error, "QUIC Initial connection IDs are missing");
            return false;
        }

        SwVector<SwQuicFrame> frames;
        frames.push_back(SwQuicFrame::crypto(0, clientHello));

        SwByteArray plaintext;
        if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) {
            return false;
        }

        SwByteArray header;
        const std::uint64_t initialProtectedLength =
            static_cast<std::uint64_t>(plaintext.size() + SwQuicPacketProtector::kTagLength);
        if (!buildHeader_(options, initialProtectedLength, header, error)) {
            return false;
        }

        const std::size_t currentSize = header.size() + plaintext.size() +
                                        SwQuicPacketProtector::kTagLength;
        if (currentSize < minimumInitialDatagramSize_()) {
            plaintext.append(SwByteArray(minimumInitialDatagramSize_() - currentSize, '\0'));
        }

        const std::uint64_t finalProtectedLength =
            static_cast<std::uint64_t>(plaintext.size() + SwQuicPacketProtector::kTagLength);
        if (!buildHeader_(options, finalProtectedLength, header, error)) {
            return false;
        }

        SwQuicInitialKeys clientKeys;
        SwQuicInitialKeys serverKeys;
        if (!SwQuicInitialSecrets::deriveV1(options.destinationConnectionId,
                                            clientKeys,
                                            serverKeys,
                                            error)) {
            return false;
        }

        if (!SwQuicPacketProtector::protectClientInitial(clientKeys,
                                                         options.packetNumber,
                                                         packetNumberLength_(),
                                                         header,
                                                         plaintext,
                                                         outPacket,
                                                         error)) {
            return false;
        }

        if (outPacket.size() < minimumInitialDatagramSize_()) {
            setError_(error, "Protected QUIC Initial is smaller than 1200 bytes");
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

private:
    static std::size_t minimumInitialDatagramSize_() { return 1200; }
    static std::uint8_t packetNumberLength_() { return 2; }

    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void appendU8_(SwByteArray& out, std::uint8_t value) {
        out.append(static_cast<char>(value));
    }

    static void appendU32_(SwByteArray& out, std::uint32_t value) {
        appendU8_(out, static_cast<std::uint8_t>((value >> 24) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>((value >> 16) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>((value >> 8) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>(value & 0xffU));
    }

    static void appendPacketNumber_(SwByteArray& out, std::uint64_t packetNumber) {
        appendU8_(out, static_cast<std::uint8_t>((packetNumber >> 8) & 0xffU));
        appendU8_(out, static_cast<std::uint8_t>(packetNumber & 0xffU));
    }

    static bool buildHeader_(const Options& options,
                             std::uint64_t protectedPayloadLength,
                             SwByteArray& outHeader,
                             SwString* error) {
        if (options.destinationConnectionId.size() > 20 ||
            options.sourceConnectionId.size() > 20) {
            setError_(error, "QUIC Initial connection ID is invalid");
            return false;
        }

        outHeader.clear();
        appendU8_(outHeader, static_cast<std::uint8_t>(0xc0U | (packetNumberLength_() - 1U)));
        appendU32_(outHeader, 1);
        appendU8_(outHeader, static_cast<std::uint8_t>(options.destinationConnectionId.size()));
        outHeader.append(options.destinationConnectionId.bytes());
        appendU8_(outHeader, static_cast<std::uint8_t>(options.sourceConnectionId.size()));
        outHeader.append(options.sourceConnectionId.bytes());

        if (!SwQuicVarIntCodec::encode(
                static_cast<std::uint64_t>(options.token.size()), outHeader, error)) {
            return false;
        }
        outHeader.append(options.token);
        if (!SwQuicVarIntCodec::encode(protectedPayloadLength + packetNumberLength_(),
                                       outHeader,
                                       error)) {
            return false;
        }
        appendPacketNumber_(outHeader, options.packetNumber);
        return true;
    }
};

#endif
