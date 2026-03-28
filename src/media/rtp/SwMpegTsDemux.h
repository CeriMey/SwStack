#pragma once

/**
 * @file src/media/rtp/SwMpegTsDemux.h
 * @ingroup media
 * @brief Declares a lightweight MPEG-TS video demux helper for UDP/RTP video sources.
 */

#include "media/SwVideoPacket.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

class SwMpegTsDemux {
public:
    using PacketCallback = std::function<void(const SwVideoPacket&)>;

    void setPacketCallback(PacketCallback callback) { m_packetCallback = std::move(callback); }

    void reset() {
        m_patParsed = false;
        m_pmtParsed = false;
        m_pmtPids.clear();
        m_videoPid = 0;
        m_tsBuffer.clear();
        m_pesBuffer.clear();
        m_pesKey = false;
        m_pesPts = 0;
        m_hasPesPts = false;
        m_hevcStream = false;
    }

    bool isHevc() const { return m_hevcStream; }

    void feed(const uint8_t* data, size_t size, uint32_t timestampBase = 0) {
        if (!data || size == 0) {
            return;
        }
        m_tsBuffer.insert(m_tsBuffer.end(), data, data + size);
        while (m_tsBuffer.size() >= 188) {
            std::vector<uint8_t> packet(m_tsBuffer.begin(), m_tsBuffer.begin() + 188);
            m_tsBuffer.erase(m_tsBuffer.begin(), m_tsBuffer.begin() + 188);
            if (packet[0] != 0x47) {
                continue;
            }

            const bool payloadStart = (packet[1] & 0x40) != 0;
            const uint16_t pid = static_cast<uint16_t>(((packet[1] & 0x1F) << 8) | packet[2]);
            const uint8_t afc = static_cast<uint8_t>((packet[3] >> 4) & 0x3);
            size_t offset = 4;
            if (afc & 0x2) {
                if (offset >= packet.size()) {
                    continue;
                }
                offset += 1 + packet[offset];
            }
            if (!(afc & 0x1) || offset >= packet.size()) {
                continue;
            }

            const uint8_t* payload = packet.data() + offset;
            const size_t payloadSize = packet.size() - offset;
            if (pid == 0) {
                parsePAT_(payload, payloadSize, payloadStart);
                continue;
            }
            for (uint16_t pmtPid : m_pmtPids) {
                if (pid == pmtPid) {
                    parsePMT_(payload, payloadSize, payloadStart);
                    break;
                }
            }
            if (m_videoPid != 0 && pid == m_videoPid) {
                handlePES_(payload, payloadSize, payloadStart, timestampBase);
            }
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
                const uint8_t nalType = static_cast<uint8_t>((data[headerIndex] >> 1) & 0x3F);
                if (nalType >= 16 && nalType <= 21) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    void emitPesFrame_(uint32_t fallbackTimestamp) {
        if (m_pesBuffer.empty() || !m_packetCallback) {
            return;
        }
        const SwVideoPacket::Codec codec =
            m_hevcStream ? SwVideoPacket::Codec::H265 : SwVideoPacket::Codec::H264;
        const uint32_t timestamp = m_hasPesPts
                                       ? static_cast<uint32_t>(m_pesPts & 0xFFFFFFFFu)
                                       : fallbackTimestamp;
        SwVideoPacket packet(codec,
                             SwByteArray(reinterpret_cast<const char*>(m_pesBuffer.data()),
                                         m_pesBuffer.size()),
                             static_cast<std::int64_t>(timestamp),
                             static_cast<std::int64_t>(timestamp),
                             m_pesKey);
        m_packetCallback(packet);
    }

    void parsePAT_(const uint8_t* data, size_t size, bool payloadStart) {
        if (!payloadStart || size < 8 || m_patParsed) {
            return;
        }
        const size_t index = static_cast<size_t>(data[0]) + 1;
        if (index + 8 > size || data[index] != 0x00) {
            return;
        }
        const size_t sectionLength = ((data[index + 1] & 0x0F) << 8) | data[index + 2];
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
        if (std::find(m_pmtPids.begin(), m_pmtPids.end(), programMapPid) == m_pmtPids.end()) {
            m_pmtPids.push_back(programMapPid);
        }
        m_patParsed = true;
    }

    void parsePMT_(const uint8_t* data, size_t size, bool payloadStart) {
        if (!payloadStart || size < 12) {
            return;
        }
        if (m_pmtParsed && m_videoPid != 0) {
            return;
        }
        const size_t index = static_cast<size_t>(data[0]) + 1;
        if (index + 12 > size || data[index] != 0x02) {
            return;
        }
        const size_t sectionLength = ((data[index + 1] & 0x0F) << 8) | data[index + 2];
        const size_t end = index + 3 + sectionLength;
        if (end > size) {
            return;
        }
        const size_t programInfoLength = ((data[index + 10] & 0x0F) << 8) | data[index + 11];
        size_t pos = index + 12 + programInfoLength;
        while (pos + 5 <= end) {
            const uint8_t streamType = data[pos];
            const uint16_t elementaryPid =
                static_cast<uint16_t>(((data[pos + 1] & 0x1F) << 8) | data[pos + 2]);
            const uint16_t esInfoLength =
                static_cast<uint16_t>(((data[pos + 3] & 0x0F) << 8) | data[pos + 4]);
            if (streamType == 0x1B) {
                m_videoPid = elementaryPid;
                m_hevcStream = false;
                m_pmtParsed = true;
                return;
            }
            if (streamType == 0x24) {
                m_videoPid = elementaryPid;
                m_hevcStream = true;
                m_pmtParsed = true;
                return;
            }
            pos += 5 + esInfoLength;
        }
    }

    static bool parsePts_(const uint8_t* data, size_t size, uint64_t& ptsOut) {
        if (size < 14) {
            return false;
        }
        const uint8_t flags = data[7];
        const uint8_t headerLength = data[8];
        if (!(flags & 0x80) || headerLength < 5 || size < 9 + headerLength) {
            return false;
        }
        const uint8_t* p = data + 9;
        uint64_t pts = 0;
        pts |= (static_cast<uint64_t>((p[0] >> 1) & 0x07) << 30);
        pts |= (static_cast<uint64_t>(p[1]) << 22) |
               (static_cast<uint64_t>((p[2] >> 1) & 0x7F) << 15);
        pts |= (static_cast<uint64_t>(p[3]) << 7) |
               (static_cast<uint64_t>((p[4] >> 1) & 0x7F));
        ptsOut = pts;
        return true;
    }

    void handlePES_(const uint8_t* data, size_t size, bool payloadStart, uint32_t fallbackTimestamp) {
        if (payloadStart) {
            if (!m_pesBuffer.empty()) {
                emitPesFrame_(fallbackTimestamp);
                m_pesBuffer.clear();
                m_pesKey = false;
                m_hasPesPts = false;
            }
            if (size < 9 || !(data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)) {
                return;
            }
            const uint8_t streamId = data[3];
            if ((streamId & 0xF0) != 0xE0 && streamId != 0x1B) {
                return;
            }
            const size_t headerLength = static_cast<size_t>(data[8]);
            const size_t payloadOffset = 9 + headerLength;
            if (payloadOffset > size) {
                return;
            }
            uint64_t pts = 0;
            if (parsePts_(data, size, pts)) {
                m_pesPts = pts;
                m_hasPesPts = true;
            }
            m_pesBuffer.insert(m_pesBuffer.end(), data + payloadOffset, data + size);
        } else {
            m_pesBuffer.insert(m_pesBuffer.end(), data, data + size);
        }
        if (m_hevcStream ? hasStartCodeHevcIdr(m_pesBuffer)
                         : hasStartCodeH264Idr(m_pesBuffer)) {
            m_pesKey = true;
        }
    }

    PacketCallback m_packetCallback{};
    bool m_patParsed{false};
    bool m_pmtParsed{false};
    std::vector<uint16_t> m_pmtPids{};
    uint16_t m_videoPid{0};
    std::vector<uint8_t> m_tsBuffer{};
    std::vector<uint8_t> m_pesBuffer{};
    bool m_pesKey{false};
    uint64_t m_pesPts{0};
    bool m_hasPesPts{false};
    bool m_hevcStream{false};
};
