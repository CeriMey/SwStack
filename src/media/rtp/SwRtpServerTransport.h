#pragma once

/**
 * @file src/media/rtp/SwRtpServerTransport.h
 * @brief RTP server transport implementation of the common media server interface.
 */

#include "media/transport/SwUdpServerTransport.h"
#include "media/rtp/SwRtpPacketizer.h"

#include <cstdint>
#include <vector>

class SwRtpServerTransport : public SwUdpServerTransport {
public:
    explicit SwRtpServerTransport(const SwString& protocolName = SwString("rtp"))
        : SwUdpServerTransport(protocolName) {}

protected:
    std::vector<SwByteArray> makeDatagramsLocked_(const SwVideoPublishStream& stream,
                                                  const SwVideoPacket& packet) override {
        return m_packetizer.packetize(stream, packet, config().mtuBytes);
    }

    SwRtpPacketizer m_packetizer{};
};
