#ifndef SWQUICPACKETPROTECTOR_H
#define SWQUICPACKETPROTECTOR_H

#include "SwByteArray.h"
#include "SwMap.h"
#include "SwString.h"
#include "quic/SwQuicInitialSecrets.h"
#include "quic/SwQuicPacketCodec.h"
#include "quic/SwQuicPacketHeader.h"
#include "quic/SwQuicVarIntCodec.h"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cwchar>
#include <vector>

#if defined(_WIN32)
#include "SwCrypto.h"
#else
#include <openssl/evp.h>
#endif

class SwQuicPacketProtector {
public:
    static const std::size_t kTagLength = 16;

    static bool protectClientInitial(const SwQuicInitialKeys& keys,
                                     std::uint64_t packetNumber,
                                     std::uint8_t packetNumberLength,
                                     const SwByteArray& unprotectedHeader,
                                     const SwByteArray& plaintextPayload,
                                     SwByteArray& outPacket,
                                     SwString* error = nullptr) {
        if (packetNumberLength < 1 || packetNumberLength > 4) {
            setError_(error, "Invalid QUIC packet number length for protection");
            return false;
        }
        if (!validateInitialKeys_(keys, error)) {
            return false;
        }
        if (unprotectedHeader.size() < packetNumberLength) {
            setError_(error, "QUIC header is too short for packet number");
            return false;
        }

        const SwByteArray nonce = nonceForPacketNumber_(keys.iv, packetNumber);
        SwByteArray ciphertextAndTag;
        if (!aes128GcmEncrypt_(keys.key,
                               nonce,
                               unprotectedHeader,
                               plaintextPayload,
                               ciphertextAndTag,
                               error)) {
            return false;
        }

        outPacket = unprotectedHeader;
        outPacket.append(ciphertextAndTag);

        const std::size_t packetNumberOffset = unprotectedHeader.size() - packetNumberLength;
        const std::size_t sampleOffset = packetNumberOffset + 4;
        if (outPacket.size() < sampleOffset + 16) {
            setError_(error, "QUIC packet is too short for header protection sample");
            return false;
        }

        SwByteArray sample = outPacket.mid(static_cast<int>(sampleOffset), 16);
        SwByteArray mask;
        if (!aes128EcbEncryptBlock_(keys.headerProtectionKey, sample, mask, error)) {
            return false;
        }

        outPacket[0] = static_cast<char>(static_cast<unsigned char>(outPacket[0]) ^
                                        (static_cast<unsigned char>(mask[0]) & 0x0fU));
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            outPacket[packetNumberOffset + i] =
                static_cast<char>(static_cast<unsigned char>(outPacket[packetNumberOffset + i]) ^
                                  static_cast<unsigned char>(mask[1 + i]));
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    // When largestReceivedPn is non-null the truncated wire packet number is
    // expanded to the full number relative to it (RFC 9000 appendix A.3)
    // before the AEAD nonce is computed; a null pointer keeps the historical
    // behaviour of using the truncated value verbatim, which is only correct
    // for the first packets of a connection.
    static bool unprotectInitial(const SwQuicInitialKeys& keys,
                                 const SwByteArray& packet,
                                 SwQuicPacketHeader& outHeader,
                                 SwByteArray& outPlaintextPayload,
                                 std::size_t* outConsumedBytes = nullptr,
                                 SwString* error = nullptr,
                                 const std::uint64_t* largestReceivedPn = nullptr) {
        if (!validateInitialKeys_(keys, error)) {
            return false;
        }

        std::size_t offset = 0;
        std::uint8_t protectedFirstByte = 0;
        if (!readByte_(packet, offset, protectedFirstByte, error)) {
            return false;
        }
        if ((protectedFirstByte & 0x80U) == 0) {
            setError_(error, "QUIC packet is not a long header packet");
            return false;
        }
        if ((protectedFirstByte & 0x40U) == 0) {
            setError_(error, "QUIC fixed bit is not set");
            return false;
        }
        if ((protectedFirstByte & 0x30U) != 0) {
            setError_(error, "QUIC protected packet is not an Initial packet");
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

        const std::size_t packetNumberOffset = offset;
        const std::size_t sampleOffset = packetNumberOffset + 4;
        if (packet.size() < sampleOffset + 16) {
            setError_(error, "QUIC protected packet is too short for header protection sample");
            return false;
        }

        SwByteArray sample = packet.mid(static_cast<int>(sampleOffset), 16);
        SwByteArray mask;
        if (!aes128EcbEncryptBlock_(keys.headerProtectionKey, sample, mask, error)) {
            return false;
        }

        const std::uint8_t firstByte = static_cast<std::uint8_t>(
            protectedFirstByte ^ (static_cast<std::uint8_t>(mask[0]) & 0x0fU));
        const std::uint8_t packetNumberLength = static_cast<std::uint8_t>((firstByte & 0x03U) + 1U);
        if (encodedPayloadLength < packetNumberLength + kTagLength) {
            setError_(error, "QUIC protected payload is shorter than packet number and tag");
            return false;
        }
        if (encodedPayloadLength > static_cast<std::uint64_t>(packet.size() - packetNumberOffset)) {
            setError_(error, "QUIC protected payload is truncated");
            return false;
        }

        const std::size_t packetNumberEnd = packetNumberOffset + packetNumberLength;
        SwByteArray aad = packet.mid(0, static_cast<int>(packetNumberEnd));
        aad[0] = static_cast<char>(firstByte);
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            aad[packetNumberOffset + i] =
                static_cast<char>(static_cast<std::uint8_t>(aad[packetNumberOffset + i]) ^
                                  static_cast<std::uint8_t>(mask[1 + i]));
        }

        std::uint64_t packetNumber =
            readPacketNumber_(aad, packetNumberOffset, packetNumberLength);
        if (largestReceivedPn) {
            packetNumber = SwQuicPacketCodec::expandPacketNumber(*largestReceivedPn, true,
                                                                 packetNumber,
                                                                 packetNumberLength);
        }
        const std::size_t ciphertextAndTagLength =
            static_cast<std::size_t>(encodedPayloadLength - packetNumberLength);
        const SwByteArray ciphertextAndTag =
            packet.mid(static_cast<int>(packetNumberEnd), static_cast<int>(ciphertextAndTagLength));

        SwByteArray plaintext;
        if (!aes128GcmDecrypt_(keys.key,
                               nonceForPacketNumber_(keys.iv, packetNumber),
                               aad,
                               ciphertextAndTag,
                               plaintext,
                               error)) {
            return false;
        }

        SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(destinationConnectionId,
                                                                   sourceConnectionId);
        header.setVersion(version);
        header.setToken(token);
        header.setPayloadLength(static_cast<std::uint64_t>(plaintext.size()));
        if (!header.setPacketNumberLength(packetNumberLength, error)) {
            return false;
        }
        header.setDecodedPacketNumber(packetNumber);

        outHeader = header;
        outPlaintextPayload = plaintext;
        if (outConsumedBytes) {
            *outConsumedBytes = packetNumberOffset + static_cast<std::size_t>(encodedPayloadLength);
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Unprotects a Handshake long-header packet (RFC 9000 17.2.4). Same AEAD and
    // header-protection scheme as Initial, but the header carries no Token field
    // and the long-packet-type bits are 0x20. outConsumed, when non-null, reports
    // the packet length so coalesced packets in one datagram can be iterated.
    static bool unprotectHandshake(const SwQuicInitialKeys& keys,
                                   const SwByteArray& packet,
                                   SwQuicPacketHeader& outHeader,
                                   SwByteArray& outPlaintextPayload,
                                   std::size_t* outConsumed = nullptr,
                                   SwString* error = nullptr,
                                   const std::uint64_t* largestReceivedPn = nullptr) {
        return unprotectLongHeaderNoToken_(keys, packet, 0x20U,
                                           SwQuicPacketHeader::LongPacketType::Handshake,
                                           outHeader, outPlaintextPayload, outConsumed,
                                           error, largestReceivedPn);
    }

    // Unprotects a 0-RTT long-header packet (RFC 9000 17.2.3). Wire layout is
    // identical to Handshake (no token) but the long-packet-type bits are 0x10;
    // the AEAD/header-protection keys are the client early (0-RTT) keys.
    static bool unprotectZeroRtt(const SwQuicInitialKeys& keys,
                                 const SwByteArray& packet,
                                 SwQuicPacketHeader& outHeader,
                                 SwByteArray& outPlaintextPayload,
                                 std::size_t* outConsumed = nullptr,
                                 SwString* error = nullptr,
                                 const std::uint64_t* largestReceivedPn = nullptr) {
        return unprotectLongHeaderNoToken_(keys, packet, 0x10U,
                                           SwQuicPacketHeader::LongPacketType::ZeroRtt,
                                           outHeader, outPlaintextPayload, outConsumed,
                                           error, largestReceivedPn);
    }

    // Shared body for token-less long-header packets (Handshake, 0-RTT).
    static bool unprotectLongHeaderNoToken_(const SwQuicInitialKeys& keys,
                                            const SwByteArray& packet,
                                            std::uint8_t expectedTypeBits,
                                            SwQuicPacketHeader::LongPacketType resultType,
                                            SwQuicPacketHeader& outHeader,
                                            SwByteArray& outPlaintextPayload,
                                            std::size_t* outConsumed,
                                            SwString* error,
                                            const std::uint64_t* largestReceivedPn) {
        if (!validateInitialKeys_(keys, error)) {
            return false;
        }

        std::size_t offset = 0;
        std::uint8_t protectedFirstByte = 0;
        if (!readByte_(packet, offset, protectedFirstByte, error)) {
            return false;
        }
        if ((protectedFirstByte & 0x80U) == 0) {
            setError_(error, "QUIC packet is not a long header packet");
            return false;
        }
        if ((protectedFirstByte & 0x40U) == 0) {
            setError_(error, "QUIC fixed bit is not set");
            return false;
        }
        if ((protectedFirstByte & 0x30U) != expectedTypeBits) {
            setError_(error, "QUIC protected packet has an unexpected long-header type");
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

        std::uint64_t encodedPayloadLength = 0;
        if (!SwQuicVarIntCodec::decode(packet, offset, encodedPayloadLength, error)) {
            return false;
        }

        const std::size_t packetNumberOffset = offset;
        const std::size_t sampleOffset = packetNumberOffset + 4;
        if (packet.size() < sampleOffset + 16) {
            setError_(error, "QUIC Handshake packet is too short for header protection sample");
            return false;
        }

        SwByteArray sample = packet.mid(static_cast<int>(sampleOffset), 16);
        SwByteArray mask;
        if (!aes128EcbEncryptBlock_(keys.headerProtectionKey, sample, mask, error)) {
            return false;
        }

        const std::uint8_t firstByte = static_cast<std::uint8_t>(
            protectedFirstByte ^ (static_cast<std::uint8_t>(mask[0]) & 0x0fU));
        const std::uint8_t packetNumberLength = static_cast<std::uint8_t>((firstByte & 0x03U) + 1U);
        if (encodedPayloadLength < packetNumberLength + kTagLength) {
            setError_(error, "QUIC Handshake payload is shorter than packet number and tag");
            return false;
        }
        if (encodedPayloadLength > static_cast<std::uint64_t>(packet.size() - packetNumberOffset)) {
            setError_(error, "QUIC Handshake payload is truncated");
            return false;
        }

        const std::size_t packetNumberEnd = packetNumberOffset + packetNumberLength;
        SwByteArray aad = packet.mid(0, static_cast<int>(packetNumberEnd));
        aad[0] = static_cast<char>(firstByte);
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            aad[packetNumberOffset + i] =
                static_cast<char>(static_cast<std::uint8_t>(aad[packetNumberOffset + i]) ^
                                  static_cast<std::uint8_t>(mask[1 + i]));
        }

        std::uint64_t packetNumber =
            readPacketNumber_(aad, packetNumberOffset, packetNumberLength);
        if (largestReceivedPn) {
            packetNumber = SwQuicPacketCodec::expandPacketNumber(*largestReceivedPn, true,
                                                                 packetNumber,
                                                                 packetNumberLength);
        }
        const std::size_t ciphertextAndTagLength =
            static_cast<std::size_t>(encodedPayloadLength - packetNumberLength);
        const SwByteArray ciphertextAndTag =
            packet.mid(static_cast<int>(packetNumberEnd), static_cast<int>(ciphertextAndTagLength));

        SwByteArray plaintext;
        if (!aes128GcmDecrypt_(keys.key,
                               nonceForPacketNumber_(keys.iv, packetNumber),
                               aad,
                               ciphertextAndTag,
                               plaintext,
                               error)) {
            return false;
        }

        SwQuicPacketHeader header;
        header.setForm(SwQuicPacketHeader::Form::Long);
        header.setLongPacketType(resultType);
        header.setVersion(version);
        header.setDestinationConnectionId(destinationConnectionId);
        header.setSourceConnectionId(sourceConnectionId);
        header.setPayloadLength(static_cast<std::uint64_t>(plaintext.size()));
        if (!header.setPacketNumberLength(packetNumberLength, error)) {
            return false;
        }
        header.setDecodedPacketNumber(packetNumber);

        outHeader = header;
        outPlaintextPayload = plaintext;
        if (outConsumed) {
            *outConsumed = packetNumberOffset + static_cast<std::size_t>(encodedPayloadLength);
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // protectClientInitial applies AEAD + header protection to any long-header
    // packet given a fully built unprotected header, so it doubles as the
    // Handshake protector. This alias documents that intent at call sites.
    static bool protectLongHeader(const SwQuicInitialKeys& keys,
                                  std::uint64_t packetNumber,
                                  std::uint8_t packetNumberLength,
                                  const SwByteArray& unprotectedHeader,
                                  const SwByteArray& plaintextPayload,
                                  SwByteArray& outPacket,
                                  SwString* error = nullptr) {
        return protectClientInitial(keys, packetNumber, packetNumberLength,
                                    unprotectedHeader, plaintextPayload, outPacket, error);
    }

    // Protects a 1-RTT (short header) packet. Unlike Initial/Handshake long
    // headers, the short header does not carry the destination connection ID
    // length on the wire: the peer knows it from connection state, so it must be
    // supplied when unprotecting.
    static bool protectShortHeader1Rtt(const SwQuicInitialKeys& keys,
                                       const SwQuicConnectionId& destinationConnectionId,
                                       std::uint64_t packetNumber,
                                       std::uint8_t packetNumberLength,
                                       bool spinBit,
                                       bool keyPhase,
                                       const SwByteArray& plaintextPayload,
                                       SwByteArray& outPacket,
                                       SwString* error = nullptr) {
        if (packetNumberLength < 1 || packetNumberLength > 4) {
            setError_(error, "Invalid QUIC packet number length for protection");
            return false;
        }
        if (!validateInitialKeys_(keys, error)) {
            return false;
        }

        SwByteArray header;
        std::uint8_t firstByte = static_cast<std::uint8_t>(0x40U | (packetNumberLength - 1U));
        if (spinBit) {
            firstByte = static_cast<std::uint8_t>(firstByte | 0x20U);
        }
        if (keyPhase) {
            firstByte = static_cast<std::uint8_t>(firstByte | 0x04U);
        }
        header.append(static_cast<char>(firstByte));
        header.append(destinationConnectionId.bytes());
        const std::size_t packetNumberOffset = header.size();
        appendPacketNumber_(header, packetNumber, packetNumberLength);

        const SwByteArray nonce = nonceForPacketNumber_(keys.iv, packetNumber);
        SwByteArray ciphertextAndTag;
        if (!aes128GcmEncrypt_(keys.key, nonce, header, plaintextPayload, ciphertextAndTag, error)) {
            return false;
        }

        outPacket = header;
        outPacket.append(ciphertextAndTag);

        const std::size_t sampleOffset = packetNumberOffset + 4;
        if (outPacket.size() < sampleOffset + 16) {
            setError_(error, "QUIC packet is too short for header protection sample");
            return false;
        }

        SwByteArray sample = outPacket.mid(static_cast<int>(sampleOffset), 16);
        SwByteArray mask;
        if (!aes128EcbEncryptBlock_(keys.headerProtectionKey, sample, mask, error)) {
            return false;
        }

        // Short header protects the low 5 bits of the first byte.
        outPacket[0] = static_cast<char>(static_cast<unsigned char>(outPacket[0]) ^
                                        (static_cast<unsigned char>(mask[0]) & 0x1fU));
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            outPacket[packetNumberOffset + i] =
                static_cast<char>(static_cast<unsigned char>(outPacket[packetNumberOffset + i]) ^
                                  static_cast<unsigned char>(mask[1 + i]));
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool unprotectShortHeader1Rtt(const SwQuicInitialKeys& keys,
                                         const SwByteArray& packet,
                                         std::size_t destinationConnectionIdLength,
                                         std::uint64_t& outPacketNumber,
                                         bool& outKeyPhase,
                                         SwByteArray& outPlaintextPayload,
                                         SwString* error = nullptr,
                                         const std::uint64_t* largestReceivedPn = nullptr) {
        if (!validateInitialKeys_(keys, error)) {
            return false;
        }
        if (destinationConnectionIdLength > SwQuicConnectionId::kMaxLength) {
            setError_(error, "QUIC short header connection ID length is invalid");
            return false;
        }
        if (packet.isEmpty()) {
            setError_(error, "QUIC short header packet is empty");
            return false;
        }

        const std::uint8_t protectedFirstByte = static_cast<std::uint8_t>(packet.constData()[0]);
        if ((protectedFirstByte & 0x80U) != 0) {
            setError_(error, "QUIC packet is not a short header packet");
            return false;
        }
        if ((protectedFirstByte & 0x40U) == 0) {
            setError_(error, "QUIC fixed bit is not set");
            return false;
        }

        const std::size_t packetNumberOffset = 1 + destinationConnectionIdLength;
        const std::size_t sampleOffset = packetNumberOffset + 4;
        if (packet.size() < sampleOffset + 16) {
            setError_(error, "QUIC short header packet is too short for header protection sample");
            return false;
        }

        SwByteArray sample = packet.mid(static_cast<int>(sampleOffset), 16);
        SwByteArray mask;
        if (!aes128EcbEncryptBlock_(keys.headerProtectionKey, sample, mask, error)) {
            return false;
        }

        const std::uint8_t firstByte = static_cast<std::uint8_t>(
            protectedFirstByte ^ (static_cast<std::uint8_t>(mask[0]) & 0x1fU));
        const std::uint8_t packetNumberLength = static_cast<std::uint8_t>((firstByte & 0x03U) + 1U);
        const std::size_t packetNumberEnd = packetNumberOffset + packetNumberLength;
        if (packet.size() < packetNumberEnd + kTagLength) {
            setError_(error, "QUIC short header payload is shorter than packet number and tag");
            return false;
        }

        SwByteArray aad = packet.mid(0, static_cast<int>(packetNumberEnd));
        aad[0] = static_cast<char>(firstByte);
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            aad[packetNumberOffset + i] =
                static_cast<char>(static_cast<std::uint8_t>(aad[packetNumberOffset + i]) ^
                                  static_cast<std::uint8_t>(mask[1 + i]));
        }

        std::uint64_t packetNumber =
            readPacketNumber_(aad, packetNumberOffset, packetNumberLength);
        if (largestReceivedPn) {
            packetNumber = SwQuicPacketCodec::expandPacketNumber(*largestReceivedPn, true,
                                                                 packetNumber,
                                                                 packetNumberLength);
        }
        const SwByteArray ciphertextAndTag =
            packet.mid(static_cast<int>(packetNumberEnd),
                       static_cast<int>(packet.size() - packetNumberEnd));

        SwByteArray plaintext;
        if (!aes128GcmDecrypt_(keys.key,
                               nonceForPacketNumber_(keys.iv, packetNumber),
                               aad,
                               ciphertextAndTag,
                               plaintext,
                               error)) {
            return false;
        }

        outPacketNumber = packetNumber;
        outKeyPhase = (firstByte & 0x04U) != 0;
        outPlaintextPayload = plaintext;
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

    static void appendPacketNumber_(SwByteArray& out,
                                    std::uint64_t packetNumber,
                                    std::uint8_t length) {
        for (std::uint8_t i = 0; i < length; ++i) {
            const std::uint8_t shift = static_cast<std::uint8_t>((length - 1 - i) * 8);
            out.append(static_cast<char>((packetNumber >> shift) & 0xffU));
        }
    }

    static bool readByte_(const SwByteArray& bytes,
                          std::size_t& offset,
                          std::uint8_t& outValue,
                          SwString* error) {
        if (offset >= bytes.size()) {
            setError_(error, "QUIC protected packet is truncated");
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
            setError_(error, "QUIC protected packet version is truncated");
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
            setError_(error, "QUIC protected packet field is truncated");
            return false;
        }

        outSlice = bytes.mid(static_cast<int>(offset), static_cast<int>(length));
        offset += length;
        return true;
    }

    static std::uint64_t readPacketNumber_(const SwByteArray& packet,
                                           std::size_t packetNumberOffset,
                                           std::uint8_t packetNumberLength) {
        std::uint64_t packetNumber = 0;
        for (std::uint8_t i = 0; i < packetNumberLength; ++i) {
            packetNumber = (packetNumber << 8) |
                           static_cast<std::uint8_t>(packet.constData()[packetNumberOffset + i]);
        }
        return packetNumber;
    }

    static bool validateInitialKeys_(const SwQuicInitialKeys& keys, SwString* error) {
        if (keys.key.size() != 16 || keys.iv.size() != 12 || keys.headerProtectionKey.size() != 16) {
            setError_(error, "Invalid QUIC Initial key material");
            return false;
        }
        return true;
    }

#if defined(_WIN32)
    static bool openAes_(BCRYPT_ALG_HANDLE& algorithm,
                         const wchar_t* chainingMode,
                         SwString* error) {
        algorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
            setError_(error, "BCryptOpenAlgorithmProvider(AES) failed");
            return false;
        }

        if (BCryptSetProperty(algorithm,
                              BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainingMode)),
                              static_cast<ULONG>((wcslen(chainingMode) + 1) * sizeof(wchar_t)),
                              0) != 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            setError_(error, "BCryptSetProperty(AES chaining mode) failed");
            return false;
        }
        return true;
    }

    static bool generateAesKey_(BCRYPT_ALG_HANDLE algorithm,
                                const SwByteArray& key,
                                BCRYPT_KEY_HANDLE& outKey,
                                std::vector<unsigned char>& keyObject,
                                SwString* error) {
        outKey = nullptr;
        ULONG cbData = 0;
        ULONG keyObjectSize = 0;
        if (BCryptGetProperty(algorithm,
                              BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&keyObjectSize),
                              sizeof(keyObjectSize),
                              &cbData,
                              0) != 0) {
            setError_(error, "BCryptGetProperty(AES object length) failed");
            return false;
        }

        keyObject.resize(keyObjectSize);
        if (BCryptGenerateSymmetricKey(algorithm,
                                       &outKey,
                                       keyObject.data(),
                                       keyObjectSize,
                                       reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
                                       static_cast<ULONG>(key.size()),
                                       0) != 0) {
            setError_(error, "BCryptGenerateSymmetricKey(AES) failed");
            return false;
        }
        return true;
    }

    // --- Cache thread-local des handles CNG : provider AES ouvert une fois par mode,
    //     clé importée une fois par jeu d'octets, réutilisés à chaque paquet.
    //     Les octets produits sont identiques -> optimisation invisible sur le fil (conforme RFC 9001). ---
    struct CngKey_ { BCRYPT_KEY_HANDLE handle = nullptr; std::vector<unsigned char> object; };
    struct CngCache_ {
        BCRYPT_ALG_HANDLE gcmAlg = nullptr;
        BCRYPT_ALG_HANDLE ecbAlg = nullptr;
        SwMap<SwByteArray, CngKey_> gcmKeys; // clé = octets de la clé AES
        SwMap<SwByteArray, CngKey_> ecbKeys;
        ~CngCache_() {
            for (SwMap<SwByteArray, CngKey_>::iterator it = gcmKeys.begin(); it != gcmKeys.end(); ++it) {
                if (it.value().handle) BCryptDestroyKey(it.value().handle);
            }
            for (SwMap<SwByteArray, CngKey_>::iterator it = ecbKeys.begin(); it != ecbKeys.end(); ++it) {
                if (it.value().handle) BCryptDestroyKey(it.value().handle);
            }
            if (gcmAlg) BCryptCloseAlgorithmProvider(gcmAlg, 0);
            if (ecbAlg) BCryptCloseAlgorithmProvider(ecbAlg, 0);
        }
    };
    static CngCache_& cngCache_() { static thread_local CngCache_ c; return c; }
    static std::size_t maxCachedKeys_() { return 8192; }

    static BCRYPT_KEY_HANDLE cachedGcmKey_(const SwByteArray& key, SwString* error) {
        CngCache_& c = cngCache_();
        if (!c.gcmAlg && !openAes_(c.gcmAlg, BCRYPT_CHAIN_MODE_GCM, error)) return nullptr;
        SwMap<SwByteArray, CngKey_>::iterator it = c.gcmKeys.find(key);
        if (it != c.gcmKeys.end()) return it.value().handle;
        if (c.gcmKeys.size() >= maxCachedKeys_()) {
            for (SwMap<SwByteArray, CngKey_>::iterator i = c.gcmKeys.begin(); i != c.gcmKeys.end(); ++i) {
                if (i.value().handle) BCryptDestroyKey(i.value().handle);
            }
            c.gcmKeys.clear();
        }
        CngKey_& slot = c.gcmKeys[key]; // operator[] insère ; référence stable (map ordonnée)
        if (!generateAesKey_(c.gcmAlg, key, slot.handle, slot.object, error)) { c.gcmKeys.remove(key); return nullptr; }
        return slot.handle;
    }
    static BCRYPT_KEY_HANDLE cachedEcbKey_(const SwByteArray& key, SwString* error) {
        CngCache_& c = cngCache_();
        if (!c.ecbAlg && !openAes_(c.ecbAlg, BCRYPT_CHAIN_MODE_ECB, error)) return nullptr;
        SwMap<SwByteArray, CngKey_>::iterator it = c.ecbKeys.find(key);
        if (it != c.ecbKeys.end()) return it.value().handle;
        if (c.ecbKeys.size() >= maxCachedKeys_()) {
            for (SwMap<SwByteArray, CngKey_>::iterator i = c.ecbKeys.begin(); i != c.ecbKeys.end(); ++i) {
                if (i.value().handle) BCryptDestroyKey(i.value().handle);
            }
            c.ecbKeys.clear();
        }
        CngKey_& slot = c.ecbKeys[key];
        if (!generateAesKey_(c.ecbAlg, key, slot.handle, slot.object, error)) { c.ecbKeys.remove(key); return nullptr; }
        return slot.handle;
    }
#else
    // --- Contextes EVP réutilisés par thread (perf, invisible sur le fil) : évite un
    //     EVP_CIPHER_CTX_new()/free() par paquet. Le schedule de clé est ré-initialisé par appel. ---
    struct EvpCtxHolder_ {
        EVP_CIPHER_CTX* ctx = nullptr;
        EVP_CIPHER_CTX* get() { if (!ctx) ctx = EVP_CIPHER_CTX_new(); return ctx; }
        ~EvpCtxHolder_() { if (ctx) EVP_CIPHER_CTX_free(ctx); }
    };
    static EVP_CIPHER_CTX* evpGcmEncCtx_() { static thread_local EvpCtxHolder_ h; return h.get(); }
    static EVP_CIPHER_CTX* evpGcmDecCtx_() { static thread_local EvpCtxHolder_ h; return h.get(); }
    static EVP_CIPHER_CTX* evpEcbCtx_()    { static thread_local EvpCtxHolder_ h; return h.get(); }
#endif

    static bool aes128GcmEncrypt_(const SwByteArray& key,
                                  const SwByteArray& nonce,
                                  const SwByteArray& aad,
                                  const SwByteArray& plaintext,
                                  SwByteArray& outCiphertextAndTag,
                                  SwString* error) {
#if defined(_WIN32)
        BCRYPT_KEY_HANDLE aesKey = cachedGcmKey_(key, error); // handle mis en cache (thread-local)
        if (!aesKey) {
            return false;
        }

        SwByteArray ciphertext(plaintext.size(), '\0');
        SwByteArray tag(kTagLength, '\0');
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
        authInfo.cbNonce = static_cast<ULONG>(nonce.size());
        authInfo.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char*>(aad.constData()));
        authInfo.cbAuthData = static_cast<ULONG>(aad.size());
        authInfo.pbTag = reinterpret_cast<PUCHAR>(tag.data());
        authInfo.cbTag = static_cast<ULONG>(tag.size());

        ULONG outputSize = 0;
        const NTSTATUS status = BCryptEncrypt(aesKey,
                                              reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.constData())),
                                              static_cast<ULONG>(plaintext.size()),
                                              &authInfo,
                                              nullptr,
                                              0,
                                              reinterpret_cast<PUCHAR>(ciphertext.data()),
                                              static_cast<ULONG>(ciphertext.size()),
                                              &outputSize,
                                              0);
        if (status != 0 || outputSize != plaintext.size()) {
            setError_(error, "BCryptEncrypt(AES-GCM) failed");
            return false;
        }

