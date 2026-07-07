#ifndef SWQUICPACKETHEADER_H
#define SWQUICPACKETHEADER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicConnectionId.h"

#include <cstdint>

class SwQuicPacketHeader {
public:
    enum class Form {
        Long,
        Short
    };

    enum class LongPacketType {
        Initial,
        ZeroRtt,
        Handshake,
        Retry,
        VersionNegotiation,
        Unknown
    };

    SwQuicPacketHeader()
        : m_form(Form::Long),
          m_longPacketType(LongPacketType::Initial),
          m_version(1),
          m_payloadLength(0),
          m_packetNumber(0),
          m_packetNumberLength(1) {
    }

    static SwQuicPacketHeader makeInitial(const SwQuicConnectionId& destinationConnectionId,
                                          const SwQuicConnectionId& sourceConnectionId) {
        SwQuicPacketHeader header;
        header.setForm(Form::Long);
        header.setLongPacketType(LongPacketType::Initial);
        header.setDestinationConnectionId(destinationConnectionId);
        header.setSourceConnectionId(sourceConnectionId);
        return header;
    }

    Form form() const { return m_form; }
    void setForm(Form form) { m_form = form; }

    LongPacketType longPacketType() const { return m_longPacketType; }
    void setLongPacketType(LongPacketType type) { m_longPacketType = type; }

    std::uint32_t version() const { return m_version; }
    void setVersion(std::uint32_t version) { m_version = version; }

    const SwQuicConnectionId& destinationConnectionId() const { return m_destinationConnectionId; }
    void setDestinationConnectionId(const SwQuicConnectionId& connectionId) {
        m_destinationConnectionId = connectionId;
    }

    const SwQuicConnectionId& sourceConnectionId() const { return m_sourceConnectionId; }
    void setSourceConnectionId(const SwQuicConnectionId& connectionId) {
        m_sourceConnectionId = connectionId;
    }

    const SwByteArray& token() const { return m_token; }
    void setToken(const SwByteArray& token) { m_token = token; }

    std::uint64_t payloadLength() const { return m_payloadLength; }
    void setPayloadLength(std::uint64_t payloadLength) { m_payloadLength = payloadLength; }

    std::uint64_t packetNumber() const { return m_packetNumber; }
    bool setPacketNumber(std::uint64_t packetNumber, SwString* error = nullptr) {
        if (packetNumber > maxPacketNumberForLength_(m_packetNumberLength)) {
            setError_(error, "QUIC packet number does not fit packet number length");
            return false;
        }

        m_packetNumber = packetNumber;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // For decoded packets whose truncated wire packet number was expanded to
    // the full value (RFC 9000 appendix A.3): the full number may exceed what
    // the wire length can carry, so no fit check applies here. Encoding paths
    // must keep using setPacketNumber().
    void setDecodedPacketNumber(std::uint64_t packetNumber) {
        m_packetNumber = packetNumber;
    }

    std::uint8_t packetNumberLength() const { return m_packetNumberLength; }
    bool setPacketNumberLength(std::uint8_t packetNumberLength, SwString* error = nullptr) {
        if (packetNumberLength < 1 || packetNumberLength > 4) {
            setError_(error, "QUIC packet number length must be between 1 and 4 bytes");
            return false;
        }

        if (m_packetNumber > maxPacketNumberForLength_(packetNumberLength)) {
            setError_(error, "QUIC packet number does not fit requested length");
            return false;
        }

        m_packetNumberLength = packetNumberLength;
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

    static std::uint64_t maxPacketNumberForLength_(std::uint8_t packetNumberLength) {
        if (packetNumberLength >= 8) {
            return 0xffffffffffffffffULL;
        }
        return (1ULL << (packetNumberLength * 8)) - 1ULL;
    }

    Form m_form;
    LongPacketType m_longPacketType;
    std::uint32_t m_version;
    SwQuicConnectionId m_destinationConnectionId;
    SwQuicConnectionId m_sourceConnectionId;
    SwByteArray m_token;
    std::uint64_t m_payloadLength;
    std::uint64_t m_packetNumber;
    std::uint8_t m_packetNumberLength;
};

#endif
