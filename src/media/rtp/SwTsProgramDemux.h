#pragma once

/**
 * @file src/media/rtp/SwTsProgramDemux.h
 * @ingroup media
 * @brief Declares a MPEG-TS program demux that emits generic media packets.
 */

#include "media/SwMediaPacket.h"
#include "media/SwMediaTrack.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

class SwTsProgramDemux {
public:
    using PacketCallback = std::function<void(const SwMediaPacket&)>;
    using TracksChangedCallback = std::function<void(const SwList<SwMediaTrack>&)>;

    void setPacketCallback(PacketCallback callback) { m_packetCallback = std::move(callback); }
    void setTracksChangedCallback(TracksChangedCallback callback) {
        m_tracksChangedCallback = std::move(callback);
        publishTracks_();
    }

    void reset() {
        m_patParsed = false;
        m_pmtPids.clear();
        m_tsBuffer.clear();
        m_streams.clear();
        m_publishedTracks.clear();
    }

    void feed(const uint8_t* data, size_t size, uint32_t timestampBase = 0) {
        if (!data || size == 0) {
            return;
        }
        m_tsBuffer.insert(m_tsBuffer.end(), data, data + size);
        while (m_tsBuffer.size() >= 188) {
            std::vector<uint8_t> packet(m_tsBuffer.begin(), m_tsBuffer.begin() + 188);
            m_tsBuffer.erase(m_tsBuffer.begin(), m_tsBuffer.begin() + 188);
            if (packet.empty() || packet[0] != 0x47) {
                continue;
            }

            const bool payloadStart = (packet[1] & 0x40) != 0;
            const uint16_t pid =
                static_cast<uint16_t>(((packet[1] & 0x1F) << 8) | packet[2]);
            const uint8_t adaptationFieldControl =
                static_cast<uint8_t>((packet[3] >> 4) & 0x3);
            size_t offset = 4;
            if (adaptationFieldControl & 0x2) {
                if (offset >= packet.size()) {
                    continue;
                }
                offset += 1 + packet[offset];
            }
            if (!(adaptationFieldControl & 0x1) || offset >= packet.size()) {
                continue;
            }

            const uint8_t* payload = packet.data() + offset;
            const size_t payloadSize = packet.size() - offset;
            if (pid == 0) {
                parsePAT_(payload, payloadSize, payloadStart);
                continue;
            }
            if (isPmtPid_(pid)) {
                parsePMT_(payload, payloadSize, payloadStart);
                continue;
            }
            auto streamIt = m_streams.find(pid);
            if (streamIt == m_streams.end()) {
                continue;
            }
            handlePES_(streamIt->second, payload, payloadSize, payloadStart, timestampBase);
        }
    }

