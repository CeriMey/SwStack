#pragma once

/**
 * @file src/media/swvtp/SwVtpProtocol.h
 * @brief Core binary framing for SwVTP, the SwLive low-latency video transport protocol.
 */

#include "core/types/SwByteArray.h"
#include "core/types/SwList.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

static constexpr const char* kSwVtpProtocolName = "SwVTP";
static constexpr uint8_t kSwVtpVersion1 = 1;
static constexpr uint32_t kSwVtpMagic = 0x53575654U; // "SWVT"
static constexpr uint16_t kSwVtpHeaderBytes = 64;

enum class SwVtpMessageType : uint8_t {
    Invalid = 0,
    Hello = 1,
    Accept = 2,
    StreamConfig = 3,
    Start = 4,
    Stop = 5,
    FrameFragment = 16,
    KlvFragment = 17,
    ReceiverStats = 32,
    Nack = 33,
    KeyFrameRequest = 34,
    BitrateControl = 35,
    Ping = 48,
    Pong = 49,
    Close = 50
};

enum class SwVtpTrackType : uint8_t {
    Unknown = 0,
    Video = 1,
    Audio = 2,
    MetadataKlv = 3,
    Control = 4
};

enum class SwVtpCodec : uint8_t {
    Unknown = 0,
    H264 = 1,
    H265 = 2,
    AV1 = 3,
    Opus = 16,
    Klv = 32
};

enum SwVtpFrameFlag : uint32_t {
    SwVtpFlag_None = 0x00000000U,
    SwVtpFlag_KeyFrame = 0x00000001U,
    SwVtpFlag_Discontinuity = 0x00000002U,
    SwVtpFlag_CodecConfig = 0x00000004U,
    SwVtpFlag_FirstFragment = 0x00000008U,
    SwVtpFlag_LastFragment = 0x00000010U,
    SwVtpFlag_Important = 0x00000020U,
    SwVtpFlag_Droppable = 0x00000040U,
    SwVtpFlag_FecParity = 0x00000080U,
    SwVtpFlag_Encrypted = 0x00000100U,
    SwVtpFlag_CompressedControl = 0x00000200U
};

enum class SwVtpDeliveryMode : uint8_t {
    Invalid = 0,
    Unicast = 1,
    Broadcast = 2,
    Multicast239 = 3
};

enum SwVtpEndpointFlag : uint8_t {
    SwVtpEndpointFlag_None = 0x00U,
    SwVtpEndpointFlag_MulticastLoopback = 0x01U
};

static constexpr uint32_t kSwVtpIpv4Any = 0x00000000U;
static constexpr uint32_t kSwVtpIpv4Broadcast = 0xFFFFFFFFU;
static constexpr uint8_t kSwVtpUdpEndpointBytes = 12;
static constexpr uint8_t kSwVtpStreamConfigBytes = 20;
static constexpr uint8_t kSwVtpClientAnnouncementBytes = 8;
static constexpr uint8_t kSwVtpBitrateControlBytes = 20;

struct SwVtpUdpEndpoint {
    SwVtpDeliveryMode deliveryMode{SwVtpDeliveryMode::Invalid};
    uint8_t flags{SwVtpEndpointFlag_None};
    uint16_t port{0};
    uint32_t ipv4{0};
    uint8_t ttl{1};

    bool isValid() const;
    bool isUnicast() const { return deliveryMode == SwVtpDeliveryMode::Unicast; }
    bool isBroadcast() const { return deliveryMode == SwVtpDeliveryMode::Broadcast; }
    bool isMulticast239() const { return deliveryMode == SwVtpDeliveryMode::Multicast239; }
};

struct SwVtpStreamConfig {
    uint16_t streamId{0};
    uint16_t trackId{0};
    SwVtpTrackType trackType{SwVtpTrackType::Unknown};
    SwVtpCodec codec{SwVtpCodec::Unknown};
    SwVtpUdpEndpoint endpoint{};

    bool isValid() const;
};

struct SwVtpClientAnnouncement {
    uint16_t streamId{0};
    uint16_t receivePort{0};
    uint32_t clientIpv4{0};

    bool isValid() const;
};

struct SwVtpHeader {
    uint8_t version{kSwVtpVersion1};
    SwVtpMessageType messageType{SwVtpMessageType::Invalid};
    uint16_t headerBytes{kSwVtpHeaderBytes};
    uint32_t flags{SwVtpFlag_None};
    uint16_t streamId{0};
    uint16_t trackId{0};
    SwVtpTrackType trackType{SwVtpTrackType::Unknown};
    SwVtpCodec codec{SwVtpCodec::Unknown};
    uint8_t temporalLayer{0};
    uint8_t spatialLayer{0};
    uint32_t frameId{0};
    uint16_t fragmentIndex{0};
    uint16_t fragmentCount{0};
    uint16_t payloadBytes{0};
    uint16_t fecGroupId{0};
    uint64_t ptsUs{0};
    uint64_t captureTimeUs{0};
    uint64_t deadlineUs{0};
    uint64_t sendTimeUs{0};

    bool isValid() const {
        return version == kSwVtpVersion1 &&
               headerBytes == kSwVtpHeaderBytes &&
               messageType != SwVtpMessageType::Invalid;
    }

    bool hasFlag(uint32_t flag) const {
        return (flags & flag) != 0U;
    }
};

struct SwVtpDatagram {
    SwVtpHeader header{};
    SwByteArray payload{};
};

struct SwVtpReceiverStats {
    uint16_t streamId{0};
    uint16_t trackId{0};
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
};

struct SwVtpBitrateControl {
    uint16_t streamId{0};
    uint16_t trackId{0};
    uint32_t targetBitrateKbps{0};
    uint32_t encoderBitrateKbps{0};
    uint32_t estimatedBandwidthKbps{0};
    uint8_t reason{0};
    uint8_t flags{0};
    uint16_t reserved{0};

