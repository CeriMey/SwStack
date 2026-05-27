#pragma once

/**
 * @file src/media/swvtp/SwVtpFrameReassembler.h
 * @brief Generic SwVTP video frame reassembler for AV1, H264 and H265 frame fragments.
 */

#include "media/SwVideoPacket.h"
#include "media/swvtp/SwVtpProtocol.h"

#include <cstdint>
#include <map>
#include <vector>

inline SwVideoPacket::Codec swVtpVideoCodecToPacketCodec(SwVtpCodec codec) {
    switch (codec) {
    case SwVtpCodec::H264:
        return SwVideoPacket::Codec::H264;
    case SwVtpCodec::H265:
        return SwVideoPacket::Codec::H265;
    case SwVtpCodec::AV1:
        return SwVideoPacket::Codec::AV1;
    default:
        break;
    }
    return SwVideoPacket::Codec::Unknown;
}

inline SwString swVtpVideoCodecName(SwVtpCodec codec) {
    switch (codec) {
    case SwVtpCodec::H264:
        return "h264";
    case SwVtpCodec::H265:
        return "h265";
    case SwVtpCodec::AV1:
        return "av1";
    default:
        break;
    }
    return "unknown";
}

class SwVtpFrameReassembler {
public:
    enum class PushStatus {
        Invalid,
        Accepted,
        Duplicate,
        Completed,
        Stale
    };

    struct PushResult {
        PushStatus status{PushStatus::Invalid};
        SwVideoPacket packet{};

        bool completed() const { return status == PushStatus::Completed; }
    };

    struct Snapshot {
        std::size_t bufferedFrames{0};
        std::size_t bufferedBytes{0};
        uint64_t acceptedFragments{0};
        uint64_t duplicateFragments{0};
        uint64_t completedFrames{0};
        uint64_t staleFragments{0};
        uint64_t droppedFrames{0};
    };

    void setLimits(std::size_t maxBufferedFrames, std::size_t maxFrameBytes) {
        m_maxBufferedFrames = maxBufferedFrames == 0U ? 1U : maxBufferedFrames;
        m_maxFrameBytes = maxFrameBytes == 0U ? 1U : maxFrameBytes;
    }

    void reset() {
        m_frames.clear();
        m_bufferedBytes = 0;
        m_acceptedFragments = 0;
        m_duplicateFragments = 0;
        m_completedFrames = 0;
        m_staleFragments = 0;
        m_droppedFrames = 0;
    }

    PushResult pushSerializedDatagram(const SwByteArray& bytes, uint64_t nowUs) {
        SwVtpDatagram datagram;
        if (!swVtpParseDatagram(bytes, datagram)) {
            return PushResult();
        }
        return pushDatagram(datagram, nowUs);
    }

    PushResult pushDatagram(const SwVtpDatagram& datagram, uint64_t nowUs) {
        PushResult result;
        const SwVtpHeader& header = datagram.header;
        if (header.messageType != SwVtpMessageType::FrameFragment ||
            header.trackType != SwVtpTrackType::Video ||
            swVtpVideoCodecToPacketCodec(header.codec) == SwVideoPacket::Codec::Unknown ||
            header.fragmentCount == 0U ||
            header.fragmentIndex >= header.fragmentCount ||
            datagram.payload.size() != header.payloadBytes) {
            return result;
        }
        if (header.deadlineUs != 0U && nowUs > header.deadlineUs) {
            ++m_staleFragments;
            result.status = PushStatus::Stale;
            return result;
        }

        expire(nowUs);
        FrameKey key;
        key.streamId = header.streamId;
        key.trackId = header.trackId;
        key.frameId = header.frameId;

        FrameState& state = m_frames[key];
        if (!state.initialized) {
            state.initialized = true;
            state.firstHeader = header;
            state.codec = header.codec;
            state.deadlineUs = header.deadlineUs;
            state.fragments.resize(header.fragmentCount);
            state.received.assign(header.fragmentCount, false);
        }

        if (state.fragments.size() != header.fragmentCount || state.codec != header.codec) {
            dropFrame_(key);
            return result;
        }
        if (state.received[header.fragmentIndex]) {
            ++m_duplicateFragments;
            result.status = PushStatus::Duplicate;
            return result;
        }

        const std::size_t incomingBytes = static_cast<std::size_t>(datagram.payload.size());
        if (state.bytes + incomingBytes > m_maxFrameBytes) {
            dropFrame_(key);
            return result;
        }

        state.fragments[header.fragmentIndex] = datagram.payload;
        state.received[header.fragmentIndex] = true;
        state.bytes += incomingBytes;
        state.receivedCount += 1U;
        state.keyFrame = state.keyFrame || header.hasFlag(SwVtpFlag_KeyFrame);
        state.discontinuity = state.discontinuity || header.hasFlag(SwVtpFlag_Discontinuity);
        state.codecConfig = state.codecConfig || header.hasFlag(SwVtpFlag_CodecConfig);
        m_bufferedBytes += incomingBytes;
        ++m_acceptedFragments;

        if (state.receivedCount == state.fragments.size()) {
            result.status = PushStatus::Completed;
            result.packet = completeFrame_(key);
            ++m_completedFrames;
            return result;
        }

        trimBufferedFrames_();
        result.status = PushStatus::Accepted;
        return result;
    }

