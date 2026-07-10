#ifndef SWQUICFRAMECODEC_H
#define SWQUICFRAMECODEC_H

#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

class SwQuicFrameCodec {
public:
    static bool encodeFrame(const SwQuicFrame& frame,
                            SwByteArray& outPayload,
                            SwString* error = nullptr) {
        switch (frame.type()) {
        case SwQuicFrame::Type::Padding:
            for (std::size_t i = 0; i < frame.paddingLength(); ++i) {
                appendByte_(outPayload, 0x00);
            }
            break;
        case SwQuicFrame::Type::Ping:
            appendByte_(outPayload, 0x01);
            break;
        case SwQuicFrame::Type::Ack:
            if (!encodeAck_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::ResetStream:
            if (!encodeResetStream_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::StopSending:
            if (!encodeStopSending_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::Crypto:
            if (!encodeCrypto_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::NewToken:
            if (!encodeNewToken_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::Stream:
            if (!encodeStream_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::MaxData:
            appendByte_(outPayload, 0x10);
            if (!appendVarInt_(frame.maximum(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::MaxStreamData:
            appendByte_(outPayload, 0x11);
            if (!appendVarInt_(frame.streamId(), outPayload, error) ||
                !appendVarInt_(frame.maximum(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::MaxStreams:
            appendByte_(outPayload, frame.isBidirectional() ? 0x12 : 0x13);
            if (!appendVarInt_(frame.maximum(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::DataBlocked:
            appendByte_(outPayload, 0x14);
            if (!appendVarInt_(frame.maximum(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::StreamDataBlocked:
            appendByte_(outPayload, 0x15);
            if (!appendVarInt_(frame.streamId(), outPayload, error) ||
                !appendVarInt_(frame.maximum(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::StreamsBlocked:
            appendByte_(outPayload, frame.isBidirectional() ? 0x16 : 0x17);
            if (!appendVarInt_(frame.maximum(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::NewConnectionId:
            if (!encodeNewConnectionId_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::RetireConnectionId:
            appendByte_(outPayload, 0x19);
            if (!appendVarInt_(frame.sequenceNumber(), outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::PathChallenge:
        case SwQuicFrame::Type::PathResponse:
            if (!encodePathData_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::ConnectionClose:
            if (!encodeConnectionClose_(frame, outPayload, error)) {
                return false;
            }
            break;
        case SwQuicFrame::Type::HandshakeDone:
            appendByte_(outPayload, 0x1e);
            break;
        case SwQuicFrame::Type::Datagram:
            if (!encodeDatagram_(frame, outPayload, error)) {
                return false;
            }
            break;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool decodeFrame(const SwByteArray& payload,
                            std::size_t& offset,
                            SwQuicFrame& outFrame,
                            SwString* error = nullptr) {
        std::uint8_t frameType = 0;
        if (!readByte_(payload, offset, frameType, error)) {
            return false;
        }

        switch (frameType) {
        case 0x00:
            outFrame = SwQuicFrame::padding();
            return true;
        case 0x01:
            outFrame = SwQuicFrame::ping();
            return true;
        case 0x02:
        case 0x03:
            return decodeAck_(frameType == 0x03, payload, offset, outFrame, error);
        case 0x04:
            return decodeResetStream_(payload, offset, outFrame, error);
        case 0x05:
            return decodeStopSending_(payload, offset, outFrame, error);
        case 0x06:
            return decodeCrypto_(payload, offset, outFrame, error);
        case 0x07:
            return decodeNewToken_(payload, offset, outFrame, error);
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            return decodeStream_(frameType, payload, offset, outFrame, error);
        case 0x10:
            return decodeMaxData_(payload, offset, outFrame, error);
        case 0x11:
            return decodeMaxStreamData_(payload, offset, outFrame, error);
        case 0x12:
        case 0x13:
            return decodeMaxStreams_(frameType == 0x12, payload, offset, outFrame, error);
        case 0x14:
            return decodeDataBlocked_(payload, offset, outFrame, error);
        case 0x15:
            return decodeStreamDataBlocked_(payload, offset, outFrame, error);
        case 0x16:
        case 0x17:
            return decodeStreamsBlocked_(frameType == 0x16, payload, offset, outFrame, error);
        case 0x18:
            return decodeNewConnectionId_(payload, offset, outFrame, error);
        case 0x19:
            return decodeRetireConnectionId_(payload, offset, outFrame, error);
        case 0x1a:
            return decodePathData_(false, payload, offset, outFrame, error);
        case 0x1b:
            return decodePathData_(true, payload, offset, outFrame, error);
        case 0x1c:
        case 0x1d:
            return decodeConnectionClose_(frameType == 0x1d, payload, offset, outFrame, error);
        case 0x1e:
            outFrame = SwQuicFrame::handshakeDone();
            return true;
        case 0x30:
        case 0x31:
            return decodeDatagram_(frameType == 0x31, payload, offset, outFrame, error);
        default:
            break;
        }

        setError_(error, "Unsupported QUIC frame type");
        return false;
    }

    static bool encodeFrames(const SwVector<SwQuicFrame>& frames,
                             SwByteArray& outPayload,
                             SwString* error = nullptr) {
        outPayload.clear();
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (!encodeFrame(frames[i], outPayload, error)) {
                return false;
            }
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool decodeFrames(const SwByteArray& payload,
                             SwVector<SwQuicFrame>& outFrames,
                             SwString* error = nullptr) {
        outFrames.clear();
        std::size_t offset = 0;
        while (offset < payload.size()) {
            // PADDING is commonly hundreds of consecutive zero bytes in a
            // 1200-byte Initial. Preserve its exact wire length in one frame
            // instead of allocating one heavyweight SwQuicFrame per byte.
            if (static_cast<std::uint8_t>(payload.constData()[offset]) == 0) {
                const std::size_t start = offset;
                do {
                    ++offset;
                } while (offset < payload.size() &&
                         static_cast<std::uint8_t>(payload.constData()[offset]) == 0);
                outFrames.push_back(SwQuicFrame::padding(offset - start));
                continue;
            }
            SwQuicFrame frame = SwQuicFrame::padding();
            if (!decodeFrame(payload, offset, frame, error)) {
                return false;
            }
            outFrames.push_back(std::move(frame));
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

    static bool readByte_(const SwByteArray& bytes,
                          std::size_t& offset,
                          std::uint8_t& outValue,
                          SwString* error) {
        if (offset >= bytes.size()) {
            setError_(error, "QUIC frame is truncated");
            return false;
        }

        outValue = static_cast<std::uint8_t>(bytes.constData()[offset]);
        ++offset;
        return true;
    }

    static bool appendVarInt_(std::uint64_t value, SwByteArray& outPayload, SwString* error) {
        return SwQuicVarIntCodec::encode(value, outPayload, error);
    }

    static bool readVarInt_(const SwByteArray& payload,
                            std::size_t& offset,
                            std::uint64_t& outValue,
                            SwString* error) {
        return SwQuicVarIntCodec::decode(payload, offset, outValue, error);
    }

    static bool ensureSizeFits_(std::uint64_t value, SwString* error) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            setError_(error, "QUIC frame field is too large for SwByteArray slicing");
            return false;
        }
        return true;
    }

    static bool readSlice_(const SwByteArray& payload,
                           std::size_t& offset,
                           std::uint64_t length,
                           SwByteArray& outSlice,
                           SwString* error) {
        if (!ensureSizeFits_(length, error)) {
            return false;
        }
        if (length > static_cast<std::uint64_t>(payload.size() - offset)) {
            setError_(error, "QUIC frame field is truncated");
            return false;
        }

        outSlice = payload.mid(static_cast<int>(offset), static_cast<int>(length));
        offset += static_cast<std::size_t>(length);
        return true;
    }

    static bool encodeAck_(const SwQuicFrame& frame, SwByteArray& outPayload, SwString* error) {
        appendByte_(outPayload, frame.hasEcn() ? 0x03 : 0x02);
        if (!appendVarInt_(frame.largestAcknowledged(), outPayload, error) ||
            !appendVarInt_(frame.ackDelay(), outPayload, error) ||
            !appendVarInt_(static_cast<std::uint64_t>(frame.ackRanges().size()), outPayload, error) ||
            !appendVarInt_(frame.firstAckRange(), outPayload, error)) {
            return false;
        }

        for (std::size_t i = 0; i < frame.ackRanges().size(); ++i) {
            if (!appendVarInt_(frame.ackRanges()[i].gap, outPayload, error) ||
                !appendVarInt_(frame.ackRanges()[i].rangeLength, outPayload, error)) {
                return false;
            }
        }

        if (frame.hasEcn()) {
            if (!appendVarInt_(frame.ect0Count(), outPayload, error) ||
                !appendVarInt_(frame.ect1Count(), outPayload, error) ||
                !appendVarInt_(frame.ecnCeCount(), outPayload, error)) {
                return false;
            }
        }
        return true;
    }

    static bool encodeResetStream_(const SwQuicFrame& frame,
                                   SwByteArray& outPayload,
                                   SwString* error) {
        appendByte_(outPayload, 0x04);
        return appendVarInt_(frame.streamId(), outPayload, error) &&
               appendVarInt_(frame.errorCode(), outPayload, error) &&
               appendVarInt_(frame.finalSize(), outPayload, error);
    }

    static bool encodeStopSending_(const SwQuicFrame& frame,
                                   SwByteArray& outPayload,
                                   SwString* error) {
        appendByte_(outPayload, 0x05);
        return appendVarInt_(frame.streamId(), outPayload, error) &&
               appendVarInt_(frame.errorCode(), outPayload, error);
    }

    static bool encodeCrypto_(const SwQuicFrame& frame, SwByteArray& outPayload, SwString* error) {
        appendByte_(outPayload, 0x06);
        return appendVarInt_(frame.offset(), outPayload, error) &&
               appendVarInt_(static_cast<std::uint64_t>(frame.data().size()), outPayload, error) &&
               (outPayload.append(frame.data()), true);
    }

    static bool encodeNewToken_(const SwQuicFrame& frame, SwByteArray& outPayload, SwString* error) {
        appendByte_(outPayload, 0x07);
        return appendVarInt_(static_cast<std::uint64_t>(frame.data().size()), outPayload, error) &&
               (outPayload.append(frame.data()), true);
    }

    static bool encodeStream_(const SwQuicFrame& frame, SwByteArray& outPayload, SwString* error) {
        std::uint8_t frameType = 0x08 | 0x02;
        if (frame.offset() > 0) {
            frameType = static_cast<std::uint8_t>(frameType | 0x04);
        }
        if (frame.fin()) {
            frameType = static_cast<std::uint8_t>(frameType | 0x01);
        }

        appendByte_(outPayload, frameType);
        if (!appendVarInt_(frame.streamId(), outPayload, error)) {
            return false;
        }
        if (frame.offset() > 0 && !appendVarInt_(frame.offset(), outPayload, error)) {
            return false;
        }
        return appendVarInt_(static_cast<std::uint64_t>(frame.data().size()), outPayload, error) &&
               (outPayload.append(frame.data()), true);
    }

    static bool encodeNewConnectionId_(const SwQuicFrame& frame,
                                       SwByteArray& outPayload,
                                       SwString* error) {
        if (frame.connectionId().size() < 1 || frame.connectionId().size() > 20) {
            setError_(error, "QUIC NEW_CONNECTION_ID length is invalid");
            return false;
        }
        if (frame.statelessResetToken().size() != 16) {
            setError_(error, "QUIC NEW_CONNECTION_ID stateless reset token must be 16 bytes");
            return false;
        }

        appendByte_(outPayload, 0x18);
        if (!appendVarInt_(frame.sequenceNumber(), outPayload, error) ||
            !appendVarInt_(frame.retirePriorTo(), outPayload, error)) {
            return false;
        }
        appendByte_(outPayload, static_cast<std::uint8_t>(frame.connectionId().size()));
        outPayload.append(frame.connectionId());
        outPayload.append(frame.statelessResetToken());
        return true;
    }

    static bool encodePathData_(const SwQuicFrame& frame, SwByteArray& outPayload, SwString* error) {
        if (frame.data().size() != 8) {
            setError_(error, "QUIC PATH_CHALLENGE/PATH_RESPONSE must carry 8 bytes");
            return false;
        }
        appendByte_(outPayload, frame.type() == SwQuicFrame::Type::PathResponse ? 0x1b : 0x1a);
        outPayload.append(frame.data());
        return true;
    }

    static bool encodeConnectionClose_(const SwQuicFrame& frame,
                                       SwByteArray& outPayload,
                                       SwString* error) {
        appendByte_(outPayload, frame.isApplicationClose() ? 0x1d : 0x1c);
        if (!appendVarInt_(frame.errorCode(), outPayload, error)) {
            return false;
        }

        if (!frame.isApplicationClose() &&
            !appendVarInt_(frame.errorFrameType(), outPayload, error)) {
            return false;
        }

        const SwString& reasonPhrase = frame.reasonPhrase();
        const SwByteArray reason(reasonPhrase.constData(), reasonPhrase.size());
        return appendVarInt_(static_cast<std::uint64_t>(reason.size()), outPayload, error) &&
               (outPayload.append(reason), true);
    }

    static bool encodeDatagram_(const SwQuicFrame& frame, SwByteArray& outPayload, SwString* error) {
        appendByte_(outPayload, 0x31);
        return appendVarInt_(static_cast<std::uint64_t>(frame.data().size()), outPayload, error) &&
               (outPayload.append(frame.data()), true);
    }

    static bool decodeAck_(bool hasEcn,
                           const SwByteArray& payload,
                           std::size_t& offset,
                           SwQuicFrame& outFrame,
                           SwString* error) {
        std::uint64_t largestAcknowledged = 0;
        std::uint64_t ackDelay = 0;
        std::uint64_t ackRangeCount = 0;
        std::uint64_t firstAckRange = 0;
        if (!readVarInt_(payload, offset, largestAcknowledged, error) ||
            !readVarInt_(payload, offset, ackDelay, error) ||
            !readVarInt_(payload, offset, ackRangeCount, error) ||
            !readVarInt_(payload, offset, firstAckRange, error)) {
            return false;
        }

        if (ackRangeCount > 8192) {
            setError_(error, "QUIC ACK frame has too many ranges for this decoder");
            return false;
        }

        SwVector<SwQuicFrame::AckRange> ranges;
        for (std::uint64_t i = 0; i < ackRangeCount; ++i) {
            SwQuicFrame::AckRange range = {0, 0};
            if (!readVarInt_(payload, offset, range.gap, error) ||
                !readVarInt_(payload, offset, range.rangeLength, error)) {
                return false;
            }
            ranges.push_back(range);
        }

        if (!hasEcn) {
            outFrame = SwQuicFrame::ack(largestAcknowledged, ackDelay, firstAckRange, ranges);
            return true;
        }

        std::uint64_t ect0 = 0;
        std::uint64_t ect1 = 0;
        std::uint64_t ecnCe = 0;
        if (!readVarInt_(payload, offset, ect0, error) ||
            !readVarInt_(payload, offset, ect1, error) ||
            !readVarInt_(payload, offset, ecnCe, error)) {
            return false;
        }

        outFrame = SwQuicFrame::ackWithEcn(largestAcknowledged, ackDelay, firstAckRange, ranges,
                                           ect0, ect1, ecnCe);
        return true;
    }

    static bool decodeResetStream_(const SwByteArray& payload,
                                   std::size_t& offset,
                                   SwQuicFrame& outFrame,
                                   SwString* error) {
        std::uint64_t streamId = 0;
        std::uint64_t errorCode = 0;
        std::uint64_t finalSize = 0;
        if (!readVarInt_(payload, offset, streamId, error) ||
            !readVarInt_(payload, offset, errorCode, error) ||
            !readVarInt_(payload, offset, finalSize, error)) {
            return false;
        }
        outFrame = SwQuicFrame::resetStream(streamId, errorCode, finalSize);
        return true;
    }

    static bool decodeStopSending_(const SwByteArray& payload,
                                   std::size_t& offset,
                                   SwQuicFrame& outFrame,
                                   SwString* error) {
        std::uint64_t streamId = 0;
        std::uint64_t errorCode = 0;
        if (!readVarInt_(payload, offset, streamId, error) ||
            !readVarInt_(payload, offset, errorCode, error)) {
            return false;
        }
        outFrame = SwQuicFrame::stopSending(streamId, errorCode);
        return true;
    }

    static bool decodeCrypto_(const SwByteArray& payload,
                              std::size_t& offset,
                              SwQuicFrame& outFrame,
                              SwString* error) {
        std::uint64_t frameOffset = 0;
        std::uint64_t length = 0;
        if (!readVarInt_(payload, offset, frameOffset, error) ||
            !readVarInt_(payload, offset, length, error)) {
            return false;
        }

        SwByteArray data;
        if (!readSlice_(payload, offset, length, data, error)) {
            return false;
        }

        outFrame = SwQuicFrame::crypto(frameOffset, data);
        return true;
    }

    static bool decodeNewToken_(const SwByteArray& payload,
                                std::size_t& offset,
                                SwQuicFrame& outFrame,
                                SwString* error) {
        std::uint64_t length = 0;
        if (!readVarInt_(payload, offset, length, error)) {
            return false;
        }

        SwByteArray token;
        if (!readSlice_(payload, offset, length, token, error)) {
            return false;
        }

        outFrame = SwQuicFrame::newToken(token);
        return true;
    }

    static bool decodeStream_(std::uint8_t frameType,
                              const SwByteArray& payload,
                              std::size_t& offset,
                              SwQuicFrame& outFrame,
                              SwString* error) {
        const bool hasOffset = (frameType & 0x04) != 0;
        const bool hasLength = (frameType & 0x02) != 0;
        const bool fin = (frameType & 0x01) != 0;

        std::uint64_t streamId = 0;
        std::uint64_t streamOffset = 0;
        std::uint64_t length = 0;
        if (!readVarInt_(payload, offset, streamId, error)) {
            return false;
        }
        if (hasOffset && !readVarInt_(payload, offset, streamOffset, error)) {
            return false;
        }
        if (hasLength) {
            if (!readVarInt_(payload, offset, length, error)) {
                return false;
            }
        } else {
            length = static_cast<std::uint64_t>(payload.size() - offset);
        }

        SwByteArray data;
        if (!readSlice_(payload, offset, length, data, error)) {
            return false;
        }

        outFrame = SwQuicFrame::stream(streamId, streamOffset, data, fin);
        return true;
    }

    static bool decodeMaxData_(const SwByteArray& payload,
                               std::size_t& offset,
                               SwQuicFrame& outFrame,
                               SwString* error) {
        std::uint64_t maximum = 0;
        if (!readVarInt_(payload, offset, maximum, error)) {
            return false;
        }
        outFrame = SwQuicFrame::maxData(maximum);
        return true;
    }

    static bool decodeMaxStreamData_(const SwByteArray& payload,
                                     std::size_t& offset,
                                     SwQuicFrame& outFrame,
                                     SwString* error) {
        std::uint64_t streamId = 0;
        std::uint64_t maximum = 0;
        if (!readVarInt_(payload, offset, streamId, error) ||
            !readVarInt_(payload, offset, maximum, error)) {
            return false;
        }
        outFrame = SwQuicFrame::maxStreamData(streamId, maximum);
        return true;
    }

    static bool decodeMaxStreams_(bool bidirectional,
                                  const SwByteArray& payload,
                                  std::size_t& offset,
                                  SwQuicFrame& outFrame,
                                  SwString* error) {
        std::uint64_t maximum = 0;
        if (!readVarInt_(payload, offset, maximum, error)) {
            return false;
        }
        outFrame = SwQuicFrame::maxStreams(bidirectional, maximum);
        return true;
    }

    static bool decodeDataBlocked_(const SwByteArray& payload,
                                   std::size_t& offset,
                                   SwQuicFrame& outFrame,
                                   SwString* error) {
        std::uint64_t limit = 0;
        if (!readVarInt_(payload, offset, limit, error)) {
            return false;
        }
        outFrame = SwQuicFrame::dataBlocked(limit);
        return true;
    }

    static bool decodeStreamDataBlocked_(const SwByteArray& payload,
                                         std::size_t& offset,
                                         SwQuicFrame& outFrame,
                                         SwString* error) {
        std::uint64_t streamId = 0;
        std::uint64_t limit = 0;
        if (!readVarInt_(payload, offset, streamId, error) ||
            !readVarInt_(payload, offset, limit, error)) {
            return false;
        }
        outFrame = SwQuicFrame::streamDataBlocked(streamId, limit);
        return true;
    }

    static bool decodeStreamsBlocked_(bool bidirectional,
                                      const SwByteArray& payload,
                                      std::size_t& offset,
                                      SwQuicFrame& outFrame,
                                      SwString* error) {
        std::uint64_t limit = 0;
        if (!readVarInt_(payload, offset, limit, error)) {
            return false;
        }
        outFrame = SwQuicFrame::streamsBlocked(bidirectional, limit);
        return true;
    }

    static bool decodeNewConnectionId_(const SwByteArray& payload,
                                       std::size_t& offset,
                                       SwQuicFrame& outFrame,
                                       SwString* error) {
        std::uint64_t sequenceNumber = 0;
        std::uint64_t retirePriorTo = 0;
        if (!readVarInt_(payload, offset, sequenceNumber, error) ||
            !readVarInt_(payload, offset, retirePriorTo, error)) {
            return false;
        }
        if (retirePriorTo > sequenceNumber) {
            setError_(error, "QUIC NEW_CONNECTION_ID retires a future sequence number");
            return false;
        }

        std::uint8_t length = 0;
        if (!readByte_(payload, offset, length, error)) {
            return false;
        }
        if (length < 1 || length > 20) {
            setError_(error, "QUIC NEW_CONNECTION_ID length is invalid");
            return false;
        }

        SwByteArray connectionId;
        SwByteArray statelessResetToken;
        if (!readSlice_(payload, offset, length, connectionId, error) ||
            !readSlice_(payload, offset, 16, statelessResetToken, error)) {
            return false;
        }

        outFrame = SwQuicFrame::newConnectionId(sequenceNumber, retirePriorTo, connectionId,
                                                statelessResetToken);
        return true;
    }

    static bool decodeRetireConnectionId_(const SwByteArray& payload,
                                          std::size_t& offset,
                                          SwQuicFrame& outFrame,
                                          SwString* error) {
        std::uint64_t sequenceNumber = 0;
        if (!readVarInt_(payload, offset, sequenceNumber, error)) {
            return false;
        }
        outFrame = SwQuicFrame::retireConnectionId(sequenceNumber);
        return true;
    }

    static bool decodePathData_(bool response,
                                const SwByteArray& payload,
                                std::size_t& offset,
                                SwQuicFrame& outFrame,
                                SwString* error) {
        SwByteArray data;
        if (!readSlice_(payload, offset, 8, data, error)) {
            return false;
        }
        outFrame = response ? SwQuicFrame::pathResponse(data) : SwQuicFrame::pathChallenge(data);
        return true;
    }

    static bool decodeConnectionClose_(bool applicationClose,
                                       const SwByteArray& payload,
                                       std::size_t& offset,
                                       SwQuicFrame& outFrame,
                                       SwString* error) {
        std::uint64_t errorCode = 0;
        std::uint64_t frameType = 0;
        std::uint64_t reasonLength = 0;
        if (!readVarInt_(payload, offset, errorCode, error)) {
            return false;
        }
        if (!applicationClose && !readVarInt_(payload, offset, frameType, error)) {
            return false;
        }
        if (!readVarInt_(payload, offset, reasonLength, error)) {
            return false;
        }

        SwByteArray reasonBytes;
        if (!readSlice_(payload, offset, reasonLength, reasonBytes, error)) {
            return false;
        }

        const SwString reason(reasonBytes);
        outFrame = applicationClose ? SwQuicFrame::applicationClose(errorCode, reason)
                                    : SwQuicFrame::connectionClose(errorCode, frameType, reason);
        return true;
    }

    static bool decodeDatagram_(bool hasLength,
                                const SwByteArray& payload,
                                std::size_t& offset,
                                SwQuicFrame& outFrame,
                                SwString* error) {
        std::uint64_t length = static_cast<std::uint64_t>(payload.size() - offset);
        if (hasLength && !readVarInt_(payload, offset, length, error)) {
            return false;
        }

        SwByteArray data;
        if (!readSlice_(payload, offset, length, data, error)) {
            return false;
        }

        outFrame = SwQuicFrame::datagram(std::move(data));
        return true;
    }
};

#endif