    static bool hasStartCodeH264Idr(const std::vector<uint8_t>& data) {
        for (size_t i = 0; i + 4 < data.size(); ++i) {
            if (data[i] == 0x00 && data[i + 1] == 0x00 &&
                ((data[i + 2] == 0x00 && data[i + 3] == 0x01) || data[i + 2] == 0x01)) {
                const size_t headerIndex = (data[i + 2] == 0x01) ? (i + 3) : (i + 4);
                if (headerIndex < data.size() && (data[headerIndex] & 0x1F) == 5) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool hasStartCodeHevcIdr(const std::vector<uint8_t>& data) {
        for (size_t i = 0; i + 5 < data.size(); ++i) {
            if (data[i] == 0x00 && data[i + 1] == 0x00 &&
                ((data[i + 2] == 0x00 && data[i + 3] == 0x01) || data[i + 2] == 0x01)) {
                const size_t headerIndex = (data[i + 2] == 0x01) ? (i + 3) : (i + 4);
                if (headerIndex >= data.size()) {
                    continue;
                }
                const uint8_t nalType =
                    static_cast<uint8_t>((data[headerIndex] >> 1) & 0x3F);
                if (nalType >= 16 && nalType <= 21) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    enum class StreamKind {
        Unknown,
        Video,
        Audio,
        Metadata
    };

    struct StreamInfo {
        uint16_t pid{0};
        StreamKind kind{StreamKind::Unknown};
        SwString trackId{};
        SwString codec{};
        int streamType{0};
        int clockRate{0};
        int sampleRate{0};
        int channelCount{0};
        bool hevc{false};
        bool keyFrame{false};
        bool hasPesPts{false};
        std::uint64_t pesPts{0};
        std::vector<uint8_t> buffer{};
    };

    static bool parsePts_(const uint8_t* data, size_t size, std::uint64_t& ptsOut) {
        if (size < 14) {
            return false;
        }
        const uint8_t flags = data[7];
        const uint8_t headerLength = data[8];
        if (!(flags & 0x80) || headerLength < 5 || size < 9 + headerLength) {
            return false;
        }
        const uint8_t* p = data + 9;
        std::uint64_t pts = 0;
        pts |= (static_cast<std::uint64_t>((p[0] >> 1) & 0x07) << 30);
        pts |= (static_cast<std::uint64_t>(p[1]) << 22) |
               (static_cast<std::uint64_t>((p[2] >> 1) & 0x7F) << 15);
        pts |= (static_cast<std::uint64_t>(p[3]) << 7) |
               (static_cast<std::uint64_t>((p[4] >> 1) & 0x7F));
        ptsOut = pts;
        return true;
    }

    static std::int64_t rescaleTimestamp_(std::uint64_t value,
                                          std::int64_t sourceClock,
                                          std::int64_t targetClock) {
        if (sourceClock <= 0 || targetClock <= 0) {
            return static_cast<std::int64_t>(value);
        }
        return static_cast<std::int64_t>(
            (value * static_cast<std::uint64_t>(targetClock)) /
            static_cast<std::uint64_t>(sourceClock));
    }

    static int defaultSampleRate_(const SwString& codec) {
        if (codec == "opus") {
            return 48000;
        }
        if (codec == "aac") {
            return 48000;
        }
        return 0;
    }

    static int defaultChannelCount_(const SwString& codec) {
        if (codec == "opus") {
            return 2;
        }
        if (codec == "aac") {
            return 2;
        }
        return 0;
    }

    bool isPmtPid_(uint16_t pid) const {
        return std::find(m_pmtPids.begin(), m_pmtPids.end(), pid) != m_pmtPids.end();
    }

    void parsePAT_(const uint8_t* data, size_t size, bool payloadStart) {
        if (!payloadStart || size < 8 || m_patParsed) {
            return;
        }
        const size_t index = static_cast<size_t>(data[0]) + 1;
        if (index + 8 > size || data[index] != 0x00) {
            return;
        }
        const size_t sectionLength =
            ((data[index + 1] & 0x0F) << 8) | data[index + 2];
        const size_t end = index + 3 + sectionLength;
        if (end > size) {
            return;
        }
        const size_t pos = index + 8;
        if (pos + 4 > end) {
            return;
        }
        const uint16_t programMapPid =
            static_cast<uint16_t>(((data[pos + 2] & 0x1F) << 8) | data[pos + 3]);
        if (!isPmtPid_(programMapPid)) {
            m_pmtPids.push_back(programMapPid);
        }
        m_patParsed = true;
    }

    void parsePMT_(const uint8_t* data, size_t size, bool payloadStart) {
        if (!payloadStart || size < 12) {
            return;
        }
        const size_t index = static_cast<size_t>(data[0]) + 1;
        if (index + 12 > size || data[index] != 0x02) {
            return;
        }
        const size_t sectionLength =
            ((data[index + 1] & 0x0F) << 8) | data[index + 2];
        const size_t end = index + 3 + sectionLength;
        if (end > size) {
            return;
        }
        const size_t programInfoLength =
            ((data[index + 10] & 0x0F) << 8) | data[index + 11];
        size_t pos = index + 12 + programInfoLength;
        bool changed = false;
        while (pos + 5 <= end) {
            const uint8_t streamType = data[pos];
            const uint16_t elementaryPid =
                static_cast<uint16_t>(((data[pos + 1] & 0x1F) << 8) | data[pos + 2]);
            const uint16_t esInfoLength =
                static_cast<uint16_t>(((data[pos + 3] & 0x0F) << 8) | data[pos + 4]);
            const uint8_t* descriptorData = data + pos + 5;
            StreamInfo recognized;
            if (recognizeStream_(streamType,
                                 descriptorData,
                                 esInfoLength,
                                 elementaryPid,
                                 recognized)) {
                changed = upsertStream_(recognized) || changed;
            }
            pos += 5 + esInfoLength;
        }
        if (changed) {
            publishTracks_();
        }
    }

    bool recognizeStream_(uint8_t streamType,
                          const uint8_t* descriptorData,
                          size_t descriptorSize,
                          uint16_t pid,
                          StreamInfo& outStream) const {
        outStream.pid = pid;
        outStream.streamType = streamType;
        switch (streamType) {
        case 0x1B:
            outStream.kind = StreamKind::Video;
            outStream.codec = "h264";
            outStream.trackId = SwString("ts-video-") + SwString(std::to_string(pid));
            outStream.clockRate = 90000;
            outStream.hevc = false;
            return true;
        case 0x24:
            outStream.kind = StreamKind::Video;
            outStream.codec = "h265";
            outStream.trackId = SwString("ts-video-") + SwString(std::to_string(pid));
            outStream.clockRate = 90000;
            outStream.hevc = true;
            return true;
        case 0x0F:
        case 0x11:
            outStream.kind = StreamKind::Audio;
            outStream.codec = "aac";
            outStream.trackId = SwString("ts-audio-") + SwString(std::to_string(pid));
            outStream.sampleRate = 48000;
            outStream.channelCount = 2;
            outStream.clockRate = outStream.sampleRate;
            return true;
        case 0x06:
            return parsePrivateDataDescriptors_(descriptorData,
                                                descriptorSize,
                                                pid,
                                                outStream);
        default:
            return false;
        }
    }

    bool parsePrivateDataDescriptors_(const uint8_t* data,
                                      size_t size,
                                      uint16_t pid,
                                      StreamInfo& outStream) const {
        size_t offset = 0;
        while (offset + 2 <= size) {
            const uint8_t tag = data[offset];
            const uint8_t length = data[offset + 1];
            offset += 2;
            if (offset + length > size) {
                break;
            }
            if (tag == 0x05 && length >= 4) {
                const std::string registration(reinterpret_cast<const char*>(data + offset), 4);
                if (registration == "KLVA") {
                    outStream.pid = pid;
                    outStream.kind = StreamKind::Metadata;
                    outStream.codec = "smpte336m";
                    outStream.trackId =
                        SwString("ts-metadata-") + SwString(std::to_string(pid));
                    outStream.clockRate = 90000;
                    return true;
                }
                if (registration == "Opus") {
                    outStream.pid = pid;
                    outStream.kind = StreamKind::Audio;
                    outStream.codec = "opus";
                    outStream.trackId =
                        SwString("ts-audio-") + SwString(std::to_string(pid));
                    outStream.sampleRate = 48000;
                    outStream.channelCount = 2;
                    outStream.clockRate = 48000;
                    return true;
                }
            }
            offset += length;
        }
        return false;
    }

    bool upsertStream_(const StreamInfo& stream) {
        auto it = m_streams.find(stream.pid);
        if (it == m_streams.end()) {
            m_streams.emplace(stream.pid, stream);
            return true;
        }
        StreamInfo& current = it->second;
        const bool changed =
            current.kind != stream.kind ||
            current.codec != stream.codec ||
            current.clockRate != stream.clockRate ||
            current.sampleRate != stream.sampleRate ||
            current.channelCount != stream.channelCount ||
            current.hevc != stream.hevc ||
            current.trackId != stream.trackId;
        current.kind = stream.kind;
        current.codec = stream.codec;
        current.trackId = stream.trackId;
        current.streamType = stream.streamType;
        current.clockRate = stream.clockRate;
        current.sampleRate = stream.sampleRate;
        current.channelCount = stream.channelCount;
        current.hevc = stream.hevc;
        return changed;
    }

    void publishTracks_() {
        SwList<SwMediaTrack> tracks;
        for (const auto& entry : m_streams) {
            const StreamInfo& stream = entry.second;
            if (stream.kind == StreamKind::Unknown) {
                continue;
            }
            SwMediaTrack track;
            track.id = stream.trackId;
            track.codec = stream.codec;
            track.clockRate = stream.kind == StreamKind::Audio
                                  ? (stream.sampleRate > 0 ? stream.sampleRate : stream.clockRate)
                                  : stream.clockRate;
            track.sampleRate = stream.sampleRate;
            track.channelCount = stream.channelCount;
            track.selected = false;
            track.availability = SwMediaTrack::Availability::Available;
            switch (stream.kind) {
            case StreamKind::Video:
                track.type = SwMediaTrack::Type::Video;
                break;
            case StreamKind::Audio:
                track.type = SwMediaTrack::Type::Audio;
                break;
            case StreamKind::Metadata:
                track.type = SwMediaTrack::Type::Metadata;
                break;
            case StreamKind::Unknown:
            default:
                track.type = SwMediaTrack::Type::Unknown;
                break;
            }
            tracks.append(track);
        }
        m_publishedTracks = tracks;
        if (m_tracksChangedCallback) {
            m_tracksChangedCallback(m_publishedTracks);
        }
    }

    void updateAudioFormatFromPayload_(StreamInfo& stream) {
        if (stream.kind != StreamKind::Audio || stream.codec != "aac") {
            return;
        }
        if (stream.buffer.size() < 7) {
            return;
        }
        const uint8_t* data = stream.buffer.data();
        if (data[0] != 0xFF || (data[1] & 0xF0) != 0xF0) {
            return;
        }
        static const std::array<int, 16> kAacSampleRates = {
            96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
            16000, 12000, 11025, 8000, 7350, 0, 0, 0
        };
        const int sampleRateIndex = static_cast<int>((data[2] >> 2) & 0x0F);
        const int sampleRate =
            (sampleRateIndex >= 0 && sampleRateIndex < static_cast<int>(kAacSampleRates.size()))
                ? kAacSampleRates[static_cast<size_t>(sampleRateIndex)]
                : 0;
        const int channelCount =
            static_cast<int>(((data[2] & 0x01) << 2) | ((data[3] >> 6) & 0x03));
        bool changed = false;
        if (sampleRate > 0 && stream.sampleRate != sampleRate) {
            stream.sampleRate = sampleRate;
            stream.clockRate = sampleRate;
            changed = true;
        }
        if (channelCount > 0 && stream.channelCount != channelCount) {
            stream.channelCount = channelCount;
            changed = true;
        }
        if (changed) {
            publishTracks_();
        }
    }

    void handlePES_(StreamInfo& stream,
                    const uint8_t* data,
                    size_t size,
                    bool payloadStart,
                    uint32_t fallbackTimestamp) {
        if (!data || size == 0) {
            return;
        }
        if (payloadStart) {
            if (!stream.buffer.empty()) {
                emitPesPacket_(stream, fallbackTimestamp);
                stream.buffer.clear();
                stream.keyFrame = false;
                stream.hasPesPts = false;
            }
            if (size < 6 ||
                !(data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)) {
                return;
            }
            const size_t headerLength = (size > 8) ? static_cast<size_t>(data[8]) : 0U;
            const size_t payloadOffset = 9U + headerLength;
            if (payloadOffset > size) {
                return;
            }
            std::uint64_t parsedPts = 0;
            if (parsePts_(data, size, parsedPts)) {
                stream.pesPts = parsedPts;
                stream.hasPesPts = true;
            }
            stream.buffer.insert(stream.buffer.end(),
                                 data + payloadOffset,
                                 data + size);
        } else {
            stream.buffer.insert(stream.buffer.end(), data, data + size);
        }

        if (stream.kind == StreamKind::Video) {
            stream.keyFrame = stream.hevc ? hasStartCodeHevcIdr(stream.buffer)
                                          : hasStartCodeH264Idr(stream.buffer);
        } else if (stream.kind == StreamKind::Audio) {
            updateAudioFormatFromPayload_(stream);
        }
    }

    void emitPesPacket_(StreamInfo& stream, uint32_t fallbackTimestamp) {
        if (stream.buffer.empty() || !m_packetCallback) {
            return;
        }

        SwMediaPacket packet;
        packet.setTrackId(stream.trackId);
        packet.setCodec(stream.codec);
        packet.setPayload(SwByteArray(reinterpret_cast<const char*>(stream.buffer.data()),
                                      static_cast<int>(stream.buffer.size())));
        const std::uint64_t rawPts = stream.hasPesPts
                                         ? stream.pesPts
                                         : static_cast<std::uint64_t>(fallbackTimestamp);
        switch (stream.kind) {
        case StreamKind::Video:
            packet.setType(SwMediaPacket::Type::Video);
            packet.setPts(static_cast<std::int64_t>(rawPts));
            packet.setDts(static_cast<std::int64_t>(rawPts));
            packet.setKeyFrame(stream.keyFrame);
            packet.setClockRate(90000);
            break;
        case StreamKind::Audio: {
            const int sampleRate =
                stream.sampleRate > 0 ? stream.sampleRate : defaultSampleRate_(stream.codec);
            const int channelCount =
                stream.channelCount > 0 ? stream.channelCount : defaultChannelCount_(stream.codec);
            packet.setType(SwMediaPacket::Type::Audio);
            packet.setSampleRate(sampleRate);
            packet.setChannelCount(channelCount);
            packet.setClockRate(sampleRate > 0 ? sampleRate : 90000);
            const std::int64_t pts =
                rescaleTimestamp_(rawPts, 90000, packet.clockRate());
            packet.setPts(pts);
            packet.setDts(pts);
            break;
        }
        case StreamKind::Metadata:
            packet.setType(SwMediaPacket::Type::Metadata);
            packet.setClockRate(90000);
            packet.setPts(static_cast<std::int64_t>(rawPts));
            packet.setDts(static_cast<std::int64_t>(rawPts));
            break;
        case StreamKind::Unknown:
        default:
            return;
        }
        m_packetCallback(packet);
    }

    PacketCallback m_packetCallback{};
    TracksChangedCallback m_tracksChangedCallback{};
    bool m_patParsed{false};
    std::vector<uint16_t> m_pmtPids{};
    std::vector<uint8_t> m_tsBuffer{};
    std::map<uint16_t, StreamInfo> m_streams{};
    SwList<SwMediaTrack> m_publishedTracks{};
};