    bool isValid() const {
        return streamId != 0U && trackId != 0U && targetBitrateKbps != 0U;
    }
};

struct SwVtpNackRequest {
    uint16_t streamId{0};
    uint16_t trackId{0};
    uint32_t frameId{0};
    SwList<uint16_t> missingFragments{};

    bool isValid() const {
        return frameId != 0 && !missingFragments.isEmpty();
    }
};

struct SwVtpClockSyncPing {
    uint32_t syncId{0};
    uint64_t clientSendTimeUs{0};
};

struct SwVtpClockSyncPong {
    uint32_t syncId{0};
    uint64_t clientSendTimeUs{0};
    uint64_t serverReceiveTimeUs{0};
    uint64_t serverSendTimeUs{0};
};

struct SwVtpClockSyncSample {
    uint32_t syncId{0};
    uint64_t clientSendTimeUs{0};
    uint64_t serverReceiveTimeUs{0};
    uint64_t serverSendTimeUs{0};
    uint64_t clientReceiveTimeUs{0};
};

struct SwVtpClockEstimate {
    bool valid{false};
    int64_t serverToClientOffsetUs{0};
    uint64_t rttUs{0};
    uint64_t serverProcessingUs{0};
    uint64_t networkRttUs{0};
    uint64_t oneWayUncertaintyUs{0};
    uint8_t confidencePercent{0};
};

struct SwVtpFrameLatencySample {
    bool valid{false};
    bool captureLatencyValid{false};
    uint64_t transferLatencyUs{0};
    uint64_t captureToReceiveUs{0};
    uint64_t estimatedServerSendClientTimeUs{0};
    uint64_t estimatedCaptureClientTimeUs{0};
    uint64_t oneWayUncertaintyUs{0};
    uint8_t confidencePercent{0};
};

struct SwVtpAdaptiveBitratePolicy {
    uint32_t minBitrateKbps{500};
    uint32_t maxBitrateKbps{20000};
    uint32_t startBitrateKbps{4000};
    uint16_t targetQueueMs{30};
    uint16_t hardQueueMs{80};
    uint16_t highLossPermille{20};
    uint16_t criticalLossPermille{80};
    uint16_t highNackPermille{50};
    uint16_t highJitterMs{30};
    uint16_t highRttMs{120};
    uint16_t bandwidthSafetyPercent{90};
    uint16_t upshiftBandwidthHeadroomPercent{85};
    uint16_t fastDownshiftPercent{65};
    uint16_t softDownshiftPercent{82};
    uint16_t upshiftPercent{110};
    uint32_t upshiftCooldownMs{3000};
};

struct SwVtpAdaptiveBitrateDecision {
    enum class Reason {
        Startup,
        Stable,
        NetworkPressure,
        ClientQueuePressure,
        DecoderPressure,
        UpshiftProbe
    };

    uint32_t targetBitrateKbps{0};
    bool requestKeyFrame{false};
    bool preferBaseTemporalLayer{false};
    Reason reason{Reason::Startup};
};

class SwVtpAdaptiveBitrateController {
public:
    explicit SwVtpAdaptiveBitrateController(const SwVtpAdaptiveBitratePolicy& policy =
                                                SwVtpAdaptiveBitratePolicy())
        : m_policy(policy),
          m_targetBitrateKbps(clampBitrate_(policy.startBitrateKbps)) {}

    void setPolicy(const SwVtpAdaptiveBitratePolicy& policy) {
        m_policy = policy;
        m_targetBitrateKbps = clampBitrate_(m_targetBitrateKbps == 0U
                                                ? policy.startBitrateKbps
                                                : m_targetBitrateKbps);
    }

    void reset(uint32_t startBitrateKbps = 0) {
        m_targetBitrateKbps = clampBitrate_(startBitrateKbps == 0U
                                                ? m_policy.startBitrateKbps
                                                : startBitrateKbps);
        m_lastUpshiftMs = 0;
        m_lastPressureMs = 0;
        m_haveLastUpshift = false;
        m_haveLastPressure = false;
    }

    uint32_t targetBitrateKbps() const { return m_targetBitrateKbps; }

