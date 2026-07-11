#pragma once

/**
 * @file src/media/rtp/SwTsMuxer.h
 * @brief Minimal single-program MPEG-TS muxer for H264/H265 elementary streams.
 *
 * The counterpart of `SwTsProgramDemux`: packs Annex-B access units into 188-byte TS
 * packets (PAT + PMT with valid MPEG CRC32, PES with PTS, PCR on the video PID,
 * adaptation-field stuffing, per-PID continuity counters). Output is standards-compliant
 * so third-party receivers (ffplay, VLC, hardware decoders) can consume it — this is what
 * the SRT server transport sends.
 *
 * Dependency-free and platform-neutral, in line with the demuxer.
 */

#include "core/types/SwByteArray.h"

#include <cstdint>
#include <cstring>
#include <vector>

class SwTsMuxer {
public:
    static const std::uint16_t kPmtPid = 0x0100;
    static const std::uint16_t kVideoPid = 0x0101;

    enum class VideoCodec {
        H264,
        H265
    };

    void setVideoCodec(VideoCodec codec) { m_codec = codec; }
    VideoCodec videoCodec() const { return m_codec; }

    void reset() {
        std::memset(m_continuity, 0, sizeof(m_continuity));
        m_tablesEmitted = false;
    }

    /**
     * @brief Muxes one Annex-B access unit into TS packets appended to `out`.
     *
     * PAT/PMT are (re)emitted before the first access unit and before every key frame,
     * and the key-frame packet carries a PCR so receivers can lock their clock.
     *
     * @param annexB   Annex-B elementary-stream bytes (start-code prefixed NAL units).
     * @param ptsMs    Presentation timestamp in milliseconds (converted to 90 kHz).
     * @param keyFrame Whether the access unit starts with a random-access point.
     */
    void muxAccessUnit(const SwByteArray& annexB,
                       std::int64_t ptsMs,
                       bool keyFrame,
                       std::vector<char>& out) {
        if (annexB.isEmpty()) {
            return;
        }
        if (!m_tablesEmitted || keyFrame) {
            appendPat_(out);
            appendPmt_(out);
            m_tablesEmitted = true;
        }

        const std::uint64_t pts90k =
            ptsMs > 0 ? static_cast<std::uint64_t>(ptsMs) * 90ULL : 0ULL;

        // PES header: start code + stream id, unbounded length (standard for video),
        // flags with PTS, 5-byte PTS field.
        std::vector<std::uint8_t> pes;
        pes.reserve(14 + static_cast<std::size_t>(annexB.size()));
        pes.push_back(0x00);
        pes.push_back(0x00);
        pes.push_back(0x01);
        pes.push_back(0xE0);
        pes.push_back(0x00);
        pes.push_back(0x00);
        pes.push_back(0x80);
        pes.push_back(0x80); // PTS only
        pes.push_back(0x05); // PES header data length
        pes.push_back(static_cast<std::uint8_t>(0x21 | (((pts90k >> 30) & 0x07) << 1)));
        pes.push_back(static_cast<std::uint8_t>((pts90k >> 22) & 0xFF));
        pes.push_back(static_cast<std::uint8_t>(0x01 | (((pts90k >> 15) & 0x7F) << 1)));
        pes.push_back(static_cast<std::uint8_t>((pts90k >> 7) & 0xFF));
        pes.push_back(static_cast<std::uint8_t>(0x01 | ((pts90k & 0x7F) << 1)));
        pes.insert(pes.end(),
                   reinterpret_cast<const std::uint8_t*>(annexB.constData()),
                   reinterpret_cast<const std::uint8_t*>(annexB.constData()) + annexB.size());

        // Split the PES across TS packets; the first one carries PUSI (+ PCR on keys).
        std::size_t offset = 0;
        bool first = true;
        while (offset < pes.size()) {
            appendVideoTsPacket_(pes, offset, first, first && keyFrame, pts90k, out);
            first = false;
        }
    }

private:
    // MPEG-2 CRC32 (poly 0x04C11DB7, init 0xFFFFFFFF, no reflection, no final xor).
    static std::uint32_t crc32_(const std::uint8_t* data, std::size_t size) {
        std::uint32_t crc = 0xFFFFFFFFU;
        for (std::size_t i = 0; i < size; ++i) {
            crc ^= static_cast<std::uint32_t>(data[i]) << 24;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x80000000U) ? (crc << 1) ^ 0x04C11DB7U : (crc << 1);
            }
        }
        return crc;
    }

    std::uint8_t nextContinuity_(std::uint16_t pid) {
        const std::uint8_t value = m_continuity[pid & 0x1FFF];
        m_continuity[pid & 0x1FFF] = static_cast<std::uint8_t>((value + 1) & 0x0F);
        return value;
    }

    void appendSection_(std::uint16_t pid,
                        const std::vector<std::uint8_t>& section,
                        std::vector<char>& out) {
        // One TS packet: header, pointer_field, section, 0xFF padding to 188 bytes.
        std::uint8_t packet[188];
        std::memset(packet, 0xFF, sizeof(packet));
        packet[0] = 0x47;
        packet[1] = static_cast<std::uint8_t>(0x40 | ((pid >> 8) & 0x1F));
        packet[2] = static_cast<std::uint8_t>(pid & 0xFF);
        packet[3] = static_cast<std::uint8_t>(0x10 | nextContinuity_(pid));
        packet[4] = 0x00; // pointer_field
        std::memcpy(packet + 5, section.data(), section.size());
        out.insert(out.end(),
                   reinterpret_cast<const char*>(packet),
                   reinterpret_cast<const char*>(packet) + sizeof(packet));
    }

    void finishSection_(std::vector<std::uint8_t>& section) {
        // Backpatch section_length (from right after the length field to CRC included),
        // then append the CRC32 over table_id..end-of-section-data.
        const std::size_t lengthValue = section.size() - 3 + 4;
        section[1] = static_cast<std::uint8_t>(0xB0 | ((lengthValue >> 8) & 0x0F));
        section[2] = static_cast<std::uint8_t>(lengthValue & 0xFF);
        const std::uint32_t crc = crc32_(section.data(), section.size());
        section.push_back(static_cast<std::uint8_t>((crc >> 24) & 0xFF));
        section.push_back(static_cast<std::uint8_t>((crc >> 16) & 0xFF));
        section.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
        section.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    }

    void appendPat_(std::vector<char>& out) {
        std::vector<std::uint8_t> section;
        section.push_back(0x00); // table_id PAT
        section.push_back(0x00); // section_length (backpatched)
        section.push_back(0x00);
        section.push_back(0x00);
        section.push_back(0x01); // transport_stream_id
        section.push_back(0xC1); // version 0, current_next 1
        section.push_back(0x00); // section_number
        section.push_back(0x00); // last_section_number
        section.push_back(0x00);
        section.push_back(0x01); // program_number 1
        section.push_back(static_cast<std::uint8_t>(0xE0 | ((kPmtPid >> 8) & 0x1F)));
        section.push_back(static_cast<std::uint8_t>(kPmtPid & 0xFF));
        finishSection_(section);
        appendSection_(0x0000, section, out);
    }

    void appendPmt_(std::vector<char>& out) {
        std::vector<std::uint8_t> section;
        section.push_back(0x02); // table_id PMT
        section.push_back(0x00); // section_length (backpatched)
        section.push_back(0x00);
        section.push_back(0x00);
        section.push_back(0x01); // program_number
        section.push_back(0xC1);
        section.push_back(0x00);
        section.push_back(0x00);
        section.push_back(static_cast<std::uint8_t>(0xE0 | ((kVideoPid >> 8) & 0x1F)));
        section.push_back(static_cast<std::uint8_t>(kVideoPid & 0xFF)); // PCR PID
        section.push_back(0xF0);
        section.push_back(0x00); // program_info_length 0
        section.push_back(m_codec == VideoCodec::H265 ? 0x24 : 0x1B); // stream_type
        section.push_back(static_cast<std::uint8_t>(0xE0 | ((kVideoPid >> 8) & 0x1F)));
        section.push_back(static_cast<std::uint8_t>(kVideoPid & 0xFF));
        section.push_back(0xF0);
        section.push_back(0x00); // es_info_length 0
        finishSection_(section);
        appendSection_(kPmtPid, section, out);
    }

    void appendVideoTsPacket_(const std::vector<std::uint8_t>& pes,
                              std::size_t& offset,
                              bool payloadStart,
                              bool withPcr,
                              std::uint64_t pts90k,
                              std::vector<char>& out) {
        std::uint8_t packet[188];
        std::size_t headerSize = 4;
        packet[0] = 0x47;
        packet[1] = static_cast<std::uint8_t>((payloadStart ? 0x40 : 0x00) |
                                              ((kVideoPid >> 8) & 0x1F));
        packet[2] = static_cast<std::uint8_t>(kVideoPid & 0xFF);

        const std::size_t remaining = pes.size() - offset;
        std::size_t adaptationSize = 0;
        if (withPcr) {
            adaptationSize = 8; // length + flags + 6-byte PCR
        }
        std::size_t capacity = sizeof(packet) - headerSize - adaptationSize;
        if (remaining < capacity) {
            // Stuff via the adaptation field so no padding leaks into the ES.
            adaptationSize += (adaptationSize == 0)
                                  ? (capacity - remaining >= 2 ? capacity - remaining : 1)
                                  : (capacity - remaining);
            capacity = sizeof(packet) - headerSize - adaptationSize;
        }

        if (adaptationSize > 0) {
            packet[3] = static_cast<std::uint8_t>(0x30 | nextContinuity_(kVideoPid));
            packet[4] = static_cast<std::uint8_t>(adaptationSize - 1);
            if (adaptationSize >= 2) {
                std::uint8_t flags = 0x00;
                std::size_t pos = 6;
                if (withPcr) {
                    flags |= 0x10; // PCR flag
                }
                if (payloadStart && withPcr) {
                    flags |= 0x40; // random access indicator
                }
                packet[5] = flags;
                if (withPcr) {
                    // PCR slightly behind the PTS (~100 ms) keeps PTS > PCR for decoders.
                    const std::uint64_t pcrBase = pts90k > 9000ULL ? pts90k - 9000ULL : 0ULL;
                    packet[6] = static_cast<std::uint8_t>((pcrBase >> 25) & 0xFF);
                    packet[7] = static_cast<std::uint8_t>((pcrBase >> 17) & 0xFF);
                    packet[8] = static_cast<std::uint8_t>((pcrBase >> 9) & 0xFF);
                    packet[9] = static_cast<std::uint8_t>((pcrBase >> 1) & 0xFF);
                    packet[10] = static_cast<std::uint8_t>(((pcrBase & 0x1) << 7) | 0x7E);
                    packet[11] = 0x00; // PCR extension
                    pos = 12;
                }
                for (std::size_t i = pos; i < headerSize + adaptationSize; ++i) {
                    packet[i] = 0xFF; // stuffing
                }
            }
        } else {
            packet[3] = static_cast<std::uint8_t>(0x10 | nextContinuity_(kVideoPid));
        }

        const std::size_t payloadBytes = remaining < capacity ? remaining : capacity;
        std::memcpy(packet + headerSize + adaptationSize, pes.data() + offset, payloadBytes);
        offset += payloadBytes;
        out.insert(out.end(),
                   reinterpret_cast<const char*>(packet),
                   reinterpret_cast<const char*>(packet) + sizeof(packet));
    }

    VideoCodec m_codec{VideoCodec::H264};
    std::uint8_t m_continuity[0x2000] = {0};
    bool m_tablesEmitted{false};
};
