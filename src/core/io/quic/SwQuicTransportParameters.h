#ifndef SWQUICTRANSPORTPARAMETERS_H
#define SWQUICTRANSPORTPARAMETERS_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstdint>

// QUIC transport parameters (RFC 9000 section 18) plus the DATAGRAM extension
// parameter (RFC 9221 section 3). The struct carries the decoded values with
// their RFC defaults; encode()/decode() translate to/from the TLS extension
// body (a sequence of id/length/value entries, all varint encoded).
//
// Unknown parameter ids are ignored on decode, as required by RFC 9000
// section 7.4.2.
class SwQuicTransportParameters {
public:
    // Parameter ids (RFC 9000 section 18.2, RFC 9221 section 3).
    static std::uint64_t idOriginalDestinationConnectionId() { return 0x00; }
    static std::uint64_t idMaxIdleTimeout() { return 0x01; }
    static std::uint64_t idStatelessResetToken() { return 0x02; }
    static std::uint64_t idMaxUdpPayloadSize() { return 0x03; }
    static std::uint64_t idInitialMaxData() { return 0x04; }
    static std::uint64_t idInitialMaxStreamDataBidiLocal() { return 0x05; }
    static std::uint64_t idInitialMaxStreamDataBidiRemote() { return 0x06; }
    static std::uint64_t idInitialMaxStreamDataUni() { return 0x07; }
    static std::uint64_t idInitialMaxStreamsBidi() { return 0x08; }
    static std::uint64_t idInitialMaxStreamsUni() { return 0x09; }
    static std::uint64_t idAckDelayExponent() { return 0x0a; }
    static std::uint64_t idMaxAckDelay() { return 0x0b; }
    static std::uint64_t idDisableActiveMigration() { return 0x0c; }
    static std::uint64_t idActiveConnectionIdLimit() { return 0x0e; }
    static std::uint64_t idInitialSourceConnectionId() { return 0x0f; }
    static std::uint64_t idRetrySourceConnectionId() { return 0x10; }
    static std::uint64_t idMaxDatagramFrameSize() { return 0x20; }

    SwQuicTransportParameters()
        : maxIdleTimeoutMs(0),
          maxUdpPayloadSize(65527),
          initialMaxData(0),
          initialMaxStreamDataBidiLocal(0),
          initialMaxStreamDataBidiRemote(0),
          initialMaxStreamDataUni(0),
          initialMaxStreamsBidi(0),
          initialMaxStreamsUni(0),
          ackDelayExponent(3),
          maxAckDelayMs(25),
          disableActiveMigration(false),
          activeConnectionIdLimit(2),
          maxDatagramFrameSize(0),
          hasOriginalDestinationConnectionId(false),
          hasStatelessResetToken(false),
          hasInitialSourceConnectionId(false),
          hasRetrySourceConnectionId(false) {
    }

    std::uint64_t maxIdleTimeoutMs;
    std::uint64_t maxUdpPayloadSize;
    std::uint64_t initialMaxData;
    std::uint64_t initialMaxStreamDataBidiLocal;
    std::uint64_t initialMaxStreamDataBidiRemote;
    std::uint64_t initialMaxStreamDataUni;
    std::uint64_t initialMaxStreamsBidi;
    std::uint64_t initialMaxStreamsUni;
    std::uint64_t ackDelayExponent;
    std::uint64_t maxAckDelayMs;
    bool disableActiveMigration;
    std::uint64_t activeConnectionIdLimit;
    std::uint64_t maxDatagramFrameSize;

    SwByteArray originalDestinationConnectionId;
    SwByteArray statelessResetToken;
    SwByteArray initialSourceConnectionId;
    SwByteArray retrySourceConnectionId;
    bool hasOriginalDestinationConnectionId;
    bool hasStatelessResetToken;
    bool hasInitialSourceConnectionId;
    bool hasRetrySourceConnectionId;

