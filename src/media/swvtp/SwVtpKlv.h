#pragma once

/**
 * @file src/media/swvtp/SwVtpKlv.h
 * @brief KLV metadata packetization and reassembly for SwVTP.
 */

#include "media/SwMediaPacket.h"
#include "media/swvtp/SwVtpProtocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

struct SwVtpKlvPacketizerOptions {
    uint16_t streamId{1};
    uint16_t trackId{2};
    uint32_t sampleId{1};
    uint64_t nowUs{0};
    uint64_t ptsUs{0};
    uint64_t captureTimeUs{0};
    uint64_t latencyBudgetUs{90000};
    std::size_t maxDatagramBytes{1200};
};

struct SwVtpKlvPacketizerResult {
    bool ok{false};
    SwList<SwVtpDatagram> datagrams{};
    SwList<SwByteArray> serializedDatagrams{};
};

class SwVtpKlvPacketizer {
public:
    static bool isKlvPacket(const SwMediaPacket& packet) {
        const SwString codec = packet.codec().toLower();
        return packet.type() == SwMediaPacket::Type::Metadata &&
               (codec.isEmpty() || codec == "klv" || codec == "smpte336m");
    }

    static SwVtpKlvPacketizerResult packetize(const SwMediaPacket& packet,
                                              const SwVtpKlvPacketizerOptions& options) {
        SwVtpKlvPacketizerResult result;
        if (!isKlvPacket(packet) || packet.payload().isEmpty()) {
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
        const uint64_t ptsUs = packet.pts() >= 0
                                   ? static_cast<uint64_t>(packet.pts())
                                   : (options.ptsUs != 0U ? options.ptsUs : options.nowUs);
        const uint64_t captureTimeUs =
            options.captureTimeUs != 0U ? options.captureTimeUs : ptsUs;
        const uint64_t deadlineUs = options.nowUs + options.latencyBudgetUs;

        result.datagrams.reserve(fragmentCount);
        result.serializedDatagrams.reserve(fragmentCount);

        for (uint16_t fragmentIndex = 0; fragmentIndex < fragmentCount; ++fragmentIndex) {
            const std::size_t begin = static_cast<std::size_t>(fragmentIndex) * maxPayloadBytes;
            const std::size_t remaining = payloadBytes - begin;
            const std::size_t count = std::min(maxPayloadBytes, remaining);

            SwVtpDatagram datagram;
            datagram.payload = payload.mid(static_cast<int>(begin), static_cast<int>(count));
            datagram.header.version = kSwVtpVersion1;
            datagram.header.messageType = SwVtpMessageType::KlvFragment;
            datagram.header.streamId = options.streamId;
            datagram.header.trackId = options.trackId;
            datagram.header.trackType = SwVtpTrackType::MetadataKlv;
            datagram.header.codec = SwVtpCodec::Klv;
            datagram.header.frameId = options.sampleId;
            datagram.header.fragmentIndex = fragmentIndex;
            datagram.header.fragmentCount = fragmentCount;
            datagram.header.payloadBytes = static_cast<uint16_t>(count);
            datagram.header.ptsUs = ptsUs;
            datagram.header.captureTimeUs = captureTimeUs;
            datagram.header.deadlineUs = deadlineUs;
            datagram.header.sendTimeUs = options.nowUs;

            uint32_t flags = SwVtpFlag_Important;
            if (packet.isDiscontinuity()) {
                flags |= SwVtpFlag_Discontinuity;
            }
            if (fragmentIndex == 0U) {
                flags |= SwVtpFlag_FirstFragment;
            }
            if (fragmentIndex + 1U == fragmentCount) {
                flags |= SwVtpFlag_LastFragment;
            }
            datagram.header.flags = flags;

            result.serializedDatagrams.append(swVtpSerializeDatagram(datagram));
            result.datagrams.append(datagram);
        }

        result.ok = !result.datagrams.isEmpty();
        return result;
    }
};

class SwVtpKlvReassembler {
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
        SwMediaPacket packet{};

        bool completed() const { return status == PushStatus::Completed; }
    };

    struct Snapshot {
        std::size_t bufferedSamples{0};
        std::size_t bufferedBytes{0};
        uint64_t acceptedFragments{0};
        uint64_t duplicateFragments{0};
        uint64_t completedSamples{0};
        uint64_t staleFragments{0};
        uint64_t droppedSamples{0};
    };

    void setTrackId(const SwString& trackId) {
        m_trackId = trackId;
    }

    void setLimits(std::size_t maxBufferedSamples, std::size_t maxSampleBytes) {
        m_maxBufferedSamples = maxBufferedSamples == 0U ? 1U : maxBufferedSamples;
        m_maxSampleBytes = maxSampleBytes == 0U ? 1U : maxSampleBytes;
    }

