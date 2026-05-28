#pragma once

/**
 * @file src/media/swvtp/SwVtpAv1.h
 * @brief AV1-aware SwVTP packetization and deadline-based reassembly.
 */

#include "media/SwAv1Bitstream.h"
#include "media/SwVideoPacket.h"
#include "media/swvtp/SwVtpProtocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

struct SwVtpAv1PacketizerOptions {
    uint16_t streamId{1};
    uint16_t trackId{1};
    uint32_t frameId{1};
    uint64_t nowUs{0};
    uint64_t captureTimeUs{0};
    uint64_t latencyBudgetUs{70000};
    uint64_t deadlineSlackUs{0};
    std::size_t maxDatagramBytes{1200};
    uint8_t temporalLayer{0};
    uint8_t spatialLayer{0};
};

struct SwVtpAv1PacketizerResult {
    bool ok{false};
    bool keyFrame{false};
    bool containsSequenceHeader{false};
    SwByteArray sequenceHeader{};
    SwList<SwVtpDatagram> datagrams{};
    SwList<SwByteArray> serializedDatagrams{};
};

class SwVtpAv1Packetizer {
public:
    static SwVtpAv1PacketizerResult packetize(const SwVideoPacket& packet,
                                              const SwVtpAv1PacketizerOptions& options) {
        SwVtpAv1PacketizerResult result;
        if (packet.codec() != SwVideoPacket::Codec::AV1 || packet.payload().isEmpty()) {
            return result;
        }
        if (options.maxDatagramBytes <= kSwVtpHeaderBytes) {
            return result;
        }

        const SwByteArray& payload = packet.payload();
        const std::size_t payloadBytes = static_cast<std::size_t>(payload.size());
        const std::size_t maxPayloadBytes = options.maxDatagramBytes - kSwVtpHeaderBytes;
        const std::size_t fragmentCountSize =
            (payloadBytes + maxPayloadBytes - 1U) / maxPayloadBytes;
        if (fragmentCountSize == 0U || fragmentCountSize > 0xFFFFU) {
            return result;
        }

        const uint16_t fragmentCount = static_cast<uint16_t>(fragmentCountSize);
        const SwList<SwAv1ObuInfo> obus = SwAv1Bitstream::parseObus(payload);
        const SwByteArray sequenceHeader =
            SwAv1Bitstream::collectSequenceHeader(payload, obus);
        const bool containsSequenceHeader = !sequenceHeader.isEmpty();
        const bool keyFrame = packet.isKeyFrame();
        const uint64_t ptsUs = packet.pts() >= 0
                                   ? static_cast<uint64_t>(packet.pts())
                                   : options.nowUs;
        uint64_t deadlineUs = options.nowUs + options.latencyBudgetUs;
        if (deadlineUs < options.nowUs ||
            deadlineUs > std::numeric_limits<uint64_t>::max() - options.deadlineSlackUs) {
            deadlineUs = std::numeric_limits<uint64_t>::max();
        } else {
            deadlineUs += options.deadlineSlackUs;
        }

        result.keyFrame = keyFrame;
        result.containsSequenceHeader = containsSequenceHeader;
        result.sequenceHeader = sequenceHeader;
        result.datagrams.reserve(fragmentCount);
        result.serializedDatagrams.reserve(fragmentCount);

        for (uint16_t fragmentIndex = 0; fragmentIndex < fragmentCount; ++fragmentIndex) {
            const std::size_t begin = static_cast<std::size_t>(fragmentIndex) * maxPayloadBytes;
            const std::size_t remaining = payloadBytes - begin;
            const std::size_t count = std::min(maxPayloadBytes, remaining);
            const std::size_t end = begin + count;
            const bool sequenceFragment =
                SwAv1Bitstream::fragmentOverlapsObuType(obus,
                                                        begin,
                                                        end,
                                                        SwAv1ObuType::SequenceHeader);

            SwVtpDatagram datagram;
            datagram.payload = payload.mid(static_cast<int>(begin), static_cast<int>(count));
            datagram.header.version = kSwVtpVersion1;
            datagram.header.messageType = SwVtpMessageType::FrameFragment;
            datagram.header.streamId = options.streamId;
            datagram.header.trackId = options.trackId;
            datagram.header.trackType = SwVtpTrackType::Video;
            datagram.header.codec = SwVtpCodec::AV1;
            datagram.header.temporalLayer =
                options.temporalLayer != 0U ? options.temporalLayer
                                            : SwAv1Bitstream::highestTemporalLayer(obus);
            datagram.header.spatialLayer = options.spatialLayer;
            datagram.header.frameId = options.frameId;
            datagram.header.fragmentIndex = fragmentIndex;
            datagram.header.fragmentCount = fragmentCount;
            datagram.header.payloadBytes = static_cast<uint16_t>(count);
            datagram.header.ptsUs = ptsUs;
            datagram.header.captureTimeUs =
                options.captureTimeUs != 0U ? options.captureTimeUs : ptsUs;
            datagram.header.deadlineUs = deadlineUs;
            datagram.header.sendTimeUs = options.nowUs;

            uint32_t flags = SwVtpFlag_None;
            if (keyFrame) {
                flags |= SwVtpFlag_KeyFrame;
            }
            if (packet.isDiscontinuity()) {
                flags |= SwVtpFlag_Discontinuity;
            }
            if (sequenceFragment) {
                flags |= SwVtpFlag_CodecConfig | SwVtpFlag_Important;
            }
            if (fragmentIndex == 0U) {
                flags |= SwVtpFlag_FirstFragment | SwVtpFlag_Important;
            }
            if (fragmentIndex + 1U == fragmentCount) {
                flags |= SwVtpFlag_LastFragment;
            }
            if (datagram.header.temporalLayer > 0U && !sequenceFragment && !keyFrame) {
                flags |= SwVtpFlag_Droppable;
            }
            datagram.header.flags = flags;

            result.serializedDatagrams.append(swVtpSerializeDatagram(datagram));
            result.datagrams.append(datagram);
        }

        result.ok = !result.datagrams.isEmpty();
        return result;
    }
};

class SwVtpAv1Reassembler {
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
            header.codec != SwVtpCodec::AV1 ||
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
            state.deadlineUs = header.deadlineUs;
            state.fragments.resize(header.fragmentCount);
            state.received.assign(header.fragmentCount, false);
        }

        if (state.fragments.size() != header.fragmentCount) {
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

        SwVideoPacket packet(SwVideoPacket::Codec::AV1,
                             payload,
                             static_cast<std::int64_t>(state.firstHeader.ptsUs),
                             static_cast<std::int64_t>(state.firstHeader.ptsUs),
                             state.keyFrame);
        packet.setClockRate(1000000);
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