    bool encode(SwByteArray& outBytes, SwString* error = nullptr) const {
        outBytes.clear();

        if (hasOriginalDestinationConnectionId &&
            !appendBytesParam_(outBytes, idOriginalDestinationConnectionId(),
                               originalDestinationConnectionId, error)) {
            return false;
        }
        if (maxIdleTimeoutMs != 0 &&
            !appendVarIntParam_(outBytes, idMaxIdleTimeout(), maxIdleTimeoutMs, error)) {
            return false;
        }
        if (hasStatelessResetToken &&
            !appendBytesParam_(outBytes, idStatelessResetToken(), statelessResetToken, error)) {
            return false;
        }
        if (maxUdpPayloadSize != 65527 &&
            !appendVarIntParam_(outBytes, idMaxUdpPayloadSize(), maxUdpPayloadSize, error)) {
            return false;
        }
        if (initialMaxData != 0 &&
            !appendVarIntParam_(outBytes, idInitialMaxData(), initialMaxData, error)) {
            return false;
        }
        if (initialMaxStreamDataBidiLocal != 0 &&
            !appendVarIntParam_(outBytes, idInitialMaxStreamDataBidiLocal(),
                                initialMaxStreamDataBidiLocal, error)) {
            return false;
        }
        if (initialMaxStreamDataBidiRemote != 0 &&
            !appendVarIntParam_(outBytes, idInitialMaxStreamDataBidiRemote(),
                                initialMaxStreamDataBidiRemote, error)) {
            return false;
        }
        if (initialMaxStreamDataUni != 0 &&
            !appendVarIntParam_(outBytes, idInitialMaxStreamDataUni(),
                                initialMaxStreamDataUni, error)) {
            return false;
        }
        if (initialMaxStreamsBidi != 0 &&
            !appendVarIntParam_(outBytes, idInitialMaxStreamsBidi(),
                                initialMaxStreamsBidi, error)) {
            return false;
        }
        if (initialMaxStreamsUni != 0 &&
            !appendVarIntParam_(outBytes, idInitialMaxStreamsUni(),
                                initialMaxStreamsUni, error)) {
            return false;
        }
        if (ackDelayExponent != 3 &&
            !appendVarIntParam_(outBytes, idAckDelayExponent(), ackDelayExponent, error)) {
            return false;
        }
        if (maxAckDelayMs != 25 &&
            !appendVarIntParam_(outBytes, idMaxAckDelay(), maxAckDelayMs, error)) {
            return false;
        }
        if (disableActiveMigration &&
            !appendEmptyParam_(outBytes, idDisableActiveMigration(), error)) {
            return false;
        }
        if (activeConnectionIdLimit != 2 &&
            !appendVarIntParam_(outBytes, idActiveConnectionIdLimit(),
                                activeConnectionIdLimit, error)) {
            return false;
        }
        if (hasInitialSourceConnectionId &&
            !appendBytesParam_(outBytes, idInitialSourceConnectionId(),
                               initialSourceConnectionId, error)) {
            return false;
        }
        if (hasRetrySourceConnectionId &&
            !appendBytesParam_(outBytes, idRetrySourceConnectionId(),
                               retrySourceConnectionId, error)) {
            return false;
        }
        if (maxDatagramFrameSize != 0 &&
            !appendVarIntParam_(outBytes, idMaxDatagramFrameSize(),
                                maxDatagramFrameSize, error)) {
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool decode(const SwByteArray& bytes,
                       SwQuicTransportParameters& outParameters,
                       SwString* error = nullptr) {
        SwQuicTransportParameters parameters;
        std::size_t offset = 0;

        while (offset < bytes.size()) {
            std::uint64_t id = 0;
            std::uint64_t length = 0;
            if (!SwQuicVarIntCodec::decode(bytes, offset, id, error) ||
                !SwQuicVarIntCodec::decode(bytes, offset, length, error)) {
                return false;
            }
            if (length > static_cast<std::uint64_t>(bytes.size() - offset)) {
                setError_(error, "QUIC transport parameter value is truncated");
                return false;
            }

            const SwByteArray value = bytes.mid(static_cast<int>(offset),
                                                static_cast<int>(length));
            offset += static_cast<std::size_t>(length);

            if (!parameters.applyDecoded_(id, value, error)) {
                return false;
            }
        }

        outParameters = parameters;
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

    bool applyDecoded_(std::uint64_t id, const SwByteArray& value, SwString* error) {
        if (id == idOriginalDestinationConnectionId()) {
            originalDestinationConnectionId = value;
            hasOriginalDestinationConnectionId = true;
            return true;
        }
        if (id == idStatelessResetToken()) {
            if (value.size() != 16) {
                setError_(error, "QUIC stateless reset token must be 16 bytes");
                return false;
            }
            statelessResetToken = value;
            hasStatelessResetToken = true;
            return true;
        }
        if (id == idInitialSourceConnectionId()) {
            initialSourceConnectionId = value;
            hasInitialSourceConnectionId = true;
            return true;
        }
        if (id == idRetrySourceConnectionId()) {
            retrySourceConnectionId = value;
            hasRetrySourceConnectionId = true;
            return true;
        }
        if (id == idDisableActiveMigration()) {
            if (!value.isEmpty()) {
                setError_(error, "QUIC disable_active_migration must be empty");
                return false;
            }
            disableActiveMigration = true;
            return true;
        }

        std::uint64_t* target = varIntTarget_(id);
        if (!target) {
            return true; // unknown parameter: ignore (RFC 9000 section 7.4.2)
        }

        std::size_t valueOffset = 0;
        std::uint64_t decoded = 0;
        if (!SwQuicVarIntCodec::decode(value, valueOffset, decoded, error)) {
            return false;
        }
        if (valueOffset != value.size()) {
            setError_(error, "QUIC transport parameter has trailing bytes");
            return false;
        }

        if (id == idAckDelayExponent() && decoded > 20) {
            setError_(error, "QUIC ack_delay_exponent must be at most 20");
            return false;
        }
        if (id == idMaxAckDelay() && decoded >= 16384) {
            setError_(error, "QUIC max_ack_delay must be below 2^14");
            return false;
        }
        if (id == idActiveConnectionIdLimit() && decoded < 2) {
            setError_(error, "QUIC active_connection_id_limit must be at least 2");
            return false;
        }
        if (id == idMaxUdpPayloadSize() && decoded < 1200) {
            setError_(error, "QUIC max_udp_payload_size must be at least 1200");
            return false;
        }

        *target = decoded;
        return true;
    }

    std::uint64_t* varIntTarget_(std::uint64_t id) {
        if (id == idMaxIdleTimeout()) return &maxIdleTimeoutMs;
        if (id == idMaxUdpPayloadSize()) return &maxUdpPayloadSize;
        if (id == idInitialMaxData()) return &initialMaxData;
        if (id == idInitialMaxStreamDataBidiLocal()) return &initialMaxStreamDataBidiLocal;
        if (id == idInitialMaxStreamDataBidiRemote()) return &initialMaxStreamDataBidiRemote;
        if (id == idInitialMaxStreamDataUni()) return &initialMaxStreamDataUni;
        if (id == idInitialMaxStreamsBidi()) return &initialMaxStreamsBidi;
        if (id == idInitialMaxStreamsUni()) return &initialMaxStreamsUni;
        if (id == idAckDelayExponent()) return &ackDelayExponent;
        if (id == idMaxAckDelay()) return &maxAckDelayMs;
        if (id == idActiveConnectionIdLimit()) return &activeConnectionIdLimit;
        if (id == idMaxDatagramFrameSize()) return &maxDatagramFrameSize;
        return nullptr;
    }

    static bool appendVarIntParam_(SwByteArray& out,
                                   std::uint64_t id,
                                   std::uint64_t value,
                                   SwString* error) {
        SwByteArray encodedValue;
        if (!SwQuicVarIntCodec::encode(value, encodedValue, error)) {
            return false;
        }
        return appendBytesParam_(out, id, encodedValue, error);
    }

    static bool appendEmptyParam_(SwByteArray& out, std::uint64_t id, SwString* error) {
        return appendBytesParam_(out, id, SwByteArray(), error);
    }

    static bool appendBytesParam_(SwByteArray& out,
                                  std::uint64_t id,
                                  const SwByteArray& value,
                                  SwString* error) {
        if (!SwQuicVarIntCodec::encode(id, out, error) ||
            !SwQuicVarIntCodec::encode(static_cast<std::uint64_t>(value.size()), out, error)) {
            return false;
        }
        out.append(value);
        return true;
    }
};

#endif
