#ifndef SWQUICFRAME_H
#define SWQUICFRAME_H

#include "SwVector.h"
#include "SwByteArray.h"
#include "SwString.h"

#include <cstdint>
#include <utility>
#include <vector>

class SwQuicFrame {
public:
    enum class Type {
        Padding,
        Ping,
        Ack,
        ResetStream,
        StopSending,
        Crypto,
        NewToken,
        Stream,
        MaxData,
        MaxStreamData,
        MaxStreams,
        DataBlocked,
        StreamDataBlocked,
        StreamsBlocked,
        NewConnectionId,
        RetireConnectionId,
        PathChallenge,
        PathResponse,
        ConnectionClose,
        HandshakeDone,
        Datagram
    };

    struct AckRange {
        std::uint64_t gap;
        std::uint64_t rangeLength;
    };

    static SwQuicFrame padding() {
        return SwQuicFrame(Type::Padding);
    }

    static SwQuicFrame ping() {
        return SwQuicFrame(Type::Ping);
    }

    static SwQuicFrame ack(std::uint64_t largestAcknowledged,
                           std::uint64_t ackDelay,
                           std::uint64_t firstAckRange,
                           const SwVector<AckRange>& ackRanges = SwVector<AckRange>()) {
        SwQuicFrame frame(Type::Ack);
        frame.m_largestAcknowledged = largestAcknowledged;
        frame.m_ackDelay = ackDelay;
        frame.m_firstAckRange = firstAckRange;
        frame.m_ackRanges = ackRanges;
        return frame;
    }

    static SwQuicFrame ackWithEcn(std::uint64_t largestAcknowledged,
                                  std::uint64_t ackDelay,
                                  std::uint64_t firstAckRange,
                                  const SwVector<AckRange>& ackRanges,
                                  std::uint64_t ect0Count,
                                  std::uint64_t ect1Count,
                                  std::uint64_t ecnCeCount) {
        SwQuicFrame frame = ack(largestAcknowledged, ackDelay, firstAckRange, ackRanges);
        frame.m_hasEcn = true;
        frame.m_ect0Count = ect0Count;
        frame.m_ect1Count = ect1Count;
        frame.m_ecnCeCount = ecnCeCount;
        return frame;
    }

    static SwQuicFrame resetStream(std::uint64_t streamId,
                                   std::uint64_t applicationErrorCode,
                                   std::uint64_t finalSize) {
        SwQuicFrame frame(Type::ResetStream);
        frame.m_streamId = streamId;
        frame.m_errorCode = applicationErrorCode;
        frame.m_finalSize = finalSize;
        return frame;
    }

    static SwQuicFrame stopSending(std::uint64_t streamId, std::uint64_t applicationErrorCode) {
        SwQuicFrame frame(Type::StopSending);
        frame.m_streamId = streamId;
        frame.m_errorCode = applicationErrorCode;
        return frame;
    }

    static SwQuicFrame crypto(std::uint64_t offset, const SwByteArray& data) {
        SwQuicFrame frame(Type::Crypto);
        frame.m_offset = offset;
        frame.m_data = data;
        return frame;
    }

    static SwQuicFrame newToken(const SwByteArray& token) {
        SwQuicFrame frame(Type::NewToken);
        frame.m_data = token;
        return frame;
    }

    static SwQuicFrame stream(std::uint64_t streamId,
                              std::uint64_t offset,
                              const SwByteArray& data,
                              bool fin) {
        SwQuicFrame frame(Type::Stream);
        frame.m_streamId = streamId;
        frame.m_offset = offset;
        frame.m_data = data;
        frame.m_fin = fin;
        return frame;
    }

    static SwQuicFrame maxData(std::uint64_t maximumData) {
        SwQuicFrame frame(Type::MaxData);
        frame.m_maximum = maximumData;
        return frame;
    }

    static SwQuicFrame maxStreamData(std::uint64_t streamId, std::uint64_t maximumStreamData) {
        SwQuicFrame frame(Type::MaxStreamData);
        frame.m_streamId = streamId;
        frame.m_maximum = maximumStreamData;
        return frame;
    }

    static SwQuicFrame maxStreams(bool bidirectional, std::uint64_t maximumStreams) {
        SwQuicFrame frame(Type::MaxStreams);
        frame.m_bidirectional = bidirectional;
        frame.m_maximum = maximumStreams;
        return frame;
    }

    static SwQuicFrame dataBlocked(std::uint64_t dataLimit) {
        SwQuicFrame frame(Type::DataBlocked);
        frame.m_maximum = dataLimit;
        return frame;
    }

    static SwQuicFrame streamDataBlocked(std::uint64_t streamId, std::uint64_t streamDataLimit) {
        SwQuicFrame frame(Type::StreamDataBlocked);
        frame.m_streamId = streamId;
        frame.m_maximum = streamDataLimit;
        return frame;
    }

    static SwQuicFrame streamsBlocked(bool bidirectional, std::uint64_t streamLimit) {
        SwQuicFrame frame(Type::StreamsBlocked);
        frame.m_bidirectional = bidirectional;
        frame.m_maximum = streamLimit;
        return frame;
    }

