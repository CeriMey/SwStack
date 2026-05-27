#pragma once

/**
 * @file src/media/server/SwVideoTransportServer.h
 * @brief Common server-side transport interface for UDP, RTP, RTSP, SwVTP, etc.
 */

#include "core/types/SwString.h"
#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"
#include "media/SwVideoPacket.h"
#include "media/server/SwMediaServerConfig.h"
#include "media/server/SwVideoPublishStream.h"
#include "media/server/SwVideoServerMetrics.h"

#include <functional>

struct SwVideoServerClientFeedback {
    SwString clientId{};
    SwString streamId{};
    uint32_t lastFrameId{0};
    uint32_t estimatedBandwidthKbps{0};
    uint16_t rttMs{0};
    uint16_t jitterMs{0};
    uint16_t lossPermille{0};
    uint16_t nackPermille{0};
    uint16_t receiveQueueMs{0};
    uint16_t decodeQueueMs{0};
    uint16_t renderQueueMs{0};
    uint16_t transferLatencyMs{0};
    uint16_t captureLatencyMs{0};
    uint16_t clockUncertaintyMs{0};
    uint32_t droppedFrames{0};
    uint32_t targetBitrateKbps{0};
    uint32_t encoderBitrateKbps{0};
    bool requestKeyFrame{false};
};

class SwVideoTransportServer {
public:
    using ClientFeedbackCallback = std::function<void(const SwVideoServerClientFeedback&)>;

    virtual ~SwVideoTransportServer() = default;

    virtual SwString protocolName() const = 0;

    virtual bool configure(const SwMediaServerConfig& config) {
        m_config = config;
        return true;
    }

    virtual bool addStream(const SwVideoPublishStream& stream) = 0;
    virtual bool addMetadataTrack(const SwMediaTrack& track) {
        (void)track;
        return false;
    }
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual bool publishVideoPacket(const SwString& streamId,
                                    const SwVideoPacket& packet) = 0;
    virtual bool publishMediaPacket(const SwString& trackId,
                                    const SwMediaPacket& packet) {
        (void)trackId;
        (void)packet;
        return false;
    }
    virtual SwVideoServerMetrics metrics() const = 0;

    void setClientFeedbackCallback(ClientFeedbackCallback callback) {
        m_clientFeedbackCallback = std::move(callback);
    }

protected:
    const SwMediaServerConfig& config() const { return m_config; }

    void emitClientFeedback(const SwVideoServerClientFeedback& feedback) {
        if (m_clientFeedbackCallback) {
            m_clientFeedbackCallback(feedback);
        }
    }

private:
    SwMediaServerConfig m_config{};
    ClientFeedbackCallback m_clientFeedbackCallback{};
};
