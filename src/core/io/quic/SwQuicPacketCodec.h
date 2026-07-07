#ifndef SWQUICPACKETCODEC_H
#define SWQUICPACKETCODEC_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicPacketHeader.h"
#include "quic/SwQuicVarIntCodec.h"

#include <limits>

class SwQuicPacketCodec {
public:
    static bool encodeInitialPacket(const SwQuicPacketHeader& header,
                                    const SwByteArray& payload,
                                    SwByteArray& outPacket,
                                    SwString* error = nullptr) {
        if (header.form() != SwQuicPacketHeader::Form::Long ||
            header.longPacketType() != SwQuicPacketHeader::LongPacketType::Initial) {
            setError_(error, "Only QUIC Initial long packets can be encoded");
            return false;
        }

        if (header.destinationConnectionId().size() > 255 ||
            header.sourceConnectionId().size() > 255) {
            setError_(error, "QUIC connection ID length does not fit long header");
            return false;
        }

        const std::uint8_t packetNumberLength = header.packetNumberLength();
        if (payload.size() > std::numeric_limits<std::uint64_t>::max() - packetNumberLength) {
            setError_(error, "QUIC packet payload is too large");
            return false;
        }

        const std::uint64_t encodedPayloadLength =
            static_cast<std::uint64_t>(payload.size()) + packetNumberLength;

        outPacket.clear();
        appendByte_(outPacket, static_cast<std::uint8_t>(0xc0U | (packetNumberLength - 1U)));
        appendU32_(outPacket, header.version());
        appendByte_(outPacket, static_cast<std::uint8_t>(header.destinationConnectionId().size()));
        outPacket.append(header.destinationConnectionId().bytes());
        appendByte_(outPacket, static_cast<std::uint8_t>(header.sourceConnectionId().size()));
        outPacket.append(header.sourceConnectionId().bytes());

        if (!SwQuicVarIntCodec::encode(static_cast<std::uint64_t>(header.token().size()),
                                       outPacket,
                                       error)) {
            return false;
        }
        outPacket.append(header.token());

        if (!SwQuicVarIntCodec::encode(encodedPayloadLength, outPacket, error)) {
            return false;
        }

        if (!appendPacketNumber_(header, outPacket, error)) {
            return false;
        }
        outPacket.append(payload);

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool decodeInitialPacket(const SwByteArray& packet,
                                    SwQuicPacketHeader& outHeader,
                                    SwByteArray& outPayload,
                                    SwString* error = nullptr) {
        return decodeInitialPacket(packet, outHeader, outPayload, nullptr, error);
    }

    // RFC 9000 appendix A.3: reconstruct a full packet number from its
    // truncated wire encoding and the largest packet number received so far in
    // the same packet number space. Callers that have not received any packet
    // yet pass hasLargestReceived=false, in which case the truncated value is
    // already the full packet number.
    static std::uint64_t expandPacketNumber(std::uint64_t largestReceived,
                                            bool hasLargestReceived,
                                            std::uint64_t truncated,
                                            std::uint8_t packetNumberLength) {
        if (!hasLargestReceived) {
            return truncated;
        }

        const std::uint64_t pnNbits = static_cast<std::uint64_t>(packetNumberLength) * 8;
        const std::uint64_t expectedPn = largestReceived + 1;
        const std::uint64_t pnWin = 1ULL << pnNbits;
        const std::uint64_t pnHwin = pnWin / 2;
        const std::uint64_t pnMask = pnWin - 1;

        std::uint64_t candidate = (expectedPn & ~pnMask) | truncated;
        if (candidate + pnHwin <= expectedPn && candidate < (1ULL << 62) - pnWin) {
            return candidate + pnWin;
        }
        if (candidate > expectedPn + pnHwin && candidate >= pnWin) {
            return candidate - pnWin;
        }
        return candidate;
    }

    // When outConsumed is non-null the packet may be followed by coalesced
    // packets in the same datagram: only this packet is decoded and the number
    // of bytes it occupied is reported. When it is null the whole buffer must be
    // exactly one Initial packet.
    static bool decodeInitialPacket(const SwByteArray& packet,
                                    SwQuicPacketHeader& outHeader,
                                    SwByteArray& outPayload,
                                    std::size_t* outConsumed,
                                    SwString* error) {
        std::size_t offset = 0;
        std::uint8_t firstByte = 0;
        if (!readByte_(packet, offset, firstByte, error)) {
            return false;
        }

        if ((firstByte & 0x80U) == 0) {
            setError_(error, "QUIC packet is not a long header packet");
            return false;
        }
        if ((firstByte & 0x40U) == 0) {
            setError_(error, "QUIC fixed bit is not set");
            return false;
        }
        if ((firstByte & 0x30U) != 0) {
            setError_(error, "QUIC packet is not an Initial packet");
            return false;
        }

        std::uint32_t version = 0;
        if (!readU32_(packet, offset, version, error)) {
            return false;
        }

        std::uint8_t dcidLength = 0;
        if (!readByte_(packet, offset, dcidLength, error)) {
            return false;
        }

        SwByteArray dcidBytes;
        if (!readSlice_(packet, offset, dcidLength, dcidBytes, error)) {
            return false;
        }

        std::uint8_t scidLength = 0;
        if (!readByte_(packet, offset, scidLength, error)) {
            return false;
        }

        SwByteArray scidBytes;
        if (!readSlice_(packet, offset, scidLength, scidBytes, error)) {
            return false;
        }

        SwQuicConnectionId destinationConnectionId;
        SwQuicConnectionId sourceConnectionId;
        if (!SwQuicConnectionId::fromBytes(dcidBytes, destinationConnectionId, error) ||
            !SwQuicConnectionId::fromBytes(scidBytes, sourceConnectionId, error)) {
            return false;
        }

        std::uint64_t tokenLength = 0;
        if (!SwQuicVarIntCodec::decode(packet, offset, tokenLength, error)) {
            return false;
        }
        if (tokenLength > static_cast<std::uint64_t>(packet.size() - offset)) {
            setError_(error, "QUIC Initial token is truncated");
            return false;
        }

        SwByteArray token;
        if (!readSlice_(packet, offset, static_cast<std::size_t>(tokenLength), token, error)) {
            return false;
        }

        std::uint64_t encodedPayloadLength = 0;
        if (!SwQuicVarIntCodec::decode(packet, offset, encodedPayloadLength, error)) {
            return false;
        }

        const std::uint8_t packetNumberLength = static_cast<std::uint8_t>((firstByte & 0x03U) + 1U);
        if (encodedPayloadLength < packetNumberLength) {
            setError_(error, "QUIC payload length is shorter than packet number");
            return false;
        }
        if (encodedPayloadLength > static_cast<std::uint64_t>(packet.size() - offset)) {
            setError_(error, "QUIC payload is truncated");
            return false;
        }
        if (!outConsumed &&
            encodedPayloadLength != static_cast<std::uint64_t>(packet.size() - offset)) {
            setError_(error, "QUIC coalesced packet decoding is not implemented");
            return false;
        }

        const std::size_t packetEnd = offset + static_cast<std::size_t>(encodedPayloadLength);
        std::uint64_t packetNumber = 0;
        if (!readPacketNumber_(packet, offset, packetNumberLength, packetNumber, error)) {
            return false;
        }

        const std::uint64_t payloadLength = encodedPayloadLength - packetNumberLength;
        SwByteArray payload;
        if (!readSlice_(packet, offset, static_cast<std::size_t>(payloadLength), payload, error)) {
            return false;
        }

        SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(destinationConnectionId,
                                                                   sourceConnectionId);
        header.setVersion(version);
        header.setToken(token);
        header.setPayloadLength(payloadLength);
        if (!header.setPacketNumberLength(packetNumberLength, error) ||
            !header.setPacketNumber(packetNumber, error)) {
            return false;
        }

        outHeader = header;
        outPayload = payload;
        if (outConsumed) {
            *outConsumed = packetEnd;
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void appendByte_(SwByteArray& outBytes, std::uint8_t value) {
        outBytes.append(static_cast<char>(value));
    }

    static void appendU32_(SwByteArray& outBytes, std::uint32_t value) {
        appendByte_(outBytes, static_cast<std::uint8_t>((value >> 24) & 0xffU));
        appendByte_(outBytes, static_cast<std::uint8_t>((value >> 16) & 0xffU));
        appendByte_(outBytes, static_cast<std::uint8_t>((value >> 8) & 0xffU));
        appendByte_(outBytes, static_cast<std::uint8_t>(value & 0xffU));
    }

    static bool readByte_(const SwByteArray& bytes,
                          std::size_t& offset,
                          std::uint8_t& outValue,
                          SwString* error) {
        if (offset >= bytes.size()) {
            setError_(error, "QUIC packet is truncated");
            return false;
        }

        outValue = static_cast<std::uint8_t>(bytes.constData()[offset]);
        ++offset;
        return true;
    }

    static bool readU32_(const SwByteArray& bytes,
                         std::size_t& offset,
                         std::uint32_t& outValue,
                         SwString* error) {
        if (bytes.size() - offset < 4) {
            setError_(error, "QUIC packet version is truncated");
            return false;
        }

        const char* data = bytes.constData();
        outValue = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset])) << 24) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 1])) << 16) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 2])) << 8) |
                   static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 3]));
        offset += 4;
        return true;
    }

    static bool readSlice_(const SwByteArray& bytes,
                           std::size_t& offset,
                           std::size_t length,
                           SwByteArray& outSlice,
                           SwString* error) {
        if (bytes.size() - offset < length) {
            setError_(error, "QUIC packet field is truncated");
            return false;
        }

        outSlice = bytes.mid(static_cast<int>(offset), static_cast<int>(length));
        offset += length;
        return true;
    }

    static bool appendPacketNumber_(const SwQuicPacketHeader& header,
                                    SwByteArray& outPacket,
                                    SwString* error) {
        const std::uint8_t packetNumberLength = header.packetNumberLength();
        if (packetNumberLength < 1 || packetNumberLength > 4) {
            setError_(error, "Invalid QUIC packet number length");
            return false;
        }

        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            const std::uint8_t shift = static_cast<std::uint8_t>((packetNumberLength - 1 - i) * 8);
            appendByte_(outPacket, static_cast<std::uint8_t>((header.packetNumber() >> shift) & 0xffU));
        }
        return true;
    }

    static bool readPacketNumber_(const SwByteArray& packet,
                                  std::size_t& offset,
                                  std::uint8_t packetNumberLength,
                                  std::uint64_t& outPacketNumber,
                                  SwString* error) {
        if (packet.size() - offset < packetNumberLength) {
            setError_(error, "QUIC packet number is truncated");
            return false;
        }

        std::uint64_t packetNumber = 0;
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            packetNumber = (packetNumber << 8) |
                           static_cast<std::uint8_t>(packet.constData()[offset + i]);
        }
        offset += packetNumberLength;
        outPacketNumber = packetNumber;
        return true;
    }
};

#endif