        outCiphertextAndTag = ciphertext;
        outCiphertextAndTag.append(tag);
        return true;
#else
        EVP_CIPHER_CTX* ctx = evpGcmEncCtx_();
        if (!ctx) { setError_(error, "EVP_CIPHER_CTX_new failed"); return false; }
        int outLen = 0;
        if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
            EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                               reinterpret_cast<const unsigned char*>(key.constData()),
                               reinterpret_cast<const unsigned char*>(nonce.constData())) != 1) {
            setError_(error, "EVP_EncryptInit(AES-128-GCM) failed");
            return false;
        }
        if (aad.size() > 0 &&
            EVP_EncryptUpdate(ctx, nullptr, &outLen,
                              reinterpret_cast<const unsigned char*>(aad.constData()),
                              static_cast<int>(aad.size())) != 1) {
            setError_(error, "EVP_EncryptUpdate(AAD) failed");
            return false;
        }
        SwByteArray ciphertext(plaintext.size(), '\0');
        if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()), &outLen,
                              reinterpret_cast<const unsigned char*>(plaintext.constData()),
                              static_cast<int>(plaintext.size())) != 1) {
            setError_(error, "EVP_EncryptUpdate(AES-128-GCM) failed");
            return false;
        }
        int finalLen = 0;
        if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + outLen, &finalLen) != 1) {
            setError_(error, "EVP_EncryptFinal(AES-128-GCM) failed");
            return false;
        }
        SwByteArray tag(kTagLength, '\0');
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagLength),
                                reinterpret_cast<unsigned char*>(tag.data())) != 1) {
            setError_(error, "EVP_CTRL_GCM_GET_TAG failed");
            return false;
        }
        outCiphertextAndTag = ciphertext;
        outCiphertextAndTag.append(tag);
        return true;
