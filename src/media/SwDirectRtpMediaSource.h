#pragma once

/**
 * @file src/media/SwDirectRtpMediaSource.h
 * @ingroup media
 * @brief Declares an SDP-driven direct RTP media source for UDP/RTP sessions without RTSP.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwRtspTrackGraph.h"
#include "media/SwSdpMediaDescription.h"
#include "media/SwVideoSource.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwRtpSessionDescriptor.h"
#include "SwDebug.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

static constexpr const char* kSwLogCategory_SwDirectRtpMediaSource = "sw.media.swdirectrtpmediasource";

class SwDirectRtpMediaSource : public SwVideoSource {
public:
    SwDirectRtpMediaSource(const SwMediaOpenOptions& options,
                           const SwSdpMediaDescription& description,
                           SwObject* parent = nullptr)
        : m_options(options)
        , m_description(description) {
        SW_UNUSED(parent);
        selectTracks_();
        m_trackGraph.reset(new SwRtspTrackGraph());
        configureTrackGraph_();
        publishTracks_();
    }

    ~SwDirectRtpMediaSource() override { stop(); }

    SwString name() const override { return "SwDirectRtpMediaSource"; }

    void start() override {
        if (isRunning()) {
            return;
        }
        if (m_selectedVideoTrackIndex < 0) {
            emitStatus(StreamState::Recovering, "No supported video track in SDP");
            return;
        }
        emitStatus(StreamState::Connecting, "Opening SDP RTP sessions...");
        m_runtimes.clear();
        if (m_trackGraph) {
            m_trackGraph->start();
        }

        bool startedAny = false;
        for (std::size_t i = 0; i < m_description.tracks.size(); ++i) {
            if (!shouldStartTrack_(static_cast<int>(i))) {
                continue;
            }
            if (startTrackSession_(static_cast<int>(i))) {
                startedAny = true;
            }
        }
        if (!startedAny) {
            if (m_trackGraph) {
                m_trackGraph->stop();
            }
            emitStatus(StreamState::Recovering, "Failed to bind SDP RTP sessions");
            return;
        }
        setRunning(true);
        publishTracks_();
        emitStatus(StreamState::Streaming, "Streaming");
    }

    void stop() override {
        for (std::size_t i = 0; i < m_runtimes.size(); ++i) {
            if (m_runtimes[i] && m_runtimes[i]->session) {
                m_runtimes[i]->session->stop();
            }
        }
        m_runtimes.clear();
        if (m_trackGraph) {
            m_trackGraph->stop();
        }
        setRunning(false);
        publishTracks_();
        emitStatus(StreamState::Stopped, "Stream stopped");
    }

    void handleConsumerPressureChanged(const SwVideoSource::ConsumerPressure& pressure) override {
        if (m_trackGraph) {
            m_trackGraph->setConsumerPressure(pressure);
        }
    }

private:
    enum class RuntimeKind {
        Video,
        Audio,
        Metadata
    };

    struct TrackRuntime {
        int trackIndex{-1};
        RuntimeKind kind{RuntimeKind::Video};
        std::unique_ptr<SwRtpSession> session{};
    };

    static std::string normalizedCodec_(const SwSdpMediaTrackDescription& track) {
        return SwSdpMediaDescription_detail::normalizeCodecName(track.codecName);
    }

    static bool isHevcCodec_(const std::string& codec) {
        return codec == "h265" || codec == "hevc";
    }

    static bool isSupportedAudioCodec_(const std::string& codec) {
        return codec == "pcmu" ||
               codec == "pcma" ||
               codec == "opus" ||
               codec == "aac" ||
               codec == "mpeg4-generic" ||
               codec == "mp4a-latm";
    }

    static bool isSupportedMetadataCodec_(const std::string& codec) {
        return codec == "smpte336m" || codec == "klv";
    }

    static SwVideoPacket::Codec videoCodec_(const SwSdpMediaTrackDescription& track) {
        const std::string codec = normalizedCodec_(track);
        if (isHevcCodec_(codec)) {
            return SwVideoPacket::Codec::H265;
        }
        return SwVideoPacket::Codec::H264;
    }

    static SwString mediaTrackId_(int index) {
        return SwString("track-") + SwString(std::to_string(index));
    }

    static bool isWildcardAddress_(const SwString& address) {
        const SwString normalized = address.trimmed().toLower();
        return normalized.isEmpty() || normalized == "*" || normalized == "0.0.0.0" ||
               normalized == "::";
    }

    int trackScore_(const SwSdpMediaTrackDescription& track) const {
        if (!track.isVideo() || track.inactive || track.sendOnly) {
            return -1;
        }
        return SwSdpMediaDescription_detail::videoTrackScore(track);
    }

    void selectTracks_() {
        m_selectedVideoTrackIndex = -1;
        int bestScore = -1;
        for (std::size_t i = 0; i < m_description.tracks.size(); ++i) {
            const int score = trackScore_(m_description.tracks[i]);
            if (score > bestScore) {
                bestScore = score;
                m_selectedVideoTrackIndex = static_cast<int>(i);
            }
        }
    }

    bool shouldStartTrack_(int trackIndex) const {
        if (trackIndex < 0 ||
            static_cast<std::size_t>(trackIndex) >= m_description.tracks.size()) {
            return false;
        }
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(trackIndex)];
        if (track.inactive || track.sendOnly || !track.isRtpPacketized()) {
            return false;
        }
        if (track.isVideo()) {
            return trackIndex == m_selectedVideoTrackIndex;
        }
        const std::string codec = normalizedCodec_(track);
        if (track.isAudio()) {
            return m_options.enableAudio && isSupportedAudioCodec_(codec);
        }
        if (track.isMetadata()) {
            return m_options.enableMetadata && isSupportedMetadataCodec_(codec);
        }
        return false;
    }

    RuntimeKind runtimeKindForTrack_(const SwSdpMediaTrackDescription& track) const {
        if (track.isAudio()) {
            return RuntimeKind::Audio;
        }
        if (track.isMetadata()) {
            return RuntimeKind::Metadata;
        }
        return RuntimeKind::Video;
    }

    bool isTrackRuntimeActive_(int trackIndex) const {
        for (std::size_t i = 0; i < m_runtimes.size(); ++i) {
            if (m_runtimes[i] && m_runtimes[i]->trackIndex == trackIndex) {
                return true;
            }
        }
        return false;
    }

    SwRtpSessionDescriptor descriptorForTrack_(const SwSdpMediaTrackDescription& track) const {
        SwRtpSessionDescriptor descriptor;
        descriptor.bindAddress = m_options.bindAddress;
        descriptor.localRtpPort = static_cast<uint16_t>(
            track.port > 0 ? track.port : (m_options.rtpPort != 0 ? m_options.rtpPort : 5004));
        descriptor.localRtcpPort = static_cast<uint16_t>(
            track.rtcpPort > 0
                ? track.rtcpPort
                : (m_options.rtcpPort != 0
                       ? m_options.rtcpPort
                       : static_cast<uint16_t>(descriptor.localRtpPort + 1)));
        if (!track.connectionAddress.empty() &&
            SwSdpMediaDescription_detail::isMulticastAddress(track.connectionAddress)) {
            descriptor.multicastGroup = SwString(track.connectionAddress);
        } else {
            descriptor.multicastGroup = m_options.multicastGroup;
        }
        descriptor.sourceAddressFilter = m_options.sourceAddressFilter;
        if (descriptor.sourceAddressFilter.isEmpty() && !track.sourceAddressFilter.empty()) {
            descriptor.sourceAddressFilter = SwString(track.sourceAddressFilter);
        }
        descriptor.sourceRtcpPort = m_options.sourceRtcpPort;
        descriptor.codec = track.isVideo() ? videoCodec_(track) : SwVideoPacket::Codec::Unknown;
        descriptor.payloadType = track.payloadType;
        descriptor.clockRate = track.clockRate > 0 ? track.clockRate : 90000;
        descriptor.format = track.isMpegTsPayload()
                                ? SwMediaOpenOptions::UdpPayloadFormat::MpegTs
                                : SwMediaOpenOptions::UdpPayloadFormat::Rtp;
        descriptor.fmtp = SwString(track.fmtp);
        descriptor.allowKeyFrameRequests =
            track.isVideo() && descriptor.multicastGroup.isEmpty();
        descriptor.lowLatency = m_options.lowLatency;
        descriptor.jitterMaxDelayMs = effectiveJitterDelayMs_(track);
        descriptor.jitterMaxPackets = effectiveJitterMaxPackets_(track);
        return descriptor;
    }

    bool startTrackSession_(int trackIndex) {
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(trackIndex)];
        SwRtpSessionDescriptor descriptor = descriptorForTrack_(track);
        std::unique_ptr<TrackRuntime> runtime(new TrackRuntime());
        runtime->trackIndex = trackIndex;
        runtime->kind = runtimeKindForTrack_(track);
        runtime->session.reset(new SwRtpSession(descriptor));
        runtime->session->setPacketCallback([this, trackIndex](const SwRtpSession::Packet& packet) {
            handleRtpPacket_(trackIndex, packet);
        });
        runtime->session->setGapCallback([this, trackIndex](uint16_t expected, uint16_t actual) {
            if (trackIndex == m_selectedVideoTrackIndex && m_trackGraph) {
                m_trackGraph->submitVideoGap(expected, actual);
            }
        });
        runtime->session->setTimeoutCallback([this, trackIndex](int secondsWithoutData) {
            const SwSdpMediaTrackDescription& timeoutTrack =
                m_description.tracks[static_cast<std::size_t>(trackIndex)];
            swCWarning(kSwLogCategory_SwDirectRtpMediaSource)
                << "[SwDirectRtpMediaSource] No RTP received for "
                << secondsWithoutData
                << " s track=" << mediaTrackId_(trackIndex)
                << " codec=" << SwString(timeoutTrack.codecName);
            if (trackIndex == m_selectedVideoTrackIndex) {
                emitStatus(StreamState::Recovering, "Video RTP stream stalled...");
                emitRecovery(SwMediaSource::RecoveryEvent::Kind::Timeout,
                             "Video RTP stream stalled...");
            }
        });
        if (!runtime->session->start()) {
            return false;
        }
        if (!descriptor.multicastGroup.isEmpty() && descriptor.sourceAddressFilter.isEmpty()) {
            swCWarning(kSwLogCategory_SwDirectRtpMediaSource)
                << "[SwDirectRtpMediaSource] Listening multicast track="
                << mediaTrackId_(trackIndex)
                << " group=" << descriptor.multicastGroup;
        }
        m_runtimes.push_back(std::move(runtime));
        return true;
    }

    void configureTrackGraph_() {
        if (!m_trackGraph || m_selectedVideoTrackIndex < 0 ||
            static_cast<std::size_t>(m_selectedVideoTrackIndex) >= m_description.tracks.size()) {
            return;
        }
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(m_selectedVideoTrackIndex)];
        SwRtspTrackGraph::VideoConfig videoConfig;
        videoConfig.codec = videoCodec_(track);
        videoConfig.payloadType = track.payloadType;
        videoConfig.clockRate = track.clockRate > 0 ? track.clockRate : 90000;
        videoConfig.fmtp = SwString(track.fmtp);
        videoConfig.liveTrimEnabled = m_options.lowLatency;
        videoConfig.latencyTargetMs = effectiveLatencyTargetMs_(track);
        videoConfig.transportStream = track.isMpegTsPayload();
        m_trackGraph->setVideoConfig(videoConfig);
        m_trackGraph->setConsumerPressure(consumerPressure());
        m_trackGraph->setVideoPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(SwVideoSource::StreamState::Streaming, "Streaming");
            emitPacket(packet);
        });
        m_trackGraph->setMediaPacketCallback([this](const SwMediaPacket& packet) {
            emitMediaPacket(packet);
        });
        m_trackGraph->setTracksChangedCallback([this](const SwList<SwMediaTrack>& tracks) {
            m_programTracks = tracks;
            publishTracks_();
        });
        m_trackGraph->setKeyFrameRequestCallback([this](const SwString& reason) {
            requestKeyFrame_(reason);
        });
        m_trackGraph->setRecoveryCallback([this](SwMediaSource::RecoveryEvent::Kind kind,
                                                 const SwString& reason) {
            emitRecovery(kind, reason);
        });
    }

    void requestKeyFrame_(const SwString& reason) {
        for (std::size_t i = 0; i < m_runtimes.size(); ++i) {
            if (!m_runtimes[i] ||
                m_runtimes[i]->trackIndex != m_selectedVideoTrackIndex ||
                !m_runtimes[i]->session) {
                continue;
            }
            m_runtimes[i]->session->requestKeyFrame(reason);
            return;
        }
    }

    SwMediaPacket makeAudioPacket_(int trackIndex, const SwRtpSession::Packet& packet) const {
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(trackIndex)];
        SwMediaPacket mediaPacket;
        mediaPacket.setType(SwMediaPacket::Type::Audio);
        mediaPacket.setTrackId(mediaTrackId_(trackIndex));
        mediaPacket.setCodec(SwString(track.codecName));
        mediaPacket.setPayload(packet.payload);
        mediaPacket.setPts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setPayloadType(track.payloadType);
        mediaPacket.setClockRate(track.clockRate);
        mediaPacket.setSampleRate(track.clockRate);
        mediaPacket.setChannelCount(track.channelCount > 0 ? track.channelCount : 1);
        return mediaPacket;
    }

    SwMediaPacket makeMetadataPacket_(int trackIndex, const SwRtpSession::Packet& packet) const {
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(trackIndex)];
        SwMediaPacket mediaPacket;
        mediaPacket.setType(SwMediaPacket::Type::Metadata);
        mediaPacket.setTrackId(mediaTrackId_(trackIndex));
        mediaPacket.setCodec(SwString(track.codecName));
        mediaPacket.setPayload(packet.payload);
        mediaPacket.setPts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setPayloadType(track.payloadType);
        mediaPacket.setClockRate(track.clockRate > 0 ? track.clockRate : 90000);
        return mediaPacket;
    }

    int effectiveLatencyTargetMs_(const SwSdpMediaTrackDescription& track) const {
        if (m_options.latencyTargetMs > 0) {
            return m_options.latencyTargetMs;
        }
        return track.frameRate >= 50.0 ? 1000 : 500;
    }

    int effectiveJitterDelayMs_(const SwSdpMediaTrackDescription& track) const {
        if (m_options.rtpJitterDelayMs > 0) {
            return m_options.rtpJitterDelayMs;
        }
        if (!m_options.lowLatency) {
            return 250;
        }
        return track.frameRate >= 50.0 ? 180 : 120;
    }

    int effectiveJitterMaxPackets_(const SwSdpMediaTrackDescription& track) const {
        if (m_options.rtpJitterMaxPackets > 0) {
            return m_options.rtpJitterMaxPackets;
        }
        if (!m_options.lowLatency) {
            return 512;
        }
        return track.frameRate >= 50.0 ? 512 : 256;
    }

    void handleRtpPacket_(int trackIndex, const SwRtpSession::Packet& packet) {
        if (trackIndex < 0 ||
            static_cast<std::size_t>(trackIndex) >= m_description.tracks.size()) {
            return;
        }
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(trackIndex)];
        if (track.isVideo()) {
            if (trackIndex == m_selectedVideoTrackIndex && m_trackGraph) {
                m_trackGraph->submitVideoPacket(packet, false);
            }
            return;
        }
        if (track.isAudio()) {
            if (m_trackGraph) {
                m_trackGraph->submitAudioPacket(makeAudioPacket_(trackIndex, packet));
            }
            return;
        }
        if (track.isMetadata()) {
            if (m_trackGraph) {
                m_trackGraph->submitMetadataPacket(makeMetadataPacket_(trackIndex, packet));
            }
            return;
        }
    }

    SwMediaTrack makePublishedTrack_(int trackIndex) const {
        const SwSdpMediaTrackDescription& track =
            m_description.tracks[static_cast<std::size_t>(trackIndex)];
        SwMediaTrack mediaTrack;
        mediaTrack.id = mediaTrackId_(trackIndex);
        if (track.isVideo()) {
            mediaTrack.type = SwMediaTrack::Type::Video;
        } else if (track.isAudio()) {
            mediaTrack.type = SwMediaTrack::Type::Audio;
        } else if (track.isMetadata()) {
            mediaTrack.type = SwMediaTrack::Type::Metadata;
        } else {
            mediaTrack.type = SwMediaTrack::Type::Unknown;
        }
        mediaTrack.codec = SwString(track.codecName);
        mediaTrack.payloadType = track.payloadType;
        mediaTrack.clockRate = track.clockRate;
        mediaTrack.sampleRate = track.isAudio() ? track.clockRate : 0;
        mediaTrack.channelCount = track.channelCount;
        mediaTrack.control = SwString(track.control);
        mediaTrack.fmtp = SwString(track.fmtp);
        mediaTrack.selected = isTrackRuntimeActive_(trackIndex) ||
                              (!isRunning() && shouldStartTrack_(trackIndex));

        const std::string codec = normalizedCodec_(track);
        if ((track.isAudio() && !isSupportedAudioCodec_(codec)) ||
            (track.isMetadata() && !isSupportedMetadataCodec_(codec)) ||
            (track.isVideo() && trackScore_(track) < 0)) {
            mediaTrack.availability = SwMediaTrack::Availability::Unsupported;
        } else {
            mediaTrack.availability = SwMediaTrack::Availability::Available;
        }
        return mediaTrack;
    }

    void publishTracks_() {
        SwList<SwMediaTrack> tracks;
        for (std::size_t i = 0; i < m_description.tracks.size(); ++i) {
            SwMediaTrack track = makePublishedTrack_(static_cast<int>(i));
            if (track.type != SwMediaTrack::Type::Unknown) {
                tracks.append(track);
            }
        }
        for (const auto& programTrack : m_programTracks) {
            bool duplicate = false;
            for (const auto& existing : tracks) {
                if (existing.id == programTrack.id) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                tracks.append(programTrack);
            }
        }
        setTracks(tracks);
    }

    SwMediaOpenOptions m_options{};
    SwSdpMediaDescription m_description{};
    int m_selectedVideoTrackIndex{-1};
    std::unique_ptr<SwRtspTrackGraph> m_trackGraph{};
    std::vector<std::unique_ptr<TrackRuntime>> m_runtimes{};
    SwList<SwMediaTrack> m_programTracks{};
};