    SwVtpAdaptiveBitrateDecision update(const SwVtpReceiverStats& stats, uint64_t nowMs) {
        SwVtpAdaptiveBitrateDecision decision;
        if (m_targetBitrateKbps == 0U) {
            reset();
            decision.reason = SwVtpAdaptiveBitrateDecision::Reason::Startup;
        }

        const bool clientQueueHard =
            stats.receiveQueueMs >= m_policy.hardQueueMs ||
            stats.renderQueueMs >= m_policy.hardQueueMs;
        const bool decoderHard = stats.decodeQueueMs >= m_policy.hardQueueMs;
        const bool networkHard =
            stats.lossPermille >= m_policy.criticalLossPermille ||
            stats.rttMs >= m_policy.highRttMs;
        const bool networkSoft =
            stats.lossPermille >= m_policy.highLossPermille ||
            stats.nackPermille >= m_policy.highNackPermille ||
            stats.jitterMs >= m_policy.highJitterMs;
        const bool queueSoft =
            stats.receiveQueueMs >= m_policy.targetQueueMs ||
            stats.renderQueueMs >= m_policy.targetQueueMs ||
            stats.decodeQueueMs >= m_policy.targetQueueMs;
        const uint32_t bandwidthCeilingKbps = bandwidthCeiling_(stats);
        const bool bandwidthPressure =
            bandwidthCeilingKbps > 0U && m_targetBitrateKbps > bandwidthCeilingKbps;

        if (networkHard || clientQueueHard || decoderHard) {
            m_targetBitrateKbps = scaledBitrate_(m_targetBitrateKbps,
                                                 m_policy.fastDownshiftPercent);
            applyBandwidthCeiling_(bandwidthCeilingKbps);
            m_lastPressureMs = nowMs;
            m_haveLastPressure = true;
            decision.requestKeyFrame = networkHard || decoderHard;
            decision.preferBaseTemporalLayer = true;
            decision.reason = decoderHard
                                  ? SwVtpAdaptiveBitrateDecision::Reason::DecoderPressure
                                  : (clientQueueHard
                                         ? SwVtpAdaptiveBitrateDecision::Reason::ClientQueuePressure
                                         : SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure);
        } else if (networkSoft || queueSoft) {
            m_targetBitrateKbps = scaledBitrate_(m_targetBitrateKbps,
                                                 m_policy.softDownshiftPercent);
            applyBandwidthCeiling_(bandwidthCeilingKbps);
            m_lastPressureMs = nowMs;
            m_haveLastPressure = true;
            decision.preferBaseTemporalLayer = true;
            decision.reason = queueSoft
                                  ? SwVtpAdaptiveBitrateDecision::Reason::ClientQueuePressure
                                  : SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure;
        } else if (bandwidthPressure) {
            m_targetBitrateKbps = bandwidthCeilingKbps;
            m_lastPressureMs = nowMs;
            m_haveLastPressure = true;
            decision.preferBaseTemporalLayer = true;
            decision.reason = SwVtpAdaptiveBitrateDecision::Reason::NetworkPressure;
        } else if (canUpshift_(stats, nowMs)) {
            m_targetBitrateKbps = scaledBitrate_(m_targetBitrateKbps,
                                                 m_policy.upshiftPercent);
            m_lastUpshiftMs = nowMs;
            m_haveLastUpshift = true;
            decision.reason = SwVtpAdaptiveBitrateDecision::Reason::UpshiftProbe;
        } else {
            decision.reason = SwVtpAdaptiveBitrateDecision::Reason::Stable;
        }

        m_targetBitrateKbps = clampBitrate_(m_targetBitrateKbps);
        decision.targetBitrateKbps = m_targetBitrateKbps;
        return decision;
    }

private:
    uint32_t clampBitrate_(uint32_t bitrateKbps) const {
        const uint32_t minBitrate = m_policy.minBitrateKbps == 0U ? 1U : m_policy.minBitrateKbps;
        const uint32_t maxBitrate = std::max(minBitrate, m_policy.maxBitrateKbps);
        return std::min(maxBitrate, std::max(minBitrate, bitrateKbps));
    }

    uint32_t scaledBitrate_(uint32_t bitrateKbps, uint16_t percent) const {
        return clampBitrate_(scaleRaw_(bitrateKbps, percent));
    }

    uint32_t scaleRaw_(uint32_t bitrateKbps, uint16_t percent) const {
        const uint64_t scaled =
            (static_cast<uint64_t>(bitrateKbps) * static_cast<uint64_t>(percent)) / 100ULL;
        return static_cast<uint32_t>(std::min<uint64_t>(scaled,
                                                        static_cast<uint64_t>(
                                                            std::numeric_limits<uint32_t>::max())));
    }

    uint32_t bandwidthCeiling_(const SwVtpReceiverStats& stats) const {
        if (stats.estimatedBandwidthKbps == 0U) {
            return 0U;
        }
        return clampBitrate_(scaleRaw_(stats.estimatedBandwidthKbps,
                                       m_policy.bandwidthSafetyPercent));
    }

    void applyBandwidthCeiling_(uint32_t bandwidthCeilingKbps) {
        if (bandwidthCeilingKbps > 0U && m_targetBitrateKbps > bandwidthCeilingKbps) {
            m_targetBitrateKbps = bandwidthCeilingKbps;
        }
    }

    bool canUpshift_(const SwVtpReceiverStats& stats, uint64_t nowMs) const {
        if (m_haveLastUpshift &&
            nowMs < m_lastUpshiftMs + m_policy.upshiftCooldownMs) {
            return false;
        }
        if (m_haveLastPressure &&
            nowMs < m_lastPressureMs + m_policy.upshiftCooldownMs) {
            return false;
        }
        if (stats.lossPermille != 0U || stats.nackPermille != 0U) {
            return false;
        }
        if (stats.receiveQueueMs != 0U && stats.receiveQueueMs >= m_policy.targetQueueMs / 2U) {
            return false;
        }
        if (stats.decodeQueueMs != 0U && stats.decodeQueueMs >= m_policy.targetQueueMs / 2U) {
            return false;
        }
        if (stats.renderQueueMs != 0U && stats.renderQueueMs >= m_policy.targetQueueMs / 2U) {
            return false;
        }
        if (stats.estimatedBandwidthKbps == 0U) {
            return m_targetBitrateKbps < m_policy.maxBitrateKbps;
        }
        return scaledBitrate_(m_targetBitrateKbps, m_policy.upshiftPercent) <=
               clampBitrate_(scaleRaw_(stats.estimatedBandwidthKbps,
                                       m_policy.upshiftBandwidthHeadroomPercent));
    }

    SwVtpAdaptiveBitratePolicy m_policy{};
    uint32_t m_targetBitrateKbps{0};
    uint64_t m_lastUpshiftMs{0};
    uint64_t m_lastPressureMs{0};
    bool m_haveLastUpshift{false};
    bool m_haveLastPressure{false};
};

inline void swVtpAppendU8(SwByteArray& out, uint8_t value) {
    out.append(static_cast<char>(value));
}

inline void swVtpAppendU16(SwByteArray& out, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>((value >> 8) & 0xFFU),
        static_cast<char>(value & 0xFFU)
    };
    out.append(bytes, 2);
}

inline void swVtpAppendU32(SwByteArray& out, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>((value >> 24) & 0xFFU),
        static_cast<char>((value >> 16) & 0xFFU),
        static_cast<char>((value >> 8) & 0xFFU),
        static_cast<char>(value & 0xFFU)
    };
    out.append(bytes, 4);
}