#endif
    }

    static bool aes128GcmDecrypt_(const SwByteArray& key,
                                  const SwByteArray& nonce,
                                  const SwByteArray& aad,
                                  const SwByteArray& ciphertextAndTag,
                                  SwByteArray& outPlaintext,
                                  SwString* error) {
#if defined(_WIN32)
        if (ciphertextAndTag.size() < kTagLength) {
            setError_(error, "AES-GCM ciphertext is shorter than its tag");
            return false;
        }

        BCRYPT_KEY_HANDLE aesKey = cachedGcmKey_(key, error); // handle mis en cache (thread-local)
        if (!aesKey) {
            return false;
        }

        const std::size_t ciphertextSize = ciphertextAndTag.size() - kTagLength;
        SwByteArray ciphertext = ciphertextAndTag.mid(0, static_cast<int>(ciphertextSize));
        SwByteArray tag = ciphertextAndTag.mid(static_cast<int>(ciphertextSize), kTagLength);
        SwByteArray plaintext(ciphertextSize, '\0');

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
        authInfo.cbNonce = static_cast<ULONG>(nonce.size());
        authInfo.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char*>(aad.constData()));
        authInfo.cbAuthData = static_cast<ULONG>(aad.size());
        authInfo.pbTag = reinterpret_cast<PUCHAR>(tag.data());
        authInfo.cbTag = static_cast<ULONG>(tag.size());

        ULONG outputSize = 0;
        const NTSTATUS status = BCryptDecrypt(aesKey,
                                              reinterpret_cast<PUCHAR>(ciphertext.data()),
                                              static_cast<ULONG>(ciphertext.size()),
                                              &authInfo,
                                              nullptr,
                                              0,
                                              reinterpret_cast<PUCHAR>(plaintext.data()),
                                              static_cast<ULONG>(plaintext.size()),
                                              &outputSize,
                                              0);
        if (status != 0 || outputSize != plaintext.size()) {
            setError_(error, "BCryptDecrypt(AES-GCM) failed");
            return false;
        }

        outPlaintext = plaintext;
        return true;
