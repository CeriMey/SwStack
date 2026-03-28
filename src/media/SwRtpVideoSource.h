#pragma once

/**
 * @file src/media/SwRtpVideoSource.h
 * @ingroup media
 * @brief Declares a generic RTP video source backed by the reusable RTP session helpers.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwVideoSource.h"
#include "media/rtp/SwMpegTsDemux.h"
#include "media/rtp/SwRtpDepacketizerH264.h"
#include "media/rtp/SwRtpDepacketizerH265.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwRtpSessionDescriptor.h"
#include "SwDebug.h"

#include <chrono>
#include <memory>
static constexpr const char* kSwLogCategory_SwRtpVideoSource = "sw.media.swrtpvideosource";

class SwRtpVideoSource : public SwVideoSource {
public:
    explicit SwRtpVideoSource(const SwMediaOpenOptions& options, SwObject* parent = nullptr)
        : m_options(options)
        , m_descriptor(SwRtpSessionDescriptor::fromOpenOptions(options)) {
        SW_UNUSED(parent);
        m_session = std::make_unique<SwRtpSession>(m_descriptor);
        m_session->setPacketCallback([this](const SwRtpSession::Packet& packet) {
            handleRtpPacket_(packet);
        });
        m_session->setGapCallback([this](uint16_t expected, uint16_t actual) {
            const int lostPackets = static_cast<int>(static_cast<uint16_t>(actual - expected));
            const bool forceKeyFrameRecovery =
                (m_descriptor.codec == SwVideoPacket::Codec::H265) ? (lostPackets > 2)
                                                                   : (lostPackets > 4);
            swCWarning(kSwLogCategory_SwRtpVideoSource)
                << "[SwRtpVideoSource] RTP gap expected=" << expected << " got=" << actual;
            emitStatus(StreamState::Recovering, "Recovering from RTP loss...");
            if (m_descriptor.codec == SwVideoPacket::Codec::H265) {
                m_h265Depacketizer.onSequenceGap(forceKeyFrameRecovery);
            } else if (m_descriptor.codec == SwVideoPacket::Codec::H264) {
                m_h264Depacketizer.onSequenceGap(forceKeyFrameRecovery);
            }
            if (forceKeyFrameRecovery || lostPackets > 1) {
                m_session->requestKeyFrame("rtp loss");
            }
        });
        m_session->setTimeoutCallback([this](int secondsWithoutData) {
            emitStatus(StreamState::Recovering,
                       SwString("No RTP received for ") +
                           SwString::number(secondsWithoutData) + SwString(" s"));
        });

        m_h264Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(StreamState::Streaming, "Streaming");
            emitPacket(packet);
        });
        m_h265Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(StreamState::Streaming, "Streaming");
            emitPacket(packet);
        });
        m_tsDemux.setPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(StreamState::Streaming, "Streaming");
            emitPacket(packet);
        });

        if (m_descriptor.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.setFmtp(m_descriptor.fmtp);
        } else {
            m_h264Depacketizer.setFmtp(m_descriptor.fmtp);
        }
    }

    ~SwRtpVideoSource() override { stop(); }

    SwString name() const override { return "SwRtpVideoSource"; }

    void start() override {
        if (isRunning()) {
            return;
        }
        emitStatus(StreamState::Connecting, "Opening RTP session...");
        if (!m_session->start()) {
            emitStatus(StreamState::Recovering, "Failed to bind RTP session");
            return;
        }
        setRunning(true);
    }

    void stop() override {
        if (m_session) {
            m_session->stop();
        }
        m_h264Depacketizer.reset();
        m_h265Depacketizer.reset();
        m_tsDemux.reset();
        setRunning(false);
        emitStatus(StreamState::Stopped, "Stream stopped");
    }

private:
    void handleRtpPacket_(const SwRtpSession::Packet& packet) {
        if (m_descriptor.format == SwMediaOpenOptions::UdpPayloadFormat::MpegTs) {
            m_tsDemux.feed(reinterpret_cast<const uint8_t*>(packet.payload.constData()),
                          packet.payload.size(),
                          packet.timestamp);
            return;
        }
        if (m_descriptor.codec == SwVideoPacket::Codec::H265) {
            m_h265Depacketizer.push(packet);
            maybeRequestKeyFrameWhileWaiting_();
            return;
        }
        m_h264Depacketizer.push(packet);
        maybeRequestKeyFrameWhileWaiting_();
    }

    void maybeRequestKeyFrameWhileWaiting_() {
        const bool waitingForKeyFrame =
            (m_descriptor.codec == SwVideoPacket::Codec::H265) ? m_h265Depacketizer.isWaitingForKeyFrame()
                                                               : m_h264Depacketizer.isWaitingForKeyFrame();
        if (!waitingForKeyFrame || !m_session) {
            m_lastWaitingKeyFrameRequestTime = {};
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastWaitingKeyFrameRequestTime.time_since_epoch().count() != 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastWaitingKeyFrameRequestTime).count();
            if (elapsed < 1000) {
                return;
            }
        }
        m_lastWaitingKeyFrameRequestTime = now;
        m_session->requestKeyFrame("waiting keyframe");
    }

    SwMediaOpenOptions m_options{};
    SwRtpSessionDescriptor m_descriptor{};
    std::unique_ptr<SwRtpSession> m_session{};
    SwRtpDepacketizerH264 m_h264Depacketizer{};
    SwRtpDepacketizerH265 m_h265Depacketizer{};
    SwMpegTsDemux m_tsDemux{};
    std::chrono::steady_clock::time_point m_lastWaitingKeyFrameRequestTime{};
};