    void reset() {
        m_samples.clear();
        m_bufferedBytes = 0;
        m_acceptedFragments = 0;
        m_duplicateFragments = 0;
        m_completedSamples = 0;
        m_staleFragments = 0;
        m_droppedSamples = 0;
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
        if (header.messageType != SwVtpMessageType::KlvFragment ||
            header.trackType != SwVtpTrackType::MetadataKlv ||
            header.codec != SwVtpCodec::Klv ||
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
        SampleKey key;
        key.streamId = header.streamId;
        key.trackId = header.trackId;
        key.sampleId = header.frameId;

        SampleState& state = m_samples[key];
        if (!state.initialized) {
            state.initialized = true;
            state.firstHeader = header;
            state.deadlineUs = header.deadlineUs;
            state.fragments.resize(header.fragmentCount);
            state.received.assign(header.fragmentCount, false);
        }

        if (state.fragments.size() != header.fragmentCount) {
            dropSample_(key);
            return result;
        }
        if (state.received[header.fragmentIndex]) {
            ++m_duplicateFragments;
            result.status = PushStatus::Duplicate;
            return result;
        }

        const std::size_t incomingBytes = static_cast<std::size_t>(datagram.payload.size());
        if (state.bytes + incomingBytes > m_maxSampleBytes) {
            dropSample_(key);
            return result;
        }

        state.fragments[header.fragmentIndex] = datagram.payload;
        state.received[header.fragmentIndex] = true;
        state.bytes += incomingBytes;
        state.receivedCount += 1U;
        m_bufferedBytes += incomingBytes;
        ++m_acceptedFragments;

        if (m_samples.size() > m_maxBufferedSamples) {
            dropOldest_();
        }

        if (state.receivedCount != state.fragments.size()) {
            result.status = PushStatus::Accepted;
            return result;
        }

        SwByteArray payload;
        payload.reserve(state.bytes);
        for (std::size_t i = 0; i < state.fragments.size(); ++i) {
            payload.append(state.fragments[i]);
        }

        SwMediaPacket packet;
        packet.setType(SwMediaPacket::Type::Metadata);
        packet.setTrackId(m_trackId.isEmpty() ? SwString("klv") : m_trackId);
        packet.setCodec("klv");
        packet.setClockRate(90000);
        packet.setPayload(std::move(payload));
        packet.setPts(static_cast<std::int64_t>(state.firstHeader.ptsUs));
        packet.setDts(static_cast<std::int64_t>(state.firstHeader.ptsUs));
        packet.setDiscontinuity(state.firstHeader.hasFlag(SwVtpFlag_Discontinuity));

        m_bufferedBytes -= state.bytes;
        m_samples.erase(key);
        ++m_completedSamples;
        result.status = PushStatus::Completed;
        result.packet = std::move(packet);
        return result;
    }

    Snapshot snapshot() const {
        Snapshot snapshot;
        snapshot.bufferedSamples = m_samples.size();
        snapshot.bufferedBytes = m_bufferedBytes;
        snapshot.acceptedFragments = m_acceptedFragments;
        snapshot.duplicateFragments = m_duplicateFragments;
        snapshot.completedSamples = m_completedSamples;
        snapshot.staleFragments = m_staleFragments;
        snapshot.droppedSamples = m_droppedSamples;
        return snapshot;
    }

private:
    struct SampleKey {
        uint16_t streamId{0};
        uint16_t trackId{0};
        uint32_t sampleId{0};

        bool operator<(const SampleKey& other) const {
            if (streamId != other.streamId) {
                return streamId < other.streamId;
            }
            if (trackId != other.trackId) {
                return trackId < other.trackId;
            }
            return sampleId < other.sampleId;
        }
    };

    struct SampleState {
        bool initialized{false};
        SwVtpHeader firstHeader{};
        uint64_t deadlineUs{0};
        std::vector<SwByteArray> fragments{};
        std::vector<bool> received{};
        std::size_t bytes{0};
        std::size_t receivedCount{0};
    };

    void expire(uint64_t nowUs) {
        std::vector<SampleKey> expired;
        for (typename std::map<SampleKey, SampleState>::const_iterator it = m_samples.begin();
             it != m_samples.end();
             ++it) {
            if (it->second.deadlineUs != 0U && nowUs > it->second.deadlineUs) {
                expired.push_back(it->first);
            }
        }
        for (std::size_t i = 0; i < expired.size(); ++i) {
            dropSample_(expired[i]);
        }
    }

    void dropOldest_() {
        if (!m_samples.empty()) {
            dropSample_(m_samples.begin()->first);
        }
    }

    void dropSample_(const SampleKey& key) {
        typename std::map<SampleKey, SampleState>::iterator it = m_samples.find(key);
        if (it == m_samples.end()) {
            return;
        }
        m_bufferedBytes -= std::min(m_bufferedBytes, it->second.bytes);
        m_samples.erase(it);
        ++m_droppedSamples;
    }

    std::map<SampleKey, SampleState> m_samples{};
    std::size_t m_bufferedBytes{0};
    std::size_t m_maxBufferedSamples{32};
    std::size_t m_maxSampleBytes{1024 * 1024};
    uint64_t m_acceptedFragments{0};
    uint64_t m_duplicateFragments{0};
    uint64_t m_completedSamples{0};
    uint64_t m_staleFragments{0};
    uint64_t m_droppedSamples{0};
    SwString m_trackId{"klv"};
};