#else
        if (ciphertextAndTag.size() < kTagLength) {
            setError_(error, "AES-GCM ciphertext is shorter than its tag");
            return false;
        }
        const std::size_t ciphertextSize = ciphertextAndTag.size() - kTagLength;
        EVP_CIPHER_CTX* ctx = evpGcmDecCtx_();
        if (!ctx) { setError_(error, "EVP_CIPHER_CTX_new failed"); return false; }
        int outLen = 0;
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
            EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                               reinterpret_cast<const unsigned char*>(key.constData()),
                               reinterpret_cast<const unsigned char*>(nonce.constData())) != 1) {
            setError_(error, "EVP_DecryptInit(AES-128-GCM) failed");
            return false;
        }
        if (aad.size() > 0 &&
            EVP_DecryptUpdate(ctx, nullptr, &outLen,
                              reinterpret_cast<const unsigned char*>(aad.constData()),
                              static_cast<int>(aad.size())) != 1) {
            setError_(error, "EVP_DecryptUpdate(AAD) failed");
            return false;
        }
        SwByteArray plaintext(ciphertextSize, '\0');
        if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &outLen,
                              reinterpret_cast<const unsigned char*>(ciphertextAndTag.constData()),
                              static_cast<int>(ciphertextSize)) != 1) {
            setError_(error, "EVP_DecryptUpdate(AES-128-GCM) failed");
            return false;
        }
        // Étiquette d'authentification attendue (les 16 derniers octets).
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagLength),
                                const_cast<char*>(ciphertextAndTag.constData()) + ciphertextSize) != 1) {
            setError_(error, "EVP_CTRL_GCM_SET_TAG failed");
            return false;
        }
        int finalLen = 0;
        if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + outLen, &finalLen) != 1) {
            setError_(error, "AES-128-GCM authentication failed"); // tag invalide (RFC 9001)
            return false;
        }
        outPlaintext = plaintext;
        return true;