inline void swVtpAppendU64(SwByteArray& out, uint64_t value) {
    const char bytes[8] = {
        static_cast<char>((value >> 56) & 0xFFU),
        static_cast<char>((value >> 48) & 0xFFU),
        static_cast<char>((value >> 40) & 0xFFU),
        static_cast<char>((value >> 32) & 0xFFU),
        static_cast<char>((value >> 24) & 0xFFU),
        static_cast<char>((value >> 16) & 0xFFU),
        static_cast<char>((value >> 8) & 0xFFU),
        static_cast<char>(value & 0xFFU)
    };
    out.append(bytes, 8);
}

inline int64_t swVtpReadSignedClockDelta(uint64_t lhs, uint64_t rhs) {
    if (lhs >= rhs) {
        const uint64_t delta = lhs - rhs;
        return delta > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
                   ? std::numeric_limits<int64_t>::max()
                   : static_cast<int64_t>(delta);
    }
    const uint64_t delta = rhs - lhs;
    return delta > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
               ? std::numeric_limits<int64_t>::min()
               : -static_cast<int64_t>(delta);
}

inline uint64_t swVtpPositiveDelta(uint64_t newer, uint64_t older) {
    return newer >= older ? newer - older : 0U;
}

inline uint8_t swVtpClockConfidencePercent(uint64_t oneWayUncertaintyUs) {
    const uint64_t uncertaintyMs = oneWayUncertaintyUs / 1000U;
    if (uncertaintyMs >= 90U) {
        return 10U;
    }
    return static_cast<uint8_t>(100U - uncertaintyMs);
}

inline int64_t swVtpAverageClockDeltas(int64_t lhs, int64_t rhs) {
    return lhs / 2 + rhs / 2 + (lhs % 2 + rhs % 2) / 2;
}

inline uint32_t swVtpMakeIpv4Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24U) |
           (static_cast<uint32_t>(b) << 16U) |
           (static_cast<uint32_t>(c) << 8U) |
           static_cast<uint32_t>(d);
}

inline uint8_t swVtpIpv4Octet(uint32_t ipv4, uint8_t index) {
    return static_cast<uint8_t>((ipv4 >> ((3U - index) * 8U)) & 0xFFU);
}

inline bool swVtpParseIpv4Address(const char* text, uint32_t& outIpv4) {
    if (!text || !*text) {
        return false;
    }

    uint8_t octets[4] = {};
    uint8_t octetIndex = 0;
    uint32_t value = 0;
    bool hasDigit = false;
    for (const char* cursor = text; ; ++cursor) {
        const char ch = *cursor;
        if (ch >= '0' && ch <= '9') {
            hasDigit = true;
            value = value * 10U + static_cast<uint32_t>(ch - '0');
            if (value > 255U) {
                return false;
            }
        } else if (ch == '.' || ch == '\0') {
            if (!hasDigit || octetIndex >= 4U) {
                return false;
            }
            octets[octetIndex++] = static_cast<uint8_t>(value);
            value = 0;
            hasDigit = false;
            if (ch == '\0') {
                break;
            }
        } else {
            return false;
        }
    }

    if (octetIndex != 4U) {
        return false;
    }

    outIpv4 = swVtpMakeIpv4Address(octets[0], octets[1], octets[2], octets[3]);
    return true;
}

inline bool swVtpIsIpv4BroadcastAddress(uint32_t ipv4) {
    return ipv4 == kSwVtpIpv4Broadcast;
}

inline bool swVtpIsIpv4MulticastAddress(uint32_t ipv4) {
    const uint8_t first = swVtpIpv4Octet(ipv4, 0);
    return first >= 224U && first <= 239U;
}

inline bool swVtpIsIpv4Multicast239Address(uint32_t ipv4) {
    return swVtpIpv4Octet(ipv4, 0) == 239U;
}

inline bool swVtpIsIpv4UnicastAddress(uint32_t ipv4) {
    const uint8_t first = swVtpIpv4Octet(ipv4, 0);
    return ipv4 != kSwVtpIpv4Any &&
           !swVtpIsIpv4BroadcastAddress(ipv4) &&
           first < 224U;
}

inline bool swVtpIsIpv4UnicastClientMask(uint32_t maskIpv4) {
    bool wildcardStarted = false;
    for (uint8_t i = 0; i < 4U; ++i) {
        const uint8_t octet = swVtpIpv4Octet(maskIpv4, i);
        if (wildcardStarted && octet != 0U) {
            return false;
        }
        if (octet == 0U) {
            wildcardStarted = true;
        }
    }
    if (maskIpv4 == kSwVtpIpv4Any) {
        return true;
    }
    const uint8_t first = swVtpIpv4Octet(maskIpv4, 0);
    return first < 224U;
}

inline bool swVtpUnicastMaskAllowsClient(uint32_t maskIpv4, uint32_t clientIpv4) {
    if (!swVtpIsIpv4UnicastClientMask(maskIpv4) ||
        !swVtpIsIpv4UnicastAddress(clientIpv4)) {
        return false;
    }
    for (uint8_t i = 0; i < 4U; ++i) {
        const uint8_t maskOctet = swVtpIpv4Octet(maskIpv4, i);
        if (maskOctet == 0U) {
            return true;
        }
        if (maskOctet != swVtpIpv4Octet(clientIpv4, i)) {
            return false;
        }
    }
    return true;
}

inline SwVtpUdpEndpoint swVtpMakeUnicastEndpoint(uint32_t clientMaskIpv4,
                                                 uint16_t port) {
    SwVtpUdpEndpoint endpoint;
    endpoint.deliveryMode = SwVtpDeliveryMode::Unicast;
    endpoint.ipv4 = clientMaskIpv4;
    endpoint.port = port;
    endpoint.ttl = 1;
    return endpoint;
}