    static SwQuicFrame newConnectionId(std::uint64_t sequenceNumber,
                                       std::uint64_t retirePriorTo,
                                       const SwByteArray& connectionId,
                                       const SwByteArray& statelessResetToken) {
        SwQuicFrame frame(Type::NewConnectionId);
        frame.m_sequenceNumber = sequenceNumber;
        frame.m_retirePriorTo = retirePriorTo;
        frame.m_connectionId = connectionId;
        frame.m_statelessResetToken = statelessResetToken;
        return frame;
    }

    static SwQuicFrame retireConnectionId(std::uint64_t sequenceNumber) {
        SwQuicFrame frame(Type::RetireConnectionId);
        frame.m_sequenceNumber = sequenceNumber;
        return frame;
    }

    static SwQuicFrame pathChallenge(const SwByteArray& data) {
        SwQuicFrame frame(Type::PathChallenge);
        frame.m_data = data;
        return frame;
    }

    static SwQuicFrame pathResponse(const SwByteArray& data) {
        SwQuicFrame frame(Type::PathResponse);
        frame.m_data = data;
        return frame;
    }

    static SwQuicFrame connectionClose(std::uint64_t errorCode,
                                       std::uint64_t frameType,
                                       const SwString& reasonPhrase) {
        SwQuicFrame frame(Type::ConnectionClose);
        frame.m_errorCode = errorCode;
        frame.m_errorFrameType = frameType;
        frame.m_reasonPhrase = reasonPhrase;
        frame.m_applicationClose = false;
        return frame;
    }

    static SwQuicFrame applicationClose(std::uint64_t errorCode, const SwString& reasonPhrase) {
        SwQuicFrame frame(Type::ConnectionClose);
        frame.m_errorCode = errorCode;
        frame.m_reasonPhrase = reasonPhrase;
        frame.m_applicationClose = true;
        return frame;
    }

    static SwQuicFrame handshakeDone() {
        return SwQuicFrame(Type::HandshakeDone);
    }

    static SwQuicFrame datagram(const SwByteArray& data) {
        SwQuicFrame frame(Type::Datagram);
        frame.m_data = data;
        return frame;
    }

    static SwQuicFrame datagram(SwByteArray&& data) {
        SwQuicFrame frame(Type::Datagram);
        frame.m_data = std::move(data);
        return frame;
    }

    Type type() const { return m_type; }

    std::uint64_t largestAcknowledged() const { return m_largestAcknowledged; }
    std::uint64_t ackDelay() const { return m_ackDelay; }
    std::uint64_t firstAckRange() const { return m_firstAckRange; }
    const SwVector<AckRange>& ackRanges() const { return m_ackRanges; }
    bool hasEcn() const { return m_hasEcn; }
    std::uint64_t ect0Count() const { return m_ect0Count; }
    std::uint64_t ect1Count() const { return m_ect1Count; }
    std::uint64_t ecnCeCount() const { return m_ecnCeCount; }

    std::uint64_t streamId() const { return m_streamId; }
    std::uint64_t offset() const { return m_offset; }
    const SwByteArray& data() const { return m_data; }
    SwByteArray takeData() { return std::move(m_data); }
    bool fin() const { return m_fin; }

    std::uint64_t finalSize() const { return m_finalSize; }
    std::uint64_t maximum() const { return m_maximum; }
    bool isBidirectional() const { return m_bidirectional; }

    std::uint64_t sequenceNumber() const { return m_sequenceNumber; }
    std::uint64_t retirePriorTo() const { return m_retirePriorTo; }
    const SwByteArray& connectionId() const { return m_connectionId; }
    const SwByteArray& statelessResetToken() const { return m_statelessResetToken; }

    std::uint64_t errorCode() const { return m_errorCode; }
    std::uint64_t errorFrameType() const { return m_errorFrameType; }
    const SwString& reasonPhrase() const { return m_reasonPhrase; }
    bool isApplicationClose() const { return m_applicationClose; }

private:
    explicit SwQuicFrame(Type type)
        : m_type(type),
          m_largestAcknowledged(0),
          m_ackDelay(0),
          m_firstAckRange(0),
          m_hasEcn(false),
          m_ect0Count(0),
          m_ect1Count(0),
          m_ecnCeCount(0),
          m_streamId(0),
          m_offset(0),
          m_fin(false),
          m_finalSize(0),
          m_maximum(0),
          m_bidirectional(true),
          m_sequenceNumber(0),
          m_retirePriorTo(0),
          m_errorCode(0),
          m_errorFrameType(0),
          m_applicationClose(false) {
    }

    Type m_type;
    std::uint64_t m_largestAcknowledged;
    std::uint64_t m_ackDelay;
    std::uint64_t m_firstAckRange;
    SwVector<AckRange> m_ackRanges;
    bool m_hasEcn;
    std::uint64_t m_ect0Count;
    std::uint64_t m_ect1Count;
    std::uint64_t m_ecnCeCount;
    std::uint64_t m_streamId;
    std::uint64_t m_offset;
    SwByteArray m_data;
    bool m_fin;
    std::uint64_t m_finalSize;
    std::uint64_t m_maximum;
    bool m_bidirectional;
    std::uint64_t m_sequenceNumber;
    std::uint64_t m_retirePriorTo;
    SwByteArray m_connectionId;
    SwByteArray m_statelessResetToken;
    std::uint64_t m_errorCode;
    std::uint64_t m_errorFrameType;
    SwString m_reasonPhrase;
    bool m_applicationClose;
};

#endif
