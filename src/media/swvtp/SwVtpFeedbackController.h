#pragma once

/**
 * @file src/media/swvtp/SwVtpFeedbackController.h
 * @brief Maps SwVTP receiver feedback to bitrate decisions.
 */

#include "media/server/SwVideoTransportServer.h"
#include "media/swvtp/SwVtpProtocol.h"

inline SwVideoServerClientFeedback swVtpReceiverStatsToClientFeedback(
    const SwVtpReceiverStats& stats,
    const SwString& clientId = SwString(),
    uint32_t targetBitrateKbps = 0,
    uint32_t encoderBitrateKbps = 0,
    bool requestKeyFrame = false) {
    SwVideoServerClientFeedback feedback;
    feedback.clientId = clientId;
    feedback.streamId = SwString::number(static_cast<int>(stats.streamId));
    feedback.lastFrameId = stats.lastFrameId;
    feedback.estimatedBandwidthKbps = stats.estimatedBandwidthKbps;
    feedback.rttMs = stats.rttMs;
    feedback.jitterMs = stats.jitterMs;
    feedback.lossPermille = stats.lossPermille;
    feedback.nackPermille = stats.nackPermille;
    feedback.receiveQueueMs = stats.receiveQueueMs;
    feedback.decodeQueueMs = stats.decodeQueueMs;
    feedback.renderQueueMs = stats.renderQueueMs;
    feedback.transferLatencyMs = stats.transferLatencyMs;
    feedback.captureLatencyMs = stats.captureLatencyMs;
    feedback.clockUncertaintyMs = stats.clockUncertaintyMs;
    feedback.droppedFrames = stats.droppedFrames;
    feedback.targetBitrateKbps = targetBitrateKbps;
    feedback.encoderBitrateKbps = encoderBitrateKbps;
    feedback.requestKeyFrame = requestKeyFrame;
    return feedback;
}

class SwVtpFeedbackController {
public:
    explicit SwVtpFeedbackController(const SwVtpAdaptiveBitratePolicy& policy =
                                         SwVtpAdaptiveBitratePolicy())
        : m_controller(policy) {}

    void setPolicy(const SwVtpAdaptiveBitratePolicy& policy) {
        m_controller.setPolicy(policy);
    }

    void reset(uint32_t startBitrateKbps = 0) {
        m_controller.reset(startBitrateKbps);
    }

    SwVtpAdaptiveBitrateDecision update(const SwVtpReceiverStats& stats,
                                        uint64_t nowMs) {
        return m_controller.update(stats, nowMs);
    }

    uint32_t targetBitrateKbps() const {
        return m_controller.targetBitrateKbps();
    }

private:
    SwVtpAdaptiveBitrateController m_controller{};
};