inline SwVtpUdpEndpoint swVtpMakeBroadcastEndpoint(uint16_t port) {
    SwVtpUdpEndpoint endpoint;
    endpoint.deliveryMode = SwVtpDeliveryMode::Broadcast;
    endpoint.ipv4 = kSwVtpIpv4Broadcast;
    endpoint.port = port;
    endpoint.ttl = 1;
    return endpoint;
}

inline SwVtpUdpEndpoint swVtpMakeMulticast239Endpoint(uint32_t groupIpv4,
                                                       uint16_t port,
                                                       uint8_t ttl = 1,
                                                       bool loopback = false) {
    SwVtpUdpEndpoint endpoint;
    endpoint.deliveryMode = SwVtpDeliveryMode::Multicast239;
    endpoint.ipv4 = groupIpv4;
    endpoint.port = port;
    endpoint.ttl = ttl;
    endpoint.flags = loopback ? SwVtpEndpointFlag_MulticastLoopback
                              : SwVtpEndpointFlag_None;
    return endpoint;
}

inline bool SwVtpUdpEndpoint::isValid() const {
    if (port == 0U || ttl == 0U) {
        return false;
    }
    switch (deliveryMode) {
    case SwVtpDeliveryMode::Unicast:
        return swVtpIsIpv4UnicastClientMask(ipv4);
    case SwVtpDeliveryMode::Broadcast:
        return swVtpIsIpv4BroadcastAddress(ipv4);
    case SwVtpDeliveryMode::Multicast239:
        return swVtpIsIpv4Multicast239Address(ipv4);
    case SwVtpDeliveryMode::Invalid:
    default:
        return false;
    }
}

inline bool SwVtpStreamConfig::isValid() const {
    return streamId != 0U &&
           trackId != 0U &&
           trackType != SwVtpTrackType::Unknown &&
           codec != SwVtpCodec::Unknown &&
           endpoint.isValid();
}

inline bool SwVtpClientAnnouncement::isValid() const {
    return streamId != 0U &&
           receivePort != 0U &&
           swVtpIsIpv4UnicastAddress(clientIpv4);
}

inline bool swVtpClientAnnouncementPayloadIsWellFormed(
    const SwVtpClientAnnouncement& client) {
    return client.streamId != 0U &&
           client.receivePort != 0U &&
           (client.clientIpv4 == kSwVtpIpv4Any ||
            swVtpIsIpv4UnicastAddress(client.clientIpv4));
}

inline bool swVtpEndpointAcceptsClient(const SwVtpUdpEndpoint& endpoint,
                                       const SwVtpClientAnnouncement& client) {
    return endpoint.deliveryMode == SwVtpDeliveryMode::Unicast &&
           endpoint.port != 0U &&
           endpoint.isValid() &&
           client.isValid() &&
           swVtpUnicastMaskAllowsClient(endpoint.ipv4, client.clientIpv4);
}

inline bool swVtpStreamConfigAcceptsClient(const SwVtpStreamConfig& config,
                                           const SwVtpClientAnnouncement& client) {
    return config.isValid() &&
           client.streamId == config.streamId &&
           swVtpEndpointAcceptsClient(config.endpoint, client);
}

inline uint8_t swVtpReadU8(const uint8_t* data, size_t& offset) {
    return data[offset++];
}

inline uint16_t swVtpReadU16(const uint8_t* data, size_t& offset) {
    const uint16_t value =
        static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) |
                              static_cast<uint16_t>(data[offset + 1]));
    offset += 2;
    return value;
}

inline uint32_t swVtpReadU32(const uint8_t* data, size_t& offset) {
    const uint32_t value =
        (static_cast<uint32_t>(data[offset]) << 24) |
        (static_cast<uint32_t>(data[offset + 1]) << 16) |
        (static_cast<uint32_t>(data[offset + 2]) << 8) |
        static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return value;
}

inline uint64_t swVtpReadU64(const uint8_t* data, size_t& offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint64_t>(data[offset + static_cast<size_t>(i)]);
    }
    offset += 8;
    return value;
}

inline SwByteArray swVtpSerializeHeader(const SwVtpHeader& header) {
    SwByteArray out;
    out.reserve(kSwVtpHeaderBytes);
    swVtpAppendU32(out, kSwVtpMagic);
    swVtpAppendU8(out, header.version);
    swVtpAppendU8(out, static_cast<uint8_t>(header.messageType));
    swVtpAppendU16(out, header.headerBytes);
    swVtpAppendU32(out, header.flags);
    swVtpAppendU16(out, header.streamId);
    swVtpAppendU16(out, header.trackId);
    swVtpAppendU8(out, static_cast<uint8_t>(header.trackType));
    swVtpAppendU8(out, static_cast<uint8_t>(header.codec));
    swVtpAppendU8(out, header.temporalLayer);
    swVtpAppendU8(out, header.spatialLayer);
    swVtpAppendU32(out, header.frameId);
    swVtpAppendU16(out, header.fragmentIndex);
    swVtpAppendU16(out, header.fragmentCount);
    swVtpAppendU16(out, header.payloadBytes);
    swVtpAppendU16(out, header.fecGroupId);
    swVtpAppendU64(out, header.ptsUs);
    swVtpAppendU64(out, header.captureTimeUs);
    swVtpAppendU64(out, header.deadlineUs);
    swVtpAppendU64(out, header.sendTimeUs);
    return out;
}