#endif
    }

    static bool aes128EcbEncryptBlock_(const SwByteArray& key,
                                       const SwByteArray& block,
                                       SwByteArray& outBlock,
                                       SwString* error) {
#if defined(_WIN32)
        if (block.size() != 16) {
            setError_(error, "AES-ECB header protection sample must be 16 bytes");
            return false;
        }

        BCRYPT_KEY_HANDLE aesKey = cachedEcbKey_(key, error); // handle mis en cache (thread-local)
        if (!aesKey) {
            return false;
        }

        outBlock = SwByteArray(16, '\0');
        ULONG outputSize = 0;
        const NTSTATUS status = BCryptEncrypt(aesKey,
                                              reinterpret_cast<PUCHAR>(const_cast<char*>(block.constData())),
                                              static_cast<ULONG>(block.size()),
                                              nullptr,
                                              nullptr,
                                              0,
                                              reinterpret_cast<PUCHAR>(outBlock.data()),
                                              static_cast<ULONG>(outBlock.size()),
                                              &outputSize,
                                              0);
        if (status != 0 || outputSize != 16) {
            setError_(error, "BCryptEncrypt(AES-ECB) failed");
            return false;
        }
        return true;
#else
        EVP_CIPHER_CTX* ctx = evpEcbCtx_();
        if (!ctx) { setError_(error, "EVP_CIPHER_CTX_new failed"); return false; }
        if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr,
                               reinterpret_cast<const unsigned char*>(key.constData()), nullptr) != 1) {
            setError_(error, "EVP_EncryptInit(AES-128-ECB) failed");
            return false;
        }
        EVP_CIPHER_CTX_set_padding(ctx, 0); // un seul bloc de 16 o, aucun padding (masque de header protection)
        outBlock = SwByteArray(16, '\0');
        int outLen = 0;
        if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(outBlock.data()), &outLen,
                              reinterpret_cast<const unsigned char*>(block.constData()), 16) != 1 || outLen != 16) {
            setError_(error, "EVP_EncryptUpdate(AES-128-ECB) failed");
            return false;
        }
        return true;
#endif
    }

    static SwByteArray nonceForPacketNumber_(const SwByteArray& iv, std::uint64_t packetNumber) {
        SwByteArray nonce = iv;
        SwByteArray packetNumberBytes;
        appendPacketNumber_(packetNumberBytes, packetNumber, 8);

        const std::size_t nonceOffset = nonce.size() - packetNumberBytes.size();
        for (std::size_t i = 0; i < packetNumberBytes.size(); ++i) {
            nonce[nonceOffset + i] =
                static_cast<char>(static_cast<unsigned char>(nonce[nonceOffset + i]) ^
                                  static_cast<unsigned char>(packetNumberBytes[i]));
        }
        return nonce;
    }
};

#endif