    std::size_t expire(uint64_t nowUs) {
        std::size_t dropped = 0;
        for (std::map<FrameKey, FrameState>::iterator it = m_frames.begin();
             it != m_frames.end();) {
            if (it->second.deadlineUs != 0U && nowUs > it->second.deadlineUs) {
                m_bufferedBytes = it->second.bytes <= m_bufferedBytes
                                      ? m_bufferedBytes - it->second.bytes
                                      : 0U;
                it = m_frames.erase(it);
                ++dropped;
                ++m_droppedFrames;
            } else {
                ++it;
            }
        }
        return dropped;
    }

    bool makeNackRequest(uint16_t streamId,
                         uint16_t trackId,
                         uint32_t frameId,
                         uint64_t nowUs,
                         uint64_t retransmitBudgetUs,
                         SwVtpNackRequest& outRequest) const {
        FrameKey key;
        key.streamId = streamId;
        key.trackId = trackId;
        key.frameId = frameId;
        std::map<FrameKey, FrameState>::const_iterator it = m_frames.find(key);
        if (it == m_frames.end()) {
            return false;
        }
        const FrameState& state = it->second;
        if (state.deadlineUs != 0U && nowUs + retransmitBudgetUs >= state.deadlineUs) {
            return false;
        }

        SwVtpNackRequest request;
        request.streamId = streamId;
        request.trackId = trackId;
        request.frameId = frameId;
        for (std::size_t i = 0; i < state.received.size(); ++i) {
            if (!state.received[i]) {
                request.missingFragments.append(static_cast<uint16_t>(i));
            }
        }
        if (!request.isValid()) {
            return false;
        }
        outRequest = request;
        return true;
    }

    Snapshot snapshot() const {
        Snapshot out;
        out.bufferedFrames = m_frames.size();
        out.bufferedBytes = m_bufferedBytes;
        out.acceptedFragments = m_acceptedFragments;
        out.duplicateFragments = m_duplicateFragments;
        out.completedFrames = m_completedFrames;
        out.staleFragments = m_staleFragments;
        out.droppedFrames = m_droppedFrames;
        return out;
    }

private:
    struct FrameKey {
        uint16_t streamId{0};
        uint16_t trackId{0};
        uint32_t frameId{0};

        bool operator<(const FrameKey& other) const {
            if (streamId != other.streamId) {
                return streamId < other.streamId;
            }
            if (trackId != other.trackId) {
                return trackId < other.trackId;
            }
            return frameId < other.frameId;
        }
    };

    struct FrameState {
        bool initialized{false};
        SwVtpHeader firstHeader{};
        SwVtpCodec codec{SwVtpCodec::Unknown};
        uint64_t deadlineUs{0};
        std::vector<SwByteArray> fragments{};
        std::vector<bool> received{};
        std::size_t receivedCount{0};
        std::size_t bytes{0};
        bool keyFrame{false};
        bool discontinuity{false};
        bool codecConfig{false};
    };

    SwVideoPacket completeFrame_(const FrameKey& key) {
        std::map<FrameKey, FrameState>::iterator it = m_frames.find(key);
        if (it == m_frames.end()) {
            return SwVideoPacket();
        }

        FrameState state = it->second;
        SwByteArray payload;
        payload.reserve(state.bytes);
        for (std::size_t i = 0; i < state.fragments.size(); ++i) {
            payload.append(state.fragments[i]);
        }
        m_bufferedBytes = state.bytes <= m_bufferedBytes ? m_bufferedBytes - state.bytes : 0U;
        m_frames.erase(it);

        SwVideoPacket packet(swVtpVideoCodecToPacketCodec(state.codec),
                             payload,
                             static_cast<std::int64_t>(state.firstHeader.ptsUs),
                             static_cast<std::int64_t>(state.firstHeader.ptsUs),
                             state.keyFrame || state.codecConfig);
        packet.setDiscontinuity(state.discontinuity);
        return packet;
    }

    void dropFrame_(const FrameKey& key) {
        std::map<FrameKey, FrameState>::iterator it = m_frames.find(key);
        if (it == m_frames.end()) {
            return;
        }
        m_bufferedBytes = it->second.bytes <= m_bufferedBytes
                              ? m_bufferedBytes - it->second.bytes
                              : 0U;
        m_frames.erase(it);
        ++m_droppedFrames;
    }

    void trimBufferedFrames_() {
        while (m_frames.size() > m_maxBufferedFrames) {
            std::map<FrameKey, FrameState>::iterator oldest = m_frames.begin();
            for (std::map<FrameKey, FrameState>::iterator it = m_frames.begin();
                 it != m_frames.end();
                 ++it) {
                const bool itOlder =
                    (it->second.deadlineUs != 0U && oldest->second.deadlineUs == 0U) ||
                    (it->second.deadlineUs != 0U &&
                     oldest->second.deadlineUs != 0U &&
                     it->second.deadlineUs < oldest->second.deadlineUs);
                if (itOlder) {
                    oldest = it;
                }
            }
            m_bufferedBytes = oldest->second.bytes <= m_bufferedBytes
                                  ? m_bufferedBytes - oldest->second.bytes
                                  : 0U;
            m_frames.erase(oldest);
            ++m_droppedFrames;
        }
    }

    std::map<FrameKey, FrameState> m_frames{};
    std::size_t m_maxBufferedFrames{64};
    std::size_t m_maxFrameBytes{16U * 1024U * 1024U};
    std::size_t m_bufferedBytes{0};
    uint64_t m_acceptedFragments{0};
    uint64_t m_duplicateFragments{0};
    uint64_t m_completedFrames{0};
    uint64_t m_staleFragments{0};
    uint64_t m_droppedFrames{0};
};