inline bool swVtpParseHeader(const SwByteArray& bytes, SwVtpHeader& outHeader) {
    if (bytes.size() < kSwVtpHeaderBytes || !bytes.constData()) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(bytes.constData());
    size_t offset = 0;
    const uint32_t magic = swVtpReadU32(data, offset);
    if (magic != kSwVtpMagic) {
        return false;
    }

    SwVtpHeader header;
    header.version = swVtpReadU8(data, offset);
    header.messageType = static_cast<SwVtpMessageType>(swVtpReadU8(data, offset));
    header.headerBytes = swVtpReadU16(data, offset);
    header.flags = swVtpReadU32(data, offset);
    header.streamId = swVtpReadU16(data, offset);
    header.trackId = swVtpReadU16(data, offset);
    header.trackType = static_cast<SwVtpTrackType>(swVtpReadU8(data, offset));
    header.codec = static_cast<SwVtpCodec>(swVtpReadU8(data, offset));
    header.temporalLayer = swVtpReadU8(data, offset);
    header.spatialLayer = swVtpReadU8(data, offset);
    header.frameId = swVtpReadU32(data, offset);
    header.fragmentIndex = swVtpReadU16(data, offset);
    header.fragmentCount = swVtpReadU16(data, offset);
    header.payloadBytes = swVtpReadU16(data, offset);
    header.fecGroupId = swVtpReadU16(data, offset);
    header.ptsUs = swVtpReadU64(data, offset);
    header.captureTimeUs = swVtpReadU64(data, offset);
    header.deadlineUs = swVtpReadU64(data, offset);
    header.sendTimeUs = swVtpReadU64(data, offset);

    if (!header.isValid()) {
        return false;
    }
    if (bytes.size() < static_cast<size_t>(header.headerBytes) + header.payloadBytes) {
        return false;
    }

    outHeader = header;
    return true;
}

inline SwByteArray swVtpSerializeDatagram(const SwVtpDatagram& datagram) {
    SwVtpHeader header = datagram.header;
    header.headerBytes = kSwVtpHeaderBytes;
    header.payloadBytes = static_cast<uint16_t>(datagram.payload.size());
    SwByteArray out = swVtpSerializeHeader(header);
    out.append(datagram.payload);
    return out;
}

inline bool swVtpParseDatagram(const SwByteArray& bytes, SwVtpDatagram& outDatagram) {
    SwVtpHeader header;
    if (!swVtpParseHeader(bytes, header)) {
        return false;
    }
    const size_t expectedBytes =
        static_cast<size_t>(header.headerBytes) + static_cast<size_t>(header.payloadBytes);
    if (bytes.size() != expectedBytes) {
        return false;
    }

    SwVtpDatagram datagram;
    datagram.header = header;
    datagram.payload = bytes.mid(static_cast<int>(header.headerBytes),
                                 static_cast<int>(header.payloadBytes));
    outDatagram = datagram;
    return true;
}

inline SwByteArray swVtpSerializeNack(const SwVtpNackRequest& request) {
    SwByteArray payload;
    swVtpAppendU32(payload, request.frameId);
    swVtpAppendU16(payload, static_cast<uint16_t>(request.missingFragments.size()));
    for (SwList<uint16_t>::const_iterator it = request.missingFragments.begin();
         it != request.missingFragments.end();
         ++it) {
        swVtpAppendU16(payload, *it);
    }
    return payload;
}

inline bool swVtpParseNackPayload(uint16_t streamId,
                                  uint16_t trackId,
                                  const SwByteArray& payload,
                                  SwVtpNackRequest& outRequest) {
    if (payload.size() < 6 || !payload.constData()) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpNackRequest request;
    request.streamId = streamId;
    request.trackId = trackId;
    request.frameId = swVtpReadU32(data, offset);
    const uint16_t count = swVtpReadU16(data, offset);
    if (payload.size() < static_cast<size_t>(6U + count * 2U)) {
        return false;
    }
    for (uint16_t i = 0; i < count; ++i) {
        request.missingFragments.append(swVtpReadU16(data, offset));
    }
    outRequest = request;
    return request.isValid();
}

inline void swVtpAppendUdpEndpoint(SwByteArray& payload,
                                   const SwVtpUdpEndpoint& endpoint) {
    swVtpAppendU8(payload, static_cast<uint8_t>(endpoint.deliveryMode));
    swVtpAppendU8(payload, endpoint.flags);
    swVtpAppendU16(payload, endpoint.port);
    swVtpAppendU32(payload, endpoint.ipv4);
    swVtpAppendU8(payload, endpoint.ttl);
    swVtpAppendU8(payload, 0);
    swVtpAppendU16(payload, 0);
}

inline bool swVtpReadUdpEndpoint(const uint8_t* data,
                                 size_t& offset,
                                 SwVtpUdpEndpoint& outEndpoint) {
    SwVtpUdpEndpoint endpoint;
    endpoint.deliveryMode = static_cast<SwVtpDeliveryMode>(swVtpReadU8(data, offset));
    endpoint.flags = swVtpReadU8(data, offset);
    endpoint.port = swVtpReadU16(data, offset);
    endpoint.ipv4 = swVtpReadU32(data, offset);
    endpoint.ttl = swVtpReadU8(data, offset);
    const uint8_t reserved8 = swVtpReadU8(data, offset);
    const uint16_t reserved16 = swVtpReadU16(data, offset);
    if (reserved8 != 0U || reserved16 != 0U || !endpoint.isValid()) {
        return false;
    }
    outEndpoint = endpoint;
    return true;
}

inline SwByteArray swVtpSerializeUdpEndpoint(const SwVtpUdpEndpoint& endpoint) {
    SwByteArray payload;
    swVtpAppendUdpEndpoint(payload, endpoint);
    return payload;
}

inline bool swVtpParseUdpEndpointPayload(const SwByteArray& payload,
                                         SwVtpUdpEndpoint& outEndpoint) {
    if (payload.size() != kSwVtpUdpEndpointBytes || !payload.constData()) {
        return false;
    }
    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    return swVtpReadUdpEndpoint(data, offset, outEndpoint);
}

