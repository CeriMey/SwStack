#pragma once

/**
 * @file src/media/SwRtpVideoSource.h
 * @ingroup media
 * @brief Declares a generic RTP video source backed by the reusable RTP session helpers.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwVideoSource.h"
#include "media/rtp/SwRtpDepacketizerH264.h"
#include "media/rtp/SwRtpDepacketizerH265.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwRtpSessionDescriptor.h"
#include "media/rtp/SwTsProgramDemux.h"
#include "SwDebug.h"

#include <atomic>
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
                forceKeyFrameRecoveryForGap_(m_descriptor.codec, lostPackets);
            swCWarning(kSwLogCategory_SwRtpVideoSource)
                << "[SwRtpVideoSource] RTP gap expected=" << expected << " got=" << actual;
            emitStatus(StreamState::Recovering, "Recovering from RTP loss...");
            if (m_descriptor.codec == SwVideoPacket::Codec::H265) {
                m_h265Depacketizer.onSequenceGap(forceKeyFrameRecovery);
            } else if (m_descriptor.codec == SwVideoPacket::Codec::H264) {
                m_h264Depacketizer.onSequenceGap(forceKeyFrameRecovery);
            }
            if (forceKeyFrameRecovery || lostPackets > 1) {
                emitRecovery(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                             "Recovering from RTP loss...");
                m_session->requestKeyFrame("rtp loss");
            }
        });
        m_session->setTimeoutCallback([this](int secondsWithoutData) {
            const SwString reason =
                SwString("No RTP received for ") +
                SwString::number(secondsWithoutData) + SwString(" s");
            emitStatus(StreamState::Recovering, reason);
            emitRecovery(SwMediaSource::RecoveryEvent::Kind::Timeout, reason);
        });

        m_h264Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(StreamState::Streaming, "Streaming");
            m_framesEmitted.fetch_add(1);
            m_waitingForKeyFrameRecovery.store(false);
            emitPacket(packet);
        });
        m_h265Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(StreamState::Streaming, "Streaming");
            m_framesEmitted.fetch_add(1);
            m_waitingForKeyFrameRecovery.store(false);
            emitPacket(packet);
        });
        m_tsDemux.setPacketCallback([this](const SwMediaPacket& packet) {
            emitProgramVideoPacket_(packet);
        });
        m_tsDemux.setTracksChangedCallback([this](const SwList<SwMediaTrack>& tracks) {
            setTracks(tracks);
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
        m_framesEmitted.store(0);
        m_waitingForKeyFrameRecovery.store(false);
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
        m_framesEmitted.store(0);
        m_waitingForKeyFrameRecovery.store(false);
        setRunning(false);
        emitStatus(StreamState::Stopped, "Stream stopped");
    }

private:
    static bool forceKeyFrameRecoveryForGap_(SwVideoPacket::Codec codec,
                                             int lostPackets) {
        if (codec == SwVideoPacket::Codec::H264 ||
            codec == SwVideoPacket::Codec::H265) {
            return lostPackets > 2;
        }
        return lostPackets > 4;
    }

    static SwVideoPacket::Codec videoCodecFromName_(const SwString& codec) {
        if (codec == "h265" || codec == "hevc") {
            return SwVideoPacket::Codec::H265;
        }
        return SwVideoPacket::Codec::H264;
    }

    void emitProgramVideoPacket_(const SwMediaPacket& packet) {
        if (packet.type() != SwMediaPacket::Type::Video) {
            emitMediaPacket(packet);
            return;
        }
        emitStatus(StreamState::Streaming, "Streaming");
        m_framesEmitted.fetch_add(1);
        m_waitingForKeyFrameRecovery.store(false);
        SwVideoPacket videoPacket(videoCodecFromName_(packet.codec()),
                                  packet.payload(),
                                  packet.pts(),
                                  packet.dts(),
                                  packet.isKeyFrame());
        videoPacket.setDiscontinuity(packet.isDiscontinuity());
        emitPacket(videoPacket);
    }

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
            m_waitingForKeyFrameRecovery.store(false);
            return;
        }
        if (m_framesEmitted.load() > 0 &&
            !m_waitingForKeyFrameRecovery.exchange(true)) {
            emitRecovery(SwMediaSource::RecoveryEvent::Kind::LiveCut,
                         "Waiting for keyframe...");
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
    SwTsProgramDemux m_tsDemux{};
    std::chrono::steady_clock::time_point m_lastWaitingKeyFrameRequestTime{};
    std::atomic<uint64_t> m_framesEmitted{0};
    std::atomic<bool> m_waitingForKeyFrameRecovery{false};
};