inline SwByteArray swVtpSerializeStreamConfig(const SwVtpStreamConfig& config) {
    SwByteArray payload;
    swVtpAppendU16(payload, config.streamId);
    swVtpAppendU16(payload, config.trackId);
    swVtpAppendU8(payload, static_cast<uint8_t>(config.trackType));
    swVtpAppendU8(payload, static_cast<uint8_t>(config.codec));
    swVtpAppendU16(payload, 0);
    swVtpAppendUdpEndpoint(payload, config.endpoint);
    return payload;
}

inline bool swVtpParseStreamConfigPayload(const SwByteArray& payload,
                                          SwVtpStreamConfig& outConfig) {
    if (payload.size() != kSwVtpStreamConfigBytes || !payload.constData()) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpStreamConfig config;
    config.streamId = swVtpReadU16(data, offset);
    config.trackId = swVtpReadU16(data, offset);
    config.trackType = static_cast<SwVtpTrackType>(swVtpReadU8(data, offset));
    config.codec = static_cast<SwVtpCodec>(swVtpReadU8(data, offset));
    const uint16_t reserved = swVtpReadU16(data, offset);
    if (reserved != 0U ||
        !swVtpReadUdpEndpoint(data, offset, config.endpoint) ||
        !config.isValid()) {
        return false;
    }
    outConfig = config;
    return true;
}

inline SwByteArray swVtpSerializeClientAnnouncement(const SwVtpClientAnnouncement& client) {
    SwByteArray payload;
    swVtpAppendU16(payload, client.streamId);
    swVtpAppendU16(payload, client.receivePort);
    swVtpAppendU32(payload, client.clientIpv4);
    return payload;
}

inline bool swVtpParseClientAnnouncementPayload(const SwByteArray& payload,
                                                SwVtpClientAnnouncement& outClient) {
    if (payload.size() != kSwVtpClientAnnouncementBytes || !payload.constData()) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpClientAnnouncement client;
    client.streamId = swVtpReadU16(data, offset);
    client.receivePort = swVtpReadU16(data, offset);
    client.clientIpv4 = swVtpReadU32(data, offset);
    if (!swVtpClientAnnouncementPayloadIsWellFormed(client)) {
        return false;
    }
    outClient = client;
    return true;
}

inline SwByteArray swVtpSerializeReceiverStats(const SwVtpReceiverStats& stats) {
    SwByteArray payload;
    swVtpAppendU16(payload, stats.streamId);
    swVtpAppendU16(payload, stats.trackId);
    swVtpAppendU32(payload, stats.lastFrameId);
    swVtpAppendU32(payload, stats.estimatedBandwidthKbps);
    swVtpAppendU16(payload, stats.rttMs);
    swVtpAppendU16(payload, stats.jitterMs);
    swVtpAppendU16(payload, stats.lossPermille);
    swVtpAppendU16(payload, stats.nackPermille);
    swVtpAppendU16(payload, stats.receiveQueueMs);
    swVtpAppendU16(payload, stats.decodeQueueMs);
    swVtpAppendU16(payload, stats.renderQueueMs);
    swVtpAppendU16(payload, stats.transferLatencyMs);
    swVtpAppendU16(payload, stats.captureLatencyMs);
    swVtpAppendU16(payload, stats.clockUncertaintyMs);
    swVtpAppendU32(payload, stats.droppedFrames);
    return payload;
}

inline bool swVtpParseReceiverStatsPayload(const SwByteArray& payload,
                                           SwVtpReceiverStats& outStats) {
    if (payload.size() < 36U || !payload.constData()) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpReceiverStats stats;
    stats.streamId = swVtpReadU16(data, offset);
    stats.trackId = swVtpReadU16(data, offset);
    stats.lastFrameId = swVtpReadU32(data, offset);
    stats.estimatedBandwidthKbps = swVtpReadU32(data, offset);
    stats.rttMs = swVtpReadU16(data, offset);
    stats.jitterMs = swVtpReadU16(data, offset);
    stats.lossPermille = swVtpReadU16(data, offset);
    stats.nackPermille = swVtpReadU16(data, offset);
    stats.receiveQueueMs = swVtpReadU16(data, offset);
    stats.decodeQueueMs = swVtpReadU16(data, offset);
    stats.renderQueueMs = swVtpReadU16(data, offset);
    stats.transferLatencyMs = swVtpReadU16(data, offset);
    stats.captureLatencyMs = swVtpReadU16(data, offset);
    stats.clockUncertaintyMs = swVtpReadU16(data, offset);
    stats.droppedFrames = swVtpReadU32(data, offset);
    outStats = stats;
    return true;
}

inline SwByteArray swVtpSerializeBitrateControl(const SwVtpBitrateControl& control) {
    SwByteArray payload;
    swVtpAppendU16(payload, control.streamId);
    swVtpAppendU16(payload, control.trackId);
    swVtpAppendU32(payload, control.targetBitrateKbps);
    swVtpAppendU32(payload, control.encoderBitrateKbps);
    swVtpAppendU32(payload, control.estimatedBandwidthKbps);
    swVtpAppendU8(payload, control.reason);
    swVtpAppendU8(payload, control.flags);
    swVtpAppendU16(payload, control.reserved);
    return payload;
}

inline bool swVtpParseBitrateControlPayload(const SwByteArray& payload,
                                            SwVtpBitrateControl& outControl) {
    if (payload.size() != kSwVtpBitrateControlBytes || !payload.constData()) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpBitrateControl control;
    control.streamId = swVtpReadU16(data, offset);
    control.trackId = swVtpReadU16(data, offset);
    control.targetBitrateKbps = swVtpReadU32(data, offset);
    control.encoderBitrateKbps = swVtpReadU32(data, offset);
    control.estimatedBandwidthKbps = swVtpReadU32(data, offset);
    control.reason = swVtpReadU8(data, offset);
    control.flags = swVtpReadU8(data, offset);
    control.reserved = swVtpReadU16(data, offset);
    if (control.reserved != 0U || !control.isValid()) {
        return false;
    }
    outControl = control;
    return true;
}

inline SwByteArray swVtpSerializeClockSyncPing(const SwVtpClockSyncPing& ping) {
    SwByteArray payload;
    swVtpAppendU32(payload, ping.syncId);
    swVtpAppendU64(payload, ping.clientSendTimeUs);
    return payload;
}

inline bool swVtpParseClockSyncPing(const SwByteArray& payload,
                                    SwVtpClockSyncPing& outPing) {
    if (payload.size() < 12U || !payload.constData()) {
        return false;
    }
    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpClockSyncPing ping;
    ping.syncId = swVtpReadU32(data, offset);
    ping.clientSendTimeUs = swVtpReadU64(data, offset);
    outPing = ping;
    return ping.syncId != 0U;
}

inline SwByteArray swVtpSerializeClockSyncPong(const SwVtpClockSyncPong& pong) {
    SwByteArray payload;
    swVtpAppendU32(payload, pong.syncId);
    swVtpAppendU64(payload, pong.clientSendTimeUs);
    swVtpAppendU64(payload, pong.serverReceiveTimeUs);
    swVtpAppendU64(payload, pong.serverSendTimeUs);
    return payload;
}

inline bool swVtpParseClockSyncPong(const SwByteArray& payload,
                                    SwVtpClockSyncPong& outPong) {
    if (payload.size() < 28U || !payload.constData()) {
        return false;
    }
    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.constData());
    size_t offset = 0;
    SwVtpClockSyncPong pong;
    pong.syncId = swVtpReadU32(data, offset);
    pong.clientSendTimeUs = swVtpReadU64(data, offset);
    pong.serverReceiveTimeUs = swVtpReadU64(data, offset);
    pong.serverSendTimeUs = swVtpReadU64(data, offset);
    outPong = pong;
    return pong.syncId != 0U &&
           pong.serverSendTimeUs >= pong.serverReceiveTimeUs;
}

inline SwVtpClockEstimate swVtpEstimateClock(const SwVtpClockSyncSample& sample) {
    SwVtpClockEstimate estimate;
    if (sample.syncId == 0U ||
        sample.clientReceiveTimeUs < sample.clientSendTimeUs ||
        sample.serverSendTimeUs < sample.serverReceiveTimeUs) {
        return estimate;
    }

    const uint64_t totalRttUs =
        swVtpPositiveDelta(sample.clientReceiveTimeUs, sample.clientSendTimeUs);
    const uint64_t serverProcessingUs =
        swVtpPositiveDelta(sample.serverSendTimeUs, sample.serverReceiveTimeUs);
    if (totalRttUs < serverProcessingUs) {
        return estimate;
    }

    const int64_t left =
        swVtpReadSignedClockDelta(sample.clientSendTimeUs, sample.serverReceiveTimeUs);
    const int64_t right =
        swVtpReadSignedClockDelta(sample.clientReceiveTimeUs, sample.serverSendTimeUs);

    estimate.valid = true;
    estimate.serverToClientOffsetUs = swVtpAverageClockDeltas(left, right);
    estimate.rttUs = totalRttUs;
    estimate.serverProcessingUs = serverProcessingUs;
    estimate.networkRttUs = totalRttUs - serverProcessingUs;
    estimate.oneWayUncertaintyUs = estimate.networkRttUs / 2U;
    estimate.confidencePercent = swVtpClockConfidencePercent(estimate.oneWayUncertaintyUs);
    return estimate;
}

inline bool swVtpServerTimeToClientTime(uint64_t serverTimeUs,
                                        const SwVtpClockEstimate& estimate,
                                        uint64_t& outClientTimeUs) {
    if (!estimate.valid) {
        return false;
    }
    const int64_t offset = estimate.serverToClientOffsetUs;
    if (offset >= 0) {
        const uint64_t unsignedOffset = static_cast<uint64_t>(offset);
        if (serverTimeUs > std::numeric_limits<uint64_t>::max() - unsignedOffset) {
            return false;
        }
        outClientTimeUs = serverTimeUs + unsignedOffset;
        return true;
    }
    const uint64_t unsignedOffset = static_cast<uint64_t>(-offset);
    if (serverTimeUs < unsignedOffset) {
        return false;
    }
    outClientTimeUs = serverTimeUs - unsignedOffset;
    return true;
}

inline SwVtpFrameLatencySample swVtpMeasureFrameLatency(const SwVtpHeader& header,
                                                        const SwVtpClockEstimate& estimate,
                                                        uint64_t clientReceiveTimeUs) {
    SwVtpFrameLatencySample sample;
    uint64_t sendClientTimeUs = 0;
    if (header.sendTimeUs == 0U ||
        !swVtpServerTimeToClientTime(header.sendTimeUs, estimate, sendClientTimeUs)) {
        return sample;
    }
    sample.valid = true;
    sample.estimatedServerSendClientTimeUs = sendClientTimeUs;
    sample.transferLatencyUs = swVtpPositiveDelta(clientReceiveTimeUs, sendClientTimeUs);
    sample.oneWayUncertaintyUs = estimate.oneWayUncertaintyUs;
    sample.confidencePercent = estimate.confidencePercent;

    if (header.captureTimeUs != 0U) {
        uint64_t captureClientTimeUs = 0;
        if (swVtpServerTimeToClientTime(header.captureTimeUs, estimate, captureClientTimeUs)) {
            sample.captureLatencyValid = true;
            sample.estimatedCaptureClientTimeUs = captureClientTimeUs;
            sample.captureToReceiveUs =
                swVtpPositiveDelta(clientReceiveTimeUs, captureClientTimeUs);
        }
    }
    return sample;
}
